"""Queue ownership, ordering, extraction and background dispatch against xs/TCC.

Uses one disposable Home and a bounded loopback model. Never contacts a provider.
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


class Model(BaseHTTPRequestHandler):
    calls = []
    release = threading.Event()
    started = threading.Event()
    hold = False

    def log_message(self, *_):
        pass

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        type(self).calls.append(body)
        if self.hold:
            type(self).hold = False
            self.started.set()
            self.release.wait(25)
        raw = json.dumps({"id": "queue-test", "model": body["model"],
            "choices": [{"index": 0, "message": {"role": "assistant", "content": "Done."},
                         "finish_reason": "stop"}],
            "usage": {"prompt_tokens": 20, "completion_tokens": 2}}).encode()
        try:
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(raw)))
            self.end_headers()
            self.wfile.write(raw)
        except (ConnectionError, OSError):
            pass


def check(host):
    with tempfile.TemporaryDirectory(prefix="queue-actions-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        port = free_port()
        config = write_site(base, port)
        home = base / "home"
        environment = {**os.environ, "USERPROFILE": str(base), "HOME": str(base),
                       "USE_WEBVIEW": "0", "MDO_QUEUE_FIXTURE_KEY": "fixture-only"}
        server = ThreadingHTTPServer(("127.0.0.1", 0), Model)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        process = None
        log = (base / "queue.log").open("wb")

        def start():
            nonlocal process
            process = subprocess.Popen([str(host), str(config), "--", "--home", str(home)],
                cwd=base, env=environment, stdout=log, stderr=subprocess.STDOUT,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
            wait_ready(port, process)

        def stop():
            nonlocal process
            if process:
                process.terminate()
                process.wait(timeout=12)
                process = None

        def call(method, path, body=None, expected=200):
            status, _, data = request(port, method, path,
                body=json.dumps(body).encode() if body is not None else None,
                headers={"Content-Type": "application/json"} if body is not None else None)
            reply = json.loads(data)
            assert status == expected, (method, path, status, reply)
            return reply.get("data", reply)

        def wait(predicate, seconds=12):
            until = time.monotonic() + seconds
            while time.monotonic() < until:
                result = predicate()
                if result:
                    return result
                time.sleep(.1)
            raise AssertionError("Queue operation did not finish")

        try:
            start()
            configure_model(port, f"http://127.0.0.1:{server.server_port}/v1")
            session = call("POST", "/api/v1/sessions", {"project_id": "default", "title": "Queue QA",
                "agent_id": "mdo.default", "model_id": "queue-fixture",
                "protocol": "openai-chat-completions", "reasoning_effort": "none"}, 201)
            path = f'/api/v1/projects/default/sessions/{session["id"]}'
            q = path + "/queue"
            image_bytes = (ROOT / "app/web/assets/mdo-app-icon.png").read_bytes()
            image_status, _, image_raw = request(port, "POST", path + "/attachments", body=image_bytes,
                headers={"Content-Type": "image/png"})
            assert image_status == 201, image_raw
            image_id = json.loads(image_raw)["data"]["id"]
            Model.hold = True
            run = call("POST", path + "/runs", {"prompt": "original running task"}, 202)
            assert Model.started.wait(5)
            ids = [str(i) * 32 for i in range(1, 4)]
            profile = {"model_id": "queue-fixture", "reasoning_effort": "none", "permission_profile": "balanced"}
            for index, item in enumerate(ids):
                call("POST", q, {"id": item, "text": f"queued-{index}", "attachments": [image_id] if index == 1 else [],
                    "first": False, "managed": True, "profile": profile}, 201)

            sequence = 10

            def action(kind, item="", before="", draft_revision=0):
                nonlocal sequence
                for _ in range(8):
                    sequence += 1
                    body = {"id": f"{sequence:032x}", "kind": kind, "item": item, "before": before,
                        "revision": call("GET", q)["revision"], "draft_revision": draft_revision}
                    status, _, raw_reply = request(port, "POST", q + "/actions", body=json.dumps(body).encode(),
                        headers={"Content-Type": "application/json"})
                    reply = json.loads(raw_reply)
                    if status == 409 and reply["error"]["code"] == "queue_revision_conflict":
                        continue
                    return status, reply, body
                raise AssertionError("Queue never reached a stable revision")

            wait(lambda: call("GET", q)["last_run_id"] == run["id"])
            status, moved, move = action("move", ids[2], ids[0])
            assert status == 200, moved
            assert [x["id"] for x in moved["data"]["items"]] == [ids[2], ids[0], ids[1]]
            assert call("POST", q + "/actions", move)["revision"] == moved["data"]["revision"]
            stale = {**move, "id": "f" * 32}
            assert call("POST", q + "/actions", stale, 409)["error"]["code"] == "queue_revision_conflict"

            status, withdrawn, extraction = action("withdraw", ids[1])
            assert status == 200, withdrawn
            draft = call("GET", path + "/draft")
            assert draft["text"] == "queued-1" and draft["composer_profile"] == profile
            assert draft["attachments"] == [image_id]
            assert call("POST", q + "/actions", extraction)["items"] == withdrawn["data"]["items"]
            status, refused, _ = action("withdraw", ids[0], draft_revision=draft["revision"])
            assert status == 409 and refused["error"]["code"] == "draft_conflict", refused
            assert call("GET", path + "/draft")["text"] == "queued-1"

            status, steered, _ = action("steer", ids[2])
            assert status == 200, steered
            wait(lambda: not call("GET", q)["items"])
            Model.release.set()
            wait(lambda: len(Model.calls) >= 3)
            assert call("GET", "/api/v1/runs/" + run["id"])["cancel_requested"]
            last_inputs = [[x for x in body["messages"] if x["role"] == "user"][-1]["content"]
                           for body in Model.calls[:3]]
            assert last_inputs == ["original running task", "queued-2", "queued-0"], last_inputs
            assert call("GET", path + "/draft")["text"] == "queued-1"
            wait(lambda: all(row["terminal"] for row in call("GET", "/api/v1/runs")["items"]))
            assert call("DELETE", path + "/attachments/" + image_id, expected=409)["error"]["code"] == "attachment_in_use"
            backup_status, _, backup = request(port, "GET", path + "/backup")
            assert backup_status == 200 and b"queue-withdrawn/" in backup, (backup_status, backup[:500])

            # A stopped/failed predecessor holds subsequent messages. A manual
            # resume is needed, so a page reload cannot silently restart work.
            Model.started.clear(); Model.release.clear(); Model.hold = True
            wait(lambda: all(row["terminal"] for row in call("GET", "/api/v1/runs")["items"]))
            second = call("POST", path + "/runs", {"prompt": "stop fixture"}, 202)
            assert Model.started.wait(5)
            fourth = "4" * 32
            call("POST", q, {"id": fourth, "text": "after stop", "first": False, "managed": True}, 201)
            wait(lambda: call("GET", q)["last_run_id"] == second["id"])
            call("DELETE", "/api/v1/runs/" + second["id"])
            paused = wait(lambda: (state if (state := call("GET", q))["paused"] else None))
            assert paused["error"] == "queue_stopped" and paused["items"][0]["id"] == fourth
            Model.release.set()
            status, resumed, _ = action("resume")
            assert status == 200, resumed
            wait(lambda: not call("GET", q)["items"])

            # Replay the on-disk handoff state immediately after draft commit,
            # including a subsequent user edit. Recovery must never overwrite it.
            wait(lambda: all(row["terminal"] for row in call("GET", "/api/v1/runs")["items"]))
            saved = call("GET", q)
            stop()
            saved["schema_version"] = 8
            saved["items"] = [{"id": ids[1], "text": "queued-1", "attachments": [image_id],
                "priority": False, "profile": profile, "state": "editing", "edit": extraction}]
            saved["actions"] = [a for a in saved["actions"] if a["id"] != extraction["id"]]
            saved["last_run_id"] = ""
            session_home = home / "sessions/default" / session["id"]
            (session_home / "queue.json").write_text(json.dumps(saved), encoding="utf-8")
            saved_draft = json.loads((session_home / "draft.json").read_text(encoding="utf-8"))
            saved_draft["revision"] += 1
            saved_draft["text"] = "user edited restored draft"
            (session_home / "draft.json").write_text(json.dumps(saved_draft), encoding="utf-8")
            start()
            wait(lambda: not call("GET", q)["items"])
            assert call("GET", path + "/draft")["text"] == "user edited restored draft"
            assert call("POST", q, {"id": ids[1], "text": "queued-1", "first": False}, 409)["error"]["code"] == "queue_item_consumed"
            # A paused, obsolete steering reservation must permit replanning.
            # Explicit resume clears its old cancellation target and priorities.
            saved = call("GET", q)
            stop()
            saved.update(schema_version=8, managed=True, paused=True, error="queue_run_changed",
                cancel_run_id=second["id"], last_run_id=second["id"], items=[
                    {"id": "5" * 32, "text": "old priority", "attachments": [], "priority": True, "state": "pending"},
                    {"id": "6" * 32, "text": "replanned first", "attachments": [], "priority": False, "state": "pending"}])
            (session_home / "queue.json").write_text(json.dumps(saved), encoding="utf-8")
            start()
            status, replanned, _ = action("move", "6" * 32, "5" * 32)
            assert status == 200 and replanned["data"]["items"][0]["id"] == "6" * 32, replanned
            status, resumed, _ = action("resume")
            assert status == 200 and resumed["data"]["cancel_run_id"] == "", resumed
            assert all(not row["priority"] for row in resumed["data"]["items"]), resumed
            wait(lambda: not call("GET", q)["items"])
            print("Queue runtime: ordering, conflict, idempotency, extraction, steer, headless drain, stop/resume, restart: PASS")
        except BaseException:
            if process and process.poll() is None:
                print("QUEUE", call("GET", q))
                print("RUNS", call("GET", "/api/v1/runs"))
                print("MODEL_CALLS", len(Model.calls))
            log.flush()
            print((base / "queue.log").read_text(encoding="utf-8", errors="replace")[-9000:])
            raise
        finally:
            Model.release.set()
            stop()
            server.shutdown(); server.server_close(); log.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, default=ROOT / ".build/host/xs.exe")
    check(parser.parse_args().host.resolve())
