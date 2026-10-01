"""Read a bounded v2 session backup from the executable's embedded app only.

One ordinary 2 MiB artifact crosses the transport queue limit. No model, shell,
queue execution, live user Home, pressure or high-load test is involved. This
checks packed routing/TCC/VFS delivery; it is not a restore or GUI download test.
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import tempfile
import time

from test_api_runtime import request as raw_request
from test_packed_home_lease import (
    ROOT, release_packed_copies, request, site, start, stop, wait_bootstrap,
)

ARTIFACT = "artifacts/run-00000000000000000001/00000000000000000001-packed.txt"


def download(port: int, path: str) -> dict[str, bytes]:
    # A fully received response can precede the executor's final slot release.
    # Retry only that documented busy result; other errors fail immediately.
    deadline = time.monotonic() + 3
    while True:
        status, headers, data = raw_request(port, "GET", path + "/backup")
        if status != 503 or json.loads(data)["error"]["code"] != "session_backup_busy":
            break
        assert time.monotonic() < deadline, (status, data)
        time.sleep(0.01)
    assert status == 200 and len(data) > 1024 * 1024, (status, data[:1024])
    assert headers["content-length"] == str(len(data))
    assert headers["content-type"] == "application/json; charset=utf-8"
    assert headers["connection"] == "close"
    assert headers["etag"] == f'"mdo-backup-sha256-{hashlib.sha256(data).hexdigest()}"'
    assert headers["content-disposition"].endswith('.backup.json"')
    document = json.loads(data)
    assert document["export_schema"] == 2 and document["restore_ready"] is False
    files = {}
    for item in document["files"]:
        content = base64.b64decode(item["data"], validate=True)
        assert item["path"] not in files
        assert len(content) == item["bytes"]
        assert hashlib.sha256(content).hexdigest() == item["sha256"]
        files[item["path"]] = content
    assert document["file_count"] == len(files)
    assert document["total_bytes"] == sum(map(len, files.values()))
    return files


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--packed", type=Path, required=True)
    packed = parser.parse_args().packed.resolve()
    assert packed.is_file(), packed
    (ROOT / ".build").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="packed-backup-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        first_site, first_port = site(base, "first", packed)
        second_site, second_port = site(base, "moved", packed)
        home = base / "mdo-home"
        env = dict(os.environ, USERPROFILE=str(base),
                   MDO_LING_RESPONSES_URL="http://127.0.0.1:9/v1",
                   MDO_LING_API_KEY="bounded-packed-backup-key")
        first = start(first_site, packed, home, env)
        second = None
        try:
            status, response = wait_bootstrap(first, first_port, first_site / "packed.log")
            assert status == 200 and response["data"]["ready"], response
            status, response = request(first_port, "POST", "/api/v1/sessions", {
                "project_id": "default", "title": "Packed backup 便携",
                "agent_id": "mdo.default", "model_id": "ling-3.0-tiny",
                "protocol": "openai-responses", "reasoning_effort": "medium",
                "max_output_tokens": 1024,
            })
            assert status == 201, response
            session_id = response["data"]["id"]
            path = f"/api/v1/projects/default/sessions/{session_id}"
            directory = home / "sessions/default" / session_id
            status, response = request(first_port, "PUT", path + "/draft", {
                "revision": 0, "text": "便携草稿 / portable draft",
            })
            assert status == 200, response
            status, response = request(first_port, "POST", path + "/queue", {
                "id": "d" * 32, "text": "requires user confirmation", "first": False, "stage": True,
            })
            assert status == 201, response
            # Only this newly created, idle fixture session is seeded directly.
            payload = bytes(range(256)) * 8192
            artifact = directory / ARTIFACT
            artifact.parent.mkdir(parents=True)
            artifact.write_bytes(payload)
            files = download(first_port, path)
            assert files[ARTIFACT] == payload
            assert json.loads(files["draft.json"])["text"] == "便携草稿 / portable draft"
            assert json.loads(files["queue.json"])["items"][0]["id"] == "d" * 32
            assert json.loads(files["meta.json"])["title"] == "Packed backup 便携"
            stop(first)
            second = start(second_site, packed, home, env)
            status, response = wait_bootstrap(second, second_port, second_site / "packed.log")
            assert status == 200 and response["data"]["ready"], response
            moved_files = download(second_port, path)
            # Checkpoint/capture timestamps can change; retained user content
            # must remain exact after loading the same Home at a new exe path.
            for name in (ARTIFACT, "draft.json", "queue.json", "meta.json"):
                assert moved_files[name] == files[name], name
            for source in (first_site, second_site):
                assert sorted(p.name for p in source.iterdir()) == [packed.name, "packed.log", "xs.json"]
            # No run was dispatched by either export or restart.
            status, response = request(second_port, "GET", path + "/queue")
            item = response["data"]["items"][0]
            assert status == 200 and "run_id" not in item and not item.get("start_claimed"), response
        finally:
            if second is not None:
                stop(second)
            stop(first)
            release_packed_copies(first_site / packed.name, second_site / packed.name)
    print("packed embedded VFS backup/restart probe: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
