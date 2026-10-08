"""Packed Markdown export with bounded loopback attachment read faults.

The actual workbench exports one real image conversation. Arm /__qa/arm/meta,
/binary or /slow after its initial render. The first two fail two GETs; slow
holds the binary body for 35 seconds. /__qa/export observes the real Blob and
preserves its download action. Only an isolated Home and a synthetic model.
Create the printed stop file to collect evidence; this copy lasts 10 minutes.
"""
import argparse
import base64
import hashlib
import http.client
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import threading
import time
from urllib.parse import parse_qs, urlsplit

from manual_image_upload_deadline_qa import Model
from manual_live_probe_qa import Proxy as LiveProxy
from manual_run_admission_qa import Proxy as ForwardProxy
from test_api_runtime import request, session_events
from test_image_runtime import wait_run
from test_packed_home_lease import site, start, stop, wait_bootstrap
from test_packed_queue_start_recovery import configure_model

PNG = base64.b64decode("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jG1kAAAAASUVORK5CYII=")
LONG_REPLY = "COPY ORIGINAL QA\n" + "A complete original line.\n" * 400 + "MDO_LONG_REPLY_END"


class TextModel(BaseHTTPRequestHandler):
    calls = []

    def log_message(self, *_):
        pass

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        text = next(row["content"] for row in reversed(body["messages"]) if row["role"] == "user")
        self.calls.append(text)
        content = LONG_REPLY if text == "EXPORT DOWNLOAD QA" else "CONTINUED — " + text
        raw = json.dumps({"id": "text-recovery-fixture", "model": body["model"],
            "choices": [{"index": 0, "message": {"role": "assistant", "content": content},
                "finish_reason": "stop"}], "usage": {"prompt_tokens": 80, "completion_tokens": 2000}}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)


class Proxy(ForwardProxy):
    tunnel = LiveProxy.tunnel

    def proxy(self):
        if self.headers.get("Upgrade", "").lower() == "websocket":
            return self.tunnel()
        if self.command == "GET" and self.path == "/__qa/export":
            raw = (Path(__file__).parent / "fixtures/packed-export-download-browser.html").read_bytes()
            raw = raw.replace(b'"__MDO_QA_ROUTE__"', json.dumps(self.server.route).encode())
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(raw)))
            self.end_headers()
            self.wfile.write(raw)
            return
        if self.command == "GET" and self.path == "/__qa/idle":
            with self.server.lock:
                self.server.export_phase = None
            return self.reply(200, {"idle": True})
        if self.command == "GET" and self.path.startswith("/__qa/arm/"):
            mode = self.path.rsplit("/", 1)[-1]
            assert mode in (("full", "list", "text-slow") if self.server.text_recovery else ("meta", "binary", "slow"))
            with self.server.lock:
                phase = {"mode": mode, "started_at": time.monotonic(), "reads": [], "left": 2,
                    "full_left": 2}
                self.server.export_phase = phase
                self.server.exports.append(phase)
            return self.reply(200, phase)
        parsed = urlsplit(self.path)
        if self.server.text_recovery and self.command == "GET" and parsed.path == self.server.text_path:
            params = parse_qs(parsed.query)
            full = params.get("full_text") == ["1"]
            with self.server.lock:
                phase = self.server.export_phase
                listing = params.get("after") == ["0"] and not full
                fault = bool(phase and ((full and phase["full_left"] > 0)
                    or (listing and phase["mode"] == "list" and phase["left"] > 0)))
                if fault:
                    phase["full_left" if full else "left"] -= 1
                hold = bool(full and phase and phase["mode"] == "text-slow")
                record = {"path": self.path, "at": time.monotonic(), "wall_at": time.time(),
                    "fault": fault and not hold, "held": hold}
                if phase and (full or listing):
                    phase["reads"].append(record)
            if fault and not hold:
                record["status"] = 503
                return self.reply(503, {"ok": False, "error": {"code": "temporary_unavailable"}})
            upstream = http.client.HTTPConnection("127.0.0.1", self.server.native, timeout=10)
            try:
                upstream.request("GET", self.path)
                response = upstream.getresponse()
                raw = response.read()
                record.update(status=response.status, bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest())
                self.send_response(response.status)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(raw)))
                self.end_headers()
                self.wfile.flush()
                if hold:
                    self.server.stopped.wait(65)
                    record["released_at"] = time.monotonic()
                self.wfile.write(raw)
            finally:
                upstream.close()
            return
        image = self.path == self.server.image_path
        info = self.path == self.server.image_path + "/info"
        if self.command != "GET" or not (image or info):
            return super().proxy()
        with self.server.lock:
            phase = self.server.export_phase
            fault = bool(phase and phase["left"] and (
                (info and phase["mode"] == "meta") or (image and phase["mode"] != "meta")))
            if fault:
                phase["left"] -= 1
            record = {"path": self.path, "at": time.monotonic(), "fault": fault}
            if phase:
                phase["reads"].append(record)
        if fault and phase["mode"] != "slow":
            record["status"] = 503
            return self.reply(503, {"ok": False, "error": {"code": "temporary_unavailable"}})
        upstream = http.client.HTTPConnection("127.0.0.1", self.server.native, timeout=10)
        try:
            upstream.request("GET", self.path)
            response = upstream.getresponse()
            raw = response.read()
            record.update(status=response.status, bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest())
            self.send_response(response.status)
            for key, value in response.getheaders():
                if key.lower() not in ("content-length", "connection", "transfer-encoding"):
                    self.send_header(key, value)
            self.send_header("Content-Length", str(len(raw)))
            self.end_headers()
            self.wfile.flush()
            if fault and phase["mode"] == "slow":
                self.server.stopped.wait(35)
                record["released_at"] = time.monotonic()
            self.wfile.write(raw)
        finally:
            upstream.close()

    do_GET = do_POST = do_PUT = do_PATCH = do_DELETE = proxy


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--packed", type=Path, required=True)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--text-recovery", action="store_true",
        help="Use a long text conversation; arm full, list or text-slow reads instead of images")
    args = parser.parse_args()
    base = args.directory.resolve()
    assert not base.exists(), "Choose a fresh isolated directory"
    base.mkdir(parents=True)
    packed = args.packed.resolve()
    native, port = site(base, "native", packed)
    model_type = TextModel if args.text_recovery else Model
    model = ThreadingHTTPServer(("127.0.0.1", 0), model_type)
    proxy = ThreadingHTTPServer(("127.0.0.1", 0), Proxy)
    proxy.native = port
    proxy.lock = threading.Lock()
    proxy.stopped = threading.Event()
    proxy.tunnels = {}
    proxy.websockets = []
    proxy.phase = proxy.export_phase = None
    proxy.phases = []
    proxy.exports = []
    proxy.text_recovery = args.text_recovery
    proxy.text_path = ""
    threads = [threading.Thread(target=server.serve_forever, daemon=True) for server in (model, proxy)]
    for thread in threads:
        thread.start()
    process = start(native, packed, base / "home",
        dict(os.environ, USE_WEBVIEW="0", MDO_QUEUE_FIXTURE_KEY="fixture-only"))
    def call(method, path, body=None, expected=200):
        status, _, raw = request(port, method, path,
            body=json.dumps(body).encode() if body is not None else None,
            headers={"Content-Type": "application/json"} if body is not None else None)
        assert status == expected, (status, raw)
        return json.loads(raw)["data"]
    try:
        assert wait_bootstrap(process, port, native / "packed.log")[1]["data"]["stage"] == "ready"
        configure_model(port, f"http://127.0.0.1:{model.server_port}/v1")
        status, headers, raw = request(port, "GET", "/api/v1/models/config")
        assert status == 200
        config = json.loads(raw)["data"]
        next(row for row in config["items"] if row["id"] == "queue-fixture")["attachments"] = ["image"]
        status, _, raw = request(port, "PUT", "/api/v1/settings/models",
            body=json.dumps({"schema_version": 1, "patch": config}).encode(),
            headers={"Content-Type": "application/json", "If-Match": headers["etag"]})
        assert status == 200, raw
        task = call("POST", "/api/v1/sessions", {"project_id": "default",
            "title": "Text read recovery" if args.text_recovery else "Image export recovery", "model_id": "queue-fixture",
            "reasoning_effort": "none", "permission_profile": "balanced"}, 201)
        path = "/api/v1/projects/default/sessions/" + task["id"]
        image_id = ""
        if not args.text_recovery:
            status, _, raw = request(port, "POST", path + "/attachments", body=PNG,
                headers={"Content-Type": "image/png", "X-Mdo-File-Name": "export.png"})
            assert status == 201, raw
            image_id = json.loads(raw)["data"]["id"]
        prompt = {"prompt": "EXPORT DOWNLOAD QA"}
        if image_id:
            prompt["attachments"] = [image_id]
        run = call("POST", path + "/runs", prompt, 202)
        finished = wait_run(port, run["id"])
        assert finished["state"] == "succeeded", finished
        proxy.image_path = path + "/attachments/" + image_id
        proxy.text_path = path + "/events"
        proxy.route = "/#/projects/default/sessions/" + task["id"]
        sources = {}
        files = ("js/state/sessions.js", "js/features/chat/timeline.js", "js/features/chat/tool-content.js",
            "js/api/request-recovery.js") if args.text_recovery else (
            "js/features/sessions/session-export-images.js", "js/api/request-recovery.js")
        for file in files:
            status, _, raw = request(port, "GET", "/" + file)
            assert status == 200
            sources[file] = hashlib.sha256(raw).hexdigest()
        state = {"url": f"http://127.0.0.1:{proxy.server_port}/__qa/export",
            "native_port": port, "proxy_port": proxy.server_port, "session_id": task["id"],
            "image_id": image_id, "image_sha256": hashlib.sha256(PNG).hexdigest(),
            "served_sources": sources, "stop_file": str(base / "stop")}
        if args.text_recovery:
            state.update(reply_bytes=len(LONG_REPLY.encode()), reply_sha256=hashlib.sha256(LONG_REPLY.encode()).hexdigest())
        (base / "state.json").write_text(json.dumps(state), encoding="utf-8")
        print(json.dumps(state), flush=True)
        deadline = time.monotonic() + 600
        while not (base / "stop").exists() and time.monotonic() < deadline:
            assert process.poll() is None, "Owned fixture exited"
            time.sleep(.1)
        events = session_events(port, path)
        proof = {**state, "exports": proxy.exports, "model_calls": model_type.calls,
            "websockets": proxy.websockets, "events": events,
            "final_errors": sum(event["kind"] == "error" for event in events)}
        (base / "proof.json").write_text(json.dumps(proof, indent=2, ensure_ascii=False), encoding="utf-8")
    finally:
        proxy.stopped.set()
        with proxy.lock:
            pairs = list(proxy.tunnels.values())
        for pair in pairs:
            for channel in pair:
                try:
                    channel.shutdown(2)
                except OSError:
                    pass
        stop(process)
        for server in (proxy, model):
            server.shutdown()
            server.server_close()
        for thread in threads:
            thread.join(timeout=3)


if __name__ == "__main__":
    main()
