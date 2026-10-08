"""Bounded real mdo read-only artifacts with Home outside the workspace."""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import re
import shutil
import tempfile
from pathlib import Path

from test_interrupt_runtime import free_port, request, start_host, stop_host


ROOT = Path(__file__).resolve().parents[1]


def run(host: Path) -> None:
    (ROOT / ".build").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="home-artifact-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        site = base / "site"
        workspace = base / "project"
        home = base / "portable-home"
        workspace.mkdir()
        shutil.copytree(ROOT / "app", site)
        # Exercise the generic spill contract with a deliberately small host
        # budget; the product's normal read page is now smaller than its cap.
        runtime = site / "src/agents/runtime.c"
        runtime.write_text(re.sub(r"DefinitionConfig\.iMaxInlineToolBytes\s*=\s*[^;]+;",
            "DefinitionConfig.iMaxInlineToolBytes = 1024u;", runtime.read_text(encoding="utf-8")), encoding="utf-8")
        shutil.copy2(ROOT / "tests/fixtures/home-artifact.c", site / "home-artifact.c")
        (base / "outside.txt").write_bytes(b"outside fixture must stay unchanged")
        (site / "probe.c").write_text(
            "#define HOME_ARTIFACT_WORKSPACE " + json.dumps(str(workspace.resolve())) + "\n"
            '#define ServiceInit HomeArtifactProductInit\n'
            '#include "generated/mdo_unity.c"\n#undef ServiceInit\n'
            '#include "home-artifact.c"\n', encoding="utf-8")
        port = free_port()
        config = site / "xs.json"
        config.write_text(json.dumps({"engine": {"workers": 1}, "services": [{
            "enabled": True, "class": "http", "name": "home-artifact",
            "ip": "127.0.0.1", "port": port, "host_default": {
                "enabled": True, "name": "mdo", "path": "web", "devlang": "c",
                "devfile": "probe.c"}}]}), encoding="utf-8")
        environment = os.environ.copy()
        environment["USERPROFILE"] = str(base)
        environment["HOME"] = str(base)
        saved: dict[str, bytes] = {}
        event_paths: dict[int, str] = {}
        previous_cursor = 0
        for sequence, marker in enumerate(("first", "second", "moved"), 1):
            if sequence == 3:
                moved = base / "moved-home"
                assert home.resolve().is_relative_to(base.resolve())
                shutil.move(str(home), str(moved))
                home = moved
            input_bytes = ((marker + "-bounded-tool-output\n") * 100).encode()
            (workspace / "input.txt").write_bytes(input_bytes)
            log = base / f"xs-{sequence}.log"
            process = None
            try:
                process = start_host(host, config, home, environment, log, port)
                output = log.read_text(encoding="utf-8", errors="replace")
                assert "home_artifact_ok=1" in output, output
                session_dir = home / "sessions/default/home-artifact-probe"
                events = [json.loads(line) for line in
                    (session_dir / "ui-events.jsonl").read_text(encoding="utf-8").splitlines()]
                current = [item for item in events if item["event_id"] > previous_cursor]
                previous_cursor = max(item["event_id"] for item in events)
                denied = {item["tool_call_id"]: item for item in current if item["kind"] == 6}
                assert not denied["home-artifact-outside-read"]["success"], denied
                assert not denied["home-artifact-denied-write"]["success"], denied
                tool = denied["home-artifact-read"]
                assert tool["success"] and tool["artifact_id"] == sequence, tool
                artifact = Path(tool["artifact_path"])
                assert artifact.is_absolute() and artifact.resolve().is_relative_to(session_dir.resolve()), tool
                relative = artifact.relative_to(home).as_posix()
                payload = artifact.read_bytes()
                assert marker.encode() in payload and len(payload) > 1024
                assert "run-00000000000000000001" in relative, relative
                assert relative not in saved
                saved[relative] = payload
                event_paths[tool["event_id"]] = relative
                for event_id, path in event_paths.items():
                    assert (home / path).read_bytes() == saved[path], path
                    status, body = request(port, "GET",
                        f"/api/v1/projects/default/sessions/home-artifact-probe/artifacts/{event_id}?limit=65536")
                    assert status == 200, (status, body)
                    data = body["data"]
                    assert data["eof"] and data["total_size"] == len(saved[path]), data
                    assert base64.b64decode(data["data"]) == saved[path], data
                    assert data["sha256"] == hashlib.sha256(saved[path]).hexdigest(), data
                assert (workspace / "input.txt").read_bytes() == input_bytes
                assert (base / "outside.txt").read_bytes() == b"outside fixture must stay unchanged"
                assert not list(workspace.glob("**/artifacts"))
                assert not list(session_dir.rglob(".xwork-*.tmp"))
                meta = json.loads((session_dir / "meta.json").read_text(encoding="utf-8"))
                assert Path(meta["workspace_root"]).resolve() == workspace.resolve()
                assert meta["permission_profile"] == "read-only"
            finally:
                stop_host(process)
        assert len(saved) == 3


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, required=True)
    options = parser.parse_args()
    run(options.host.resolve())
    print("external Home read-only artifact restart/move/API probe: PASS")
