#!/usr/bin/env python3
"""Bounded compatibility smoke for the archived mdo application on a new xs host."""
from __future__ import annotations

import argparse
import http.client
import json
import os
import shutil
import signal
import socket
import subprocess
import tempfile
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
LEGACY_APP = ROOT / "app_bak"


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.bind(("127.0.0.1", 0))
        return int(probe.getsockname()[1])


def request(port: int, target: str) -> tuple[int, bytes]:
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=3)
    try:
        connection.request("GET", target)
        response = connection.getresponse()
        return response.status, response.read()
    finally:
        connection.close()


def wait_ready(port: int, process: subprocess.Popen[bytes], timeout: float = 20.0) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"legacy mdo exited during startup: {process.returncode}")
        try:
            status, _ = request(port, "/")
            if status == 200:
                return
        except OSError:
            pass
        time.sleep(0.05)
    raise RuntimeError("legacy mdo did not become ready")


def stop(process: subprocess.Popen[bytes]) -> None:
    if process.poll() is not None:
        return
    if os.name == "nt":
        process.send_signal(signal.CTRL_BREAK_EVENT)
    else:
        process.send_signal(signal.SIGTERM)
    try:
        process.wait(timeout=15)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)
        raise RuntimeError("legacy mdo did not stop gracefully")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    args = parser.parse_args()
    exe = args.exe.resolve()
    if not exe.is_file():
        parser.error(f"missing xs executable: {exe}")
    if not LEGACY_APP.is_dir():
        parser.error(f"missing archived application: {LEGACY_APP}")

    with tempfile.TemporaryDirectory(prefix="mdo-legacy-compat-") as temp_name:
        root = Path(temp_name)
        app = root / "app"
        host = root / exe.name
        log_path = root / "run.log"
        shutil.copy2(exe, host)
        shutil.copytree(LEGACY_APP, app)
        port = free_port()
        config = {
            "engine": {"workers": 1},
            "services": [{
                "enabled": True,
                "class": "http",
                "name": "mdo-legacy-compat",
                "ip": "127.0.0.1",
                "port": port,
                "host_default": {
                    "enabled": True,
                    "name": "mdo",
                    "path": "app/wwwroot",
                    "devlang": "c",
                    "devfile": "app/main.c",
                },
            }],
        }
        config_path = root / "xs.json"
        config_path.write_text(json.dumps(config, ensure_ascii=False), encoding="utf-8")
        creationflags = subprocess.CREATE_NEW_PROCESS_GROUP if os.name == "nt" else 0
        with log_path.open("wb") as log:
            process = subprocess.Popen(
                [str(host), str(config_path)], cwd=root, stdout=log,
                stderr=subprocess.STDOUT, creationflags=creationflags,
            )
            failure: BaseException | None = None
            try:
                wait_ready(port, process)
                status, home = request(port, "/")
                if status != 200 or b"<html" not in home.lower():
                    raise RuntimeError(f"legacy home response: {status}, {home[:80]!r}")
                status, settings = request(port, "/api/settings")
                if status != 200 or b'"ok":true' not in settings:
                    raise RuntimeError(f"legacy settings response: {status}, {settings[:160]!r}")
                status, i18n = request(port, "/i18n-data.js?lang=zh")
                if status != 200 or b"export const" not in i18n:
                    raise RuntimeError(f"legacy i18n response: {status}, {i18n[:160]!r}")
                time.sleep(1.0)
                if process.poll() is not None:
                    raise RuntimeError(f"legacy mdo exited after requests: {process.returncode}")
            except BaseException as error:
                failure = error
            finally:
                try:
                    stop(process)
                except BaseException as error:
                    if failure is None:
                        failure = error

        output = log_path.read_text(encoding="utf-8", errors="replace")
        if failure is not None:
            raise RuntimeError(f"{failure}\n--- xs log ---\n{output[-5000:]}") from failure
        if process.returncode != 0 or "[xs] bye" not in output:
            raise RuntimeError(f"unexpected shutdown ({process.returncode})\n{output[-5000:]}")
        print("LEGACY MDO COMPAT PASS: static, settings, i18n, graceful stop")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
