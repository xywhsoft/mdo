"""Real managed queues advance after a failed reply and pause on explicit Stop."""
import argparse
import json
import os
from pathlib import Path
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from test_packed_home_lease import site, start, stop, wait_bootstrap
from test_packed_queue_start_recovery import configure_model
from test_api_runtime import request

ROOT = Path(__file__).resolve().parents[1]


def check(packed):
    entered = [threading.Event() for _ in range(3)]
    release = [threading.Event() for _ in range(3)]
    calls = []

    class Model(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_POST(self):
            self.rfile.read(int(self.headers["Content-Length"]))
            index = len(calls)
            calls.append(index)
            if index < 3:
                entered[index].set()
            if index in (0, 2):
                release[index].wait(20)
            status = 400 if index == 0 else 200
            body = {"error": {"message": "Controlled invalid request", "type": "invalid_request_error"}} if index == 0 else {
                "id": "continuation", "choices": [{"index": 0, "message": {
                    "role": "assistant", "content": "Next queued message completed."}, "finish_reason": "stop"}],
                "usage": {"prompt_tokens": 10, "completion_tokens": 5}}
            raw = json.dumps(body).encode()
            try:
                self.send_response(status)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(raw)))
                self.end_headers()
                self.wfile.write(raw)
            except OSError:
                pass

    peer = ThreadingHTTPServer(("127.0.0.1", 0), Model)
    peer.daemon_threads = True
    thread = threading.Thread(target=peer.serve_forever, daemon=True)
    thread.start()
    with tempfile.TemporaryDirectory(prefix="queue-failure-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        native, port = site(base, "native", packed)
        process = start(native, packed, base / "mdo-home", dict(os.environ, MDO_QUEUE_FIXTURE_KEY="fixture-only"))

        def call(method, path, body=None, expected=200):
            status, _, text = request(port, method, "/api/v1" + path,
                body=None if body is None else json.dumps(body).encode(), headers={"Content-Type": "application/json"})
            assert status == expected, (status, text[:1000])
            return json.loads(text)["data"]

        def wait(predicate):
            deadline = time.monotonic() + 15
            while time.monotonic() < deadline:
                value = predicate()
                if value:
                    return value
                time.sleep(.05)
            raise AssertionError("Managed queue failed to advance within its local test budget")

        try:
            status, boot = wait_bootstrap(process, port, native / "packed.log")
            assert status == 200 and boot["data"]["ready"]
            configure_model(port, f"http://127.0.0.1:{peer.server_port}/v1/chat/completions")
            session = call("POST", "/sessions", {"project_id": "default", "title": "Queue failure continuation",
                "model_id": "queue-fixture", "reasoning_effort": "none", "permission_profile": "balanced"}, 201)
            queue = f'/projects/default/sessions/{session["id"]}/queue'

            def enqueue(char):
                return call("POST", queue, {"id": char * 32, "text": "Controlled queue test " + char,
                    "first": False, "managed": True}, 201)

            enqueue("a")
            assert entered[0].wait(10)
            enqueue("b")
            release[0].set()
            # No /runs read is allowed to finish the previous runtime for us.
            wait(lambda: call("GET", queue) and entered[1].is_set())
            wait(lambda: all(row["terminal"] for row in call("GET", "/runs")["items"]))
            assert len(calls) == 2, calls
            state = call("GET", queue)
            assert not state["paused"] and not state["items"], state
            events_path = queue.removesuffix("/queue") + "/events"
            starts = [row for row in call("GET", events_path)["items"] if row["kind"] == "agent_start"]
            assert [row.get("queue_item_id") for row in starts] == ["a" * 32, "b" * 32], starts

            enqueue("c")
            assert entered[2].wait(10)
            enqueue("d")
            run = next(row for row in call("GET", "/runs")["items"] if not row["terminal"])
            call("DELETE", "/runs/" + run["id"])
            paused = wait(lambda: (q if (q := call("GET", queue))["paused"] else None))
            assert paused["error"] == "queue_stopped" and paused["items"][0]["id"] == "d" * 32, paused
            assert len(calls) == 3, calls
            return {"failed_reply_advances_next_message": True, "explicit_stop_preserves_remaining_queue": True,
                    "history_preserves_submission_ids": True,
                    "no_duplicate_model_requests": True, "real_model_calls": False}
        finally:
            for gate in release:
                gate.set()
            stop(process)
            peer.shutdown()
            peer.server_close()
            thread.join(timeout=2)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--packed", type=Path, required=True)
    parser.add_argument("--record", type=Path)
    args = parser.parse_args()
    result = check(args.packed.resolve())
    if args.record:
        args.record.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))
