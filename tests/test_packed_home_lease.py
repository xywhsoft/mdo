"""Bounded two-process check for the packed executable's portable Home lease."""

from __future__ import annotations

import argparse
import http.client
import json
import os
import shutil
import socket
import subprocess
import tempfile
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent


def free_port() -> int:
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        return listener.getsockname()[1]


def request(port: int, method: str, path: str, body: dict | None = None):
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=2)
    try:
        data = json.dumps(body).encode() if body is not None else None
        headers = {"Content-Type": "application/json"} if data else {}
        if method not in ("GET", "HEAD", "OPTIONS"):
            from test_api_runtime import request as raw_request
            _, metadata, _ = raw_request(port, "GET", "/api/v1/bootstrap")
            headers["X-Mdo-Write-Token"] = metadata["x-mdo-write-token"]
        connection.request(method, path, data,
                           headers)
        response = connection.getresponse()
        return response.status, json.loads(response.read())
    finally:
        connection.close()


def site(base: Path, name: str, packed: Path) -> tuple[Path, int]:
    path = base / name
    path.mkdir()
    shutil.copy2(packed, path / packed.name)
    port = free_port()
    (path / "xs.json").write_text(json.dumps({"services": [{
        "enabled": True, "class": "http", "name": "mdo", "ip": "127.0.0.1",
        "port": port, "recv_limit": 8454144, "body_limit": 8388608,
        "host_default": {"enabled": True, "name": "mdo", "path": "web",
                         "devlang": "c", "devfile": "generated/mdo_unity.c"},
    }]}), encoding="utf-8")
    return path, port


def start(path: Path, packed: Path, home: Path, env: dict) -> subprocess.Popen:
    with (path / "packed.log").open("ab") as output:
        return subprocess.Popen(
            [str(path / packed.name), "--", "--home", str(home)],
            cwd=path, env=env, stdout=output, stderr=subprocess.STDOUT,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0,
        )


def wait_bootstrap(process: subprocess.Popen, port: int, log: Path) -> tuple[int, dict]:
    deadline = time.monotonic() + 10.0
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise AssertionError(log.read_text(errors="replace"))
        try:
            status, response = request(port, "GET", "/api/v1/bootstrap")
            if status == 200 and response.get("data", {}).get("stage") in {"ready", "failed"}:
                return status, response
        except (OSError, ValueError):
            pass
        time.sleep(0.1)
    raise AssertionError(f"bootstrap timed out: {log.read_text(errors='replace')}")


def stop(process: subprocess.Popen) -> None:
    if process.poll() is None:
        process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


def release_packed_copies(*copies: Path) -> None:
    """Allow Windows to release an exited executable before temp cleanup."""
    for copy in copies:
        deadline = time.monotonic() + 5.0
        while True:
            try:
                copy.unlink()
                break
            except PermissionError:
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.05)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--packed", type=Path, default=ROOT / ("mdo.exe" if os.name == "nt" else "mdo"))
    parser.add_argument("--pause-on-conflict", action="store_true",
                        help="hold both processes so the failed page can be inspected")
    args = parser.parse_args()
    packed = args.packed.resolve()
    assert packed.is_file(), packed
    (ROOT / ".build").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="packed-home-lease-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        first_site, first_port = site(base, "first", packed)
        second_site, second_port = site(base, "second", packed)
        home = base / "mdo-home"
        env = os.environ.copy()
        env["USERPROFILE"] = str(base)
        env["MDO_LING_RESPONSES_URL"] = "http://127.0.0.1:9/v1"
        env["MDO_LING_API_KEY"] = "bounded-home-lease-key"
        first = start(first_site, packed, home, env)
        second = None
        try:
            status, response = wait_bootstrap(first, first_port, first_site / "packed.log")
            assert status == 200 and response["data"]["ready"], response
            status, response = request(first_port, "POST", "/api/v1/sessions", {
                "project_id": "default", "title": "Home lease QA",
                "agent_id": "mdo.default", "model_id": "ling-3.0-tiny",
                "protocol": "openai-responses", "reasoning_effort": "medium",
                "max_output_tokens": 1024,
            })
            assert status == 201, response
            session_id = response["data"]["id"]
            assert (home / ".mdo.lock").is_file()
            second = start(second_site, packed, home, env)
            status, response = wait_bootstrap(second, second_port, second_site / "packed.log")
            assert status == 200 and response["data"]["stage"] == "failed", response
            assert "already in use" in response["data"]["message"], response
            assert first.poll() is None
            if args.pause_on_conflict:
                print(f"READY url=http://127.0.0.1:{second_port}/ base={base}", flush=True)
                input("Press Enter to stop the conflict fixture.")
        finally:
            if second is not None:
                stop(second)
            if first.poll() is None:
                first.kill()
            first.wait(timeout=5)
        restarted = start(second_site, packed, home, env)
        try:
            status, response = wait_bootstrap(restarted, second_port,
                                              second_site / "packed.log")
            assert status == 200 and response["data"]["ready"], response
            status, response = request(second_port, "GET", "/api/v1/sessions")
            assert status == 200, response
            assert any(item["id"] == session_id for item in response["data"]["items"]), response
        finally:
            stop(restarted)
        release_packed_copies(first_site / packed.name,
                              second_site / packed.name)
    print("packed Home lease probe: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
