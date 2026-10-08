"""Read-only live catalog checks with real packed Linux service products.

Runs each libc product in an isolated Home. Does not log in, install packages,
change the website, make model calls, or replace the running executable.
"""
import argparse
import hashlib
import http.client
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import time

from manual_linux_products_qa import request, stop


def check(binary, libc):
    folder = Path(tempfile.mkdtemp(prefix="mdo-linux-online-"))
    local = folder / "mdo-server"
    shutil.copy2(binary, local)
    local.chmod(0o755)
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    log_path = folder / "run.log"
    with log_path.open("ab") as log:
        process = subprocess.Popen([str(local), "--port", str(port)], cwd=folder,
            env=dict(os.environ, MDO_HOME=str(folder / "home")), stdout=log, stderr=log)
    try:
        deadline = time.monotonic() + 150
        update = distribution = None
        while time.monotonic() < deadline:
            if process.poll() is not None:
                raise AssertionError(log_path.read_text(encoding="utf-8", errors="replace"))
            try:
                status, _, bootstrap = request(port, "GET", "/api/v1/bootstrap")
                if status == 200:
                    status, _, result = request(port, "GET", "/api/v1/update")
                    if status == 200:
                        update = result["data"]
                    status, _, result = request(port, "GET", "/api/v1/distribution")
                    if status == 200:
                        distribution = result["data"]
                    if update and update["status"] == "current" and not update["busy"] and distribution and distribution.get("toolpacks"):
                        break
            except (OSError, ValueError, http.client.HTTPException):
                pass
            time.sleep(.25)
        else:
            raise AssertionError(dict(update=update, distribution=distribution, log=str(log_path)))
        digest = hashlib.sha256(local.read_bytes()).hexdigest()
        assert update["enabled"] and not update["available"], update
        assert update["platform"] == "linux-x86_64-" + libc and update["edition"] == "server", update
        assert update["local_sha256"] == digest and update["sha256"] == digest, update
        assert distribution["platform"] == "linux-x86_64", distribution
        packs = distribution["toolpacks"]
        assert {item["id"] for item in packs} == {"core", "python"}, packs
        assert all(item["platform"] == "linux-x86_64" for item in packs), packs
        stop(process)
        return dict(passed=True, libc=libc, binary_sha256=digest,
            build_id=distribution["build_id"], platform=update["platform"],
            toolpacks=[dict(id=p["id"], revision=p["revision"], sha256=p["sha256"]) for p in packs],
            checks=["packed standalone startup", "public HTTPS update check", "correct libc/edition", "published binary hash", "Linux-only tool catalog", "SIGTERM"])
    finally:
        stop(process)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dist", type=Path, default=Path("dist"))
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    receipt = dict(passed=True, origin="https://ai.xywhsoft.com", products=[
        check((args.dist / ("linux-x86_64-" + libc + "-server") / "mdo-server").resolve(), libc)
        for libc in ("glibc", "musl")])
    if args.output:
        args.output.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(receipt, indent=2))


if __name__ == "__main__":
    main()
