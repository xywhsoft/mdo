"""Bounded packed restart: a durable Agent start resolves its queue claim."""

from __future__ import annotations

import argparse
import json
import os
import tempfile
import time
from pathlib import Path

from test_packed_home_lease import (
    ROOT, release_packed_copies, request, site, start, stop, wait_bootstrap,
)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--packed", type=Path, required=True)
    packed = parser.parse_args().packed.resolve()
    assert packed.is_file(), packed
    (ROOT / ".build").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="packed-queue-start-",
                                     dir=ROOT / ".build") as raw:
        base = Path(raw)
        first_site, first_port = site(base, "first", packed)
        second_site, second_port = site(base, "second", packed)
        home = base / "mdo-home"
        env = os.environ.copy()
        env["USERPROFILE"] = str(base)
        # Only localhost is used; the model call may fail after Agent Start.
        env["MDO_LING_RESPONSES_URL"] = "http://127.0.0.1:9/v1"
        env["MDO_LING_API_KEY"] = "bounded-queue-recovery-key"
        first = start(first_site, packed, home, env)
        second = None
        try:
            status, response = wait_bootstrap(first, first_port,
                                              first_site / "packed.log")
            assert status == 200 and response["data"]["ready"], response
            status, response = request(first_port, "POST", "/api/v1/sessions", {
                "project_id": "default", "title": "Queue start recovery QA",
                "agent_id": "mdo.default", "model_id": "ling-3.0-tiny",
                "protocol": "openai-responses", "reasoning_effort": "medium",
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
            item = response["data"]["items"][0]
            assert item["run_id"] == run_id and not item.get(
                "start_claimed", False), item
            assert json.loads(receipt_file.read_text(encoding="utf-8")) == {
                "schema_version": 1, "id": queue_id, "run_id": run_id,
            }
            status, response = request(second_port, "POST", path + "/runs", {
                "prompt": "durable packed start", "queue_item_id": queue_id,
            })
            assert status == 409 and response["error"]["code"] == \
                "queue_run_started", response
        finally:
            if second is not None:
                stop(second)
            stop(first)
            release_packed_copies(first_site / packed.name,
                                  second_site / packed.name)
    print("packed queue start recovery probe: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
