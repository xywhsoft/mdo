"""Local HTTP authority and browser origin checks in a disposable Home."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from test_api_runtime import free_port, request, wait_ready

ROOT = Path(__file__).resolve().parents[1]


def run(host):
    with tempfile.TemporaryDirectory(prefix="api-origin-", dir=ROOT / ".build") as temporary:
        base = Path(temporary)
        site = base / "site"
        shutil.copytree(ROOT / "app", site)
        port = free_port()
        config = json.loads((site / "xs.json").read_text(encoding="utf-8"))
        service = config["services"][0]
        service["class"] = "http"
        service["port"] = port
        service.pop("window", None)
        (site / "xs.json").write_text(json.dumps(config), encoding="utf-8")
        with (base / "native.log").open("wb") as log:
            process = subprocess.Popen([str(host), str(site / "xs.json")], cwd=site,
                env=dict(os.environ, MDO_HOME=str(base / "home")), stdout=log, stderr=log,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
            try:
                try:
                    wait_ready(port, process)
                except Exception:
                    log.flush()
                    print((base / "native.log").read_text(errors="replace"))
                    raise
                for authority in (f"127.0.0.1:{port}", f"localhost:{port}", f"[::1]:{port}"):
                    status, headers, _ = request(port, "GET", "/api/v1/bootstrap",
                        headers={"Host": authority, "Origin": "http://" + authority})
                    assert status == 200 and "x-mdo-write-token" in headers, (authority, status)
                for authority in (f"attacker.invalid:{port}", f"127.0.0.1.attacker.invalid:{port}",
                                  "127.0.0.1:1", f"127.0.0.1:{port}@attacker.invalid", "localhost:"):
                    status, headers, body = request(port, "GET", "/api/v1/bootstrap", headers={"Host": authority})
                    assert status in (400, 403) and "x-mdo-write-token" not in headers, (authority, status, body)
                for origin in ("https://attacker.invalid", "null", f"http://localhost:{port}"):
                    status, headers, body = request(port, "GET", "/api/v1/bootstrap", headers={"Origin": origin})
                    assert status == 403 and "x-mdo-write-token" not in headers, (origin, status, body)
                # A valid write token cannot make a foreign browser origin safe.
                _, valid, _ = request(port, "GET", "/api/v1/bootstrap")
                status, _, _ = request(port, "POST", "/api/v1/account/logout", body=b"{}",
                    headers={"Origin": "https://attacker.invalid", "Content-Type": "application/json",
                             "X-Mdo-Write-Token": valid["x-mdo-write-token"]})
                assert status == 403, status
                # Top-level PKCE callback navigation has no Origin, as do native
                # device bridges and CLI clients. Keep those supported.
                assert request(port, "GET", "/api/v1/account/callback?state=invalid")[0] == 400
                print("PASS local authority, DNS rebinding rejection, same-origin reads/writes and callback navigation")
            finally:
                if process.poll() is None:
                    process.terminate()
                process.wait(timeout=10)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, default=ROOT / ".build/host/xs.exe")
    run(parser.parse_args().host.resolve())
