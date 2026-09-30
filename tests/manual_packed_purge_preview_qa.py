"""Review the project candidate inventory in an isolated single-file pack.

Only synthetic local project data is seeded. No model is called and no purge
is executed. Type 'break'/'fix' to toggle one orphan plan file for error UI;
'status' verifies every seeded project/workspace byte remains unchanged.
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

from test_api_runtime import ROOT, free_port, request, wait_ready
from test_interrupt_runtime import stop_host


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--packed-path", type=Path, default=ROOT / "mdo.exe")
    parser.add_argument("--include-references", action="store_true",
        help="Seed attributable global draft and last-session references for preview QA")
    args = parser.parse_args()
    base = Path(tempfile.mkdtemp(prefix="mdo-packed-purge-", dir=ROOT / ".build"))
    packed = base / ("mdo.exe" if os.name == "nt" else "mdo")
    shutil.copy2(args.packed_path, packed)
    home = base / "mdo-home"
    workspace = base / "workspace"
    workspace.mkdir()
    sentinel = workspace / "source.txt"
    sentinel.write_bytes(b"Synthetic user workspace remains in place.")
    port = free_port()
    (base / "xs.json").write_text(json.dumps({"services": [{
        "enabled": True, "class": "http", "name": "mdo", "ip": "127.0.0.1",
        "port": port, "host_default": {"enabled": True, "name": "mdo", "path": "web",
            "devlang": "c", "devfile": "generated/mdo_unity.c"}}]}), encoding="utf-8")
    env = dict(os.environ, USERPROFILE=str(base), HOME=str(base), MDO_HOME=str(home))

    def api(method: str, path: str, payload: dict | None = None, **headers) -> dict:
        status, _, body = request(port, method, "/api/v1/" + path,
            body=json.dumps(payload).encode() if payload is not None else b"",
            headers={"Content-Type": "application/json", **headers})
        assert 200 <= status < 300, (status, body)
        return json.loads(body)["data"]

    with (base / "packed.log").open("ab") as log:
        process = subprocess.Popen([str(packed)], cwd=base, env=env, stdout=log,
            stderr=subprocess.STDOUT,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
    try:
        wait_ready(port, process)
        project = "inventory-ui"
        api("POST", "projects", {"id": project, "name": "候选清单验证",
            "workspace_root": str(workspace), "default_model_id": "ling-3.0-tiny"})
        api("PUT", f"projects/{project}/draft", {"revision": 0, "text": "尚未发送的新任务草稿"})
        session = api("POST", "sessions", {"project_id": project, "title": "清单里的会话"})
        if args.include_references:
            api("PUT", "workspace-state", {"project_id": project, "session_id": session["id"]})
            draft = api("GET", "draft")
            api("PUT", "draft", {"revision": draft["revision"], "text": "当前项目的全局待建任务草稿",
                "new_task": {"project_id": project, "session_id": "f" * 32, "title": "待建任务",
                    "agent_id": "mdo.default", "model_id": "ling-3.0-tiny", "reasoning_effort": "medium",
                    "permission_profile": "balanced", "phase": "rejected"},
                "submissions": [], "attachments": [], "run_admission_uncertain": False})
        _, headers, _ = request(port, "GET", f"/api/v1/memory/projects/{project}")
        api("PUT", f"memory/projects/{project}", {"id": "fixture-note", "title": "项目记忆",
            "content": "用于核对候选数据范围。", "tags": [], "pinned": False},
            **{"If-Match": headers["etag"]})
        api("POST", "schedules", {"id": "inventory-plan", "label": "已停用的核对计划",
            "notify": "", "project_id": project, "agent_id": "mdo.default",
            "model_id": "ling-3.0-tiny", "protocol": "openai-responses",
            "reasoning_effort": "medium", "max_output_tokens": 1024,
            "workspace_root": str(workspace), "input": "Never run this fixture.",
            "frequency": "once", "interval": 1, "start_at": 4102444800000000,
            "weekday_mask": 0, "timezone": "utc", "utc_offset_seconds": 0,
            "fold_policy": "earlier", "misfire_policy": "run_once", "misfire_grace_seconds": 60,
            "max_catch_up": 1, "overlap_policy": "skip", "max_concurrent_runs": 1,
            "enabled": False})
        for relative in (f"projects/{project}.json", f"data/project-drafts/{project}.json",
                         f"memory/projects/{project}.json", "schedules/inventory-plan.json"):
            path = home / relative
            path.with_suffix(".json.bak").write_bytes(path.read_bytes())
        history = home / "schedules/history/inventory-plan.jsonl"
        history.parent.mkdir(parents=True, exist_ok=True)
        history.write_bytes(b'{"fixture":"owned history"}\n')
        sidecar = home / f"migration/session-prompts/{project}/prompt.txt"
        sidecar.parent.mkdir(parents=True)
        sidecar.write_bytes(b"Synthetic legacy prompt.")
        preview_path = f"projects/{project}/purge-preview"
        preview = api("GET", preview_path)
        seeded = {sentinel: sentinel.read_bytes()}
        for item in preview["targets"]:
            path = home / item["path"]
            for leaf in path.rglob("*") if path.is_dir() else [path]:
                if leaf.is_file(): seeded[leaf] = leaf.read_bytes()
        orphan = home / "schedules/orphan.json.bak"
        print(f"READY url=http://127.0.0.1:{port}/#/settings base={base}", flush=True)
        while True:
            command = input("Type status, break, fix, or Enter to stop: ").strip()
            if not command: break
            if command == "break": orphan.write_bytes(b"Synthetic orphan")
            elif command == "fix": orphan.unlink(missing_ok=True)
            else: print(json.dumps(api("GET", preview_path), ensure_ascii=False), flush=True)
            assert all(path.read_bytes() == data for path, data in seeded.items())
            print("PASS: seeded project and workspace bytes unchanged", flush=True)
    finally:
        stop_host(process)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
