#!/usr/bin/env python3
"""Bounded cancel-to-priority-send probe through the real HTTP and TCC path."""

from __future__ import annotations

import argparse
import http.client
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent


class ModelHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    lock = threading.Lock()
    calls = 0

    def log_message(self, *_args: object) -> None:
        pass

    def do_POST(self) -> None:  # noqa: N802 - stdlib callback name
        if self.path != "/v1/responses":
            self.send_error(404)
            return
        length = int(self.headers.get("Content-Length", "0"))
        if length < 1 or length > 1024 * 1024:
            self.send_error(400)
            return
        json.loads(self.rfile.read(length))
        with ModelHandler.lock:
            ModelHandler.calls += 1
            first = ModelHandler.calls == 1
        if first:
            time.sleep(2)
        payload = json.dumps({
            "id": "resp_interrupt_probe", "model": "ornith-1.5-35b",
            "status": "completed",
            "output": [{"type": "message", "content": [
                {"type": "output_text", "text": "Priority reply complete"}]}],
            "usage": {"input_tokens": 8, "output_tokens": 3,
                      "total_tokens": 11},
        }).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        try:
            self.wfile.write(payload)
        except (BrokenPipeError, ConnectionResetError):
            pass


class ModelServer(ThreadingHTTPServer):
    def handle_error(self, request, client_address) -> None:
        if isinstance(sys.exc_info()[1], (BrokenPipeError, ConnectionResetError)):
            return
        super().handle_error(request, client_address)


def free_port() -> int:
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def request(port: int, method: str, path: str, body: dict | None = None):
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=4)
    payload = json.dumps(body).encode() if body is not None else None
    headers = {"Content-Type": "application/json"} if payload else {}
    if method not in ("GET", "HEAD", "OPTIONS") and path.startswith("/api/v1/"):
        from test_api_runtime import request as raw_request
        _, metadata, _ = raw_request(port, "GET", "/api/v1/bootstrap")
        headers["X-Mdo-Write-Token"] = metadata["x-mdo-write-token"]
    try:
        connection.request(method, path, body=payload, headers=headers)
        response = connection.getresponse()
        raw = response.read()
        return response.status, json.loads(raw)
    finally:
        connection.close()


def until(predicate, seconds: float = 5.0):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(0.05)
    raise AssertionError("interrupt probe exceeded its bounded deadline")


def start_host(host: Path, config: Path, home: Path, environment: dict,
               log: Path, port: int) -> subprocess.Popen:
    output = log.open("ab")
    try:
        process = subprocess.Popen(
            [str(host), str(config), "--", "--home", str(home)],
            cwd=config.parent, env=environment, stdout=output,
            stderr=subprocess.STDOUT,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0,
        )
    finally:
        output.close()

    def ready():
        if process.poll() is not None:
            raise AssertionError(f"xs exited: {process.returncode}")
        try:
            status, body = request(port, "GET", "/api/v1/bootstrap")
            return status == 200 and body["data"]["ready"]
        except (OSError, KeyError, json.JSONDecodeError):
            return False

    try:
        until(ready, 20)
    except BaseException:
        stop_host(process)
        raise
    return process


def stop_host(process: subprocess.Popen | None) -> None:
    if process is None or process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


def run_probe(host: Path) -> None:
    if not (ROOT / "app/generated/mdo_unity.c").is_file():
        raise RuntimeError("run tools/build_mdo.py --prepare-only first")
    (ROOT / ".build").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="interrupt-runtime-",
                                     dir=ROOT / ".build") as raw:
        base = Path(raw)
        site = base / "site"
        shutil.copytree(ROOT / "app", site)
        port = free_port()
        model = ModelServer(("127.0.0.1", 0), ModelHandler)
        model_port = int(model.server_address[1])
        model_thread = threading.Thread(target=model.serve_forever, daemon=True)
        model_thread.start()
        (site / "xs.json").write_text(json.dumps({
            "engine": {"workers": 1},
            "services": [{"enabled": True, "class": "http",
                          "name": "mdo-interrupt-probe", "ip": "127.0.0.1",
                          "port": port, "host_default": {
                              "enabled": True, "name": "mdo", "path": "web",
                              "devlang": "c",
                              "devfile": "generated/mdo_unity.c"}}],
        }), encoding="utf-8")
        environment = os.environ.copy()
        environment["USERPROFILE"] = str(base)
        environment["HOME"] = str(base)
        environment["MDO_ORNITH_RESPONSES_URL"] = (
            f"http://127.0.0.1:{model_port}/v1")
        environment["MDO_ORNITH_API_KEY"] = "bounded-interrupt-test-key"
        config = site / "xs.json"
        home = base / "home"
        log = base / "xs.log"
        process = None
        try:
            process = start_host(host, config, home, environment, log, port)
            status, body = request(port, "POST", "/api/v1/sessions", {
                "project_id": "default", "title": "Interrupt probe",
                "agent_id": "mdo.default", "model_id": "ornith-1.5-35b",
                "protocol": "openai-responses", "reasoning_effort": "medium",
                "max_output_tokens": 1024,
            })
            assert status == 201, (status, body)
            session_id = body["data"]["id"]
            path = f"/api/v1/projects/default/sessions/{session_id}"
            status, body = request(port, "POST", path + "/runs", {
                "prompt": "Interrupt this model call"})
            assert status == 202, (status, body)
            run_path = "/api/v1/runs/" + body["data"]["id"]

            def user_recorded():
                events = request(port, "GET", path +
                                 "/events?after=0&limit=32")[1]["data"]["items"]
                return any(event["kind"] == "agent_start" and
                           event["user_message_sequence"] > 0
                           for event in events)

            until(user_recorded)
            status, body = request(port, "DELETE", run_path)
            assert status == 200, (status, body)
            until(lambda: request(port, "GET", run_path)[1]["data"]["terminal"])
            status, body = request(port, "GET", path +
                                   "/events?after=0&limit=32")
            assert status == 200, (status, body)
            stopped = [event for event in body["data"]["items"]
                       if event["kind"] == "agent_done" and
                       not event["success"] and event["terminal"]]
            assert len(stopped) == 1, body
            stopped_event_id = stopped[0]["event_id"]
            status, body = request(port, "GET", path + "/recovery")
            assert status == 200, (status, body)
            recovery = body["data"]
            assert recovery["resume_required"] and recovery["total"] == 0
            assert recovery["last_sequence"] > 0
            stale = {"revision": recovery["revision"],
                     "last_sequence": recovery["last_sequence"] + 1}
            status, body = request(port, "POST", path + "/abandon", stale)
            assert status == 409, (status, body)
            status, body = request(port, "POST", path + "/abandon", {
                "revision": recovery["revision"],
                "last_sequence": recovery["last_sequence"],
            })
            assert status == 200, (status, body)
            assert body["data"]["last_sequence"] == (
                recovery["last_sequence"] + 1), body
            status, body = request(port, "POST", path + "/abandon", {
                "revision": recovery["revision"],
                "last_sequence": recovery["last_sequence"],
            })
            assert status == 409, (status, body)

            stop_host(process)
            process = start_host(host, config, home, environment, log, port)
            status, body = request(port, "GET", path +
                                   "/events?after=0&limit=32")
            assert status == 200 and any(
                event["event_id"] == stopped_event_id and
                event["kind"] == "agent_done" and not event["success"]
                for event in body["data"]["items"]), body
            status, body = request(port, "GET", path + "/recovery")
            assert status == 200 and not body["data"]["resume_required"], body
            status, body = request(port, "POST", path + "/runs", {
                "prompt": "Priority prompt after cancellation"})
            assert status == 202, (status, body)
            next_path = "/api/v1/runs/" + body["data"]["id"]
            until(lambda: request(port, "GET", next_path)[1]["data"]["terminal"])
            status, body = request(port, "GET", next_path)
            assert status == 200 and body["data"]["state"] == "succeeded", body
            print("interrupt transaction runtime probe: PASS")
        finally:
            stop_host(process)
            model.shutdown()
            model.server_close()
            model_thread.join(timeout=3)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True, type=Path)
    args = parser.parse_args()
    run_probe(args.host.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
