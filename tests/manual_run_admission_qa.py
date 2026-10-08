"""Bounded real packed send inspection and lost admission-response checks.

The loopback proxy forwards real writes before losing/delaying their response.
It can fail two recovery or receipt GETs. WebSocket is unavailable here so the
real polling fallback is exercised. No source, clipboard or download mocking.
Arm /__qa/arm/preflight, /lost or /slow, then send in the matching printed task.
Create the printed stop file when finished; the fixture lasts ten minutes.
"""
import argparse
from collections import Counter
import http.client
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import socket
import threading
import time

from test_api_runtime import request, session_events
from test_packed_home_lease import site, start, stop, wait_bootstrap
from test_packed_queue_start_recovery import configure_model


class Model(BaseHTTPRequestHandler):
    calls = Counter()
    lock = threading.Lock()

    def log_message(self, *_): pass

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        prompt = next(row["content"] for row in reversed(body["messages"]) if row["role"] == "user")
        assert isinstance(prompt, str), "This fixture uses text only"
        with self.lock: self.calls[prompt] += 1
        raw = json.dumps({"id": "run-admission-fixture", "model": body["model"],
            "choices": [{"index": 0, "message": {"role": "assistant",
                "content": "ADMISSION_CONFIRMED — " + prompt}, "finish_reason": "stop"}],
            "usage": {"prompt_tokens": 50, "completion_tokens": 20}}).encode()
        self.send_response(200); self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw))); self.end_headers(); self.wfile.write(raw)


class Proxy(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *_): pass

    def reply(self, status, data):
        raw = json.dumps(data).encode()
        self.send_response(status); self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw))); self.end_headers(); self.wfile.write(raw)

    def handle(self):
        try: super().handle()
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError): pass

    def proxy(self):
        if self.path.startswith("/__qa/arm/") and self.command == "GET":
            mode = self.path.rsplit("/", 1)[-1]
            assert mode in ("preflight", "lost", "slow")
            with self.server.lock:
                self.server.phase = {"mode": mode, "queue_posts": 0, "run_posts": 0,
                    "native_run_accepts": 0, "lost_responses": 0, "delayed_responses": 0,
                    "recovery_failed": 0, "receipt_failed": 0, "receipt_reads": 0,
                    "recovery_left": 0, "receipt_left": 0, "armed": True}
                self.server.phases.append(self.server.phase)
            return self.reply(200, self.server.phase)
        if self.headers.get("Upgrade", "").lower() == "websocket":
            return self.reply(503, {"ok": False, "error": {"code": "fixture_live_unavailable"}})
        run_post = self.command == "POST" and self.path.startswith("/api/v1/projects/") and self.path.endswith("/runs")
        queue_post = self.command == "POST" and self.path.startswith("/api/v1/projects/") and self.path.endswith("/queue")
        recovery_get = self.command == "GET" and self.path.endswith("/recovery")
        receipt_get = self.command == "GET" and "/queue/" in self.path
        lose = delay = fail = False
        with self.server.lock:
            phase = self.server.phase
            if phase:
                if queue_post:
                    phase["queue_posts"] += 1
                    if phase["mode"] == "preflight" and phase["armed"]:
                        phase["recovery_left"] = 2; phase["armed"] = False
                if run_post:
                    phase["run_posts"] += 1
                    lose = phase["mode"] == "lost" and phase["armed"]
                    delay = phase["mode"] == "slow" and phase["armed"]
                    if lose or delay: phase["armed"] = False
                if receipt_get: phase["receipt_reads"] += 1
                for matched, field in ((recovery_get, "recovery"), (receipt_get, "receipt")):
                    if matched and phase[field + "_left"]:
                        phase[field + "_left"] -= 1; phase[field + "_failed"] += 1; fail = True
        if fail:
            return self.reply(503, {"ok": False, "error": {"code": "fixture_read_unavailable", "message": "Transient read fault"}})
        body = self.rfile.read(int(self.headers.get("Content-Length", "0")))
        headers = {key: value for key, value in self.headers.items() if key.lower() not in ("host", "connection")}
        if headers.get("Origin") == f"http://127.0.0.1:{self.server.server_port}":
            headers["Origin"] = f"http://127.0.0.1:{self.server.native}"
        upstream = http.client.HTTPConnection("127.0.0.1", self.server.native, timeout=10)
        try:
            upstream.request(self.command, self.path, body=body, headers=headers)
            response = upstream.getresponse(); raw = response.read()
            if run_post and phase:
                assert response.status == 202, raw
                with self.server.lock:
                    phase["native_run_accepts"] += 1
                    if lose: phase["receipt_left"] = 2; phase["lost_responses"] += 1
                    if delay: phase["delayed_responses"] += 1
            if delay: self.server.stopped.wait(12)
            self.send_response(response.status)
            for key, value in response.getheaders():
                if key.lower() not in ("content-length", "connection", "transfer-encoding"):
                    self.send_header(key, value)
            self.send_header("Content-Length", str(len(raw))); self.end_headers()
            self.wfile.write(raw[:4] if lose else raw); self.wfile.flush()
            if lose:
                self.close_connection = True; self.connection.shutdown(socket.SHUT_RDWR)
        finally: upstream.close()

    do_GET = do_POST = do_PUT = do_PATCH = do_DELETE = proxy


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--packed", type=Path, required=True)
    parser.add_argument("--directory", type=Path, required=True)
    args = parser.parse_args(); base = args.directory.resolve()
    assert not base.exists(), "Choose a fresh isolated directory"
    base.mkdir(parents=True); native, port = site(base, "native", args.packed.resolve())
    model = ThreadingHTTPServer(("127.0.0.1", 0), Model)
    proxy = ThreadingHTTPServer(("127.0.0.1", 0), Proxy)
    proxy.native = port; proxy.lock = threading.Lock(); proxy.phase = None; proxy.phases = []
    proxy.stopped = threading.Event()
    threads = [threading.Thread(target=server.serve_forever, daemon=True) for server in (model, proxy)]
    for thread in threads: thread.start()
    process = start(native, args.packed.resolve(), base / "home",
        dict(os.environ, USE_WEBVIEW="0", MDO_QUEUE_FIXTURE_KEY="fixture-only"))
    def call(method, path, body=None, expected=200):
        status, _, raw = request(port, method, path, body=None if body is None else json.dumps(body).encode(),
            headers={"Content-Type": "application/json"})
        assert status == expected, raw
        return json.loads(raw)["data"]
    try:
        status, bootstrap = wait_bootstrap(process, port, native / "packed.log")
        assert status == 200 and bootstrap["data"]["ready"], bootstrap
        configure_model(port, f"http://127.0.0.1:{model.server_port}/v1")
        sessions = [call("POST", "/api/v1/sessions", {"project_id": "default", "title": "Start " + mode,
            "model_id": "queue-fixture", "reasoning_effort": "none", "permission_profile": "balanced"}, 201)
            for mode in ("preflight", "lost", "slow")]
        print(json.dumps({"urls": [f"http://127.0.0.1:{proxy.server_port}/#/projects/default/sessions/" + item["id"] for item in sessions],
            "proxy_port": proxy.server_port, "native_port": port, "stop_file": str(base / "stop")}), flush=True)
        deadline = time.monotonic() + 600
        while not (base / "stop").exists() and time.monotonic() < deadline:
            assert process.poll() is None, "Native fixture exited"
            time.sleep(.1)
        proof = {"proxy_phases": proxy.phases, "model_calls": dict(Model.calls), "sessions": [], "websocket_unavailable": True}
        for item in sessions:
            path = "/api/v1/projects/default/sessions/" + item["id"]
            events = session_events(port, path)
            proof["sessions"].append({"id": item["id"], "title": item["title"],
                "user_inputs": [event.get("text", "") for event in events if event["kind"] == "agent_start"],
                "final_errors": sum(event["kind"] == "error" for event in events),
                "event_kinds": dict(Counter(event["kind"] for event in events)),
                "queue": call("GET", path + "/queue"), "draft": call("GET", path + "/draft")})
        (base / "proof.json").write_text(json.dumps(proof, indent=2), encoding="utf-8")
    finally:
        proxy.stopped.set(); stop(process)
        for server in (proxy, model): server.shutdown(); server.server_close()
        for thread in threads: thread.join(timeout=3)


if __name__ == "__main__": main()
