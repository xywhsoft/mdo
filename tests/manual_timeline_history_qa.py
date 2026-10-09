"""Disposable packed page proxy for one bounded late-history response.

The upstream is the existing loopback model fixture, with its normal APIs and
separate Home. Creating hold-before holds one already-produced history body;
release-before releases it. No product switch or public fault route is added.
"""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import http.client
import json
import os
from pathlib import Path
import subprocess
import sys
import threading
import time
from urllib.parse import parse_qs, urlsplit

ROOT = Path(__file__).resolve().parents[1]


class Proxy(BaseHTTPRequestHandler):
    root = None
    upstream = 0
    held = False
    lock = threading.Lock()
    quitting = threading.Event()
    protocol_version = "HTTP/1.1"

    def log_message(self, *_): pass

    def handle(self):
        try: super().handle()
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError): pass

    def forward(self):
        if self.path == "/__fixture/navigation-cancel":
            raw = (ROOT / "tests/fixtures/conversation-navigation-cancel-browser.html").read_bytes()
            self.send_response(200); self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(raw))); self.end_headers()
            self.wfile.write(raw); return
        # WebSocket is deliberately unavailable here; the production client
        # uses its existing quiet, bounded HTTP replay fallback.
        if urlsplit(self.path).path == "/api/v1/live":
            self.send_response(503); self.send_header("Content-Length", "0")
            self.end_headers(); return
        connection = http.client.HTTPConnection("127.0.0.1", self.upstream, timeout=15)
        body = self.rfile.read(int(self.headers.get("Content-Length", "0")))
        headers = {name: value for name, value in self.headers.items()
                   if name.lower() not in ("connection", "transfer-encoding")}
        headers["Connection"] = "close"
        held = False
        try:
            target = self.path.removeprefix("/app/web") if self.path.startswith("/app/web/") else self.path
            connection.request(self.command, target, body, headers)
            response = connection.getresponse(); raw = response.read()
            parts = urlsplit(self.path)
            query = parse_qs(parts.query)
            with self.lock:
                if not type(self).held and (self.root / "hold-before").exists() and \
                        parts.path.endswith("/conversation") and int(query.get("before", [0])[0]) > 0:
                    type(self).held = held = True
            if held:
                record = {"path": self.path, "status": response.status,
                          "response": json.loads(raw), "model_requests": 0}
                (self.root / "held-body.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
                deadline = time.monotonic() + 45
                while not self.quitting.is_set() and not (self.root / "release-before").exists() and time.monotonic() < deadline:
                    time.sleep(.05)
            self.send_response(response.status)
            for name, value in response.getheaders():
                if name.lower() not in ("content-length", "transfer-encoding", "connection"):
                    self.send_header(name, value)
            self.send_header("Content-Length", str(len(raw)))
            self.end_headers(); self.wfile.write(raw)
            if held: (self.root / "held-send.txt").write_text("sent", encoding="utf-8")
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            if held: (self.root / "held-send.txt").write_text("peer closed", encoding="utf-8")
        finally: connection.close()

    do_GET = do_POST = do_PUT = do_PATCH = do_DELETE = do_HEAD = forward


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--packed", type=Path, required=True)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--duration", type=int, default=1200)
    args = parser.parse_args(); base = args.directory.resolve()
    if base.exists(): raise RuntimeError("Choose a new disposable directory")
    base.mkdir(parents=True); upstream = base / "upstream"
    log = (base / "fixture.log").open("wb")
    child = subprocess.Popen([sys.executable, str(ROOT / "tests/manual_conversation_retry_qa.py"),
        "--packed", str(args.packed.resolve()), "--directory", str(upstream),
        "--duration", str(max(1, min(args.duration, 1800)))], cwd=ROOT,
        env=dict(os.environ, USE_WEBVIEW="0"), stdout=log, stderr=log,
        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    server = None; thread = None
    try:
        deadline = time.monotonic() + 40
        while not (upstream / "state.json").exists():
            assert child.poll() is None, "upstream exited; inspect fixture.log"
            assert time.monotonic() < deadline, "upstream startup expired"
            time.sleep(.05)
        native = json.loads((upstream / "state.json").read_text(encoding="utf-8"))
        Proxy.root = base; Proxy.upstream = native["port"]
        server = ThreadingHTTPServer(("127.0.0.1", 0), Proxy)
        thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
        state = dict(native, upstream_port=native["port"], port=server.server_port,
            url=native["url"].replace(f':{native["port"]}/', f':{server.server_port}/'))
        (base / "state.json").write_text(json.dumps(state, indent=2) + "\n", encoding="utf-8")
        print(state["url"], flush=True)
        deadline = time.monotonic() + max(1, min(args.duration, 1800))
        while child.poll() is None and not (base / "stop").exists() and time.monotonic() < deadline:
            time.sleep(.2)
    finally:
        Proxy.quitting.set()
        if server: server.shutdown(); server.server_close()
        if thread: thread.join(timeout=2)
        if upstream.exists(): (upstream / "stop").touch()
        try: child.wait(timeout=20)
        except subprocess.TimeoutExpired: child.terminate(); child.wait(timeout=10)
        log.close()
    assert child.returncode == 0, "upstream failed; inspect fixture.log"


if __name__ == "__main__": main()
