"""Serve a single packed mdo with only synthetic portable cache and legacy data.

Open the printed URL, use Settings / Diagnostics and storage to import, then
type 'restart' here to verify startup recovery and the imported dark theme.
The fixture creates no session before the import and never calls a model.
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

from test_api_runtime import ROOT, free_port, request, wait_ready
from test_interrupt_runtime import stop_host


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--packed-path", type=Path, default=ROOT / "mdo.exe")
    args = parser.parse_args()
    base = Path(tempfile.mkdtemp(prefix="mdo-packed-migration-", dir=ROOT / ".build"))
    packed = base / ("mdo.exe" if os.name == "nt" else "mdo")
    shutil.copy2(args.packed_path, packed)
    home = base / "mdo-home"
    cache = home / "data/cache/webview2/cache.bin"
    cache.parent.mkdir(parents=True)
    cache.write_bytes(b"portable-cache-fixture")
    legacy = base / ".mdo/config.json"
    legacy.parent.mkdir()
    source = b'{"models":[],"settings":{"theme":"dark"}}'
    legacy.write_bytes(source)
    port = free_port()
    (base / "xs.json").write_text(json.dumps({"services": [{
        "enabled": True, "class": "http", "name": "mdo", "ip": "127.0.0.1",
        "port": port, "host_default": {"enabled": True, "name": "mdo", "path": "web",
            "devlang": "c", "devfile": "generated/mdo_unity.c"}}]}), encoding="utf-8")
    env = dict(os.environ, USERPROFILE=str(base), HOME=str(base), MDO_HOME=str(home))
    process = None

    def start() -> subprocess.Popen:
        with (base / "packed.log").open("ab") as log:
            running = subprocess.Popen([str(packed)], cwd=base, env=env, stdout=log,
                stderr=subprocess.STDOUT,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        wait_ready(port, running)
        return running

    try:
        process = start()
        print(f"READY url=http://127.0.0.1:{port}/#/settings base={base}", flush=True)
        while True:
            command = input("Type restart, status, or Enter to stop: ").strip()
            if not command: break
            if command == "restart":
                stop_host(process)
                process = start()
                status, _, body = request(port, "GET", "/api/v1/settings")
                assert status == 200 and json.loads(body)["data"]["appearance"]["theme"] == "dark", body
                assert not (home / ".mdo-import").exists()
                assert not (home / ".mdo-import-cleanup").exists()
                print("RESTART PASS: imported theme loaded; transaction retired", flush=True)
            status, _, body = request(port, "GET", "/api/v1/bootstrap")
            assert status == 200, body
            assert cache.read_bytes() == b"portable-cache-fixture"
            assert legacy.read_bytes() == source
            print(json.dumps(json.loads(body)["data"]["home"], ensure_ascii=False), flush=True)
    finally:
        if process is not None: stop_host(process)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
