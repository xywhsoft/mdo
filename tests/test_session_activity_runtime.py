"""Message ordering and durable completion results through the real xs API.

Uses a bounded loopback model and a disposable Home; no provider or user data.
"""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import subprocess
import tempfile
import threading
import time

from test_api_runtime import ROOT, free_port, request, wait_ready, write_site
from test_packed_queue_start_recovery import configure_model


def check(host):
    entered, release = threading.Event(), threading.Event()
    mode = "ok"
    calls = []

    class Model(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_POST(self):
            body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
            current = mode
            calls.append(current)
            entered.set()
            if current == "hold":
                release.wait(15)
            status = 400 if current == "fail" else 200
            result = {"error": {"message": "Controlled invalid request", "type": "invalid_request_error"}} if status == 400 else {
                "id": "activity-test", "model": body["model"], "choices": [{"index": 0,
                "message": {"role": "assistant", "content": "Completed reply."}, "finish_reason": "stop"}],
                "usage": {"prompt_tokens": 20, "completion_tokens": 3}}
            raw = json.dumps(result).encode()
            try:
                self.send_response(status)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(raw)))
                self.end_headers()
                self.wfile.write(raw)
            except (ConnectionError, OSError):
                pass

    server = ThreadingHTTPServer(("127.0.0.1", 0), Model)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    with tempfile.TemporaryDirectory(prefix="session-activity-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        port = free_port()
        config = write_site(base, port)
        home = base / "home"
        env = {**os.environ, "USERPROFILE": str(base), "HOME": str(base), "USE_WEBVIEW": "0",
               "MDO_QUEUE_FIXTURE_KEY": "fixture-only"}
        process = None
        log = (base / "host.log").open("wb")

        def start():
            nonlocal process
            process = subprocess.Popen([str(host), str(config), "--", "--home", str(home)],
                cwd=base, env=env, stdout=log, stderr=subprocess.STDOUT,
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            wait_ready(port, process)

        def stop():
            nonlocal process
            if process:
                process.terminate()
                process.wait(timeout=12)
                process = None

        def call(method, path, body=None, expected=200, headers=None):
            status, _, response = request(port, method, path,
                body=json.dumps(body).encode() if body is not None else None,
                headers={"Content-Type": "application/json", **(headers or {})})
            result = json.loads(response)
            assert status == expected, (method, path, status, result)
            return result.get("data", result)

        def wait(predicate):
            end = time.monotonic() + 12
            while time.monotonic() < end:
                result = predicate()
                if result:
                    return result
                time.sleep(.04)
            raise AssertionError("Activity state did not settle")

        def create(title):
            return call("POST", "/api/v1/sessions", {"project_id": "default", "title": title,
                "agent_id": "mdo.default", "model_id": "queue-fixture", "protocol": "openai-chat-completions",
                "reasoning_effort": "none"}, 201)

        def path(session):
            return f'/api/v1/projects/default/sessions/{session["id"]}'

        def meta(session):
            return call("GET", path(session))

        def order():
            return [s["id"] for s in call("GET", "/api/v1/sessions")["items"]]

        def run(session, prompt, timeout_ms=None):
            entered.clear()
            body = {"prompt": prompt}
            if timeout_ms is not None:
                body["timeout_ms"] = timeout_ms
            result = call("POST", path(session) + "/runs", body, 202)
            assert entered.wait(8), "loopback model did not start"
            return result

        def finish(result, expected):
            item = wait(lambda: (r if (r := call("GET", "/api/v1/runs/" + result["id"]))["terminal"] else None))
            assert item["state"] == expected, item

        try:
            start()
            configure_model(port, f"http://127.0.0.1:{server.server_port}/v1")
            a, b = create("Activity A"), create("Activity B")
            assert order()[:2] == [b["id"], a["id"]]
            original = meta(a)
            call("PUT", "/api/v1/workspace-state", {"project_id": "default", "session_id": a["id"]})
            current = meta(a)
            assert current["last_activated_at"] > original["last_activated_at"]
            assert current["last_message_at"] == original["last_message_at"]
            call("PATCH", path(a), {"title": "Renamed A"},
                 headers={"If-Match": f'"mdo-session-{a["id"]}-{current["revision"]}"'})
            assert meta(a)["last_message_at"] == original["last_message_at"]
            assert order()[:2] == [b["id"], a["id"]]

            mode = "hold"
            first = run(a, "first message")
            started = meta(a)
            assert started["last_message_at"] > original["last_message_at"]
            assert started["last_run_outcome"] == "" and started["last_run_at"] > 0
            assert order()[:2] == [a["id"], b["id"]]
            release.set()
            finish(first, "succeeded")
            succeeded = meta(a)
            assert succeeded["last_run_outcome"] == "succeeded"
            assert succeeded["last_message_at"] > started["last_message_at"], (started, succeeded)
            assert succeeded["last_run_at"] >= succeeded["last_message_at"]

            mode = "fail"
            # Repairable model errors now wait. An explicit caller deadline is
            # still a terminal failure and must preserve the unread red result.
            failed = run(a, "controlled failure", timeout_ms=1000)
            finish(failed, "timed_out")
            assert meta(a)["last_run_outcome"] == "failed"

            mode = "hold"
            release.clear()
            cancelled = run(a, "manual stop")
            assert meta(a)["last_run_outcome"] == ""
            call("DELETE", "/api/v1/runs/" + cancelled["id"])
            finish(cancelled, "cancelled")
            release.set()
            assert meta(a)["last_run_outcome"] == "cancelled"

            mode = "ok"
            last = run(b, "newer conversation")
            finish(last, "succeeded")
            assert order()[:2] == [b["id"], a["id"]]
            call("PUT", "/api/v1/workspace-state", {"project_id": "default", "session_id": a["id"]})
            assert order()[:2] == [b["id"], a["id"]]

            # New durable user input counts once, even before dispatch. A retry
            # of that HTTP request is not a second message and cannot reorder.
            q = path(a) + "/queue"
            before_queue = meta(a)["last_message_at"]
            item = {"id": "a" * 32, "text": "staged input", "first": False, "stage": True}
            call("POST", q, item, 201)
            admitted = meta(a)["last_message_at"]
            assert admitted > before_queue and order()[0] == a["id"]
            call("POST", q, item)
            assert meta(a)["last_message_at"] == admitted

            # A queue blocked before it can create a run still needs a red dot.
            c = create("Preflight blockage")
            before_calls = len(calls)
            call("POST", path(c) + "/queue", {"id": "c" * 32, "text": "invalid model profile",
                "first": True, "managed": True, "profile": {"model_id": "missing-model",
                "reasoning_effort": "none", "permission_profile": "balanced"}}, 201)
            wait(lambda: call("GET", path(c) + "/queue")["paused"])
            wait(lambda: meta(c)["last_run_outcome"] == "failed")
            assert len(calls) == before_calls
            saved = {s["id"]: (meta(s)["last_message_at"], meta(s)["last_run_outcome"]) for s in (a, b, c)}
            stop()
            start()
            assert call("GET", "/api/v1/runs")["items"] == []
            assert {s["id"]: (meta(s)["last_message_at"], meta(s)["last_run_outcome"]) for s in (a, b, c)} == saved
            assert [s["last_run_outcome"] for s in call("GET", "/api/v1/sessions")["items"]] == ["failed", "cancelled", "succeeded"]
            # Old metadata is seeded from its previous update time, never its
            # navigation clock. The first write freezes that seed permanently.
            stop()
            legacy_path = home / "sessions/default" / a["id"] / "meta.json"
            legacy = json.loads(legacy_path.read_text(encoding="utf-8"))
            for key in ("last_message_at_us", "last_run_at_us", "last_run_outcome"):
                legacy.pop(key)
            legacy_path.write_text(json.dumps(legacy), encoding="utf-8")
            start()
            seeded = meta(a)
            assert seeded["last_message_at"] == legacy["updated_at_us"]
            assert seeded["last_run_outcome"] == ""
            call("PUT", "/api/v1/workspace-state", {"project_id": "default", "session_id": a["id"]})
            renamed = call("PATCH", path(a), {"title": "Legacy seed remains stable"},
                headers={"If-Match": f'"mdo-session-{a["id"]}-{seeded["revision"]}"'})
            assert renamed["last_message_at"] == seeded["last_message_at"]
        except BaseException:
            stop()
            log.flush()
            print((base / "host.log").read_text(encoding="utf-8", errors="replace")[-5000:])
            raise
        finally:
            release.set()
            stop()
            log.close()
            server.shutdown()
            server.server_close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, default=ROOT / ".build/host/xs.exe")
    args = parser.parse_args()
    check(args.host.resolve())
    print("PASS message-only ordering, queue replay, success/failure/stop, preflight blockage and restart")
