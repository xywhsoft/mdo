"""Bounded packed-app checks for the portable default workspace. No model calls."""

from __future__ import annotations

import argparse
import os
import tempfile
from pathlib import Path

from test_packed_home_lease import (
    ROOT, release_packed_copies, request, site, start, stop, wait_bootstrap,
)


def ready(directory: Path, packed: Path, home: Path, port: int):
    env = os.environ.copy()
    env["MDO_HOME"] = str(directory / "ignored-environment-home")
    env["MDO_ORNITH_API_KEY"] = "default-workspace-probe"
    env["MDO_ORNITH_RESPONSES_URL"] = "http://127.0.0.1:9/v1"
    process = start(directory, packed, home, env)
    try:
        status, document = wait_bootstrap(process, port, directory / "packed.log")
        assert status == 200 and document["data"]["ready"], document
    except BaseException:
        stop(process)
        raise
    return process


def create(port: int, **fields):
    status, document = request(port, "POST", "/api/v1/sessions", {
        "project_id": "default", "title": "Default workspace probe",
        "agent_id": "mdo.default", "model_id": "ornith-1.5-35b",
        "protocol": "openai-responses", "reasoning_effort": "medium",
        "max_output_tokens": 1024, **fields,
    })
    assert status in (200, 201), (status, document)
    return status, document["data"]


def default_probe(base: Path, packed: Path) -> None:
    directory, port = site(base, "launch-directory", packed)
    home = base / "便携 Home"
    workspace = home / "workspace"
    # A launch-directory match must never leak into default-project completion.
    (directory / "mention-probe-launch.txt").write_text("outside", encoding="utf-8")
    process = ready(directory, packed, home, port)
    try:
        assert not home.exists()
        files = "/api/v1/projects/default/workspace/files?q=mention-probe"
        status, document = request(port, "GET", files)
        assert status == 200 and document["data"]["items"] == [], document
        assert not home.exists(), "read-only completion created Home"
        requested_id = "d" * 32
        status, session = create(port, client_session_id=requested_id)
        assert status == 201 and Path(session["workspace_root"]) == workspace
        assert workspace.is_dir()
        assert not (directory / "ignored-environment-home").exists()
        (workspace / "mention-probe-workspace.txt").write_text("inside", encoding="utf-8")
        status, replay = create(port, client_session_id=requested_id)
        assert status == 200 and replay["id"] == session["id"]
        status, document = request(port, "GET", files)
        assert status == 200 and document["data"]["items"] == ["mention-probe-workspace.txt"], document
        status, document = request(port, "GET", f"/api/v1/projects/default/sessions/{session['id']}/workspace/files?q=mention-probe")
        assert status == 200 and document["data"]["items"] == ["mention-probe-workspace.txt"], document
        status, document = request(port, "GET", "/api/v1/projects")
        default = next(item for item in document["data"]["items"] if item["id"] == "default")
        assert status == 200 and Path(default["workspace_root"]) == workspace

        # Historical sessions and explicit roots keep their original directory.
        _, legacy = create(port, workspace_root=str(directory))
        assert Path(legacy["workspace_root"]) == directory
        status, document = request(port, "GET", f"/api/v1/projects/default/sessions/{legacy['id']}/workspace/files?q=mention-probe")
        assert status == 200 and document["data"]["items"] == ["mention-probe-launch.txt"], document

        custom = directory / "custom-project"
        custom.mkdir()
        status, document = request(port, "POST", "/api/v1/projects", {
            "id": "default", "name": "Explicit default", "workspace_root": str(custom),
        })
        assert status == 201, document
        _, managed = create(port)
        assert Path(managed["workspace_root"]) == custom
    finally:
        stop(process)

    process = ready(directory, packed, home, port)
    try:
        for saved in (session, legacy, managed):
            status, document = request(port, "GET", f"/api/v1/projects/default/sessions/{saved['id']}")
            assert status == 200 and document["data"]["workspace_root"] == saved["workspace_root"], document
    finally:
        stop(process)
        release_packed_copies(directory / packed.name)
    print("Default workspace, file completion, replay, overrides and recovery: PASS")


def invalid_workspace_probe(base: Path, packed: Path) -> None:
    directory, port = site(base, "invalid-workspace", packed)
    home = base / "invalid-home"
    home.mkdir()
    workspace = home / "workspace"
    workspace.write_text("keep this file", encoding="utf-8")
    process = ready(directory, packed, home, port)
    try:
        status, document = request(port, "POST", "/api/v1/sessions", {"project_id": "default"})
        assert status == 500 and document["error"]["code"] == "session_persistence_failed", (status, document)
        assert workspace.read_text(encoding="utf-8") == "keep this file"
        assert not (home / "sessions").exists()
        status, document = request(port, "GET", "/api/v1/projects/default/workspace/files?q=mention")
        assert status == 503, document
    finally:
        stop(process)
        release_packed_copies(directory / packed.name)
    print("Non-directory workspace refused without cwd fallback or overwrite: PASS")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--packed", type=Path,
        default=ROOT / ("mdo.exe" if os.name == "nt" else "mdo"))
    args = parser.parse_args()
    packed = args.packed.resolve()
    assert packed.is_file(), packed
    with tempfile.TemporaryDirectory(prefix="default-workspace-", dir=ROOT / ".build") as raw:
        base = Path(raw).resolve()
        assert base.parent == (ROOT / ".build").resolve()
        default_probe(base, packed)
        invalid_workspace_probe(base, packed)
    print("portable default workspace runtime probe: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
