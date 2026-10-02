#!/usr/bin/env python3
"""Bounded real HTTP/TLS backup download, cancellation and unload probe.

One 2 MiB artifact exceeds the default TCP send queue; this is a normal file
transfer, not a concurrency/load test. Fault controls only patch copied sources.
No live model, shell, user Home, stress or high-load tests are involved.
"""
from __future__ import annotations

import argparse
import base64
from contextlib import closing
import hashlib
import http.client
import json
import os
from pathlib import Path
import shutil
import socket
import ssl
import subprocess
import tempfile
import time

from test_api_runtime import ROOT, free_port

CONTROL = "/__fixture/backup-download/"
ARTIFACT = "artifacts/run-00000000000000000001/00000000000000000001-probe.txt"


def replace(site: Path, name: str, old: str, new: str) -> None:
    file = site / name
    text = file.read_text(encoding="utf-8")
    assert text.count(old) == 1, (name, old)
    file.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")


class Probe:
    def __init__(self, host: Path, base: Path, secure: bool):
        self.host, self.base, self.secure = host, base, secure
        self.site, self.home = base / "site", base / "home"
        self.port = free_port()
        self.process = None
        self.log = None
        shutil.copytree(ROOT / "app", self.site)
        shutil.copy2(ROOT / "tests/fixtures/backup-download.c", self.site / "src/bootstrap/backup-download.c")
        replace(self.site, "src/bootstrap/service.c", "void ServiceInit(XS_HostInfo* pHost)",
                '#include "backup-download.c"\nvoid ServiceInit(XS_HostInfo* pHost)')
        replace(self.site, "src/bootstrap/service.c", "    (void)MdoApiInit();",
                "    (void)MdoApiInit();\n    BackupDownloadFixtureInit();")
        replace(self.site, "src/bootstrap/service.c", "    MdoBootstrapUnit();",
                "    BackupDownloadFixtureUnit();\n    MdoBootstrapUnit();")
        replace(self.site, "src/bootstrap/service.c", "    return MdoApiRequest(pRequest);",
                "    if ( BackupDownloadFixtureControl(pRequest) ) return XS_OK;\n"
                "    return MdoApiRequest(pRequest);")
        replace(self.site, "src/api/downloads.c", "#define MDO_DOWNLOAD_CHUNK 16384u",
                "uint64 BackupDownloadFixtureTimeout(void);\n"
                "bool BackupDownloadFixtureLowFiles(void);\n"
                "bool BackupDownloadFixturePause(unsigned, MdoApiContext*);\n"
                "#define MDO_DOWNLOAD_CHUNK 16384u")
        replace(self.site, "src/api/downloads.c", "    Job->Context.SendCancel = Cancel;",
                "    Job->Context.SendCancel = Cancel;\n"
                "    if (!BackupDownloadFixturePause(1u, &Job->Context)) return XTASK_CANCELLED;")
        replace(self.site, "src/api/downloads.c", "    while ( Offset < Bytes ) {",
                "    if (!BackupDownloadFixturePause(2u, Context)) return false;\n"
                "    while ( Offset < Bytes ) {")
        replace(self.site, "src/api/downloads.c", "xrtDeadlineAfter(MDO_DOWNLOAD_TIMEOUT_US)",
                "xrtDeadlineAfter(BackupDownloadFixtureTimeout())")
        replace(self.site, "src/api/downloads.c", "    Limits.Deadline = Job->Context.SendDeadline;",
                "    Limits.Deadline = Job->Context.SendDeadline;\n"
                "    if (BackupDownloadFixtureLowFiles()) Limits.Files = 1u;")
        host_config = {"enabled": True, "name": "mdo", "path": "web", "devlang": "c",
                       "devfile": "generated/mdo_unity.c"}
        if secure:
            for kind in ("cert", "key"):
                shutil.copy2(ROOT / f"tests/fixtures/proxy-localhost-{kind}.pem", self.site / f"fixture-{kind}.pem")
            host_config.update(tls_cert="fixture-cert.pem", tls_key="fixture-key.pem")
        self.config = self.site / "xs.json"
        plain_port = free_port() if secure else self.port
        while secure and plain_port == self.port:
            plain_port = free_port()
        self.config.write_text(json.dumps({"engine": {"workers": 1}, "services": [{
            "enabled": True, "class": "http", "tls": secure, "name": "backup-probe",
            "ip": "127.0.0.1", "ip_tls": "127.0.0.1", "port": plain_port,
            "port_tls": self.port if secure else 0, "host_default": host_config,
        }]}), encoding="utf-8")

    def connection(self, small_window=False):
        if self.secure:
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
            context.check_hostname = False
            context.verify_mode = ssl.CERT_NONE  # loopback fixture certificate only
            connection = http.client.HTTPSConnection("127.0.0.1", self.port, timeout=4, context=context)
        else:
            connection = http.client.HTTPConnection("127.0.0.1", self.port, timeout=4)
        connection.connect()
        if small_window:
            connection.sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
        return connection

    def call(self, method, path, body=None, headers=None):
        headers = dict(headers or {})
        if method not in ("GET", "HEAD", "OPTIONS") and path.startswith("/api/v1/"):
            headers["X-Mdo-Write-Token"] = self.call("GET", "/api/v1/bootstrap")[1]["x-mdo-write-token"]
        data = json.dumps(body).encode() if body is not None else None
        if data is not None:
            headers["Content-Type"] = "application/json"
        with closing(self.connection()) as conn:
            conn.request(method, path, body=data, headers=headers)
            response = conn.getresponse()
            return response.status, {k.lower(): v for k, v in response.getheaders()}, response.read()

    def control(self, name):
        status, _, body = self.call("GET", CONTROL + name)
        assert status == 200, (status, body)
        return json.loads(body)["data"]

    def wait(self, predicate):
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            value = self.control("state")
            if predicate(value):
                return value
            time.sleep(0.01)
        raise AssertionError(("fixture did not reach state", value))

    def start(self):
        self.log = (self.base / "xs.log").open("wb")
        self.process = subprocess.Popen([str(self.host), str(self.config)], cwd=self.site,
            env=dict(os.environ, MDO_HOME=str(self.home),
                     MDO_LING_RESPONSES_URL="https://example.invalid/v1", MDO_LING_API_KEY="backup-fixture"),
            stdout=self.log, stderr=subprocess.STDOUT,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                raise AssertionError((self.process.returncode, (self.base / "xs.log").read_text(errors="replace")))
            try:
                status, _, body = self.call("GET", "/api/v1/bootstrap")
                if status == 200 and json.loads(body)["data"]["ready"]:
                    return
            except (OSError, ValueError):
                pass
            time.sleep(0.05)
        raise AssertionError((self.base / "xs.log").read_text(errors="replace"))

    def stop(self):
        if self.process and self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=3)
        if self.log:
            self.log.close()

    def exercise(self):
        ids = ["a" * 32, "b" * 32]
        paths = []
        for identifier in ids:
            status, _, body = self.call("POST", "/api/v1/sessions", {
                "project_id": "default", "client_session_id": identifier, "title": "Backup fixture"})
            assert status == 201, (status, body)
            paths.append(f"/api/v1/projects/default/sessions/{identifier}")
        folder = self.home / "sessions/default" / ids[0]
        artifact = folder / ARTIFACT
        artifact.parent.mkdir(parents=True)
        payload = (b"bounded normal artifact\x00\xff\n" * 100000)[:2 * 1024 * 1024]
        assert len(payload) == 2 * 1024 * 1024
        artifact.write_bytes(payload)
        self.control("hold-send")
        held = self.connection()
        held.request("GET", paths[0] + "/backup")
        state = self.wait(lambda v: v["entered"] == 2)
        assert state["busy"] and state["storage_free"], state
        # All capture/handle locks must have been released before transport.
        status, headers, body = self.call("GET", paths[0])
        current = json.loads(body)["data"]
        assert status == 200, (status, body)
        status, _, body = self.call("PATCH", paths[0], {"title": "Changed during download"},
                                    {"If-Match": headers["etag"]})
        assert status == 200, (status, body)
        status, _, body = self.call("GET", paths[1] + "/backup")
        assert status == 503 and json.loads(body)["error"]["code"] == "session_backup_busy", (status, body)
        self.control("resume")
        response = held.getresponse()
        headers = {k.lower(): v for k, v in response.getheaders()}
        data = response.read()
        held.close()
        assert response.status == 200 and len(data) > 1024 * 1024, (
            response.status, len(data), headers, data[:1024])
        assert headers["content-length"] == str(len(data)) and headers["connection"] == "close"
        assert headers["content-type"] == "application/json; charset=utf-8"
        assert headers["content-disposition"] == f'attachment; filename="mdo-session-{ids[0]}.backup.json"'
        assert headers["etag"] == f'"mdo-backup-sha256-{hashlib.sha256(data).hexdigest()}"'
        backup = json.loads(data)
        assert backup["restore_ready"] is False and backup["revision"] == current["revision"]
        files = {}
        for item in backup["files"]:
            content = base64.b64decode(item["data"], validate=True)
            assert len(content) == item["bytes"] and hashlib.sha256(content).hexdigest() == item["sha256"]
            files[item["path"]] = content
        assert files[ARTIFACT] == payload
        assert json.loads(files["meta.json"])["title"] == "Backup fixture"
        self.wait(lambda v: not v["busy"])
        # HEAD has full bounded content length and no body, then closes cleanly.
        status, headers, body = self.call("HEAD", paths[0] + "/backup")
        assert status == 200 and body == b"" and int(headers["content-length"]) > 1024 * 1024
        self.wait(lambda v: not v["busy"])
        # Force ordinary socket backpressure on this one transfer, then cancel
        # the real transport wait (not just the fixture's pause Future).
        self.control("hold-send")
        held = self.connection(small_window=True)
        held.request("GET", paths[0] + "/backup")
        self.wait(lambda v: v["entered"] == 2)
        assert self.control("small-socket")["small_socket"]
        self.control("resume")
        self.wait(lambda v: v["busy"] and v["pending"] > 0)
        state = self.wait(lambda v: v["busy"] and v["pending"] > 0)
        assert state["storage_free"]
        start = time.monotonic()
        assert not self.control("unit")["busy"]
        assert time.monotonic() - start < 3
        try:
            response = held.getresponse()
            partial_body = response.read()
            assert len(partial_body) < int(response.getheader("Content-Length"))
        except (http.client.RemoteDisconnected, http.client.IncompleteRead,
                ConnectionResetError, ssl.SSLError):
            pass
        finally:
            held.close()
        self.wait(lambda v: not v["busy"])
        # Deliberately hold before capture; client close cannot strand the slot.
        self.control("hold-capture")
        held = self.connection()
        held.request("GET", paths[0] + "/backup")
        self.wait(lambda v: v["entered"] == 1)
        held.close()
        self.control("resume")
        self.wait(lambda v: not v["busy"])
        # The deadline closes an unfinished download without a successful body.
        self.control("hold-send")
        self.control("short")
        held = self.connection()
        held.request("GET", paths[0] + "/backup")
        self.wait(lambda v: v["entered"] == 2)
        try:
            held.getresponse()
            raise AssertionError("timed-out transfer reported a response")
        except (http.client.RemoteDisconnected, ConnectionResetError):
            pass
        finally:
            held.close()
        self.wait(lambda v: not v["busy"])
        self.control("resume")
        # Unit actually executes on xs's sole network worker. It must cancel
        # and join without waiting for that worker to complete transport work.
        self.control("hold-send")
        held = self.connection()
        held.request("GET", paths[0] + "/backup")
        self.wait(lambda v: v["entered"] == 2)
        start = time.monotonic()
        assert not self.control("unit")["busy"]
        assert time.monotonic() - start < 3
        held.close()
        status, _, body = self.call("GET", paths[1] + "/backup")
        assert status == 200 and json.loads(body)["session_id"] == ids[1], (status, body[:1024])
        self.wait(lambda v: not v["busy"])
        assert self.process.poll() is None
        assert self.call("GET", "/api/v1/bootstrap")[0] == 200
        assert self.call("GET", paths[0] + "/export")[0] == 200
        assert self.call("GET", paths[0] + "/backup", {"unexpected": True})[0] == 400
        self.wait(lambda v: not v["busy"])
        unknown = folder / "unknown.txt"
        unknown.write_bytes(b"unclassified content must not silently disappear")
        status, _, body = self.call("GET", paths[0] + "/backup")
        assert status == 422 and json.loads(body)["error"]["code"] == "session_backup_invalid", (status, body)
        unknown.unlink()
        self.wait(lambda v: not v["busy"])
        self.control("file-limit")
        status, _, body = self.call("GET", paths[0] + "/backup")
        assert status == 413 and json.loads(body)["error"]["code"] == "session_backup_limit", (status, body)
        self.wait(lambda v: not v["busy"])
        status, _, body = self.call("GET", paths[0] + "/backup")
        assert status == 200 and json.loads(body)["session_id"] == ids[0]
        self.wait(lambda v: not v["busy"])
        print(f"backup {'TLS' if self.secure else 'HTTP'}: exact bytes/hash, released capture, admission, HEAD, real backpressure cancel, peer close, deadline, network-worker Unit, invalid/budget/retry PASS")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, default=ROOT / ".build/host" / ("xs.exe" if os.name == "nt" else "xs"))
    args = parser.parse_args()
    (ROOT / ".build").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="backup-download-", dir=ROOT / ".build") as raw:
        for secure in (False, True):
            base = Path(raw) / ("tls" if secure else "http")
            probe = Probe(args.host.resolve(), base, secure)
            try:
                probe.start()
                probe.exercise()
            except BaseException:
                print((base / "xs.log").read_text(errors="replace"))
                raise
            finally:
                probe.stop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
