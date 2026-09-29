"""Local packed-page fixture for todo, ask, and approval UI review.

Run from the repository root after building mdo.exe, or pass --packed-path to
inspect an isolated pack while the installed executable is running. The model
endpoint only binds to localhost and returns deterministic tool calls for marker prompts:
TODO UI, ASK UI, LONG ASK UI, SEQUENTIAL DECISIONS UI, APPROVAL UI, APPROVAL RUN UI,
APPROVAL NEXT UI, TASK UI, or ARTIFACT UI. The long
ask has multiline question and options; the latter reads one bounded synthetic
text file so the normal tool-output artifact path is used. The optional chat
stream emits two bounded chunks with interleaved text and reasoning fields.
"""

import argparse
import base64
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
    model_delay_seconds = 0
    task_seconds = 12
    artifact_file = None
    chat_stream = False

    def log_message(self, *_args):
        pass

    def do_POST(self):
        if self.path == "/v1/chat/completions" and Model.chat_stream:
            request_body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
            if request_body.get("stream") is not True:
                self.send_error(400, "streaming request required")
                return
            chunks = [
                {"choices": [{"index": 0, "delta": {"role": "assistant",
                    "content": "Hello ", "reasoning_content": "Thinking "},
                    "finish_reason": None}]},
                {"choices": [{"index": 0, "delta": {"content": "world",
                    "reasoning_content": "again"}, "finish_reason": None}]},
                {"choices": [{"index": 0, "delta": {}, "finish_reason": "stop"}],
                    "usage": {"prompt_tokens": 7, "completion_tokens": 3,
                              "total_tokens": 10}},
            ]
            payload = ("".join("data: " + json.dumps({"id": "chatcmpl-qa",
                "object": "chat.completion.chunk", "model": "ling-3.0-tiny",
                **chunk}) + "\n\n" for chunk in chunks) +
                "data: [DONE]\n\n").encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)
            print("QA interleaved chat stream served", flush=True)
            return
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
                script = (f"import time; time.sleep({Model.task_seconds:g}); "
                          "print('task UI fixture')")
                if Model.task_output_lines > 1:
                    script = ("import time; "
                              f"print(('task UI fixture line\\n')*{Model.task_output_lines}, "
                              f"end='', flush=True); time.sleep({Model.task_seconds:g})")
                output = [{"type": "function_call", "call_id": "ui-task-1",
                           "name": "spawn", "arguments": json.dumps({
                               "argv": [sys.executable, "-c", script]})}]
            elif "SEQUENTIAL DECISIONS UI" in wire and "sequential-decisions" not in Model.sent:
                Model.sent.add("sequential-decisions")
                output = [
                    {"type": "function_call", "call_id": "ui-sequential-ask",
                     "name": "ask_user", "arguments": json.dumps({
                         "question": "Which bounded check should run next?",
                         "options": ["Inspect", "Continue"]})},
                    {"type": "function_call", "call_id": "ui-sequential-approval",
                     "name": "exec", "arguments": json.dumps({
                         "argv": [sys.executable, "-c",
                                  "print('sequential decision fixture')"],
                         "timeout_ms": 5000})},
                ]
            elif "ASK UI" in wire and "TASK UI" not in wire and "ask" not in Model.sent:
                Model.sent.add("ask")
                long_ask = "LONG ASK UI" in wire
                output = [{"type": "function_call", "call_id": "ui-ask-1",
                           "name": "ask_user", "arguments": json.dumps({
                               "question": (
                                   "Нужно выбрать способ проверки изменений в нескольких "
                                   "модулях: сохранить черновик, проверить доступные "
                                   "результаты и затем продолжить работу. Какой вариант "
                                   "подходит для этого сеанса?"
                               ) if long_ask else "Which route should I take?",
                               "options": [
                                   "Сначала проверить локальные результаты и только "
                                   "потом продолжить" if long_ask else "Fast",
                                   "Продолжить сейчас, а результаты проверить после "
                                   "следующего шага" if long_ask else "Careful",
                               ]})}]
            elif "APPROVAL RUN UI" in wire and "approval-run-1" not in Model.sent:
                Model.sent.add("approval-run-1")
                output = [{"type": "function_call", "call_id": "ui-approval-run-1",
                           "name": "exec", "arguments": json.dumps({
                               "argv": [sys.executable, "-c",
                                        "print('approval run fixture 1')"],
                               "timeout_ms": 5000})}]
            elif "APPROVAL RUN UI" in wire and "approval-run-2" not in Model.sent:
                Model.sent.add("approval-run-2")
                output = [{"type": "function_call", "call_id": "ui-approval-run-2",
                           "name": "exec", "arguments": json.dumps({
                               "argv": [sys.executable, "-c",
                                        "print('approval run fixture 2')"],
                               "timeout_ms": 5000})}]
            elif "APPROVAL NEXT UI" in wire and "approval-next" not in Model.sent:
                Model.sent.add("approval-next")
                output = [{"type": "function_call", "call_id": "ui-approval-next",
                           "name": "exec", "arguments": json.dumps({
                               "argv": [sys.executable, "-c",
                                        "print('approval next fixture')"],
                               "timeout_ms": 5000})}]
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
        elif Model.model_delay_seconds:
            time.sleep(Model.model_delay_seconds)
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

    LOCALE_HOTKEY = b'''<script>
// Isolated packed-page probe: switch UI language without moving focus away
// from the current conversation control. F9 is only active in this proxy.
document.addEventListener('keydown', async event => {
  if (event.key !== 'F9') return;
  event.preventDefault();
  const locale = await import('/js/i18n.js');
  await locale.loadLocale(locale.currentLocale() === 'zh-CN' ? 'en-US' : 'zh-CN');
}, true);
</script>'''

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

    def do_HEAD(self):
        self.forward()

    def forward(self):
        if (self.command == "GET" and self.path == "/js/main.js" and
                (self.server.fail_first_module or
                 self.server.delay_first_module_seconds)):
            with self.server.count_lock:
                self.server.module_reads += 1
                module_read = self.server.module_reads
            if module_read == 1:
                if self.server.delay_first_module_seconds:
                    print("QA delaying first main module GET", flush=True)
                    time.sleep(self.server.delay_first_module_seconds)
                if self.server.fail_first_module:
                    print("QA rejected first main module GET", flush=True)
                    self.send_response(503)
                    self.send_header("Content-Type", "text/javascript")
                    self.send_header("Content-Length", "0")
                    self.send_header("Connection", "close")
                    self.end_headers()
                    self.close_connection = True
                    return
        length = int(self.headers.get("Content-Length", "0"))
        if length < 0 or length > 8 * 1024 * 1024:
            self.send_error(413)
            return
        body = self.rfile.read(length) if length else None
        if (self.command == "POST" and self.path.startswith("/api/v1/projects/")
                and "/sessions/" in self.path and
                self.path.endswith("/attachments")):
            with self.server.count_lock:
                self.server.attachment_posts += 1
                count = self.server.attachment_posts
            print(f"QA attachment POST #{count}", flush=True)
            time.sleep(self.server.attachment_delay_seconds)
            if self.server.fail_first_attachment and count == 1:
                payload = json.dumps({"ok": False, "error": {
                    "code": "attachment_store_failed",
                    "message": "Synthetic attachment failure"
                }}).encode()
                self.send_response(503)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(payload)))
                self.send_header("Connection", "close")
                self.end_headers()
                self.wfile.write(payload)
                self.close_connection = True
                return
        if (self.command == "DELETE" and
                self.path.startswith("/api/v1/projects/") and
                "/sessions/" in self.path and "/attachments/" in self.path):
            with self.server.count_lock:
                self.server.attachment_deletes += 1
                count = self.server.attachment_deletes
            print(f"QA attachment DELETE #{count}", flush=True)
            time.sleep(self.server.attachment_delete_delay_seconds)
            if self.server.fail_first_attachment_delete and count == 1:
                payload = json.dumps({"ok": False, "error": {
                    "code": "attachment_unavailable",
                    "message": "Synthetic attachment delete failure"
                }}).encode()
                self.send_response(503)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(payload)))
                self.send_header("Connection", "close")
                self.end_headers()
                self.wfile.write(payload)
                self.close_connection = True
                return
        if (self.command == "POST" and
                self.path.startswith("/api/v1/projects/") and
                "/sessions/" in self.path and
                "/queue/discard-images/" in self.path):
            with self.server.count_lock:
                self.server.discard_markers += 1
                count = self.server.discard_markers
            print(f"QA discard marker POST #{count}", flush=True)
            if self.server.fail_first_discard_marker and count == 1:
                payload = json.dumps({"ok": False, "error": {
                    "code": "queue_unavailable",
                    "message": "Synthetic cleanup marker failure"
                }}).encode()
                self.send_response(503)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(payload)))
                self.send_header("Connection", "close")
                self.end_headers()
                self.wfile.write(payload)
                self.close_connection = True
                return
        if (self.command == "GET" and self.path in
                {"/lang/en-US.json", "/lang/ru-RU.json"}):
            print(f"QA locale GET {self.path}", flush=True)
            time.sleep(self.server.locale_delay_seconds)
        if self.command == "POST" and self.path == "/api/v1/projects":
            with self.server.count_lock:
                self.server.project_posts += 1
                count = self.server.project_posts
            print(f"QA project POST #{count}", flush=True)
            time.sleep(self.server.project_delay_seconds)
            if self.server.fail_first_project and count == 1:
                payload = json.dumps({"ok": False, "error": {
                    "code": "project_invalid",
                    "message": "Synthetic project rejection"
                }}).encode()
                self.send_response(422)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(payload)))
                self.send_header("Connection", "close")
                self.end_headers()
                self.wfile.write(payload)
                self.close_connection = True
                return
        if (self.server.reject_pane_layout and self.path == "/api/v1/pane-layout"
                and self.command in {"GET", "PUT"}):
            payload = json.dumps({"ok": False, "error": {
                "code": "qa_pane_unavailable", "message": "Synthetic layout failure"
            }}).encode()
            self.send_response(503)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(payload)))
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(payload)
            self.close_connection = True
            return
        drop_response = False
        drop_run_response = False
        drop_create_response = False
        delay_run_response = False
        if self.command == "POST" and self.path == "/api/v1/sessions":
            with self.server.count_lock:
                self.server.create_posts += 1
                count = self.server.create_posts
            print(f"QA create POST #{count}", flush=True)
            time.sleep(self.server.create_delay_seconds)
            drop_create_response = (self.server.drop_first_create_response
                                    and count == 1)
            if self.server.fail_first_create and count == 1:
                payload = json.dumps({"ok": False, "error": {
                    "code": "session_profile_invalid",
                    "message": "Synthetic profile rejection"
                }}).encode()
                self.send_response(422)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(payload)))
                self.send_header("Connection", "close")
                self.end_headers()
                self.wfile.write(payload)
                self.close_connection = True
                return
        if (self.command == "GET" and self.path.startswith("/api/v1/projects/")
                and "/sessions/" in self.path and self.path.endswith("/queue")):
            with self.server.count_lock:
                self.server.queue_reads += 1
                queue_read_count = self.server.queue_reads
            print(f"QA queue GET #{queue_read_count}", flush=True)
            time.sleep(self.server.queue_read_delay_seconds)
            fail_read = (self.server.fail_first_queue_read
                         and queue_read_count == 1)
            fail_reconcile = False
            if self.server.queue_read_failures_remaining:
                with self.server.count_lock:
                    fail_reconcile = (self.server.dropped_queue_response
                                      and self.server.queue_read_failures_remaining > 0)
                    if fail_reconcile:
                        self.server.queue_read_failures_remaining -= 1
            if fail_read or fail_reconcile:
                payload = json.dumps({"ok": False, "error": {
                    "code": "qa_read_rejected", "message": "Synthetic queue read failure"
                }}).encode()
                self.send_response(503)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(payload)))
                self.send_header("Connection", "close")
                self.end_headers()
                self.wfile.write(payload)
                self.close_connection = True
                return
        if (self.command == "GET" and self.path.startswith("/api/v1/projects/")
                and "/sessions/" in self.path and self.path.endswith("/history")):
            time.sleep(self.server.history_delay_seconds)
        if self.command == "GET" and self.path.startswith("/api/v1/runs/"):
            with self.server.count_lock:
                self.server.run_reads += 1
                count = self.server.run_reads
            delay_run_response = True
        if self.command == "PUT" and self.path.startswith("/api/v1/approvals/"):
            with self.server.count_lock:
                self.server.approval_puts += 1
                count = self.server.approval_puts
            print(f"QA approval PUT #{count}", flush=True)
            time.sleep(self.server.approval_delay_seconds)
        if (self.command == "PUT" and self.path.startswith("/api/v1/projects/")
                and "/sessions/" in self.path and "/asks/" in self.path):
            with self.server.count_lock:
                self.server.ask_puts += 1
                count = self.server.ask_puts
            print(f"QA ask PUT #{count}", flush=True)
            time.sleep(self.server.ask_delay_seconds)
        if (self.command == "DELETE" and self.path.startswith("/api/v1/tasks/")
                and self.path.rsplit("/", 1)[-1].isdigit()):
            with self.server.count_lock:
                self.server.task_deletes += 1
                count = self.server.task_deletes
            print(f"QA task DELETE #{count}", flush=True)
            time.sleep(self.server.task_cancel_delay_seconds)
        if self.command == "DELETE" and self.path.startswith("/api/v1/runs/"):
            with self.server.count_lock:
                self.server.run_deletes += 1
                count = self.server.run_deletes
            print(f"QA run DELETE #{count}", flush=True)
            time.sleep(self.server.run_cancel_delay_seconds)
        if (self.command == "POST" and self.path.startswith("/api/v1/projects/")
                and "/sessions/" in self.path and self.path.endswith("/queue")):
            with self.server.count_lock:
                self.server.queue_posts += 1
                count = self.server.queue_posts
            print(f"QA queue POST #{count}", flush=True)
            time.sleep(self.server.queue_delay_seconds)
            drop_response = self.server.drop_first_queue_response and count == 1
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
            if self.server.full_first_queue and count == 1:
                payload = json.dumps({"ok": False, "error": {
                    "code": "queue_full", "message": "Synthetic queue is full"
                }}).encode()
                self.send_response(422)
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
            drop_run_response = count == self.server.drop_run_response_number
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
            response_status = response.status
            if (self.server.locale_hotkey and self.command == "GET" and
                    self.path == "/" and response_status == 200):
                if b"</body>" not in payload:
                    raise RuntimeError("packed page has no body for locale probe")
                payload = payload.replace(b"</body>",
                                          self.LOCALE_HOTKEY + b"</body>", 1)
            if delay_run_response:
                try:
                    run_state = json.loads(payload)["data"]["state"]
                except (KeyError, TypeError, ValueError):
                    run_state = "unknown"
                print(f"QA run GET #{count} captured={run_state}", flush=True)
                time.sleep(self.server.run_read_delay_seconds)
                if self.server.fail_first_run_read and count == 1:
                    response_status = 503
                    payload = json.dumps({"ok": False, "error": {
                        "code": "qa_run_read_rejected",
                        "message": "Synthetic stale run read failure"
                    }}).encode()
                    print("QA first run GET rejected after delay", flush=True)
            if drop_response:
                if self.server.consume_dropped_queue_response:
                    if response.status != 201:
                        raise RuntimeError(("queue was not accepted", response.status,
                                            payload))
                    item = json.loads(body)
                    item_path = self.path + "/" + item["id"]
                    for state in ("pending", "sending"):
                        status, result = request(self.server.upstream_port,
                                                 "PUT", item_path,
                                                 {"state": state})
                        if status != 200:
                            raise RuntimeError(("queue promotion", status,
                                                result))
                    run_body = {"prompt": item["text"],
                                "queue_item_id": item["id"]}
                    if item.get("attachments"):
                        run_body["attachments"] = item["attachments"]
                    run_path = self.path[:-len("queue")] + "runs"
                    status, result = request(self.server.upstream_port,
                                             "POST", run_path, run_body)
                    if status != 202:
                        raise RuntimeError(("queue run", status, result))
                    status, result = request(self.server.upstream_port,
                                             "DELETE", item_path)
                    if status != 200:
                        raise RuntimeError(("queue removal", status, result))
                    print("QA another page consumed the accepted queue item",
                          flush=True)
                print("QA queue response dropped after upstream acceptance", flush=True)
                with self.server.count_lock:
                    self.server.dropped_queue_response = True
                self.close_connection = True
                self.connection.close()
                return
            if drop_run_response:
                print("QA run response dropped after upstream acceptance", flush=True)
                self.close_connection = True
                self.connection.close()
                return
            if drop_create_response:
                print("QA create response dropped after upstream acceptance",
                      flush=True)
                self.close_connection = True
                self.connection.close()
                return
            self.send_response(response_status)
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
        if isinstance(sys.exc_info()[1],
                      (BrokenPipeError, ConnectionAbortedError, ConnectionResetError)):
            return
        super().handle_error(request, client_address)


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--packed-path", type=Path,
                    default=ROOT / ("mdo.exe" if os.name == "nt" else "mdo"),
                    help="packed executable to copy into the isolated QA Home")
parser.add_argument("--approval-delay-ms", type=int, default=0,
                    help="delay one approval PUT by 0-5000 ms for manual duplicate-click QA")
parser.add_argument("--ask-delay-ms", type=int, default=0,
                    help="delay ask PUTs by 0-5000 ms for cross-session QA")
parser.add_argument("--task-cancel-delay-ms", type=int, default=0,
                    help="delay task DELETE by 0-5000 ms for duplicate-click QA")
parser.add_argument("--run-cancel-delay-ms", type=int, default=0,
                    help="delay run DELETE by 0-5000 ms for stop-focus QA")
parser.add_argument("--queue-delay-ms", type=int, default=0,
                    help="delay queue POSTs by 0-12000 ms for bounded dispatch race QA")
parser.add_argument("--queue-read-delay-ms", type=int, default=0,
                    help="delay queue GETs by 0-5000 ms for review-focus QA")
parser.add_argument("--fail-first-queue-read", action="store_true",
                    help="reject the first queue GET for route-switch QA")
parser.add_argument("--attachment-delay-ms", type=int, default=0,
                    help="delay attachment POSTs by 0-5000 ms for paste/drop QA")
parser.add_argument("--fail-first-attachment", action="store_true",
                    help="reject the first attachment POST for queued-upload QA")
parser.add_argument("--fail-first-attachment-delete", action="store_true",
                    help="reject the first attachment DELETE for cleanup-retry QA")
parser.add_argument("--attachment-delete-delay-ms", type=int, default=0,
                    help="delay attachment DELETEs by 0-5000 ms for cleanup-marker QA")
parser.add_argument("--fail-first-discard-marker", action="store_true",
                    help="reject the first durable image cleanup marker")
parser.add_argument("--run-delay-ms", type=int, default=0,
                    help="delay run POSTs by 0-5000 ms for composer handoff QA")
parser.add_argument("--run-read-delay-ms", type=int, default=0,
                    help="delay run GETs by 0-5000 ms for route-switch QA")
parser.add_argument("--fail-first-run-read", action="store_true",
                    help="reject the first delayed run GET after capturing its state")
parser.add_argument("--create-delay-ms", type=int, default=0,
                    help="delay session create POSTs by 0-5000 ms for new-task QA")
parser.add_argument("--project-delay-ms", type=int, default=0,
                    help="delay project create POSTs by 0-5000 ms for sidebar navigation QA")
parser.add_argument("--locale-delay-ms", type=int, default=0,
                    help="delay non-default locale GETs by 0-5000 ms for settings navigation QA")
parser.add_argument("--fail-first-project", action="store_true",
                    help="reject one project creation with a 422 error after any delay")
parser.add_argument("--history-delay-ms", type=int, default=0,
                    help="delay history GETs by 0-5000 ms for message-action QA")
parser.add_argument("--drop-first-create-response", action="store_true",
                    help="accept one session creation then close before replying")
parser.add_argument("--fail-first-create", action="store_true",
                    help="reject one session creation with a 422 profile error")
parser.add_argument("--fail-first-run", action="store_true",
                    help="reject one run POST before forwarding, for draft recovery QA")
parser.add_argument("--drop-first-run-response", action="store_true",
                    help="accept one run POST upstream but close before replying")
parser.add_argument("--drop-run-response-number", type=int, default=0,
                    help="accept this numbered run POST upstream but close before replying (1-3)")
parser.add_argument("--fail-first-queue", action="store_true",
                    help="reject one queue POST before forwarding, for staged draft QA")
parser.add_argument("--full-first-queue", action="store_true",
                    help="reject one queue POST with a definite 422 queue_full")
parser.add_argument("--drop-first-queue-response", action="store_true",
                    help="accept one queue POST upstream but close before replying")
parser.add_argument("--consume-dropped-queue-response", action="store_true",
                    help="consume that item upstream before dropping its response")
parser.add_argument("--fail-first-queue-reconcile", action="store_true",
                    help="also fail the first queue GET after a dropped response")
parser.add_argument("--queue-read-failures", type=int, default=0,
                    help="fail 0-8 queue GETs after a dropped response")
parser.add_argument("--reject-pane-layout", action="store_true",
                    help="reject layout GET/PUT for localized error feedback QA")
parser.add_argument("--slow-ms", type=int, default=15000,
                    help="first SLOW UI model response delay, 0-30000 ms")
parser.add_argument("--model-delay-ms", type=int, default=0,
                    help="delay regular fixture model responses, 0-5000 ms")
parser.add_argument("--task-ms", type=int, default=12000,
                    help="TASK UI background process sleep, 0-30000 ms")
parser.add_argument("--task-output-lines", type=int, default=1,
                    help="TASK UI emits 1-120 short lines before sleeping")
parser.add_argument("--resume-verify", action="store_true",
                    help="let the local model verify a resumed run with a bounded read-only command")
parser.add_argument("--image-capable", action="store_true",
                    help="enable image input in the isolated built-in model fixture")
parser.add_argument("--locale-hotkey", action="store_true",
                    help="let F9 change packed-page locale without moving focus")
parser.add_argument("--fail-first-module", action="store_true",
                    help="reject the first main.js GET to test startup recovery")
parser.add_argument("--delay-first-module-ms", type=int, default=0,
                    help="delay first main.js GET by 0-60000 ms to test startup timeout")
parser.add_argument("--agent-profile-fixture", action="store_true",
                    help="install an isolated custom Agent with model, reasoning and permission defaults")
parser.add_argument("--second-model-context-tokens", type=int, default=0,
                    help="set the isolated second model context to 131072-262144 tokens")
parser.add_argument("--interleaved-chat-stream", action="store_true",
                    help="serve a bounded Chat Completions stream with alternating text and reasoning")
args = parser.parse_args()
if not args.packed_path.is_file():
    parser.error(f"packed executable not found: {args.packed_path}")
if args.agent_profile_fixture and not args.image_capable:
    parser.error("--agent-profile-fixture requires --image-capable")
if not 0 <= args.approval_delay_ms <= 5000:
    parser.error("--approval-delay-ms must be between 0 and 5000")
if not 0 <= args.delay_first_module_ms <= 60000:
    parser.error("--delay-first-module-ms must be between 0 and 60000")
if not 0 <= args.ask_delay_ms <= 5000:
    parser.error("--ask-delay-ms must be between 0 and 5000")
if not 0 <= args.task_cancel_delay_ms <= 5000:
    parser.error("--task-cancel-delay-ms must be between 0 and 5000")
if not 0 <= args.run_cancel_delay_ms <= 5000:
    parser.error("--run-cancel-delay-ms must be between 0 and 5000")
if not 0 <= args.queue_delay_ms <= 12000:
    parser.error("--queue-delay-ms must be between 0 and 12000")
if not 0 <= args.queue_read_delay_ms <= 5000:
    parser.error("--queue-read-delay-ms must be between 0 and 5000")
if not 0 <= args.attachment_delay_ms <= 5000:
    parser.error("--attachment-delay-ms must be between 0 and 5000")
if not 0 <= args.attachment_delete_delay_ms <= 5000:
    parser.error("--attachment-delete-delay-ms must be between 0 and 5000")
if not 0 <= args.queue_read_failures <= 8:
    parser.error("--queue-read-failures must be between 0 and 8")
if not 0 <= args.run_delay_ms <= 5000:
    parser.error("--run-delay-ms must be between 0 and 5000")
if not 0 <= args.run_read_delay_ms <= 5000:
    parser.error("--run-read-delay-ms must be between 0 and 5000")
if not 0 <= args.create_delay_ms <= 5000:
    parser.error("--create-delay-ms must be between 0 and 5000")
if not 0 <= args.project_delay_ms <= 5000:
    parser.error("--project-delay-ms must be between 0 and 5000")
if not 0 <= args.locale_delay_ms <= 5000:
    parser.error("--locale-delay-ms must be between 0 and 5000")
if not 0 <= args.history_delay_ms <= 5000:
    parser.error("--history-delay-ms must be between 0 and 5000")
if not 0 <= args.drop_run_response_number <= 3:
    parser.error("--drop-run-response-number must be between 0 and 3")
if args.drop_first_run_response and args.drop_run_response_number:
    parser.error("choose only one run response drop option")
if not 0 <= args.slow_ms <= 30000:
    parser.error("--slow-ms must be between 0 and 30000")
if not 0 <= args.model_delay_ms <= 5000:
    parser.error("--model-delay-ms must be between 0 and 5000")
if not 0 <= args.task_ms <= 30000:
    parser.error("--task-ms must be between 0 and 30000")
if not 1 <= args.task_output_lines <= 120:
    parser.error("--task-output-lines must be between 1 and 120")
if (args.fail_first_queue_reconcile or args.queue_read_failures) and not args.drop_first_queue_response:
    parser.error("queue read failures require --drop-first-queue-response")
if args.consume_dropped_queue_response and not args.drop_first_queue_response:
    parser.error("--consume-dropped-queue-response requires --drop-first-queue-response")
if args.full_first_queue and (args.fail_first_queue or args.drop_first_queue_response):
    parser.error("choose only one first-queue rejection or response drop")
if args.fail_first_create and args.drop_first_create_response:
    parser.error("choose only one first-create failure option")
if args.second_model_context_tokens and (
        not args.image_capable or
        not 131072 <= args.second_model_context_tokens <= 262144):
    parser.error("--second-model-context-tokens requires --image-capable and 131072-262144")

base = Path(tempfile.mkdtemp(prefix="mdo-packed-docks-", dir=ROOT / ".build"))
if args.image_capable:
    defaults = json.loads((ROOT / "app/default-home/config/defaults.json")
                          .read_text(encoding="utf-8"))
    model_config = defaults["models"]["items"][0]
    text_model = json.loads(json.dumps(model_config))
    text_model["id"] = "ling-3.0-tiny-text-qa"
    text_model["name"] = "Ling Text QA"
    if args.second_model_context_tokens:
        text_model["window"]["context_tokens"] = args.second_model_context_tokens
        text_model["window"]["max_input_tokens"] = (
            args.second_model_context_tokens - 1)
    defaults["models"]["items"].append(text_model)
    model_config["capabilities"].append("media-input")
    model_config["attachments"] = ["image"]
    override = base / "default-home/config/defaults.json"
    override.parent.mkdir(parents=True)
    override.write_text(json.dumps(defaults), encoding="utf-8")
    (base / "fixture.png").write_bytes(base64.b64decode(
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+/qokAAAAASUVORK5CYII="))
Model.verify_recovery = args.resume_verify
Model.verification_file = base / "README.md"
Model.artifact_file = base / "artifact-fixture.txt"
Model.slow_seconds = args.slow_ms / 1000
Model.model_delay_seconds = args.model_delay_ms / 1000
Model.task_seconds = args.task_ms / 1000
Model.task_output_lines = args.task_output_lines
Model.chat_stream = args.interleaved_chat_stream
executable_name = "mdo.exe" if os.name == "nt" else "mdo"
shutil.copy2(args.packed_path, base / executable_name)
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
(base / "src/omega@beta.c").write_text("/* filename with at-sign */\n",
                                        encoding="utf-8")
(base / "notes").mkdir()
(base / "notes/QA notes.txt").write_text("Synthetic spaced filename.\n",
                                          encoding="utf-8")
(base / "notes" / (("资料" * 24) + ".txt")).write_text(
    "Synthetic long UTF-8 filename.\n", encoding="utf-8")
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
if args.agent_profile_fixture:
    agent_dir = home / "modules/agents"
    agent_dir.mkdir(parents=True)
    shutil.copy2(ROOT / "tests/fixtures/agent-profile-qa.c",
                 agent_dir / "agent-profile-qa.c")
env = os.environ.copy()
env["USERPROFILE"] = str(base)
env["MDO_LING_RESPONSES_URL"] = f"http://127.0.0.1:{model.server_address[1]}/v1"
if args.interleaved_chat_stream:
    env["MDO_LING_CHAT_COMPLETIONS_URL"] = (
        f"http://127.0.0.1:{model.server_address[1]}/v1")
env["MDO_LING_API_KEY"] = "bounded-packed-docks-key"
process = None
proxy = None
try:
    with (base / "packed.log").open("ab") as log:
        process = subprocess.Popen([str(base / executable_name), "--", "--home",
                                    str(home)], cwd=base, env=env, stdout=log,
                                   stderr=subprocess.STDOUT,
                                   creationflags=(subprocess.CREATE_NO_WINDOW
                                                  if os.name == "nt" else 0))
    wait_ready(port, process)
    options = {
        "project_id": "default", "title": "Packed docks QA",
        "agent_id": "mdo.default", "model_id": "ling-3.0-tiny",
        "protocol": ("openai-chat-completions" if args.interleaved_chat_stream
                     else "openai-responses"), "reasoning_effort": "medium",
        "max_output_tokens": 1024,
    }
    if args.resume_verify:
        options["permission_profile"] = "full-access"
    status, response = request(port, "POST", "/api/v1/sessions", options)
    if status != 201:
        raise RuntimeError((status, response))
    session = response["data"]["id"]
    browser_port = port
    if (args.approval_delay_ms or args.ask_delay_ms or args.task_cancel_delay_ms
            or args.run_cancel_delay_ms
            or args.queue_delay_ms or args.queue_read_delay_ms
            or args.fail_first_queue_read
            or args.attachment_delay_ms or args.fail_first_attachment
            or args.fail_first_attachment_delete or args.attachment_delete_delay_ms
            or args.fail_first_discard_marker
            or args.run_delay_ms
            or args.run_read_delay_ms
            or args.fail_first_run_read
            or args.create_delay_ms or args.project_delay_ms
            or args.locale_delay_ms or args.fail_first_project
            or args.history_delay_ms
            or args.drop_first_create_response
            or args.fail_first_create
            or args.fail_first_run or args.fail_first_queue
            or args.full_first_queue
            or args.drop_first_run_response or args.drop_run_response_number
            or args.drop_first_queue_response or args.fail_first_queue_reconcile
            or args.queue_read_failures
            or args.reject_pane_layout or args.locale_hotkey
            or args.fail_first_module or args.delay_first_module_ms):
        proxy = BoundedDelayProxyServer(("127.0.0.1", 0), BoundedDelayProxy)
        proxy.upstream_port = port
        proxy.locale_hotkey = args.locale_hotkey
        proxy.fail_first_module = args.fail_first_module
        proxy.delay_first_module_seconds = args.delay_first_module_ms / 1000
        proxy.module_reads = 0
        proxy.approval_delay_seconds = args.approval_delay_ms / 1000
        proxy.ask_delay_seconds = args.ask_delay_ms / 1000
        proxy.task_cancel_delay_seconds = args.task_cancel_delay_ms / 1000
        proxy.run_cancel_delay_seconds = args.run_cancel_delay_ms / 1000
        proxy.queue_delay_seconds = args.queue_delay_ms / 1000
        proxy.queue_read_delay_seconds = args.queue_read_delay_ms / 1000
        proxy.fail_first_queue_read = args.fail_first_queue_read
        proxy.queue_reads = 0
        proxy.attachment_delay_seconds = args.attachment_delay_ms / 1000
        proxy.fail_first_attachment = args.fail_first_attachment
        proxy.attachment_posts = 0
        proxy.fail_first_attachment_delete = args.fail_first_attachment_delete
        proxy.attachment_delete_delay_seconds = args.attachment_delete_delay_ms / 1000
        proxy.attachment_deletes = 0
        proxy.fail_first_discard_marker = args.fail_first_discard_marker
        proxy.discard_markers = 0
        proxy.run_delay_seconds = args.run_delay_ms / 1000
        proxy.run_read_delay_seconds = args.run_read_delay_ms / 1000
        proxy.fail_first_run_read = args.fail_first_run_read
        proxy.create_delay_seconds = args.create_delay_ms / 1000
        proxy.project_delay_seconds = args.project_delay_ms / 1000
        proxy.locale_delay_seconds = args.locale_delay_ms / 1000
        proxy.fail_first_project = args.fail_first_project
        proxy.history_delay_seconds = args.history_delay_ms / 1000
        proxy.drop_first_create_response = args.drop_first_create_response
        proxy.fail_first_create = args.fail_first_create
        proxy.fail_first_run = args.fail_first_run
        proxy.drop_run_response_number = (args.drop_run_response_number or
                                          (1 if args.drop_first_run_response else 0))
        proxy.fail_first_queue = args.fail_first_queue
        proxy.full_first_queue = args.full_first_queue
        proxy.drop_first_queue_response = args.drop_first_queue_response
        proxy.consume_dropped_queue_response = args.consume_dropped_queue_response
        proxy.queue_read_failures_remaining = max(
            args.queue_read_failures, int(args.fail_first_queue_reconcile))
        proxy.reject_pane_layout = args.reject_pane_layout
        proxy.dropped_queue_response = False
        proxy.count_lock = threading.Lock()
        proxy.approval_puts = 0
        proxy.ask_puts = 0
        proxy.task_deletes = 0
        proxy.run_deletes = 0
        proxy.run_reads = 0
        proxy.queue_posts = 0
        proxy.run_posts = 0
        proxy.create_posts = 0
        proxy.project_posts = 0
        threading.Thread(target=proxy.serve_forever, daemon=True).start()
        browser_port = proxy.server_address[1]
    print(f"READY url=http://127.0.0.1:{browser_port}/#/projects/default/"
          f"sessions/{session} base={base}", flush=True)
    input("Press Enter to stop QA servers.\n")
finally:
    if proxy:
        print(f"QA attachment DELETE total={proxy.attachment_deletes}", flush=True)
        print(f"QA discard marker POST total={proxy.discard_markers}", flush=True)
        print(f"QA approval PUT total={proxy.approval_puts}", flush=True)
        print(f"QA ask PUT total={proxy.ask_puts}", flush=True)
        print(f"QA task DELETE total={proxy.task_deletes}", flush=True)
        print(f"QA run DELETE total={proxy.run_deletes}", flush=True)
        print(f"QA run GET total={proxy.run_reads}", flush=True)
        print(f"QA queue GET total={proxy.queue_reads}", flush=True)
        print(f"QA queue POST total={proxy.queue_posts}", flush=True)
        print(f"QA run POST total={proxy.run_posts}", flush=True)
        print(f"QA create POST total={proxy.create_posts}", flush=True)
        print(f"QA project POST total={proxy.project_posts}", flush=True)
        proxy.shutdown()
        proxy.server_close()
    stop_host(process)
    model.shutdown()
    model.server_close()
