"""Run an isolated packed settings page with one delayed/failed request.

POST /__qa/arm?mode=hold, hold-fail, fail, or conflict affects one bounded request.
hold-fail releases a delayed 503 instead of the captured upstream response.
An optional path= selects a catalog GET, models PUT, or models preview POST.
GET /__qa/control reports its arrival and bounded request counts.
GET /__qa/session-menu serves the bounded menu/indicator component fixture with
the exact packed assets; it does not change sessions or runs on the server.
POST /__qa/release releases the delayed reply. No model requests are made.
"""
from __future__ import annotations

import argparse
import http.client
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import threading
from urllib.parse import parse_qs, urlsplit

from test_api_runtime import ROOT, free_port, wait_ready
from test_interrupt_runtime import stop_host

SETTINGS = "/api/v1/settings/settings"
READ_PATHS = {"/api/v1/" + name for name in (
    "models/config", "modules", "skills", "mcp", "permissions", "storage",
    "diagnostics", "migrations/legacy")}


WRITE_PATHS = {"/api/v1/settings/models": "PUT",
               "/api/v1/settings/models/preview": "POST"}


class SettingsProxy(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *_args):
        pass

    def do_GET(self):
        self.forward()

    def do_POST(self):
        self.forward()

    def do_PUT(self):
        self.forward()

    def do_PATCH(self):
        self.forward()

    def do_DELETE(self):
        self.forward()

    def reply(self, value, status=200):
        payload = json.dumps(value).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def forward(self):
        target = urlsplit(self.path)
        if self.command == "GET" and target.path == "/__qa/session-menu":
            payload = (ROOT / "tests/fixtures/session-menu-viewport-browser.html").read_bytes()
            payload = payload.replace(b"/app/web/", b"/")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(payload)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(payload)
            return
        if target.path == "/__qa/control":
            with self.server.lock:
                state = {"mode": self.server.mode, "target": self.server.target,
                         "arrived": self.server.arrived,
                         "released": self.server.release.is_set(),
                         "counts": dict(self.server.counts), "patches": list(self.server.patches)}
            return self.reply(state)
        if target.path == "/__qa/arm" and self.command == "POST":
            query = parse_qs(target.query)
            mode = query.get("mode", [""])[0]
            assert mode in ("hold", "hold-fail", "fail", "conflict")
            path = query.get("path", [SETTINGS])[0]
            assert path == SETTINGS or path in READ_PATHS or path in WRITE_PATHS
            with self.server.lock:
                self.server.release.set()
                self.server.release = threading.Event()
                self.server.mode = mode
                self.server.target = ("PATCH" if path == SETTINGS else
                                      WRITE_PATHS.get(path, "GET"), path)
                self.server.arrived = False
            return self.reply({"armed": mode})
        if target.path == "/__qa/release" and self.command == "POST":
            self.server.release.set()
            return self.reply({"released": True})

        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length) if length else None
        mode = ""
        gate = None
        with self.server.lock:
            key = f"{self.command} {target.path}"
            self.server.counts[key] = self.server.counts.get(key, 0) + 1
            if self.command == "PATCH" and target.path == SETTINGS:
                self.server.patches.append(json.loads(body)["patch"])
            if (self.command, target.path) == self.server.target:
                mode = self.server.mode
                self.server.mode = ""
                if mode:
                    self.server.arrived = True
                    gate = self.server.release
        if mode in ("fail", "conflict"):
            return self.reply({"ok": False, "error": {"code": "qa_save_failed",
                "message": "Bounded fixture request failure"}},
                412 if mode == "conflict" else 503)
        headers = {key: value for key, value in self.headers.items()
                   if key.lower() not in ("host", "connection", "content-length")}
        connection = http.client.HTTPConnection("127.0.0.1", self.server.upstream, timeout=8)
        try:
            connection.request(self.command, self.path, body=body, headers=headers)
            response = connection.getresponse()
            payload = response.read()
            status, result_headers = response.status, response.getheaders()
        finally:
            connection.close()
        if mode in ("hold", "hold-fail") and not gate.wait(40):
            print("Settings reply gate reached its bounded timeout", flush=True)
        if mode == "hold-fail":
            return self.reply({"ok": False, "error": {"code": "qa_delayed_read_failed",
                "message": "Bounded delayed fixture failure"}}, 503)
        self.send_response(status)
        for key, value in result_headers:
            if key.lower() not in ("content-length", "connection", "transfer-encoding"):
                self.send_header(key, value)
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        try:
            self.wfile.write(payload)
        except (BrokenPipeError, ConnectionResetError):
            pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--packed-path", type=Path, required=True)
    parser.add_argument("--metadata-output", type=Path,
        default=ROOT / ".build/qa-settings-autosave-live.json")
    args = parser.parse_args()
    base = Path(tempfile.mkdtemp(prefix="qa-settings-live-", dir=ROOT / ".build"))
    exe = base / ("mdo.exe" if os.name == "nt" else "mdo")
    shutil.copy2(args.packed_path, exe)
    port = free_port()
    (base / "xs.json").write_text(json.dumps({"services": [{"enabled": True,
        "class": "http", "name": "mdo", "ip": "127.0.0.1", "port": port,
        "host_default": {"enabled": True, "name": "mdo", "path": "web",
            "devlang": "c", "devfile": "generated/mdo_unity.c"}}]}), encoding="utf-8")
    environment = dict(os.environ, USERPROFILE=str(base), HOME=str(base),
        MDO_ORNITH_API_KEY="bounded-settings-fixture-key",
        MDO_ORNITH_RESPONSES_URL="http://127.0.0.1:1/v1")
    process = None
    proxy = None
    try:
        with (base / "packed.log").open("wb") as log:
            process = subprocess.Popen([str(exe), "--", "--home", str(base / "mdo-home")],
                cwd=base, env=environment, stdout=log, stderr=subprocess.STDOUT,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        wait_ready(port, process)
        proxy = ThreadingHTTPServer(("127.0.0.1", 0), SettingsProxy)
        proxy.daemon_threads = True
        proxy.upstream = port
        proxy.lock = threading.Lock()
        proxy.release = threading.Event()
        proxy.mode, proxy.arrived = "", False
        proxy.target = ("PATCH", SETTINGS)
        proxy.counts, proxy.patches = {}, []
        threading.Thread(target=proxy.serve_forever, daemon=True).start()
        metadata = {"base": str(base), "port": proxy.server_address[1], "upstream": port}
        args.metadata_output.write_text(json.dumps(metadata), encoding="utf-8")
        print("READY " + json.dumps(metadata), flush=True)
        input("Press Enter to stop isolated settings fixture.\n")
    finally:
        if proxy:
            proxy.release.set()
            proxy.shutdown()
            proxy.server_close()
        stop_host(process)


if __name__ == "__main__":
    main()
