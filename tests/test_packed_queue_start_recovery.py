"""Bounded packed model interruption: restore queue claims, then continue.

One loopback model, three generations and a disposable Home. No credentials
or external services, and no stress or high-load tests.
"""

from __future__ import annotations

import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
import tempfile
import threading
import time
from pathlib import Path

from test_api_runtime import request as raw_request, session_events
from test_packed_home_lease import (
    ROOT, release_packed_copies, request, site, start, stop, wait_bootstrap,
)


class Model(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    started = threading.Event()
    release = threading.Event()
    finished = threading.Event()
    requests = []

    def log_message(self, *_):
        pass

    def handle(self):
        try:
            super().handle()
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        type(self).requests.append(body["messages"])
        first = len(self.requests) == 1
        try:
            if first:
                self.started.set()
                assert self.release.wait(10), "Interruption fixture was not released"
            raw = json.dumps({"id": "queue-recovery-fixture", "model": body["model"],
                "choices": [{"index": 0, "message": {"role": "assistant",
                    "content": "The original task can continue."}, "finish_reason": "stop"}],
                "usage": {"prompt_tokens": 20, "completion_tokens": 10}}).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(raw)))
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(raw)
        finally:
            if first:
                self.finished.set()


def configure_model(port: int, endpoint: str) -> None:
    status, headers, raw = raw_request(port, "GET", "/api/v1/models/config")
    assert status == 200, raw
    config = json.loads(raw)["data"]
    provider = json.loads(json.dumps(config["providers"][0]))
    provider.update(id="queue-fixture", name="Queue fixture", builtin=False,
        editable=True, removable=True, endpoints={"chat_completions": endpoint},
        credential={"secret_ref": "env:MDO_QUEUE_FIXTURE_KEY"})
    model = json.loads(json.dumps(config["items"][0]))
    model.update(id="queue-fixture", name="Queue fixture", provider="queue-fixture",
        builtin=False, free=False, editable=True, removable=True,
        protocols=["openai-chat-completions"], default_protocol="openai-chat-completions")
    config["providers"].append(provider)
    config["items"].append(model)
    config.pop("runtime_override", None)
    status, _, raw = raw_request(port, "PUT", "/api/v1/settings/models",
        body=json.dumps({"schema_version": 1, "patch": config}).encode(),
        headers={"Content-Type": "application/json", "If-Match": headers["etag"]})
    assert status == 200, raw


def check(packed: Path, endpoint: str) -> dict:
    (ROOT / ".build").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="packed-queue-start-",
                                     dir=ROOT / ".build") as raw:
        base = Path(raw)
        first_site, first_port = site(base, "first", packed)
        second_site, second_port = site(base, "second", packed)
        home = base / "mdo-home"
        env = os.environ.copy()
        env["USERPROFILE"] = str(base)
        env["USE_WEBVIEW"] = "0"
        env["MDO_QUEUE_FIXTURE_KEY"] = "fixture-only"
        first = start(first_site, packed, home, env)
        second = None
        try:
            status, response = wait_bootstrap(first, first_port,
                                              first_site / "packed.log")
            assert status == 200 and response["data"]["ready"], response
            configure_model(first_port, endpoint)
            status, response = request(first_port, "POST", "/api/v1/sessions", {
                "project_id": "default", "title": "Queue start recovery QA",
                "agent_id": "mdo.default", "model_id": "queue-fixture",
                "protocol": "openai-chat-completions", "reasoning_effort": "none",
                "max_output_tokens": 1024,
            })
            assert status == 201, response
            session_id = response["data"]["id"]
            path = f"/api/v1/projects/default/sessions/{session_id}"
            queue_id = "a" * 32
            queue_item = path + "/queue/" + queue_id
            status, response = request(first_port, "POST", path + "/queue", {
                "id": queue_id, "text": "durable packed start",
                "first": False, "stage": True,
            })
            assert status == 201, response
            for state in ("pending", "sending"):
                status, response = request(first_port, "PUT", queue_item,
                                           {"state": state})
                assert status == 200, response
            status, response = request(first_port, "POST", path + "/runs", {
                "prompt": "durable packed start", "queue_item_id": queue_id,
            })
            assert status == 202, response
            run_id = response["data"]["id"]
            assert Model.started.wait(5), "The model request never started"
            status, response = request(first_port, "GET", "/api/v1/runs/" + run_id)
            assert status == 200 and not response["data"]["terminal"], response
            queued_id = "b" * 32
            status, response = request(first_port, "POST", path + "/queue", {
                "id": queued_id, "text": "next queued task", "first": False,
            })
            assert status == 201, response
            events_file = home / "sessions" / "default" / session_id / "ui-events.jsonl"
            deadline = time.monotonic() + 5.0
            evidence = None
            while time.monotonic() < deadline:
                if events_file.exists():
                    for line in events_file.read_text(encoding="utf-8").splitlines():
                        try:
                            event = json.loads(line)
                        except json.JSONDecodeError:
                            continue
                        if event.get("queue_item_id") == queue_id:
                            evidence = event
                            break
                if evidence is not None:
                    break
                time.sleep(0.02)
            assert evidence is not None and evidence["run_id"] > 0, evidence
            first.kill()
            first.wait(timeout=5)
            Model.release.set()
            assert Model.finished.wait(3), "Interrupted fixture did not finish"
            # Emulate power loss between the durable start event and the
            # receipt/queue promotion. These files are changed only offline.
            session_dir = events_file.parent
            receipt_file = session_dir / "queue-receipts" / (queue_id + ".json")
            receipt_file.write_text(json.dumps({
                "schema_version": 3, "id": queue_id,
                "state": "starting", "run_id": run_id,
                "agent_run_id": evidence["run_id"],
            }), encoding="utf-8")
            queue_file = session_dir / "queue.json"
            queue = json.loads(queue_file.read_text(encoding="utf-8"))
            assert queue["items"][0]["id"] == queue_id, queue
            queue["items"][0].pop("run_id", None)
            queue_file.write_text(json.dumps(queue), encoding="utf-8")
            second = start(second_site, packed, home, env)
            status, response = wait_bootstrap(second, second_port,
                                              second_site / "packed.log")
            assert status == 200 and response["data"]["ready"], response
            status, response = request(second_port, "GET", path + "/queue")
            assert status == 200, response
            assert len(response["data"]["items"]) == 2, response
            item = response["data"]["items"][0]
            assert item["run_id"] == run_id and not item.get(
                "start_claimed", False), item
            assert response["data"]["items"][1]["id"] == queued_id, response
            assert len(Model.requests) == 1, "Restart replayed the interrupted model"
            assert json.loads(receipt_file.read_text(encoding="utf-8")) == {
                "schema_version": 1, "id": queue_id, "run_id": run_id,
            }
            status, response = request(second_port, "POST", path + "/runs", {
                "prompt": "durable packed start", "queue_item_id": queue_id,
            })
            assert status == 409 and response["error"]["code"] == \
                "queue_run_started", response
            assert len(Model.requests) == 1, "A duplicate queue start reached the model"
            status, response = request(second_port, "GET", path + "/recovery")
            assert status == 200 and response["data"]["resume_required"], response
            assert response["data"]["items"] == [], response
            status, response = request(second_port, "POST", path + "/resume", {
                "recovery_token": response["data"]["recovery_token"], "decisions": [],
                "client_resume_id": "c" * 32,
            })
            assert status == 202 and response["data"]["resume"], response

            def wait(run: dict) -> None:
                deadline = time.monotonic() + 5
                while time.monotonic() < deadline:
                    status, response = request(second_port, "GET", "/api/v1/runs/" + run["id"])
                    assert status == 200, response
                    if response["data"]["terminal"]:
                        assert response["data"]["state"] == "succeeded", response
                        return
                    time.sleep(.02)
                raise AssertionError("Continuation did not finish")

            wait(response["data"])
            status, response = request(second_port, "GET", path + "/recovery")
            assert status == 200 and not response["data"]["resume_required"], response
            status, response = request(second_port, "PUT", path + "/queue/" + queued_id,
                {"state": "sending"})
            assert status == 200, response
            status, response = request(second_port, "POST", path + "/runs", {
                "prompt": "next queued task", "queue_item_id": queued_id,
            })
            assert status == 202, response
            wait(response["data"])
            events = session_events(second_port, path)
            assert sum(e["kind"] == "agent_start" and e["user_message_sequence"] > 0
                for e in events) == 2, events
            assert not any(e["kind"] == "error" for e in events), events
            assert len(Model.requests) == 3, Model.requests
            for messages in Model.requests:
                assert sum(m["role"] == "user" and m["content"] == "durable packed start"
                    for m in messages) == 1, messages
            assert any(m["role"] == "user" and m["content"] == "next queued task"
                for m in Model.requests[-1]), Model.requests[-1]
        finally:
            if second is not None:
                stop(second)
            stop(first)
            release_packed_copies(first_site / packed.name,
                                  second_site / packed.name)
    return {"passed": True, "model_calls": len(Model.requests),
        "interrupted_while_model_active": True, "restored_queue_items": 2,
        "restart_and_duplicate_start_did_not_replay": True,
        "original_input_not_duplicated": True, "explicit_resume_succeeded": True,
        "queued_followup_succeeded": True, "final_errors": 0}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--packed", type=Path, required=True)
    parser.add_argument("--record", type=Path)
    args = parser.parse_args()
    packed = args.packed.resolve()
    assert packed.is_file(), packed
    (ROOT / ".build").mkdir(exist_ok=True)
    with ThreadingHTTPServer(("127.0.0.1", 0), Model) as server:
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            result = check(packed, f"http://127.0.0.1:{server.server_port}/v1")
        finally:
            Model.release.set()
            server.shutdown()
            thread.join(timeout=3)
    if args.record:
        args.record.parent.mkdir(parents=True, exist_ok=True)
        args.record.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
