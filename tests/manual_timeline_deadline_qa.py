"""Real packed cold history and background synchronization with slow replies.

Uses a loopback response-delay proxy, an isolated Home and a normal local
model. No product QA injection. Open the first printed URL, check its seeded
conversation and saved draft, then GET /__qa/timeline-arm/refresh. Leave the
conversation idle through that delayed background read before sending the
draft and a follow-up in the second task. Create the stop file to save proof.
"""
import argparse
from collections import Counter
from http.server import ThreadingHTTPServer
import json
import os
from pathlib import Path
import threading
import time

from manual_resource_deadline_qa import Proxy as SlowProxy
from manual_run_admission_qa import Model
from test_api_runtime import request, session_events
from test_packed_home_lease import site, start, stop, wait_bootstrap
from test_packed_queue_start_recovery import configure_model


class Proxy(SlowProxy):
    def proxy(self):
        if self.command == "GET" and self.path == "/__qa/timeline-arm/refresh":
            with self.server.lock:
                self.server.resources = {self.server.refresh_path}
                self.server.reads.setdefault(self.server.refresh_path, [])
                return self.reply(200, {"armed": self.server.refresh_path})
        return super().proxy()

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
    proxy.resources = set(); proxy.reads = {}; proxy.writes = []; proxy.refresh_path = ""
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
        sessions = [call("POST", "/api/v1/sessions", {"project_id": "default", "title": title,
            "model_id": "queue-fixture", "reasoning_effort": "none", "permission_profile": "balanced"}, 201)
            for title in ("Timeline deadline", "Timeline follow-up")]
        first_path = "/api/v1/projects/default/sessions/" + sessions[0]["id"]
        seed = call("POST", first_path + "/runs", {"prompt": "TIMELINE_SEEDED_HISTORY"}, 202)
        deadline = time.monotonic() + 5
        while not call("GET", "/api/v1/runs/" + seed["id"])["terminal"]:
            assert time.monotonic() < deadline, "Seed reply did not finish"
            time.sleep(.1)
        call("PUT", first_path + "/draft", {"revision": 0, "text": "TIMELINE_SAVED_DRAFT"})
        snapshot = call("GET", first_path + "/conversation?limit=4")
        with proxy.lock:
            cold = first_path + "/conversation?limit=4"
            proxy.resources = {cold}; proxy.reads = {cold: []}
            proxy.refresh_path = first_path + f'/conversation?after={snapshot["next_cursor"]}&epoch={snapshot["epoch"]}&limit=4'
        print(json.dumps({"urls": [f"http://127.0.0.1:{proxy.server_port}/#/projects/default/sessions/" + item["id"] for item in sessions],
            "proxy_port": proxy.server_port, "native_port": port, "stop_file": str(base / "stop")}), flush=True)
        deadline = time.monotonic() + 600
        while not (base / "stop").exists() and time.monotonic() < deadline:
            assert process.poll() is None, "Native fixture exited"
            time.sleep(.1)
        proof = {"reads": proxy.reads, "writes": proxy.writes,
            "model_calls": dict(Model.calls), "sessions": [], "websocket_unavailable": True,
            "seed_prompt": "TIMELINE_SEEDED_HISTORY"}
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
        proxy.stopped.set(); stop(process)
        for server in (proxy, model): server.shutdown(); server.server_close()
        for thread in threads: thread.join(timeout=3)


if __name__ == "__main__": main()
