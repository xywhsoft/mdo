"""One bounded restore/restart from an executable's embedded TCC/VFS sources.

Idle source, draft/staged queue, ordinary PNG and 2 MiB artifact; no live model,
tools, user Home, stress or high-load. No product source/fault injection.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import tempfile
import time

from fixture_images import ordinary_png
from test_api_runtime import request as raw_request
from test_packed_home_lease import ROOT, release_packed_copies, request, site, start, stop, wait_bootstrap


def inventory(directory):
    return {p.relative_to(directory).as_posix(): p.read_bytes() for p in directory.rglob("*") if p.is_file()}


def restore(port, home):
    status, value = request(port, "POST", "/api/v1/sessions", {
        "project_id": "default", "title": "Packed restore 中文", "agent_id": "mdo.default",
        "model_id": "ling-3.0-tiny", "protocol": "openai-responses", "reasoning_effort": "medium",
        "max_output_tokens": 1024,
    })
    assert status == 201, value
    source = value["data"]["id"]
    path = f"/api/v1/projects/default/sessions/{source}"
    assert request(port, "PUT", path + "/draft", {"revision": 0, "text": "Review imported draft"})[0] == 200
    assert request(port, "POST", path + "/queue", {
        "id": "d" * 32, "text": "Never run automatically", "first": False, "stage": True,
    })[0] == 201
    png = ordinary_png()
    status, _, data = raw_request(port, "POST", path + "/attachments", body=png, headers={"Content-Type": "image/png"})
    assert status == 201, data[:512]
    image_id = json.loads(data)["data"]["id"]
    original = home / "sessions/default" / source
    artifact_path = "artifacts/run-00000000000000000001/00000000000000000001-packed-restore.txt"
    payload = bytes(range(256)) * 8192
    artifact = original / artifact_path
    artifact.parent.mkdir(parents=True)
    artifact.write_bytes(payload)
    status, _, data = raw_request(port, "GET", path + "/backup")
    assert status == 200, data[:512]
    manifest = json.loads(data)
    assert manifest["export_schema"] == 2 and manifest["restore_ready"] is False
    before = inventory(original)
    upload = "/api/v1/session-backups/uploads/" + "e" * 32
    digest = hashlib.sha256(data).hexdigest()
    assert request(port, "POST", "/api/v1/session-backups/uploads", {
        "id": "e" * 32, "bytes": len(data), "sha256": digest,
    })[0] == 201
    for offset in range(0, len(data), 256 * 1024):
        status, _, response = raw_request(port, "PUT", upload + f"/chunks/{offset}",
            body=data[offset:offset + 256 * 1024], headers={"Content-Type": "application/octet-stream"})
        assert status == 200, response[:512]
    assert request(port, "POST", upload + "/seal")[0] == 200
    status, value = request(port, "POST", upload + "/preview")
    assert status == 202, value
    preview = "/api/v1/session-backups/previews/" + value["data"]["id"]
    deadline = time.monotonic() + 5
    while True:
        status, value = request(port, "GET", preview)
        assert status == 200, value
        if value["data"]["terminal"]:
            assert value["data"]["state"] == "succeeded", value
            break
        assert time.monotonic() < deadline, value
        time.sleep(0.01)
    status, value = request(port, "POST", preview + "/restore-review", {"project_id": "default"})
    assert status == 201 and not value["data"]["accepted"], value
    target_id = value["data"]["id"]
    target_path = "/api/v1/session-backups/restores/" + target_id
    assert request(port, "POST", target_path + "/apply")[0] == 202
    deadline = time.monotonic() + 5
    while True:
        status, value = request(port, "GET", target_path)
        assert status == 200, value
        if value["data"].get("worker_finished"):
            assert value["data"]["committed"] and value["data"]["state"] == "committed", value
            break
        assert time.monotonic() < deadline, value
        time.sleep(0.01)
    target = home / "sessions/default" / target_id
    actual = inventory(target)
    assert actual[artifact_path] == payload and actual[f"attachments/{image_id}.bin"] == png
    for name in ("snapshot.json", "journal.jsonl"):
        if name in before:
            assert actual[name] == before[name]
    assert json.loads(actual["queue.json"])["items"][0]["state"] == "staged"
    meta = json.loads(actual["meta.json"])
    assert meta["id"] == target_id and meta["project_id"] == "default"
    assert inventory(original) == before
    assert request(port, "DELETE", preview)[0] == 200
    assert request(port, "DELETE", upload)[0] == 200
    assert request(port, "DELETE", target_path)[0] == 200
    catalog = request(port, "GET", "/api/v1/sessions")[1]["data"]["items"]
    assert next(item for item in catalog if item["id"] == target_id)["runtime_open"] is False
    assert request(port, "POST", target_path + "/apply")[1]["data"]["committed"]
    assert inventory(target) == actual
    return target_id, actual


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--packed", type=Path, required=True)
    packed = parser.parse_args().packed.resolve()
    with tempfile.TemporaryDirectory(prefix="packed-restore-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        first_site, first_port = site(base, "first", packed)
        second_site, second_port = site(base, "moved", packed)
        home = base / "mdo-home"
        env = dict(os.environ, USERPROFILE=str(base), MDO_LING_RESPONSES_URL="http://127.0.0.1:9/v1",
                   MDO_LING_API_KEY="bounded-packed-restore-key")
        first, second = start(first_site, packed, home, env), None
        try:
            assert wait_bootstrap(first, first_port, first_site / "packed.log")[1]["data"]["ready"]
            assert not (first_site / "app").exists() and not (first_site / "src").exists()
            target_id, actual = restore(first_port, home)
            stop(first)
            second = start(second_site, packed, home, env)
            assert wait_bootstrap(second, second_port, second_site / "packed.log")[1]["data"]["ready"]
            path = "/api/v1/session-backups/restores/" + target_id
            recorded = request(second_port, "GET", path)[1]["data"]
            assert recorded["committed"] and recorded["accepted"] and "phase" not in recorded
            assert request(second_port, "POST", path + "/apply")[1]["data"] == recorded
            assert inventory(home / "sessions/default" / target_id) == actual
        finally:
            stop(first)
            if second is not None:
                stop(second)
            release_packed_copies(first_site / packed.name, second_site / packed.name)
    print("packed embedded VFS restore/admission/receipt/moved-executable replay probe: PASS")


if __name__ == "__main__":
    main()
