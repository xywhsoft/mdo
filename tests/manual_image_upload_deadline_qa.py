"""Real packed image-first conversation after a held upload acknowledgement.

Choose a normal public PNG through the printed page's file chooser. The real
PUT saves it before its response body is held; two metadata reads return 503.
Keep typing while recovery runs, send once the thumbnail appears, then send a
follow-up. Create the printed stop file to collect evidence and stop this copy.
Only loopback services and a disposable Home; no external model or credentials.
With --draft-read-fault, only the selected session's cold draft read has a
35-second held body and two failed retry GETs. Type before it finishes.
With --draft-write-fault, its first draft PUT is really saved before its reply
body is held for 35 seconds. Two following confirmation GETs return 503.
"""
import argparse
import base64
from collections import Counter
import hashlib
import http.client
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import re
import threading
import time

from manual_live_probe_qa import Proxy as LiveProxy
from manual_run_admission_qa import Proxy as ForwardProxy
from test_api_runtime import request, session_events
from test_packed_home_lease import site, start, stop, wait_bootstrap
from test_packed_queue_start_recovery import configure_model


class Model(BaseHTTPRequestHandler):
    calls = []
    lock = threading.Lock()

    def log_message(self, *_): pass

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        content = next(row["content"] for row in reversed(body["messages"]) if row["role"] == "user")
        parts = content if isinstance(content, list) else [{"type": "text", "text": content}]
        text = "\n".join(part["text"] for part in parts if part["type"] == "text")
        images = []
        for part in parts:
            if part["type"] != "image_url": continue
            url = part["image_url"]["url"]
            assert url.startswith("data:image/"), "fixture accepts only inline test images"
            images.append(hashlib.sha256(base64.b64decode(url.split(",", 1)[1])).hexdigest())
        with self.lock: self.calls.append({"text": text, "images": images})
        raw = json.dumps({"id": "image-recovery-fixture", "model": body["model"],
            "choices": [{"index": 0, "message": {"role": "assistant",
                "content": f"IMAGE_CONFIRMED — {text} · images={len(images)}"}, "finish_reason": "stop"}],
            "usage": {"prompt_tokens": 80, "completion_tokens": 20}}, ensure_ascii=False).encode()
        self.send_response(200); self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw))); self.end_headers(); self.wfile.write(raw)


class Proxy(ForwardProxy):
    tunnel = LiveProxy.tunnel

    def proxy(self):
        if self.headers.get("Upgrade", "").lower() == "websocket": return self.tunnel()
        if self.command == "GET" and self.path == "/__qa/image-state":
            with self.server.lock:
                return self.reply(200, {"uploads": self.server.uploads, "confirmations": self.server.confirmations,
                    "draft_reads": self.server.draft_reads, "draft_writes": self.server.draft_writes})
        draft = self.path.startswith("/api/v1/") and self.path.endswith("/draft")
        if draft and self.command == "PUT":
            with self.server.lock:
                record = {"path": self.path, "at": time.monotonic()}
                self.server.draft_writes.append(record)
                hold = self.server.draft_write_fault and self.path == self.server.draft_fault_path and not self.server.draft_write_held
                if hold: self.server.draft_write_held = True
            if hold:
                body = self.rfile.read(int(self.headers["Content-Length"]))
                record["body"] = json.loads(body)
                upstream = http.client.HTTPConnection("127.0.0.1", self.server.native, timeout=10)
                try:
                    headers = dict(self.headers); headers["Host"] = f"127.0.0.1:{self.server.native}"
                    if headers.get("Origin") == f"http://127.0.0.1:{self.server.server_port}":
                        headers["Origin"] = f"http://127.0.0.1:{self.server.native}"
                    upstream.request("PUT", self.path, body, headers)
                    response = upstream.getresponse(); raw = response.read()
                    assert response.status == 200, raw
                    record["status"] = response.status; record["saved_at"] = time.monotonic()
                    record["revision"] = json.loads(raw)["data"]["revision"]
                    with self.server.lock: self.server.draft_confirm_faults = 2
                    self.send_response(response.status)
                    for name, value in response.getheaders():
                        if name.lower() not in ("content-length", "connection", "transfer-encoding"): self.send_header(name, value)
                    self.send_header("Content-Length", str(len(raw))); self.end_headers(); self.wfile.flush()
                    self.server.stopped.wait(35); record["released_at"] = time.monotonic(); self.wfile.write(raw)
                finally: upstream.close()
                return
        if self.path == self.server.draft_fault_path and self.command == "GET" and self.server.draft_write_held:
            with self.server.lock:
                fault = self.server.draft_confirm_faults > 0
                if fault: self.server.draft_confirm_faults -= 1
                self.server.draft_reads.append({"path": self.path, "at": time.monotonic(), "fault": fault})
            if fault: return self.reply(503, {"ok": False, "error": {"code": "temporary_unavailable", "message": "owned draft confirmation fault"}})
        if self.path == self.server.draft_fault_path and self.command == "GET" and self.server.draft_fault_enabled:
            with self.server.lock:
                number = len(self.server.draft_reads) + 1
                record = {"path": self.path, "at": time.monotonic(), "number": number}
                self.server.draft_reads.append(record)
            if number in (2, 3):
                record["status"] = 503
                return self.reply(503, {"ok": False, "error": {"code": "temporary_unavailable", "message": "owned draft read fault"}})
            if number == 1:
                upstream = http.client.HTTPConnection("127.0.0.1", self.server.native, timeout=10)
                try:
                    upstream.request("GET", self.path)
                    response = upstream.getresponse(); raw = response.read()
                    record["status"] = response.status
                    self.send_response(response.status)
                    self.send_header("Content-Type", "application/json")
                    self.send_header("Content-Length", str(len(raw))); self.end_headers(); self.wfile.flush()
                    self.server.stopped.wait(35)
                    record["released_at"] = time.monotonic()
                    self.wfile.write(raw)
                finally: upstream.close()
                return
        keyed = re.fullmatch(r"/api/v1/projects/default/sessions/[^/]+/attachments/[0-9a-f]{32}", self.path)
        info = re.fullmatch(r"/api/v1/projects/default/sessions/[^/]+/attachments/[0-9a-f]{32}/info", self.path)
        if self.command == "GET" and info:
            with self.server.lock:
                fault = self.server.failures > 0
                if fault: self.server.failures -= 1
                self.server.confirmations.append({"path": self.path, "at": time.monotonic(), "fault": fault})
            if fault: return self.reply(503, {"ok": False, "error": {"code": "temporary_unavailable", "message": "owned read fault"}})
        if self.command != "PUT" or not keyed: return super().proxy()
        body = self.rfile.read(int(self.headers["Content-Length"]))
        upstream = http.client.HTTPConnection("127.0.0.1", self.server.native, timeout=10)
        try:
            headers = dict(self.headers)
            headers["Host"] = f"127.0.0.1:{self.server.native}"
            if headers.get("Origin") == f"http://127.0.0.1:{self.server.server_port}":
                headers["Origin"] = f"http://127.0.0.1:{self.server.native}"
            upstream.request("PUT", self.path, body, headers)
            response = upstream.getresponse(); raw = response.read()
            record = {"path": self.path, "saved_at": time.monotonic(), "status": response.status,
                      "bytes": len(body), "sha256": hashlib.sha256(body).hexdigest()}
            with self.server.lock: self.server.uploads.append(record)
            assert response.status == 201, raw
            self.send_response(response.status)
            for key, value in response.getheaders():
                if key.lower() not in ("content-length", "connection", "transfer-encoding"): self.send_header(key, value)
            self.send_header("Content-Length", str(len(raw))); self.end_headers(); self.wfile.flush()
            self.server.stopped.wait(43)
            with self.server.lock: record["released_at"] = time.monotonic()
            self.wfile.write(raw)
        finally: upstream.close()

    do_GET = do_POST = do_PUT = do_PATCH = do_DELETE = proxy


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--packed", type=Path, required=True)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--resume", action="store_true", help="Reopen this fixture's validated previous Home")
    parser.add_argument("--draft-read-fault", action="store_true", help="Hold the first cold draft body for 35 seconds, then fail two retry GETs")
    parser.add_argument("--draft-write-fault", action="store_true", help="Really save one draft PUT, hold its body for 35 seconds, then fail two confirmation GETs")
    args = parser.parse_args(); base = args.directory.resolve()
    assert not (args.draft_read_fault and args.draft_write_fault), "Choose one isolated draft fault"
    packed = args.packed.resolve()
    if args.resume:
        assert (base / "proof.json").is_file(), "Only resume a completed fixture"
        native = base / "native"
        assert hashlib.sha256((native / packed.name).read_bytes()).digest() == hashlib.sha256(packed.read_bytes()).digest()
        settings = json.loads((native / "xs.json").read_text(encoding="utf-8"))["services"][0]
        assert settings["ip"] == "127.0.0.1", "Fixture must remain loopback-only"
        port = settings["port"]
        (base / "stop").unlink(missing_ok=True)
    else:
        assert not base.exists(), "Choose a fresh isolated directory"
        base.mkdir(parents=True); native, port = site(base, "native", packed)
    model = ThreadingHTTPServer(("127.0.0.1", 0), Model)
    proxy = ThreadingHTTPServer(("127.0.0.1", 0), Proxy)
    proxy.native = port; proxy.lock = threading.Lock(); proxy.phase = None; proxy.phases = []
    proxy.tunnels = {}; proxy.websockets = []; proxy.stopped = threading.Event()
    proxy.uploads = []; proxy.confirmations = []; proxy.failures = 0 if args.resume else 2
    proxy.draft_fault_enabled = args.draft_read_fault
    proxy.draft_fault_path = None
    proxy.draft_write_fault = args.draft_write_fault; proxy.draft_write_held = False; proxy.draft_confirm_faults = 0
    proxy.draft_reads = []; proxy.draft_writes = []
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
        if not args.resume:
            configure_model(port, f"http://127.0.0.1:{model.server_port}/v1")
        status, headers, raw = request(port, "GET", "/api/v1/models/config")
        assert status == 200, raw
        config = json.loads(raw)["data"]
        next(item for item in config["items"] if item["id"] == "queue-fixture")["attachments"] = ["image"]
        next(item for item in config["providers"] if item["id"] == "queue-fixture")["endpoints"]["chat_completions"] = f"http://127.0.0.1:{model.server_port}/v1"
        status, _, raw = request(port, "PUT", "/api/v1/settings/models",
            body=json.dumps({"schema_version": 1, "patch": config}).encode(),
            headers={"Content-Type": "application/json", "If-Match": headers["etag"]})
        assert status == 200, raw
        if args.resume:
            previous = json.loads((base / "proof.json").read_text(encoding="utf-8"))
            identifier = previous.get("session_id") or previous["runs"]["items"][0]["session_id"]
            task = call("GET", "/api/v1/projects/default/sessions/" + identifier)
        else:
            task = call("POST", "/api/v1/sessions", {"project_id": "default",
                "title": "Draft save deadline" if args.draft_write_fault else "Draft read deadline" if args.draft_read_fault else "Image deadline",
                "model_id": "queue-fixture", "reasoning_effort": "none", "permission_profile": "balanced"}, 201)
        path = "/api/v1/projects/default/sessions/" + task["id"]
        proxy.draft_fault_path = path + "/draft"
        print(json.dumps({"url": f"http://127.0.0.1:{proxy.server_port}/#/projects/default/sessions/{task['id']}",
            "native_port": port, "proxy_port": proxy.server_port, "stop_file": str(base / "stop")}), flush=True)
        deadline = time.monotonic() + 600
        while not (base / "stop").exists() and time.monotonic() < deadline:
            assert process.poll() is None, "Native fixture exited"
            time.sleep(.1)
        events = session_events(port, path)
        attachments = base / "home/sessions/default" / task["id"] / "attachments"
        proof = {"resumed": args.resume, "session_id": task["id"], "uploads": proxy.uploads, "confirmations": proxy.confirmations,
            "websockets": proxy.websockets, "model_calls": Model.calls,
            "draft_reads": proxy.draft_reads, "draft_writes": proxy.draft_writes,
            "user_inputs": [event.get("text", "") for event in events if event["kind"] == "agent_start"],
            "final_model_errors": sum(event["kind"] == "error" for event in events),
            "event_kinds": dict(Counter(event["kind"] for event in events)),
            "attachment_files": {file.name: hashlib.sha256(file.read_bytes()).hexdigest() for file in attachments.glob("*") if file.is_file()},
            "queue": call("GET", path + "/queue"), "draft": call("GET", path + "/draft"),
            "runs": call("GET", "/api/v1/runs")}
        (base / ("proof-resumed.json" if args.resume else "proof.json")).write_text(json.dumps(proof, indent=2), encoding="utf-8")
    finally:
        proxy.stopped.set()
        with proxy.lock: pairs = list(proxy.tunnels.values())
        for pair in pairs:
            for channel in pair:
                try: channel.shutdown(2)
                except OSError: pass
        stop(process)
        for server in (proxy, model): server.shutdown(); server.server_close()
        for thread in threads: thread.join(timeout=3)


if __name__ == "__main__": main()
