"""Local packed-page fixture for todo, ask, and approval UI review.

Run from the repository root after building mdo.exe. The model endpoint only
binds to localhost and returns one deterministic tool call per marker prompt:
TODO UI, ASK UI, APPROVAL UI, TASK UI, or ARTIFACT UI. The latter reads one
bounded synthetic text file so the normal tool-output artifact path is used.
"""

import argparse
import http.client
import json
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
from test_api_runtime import wait_ready  # noqa: E402
from test_interrupt_runtime import free_port, request, stop_host  # noqa: E402


class Model(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    lock = threading.Lock()
    sent = set()
    verify_recovery = False
    verification_file = None
    slow_seconds = 15
    task_seconds = 12
    artifact_file = None

    def log_message(self, *_args):
        pass

    def do_POST(self):
        if self.path != "/v1/responses":
            self.send_error(404)
            return
        payload = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        wire = json.dumps(payload)
        output = [{"type": "message", "content": [
            {"type": "output_text", "text": "UI fixture completed."}]}]
        if "MARKDOWN UI" in wire:
            output[0]["content"][0]["text"] = (
                "## Markdown QA\n\nSee [Example](https://example.com/guide).\n\n"
                "```c\nint answer(void) { return 42; }\n```"
            )
        with Model.lock:
            slow = "SLOW UI" in wire and "slow" not in Model.sent
            if slow:
                Model.sent.add("slow")
            if "TODO UI" in wire and "todo" not in Model.sent:
                Model.sent.add("todo")
                output = [{"type": "function_call", "call_id": "ui-todo-1",
                           "name": "mdo.todo", "arguments": json.dumps({"items": [
                               {"text": "Inspect UI", "done": True},
                               {"text": "Verify refresh", "done": False}]})}]
            elif "ARTIFACT UI" in wire and "artifact" not in Model.sent:
                Model.sent.add("artifact")
                output = [{"type": "function_call", "call_id": "ui-artifact-1",
                           "name": "read", "arguments": json.dumps({
                               "path": str(Model.artifact_file), "max_lines": 200})}]
            elif "TASK UI" in wire and "task" not in Model.sent:
                Model.sent.add("task")
                output = [{"type": "function_call", "call_id": "ui-task-1",
                           "name": "spawn", "arguments": json.dumps({
                               "argv": [sys.executable, "-c",
                                        f"import time; time.sleep({Model.task_seconds:g}); "
                                        "print('task UI fixture')"]})}]
            elif "ASK UI" in wire and "TASK UI" not in wire and "ask" not in Model.sent:
                Model.sent.add("ask")
                output = [{"type": "function_call", "call_id": "ui-ask-1",
                           "name": "ask_user", "arguments": json.dumps({
                               "question": "Which route should I take?",
                               "options": ["Fast", "Careful"]})}]
            elif "APPROVAL UI" in wire and "approval" not in Model.sent:
                Model.sent.add("approval")
                output = [{"type": "function_call", "call_id": "ui-approval-1",
                           "name": "exec", "arguments": json.dumps({
                               "argv": [sys.executable, "-c",
                                        "print('approval UI fixture')"],
                               "timeout_ms": 5000})}]
            elif (Model.verify_recovery and
                  "Completion verification gate:" in wire and
                  "resume-verify" not in Model.sent):
                Model.sent.add("resume-verify")
                output = [{"type": "function_call", "call_id": "ui-resume-verify-1",
                           "name": "exec", "arguments": json.dumps({
                               "argv": [sys.executable, "-c",
                                        "import pathlib,sys; "
                                        "assert pathlib.Path(sys.argv[1]).read_text("
                                        "encoding='utf-8').startswith('Synthetic workspace')",
                                        str(Model.verification_file)],
                               "timeout_ms": 5000})}]
        if slow:
            time.sleep(Model.slow_seconds)
        body = json.dumps({"id": "resp_ui_fixture", "model": "ling-3.0-tiny",
                           "status": "completed", "output": output,
                           "usage": {"input_tokens": 7, "output_tokens": 3,
                                     "total_tokens": 10}}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


class ModelServer(ThreadingHTTPServer):
    def handle_error(self, _request, _client_address):
        if isinstance(sys.exc_info()[1], (BrokenPipeError, ConnectionResetError)):
            return
        super().handle_error(_request, _client_address)


class BoundedDelayProxy(BaseHTTPRequestHandler):
    """Forward one browser origin with bounded delays for UI race QA."""

    protocol_version = "HTTP/1.1"

    def log_message(self, *_args):
        pass

    def do_GET(self):
        self.forward()

    def do_POST(self):
        self.forward()

    def do_PUT(self):
        self.forward()

    def do_DELETE(self):
        self.forward()

    def do_HEAD(self):
        self.forward()

    def forward(self):
        length = int(self.headers.get("Content-Length", "0"))
        if length < 0 or length > 8 * 1024 * 1024:
            self.send_error(413)
            return
        body = self.rfile.read(length) if length else None
        if self.command == "PUT" and self.path.startswith("/api/v1/approvals/"):
            with self.server.count_lock:
                self.server.approval_puts += 1
                count = self.server.approval_puts
            print(f"QA approval PUT #{count}", flush=True)
            time.sleep(self.server.approval_delay_seconds)
        if (self.command == "POST" and self.path.startswith("/api/v1/projects/")
                and "/sessions/" in self.path and self.path.endswith("/queue")):
            with self.server.count_lock:
                self.server.queue_posts += 1
                count = self.server.queue_posts
            print(f"QA queue POST #{count}", flush=True)
            time.sleep(self.server.queue_delay_seconds)
            if self.server.fail_first_queue and count == 1:
                payload = json.dumps({"ok": False, "error": {
                    "code": "qa_queue_rejected", "message": "Synthetic queue failure"
                }}).encode()
                self.send_response(503)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(payload)))
                self.send_header("Connection", "close")
                self.end_headers()
                self.wfile.write(payload)
                self.close_connection = True
                return
        if (self.command == "POST" and self.path.startswith("/api/v1/projects/")
                and "/sessions/" in self.path and self.path.endswith("/runs")):
            with self.server.count_lock:
                self.server.run_posts += 1
                count = self.server.run_posts
            print(f"QA run POST #{count}", flush=True)
            time.sleep(self.server.run_delay_seconds)
            if self.server.fail_first_run and count == 1:
                payload = json.dumps({"ok": False, "error": {
                    "code": "qa_run_rejected", "message": "Synthetic run failure"
                }}).encode()
                self.send_response(503)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(payload)))
                self.send_header("Connection", "close")
                self.end_headers()
                self.wfile.write(payload)
                self.close_connection = True
                return
        headers = {key: value for key, value in self.headers.items()
                   if key.lower() not in {"host", "connection", "content-length"}}
        headers["Host"] = f"127.0.0.1:{self.server.upstream_port}"
        headers["Connection"] = "close"
        if body is not None:
            headers["Content-Length"] = str(len(body))
        upstream = http.client.HTTPConnection(
            "127.0.0.1", self.server.upstream_port, timeout=30)
        try:
            upstream.request(self.command, self.path, body=body, headers=headers)
            response = upstream.getresponse()
            payload = response.read()
            self.send_response(response.status)
            for key, value in response.getheaders():
                if key.lower() not in {"connection", "content-length",
                                       "transfer-encoding"}:
                    self.send_header(key, value)
            self.send_header("Content-Length", str(len(payload)))
            self.send_header("Connection", "close")
            self.end_headers()
            if self.command != "HEAD":
                self.wfile.write(payload)
            self.close_connection = True
        except (BrokenPipeError, ConnectionResetError):
            pass
        finally:
            upstream.close()


class BoundedDelayProxyServer(ThreadingHTTPServer):
    daemon_threads = True

    def handle_error(self, request, client_address):
        if isinstance(sys.exc_info()[1], (BrokenPipeError, ConnectionResetError)):
            return
        super().handle_error(request, client_address)


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--approval-delay-ms", type=int, default=0,
                    help="delay one approval PUT by 0-5000 ms for manual duplicate-click QA")
parser.add_argument("--queue-delay-ms", type=int, default=0,
                    help="delay queue POSTs by 0-12000 ms for bounded dispatch race QA")
parser.add_argument("--run-delay-ms", type=int, default=0,
                    help="delay run POSTs by 0-5000 ms for composer handoff QA")
parser.add_argument("--fail-first-run", action="store_true",
                    help="reject one run POST before forwarding, for draft recovery QA")
parser.add_argument("--fail-first-queue", action="store_true",
                    help="reject one queue POST before forwarding, for staged draft QA")
parser.add_argument("--slow-ms", type=int, default=15000,
                    help="first SLOW UI model response delay, 0-15000 ms")
parser.add_argument("--task-ms", type=int, default=12000,
                    help="TASK UI background process sleep, 0-30000 ms")
parser.add_argument("--resume-verify", action="store_true",
                    help="let the local model verify a resumed run with a bounded read-only command")
args = parser.parse_args()
if not 0 <= args.approval_delay_ms <= 5000:
    parser.error("--approval-delay-ms must be between 0 and 5000")
if not 0 <= args.queue_delay_ms <= 12000:
    parser.error("--queue-delay-ms must be between 0 and 12000")
if not 0 <= args.run_delay_ms <= 5000:
    parser.error("--run-delay-ms must be between 0 and 5000")
if not 0 <= args.slow_ms <= 15000:
    parser.error("--slow-ms must be between 0 and 15000")
if not 0 <= args.task_ms <= 30000:
    parser.error("--task-ms must be between 0 and 30000")

base = Path(tempfile.mkdtemp(prefix="mdo-packed-docks-", dir=ROOT / ".build"))
Model.verify_recovery = args.resume_verify
Model.verification_file = base / "README.md"
Model.artifact_file = base / "artifact-fixture.txt"
Model.slow_seconds = args.slow_ms / 1000
Model.task_seconds = args.task_ms / 1000
shutil.copy2(ROOT / "mdo.exe", base / "mdo.exe")
(base / "README.md").write_text("Synthetic workspace file for @ completion.\n",
                                encoding="utf-8")
Model.artifact_file.write_text("".join(
    f"{index:03d} " + ("bounded artifact preview " * 36) + "\n"
    for index in range(90)), encoding="utf-8")
(base / "src").mkdir()
(base / "src/alpha.c").write_text("/* first completion item */\n",
                                  encoding="utf-8")
(base / "src/alpha-test.c").write_text("/* second completion item */\n",
                                       encoding="utf-8")
(base / "notes").mkdir()
(base / "notes/QA notes.txt").write_text("Synthetic spaced filename.\n",
                                          encoding="utf-8")
port = free_port()
model = ModelServer(("127.0.0.1", 0), Model)
model.daemon_threads = True
threading.Thread(target=model.serve_forever, daemon=True).start()
(base / "xs.json").write_text(json.dumps({"services": [{
    "enabled": True, "class": "http", "name": "mdo", "ip": "127.0.0.1",
    "port": port, "recv_limit": 8454144, "body_limit": 8388608,
    "host_default": {"enabled": True, "name": "mdo", "path": "web",
                     "devlang": "c", "devfile": "generated/mdo_unity.c"}}]}),
    encoding="utf-8")
home = base / "mdo-home"
env = os.environ.copy()
env["USERPROFILE"] = str(base)
env["MDO_LING_RESPONSES_URL"] = f"http://127.0.0.1:{model.server_address[1]}/v1"
env["MDO_LING_API_KEY"] = "bounded-packed-docks-key"
process = None
proxy = None
try:
    with (base / "packed.log").open("ab") as log:
        process = subprocess.Popen([str(base / "mdo.exe"), "--", "--home",
                                    str(home)], cwd=base, env=env, stdout=log,
                                   stderr=subprocess.STDOUT,
                                   creationflags=(subprocess.CREATE_NO_WINDOW
                                                  if os.name == "nt" else 0))
    wait_ready(port, process)
    options = {
        "project_id": "default", "title": "Packed docks QA",
        "agent_id": "mdo.default", "model_id": "ling-3.0-tiny",
        "protocol": "openai-responses", "reasoning_effort": "medium",
        "max_output_tokens": 1024,
    }
    if args.resume_verify:
        options["permission_profile"] = "full-access"
    status, response = request(port, "POST", "/api/v1/sessions", options)
    if status != 201:
        raise RuntimeError((status, response))
    session = response["data"]["id"]
    browser_port = port
    if (args.approval_delay_ms or args.queue_delay_ms or args.run_delay_ms
            or args.fail_first_run or args.fail_first_queue):
        proxy = BoundedDelayProxyServer(("127.0.0.1", 0), BoundedDelayProxy)
        proxy.upstream_port = port
        proxy.approval_delay_seconds = args.approval_delay_ms / 1000
        proxy.queue_delay_seconds = args.queue_delay_ms / 1000
        proxy.run_delay_seconds = args.run_delay_ms / 1000
        proxy.fail_first_run = args.fail_first_run
        proxy.fail_first_queue = args.fail_first_queue
        proxy.count_lock = threading.Lock()
        proxy.approval_puts = 0
        proxy.queue_posts = 0
        proxy.run_posts = 0
        threading.Thread(target=proxy.serve_forever, daemon=True).start()
        browser_port = proxy.server_address[1]
    print(f"READY url=http://127.0.0.1:{browser_port}/#/projects/default/"
          f"sessions/{session} base={base}", flush=True)
    input("Press Enter to stop QA servers.\n")
finally:
    if proxy:
        print(f"QA approval PUT total={proxy.approval_puts}", flush=True)
        print(f"QA queue POST total={proxy.queue_posts}", flush=True)
        print(f"QA run POST total={proxy.run_posts}", flush=True)
        proxy.shutdown()
        proxy.server_close()
    stop_host(process)
    model.shutdown()
    model.server_close()
