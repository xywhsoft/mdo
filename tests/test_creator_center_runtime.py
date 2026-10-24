"""Real mdo -> website -> review -> mdo install -> safe native executable.

Only disposable loopback hosts, accounts and fixture C source are used.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
from test_api_runtime import free_port
from test_cli_tools_runtime import stage_forwarder

ROOT = Path(__file__).resolve().parents[1]
HOME = ROOT.parent / "home"
sys.path.insert(0, str(HOME / "tests"))
from test_mdo_delivery import fixture, request, EXE


def run(host):
    base, site, port, admin_user, admin_password = fixture("mdo-creator-native")
    origin = f"http://127.0.0.1:{port}"
    (site / "db/identity.json").write_text(json.dumps({"public_origin": origin}))
    app = base / "native-app"
    home = base / "native-home"
    if app.exists(): shutil.rmtree(app)
    if home.exists(): shutil.rmtree(home)
    shutil.copytree(ROOT / "app", app)
    home.mkdir()
    stage_forwarder(app)
    unity = app / "generated/mdo_unity.c"
    unity.write_text(f'#define MDO_ACCOUNT_SERVICE_ORIGIN "{origin}"\n#define MDO_DISTRIBUTION_ORIGIN "{origin}"\n#define MDO_DISTRIBUTION_HOST "127.0.0.1"\n#define MDO_DISTRIBUTION_PORT {port}\n#define MDO_DISTRIBUTION_SECURE false\n' + unity.read_text(encoding="utf-8"), encoding="utf-8")
    app_port = free_port()
    config = dict(services=[dict(class_="http", name="creator-client", enabled=True, ip="127.0.0.1", port=app_port,
                                host_default=dict(name="creator-client", enabled=True, path="web", devlang="c", devfile="generated/mdo_unity.c"))])
    config["services"][0]["recv_limit"] = 100*1048576+131072
    config["services"][0]["body_limit"] = 100*1048576+16384
    config["services"][0]["class"] = config["services"][0].pop("class_")
    (app / "xs.json").write_text(json.dumps(config))
    logs = []; running = []
    def start(exe, cfg, cwd, local_home=None):
        log = (base / ("client.log" if local_home else "website.log")).open("wb"); logs.append(log)
        env = dict(os.environ, USE_WEBVIEW="0")
        if local_home: env["MDO_HOME"] = str(local_home)
        proc = subprocess.Popen([str(exe), str(cfg)], cwd=cwd, env=env, stdout=log, stderr=log,
                                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0)); running.append(proc)
        return proc
    def ready(proc, p, route):
        for _ in range(200):
            if proc.poll() is not None: raise AssertionError("Native fixture exited: " + str(base))
            try:
                if request(p, "GET", route)[0] == 200: return
            except OSError: pass
            time.sleep(.1)
        raise AssertionError("Native fixture not ready: " + str(base))
    def web(method, path, body=None, headers=None, cookie=None, status=200):
        code, head, data = request(port, method, path, body, cookie, headers)
        assert code == status, (path, code, data[:1000])
        return json.loads(data).get("data"), head
    def client(method, path, body=None, status=200):
        headers = {}
        if method != "GET": headers["X-Mdo-Write-Token"] = request(app_port, "GET", "/api/v1/account")[1]["X-Mdo-Write-Token"]
        code, _, data = request(app_port, method, "/api/v1" + path, body, headers=headers)
        assert code == status, (path, code, data[:1000])
        return json.loads(data)["data"]
    def task():
        for _ in range(300):
            data = client("GET", "/cli-tools")
            row = data["tasks"][-1]
            if row["state"] not in ("queued", "running", "uploading", "installing"):
                assert row["state"] == "complete", row
                return data, row
            time.sleep(.1)
        raise AssertionError("Creator task did not terminate")
    try:
        server = start(EXE, site / "xs.json", HOME); ready(server, port, "/api/v1/mdo/cli/catalog")
        web("POST", "/api/v1/register", dict(username="creator_native", password="Fixture-2026-password"), status=201)
        login, _ = web("POST", "/api/v1/login", dict(identifier="creator_native", password="Fixture-2026-password"))
        auth = {"Authorization": "Bearer " + login["access_token"]}
        _, head = web("POST", "/admin/login", dict(username=admin_user, password=admin_password))
        cookie = "; ".join(v.split(";", 1)[0] for v in head["_cookies"])
        inventory, _ = web("GET", "/admin/api/mdo/cli/manage", cookie=cookie)
        csrf = {"X-CSRF-Token": inventory["csrf"]}
        native = start(host.resolve(), app / "xs.json", app, home); ready(native, app_port, "/api/v1/account")
        client("POST", "/account/login", dict(identifier="creator_native", password="Fixture-2026-password", remember=False))
        for _ in range(100):
            account = client("GET", "/account")
            if account["state"] == "signed_in" and not account["busy"]: break
            time.sleep(.1)
        assert account["state"] == "signed_in", account
        source = base / "creator-fixture.c"
        source.write_text('#include <stdio.h>\nint main(void){puts("CREATOR_ROUND_TRIP_OK");return 0;}\n')
        program = base / "creator-fixture.exe"
        subprocess.run([r"E:\software\w64devkit\bin\gcc.exe", "-Os", "-s", str(source), "-o", str(program)], check=True)
        snapshot = client("GET", "/cli-tools")
        tool = dict(id="creator-fixture", name="Creator Fixture", version="1.0.0", platform="windows-x86_64", path=str(program),
                    description="USER_FACING_CREATOR_DESCRIPTION",prompt="Print a safe fixture marker.", license="MIT", links=[], enabled=True)
        client("POST", "/cli-tools", dict(action="save", revision=snapshot.get("revision",0), tool=tool))
        client("POST", "/cli-tools", dict(action="publish", id=tool["id"], name=tool["name"], publish=True,
                                           review_file=str(source), review_notes="Build with gcc. Prints a marker; no files or network access."))
        _, completed = task(); submitted = completed["submission"]
        assert submitted["state"] == "pending"
        catalog, _ = web("GET", "/api/v1/mdo/cli/catalog"); assert not catalog["items"]
        detail, _ = web("GET", "/admin/api/mdo/creator/detail?source=cli&id=" + str(submitted["id"]), cookie=cookie)
        assert detail["description"]==tool["description"] and detail["prompt"]==tool["prompt"]
        assert detail["evidence_hash"] == hashlib.sha256(source.read_bytes()).hexdigest()
        web("POST", "/admin/api/mdo/cli/manage", dict(action="approve", id=submitted["id"], revision=1, reason="Fixture source and behavior verified"), headers=csrf, cookie=cookie)
        client("POST", "/cli-tools", dict(action="enable", id=tool["id"], enabled=False))
        client("POST", "/cli-tools", dict(action="install", store_id=submitted["id"], name=tool["name"]))
        snapshot, _ = task()
        installed = next(t for t in snapshot["items"] if t.get("store_id") == submitted["id"])
        assert installed["description"]==tool["description"] and installed["prompt"]==tool["prompt"]
        installed_path = Path(installed["path"])
        assert hashlib.sha256(installed_path.read_bytes()).digest() == hashlib.sha256(program.read_bytes()).digest()
        result = subprocess.run([str(installed_path)], capture_output=True, timeout=5, check=True)
        assert result.stdout.strip() == b"CREATOR_ROUND_TRIP_OK"
        # Large portable source goes through native upload chunks and bounded
        # authenticated download chunks rather than the SDK's 64 MiB fetch cap.
        import base64
        large = {"format":"mdo.extension.v2", "manifest":dict(slug="creator-large",name="Creator Large",version="1.0.0",description="Large source fixture",readme="Not executed",license="MIT",platforms=["windows-x86_64"],min_build_id=50000009), "resources":[dict(kind="plugins",id="creator-large",content="{}",files=[dict(path="web/source.txt",base64=base64.b64encode(b"x"*(5*1048576)).decode())])]}
        submitted_source = client("POST", "/ecosystem", dict(action="submit",package=large,publish=True,review_notes="Source file only, never executed; no network or privileges."), status=201)
        web("POST", "/admin/api/mdo/ecosystem", dict(action="approve", id=submitted_source["id"], revision=1, reason="Large source verified"), headers=csrf, cookie=cookie)
        source_detail = client("POST", "/ecosystem", dict(action="detail",id=submitted_source["id"]))
        assert source_detail["package"] == large
        client("POST", "/ecosystem", dict(action="install",id=submitted_source["id"],trust_code=True))
        web("POST", "/api/v1/mdo/creator/work", dict(action="withdraw", work=submitted["work"], revision=detail["work_revision"]), headers=auth)
        code, _, _ = request(port, "GET", "/api/v1/mdo/cli/blob?hash=" + detail["sha256"]); assert code == 404
        assert source.read_text().startswith('#include')
        print("PASS real mdo sign-in, explicit C evidence upload, immutable review, public download, native install, exact bytes, safe execution and unpublication")
    finally:
        for proc in reversed(running):
            proc.terminate(); proc.wait(15)
        for log in logs: log.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(); parser.add_argument("--host", type=Path, default=ROOT / ".build/host/xs.exe")
    run(parser.parse_args().host)
