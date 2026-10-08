"""One offline search-to-read conversation through real TCC and session journals."""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import shutil
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from test_interrupt_runtime import free_port, start_host, stop_host, request
from test_web_runtime import ROOT

def run(host: Path) -> None:
    class Handler(BaseHTTPRequestHandler):
        calls = 0
        def log_message(self, *_): pass
        def do_POST(self):
            assert self.path == "/api/v1/search"
            assert self.headers.get("Authorization") == "Bearer probe-secret"
            self.rfile.read(int(self.headers["Content-Length"])); Handler.calls += 1
            body = json.dumps({"code": 0, "message": "", "data": {
                "provider": "bocha", "request_id": "0123456789abcdef0123456789abcdef",
                "truncated": False, "count": 10, "results": [{"title": f"搜索 Пример 🌍 {i}",
                    "url": f"https://example.com/{i}", "snippet": "搜索я🌍" * 120,
                    "site": "example.com", "published_at": "2026-10-09"} for i in range(10)]}}, ensure_ascii=False).encode()
            self.send_response(200); self.send_header("Content-Length", str(len(body)))
            self.end_headers(); self.wfile.write(body)
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
    try:
        with tempfile.TemporaryDirectory(prefix="web-result-session-", dir=ROOT / ".build") as raw:
            base = Path(raw); site = base / "site"; home = base / "home"; workspace = base / "project"
            workspace.mkdir(); shutil.copytree(ROOT / "app", site)
            shutil.copy2(ROOT / "tests/fixtures/web-result-session.c", site / "web-result-session.c")
            (site / "probe.c").write_text('#define MDO_ACCOUNT_SERVICE_ORIGIN ' + json.dumps(f"http://127.0.0.1:{server.server_port}") + '\n'
                '#define WEB_SESSION_WORKSPACE ' + json.dumps(str(workspace.resolve())) + '\n'
                '#define ServiceInit WebSessionProductInit\n#include "generated/mdo_unity.c"\n#undef ServiceInit\n'
                '#include "web-result-session.c"\n', encoding="utf-8")
            port = free_port(); config = site / "xs.json"
            config.write_text(json.dumps({"engine": {"workers": 1}, "services": [{"enabled": True,
                "class": "http", "name": "web-session", "ip": "127.0.0.1", "port": port,
                "host_default": {"enabled": True, "name": "mdo", "path": "web", "devlang": "c", "devfile": "probe.c"}}]}))
            log = base / "xs.log"; process = None
            try:
                process = start_host(host, config, home, os.environ.copy(), log, port)
                output = log.read_text(encoding="utf-8", errors="replace")
                assert "web_session_ok=1" in output, output
                assert Handler.calls == 1, Handler.calls
                session = home / "sessions/default/web-result-session"
                events = [json.loads(line) for line in (session / "ui-events.jsonl").read_text(encoding="utf-8").splitlines()]
                done = [event for event in events if event["kind"] == 6]
                assert len(done) == 2 and all(event["success"] for event in done), done
                assert any(event["kind"] == 10 and event["success"] for event in events), events[-4:]
                status, reply = request(port, "GET", "/api/v1/projects/default/sessions/web-result-session/recovery")
                assert status == 200 and reply["data"]["resume_required"] is False, reply
                artifacts = list((session / "artifacts").rglob("*.md"))
                assert len(artifacts) == 1 and len(artifacts[0].read_bytes()) > 12000
                receipts = list((session / "artifacts/completed").glob("*.json"))
                assert len(receipts) == 1
            finally: stop_host(process)
    finally:
        server.shutdown(); server.server_close(); thread.join(timeout=2)
    print("PASS real search/read/model ledger/UI completion/recovery pipeline")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__); parser.add_argument("--host", type=Path, required=True)
    run(parser.parse_args().host.resolve())
