"""Real packed app confirmation/cancel/navigation using a temporary session."""
import argparse
import http.client
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import subprocess
import tempfile
import threading

from manual_live_probe_qa import Proxy as LiveProxy
from test_api_runtime import request
from test_packed_home_lease import site, start, stop, wait_bootstrap, release_packed_copies
from test_sidebar_focus_browser import ResultParser

ROOT = Path(__file__).resolve().parents[1]


class Proxy(BaseHTTPRequestHandler):
    tunnel = LiveProxy.tunnel
    def log_message(self, *_): pass

    def proxy(self):
        if self.headers.get("Upgrade", "").lower() == "websocket": return self.tunnel()
        if self.path.startswith("/__qa/browser"):
            fixture = "session-trash-packed-browser.html" if self.server.action == "trash" else "session-purge-packed-browser.html"
            raw = (ROOT / "tests/fixtures" / fixture).read_bytes()
            return self.reply(200, raw, "text/html; charset=utf-8")
        if self.path == "/__qa/proof":
            status, _, raw = request(self.server.native, "GET", self.server.session_path)
            data = json.loads(raw).get("data", {})
            return self.reply(200, json.dumps({"purges": self.server.purges, "trashes": self.server.trashes,
                "session_status": status, "status": data.get("status")}).encode(), "application/json")
        size = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(size) if size else None
        headers = {key: value for key, value in self.headers.items() if key.lower() not in ("host", "connection", "content-length")}
        if "Origin" in headers: headers["Origin"] = f"http://127.0.0.1:{self.server.native}"
        if self.command == "POST" and self.path.endswith("/purge"): self.server.purges += 1
        if self.command == "DELETE" and self.path == self.server.session_path: self.server.trashes += 1
        upstream = http.client.HTTPConnection("127.0.0.1", self.server.native, timeout=8)
        try:
            upstream.request(self.command, self.path, body, headers)
            response = upstream.getresponse(); raw = response.read()
            self.send_response(response.status)
            for key, value in response.getheaders():
                if key.lower() not in ("connection", "transfer-encoding", "content-length"): self.send_header(key, value)
            self.send_header("Content-Length", str(len(raw))); self.end_headers(); self.wfile.write(raw)
        finally: upstream.close()

    def reply(self, status, raw, kind):
        self.send_response(status); self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(raw))); self.end_headers(); self.wfile.write(raw)

    do_GET = do_POST = do_PUT = do_PATCH = do_DELETE = do_HEAD = proxy


def run(packed, browser, action="purge", menu="header", unselected=False):
    with tempfile.TemporaryDirectory(prefix="mdo-session-purge-browser-") as temporary:
        base = Path(temporary); native, port = site(base, "native", packed)
        home = base / "home"
        process = start(native, packed, home, dict(os.environ, MDO_HOME=str(home)))
        proxy = None
        try:
            wait_bootstrap(process, port, native / "packed.log")
            status, _, raw = request(port, "POST", "/api/v1/sessions", body=json.dumps({"project_id": "default", "title": "Disposable browser delete"}).encode(), headers={"Content-Type": "application/json"})
            assert status == 201, raw
            item = json.loads(raw)["data"]; session_path = "/api/v1/projects/default/sessions/" + item["id"]
            selected_id = item["id"]
            if action == "purge":
                tag = request(port, "GET", session_path)[1]["etag"]
                assert request(port, "DELETE", session_path, headers={"If-Match": tag})[0] == 200
            if unselected:
                status, _, raw = request(port, "POST", "/api/v1/sessions", body=json.dumps({"project_id": "default", "title": "Keep selected"}).encode(), headers={"Content-Type": "application/json"})
                assert status == 201, raw
                selected_id = json.loads(raw)["data"]["id"]
            proxy = ThreadingHTTPServer(("127.0.0.1", 0), Proxy)
            proxy.native = port; proxy.session_path = session_path; proxy.purges = 0; proxy.trashes = 0; proxy.action = action
            proxy.lock = threading.Lock(); proxy.websockets = []; proxy.tunnels = {}; proxy.stopped = threading.Event()
            thread = threading.Thread(target=proxy.serve_forever, daemon=True); thread.start()
            command = [str(browser), "--headless=new", "--disable-gpu", "--no-first-run", "--disable-extensions",
                "--disable-background-networking", "--user-data-dir=" + str(base / "browser"), "--dump-dom",
                "--virtual-time-budget=16000", f"http://127.0.0.1:{proxy.server_port}/__qa/browser?session=" + item["id"] + "&selected=" + selected_id + "&menu=" + menu]
            output = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=40,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
            parser = ResultParser(); parser.feed(output.stdout.decode("utf-8", errors="replace"))
            proof = json.loads(parser.result); assert proof["passed"], proof
            assert (home / "sessions/default" / item["id"]).exists() == (action == "trash")
            print(json.dumps(proof, ensure_ascii=False))
        finally:
            if proxy:
                proxy.stopped.set(); proxy.shutdown(); proxy.server_close(); thread.join(timeout=3)
            stop(process); release_packed_copies(native / packed.name)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--packed", type=Path, required=True)
    parser.add_argument("--browser", type=Path, default=Path("C:/Program Files/Google/Chrome/Application/chrome.exe"))
    parser.add_argument("--action", choices=("purge", "trash"), default="purge")
    parser.add_argument("--menu", choices=("header", "sidebar"), default="header")
    parser.add_argument("--unselected", action="store_true")
    args = parser.parse_args(); run(args.packed.resolve(), args.browser.resolve(), args.action, args.menu, args.unselected)
