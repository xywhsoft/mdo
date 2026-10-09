"""Finite real packed startup/selection recovery, with bounded loopback faults.

Reuses the HTTP/WS fixture and standard model setup. The wrapper provides
cold-page cases for saved selection, saved detail and uncertain writes. Native
writes are real and never repeated by the proxy. GET /__qa/workspace-proof
records actual stored selection, both tasks and served source hashes. Save
wrapper #observations, then create the printed stop file to close the copy.
"""
from collections import Counter
import hashlib
import json
from pathlib import Path
import time
from urllib.parse import parse_qs, urlsplit

import manual_service_read_qa as fixture
from test_api_runtime import request, session_events


class Proxy(fixture.Proxy):
    snapshots = []

    def native(self, method, path, body=None, headers=None):
        return request(self.server.native, method, path, body=body,
            headers=headers or {"Content-Type": "application/json"})

    def initialize(self):
        if getattr(self.server, "startup_saved", None): return
        route = parse_qs(urlsplit(self.path).query)["route"][0]
        saved = {"project_id": "default", "session_id": route.rsplit("/", 1)[-1]}
        status, _, raw = self.native("POST", "/api/v1/sessions", json.dumps({
            "project_id": "default", "title": "Catalog recent task", "model_id": "queue-fixture",
            "reasoning_effort": "none", "permission_profile": "balanced"}).encode())
        assert status == 201, raw
        other = {"project_id": "default", "session_id": json.loads(raw)["data"]["id"]}
        self.server.startup_saved, self.server.startup_other = saved, other
        self.server.startup_phases, self.server.startup_phase = [], None
        status, _, raw = self.native("PUT", "/api/v1/workspace-state", json.dumps(saved).encode())
        assert status == 200, raw

    def task_path(self, choice):
        return "/api/v1/projects/default/sessions/" + choice["session_id"]

    def emit(self, status, headers, raw, hold=False):
        self.send_response(status)
        for key, value in headers.items():
            if key.lower() not in ("content-length", "connection", "transfer-encoding"):
                self.send_header(key, value)
        self.send_header("Content-Length", str(len(raw))); self.end_headers()
        if hold: self.wfile.flush(); self.server.stopped.wait(12)
        return self.wfile.write(raw)

    def proxy(self):
        if self.command == "GET" and self.path.startswith("/__qa/service"):
            self.initialize()
            raw = Path(__file__).with_name("packed-workspace-startup-browser.html").read_bytes()
            return self.emit(200, {"Content-Type": "text/html; charset=utf-8"}, raw)
        if self.command == "GET" and self.path.startswith("/__qa/workspace-arm/"):
            mode = self.path.rsplit("/", 1)[-1]
            assert mode in ("initial", "detail", "exhaust", "lost", "hold-write", "exhaust-write", "hold-read")
            # Each cold case starts from the same real saved choice.
            status, _, raw = self.native("PUT", "/api/v1/workspace-state", json.dumps(self.server.startup_saved).encode())
            assert status == 200, raw
            phase = {"mode": mode, "started": time.monotonic(), "reads": [], "writes": [],
                "left": 6 if mode in ("exhaust", "exhaust-write") else 1 if mode == "hold-read" else 2,
                "accepted": False}
            with self.server.lock:
                self.server.startup_phase = phase; self.server.startup_phases.append(phase)
            return self.reply(200, {"other_route": "#/projects/default/sessions/" + self.server.startup_other["session_id"]})
        if self.command == "GET" and self.path == "/__qa/workspace-proof":
            status, _, raw = self.native("GET", "/api/v1/workspace-state"); assert status == 200
            sources = {}
            for name in ["js/features/shell/workspace-startup.js", "lang/zh-CN.json", "lang/en-US.json", "lang/ru-RU.json"]:
                code, _, source = self.native("GET", "/" + name); assert code == 200
                sources[name] = hashlib.sha256(source).hexdigest()
            tasks = []
            for choice in [self.server.startup_saved, self.server.startup_other]:
                path = self.task_path(choice); events = session_events(self.server.native, path)
                _, _, draft = self.native("GET", path + "/draft")
                tasks.append({**choice, "inputs": [x.get("text") for x in events if x["kind"] == "agent_start"],
                    "errors": sum(x["kind"] == "error" for x in events),
                    "event_kinds": dict(Counter(x["kind"] for x in events)), "draft": json.loads(draft)["data"]})
            snapshot = {"stored": json.loads(raw)["data"], "saved": self.server.startup_saved,
                "other": self.server.startup_other, "phases": self.server.startup_phases,
                "tasks": tasks, "sources": sources}
            # Deep copy: later phase counters must not mutate prior snapshots.
            self.snapshots.append(json.loads(json.dumps(snapshot)))
            return self.reply(200, snapshot)
        with self.server.lock: phase = getattr(self.server, "startup_phase", None)
        selection = self.path == "/api/v1/workspace-state"
        detail = phase and self.path == self.task_path(self.server.startup_saved)
        if phase and self.command == "GET" and (selection or detail):
            record = {"path": self.path, "started": time.monotonic(), "fault": None}
            with self.server.lock:
                phase["reads"].append(record)
                faulty = phase["left"] and ((selection and phase["mode"] in ("initial", "exhaust", "hold-read")) or
                    (detail and phase["mode"] == "detail") or
                    (selection and phase["accepted"] and phase["mode"] in ("lost", "hold-write", "exhaust-write")))
                if faulty:
                    phase["left"] -= 1
                    record["fault"] = "hold" if phase["mode"] == "hold-read" else "503"
            if record["fault"] == "503":
                record.update(status=503, ended=time.monotonic())
                return self.reply(503, {"ok": False, "error": {"code": "network_error", "message": "QA read unavailable"}})
            status, headers, raw = self.native("GET", self.path, headers=dict(self.headers))
            record.update(status=status)
            self.emit(status, headers, raw, record["fault"] == "hold")
            record["ended"] = time.monotonic(); return
        if phase and selection and self.command == "PUT":
            body = self.rfile.read(int(self.headers.get("Content-Length", "0")))
            record = {"started": time.monotonic(), "body": json.loads(body)}
            with self.server.lock:
                phase["writes"].append(record)
                self.server.writes.append({"method": "PUT", "path": self.path, "started": record["started"]})
            status, headers, raw = self.native("PUT", self.path, body, dict(self.headers))
            record["native_status"] = status
            assert status == 200, raw
            first = not phase["accepted"]; phase["accepted"] = True
            if first and phase["mode"] in ("lost", "exhaust-write"):
                record.update(reply="lost", ended=time.monotonic())
                return self.reply(503, {"ok": False, "error": {"code": "network_error", "message": "QA lost acknowledgement"}})
            record["reply"] = "held" if first and phase["mode"] == "hold-write" else "normal"
            self.emit(status, headers, raw, record["reply"] == "held")
            record["ended"] = time.monotonic(); return
        return super().proxy()

    do_GET = do_POST = do_PUT = do_PATCH = do_DELETE = proxy


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--directory", type=Path, required=True)
    args, _ = parser.parse_known_args()
    fixture.Proxy = Proxy
    try: fixture.main()
    finally:
        if args.directory.exists():
            (args.directory / "workspace-snapshots.json").write_text(
                json.dumps(Proxy.snapshots, ensure_ascii=False, indent=2), encoding="utf-8")
