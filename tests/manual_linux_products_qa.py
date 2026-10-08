"""Bounded packed-product smoke tests, without model calls or load tests.

Run on Linux for ELF variants; the same HTTP/persistence checks also accept a
Windows service executable. Each invocation owns an isolated temporary Home.
"""
from __future__ import annotations

import argparse
import hashlib
import http.client
import json
import os
from pathlib import Path
import shutil
import signal
import socket
import subprocess
import tempfile
import time


def request(port: int, method: str, path: str, body=None, token: str = ""):
    client = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
    headers = {"Content-Type": "application/json", "Origin": f"http://127.0.0.1:{port}"}
    if token:
        headers["X-Mdo-Write-Token"] = token
    client.request(method, path, json.dumps(body) if body is not None else None, headers)
    response = client.getresponse()
    data = response.read()
    result = response.status, dict(response.getheaders()), json.loads(data) if data else None
    client.close()
    return result


def stop(process: subprocess.Popen) -> None:
    if process.poll() is None:
        if os.name == "nt":
            # A hidden console gives the service CLI a real console-control
            # event even when this test itself was launched through pipes.
            import ctypes
            kernel = ctypes.windll.kernel32
            kernel.FreeConsole()
            if not kernel.AttachConsole(process.pid):
                raise ctypes.WinError()
            try:
                if not kernel.GenerateConsoleCtrlEvent(1, process.pid):
                    raise ctypes.WinError()
            finally:
                kernel.FreeConsole()
                kernel.AttachConsole(ctypes.c_ulong(-1))
        else:
            process.send_signal(signal.SIGTERM)
        process.wait(timeout=30)
    assert process.returncode == 0, f"abnormal exit: {process.returncode}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve()
    folder = Path(tempfile.mkdtemp(prefix="mdo-linux-qa-"))
    local = folder / binary.name
    shutil.copy2(binary, local)
    os.chmod(local, 0o755)
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    home = folder / "portable-home"
    log_path = folder / "run.log"
    environment = dict(os.environ, MDO_HOME=str(home))
    # Test the service contract even when the input contains a GUI backend.
    environment.pop("DISPLAY", None); environment.pop("WAYLAND_DISPLAY", None)
    process = None
    try:
        def start():
            log = log_path.open("ab")
            startup = None
            if os.name == "nt":
                startup = subprocess.STARTUPINFO()
                startup.dwFlags = subprocess.STARTF_USESHOWWINDOW
                startup.wShowWindow = subprocess.SW_HIDE
            child = subprocess.Popen([str(local), "--port", str(port)], cwd=folder,
                env=environment, stdout=log, stderr=subprocess.STDOUT,
                startupinfo=startup,
                creationflags=subprocess.CREATE_NEW_PROCESS_GROUP | subprocess.CREATE_NEW_CONSOLE if os.name == "nt" else 0)
            log.close()
            deadline = time.monotonic() + 90
            while time.monotonic() < deadline:
                if child.poll() is not None:
                    raise AssertionError(log_path.read_text(errors="replace"))
                try:
                    status, headers, bootstrap = request(port, "GET", "/api/v1/bootstrap")
                    if status == 200:
                        return child, headers, bootstrap["data"]
                except (OSError, ValueError, http.client.HTTPException):
                    pass
                time.sleep(0.1)
            child.terminate(); child.wait(timeout=30)
            raise AssertionError("startup timeout: " + log_path.read_text(errors="replace"))

        process, headers, bootstrap = start()
        token = next((v for k, v in headers.items() if k.lower() == "x-mdo-write-token"), "")
        assert token, (headers, bootstrap)
        status, _, projects = request(port, "GET", "/api/v1/projects")
        assert status == 200 and isinstance(projects["data"]["items"], list), projects
        status, _, result = request(port, "POST", "/api/v1/projects", {"id": "linux-smoke", "name": "Linux smoke", "workspace_root": str(folder)}, token)
        assert status in (200, 201), result
        project_id = result["data"]["id"]
        stop(process); process = None
        process, _, _ = start()
        status, _, projects = request(port, "GET", "/api/v1/projects")
        assert status == 200 and any(item["id"] == project_id for item in projects["data"]["items"]), projects
        stop(process); process = None
        receipt = {"passed": True, "binary": str(binary), "binary_sha256": hashlib.sha256(local.read_bytes()).hexdigest(), "home": str(home),
                   "checks": ["packed TCC startup", "bootstrap/write token", "projects API", "project persistence", "SIGTERM/console stop", "restart"],
                   "log": str(log_path)}
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(receipt, indent=2))
        return 0
    finally:
        if process is not None:
            stop(process)


if __name__ == "__main__":
    raise SystemExit(main())
