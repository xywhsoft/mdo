"""Real packed fallback polling with one delayed read and navigation during it.

Arm /__qa/poll-arm/timeout, then send 'streaming' in the first printed session.
The first exact run GET captures real state and waits twelve seconds. Later
reads work, while run catalog reads briefly fail to isolate this fallback.
Arm /navigation before sending 'stream-stop', then select the second session
while its exact GET is held and send a follow-up there. Model traffic is local;
create the printed stop file to save proof and stop only this copied program.
"""
import argparse
from collections import Counter
import http.client
from http.server import ThreadingHTTPServer
import json
import os
from pathlib import Path
import re
import threading
import time

from manual_conversation_retry_qa import Model
from manual_run_admission_qa import Proxy as ForwardProxy
from test_api_runtime import request, session_events
from test_packed_home_lease import site, start, stop, wait_bootstrap
from test_packed_queue_start_recovery import configure_model


class Proxy(ForwardProxy):
    def proxy(self):
        if self.command == "GET" and self.path.startswith("/__qa/poll-arm/"):
            mode = self.path.rsplit("/", 1)[-1]
            assert mode in ("timeout", "navigation", "normal")
            with self.server.lock:
                self.server.poll_phase = {"mode": mode, "run_id": None, "held_reads": 0,
                    "run_reads": [], "list_failures": 0, "run_posts": 0}
                self.server.poll_phases.append(self.server.poll_phase)
            return self.reply(200, self.server.poll_phase)
        if self.command == "GET" and self.path == "/__qa/poll-state":
            with self.server.lock: return self.reply(200, self.server.poll_phases)
        run_path = re.fullmatch(r"/api/v1/runs/(run-[A-Za-z0-9_.-]+)", self.path)
        hold = False; record = None
        with self.server.lock:
            phase = self.server.poll_phase
            if phase:
                if self.command == "POST" and self.path.endswith("/runs"):
                    phase["run_posts"] += 1
                if phase["mode"] != "normal" and self.command == "GET":
                    if self.path == "/api/v1/runs" and phase["run_reads"] and (
                            time.monotonic() - phase["run_reads"][0]["started"] < 12):
                        phase["list_failures"] += 1
                        return self.reply(503, {"ok": False, "error": {"code": "fixture_list_unavailable"}})
                    if run_path:
                        record = {"run_id": run_path[1], "started": time.monotonic()}
                        phase["run_reads"].append(record)
                        if not phase["held_reads"]:
                            phase["held_reads"] += 1; phase["run_id"] = run_path[1]; hold = True
        if not hold: return super().proxy()
        upstream = http.client.HTTPConnection("127.0.0.1", self.server.native, timeout=10)
        try:
            upstream.request("GET", self.path, headers={"Accept": "application/json"})
            response = upstream.getresponse(); raw = response.read()
            assert response.status == 200, raw
            with self.server.lock:
                record["captured"] = time.monotonic()
                record["captured_run"] = json.loads(raw)["data"]
            self.server.stopped.wait(12)
            with self.server.lock: record["released"] = time.monotonic()
            self.send_response(response.status)
            for key, value in response.getheaders():
                if key.lower() not in ("content-length", "connection", "transfer-encoding"):
                    self.send_header(key, value)
            self.send_header("Content-Length", str(len(raw))); self.end_headers(); self.wfile.write(raw)
        finally: upstream.close()

    do_GET = do_POST = do_PUT = do_PATCH = do_DELETE = proxy


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--packed", type=Path, required=True)
    parser.add_argument("--directory", type=Path, required=True)
    args = parser.parse_args(); base = args.directory.resolve()
    assert not base.exists(), "Choose a fresh isolated directory"
    base.mkdir(parents=True); native, port = site(base, "native", args.packed.resolve())
    Model.workspace = base; Model.stream_wait = 25; Model.shutdown_requested.clear()
    model = ThreadingHTTPServer(("127.0.0.1", 0), Model)
    proxy = ThreadingHTTPServer(("127.0.0.1", 0), Proxy)
    proxy.native = port; proxy.lock = threading.Lock(); proxy.phase = None; proxy.phases = []
    proxy.poll_phase = None; proxy.poll_phases = []; proxy.stopped = threading.Event()
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
        sessions = [call("POST", "/api/v1/sessions", {"project_id": "default", "title": title,
            "model_id": "queue-fixture", "reasoning_effort": "none", "permission_profile": "balanced"}, 201)
            for title in ("Poll original", "Poll next session")]
        print(json.dumps({"urls": [f"http://127.0.0.1:{proxy.server_port}/#/projects/default/sessions/" + item["id"] for item in sessions],
            "proxy_port": proxy.server_port, "native_port": port, "stop_file": str(base / "stop")}), flush=True)
        deadline = time.monotonic() + 600
        while not (base / "stop").exists() and time.monotonic() < deadline:
            assert process.poll() is None, "Native fixture exited"
            time.sleep(.1)
        proof = {"phases": proxy.poll_phases, "model_calls": Model.calls, "sessions": [], "websocket_unavailable": True}
        for item in sessions:
            path = "/api/v1/projects/default/sessions/" + item["id"]
            events = session_events(port, path)
            proof["sessions"].append({"id": item["id"], "title": item["title"],
                "user_inputs": [event.get("text", "") for event in events if event["kind"] == "agent_start"],
                "final_model_errors": sum(event["kind"] == "error" for event in events),
                "event_kinds": dict(Counter(event["kind"] for event in events)),
                "queue": call("GET", path + "/queue"), "draft": call("GET", path + "/draft")})
        proof["runs"] = call("GET", "/api/v1/runs")
        (base / "proof.json").write_text(json.dumps(proof, indent=2), encoding="utf-8")
    finally:
        proxy.stopped.set(); Model.shutdown_requested.set(); stop(process)
        for server in (proxy, model): server.shutdown(); server.server_close()
        for thread in threads: thread.join(timeout=3)


if __name__ == "__main__": main()
