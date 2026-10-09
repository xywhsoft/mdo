"""Real packed Windows service CLI update through HTTP, with an isolated Home.

No SCM registration or elevation. Dedicated SCM restart additionally requires
an administrator test environment; this test covers the complete CLI path.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit
from manual_linux_products_qa import request
from test_api_runtime import free_port

ROOT = Path(__file__).resolve().parents[1]


def run():
    if os.name != "nt":
        print("SKIP Windows packed server updater")
        return
    data = (ROOT / "mdo-server.exe").read_bytes()
    sha = hashlib.sha256(data).hexdigest()
    build = json.loads((ROOT / "app/release.json").read_text())["windows_server_build_id"]
    queries = []

    class Publisher(BaseHTTPRequestHandler):
        edition = "desktop"  # First response deliberately targets the wrong product.
        def log_message(self, *args):
            pass
        def do_GET(self):
            if self.path.startswith("/update/version?"):
                queries.append(parse_qs(urlsplit(self.path).query))
                body = json.dumps(dict(platform="windows-x86_64", edition=self.edition,
                    build_id=build, sha256=sha, size=len(data), notes="Fixture", required=False,
                    url="/mdo/blob/" + sha)).encode()
            elif self.path == "/mdo/blob/" + sha:
                body = data
            elif self.path.startswith("/mdo/catalog"):
                body = b'{"revision":1,"notices":[],"toolpacks":[]}'
            else:
                self.send_error(404)
                return
            self.send_response(200)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    publisher = ThreadingHTTPServer(("127.0.0.1", 0), Publisher)
    threading.Thread(target=publisher.serve_forever, daemon=True).start()
    with tempfile.TemporaryDirectory(prefix="server-update-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        package = base / "package"
        shutil.copytree(ROOT / ".build/packages/windows-server-glibc", package)
        unity = package / "generated/mdo_unity.c"
        text = unity.read_text(encoding="utf-8").replace(f"MDO_BUILD_ID {build}u", "MDO_BUILD_ID 30000046u")
        defines=f'#define MDO_UPDATE_ORIGIN "http://127.0.0.1:{publisher.server_port}"\n'
        defines+='#define MDO_DISTRIBUTION_HOST "127.0.0.1"\n#define MDO_DISTRIBUTION_SECURE false\n'
        defines+=f'#define MDO_DISTRIBUTION_PORT {publisher.server_port}\n'
        unity.write_text(defines + text, encoding="utf-8")
        target = base / "probe.exe"
        subprocess.run([str(ROOT / ".build/host/windows-server-glibc/xs.exe"), "pack", str(package), "-o", str(target)], check=True)
        old = target.read_bytes()
        port = free_port()
        home = base / "mdo-home"
        log = (base / "run.log").open("wb")
        process = subprocess.Popen([str(target), "--port", str(port), "--", "--home", str(home)],
            cwd=base, stdout=log, stderr=log, creationflags=subprocess.CREATE_NO_WINDOW)

        def poll(predicate, timeout=35):
            deadline = time.monotonic() + timeout
            last = None
            while time.monotonic() < deadline:
                try:
                    status, headers, body = request(port, "GET", "/api/v1/update")
                    last = body
                    if status == 200 and predicate(body["data"]):
                        return headers, body["data"]
                except (OSError, ValueError, KeyError):
                    pass
                time.sleep(.15)
            raise AssertionError((last, (base / "run.log").read_text(errors="replace")[-2000:]))

        try:
            poll(lambda s: s["enabled"] and not s["busy"] and s["status"] == "error")
            assert target.read_bytes() == old, "Wrong edition was accepted"
            Publisher.edition = "server"
            _, headers, _ = request(port, "GET", "/api/v1/bootstrap")
            token = next(v for k,v in headers.items() if k.lower() == "x-mdo-write-token")
            assert request(port, "POST", "/api/v1/update", {}, token)[0] == 200
            poll(lambda s: s["available"] and not s["busy"])
            assert queries[-1]["edition"] == ["server"] and queries[-1]["build_id"] == ["30000046"]
            assert request(port, "POST", "/api/v1/update/download", {}, token)[0] == 200
            poll(lambda s: s["ready"] and not s["busy"])
            assert request(port, "POST", "/api/v1/update/install", {}, token)[0] == 202
            process.wait(timeout=35)
            result_path = home / "data/update/install-result.json"
            deadline = time.monotonic() + 35
            while not result_path.exists():
                assert time.monotonic() < deadline, "Installer did not finish"
                time.sleep(.15)
            result = json.loads(result_path.read_text())
            assert result["status"] == "success", result
            assert target.read_bytes() == data and (home / "data/update/old.exe").read_bytes() == old
            # Same nondefault port and Home after replacement prove native args survive.
            poll(lambda s: s["enabled"] and s["edition"] == "server")
            print("PASS packed Windows server: reject desktop manifest, download, replace, backup, restart, preserve port/Home")
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(10)
            # Only the restarted executable owned by this temporary fixture.
            script = "$target='" + str(target).replace("'", "''") + "'; Get-CimInstance Win32_Process | Where-Object {$_.ExecutablePath -eq $target} | ForEach-Object {Stop-Process -Id $_.ProcessId -Force}"
            subprocess.run(["powershell.exe", "-NoProfile", "-NonInteractive", "-Command", script], check=True,
                creationflags=subprocess.CREATE_NO_WINDOW)
            log.close()
    publisher.shutdown()
    publisher.server_close()


if __name__ == "__main__":
    run()
