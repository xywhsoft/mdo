"""Small, serial history-edit replay probe against the packed xs/TCC runtime.

Uses only the loopback model fixture and its disposable Home. No provider
credentials, external model requests, concurrency or load testing are used.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from test_api_runtime import ROOT, request, session_events


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--packed", required=True, type=Path)
    parser.add_argument("--record", type=Path)
    args = parser.parse_args()
    packed = args.packed.resolve()
    assert packed.is_file(), packed
    results: list[str] = []
    with tempfile.TemporaryDirectory(prefix="message-edit-probe-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        fixture = base / "fixture"
        log = (base / "fixture.log").open("wb")
        env = dict(os.environ, USE_WEBVIEW="0")
        child = subprocess.Popen([sys.executable, str(ROOT / "tests/manual_conversation_retry_qa.py"),
            "--packed", str(packed), "--directory", str(fixture), "--duration", "300"],
            cwd=ROOT, env=env, stdout=log, stderr=log,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        try:
            deadline = time.monotonic() + 40
            while not (fixture / "state.json").exists():
                assert child.poll() is None, (base / "fixture.log").read_text(errors="replace")
                assert time.monotonic() < deadline, "packed fixture did not become ready"
                time.sleep(.1)
            state = json.loads((fixture / "state.json").read_text())
            port, session = state["port"], state["session"]
            path = f"/api/v1/projects/default/sessions/{session}"
            directory = fixture / "home/sessions/default" / session

            def call(method: str, target: str, body=None, etag=None, **extra):
                headers = dict(extra)
                if body is not None: headers["Content-Type"] = "application/json"
                if etag is not None: headers["If-Match"] = etag
                status, headers, payload = request(port, method, target,
                    body=None if body is None else json.dumps(body).encode(), headers=headers)
                return status, headers, json.loads(payload)

            def files():
                return {name: (directory / name).read_bytes() if (directory / name).exists() else None
                    for name in ("meta.json", "snapshot.json", "journal.jsonl", "ui-events.jsonl", "todo.json")}

            def check_error(body, etag, status, code):
                before = files()
                actual, _, response = call("POST", path + "/truncate", body, etag)
                assert actual == status and response["error"]["code"] == code, (actual, response)
                assert files() == before, "a refused edit modified session files"

            for prompt in ("EDIT_PROTOCOL_FIRST", "EDIT_PROTOCOL_SECOND"):
                status, _, response = call("POST", path + "/runs", {"prompt": prompt})
                assert status == 202, (status, response)
                run = response["data"]["id"]
                deadline = time.monotonic() + 15
                while True:
                    status, _, response = call("GET", f"/api/v1/runs/{run}")
                    assert status == 200, response
                    if response["data"]["state"] not in ("queued", "starting", "running"):
                        assert response["data"]["state"] == "succeeded", response
                        # Terminal UI state can precede runtime release. Do
                        # not make this protocol probe race that separate
                        # admission path when starting its second turn.
                        status, _, metadata = call("GET", path)
                        assert status == 200, metadata
                        if not metadata["data"]["runtime_open"]: break
                    assert time.monotonic() < deadline, "loopback turn did not finish"
                    time.sleep(.05)

            source = next(row for row in session_events(port, path)
                if row["kind"] == "agent_start" and row["text"] == "EDIT_PROTOCOL_SECOND")
            status, headers, response = call("GET", path)
            assert status == 200, response
            original_etag, original_revision = headers["etag"], response["data"]["revision"]
            edit = {"through_sequence": source["user_message_sequence"] - 1,
                "source_event_id": source["event_id"], "client_edit_id": "a" * 32}
            check_error(edit, None, 428, "precondition_required")
            check_error(edit, "invalid", 400, "invalid_precondition")
            check_error(edit, '\"mdo-session-other-1\"', 412, "revision_conflict")
            for bad in ("A" * 32, "", "short", None):
                check_error({**edit, "client_edit_id": bad}, original_etag, 422, "session_truncate_invalid")
            check_error({"through_sequence": 0, "client_edit_id": "b" * 32},
                original_etag, 422, "session_truncate_invalid")
            check_error({**edit, "source_event_id": source["event_id"] + 1},
                original_etag, 409, "session_message_changed")
            results.append("invalid inputs, missing preconditions and source guards leave all session files unchanged")

            status, headers, response = call("POST", path + "/truncate", edit, original_etag)
            assert status == 200, (status, response)
            assert response["data"]["revision"] == original_revision + 1, response
            committed_etag, committed_files = headers["etag"], files()
            for _ in range(2):
                status, headers, replay = call("POST", path + "/truncate", edit, original_etag)
                # Runtime presence is deliberately fresh: replay loads only
                # metadata and must not reopen the Agent just to reply.
                stable = lambda info: {key: value for key, value in info.items() if key != "runtime_open"}
                assert status == 200 and stable(replay["data"]) == stable(response["data"]), (status, replay)
                assert headers["etag"] == committed_etag and replay["data"]["runtime_open"] is False
                assert files() == committed_files, "replay trimmed history or advanced its revision"
            events = session_events(port, path)
            assert sum(row["kind"] == "history_truncated" for row in events) == 1, events
            assert not any(row.get("text") == "EDIT_PROTOCOL_SECOND" for row in events), events
            results.append("two replays return the original successful response without a second cut or revision change")
            check_error({**edit, "through_sequence": 0}, original_etag, 409, "session_edit_conflict")
            check_error({**edit, "client_edit_id": "c" * 32}, original_etag, 412, "revision_conflict")
            check_error({**edit, "client_edit_id": "c" * 32}, committed_etag, 409, "session_message_changed")
            results.append("changed replay parameters, a new ID with an old revision and a removed source are refused")

            status, headers, response = call("PATCH", path, {"title": "Updated after edit"}, committed_etag)
            assert status == 200, (status, response)
            renamed_etag = headers["etag"]
            check_error(edit, original_etag, 412, "revision_conflict")
            first = next(row for row in session_events(port, path)
                if row["kind"] == "agent_start" and row["text"] == "EDIT_PROTOCOL_FIRST")
            legacy = {"through_sequence": first["user_message_sequence"] - 1,
                "source_event_id": first["event_id"]}
            status, headers, response = call("POST", path + "/truncate", legacy, renamed_etag)
            assert status == 200, (status, response)
            check_error(legacy, headers["etag"], 409, "session_message_changed")
            results.append("a later session change invalidates cached replies; legacy guarded cuts still work")

            _, old_headers, _ = call("GET", "/api/v1/bootstrap")
            old_token, old_pid = old_headers["x-mdo-write-token"], state["pid"]
            (fixture / "restart").touch()
            deadline = time.monotonic() + 25
            while True:
                refreshed = json.loads((fixture / "state.json").read_text())
                if refreshed["pid"] != old_pid: break
                assert time.monotonic() < deadline, "fixture restart timed out"
                time.sleep(.1)
            before = files()
            status, _, response = call("POST", path + "/truncate", edit, original_etag,
                **{"X-Mdo-Write-Token": old_token})
            assert status == 412 and response["error"]["code"] == "write_token_conflict", (status, response)
            assert files() == before
            results.append("host restart fences the old page instead of replaying its writes")
        finally:
            (fixture / "stop").touch()
            try: child.wait(timeout=20)
            except subprocess.TimeoutExpired:
                child.terminate(); child.wait(timeout=10)
            log.close()
        assert child.returncode == 0, (base / "fixture.log").read_text(errors="replace")
        calls = json.loads((fixture / "calls.json").read_text())
        assert calls == {"EDIT_PROTOCOL_FIRST": 1, "EDIT_PROTOCOL_SECOND": 1}, calls
        results.append("only the two explicitly started loopback model turns executed")
    record = {"passed": True, "checks": results, "model_requests": 2,
        "packed_sha256": hashlib.sha256(packed.read_bytes()).hexdigest()}
    if args.record:
        args.record.parent.mkdir(parents=True, exist_ok=True)
        args.record.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(record, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
