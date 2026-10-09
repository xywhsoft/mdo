"""Loopback MCP peer: one real HTTP failure per target, then normal recovery.

No external access or credentials. The optional hold-auth/release-auth files
hold one retry's initialization so the packed page can be inspected while busy.
"""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import threading
import time


class Peer(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    lock = threading.Lock()
    failed = set()
    root = None
    stopping = threading.Event()

    def log_message(self, *_args): pass

    def handle(self):
        try: super().handle()
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError): pass

    def reply(self, status, body=b"", content_type="application/json"):
        if isinstance(body, dict): body = json.dumps(body).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        name = self.path.rsplit("/", 1)[-1]
        request = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        method, request_id = request["method"], request.get("id")
        status = 202 if method == "notifications/initialized" else 200
        fail = False
        with self.lock:
            if name not in self.failed and method == "initialize":
                self.failed.add(name); fail = True
        if fail:
            status = dict(auth=401, permission=403, limit=429, service=503)[name]
            body = {"jsonrpc": "2.0", "id": request_id,
                    "error": {"code": -32020, "message": "qa-private-http-marker"}}
            if name == "permission": body["id"] = None
            if name == "service": body = b"{qa-private-http-marker"
        elif method == "initialize":
            if name == "auth" and (self.root / "hold-auth").exists():
                (self.root / "auth-held.json").write_text('{"held":true}\n', encoding="utf-8")
                deadline = time.monotonic() + 45
                while not self.stopping.is_set() and not (self.root / "release-auth").exists() and time.monotonic() < deadline:
                    time.sleep(.05)
            body = {"jsonrpc": "2.0", "id": request_id, "result": {
                "protocolVersion": "2025-11-25", "capabilities": {"tools": {}}}}
        elif method == "tools/list":
            body = {"jsonrpc": "2.0", "id": request_id, "result": {"tools": []}}
        else:
            body = b""
        with self.lock:
            with (self.root / "peer-methods.jsonl").open("a", encoding="utf-8") as output:
                output.write(json.dumps(dict(target=name, method=method, status=status)) + "\n")
        self.reply(status, body)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, required=True)
    args = parser.parse_args(); Peer.root = args.directory.resolve()
    assert (Peer.root / "state.json").is_file(), "start the disposable packed fixture first"
    server = ThreadingHTTPServer(("127.0.0.1", 0), Peer); server.daemon_threads = True
    thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
    (Peer.root / "peer.json").write_text(json.dumps(dict(port=server.server_port)) + "\n", encoding="utf-8")
    print("loopback HTTP peer ready", flush=True)
    try:
        deadline = time.monotonic() + 1200
        while not (Peer.root / "stop").exists() and time.monotonic() < deadline: time.sleep(.2)
    finally:
        Peer.stopping.set(); server.shutdown(); server.server_close(); thread.join(timeout=2)


if __name__ == "__main__": main()
