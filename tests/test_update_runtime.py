"""Bounded updater integration through real xs, TCC and HTTP. No load tests."""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from test_api_runtime import free_port, request, wait_ready

ROOT = Path(__file__).resolve().parents[1]
SOURCE = b"MZ" + b"old package" * 20 + b"XRTPEND\0" + bytes(24)
NEXT = b"MZ" + b"new package" * 20 + b"XRTPEND\0" + bytes(24)


class Published(BaseHTTPRequestHandler):
    package = SOURCE
    mode = "normal"
    required = None
    def log_message(self, *args): pass
    def do_GET(self):
        digest = hashlib.sha256(type(self).package).hexdigest()
        url = "/update/download/windows-x86_64/" + digest
        if self.path.startswith("/update/version?"):
            if self.mode == "offline":
                self.send_response(503); self.send_header("Content-Length", "0"); self.end_headers(); return
            if self.mode == "missing":
                self.send_response(404); self.send_header("Content-Length", "0"); self.end_headers(); return
            metadata = dict(platform="windows-x86_64", sha256=digest,
                                   size=len(self.package), notes="Test update",
                                   url="https://invalid.example/package" if self.mode == "external" else url)
            if self.required is not None: metadata["required"] = self.required
            body = json.dumps(metadata).encode()
        elif self.path == url:
            body = bytes(len(self.package)) if self.mode == "damaged" else self.package
            if self.mode == "slow": time.sleep(2)
        else:
            self.send_error(404); return
        self.send_response(200); self.send_header("Content-Length", str(len(body))); self.end_headers()
        try: self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError): pass


def run(host):
    publisher = ThreadingHTTPServer(("127.0.0.1", 0), Published)
    threading.Thread(target=publisher.serve_forever, daemon=True).start()
    with tempfile.TemporaryDirectory(prefix="update-runtime-", dir=ROOT / ".build") as raw:
        base = Path(raw); site = base / "site"; home = base / "mdo-home"
        shutil.copytree(ROOT / "app", site)
        (site / "fixture.exe").write_bytes(SOURCE)
        port = free_port()
        (site / "probe.c").write_text(
            '#define MDO_DISTRIBUTION_HOST \"127.0.0.1\"\n#define MDO_DISTRIBUTION_SECURE false\n#define MDO_DISTRIBUTION_PORT ' + str(publisher.server_port) + '\n'
            '#define MDO_UPDATE_ORIGIN "http://127.0.0.1:' + str(publisher.server_port) + '"\n'
            '#define RequestProc ProductRequestProc\n#define ServiceInit ProductServiceInit\n#include "generated/mdo_unity.c"\n#undef ServiceInit\n#undef RequestProc\n'
            'void ServiceInit(XS_HostInfo* host) {\n ProductServiceInit(host); MdoUpdateUnit(); MdoUpdateSetEngine(host->Server->Engine);\n'
            'g_MdoUpdate.Lock=xrtMutexCreate(); g_MdoUpdate.Source=xrtStrDup("' + (site / "fixture.exe").as_posix() + '");\n'
            'g_MdoUpdate.Status.Enabled=true; strcpy(g_MdoUpdate.Status.Platform,"windows-x86_64"); strcpy(g_MdoUpdate.Status.Edition,"desktop");\n'
            'MdoUpdatePolicyLoad(&g_MdoUpdate.Status);\n'
            'g_MdoUpdate.Thread=xrtThreadCreate(MdoUpdateThread,NULL,0); MdoUpdateCheck();\n}\n'
            'XS_RequestResult RequestProc(XS_HttpReq* req) {\n'
            'if(MdoApiViewEqualText(req->head->Target,"/api/v1/update?fixture=periodic")){\n'
            'xrtMutexLock(g_MdoUpdate.Lock);g_MdoUpdate.NextCheck=xrtDeadlineAfter(0);xrtMutexUnlock(g_MdoUpdate.Lock);}\n'
            'if (MdoApiViewEqualText(req->head->Target,"/api/v1/update?fixture=pause") ||\n'
            '    MdoApiViewEqualText(req->head->Target,"/api/v1/update?fixture=resume")) {\n'
            ' xrtMutexLock(g_MdoUpdate.Lock); g_MdoUpdate.Status.Installing=\n'
            ' MdoApiViewEqualText(req->head->Target,"/api/v1/update?fixture=pause");\n'
            ' xrtMutexUnlock(g_MdoUpdate.Lock); } return ProductRequestProc(req); }\n',
            encoding="utf-8")
        config = dict(engine=dict(workers=2), services=[dict(enabled=True, name="update-probe",
                      ip="127.0.0.1", port=port, **{"class": "http"},
                      host_default=dict(enabled=True, name="mdo", path="web", devlang="c", devfile="probe.c"))])
        path = site / "xs.json"; path.write_text(json.dumps(config))
        log_path = base / "host.log"
        with log_path.open("wb") as log:
            proc = subprocess.Popen([str(host), str(path), "--", "--home", str(home)], cwd=site,
                                    stdout=log, stderr=log, creationflags=subprocess.CREATE_NO_WINDOW)
            def state():
                st, _, body = request(port, "GET", "/api/v1/update")
                assert st == 200, (st, body)
                return json.loads(body)["data"]
            def settled():
                for _ in range(160):
                    value = state()
                    if not value["busy"]: return value
                    time.sleep(.05)
                raise AssertionError("Updater failed to settle")
            def post(endpoint):
                st, _, body = request(port, "POST", "/api/v1/update" + endpoint, body=b"{}",
                                      headers={"Content-Type": "application/json"})
                assert st in (200,202), (st,body)
            try:
                wait_ready(port, proc)
                value = settled()
                assert value["status"] == "current" and value["local_sha256"] == hashlib.sha256(SOURCE).hexdigest(), value
                assert value["edition"] == "desktop", value
                assert not home.exists(), "startup checks must not create Home"
                print("PASS matching hash; startup is read only")
                Published.package = NEXT
                post(""); assert settled()["status"] == "available"
                Published.mode = "external"
                post(""); assert settled()["status"] == "error"
                print("PASS different hash and external URL rejection")
                Published.mode = "normal"
                post(""); assert settled()["status"] == "available"
                Published.mode = "damaged"
                post("/download"); value = settled()
                assert not value["ready"] and not (home / "data/update/new.exe").exists(), value
                print("PASS checksum mismatch leaves running package and cache unchanged")
                Published.mode = "normal"
                post("/download"); value = settled()
                assert value["ready"] and (home / "data/update/new.exe").read_bytes() == NEXT, value
                print("PASS verified, atomically saved package")
                post("/install"); value = settled()
                assert value["status"] == "ready" and proc.poll() is None
                assert not (home / "data/update/install.go").exists()
                print("PASS remote/headless request cannot stop host without native confirmation")
                Published.package = NEXT[:2] + b"different" + NEXT[2:]
                post(""); settled()
                Published.mode = "slow"; post("/download")
                st, _, _ = request(port, "DELETE", "/api/v1/update/download")
                assert st == 200 and not settled()["busy"]
                assert request(port, "GET", "/api/v1/bootstrap")[0] == 200
                print("PASS cancellation leaves host responsive")
                Published.mode = "missing"
                post(""); assert settled()["status"] == "no-package"
                print("PASS unpublished platform has no update prompt")
                Published.mode = "normal"; Published.required = True
                post(""); value = settled()
                assert value["blocked"] and value["required"] and value["available"], value
                cache = home / "data/update/policy.json"
                assert json.loads(cache.read_text())["required"] is True
                st, _, body = request(port,"POST","/api/v1/projects",body=b"{}")
                assert st == 409 and json.loads(body)["error"]["code"] == "update_required", (st, body)
                assert request(port,"GET","/api/v1/bootstrap")[0] == 200
                print("PASS mandatory policy persists and backend blocks mutations")
                for mode, required in (("external", True), ("normal", "true"), ("missing", True), ("offline", True)):
                    Published.mode = mode; Published.required = required
                    post(""); value = settled()
                    assert value["blocked"] and value["required"] and value["status"] == "error", value
                    assert json.loads(cache.read_text())["required"] is True
                print("PASS invalid metadata, 404 and offline cannot revoke mandatory policy")
                Published.mode = "damaged"; Published.required = True
                post("/download"); assert settled()["blocked"]
                Published.mode = "slow"; post("/download")
                assert request(port,"DELETE","/api/v1/update/download")[0] == 200
                assert settled()["blocked"]
                Published.mode = "normal"; post("/download")
                assert settled()["ready"]
                post("/install"); value = settled()
                assert value["blocked"] and value["ready"] and proc.poll() is None
                print("PASS failed/cancelled download and missing native confirmation retain lock")
                Published.mode = "offline"
                proc.terminate(); proc.wait(timeout=5)
                proc = subprocess.Popen([str(host), str(path), "--", "--home", str(home)], cwd=site,
                                        stdout=log, stderr=log, creationflags=subprocess.CREATE_NO_WINDOW)
                wait_ready(port,proc)
                value = settled(); assert value["blocked"] and value["ready"], value
                print("PASS cached mandatory policy and verified package survive offline restart")
                Published.mode = "normal"; Published.required = False
                post(""); assert not settled()["blocked"]
                assert json.loads(cache.read_text())["required"] is False
                print("PASS valid policy revocation releases lock without replacing package")
                Published.package = SOURCE; Published.required = True
                post(""); value = settled()
                assert value["required"] and not value["blocked"] and not value["available"], value
                print("PASS matching running hash is exempt from mandatory lock")
                assert request(port,"GET","/api/v1/update?fixture=pause")[0] == 200
                st, _, body = request(port,"POST","/api/v1/update",body=b"{}")
                assert st == 409 and json.loads(body)["error"]["code"] == "update_installing", (st,body)
                assert request(port,"GET","/api/v1/bootstrap")[0] == 200
                assert request(port,"GET","/api/v1/update?fixture=resume")[0] == 200
                post(""); settled()
                print("PASS installation pauses mutations while reads and resume remain available")
                Published.package = NEXT; Published.required = True
                assert request(port,"GET","/api/v1/update?fixture=periodic")[0] == 200
                for _ in range(40):
                    if state()["blocked"]: break
                    time.sleep(.1)
                assert settled()["blocked"]
                print("PASS worker deadline triggers remote check without frontend polling POST")
            except BaseException:
                log.flush(); print(log_path.read_text(errors="replace")[-5000:]); raise
            finally:
                proc.terminate()
                try: proc.wait(timeout=5)
                except subprocess.TimeoutExpired: proc.kill(); proc.wait(timeout=5)
    publisher.shutdown(); publisher.server_close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", type=Path, default=ROOT / ".build/host/xs.exe")
    run(parser.parse_args().host.resolve())
