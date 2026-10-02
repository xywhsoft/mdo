#!/usr/bin/env python3
"""Bounded session-artifact replay through xs/TCC after restart and Home move."""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import shutil
import sys
import tempfile
from pathlib import Path

from test_interrupt_runtime import free_port, request, start_host, stop_host


ROOT = Path(__file__).resolve().parents[1]


def run_probe(host: Path) -> None:
    (ROOT / ".build").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="mdo-artifact-replay-",
                                     dir=ROOT / ".build") as raw:
        base = Path(raw)
        site = base / "site"
        shutil.copytree(ROOT / "app", site)
        port = free_port()
        config = site / "xs.json"
        config.write_text(json.dumps({"engine": {"workers": 1},
            "services": [{"enabled": True, "class": "http", "name": "mdo",
                "ip": "127.0.0.1", "port": port,
                "host_default": {"enabled": True, "name": "mdo",
                    "path": "web", "devlang": "c",
                    "devfile": "generated/mdo_unity.c"}}]}), encoding="utf-8")
        home = base / "home"
        environment = os.environ.copy()
        environment["USERPROFILE"] = str(base)
        environment["HOME"] = str(base)
        log = base / "xs.log"
        process = None
        try:
            process = start_host(host, config, home, environment, log, port)
            status, body = request(port, "POST", "/api/v1/sessions", {
                "project_id": "default", "title": "Artifact replay probe",
                "agent_id": "mdo.default", "model_id": "ornith-1.5-35b",
                "protocol": "openai-responses", "reasoning_effort": "medium",
                "max_output_tokens": 1024,
            })
            assert status == 201, (status, body)
            session_id = body["data"]["id"]
            status, body = request(port, "POST", "/api/v1/sessions", {
                "project_id": "default", "title": "Other session",
                "agent_id": "mdo.default", "model_id": "ornith-1.5-35b",
                "protocol": "openai-responses", "reasoning_effort": "medium",
                "max_output_tokens": 1024,
            })
            assert status == 201, (status, body)
            other_id = body["data"]["id"]
        finally:
            stop_host(process)

        session_dir = home / "sessions/default" / session_id
        artifact_dir = session_dir / "artifacts/run-00000000000000000007"
        artifact_dir.mkdir(parents=True)
        artifact_file = artifact_dir / "00000000000000000009-read.txt"
        content = b"bounded artifact replay\n" * 3600
        artifact_file.write_bytes(content)
        event = {
            "schema_version": 3, "event_id": 1, "source_event_id": 1,
            "occurred_at_us": 1, "project_id": "default",
            "session_id": session_id, "kind": 6, "agent_turn": 1,
            "user_message_sequence": 0, "agent_depth": 0, "agent_id": 1,
            "run_id": 7, "task_id": 0, "artifact_id": 9,
            "parent_run_id": 0, "effects": 1, "task_state": 0,
            "task_revision": 0, "input_tokens": 0, "output_tokens": 0,
            "total_tokens": 0, "success": True, "effect_applied": True,
            "text_truncated": False, "text": "output truncated",
            "tool_name": "read", "tool_call_id": "artifact-probe",
            "artifact_path": str(artifact_file), "model": "",
        }
        bad_event = {**event, "event_id": 2,
                     "artifact_path": str(artifact_file) + ".bak"}
        (session_dir / "ui-events.jsonl").write_text(
            "".join(json.dumps(item, separators=(",", ":")) + "\n"
                    for item in (event, bad_event)), encoding="utf-8")

        # The journal retains the old absolute path. The endpoint must bind
        # its validated final components to the moved, current Home root.
        moved_home = base / "moved-home"
        assert home.resolve().is_relative_to(base.resolve())
        assert moved_home.resolve().is_relative_to(base.resolve())
        shutil.move(str(home), str(moved_home))
        process = None
        try:
            process = start_host(host, config, moved_home, environment,
                                 log, port)
            path = (f"/api/v1/projects/default/sessions/{session_id}"
                    "/artifacts/1")
            status, body = request(port, "GET", path + "?offset=0&limit=65536")
            assert status == 200, (status, body)
            data = body["data"]
            assert data["event_id"] == 1 and data["artifact_id"] == 9, data
            assert data["total_size"] == len(content) and not data["eof"], data
            assert base64.b64decode(data["data"]) == content[:65536], data
            assert data["sha256"] == hashlib.sha256(content).hexdigest(), data
            assert request(port, "GET", path + f"?offset={len(content)+1}")[0] == 416
            assert request(port, "GET", path + "?limit=65537")[0] == 400
            assert request(port, "GET", path[:-1] + "2")[0] == 404
            assert request(port, "GET", path[:-1] + "3")[0] == 404
            other_path = (f"/api/v1/projects/default/sessions/{other_id}"
                          "/artifacts/1")
            assert request(port, "GET", other_path)[0] == 404
            status, body = request(port, "GET", "/api/v1/artifacts")
            assert status == 200 and body["data"]["total"] == 0, body
        finally:
            stop_host(process)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", type=Path, required=True)
    options = parser.parse_args()
    try:
        run_probe(options.host.resolve())
    except BaseException as error:
        print(f"session artifact replay runtime probe failed: {error}",
              file=sys.stderr)
        raise
    print("session artifact replay runtime probe: PASS")
