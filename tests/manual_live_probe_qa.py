"""Real packed WebSocket reconnect with one delayed read-token response.

Open the first URL, GET /__qa/live-arm to drop its real WebSocket and hold
the next project-purge-intent reply for twelve seconds. Check that the draft
survives and the connection reopens, then send the saved draft and a follow-up
in the second task. Standard-library loopback tunnel; no product QA injection.
Create the printed stop file to save proof and stop only this copied program.
"""
import argparse
from collections import Counter
from http.server import ThreadingHTTPServer
import json
import os
from pathlib import Path
import select
import socket
import threading
import time

from manual_resource_deadline_qa import Proxy as SlowProxy
from manual_run_admission_qa import Model
from test_api_runtime import request, session_events
from test_packed_home_lease import site, start, stop, wait_bootstrap
from test_packed_queue_start_recovery import configure_model


class Proxy(SlowProxy):
    def proxy(self):
        if self.command == "GET" and self.path == "/__qa/live-arm":
            with self.server.lock:
                assert self.server.tunnels, "Open a real live connection before arming"
                self.server.resources = {"/api/v1/project-purge-intent"}
                self.server.reads = {"/api/v1/project-purge-intent": []}
                self.server.armed_at = time.monotonic()
                pairs = list(self.server.tunnels.values())
            for pair in pairs:
                for channel in pair:
                    try: channel.shutdown(socket.SHUT_RDWR)
                    except OSError: pass
            return self.reply(200, {"dropped": len(pairs)})
        if self.headers.get("Upgrade", "").lower() == "websocket":
            return self.tunnel()
        return super().proxy()

    def tunnel(self):
        upstream = socket.create_connection(("127.0.0.1", self.server.native), timeout=3)
        record = {"started": time.monotonic(), "status": None,
                  "upstream_bytes": 0, "downstream_bytes": 0}
        with self.server.lock:
            self.server.websockets.append(record); key = len(self.server.websockets)
        try:
            headers = []
            for name, value in self.headers.items():
                if name.lower() == "host": value = f"127.0.0.1:{self.server.native}"
                if name.lower() == "origin" and value == f"http://127.0.0.1:{self.server.server_port}":
                    value = f"http://127.0.0.1:{self.server.native}"
                headers.append(f"{name}: {value}")
            upstream.sendall((f"GET {self.path} HTTP/1.1\r\n" + "\r\n".join(headers) + "\r\n\r\n").encode())
            received = b""
            while b"\r\n\r\n" not in received:
                block = upstream.recv(4096)
                assert block, "Native WebSocket closed during handshake"
                received += block; assert len(received) <= 65536, "Oversized handshake"
            head, _ = received.split(b"\r\n\r\n", 1)
            status = int(head.split(b" ", 2)[1]); record["status"] = status
            self.connection.sendall(received)
            if status != 101: return
            record["opened"] = time.monotonic(); record["downstream_bytes"] += len(received)
            upstream.settimeout(None); self.connection.settimeout(None)
            with self.server.lock: self.server.tunnels[key] = (self.connection, upstream)
            while not self.server.stopped.is_set():
                ready, _, _ = select.select((self.connection, upstream), (), (), .25)
                for source in ready:
                    block = source.recv(65536)
                    if not block: return
                    destination = upstream if source is self.connection else self.connection
                    destination.sendall(block)
                    field = "upstream_bytes" if source is self.connection else "downstream_bytes"
                    with self.server.lock: record[field] += len(block)
        except (ConnectionResetError, ConnectionAbortedError, BrokenPipeError):
            pass  # Deliberate connection loss and normal cancellation.
        finally:
            self.close_connection = True
            with self.server.lock:
                self.server.tunnels.pop(key, None); record["closed"] = time.monotonic()
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
    proxy.tunnels = {}; proxy.websockets = []; proxy.armed_at = None
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
            for title in ("Live probe", "Live follow-up")]
        first = "/api/v1/projects/default/sessions/" + sessions[0]["id"]
        call("PUT", first + "/draft", {"revision": 0, "text": "LIVE_PROBE_SAVED_DRAFT"})
        print(json.dumps({"urls": [f"http://127.0.0.1:{proxy.server_port}/#/projects/default/sessions/" + item["id"] for item in sessions],
            "proxy_port": proxy.server_port, "native_port": port, "stop_file": str(base / "stop")}), flush=True)
        deadline = time.monotonic() + 600
        while not (base / "stop").exists() and time.monotonic() < deadline:
            assert process.poll() is None, "Native fixture exited"
            time.sleep(.1)
        proof = {"reads": proxy.reads, "writes": proxy.writes, "websockets": proxy.websockets,
            "armed_at": proxy.armed_at, "model_calls": dict(Model.calls), "sessions": []}
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
