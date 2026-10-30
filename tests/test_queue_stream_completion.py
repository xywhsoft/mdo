"""Packed queue regression: delayed HTTP framing after an SSE terminal event.

Runs copied packed binaries with private empty Homes and loopback-only models.
"""
import argparse
import json
import os
from pathlib import Path
import sys
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
from test_packed_home_lease import site, start, stop, wait_bootstrap, request, release_packed_copies
from test_packed_queue_start_recovery import configure_model


def check(packed, delay):
    timeline = {}
    lock = threading.Lock()
    calls = []
    class Model(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"
        def log_message(self, *_): pass
        def handle(self):
            try: super().handle()
            except (ConnectionError, OSError): pass
        def do_POST(self):
            body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
            with lock:
                number = len(calls) + 1
                calls.append(time.monotonic())
                timeline[f"model{number}_request"] = calls[-1]
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Transfer-Encoding", "chunked")
            self.end_headers()
            chunks = [
                {"id":"probe", "model":body["model"], "choices":[{"index":0,"delta":{"role":"assistant","content":"Done."},"finish_reason":None}]},
                {"id":"probe", "model":body["model"], "choices":[{"index":0,"delta":{},"finish_reason":"stop"}],"usage":{"prompt_tokens":20,"completion_tokens":2}},
            ]
            try:
                for obj in chunks:
                    self.chunk(("data: " + json.dumps(obj) + "\n\n").encode())
                self.chunk(b"data: [DONE]\n\n")
                timeline[f"model{number}_sse_done"] = time.monotonic()
                if number == 1: time.sleep(delay)
                self.wfile.write(b"0\r\n\r\n"); self.wfile.flush()
                timeline[f"model{number}_http_end"] = time.monotonic()
            except (ConnectionError, OSError): pass
        def chunk(self, data):
            self.wfile.write(f"{len(data):x}\r\n".encode() + data + b"\r\n")
            self.wfile.flush()
    server = ThreadingHTTPServer(("127.0.0.1", 0), Model)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    with tempfile.TemporaryDirectory(prefix="queue-tail-", dir=ROOT / ".build") as raw:
        base = Path(raw); runtime, port = site(base, "runtime", packed)
        env = {**os.environ, "USERPROFILE":str(base), "HOME":str(base), "USE_WEBVIEW":"0", "MDO_QUEUE_FIXTURE_KEY":"local-fixture-only"}
        process = start(runtime, packed, base / "home", env)
        def call(method, path, body=None, expected=200):
            status, reply = request(port, method, path, body)
            assert status == expected, (status, reply)
            return reply["data"]
        try:
            _, boot = wait_bootstrap(process, port, runtime / "packed.log")
            assert boot["data"]["ready"], boot
            configure_model(port, f"http://127.0.0.1:{server.server_port}/v1")
            session = call("POST", "/api/v1/sessions", {"project_id":"default", "title":"queue-tail-probe", "agent_id":"mdo.default", "model_id":"queue-fixture", "protocol":"openai-chat-completions", "reasoning_effort":"none", "max_output_tokens":1024}, 201)
            path = f'/api/v1/projects/default/sessions/{session["id"]}'
            first = call("POST", path + "/runs", {"prompt":"first"}, 202)
            call("POST", path + "/queue", {"id":"a"*32,"text":"second","attachments":[],"first":False,"managed":True},201)
            end = time.monotonic()+15
            while time.monotonic() < end:
                runs = call("GET", "/api/v1/runs")["items"]
                now = time.monotonic()
                for run in runs:
                    if run["id"] == first["id"] and run["terminal"]:
                        timeline.setdefault("first_terminal_observed", now)
                if len(calls)>=2 and all(r["terminal"] for r in runs): break
                time.sleep(.05)
            assert len(calls)==2, timeline
            origin = timeline["model1_sse_done"]
            result = {"packed":str(packed),"tail_delay_seconds":delay,"relative_ms":{k:round((v-origin)*1000,1) for k,v in timeline.items()},"model_calls":len(calls)}
            print(json.dumps(result), flush=True)
            return result
        finally:
            stop(process);release_packed_copies(runtime/packed.name)
            server.shutdown();server.server_close()

if __name__ == "__main__":
    parser=argparse.ArgumentParser();parser.add_argument("--packed",type=Path,required=True);parser.add_argument("--record",type=Path)
    args=parser.parse_args()
    results=[check(args.packed.resolve(),d) for d in (0,4)]
    for row in results:
        assert row["relative_ms"]["first_terminal_observed"] < 2000, row
        assert row["relative_ms"]["model2_request"] < 2000, row
    if args.record: args.record.write_text(json.dumps(results,indent=2),encoding="utf-8")
