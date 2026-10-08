"""Bounded real packed startup with four slow, successful resource reads.

Only the first GET of models, settings, session list and first session detail
is held for twelve seconds in a loopback proxy. Subsequent reads are real.
The candidate and Home are isolated; no product QA injection or credentials.
Open the printed first URL, confirm the saved draft, send it, then select the
second task and send a follow-up. Create the printed stop file to save proof.
An uncooperative JavaScript transport is covered by deterministic tests; this
native fixture instead exercises the browser's real fetch and read recovery.
"""
import argparse
from collections import Counter
import http.client
from http.server import ThreadingHTTPServer
import json
import os
from pathlib import Path
import threading
import time

from manual_run_admission_qa import Model, Proxy as ForwardProxy
from test_api_runtime import request, session_events
from test_packed_home_lease import site, start, stop, wait_bootstrap
from test_packed_queue_start_recovery import configure_model


class Proxy(ForwardProxy):
    def proxy(self):
        if self.command == "GET" and self.path == "/__qa/resource-state":
            with self.server.lock:
                return self.reply(200, {"reads": self.server.reads, "writes": self.server.writes})
        held = False; record = None
        with self.server.lock:
            if self.command == "GET" and self.path in self.server.resources:
                records = self.server.reads[self.path]
                record = {"started": time.monotonic(), "held": not records}
                records.append(record); held = record["held"]
            elif self.command in ("POST", "PUT", "PATCH", "DELETE"):
                self.server.writes.append({"method": self.command, "path": self.path,
                                           "started": time.monotonic()})
        if not held:
            return super().proxy()
        upstream = http.client.HTTPConnection("127.0.0.1", self.server.native, timeout=10)
        try:
            upstream.request("GET", self.path, headers={"Accept": "application/json"})
            response = upstream.getresponse(); raw = response.read()
            assert response.status == 200, raw
            with self.server.lock:
                record.update(captured=time.monotonic(), native_status=response.status)
            self.server.stopped.wait(12)
            with self.server.lock:
                record["released"] = time.monotonic()
            self.send_response(response.status)
            for key, value in response.getheaders():
                if key.lower() not in ("content-length", "connection", "transfer-encoding"):
                    self.send_header(key, value)
            self.send_header("Content-Length", str(len(raw)))
            self.end_headers(); self.wfile.write(raw)
        finally:
            upstream.close()

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
    proxy.resources = set(); proxy.reads = {}; proxy.writes = []
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
            for title in ("Slow resources", "Resource follow-up")]
        first_path = "/api/v1/projects/default/sessions/" + sessions[0]["id"]
        call("PUT", first_path + "/draft", {"revision": 0, "text": "SAVED_RESOURCE_DEADLINE_DRAFT"})
        with proxy.lock:
            proxy.resources = {"/api/v1/models", "/api/v1/settings", "/api/v1/sessions", first_path}
            proxy.reads = {path: [] for path in sorted(proxy.resources)}
        print(json.dumps({"urls": [f"http://127.0.0.1:{proxy.server_port}/#/projects/default/sessions/" + item["id"] for item in sessions],
            "proxy_port": proxy.server_port, "native_port": port, "stop_file": str(base / "stop")}), flush=True)
        deadline = time.monotonic() + 600
        while not (base / "stop").exists() and time.monotonic() < deadline:
            assert process.poll() is None, "Native fixture exited"
            time.sleep(.1)
        proof = {"reads": proxy.reads, "writes": proxy.writes,
                 "model_calls": dict(Model.calls), "sessions": [], "websocket_unavailable": True}
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
