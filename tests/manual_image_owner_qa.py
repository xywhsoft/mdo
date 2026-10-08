"""Isolated packed UI probe: an image upload retains its composer owner.

Select a public test PNG in the first task, open Settings or Schedules, then
release /__qa/release through the fixture API. Returning must retain the image.
Switching to the other task instead must discard it without changing that draft.
No external providers, real credentials, or user Home are used.
"""
import argparse
import hashlib
import http.client
from http.server import ThreadingHTTPServer
import json
import os
from pathlib import Path
import re
import threading
import time

from manual_image_upload_deadline_qa import Model
from manual_live_probe_qa import Proxy as LiveProxy
from manual_run_admission_qa import Proxy as ForwardProxy
from test_api_runtime import request, session_events
from test_packed_home_lease import site, start, stop, wait_bootstrap
from test_packed_queue_start_recovery import configure_model


class Proxy(ForwardProxy):
    tunnel = LiveProxy.tunnel

    def proxy(self):
        if self.headers.get("Upgrade", "").lower() == "websocket":
            return self.tunnel()
        if self.command == "GET" and self.path == "/__qa/release":
            self.server.release.set()
            return self.reply(200, {"released": True})
        if self.command == "GET" and self.path == "/__qa/hold":
            self.server.release.clear()
            return self.reply(200, {"held": True})
        if self.command == "GET" and self.path == "/__qa/state":
            return self.reply(200, {"uploads": self.server.uploads})
        if self.command == "GET" and self.path == "/__qa/component":
            raw = (Path(__file__).parent / "fixtures/composer-image-owner-browser.html").read_bytes()
            raw = raw.replace(b"/app/web/", b"/")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(raw)))
            self.end_headers()
            self.wfile.write(raw)
            return
        keyed = re.fullmatch(r"/api/v1/projects/default/sessions/[^/]+/attachments/[0-9a-f]{32}", self.path)
        if self.command != "PUT" or not keyed:
            return super().proxy()
        body = self.rfile.read(int(self.headers["Content-Length"]))
        upstream = http.client.HTTPConnection("127.0.0.1", self.server.native, timeout=10)
        try:
            headers = dict(self.headers)
            headers["Host"] = f"127.0.0.1:{self.server.native}"
            if headers.get("Origin") == f"http://127.0.0.1:{self.server.server_port}":
                headers["Origin"] = f"http://127.0.0.1:{self.server.native}"
            upstream.request("PUT", self.path, body, headers)
            response = upstream.getresponse()
            raw = response.read()
            assert response.status == 201, raw
            held = self.path.split("/")[6] == self.server.owner
            record = {"path": self.path, "held": held, "status": response.status,
                "sha256": hashlib.sha256(body).hexdigest(), "saved_at": time.monotonic()}
            with self.server.lock:
                self.server.uploads.append(record)
            self.send_response(response.status)
            for key, value in response.getheaders():
                if key.lower() not in ("content-length", "connection", "transfer-encoding"):
                    self.send_header(key, value)
            self.send_header("Content-Length", str(len(raw)))
            self.end_headers()
            self.wfile.flush()
            if held:
                self.server.release.wait(120)
            record["released_at"] = time.monotonic()
            self.wfile.write(raw)
        finally:
            upstream.close()

    do_GET = do_POST = do_PUT = do_PATCH = do_DELETE = proxy


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--packed", type=Path, required=True)
    parser.add_argument("--directory", type=Path, required=True)
    args = parser.parse_args()
    base = args.directory.resolve()
    assert not base.exists(), "Choose a fresh isolated directory"
    base.mkdir(parents=True)
    packed = args.packed.resolve()
    native, port = site(base, "native", packed)
    model = ThreadingHTTPServer(("127.0.0.1", 0), Model)
    proxy = ThreadingHTTPServer(("127.0.0.1", 0), Proxy)
    proxy.native = port
    proxy.lock = threading.Lock()
    proxy.release = threading.Event()
    proxy.stopped = threading.Event()
    proxy.uploads = []
    proxy.tunnels = {}
    proxy.websockets = []
    proxy.phase = None
    proxy.phases = []
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
        tasks = [call("POST", "/api/v1/sessions", {"project_id": "default",
            "title": title, "model_id": "queue-fixture", "reasoning_effort": "none",
            "permission_profile": "balanced"}, 201) for title in ("Image owner A", "Image owner B")]
        proxy.owner = tasks[0]["id"]
        state = {"url": f"http://127.0.0.1:{proxy.server_port}/#/projects/default/sessions/{proxy.owner}",
            "native_port": port, "proxy_port": proxy.server_port,
            "session_ids": [row["id"] for row in tasks], "stop_file": str(base / "stop")}
        (base / "state.json").write_text(json.dumps(state), encoding="utf-8")
        print(json.dumps(state), flush=True)
        deadline = time.monotonic() + 600
        while not (base / "stop").exists() and time.monotonic() < deadline:
            assert process.poll() is None, "Owned fixture exited"
            time.sleep(.1)
        proof = {"uploads": proxy.uploads, "model_calls": Model.calls,
            "websockets": proxy.websockets}
        for row in tasks:
            identifier = row["id"]
            path = "/api/v1/projects/default/sessions/" + identifier
            directory = base / "home/sessions/default" / identifier / "attachments"
            proof[identifier] = {"draft": call("GET", path + "/draft"),
                "queue": call("GET", path + "/queue"), "events": session_events(port, path),
                "files": {file.name: hashlib.sha256(file.read_bytes()).hexdigest()
                    for file in directory.glob("*") if file.is_file()}}
        (base / "proof.json").write_text(json.dumps(proof, indent=2, ensure_ascii=False), encoding="utf-8")
    finally:
        proxy.release.set()
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
