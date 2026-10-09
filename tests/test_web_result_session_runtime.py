"""One offline search-to-read conversation through real TCC and session journals."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from test_interrupt_runtime import free_port, start_host, stop_host, request
from test_web_runtime import ROOT

def run(host: Path, *, page_recovery: bool = False, page_denied: bool = False,
        search_recovery: bool = False, search_denied: bool = False,
        output_file: Path | None = None) -> None:
    denied = page_denied or search_denied
    class Handler(BaseHTTPRequestHandler):
        calls = 0
        page_calls = 0
        def log_message(self, *_): pass
        def do_GET(self):
            if self.path != "/page":
                self.send_response(404); self.send_header("Content-Length", "0")
                self.end_headers(); return
            Handler.page_calls += 1
            body = ("网页恢复 Пример 🌍\n" * 1000).encode()
            self.send_response(403 if page_denied else 503 if Handler.page_calls <= 2 else 200)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers(); self.wfile.write(body)
        def do_POST(self):
            assert self.path == "/api/v1/search/requests"
            assert self.headers.get("Authorization") == "Bearer probe-secret"
            args = json.loads(self.rfile.read(int(self.headers["Content-Length"]))); Handler.calls += 1
            if search_denied or (search_recovery and Handler.calls <= 2):
                body=json.dumps({'code':429,'message':'HOSTILE_SECRET_BODY','data':{'error':{
                    'code':'daily_limit' if search_denied else 'server_busy',
                    'retry_safe':not search_denied,'dispatched':False,
                    'retry_after_ms':86400000 if search_denied else 100}}}).encode()
                self.send_response(429);self.send_header('Content-Length',str(len(body)))
                self.end_headers();self.wfile.write(body);return
            body = json.dumps({"code": 0, "message": "", "data": {
                "provider": "bocha", "request_id": args["request_id"],
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
            (workspace / "input.txt").write_text("bounded recovery fixture", encoding="utf-8")
            shutil.copy2(ROOT / "tests/fixtures/web-result-session.c", site / "web-result-session.c")
            if page_recovery or denied:
                fixture = site / "web-result-session.c"
                text = fixture.read_text(encoding="utf-8")
                if not search_denied:
                    text = text.replace('const char* Name = "web_search";', 'const char* Name = "web_open";')
                    original = r'strcpy(Arguments, "{\"query\":\"fixture-0\",\"count\":10}");'
                    assert text.count(original) == 1
                    arguments = json.dumps({"url": f"http://127.0.0.1:{server.server_port}/page"})
                    text = text.replace(original, 'strcpy(Arguments, ' + json.dumps(arguments) + ');')
                if denied:
                    # A denied page gives one failed tool result to the model,
                    # which can finish normally without trying to read a file.
                    text = text.replace('if (g_WebSessionCalls == 2u) {', 'if (false) {')
                    text = text.replace('g_WebSessionCalls == 3u', 'g_WebSessionCalls == 2u')
                    text = text.replace('g_WebSessionCalls != 3u', 'g_WebSessionCalls != 2u')
                    text = text.replace('Search and file read complete',
                        'Search quota exhausted; wait for the daily reset' if search_denied else
                        'Page access denied; use another source')
                fixture.write_text(text, encoding="utf-8")
                defaults = site / "default-home/config/defaults.json"
                value = json.loads(defaults.read_text(encoding="utf-8"))
                value["settings"]["web"].update(allow_http=True, allow_private_networks=True)
                defaults.write_text(json.dumps(value), encoding="utf-8")
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
                assert Handler.calls == (0 if page_recovery or page_denied else 3 if search_recovery else 1), Handler.calls
                assert 'HOSTILE_SECRET_BODY' not in output, output
                if page_recovery or page_denied:
                    assert Handler.page_calls == (1 if page_denied else 3), Handler.page_calls
                session = home / "sessions/default/web-result-session"
                events = [json.loads(line) for line in (session / "ui-events.jsonl").read_text(encoding="utf-8").splitlines()]
                done = [event for event in events if event["kind"] == 6]
                assert len(done) == (1 if denied else 2), done
                assert all(event["success"] is (not denied) for event in done), done
                assert any(event["kind"] == 10 and event["success"] for event in events), events[-4:]
                status, reply = request(port, "GET", "/api/v1/projects/default/sessions/web-result-session/recovery")
                assert status == 200 and reply["data"]["resume_required"] is False, reply
                artifacts = list((session / "artifacts").rglob("*.md"))
                assert len(artifacts) == (0 if denied else 1)
                if not denied:
                    assert len(artifacts[0].read_bytes()) > 12000
                receipts = list((session / "artifacts/completed").glob("*.json"))
                # Completed error results also retain one receipt; this is not
                # a successful page artifact or an extra retry completion.
                assert len(receipts) == 1
                for session_id, automatic in (("read-stop-session", False), ("read-io-session", True)):
                    status, reply = request(port, "GET", f"/api/v1/projects/default/sessions/{session_id}/recovery")
                    data = reply["data"]
                    assert status == 200 and data["resume_required"] and data["automatic_resume"] is automatic, reply
                    assert len(data["items"]) == 1 and data["items"][0]["automatic_retry_safe"], data
                if output_file:
                    output_file.parent.mkdir(parents=True, exist_ok=True)
                    output_file.write_text(json.dumps({
                        "mode": "search_quota" if search_denied else "search_recovery" if search_recovery else
                            "denied" if page_denied else "page_recovery" if page_recovery else "search",
                        "page_requests": Handler.page_calls, "search_requests": Handler.calls,
                        "tool_completions": [{"kind": e["kind"], "success": e["success"]} for e in done],
                        "agent_completed_successfully": any(e["kind"] == 10 and e["success"] for e in events),
                        "artifacts": [{"bytes": len(p.read_bytes()), "sha256": hashlib.sha256(p.read_bytes()).hexdigest()}
                                      for p in artifacts],
                    }, indent=2) + "\n", encoding="utf-8")
            finally: stop_host(process)
    finally:
        server.shutdown(); server.server_close(); thread.join(timeout=2)
    print("PASS search quota returned once; agent continued and completed" if search_denied else
          "PASS search quietly recovered; one result; agent completed" if search_recovery else
          "PASS denied page returned once; agent continued and completed" if page_denied else
          "PASS real page retry/read/model ledger/one completion/no intermediate failures" if page_recovery else
          "PASS real search/read/model ledger/UI completion/recovery pipeline")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__); parser.add_argument("--host", type=Path, required=True)
    modes=parser.add_mutually_exclusive_group()
    modes.add_argument("--page-recovery", action="store_true")
    modes.add_argument("--page-denied", action="store_true")
    modes.add_argument("--search-recovery", action="store_true")
    modes.add_argument("--search-denied", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    run(args.host.resolve(), page_recovery=args.page_recovery, page_denied=args.page_denied,
        search_recovery=args.search_recovery, search_denied=args.search_denied,
        output_file=args.output)
