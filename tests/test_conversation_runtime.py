"""Bounded history regressions: one offline journal fixture, not a load test.

Exercises reverse summary pages, byte seeking, restart and append beyond the
old 16 MiB eviction boundary, using the real xs/TCC API and a local model.
"""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import threading
import time
from http.server import ThreadingHTTPServer
from test_live_runtime import Model
from test_packed_home_lease import free_port, request, stop, wait_bootstrap

ROOT = Path(__file__).resolve().parents[1]


def probe(host: Path | None, packed: Path | None = None):
    model = ThreadingHTTPServer(("127.0.0.1", 0), Model)
    threading.Thread(target=model.serve_forever, daemon=True).start()
    process = None
    with tempfile.TemporaryDirectory(prefix="conversation-history-", dir=ROOT / ".build") as raw:
        base = Path(raw); site = base / "site"; home = base / "home"
        if packed:
            site.mkdir(); shutil.copy2(packed, site / packed.name)
        else:
            shutil.copytree(ROOT / "app", site)
        port = free_port()
        (site / "xs.json").write_text(json.dumps({"services": [{"class": "http", "name": "mdo",
            "ip": "127.0.0.1", "port": port, "enabled": True,
            "host_default": {"name": "mdo", "enabled": True, "path": "web",
                "devlang": "c", "devfile": "generated/mdo_unity.c"}}]}), encoding="utf-8")
        env = os.environ.copy()
        env.update(MDO_ORNITH_API_KEY="history-fixture-key",
            MDO_ORNITH_CHAT_COMPLETIONS_URL=f"http://127.0.0.1:{model.server_port}/v1",
            MDO_ORNITH_RESPONSES_URL=f"http://127.0.0.1:{model.server_port}/v1",
            MDO_ORNITH_ANTHROPIC_URL="https://example.invalid")

        def launch():
            with (site / "native.log").open("ab") as log:
                command = [str(site / packed.name), "--", "--home", str(home)] if packed else [
                    str(host), str(site / "xs.json"), "--", "--home", str(home)]
                value = subprocess.Popen(command,
                    cwd=site, env=env, stdout=log, stderr=subprocess.STDOUT,
                    creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
            wait_bootstrap(value, port, site / "native.log")
            return value

        def run(path):
            status, doc = request(port, "POST", path + "/runs", {"prompt": "history fixture", "timeout_ms": 10000})
            assert status == 202, doc
            deadline = time.monotonic() + 6
            while time.monotonic() < deadline:
                _, state = request(port, "GET", f"/api/v1/runs/{doc['data']['id']}")
                if state["data"]["terminal"]: return
                time.sleep(.05)
            raise AssertionError("ordinary fixture reply did not complete")

        try:
            process = launch()
            status, doc = request(port, "POST", "/api/v1/sessions", {"project_id": "default",
                "title": "History fixture", "agent_id": "mdo.default", "model_id": "ornith-1.5-35b",
                "reasoning_effort": "medium", "permission_profile": "read-only",
                "protocol": "openai-chat-completions", "max_output_tokens": 1024})
            assert status == 201, doc
            session = doc["data"]["id"]
            path = f"/api/v1/projects/default/sessions/{session}"
            run(path)
            journal = home / "sessions/default" / session / "ui-events.jsonl"
            originals = [json.loads(line) for line in journal.read_text(encoding="utf-8").splitlines()]
            starts = next(item for item in originals if item["kind"] == 0)
            delta = next(item for item in originals if item["kind"] == 2)
            done = next(item for item in originals if item["kind"] == 10)
            stop(process); process = None
            records = []
            for turn in range(70):
                first = turn * 6 + 1
                for offset, template in enumerate([starts, delta, delta, delta, delta, done]):
                    event = {**template, "event_id": first + offset, "run_id": turn + 1,
                        "agent_id": 1, "agent_turn": 1, "agent_depth": 0,
                        "occurred_at_us": (first + offset) * 1000000,
                        "user_message_sequence": turn + 1 if offset == 0 else 0}
                    event["text"] = f"Question {turn}" if offset == 0 else (
                        f"Answer {turn} " + "x" * 62000 if offset < 5 else "")
                    records.append(json.dumps(event, ensure_ascii=False, separators=(",", ":")).encode() + b"\n")
            journal.write_bytes(b"".join(records))
            original_size = journal.stat().st_size
            assert original_size > 16 * 1024 * 1024
            process = launch()
            _, doc = request(port, "GET", path + "/turns?limit=4")
            page = doc["data"]
            assert [item["question"] for item in page["items"]] == [f"Question {i}" for i in range(66, 70)]
            assert all(item["state"] == "done" and item["answer"].startswith("Answer ") for item in page["items"])
            assert page["latest_event_id"] == 420 and page["has_more"]
            _, doc = request(port, "GET", path + f"/turns?before={page['next_before']}&limit=64")
            assert len(doc["data"]["items"]) == 64 and doc["data"]["has_more"]
            _, doc = request(port, "GET", path + "/turns?before=13&limit=64")
            assert [item["first_event_id"] for item in doc["data"]["items"]] == [1, 7]
            assert not doc["data"]["has_more"]
            for after in (0, 1, 31, 211, 389, 419, 420, 999):
                _, doc = request(port, "GET", path + f"/events?after={after}&limit=32")
                items = doc["data"]["items"]
                assert [item["event_id"] for item in items] == list(range(after + 1, min(after + 33, 421)))
                assert doc["data"]["latest_event_id"] == 420 and not doc["data"]["history_lost"]
            for query in ("limit=65", "limit=0", "before=-1", "limit=4&limit=4", "before=1&"):
                status, _ = request(port, "GET", path + "/turns?" + query)
                assert status == 400, query
            # A recovery run reuses model turn 1 but has no new user input.
            # It belongs to the last question; do not prepend the older run's
            # text to its final-answer summary or invent a navigation entry.
            stop(process); process = None
            with journal.open("ab") as file:
                for offset, template in enumerate([starts, delta, done]):
                    event = {**template, "event_id": 421 + offset, "run_id": 900,
                        "agent_depth": 0, "agent_turn": 1, "user_message_sequence": 0,
                        "text": "Continuation" if offset == 1 else ""}
                    file.write(json.dumps(event, separators=(",", ":")).encode() + b"\n")
            process = launch()
            _, doc = request(port, "GET", path + "/turns?limit=1")
            assert doc["data"]["items"][0]["question"] == "Question 69"
            assert doc["data"]["items"][0]["answer"] == "Continuation"
            run(path)
            assert journal.stat().st_size > original_size
            with journal.open("rb") as file: assert file.readline() == records[0], "append discarded old history"
            stop(process); process = None
            process = launch()
            _, doc = request(port, "GET", path + "/events?after=0&limit=1")
            assert doc["data"]["items"][0]["text"] == "Question 0"
            assert doc["data"]["latest_event_id"] > 420
        finally:
            if process: stop(process)
            model.shutdown(); model.server_close()
    print("PASS conversation history: reverse pages, summaries, seek, unlimited append, restart, query validation")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(); source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--host", type=Path); source.add_argument("--packed", type=Path)
    args = parser.parse_args()
    probe(args.host.resolve() if args.host else None, args.packed.resolve() if args.packed else None)
