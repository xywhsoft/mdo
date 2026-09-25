"""Local packed-page fixture for todo, ask, and approval UI review.

Run from the repository root after building mdo.exe. The model endpoint only
binds to localhost and returns one deterministic tool call per marker prompt:
TODO UI, ASK UI, or APPROVAL UI. The latter requests a harmless print command.
"""

import json
import os
import shutil
import subprocess
import sys
import tempfile
import threading
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
        with Model.lock:
            if "TODO UI" in wire and "todo" not in Model.sent:
                Model.sent.add("todo")
                output = [{"type": "function_call", "call_id": "ui-todo-1",
                           "name": "mdo.todo", "arguments": json.dumps({"items": [
                               {"text": "Inspect UI", "done": True},
                               {"text": "Verify refresh", "done": False}]})}]
            elif "ASK UI" in wire and "ask" not in Model.sent:
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


base = Path(tempfile.mkdtemp(prefix="mdo-packed-docks-", dir=ROOT / ".build"))
shutil.copy2(ROOT / "mdo.exe", base / "mdo.exe")
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
try:
    with (base / "packed.log").open("ab") as log:
        process = subprocess.Popen([str(base / "mdo.exe"), "--", "--home",
                                    str(home)], cwd=base, env=env, stdout=log,
                                   stderr=subprocess.STDOUT,
                                   creationflags=(subprocess.CREATE_NO_WINDOW
                                                  if os.name == "nt" else 0))
    wait_ready(port, process)
    status, response = request(port, "POST", "/api/v1/sessions", {
        "project_id": "default", "title": "Packed docks QA",
        "agent_id": "mdo.default", "model_id": "ling-3.0-tiny",
        "protocol": "openai-responses", "reasoning_effort": "medium",
        "max_output_tokens": 1024})
    if status != 201:
        raise RuntimeError((status, response))
    session = response["data"]["id"]
    print(f"READY url=http://127.0.0.1:{port}/#/projects/default/"
          f"sessions/{session} base={base}", flush=True)
    input("Press Enter to stop QA servers.\n")
finally:
    stop_host(process)
    model.shutdown()
    model.server_close()
