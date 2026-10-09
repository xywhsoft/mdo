"""Small offline regression for atomic reply/process history pages.

No model requests or user journal writes. Uses an isolated packed Home and
five replies, including tool-only calls, long paragraphs and a reused run ID.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import tempfile
from test_packed_home_lease import site, start, stop, request, wait_bootstrap

ROOT = Path(__file__).resolve().parents[1]


def records(session):
    items = []

    def add(kind, turn=1, text="", **fields):
        event_id = len(items) + 1
        item = dict(schema_version=6, event_id=event_id, source_event_id=event_id,
            occurred_at_us=event_id * 1_000_000, project_id="default", session_id=session,
            kind=kind, agent_turn=turn, user_message_sequence=0, agent_depth=0,
            agent_id=1, run_id=1, queue_item_id="", task_id=0, artifact_id=0, parent_run_id=0,
            effects=0, task_state=0, task_revision=0, input_tokens=10, output_tokens=5,
            total_tokens=15, success=True, effect_applied=False, text_truncated=False,
            text=text, tool_name="", tool_call_id="", artifact_path="", model="fixture",
            model_id="ornith-1.5-35b", context_window_tokens=128000)
        item.update(fields); items.append(item)
        return event_id

    add(0, text="原始问题", user_message_sequence=1)
    bodies, starts = [], []
    for n in range(1, 6):
        # The last continuation deliberately reuses run_id and model turn 1.
        turn = n * 3 if n < 5 else 1
        if n == 5:
            add(10, turn=12)
            add(0, turn=turn)  # recovery adds no new user input
        for extra in (0, 1):
            call = turn + extra
            add(1, call)
            add(3, call, f"工具调用前思考 {n}/{extra}")
            add(4, call)  # tool-only MODEL_DONE must not split the process
            starts.append(add(5, call, "读取结果", tool_call_id=f"tool-{n}-{extra}", tool_name="read"))
            # Child events cannot act as public reply boundaries.
            add(2, call, "子 Agent 回复", agent_depth=1, agent_id=2, run_id=2)
            add(6, call, "读取成功", tool_call_id=f"tool-{n}-{extra}", tool_name="read")
        add(1, turn + 2)
        add(3, turn + 2, f"回复前思考 {n}")
        text = f"完整回复 {n}：\n\n" + "段落内容。" * 2000 + f"\n\n回复 {n} 的结尾。"
        first = add(2, turn + 2, text[:6000]); last = add(2, turn + 2, text[6000:])
        bodies.append((first, last, text))
        add(4, turn + 2)
    add(10, turn=3)
    return items, bodies, starts


def probe(packed, replay_journal=None):
    with tempfile.TemporaryDirectory(prefix="conversation-groups-", dir=ROOT / ".build") as raw:
        base = Path(raw); path, port = site(base, "site", packed); home = base / "home"
        env = dict(os.environ, USERPROFILE=str(base), MDO_ORNITH_RESPONSES_URL="http://127.0.0.1:1/v1")
        process = start(path, packed, home, env)
        try:
            _, boot = wait_bootstrap(process, port, path / "packed.log")
            assert boot["data"]["ready"], boot
            status, doc = request(port, "POST", "/api/v1/sessions", dict(project_id="default",
                title="Complete group fixture", agent_id="mdo.default", model_id="ornith-1.5-35b",
                protocol="openai-responses", reasoning_effort="medium", max_output_tokens=1024))
            assert status == 201, doc
            session = doc["data"]["id"]
            stop(process); process = None
            items, bodies, starts = records(session)
            journal = home / "sessions/default" / session / "ui-events.jsonl"
            journal.write_text("".join(json.dumps(item, ensure_ascii=False) + "\n" for item in items), encoding="utf-8")
            original = journal.read_bytes()
            process = start(path, packed, home, env)
            wait_bootstrap(process, port, path / "packed.log")
            endpoint = f"/api/v1/projects/default/sessions/{session}/conversation"
            before, seen = 0, []
            for n in range(5, 0, -1):
                status, doc = request(port, "GET", endpoint + f"?limit=1&before={before}")
                assert status == 200, doc
                page = doc["data"]; events = json.loads(page["items_json"])
                assert hashlib.sha256(page["items_json"].encode()).hexdigest() == page["items_hash"]
                body = [e for e in events if e["kind"] == "model_text_delta" and e["agent_depth"] == 0]
                first, last, text = bodies[n - 1]
                assert len(body) == 1 and body[0]["event_id"] == first, (n, body)
                assert body[0]["aggregate_end_id"] == last and body[0]["text"] == text
                assert not body[0]["text_truncated"], n
                assert len([e for e in events if e["kind"] == "tool_start"]) == 2, (n,
                    [(e["event_id"], e["kind"], e["agent_turn"]) for e in events])
                assert len([e for e in events if e["kind"] == "tool_done"]) == 2, n
                assert any(e["kind"] == "model_start" and e["agent_turn"] == body[0]["agent_turn"] for e in events)
                assert any(e["kind"] == "model_done" and e["agent_turn"] == body[0]["agent_turn"] for e in events)
                ids = [e["event_id"] for e in events]
                assert not set(ids) & set(seen), n
                seen.extend(ids); before = page["next_before"]
                assert page["has_more"] == (n > 1), (n, page)
                if n == 5: epoch, latest = page["epoch"], page["latest_event_id"]
            assert journal.read_bytes() == original, "display reads modified the journal"
            status, doc = request(port, "GET", endpoint + f"?after={latest}&epoch={epoch}")
            assert status == 200 and doc["data"]["delta"] and json.loads(doc["data"]["items_json"]) == []
            # One escaped record can exceed the soft JSON budget. It must
            # still advance the live cursor instead of returning empty forever.
            extra = {**items[-1], "kind": 2, "event_id": latest + 1, "text": "\n" * 10_000 + "后续正文"}
            with journal.open("a", encoding="utf-8") as output:
                output.write(json.dumps(extra, ensure_ascii=False) + "\n")
            status, doc = request(port, "GET", endpoint + f"?after={latest}&epoch={epoch}")
            assert status == 200 and doc["data"]["next_cursor"] == latest + 1, doc
            assert json.loads(doc["data"]["items_json"])[0]["text"] == extra["text"]
            print("complete history groups: 5 replies, 10 tool pairs, long body, resume and delta PASS")
            if replay_journal:
                # Optional read-only reproduction of a user-reported journal.
                # Only the disposable Home receives a remapped copy.
                source = replay_journal.read_bytes()
                replay = [json.loads(line) for line in source.splitlines() if line.strip()]
                streams, expected = {}, {}
                for item in replay:
                    item["project_id"], item["session_id"] = "default", session
                    if item["kind"] in (0, 1, 4): streams.clear()
                    if item["kind"] == 2 and item["agent_depth"] == 0:
                        key = (item["run_id"], item["agent_id"], item["agent_turn"])
                        first = streams.setdefault(key, item["event_id"])
                        expected[first] = expected.get(first, "") + item["text"]
                stop(process); process = None
                journal.write_text("".join(json.dumps(item, ensure_ascii=False) + "\n" for item in replay), encoding="utf-8")
                process = start(path, packed, home, env); wait_bootstrap(process, port, path / "packed.log")
                before, verified = 0, 0
                for _ in range(3):
                    status, doc = request(port, "GET", endpoint + f"?limit=4&before={before}")
                    assert status == 200, doc
                    page = doc["data"]
                    for event in json.loads(page["items_json"]):
                        if event["kind"] == "model_text_delta" and event["agent_depth"] == 0:
                            assert event["text"] == expected[event["event_id"]], event["event_id"]
                            verified += 1
                    before = page["next_before"]
                    if not page["has_more"]: break
                assert verified > 0 and replay_journal.read_bytes() == source
                print(f"read-only journal replay: {verified} complete public replies across 3 pages PASS")
        finally:
            if process: stop(process)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--packed", type=Path, required=True)
    parser.add_argument("--replay-journal", type=Path)
    args = parser.parse_args()
    probe(args.packed.resolve(), args.replay_journal)
