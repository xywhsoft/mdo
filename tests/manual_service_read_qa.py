"""Real packed continuity reads with bounded loopback faults and UI observation.

Open the printed QA URL and choose cold/temporary/exhausted read. The wrapper
only observes actual DOM and dispatches a labelled synthetic focus event; it
does not mock the chat controller or replace its requests. HTTP/WS are proxied
to the isolated packed program. Writes and subsequent model inputs are real.
Create the printed stop file to collect proof; lifetime is ten minutes.
"""
import argparse
from collections import Counter
from http.server import ThreadingHTTPServer
import hashlib
import json
import os
from pathlib import Path
import threading
import time

from manual_live_probe_qa import Proxy as LiveProxy
from manual_run_admission_qa import Model
from test_api_runtime import request, session_events
from test_packed_home_lease import site, start, stop, wait_bootstrap
from test_packed_queue_start_recovery import configure_model


class Proxy(LiveProxy):
    def proxy(self):
        if self.command == "GET" and self.path.startswith("/__qa/service"):
            raw = Path(__file__).with_name("packed-service-read-browser.html").read_bytes()
            self.send_response(200); self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(raw))); self.end_headers()
            return self.wfile.write(raw)
        if self.command == "GET" and self.path.startswith("/__qa/arm/"):
            mode = self.path.rsplit("/", 1)[-1]
            assert mode in ("temporary", "exhaust", "hold")
            with self.server.lock:
                phase = {"mode": mode, "started": time.monotonic(), "reads": [],
                    "left": 1 if mode == "hold" else 2 if mode == "temporary" else 6}
                self.server.service_phase = phase; self.server.service_phases.append(phase)
            return self.reply(200, phase)
        if self.command == "GET" and self.path == "/api/v1/project-purge-intent":
            with self.server.lock:
                phase = self.server.service_phase
                record = {"started": time.monotonic(), "fault": None}
                self.server.service_reads.append(record)
                if phase:
                    phase["reads"].append(record)
                    if phase["left"]:
                        phase["left"] -= 1
                        record["fault"] = phase["mode"]
            if record["fault"] in ("temporary", "exhaust"):
                record.update(status=503, ended=time.monotonic())
                return self.reply(503, {"ok": False,
                    "error": {"code": "network_error", "message": "QA temporary read failure"}})
            status, headers, raw = request(self.server.native, "GET", self.path)
            record.update(status=status, sha256=hashlib.sha256(raw).hexdigest())
            self.send_response(status)
            for name, value in headers.items():
                if name.lower() not in ("content-length", "connection", "transfer-encoding"):
                    self.send_header(name, value)
            self.send_header("Content-Length", str(len(raw))); self.end_headers()
            if record["fault"] == "hold":
                self.wfile.flush(); self.server.stopped.wait(12)
            record["ended"] = time.monotonic()
            return self.wfile.write(raw)
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
    proxy.resources = set(); proxy.reads = {}; proxy.writes = []
    proxy.tunnels = {}; proxy.websockets = []; proxy.replay_paths = []
    proxy.service_phase = None; proxy.service_phases = []; proxy.service_reads = []
    proxy.stopped = threading.Event()
    threads = [threading.Thread(target=server.serve_forever, daemon=True) for server in (model, proxy)]
    for thread in threads: thread.start()
    process = start(native, args.packed.resolve(), base / "home",
        dict(os.environ, USE_WEBVIEW="0", MDO_QUEUE_FIXTURE_KEY="fixture-only"))

    def call(method, path, body=None, expected=200):
        status, _, raw = request(port, method, path,
            body=None if body is None else json.dumps(body).encode(),
            headers={"Content-Type": "application/json"})
        assert status == expected, raw
        return json.loads(raw)["data"]

    try:
        status, bootstrap = wait_bootstrap(process, port, native / "packed.log")
        assert status == 200 and bootstrap["data"]["ready"], bootstrap
        configure_model(port, f"http://127.0.0.1:{model.server_port}/v1")
        item = call("POST", "/api/v1/sessions", {"project_id": "default",
            "title": "Service read recovery", "model_id": "queue-fixture",
            "reasoning_effort": "none", "permission_profile": "balanced"}, 201)
        path = "/api/v1/projects/default/sessions/" + item["id"]
        call("PUT", path + "/draft", {"revision": 0, "text": "SERVICE_READ_SAVED_DRAFT"})
        route = "#/projects/default/sessions/" + item["id"]
        print(json.dumps({"url": f"http://127.0.0.1:{proxy.server_port}/__qa/service?route=" + route[1:],
            "native_url": f"http://127.0.0.1:{port}/" + route,
            "proxy_port": proxy.server_port, "native_port": port,
            "stop_file": str(base / "stop")}), flush=True)
        deadline = time.monotonic() + 600
        while not (base / "stop").exists() and time.monotonic() < deadline:
            assert process.poll() is None, "Native fixture exited"
            time.sleep(.1)
        events = session_events(port, path)
        sources = {}
        for name in ("js/app.js", "js/features/settings/project-purge-recovery.js",
                "js/features/projects/project-purge-recovery-panel.js", "js/api/request-recovery.js",
                "js/features/shell/workspace-startup.js",
                "lang/zh-CN.json", "lang/en-US.json", "lang/ru-RU.json"):
            status, _, raw = request(port, "GET", "/" + name)
            assert status == 200, name
            sources[name] = hashlib.sha256(raw).hexdigest()
        proof = {"phases": proxy.service_phases, "reads": proxy.service_reads,
            "writes": proxy.writes, "websockets": proxy.websockets,
            "model_calls": dict(Model.calls), "served_sources": sources,
            "user_inputs": [event.get("text", "") for event in events if event["kind"] == "agent_start"],
            "final_model_errors": sum(event["kind"] == "error" for event in events),
            "event_kinds": dict(Counter(event["kind"] for event in events)),
            "draft": call("GET", path + "/draft"), "queue": call("GET", path + "/queue"),
            "runs": call("GET", "/api/v1/runs")}
        (base / "proof.json").write_text(json.dumps(proof, indent=2, ensure_ascii=False), encoding="utf-8")
    finally:
        proxy.stopped.set(); stop(process)
        for server in (proxy, model): server.shutdown(); server.server_close()
        for thread in threads: thread.join(timeout=3)


if __name__ == "__main__": main()
