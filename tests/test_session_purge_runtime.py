"""Permanent deletion API and interrupted storage recovery in disposable Homes."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import tempfile
import uuid

from test_api_runtime import request
from test_packed_home_lease import site, start, stop, wait_bootstrap, release_packed_copies
from test_home_purge_runtime import PROBE, site_fixture, launch

ROOT = Path(__file__).resolve().parents[1]


def api_probe(packed: Path):
    with tempfile.TemporaryDirectory(prefix="mdo-session-purge-api-") as temporary:
        base = Path(temporary)
        path, port = site(base, "site", packed)
        home = base / "home"
        process = start(path, packed, home, dict(os.environ, MDO_HOME=str(home)))
        try:
            assert wait_bootstrap(process, port, path / "packed.log")[1]["data"]["stage"] == "ready"

            def call(method, route, body=None, etag=None, **options):
                headers = {"If-Match": etag} if etag else {}
                if body is not None: headers["Content-Type"] = "application/json"
                status, metadata, raw = request(port, method, "/api/v1" + route,
                    body=json.dumps(body).encode() if body is not None else None, headers=headers, **options)
                return status, metadata, json.loads(raw)

            def create(title):
                status, headers, result = call("POST", "/sessions", {"project_id": "default", "title": title,
                    "client_session_id": uuid.uuid4().hex})
                assert status == 201, result
                value = result["data"]
                return f'/projects/default/sessions/{value["id"]}', headers["etag"], value

            route, tag, value = create("disposable purge target")
            neighbour_route, neighbour_tag, neighbour = create("preserved neighbour")
            root = home / "sessions/default" / value["id"]
            neighbour_root = home / "sessions/default" / neighbour["id"]
            assert call("POST", route + "/purge", etag=tag)[0] == 409
            assert call("POST", route + "/purge")[0] == 428
            assert call("POST", route + "/purge", {}, tag)[0] == 400
            assert call("POST", route + "/purge", etag=tag, read_write_token=False)[0] in (403, 409, 428)
            assert root.is_dir()
            status, headers, result = call("DELETE", route, etag=tag)
            assert status == 200 and result["data"]["status"] == "trash", result
            trash_tag = headers["etag"]
            assert call("POST", route + "/purge", etag=tag)[0] == 412
            assert call("POST", route + "/purge", etag=neighbour_tag)[0] == 412
            # All session-owned sidecars/artifacts are removed, including future
            # unknown files, while unrelated workspace/project records survive.
            for name in ("draft.json", "queue.json", "queue-receipts/saved.json", "images/example.bin", "artifacts/report.txt"):
                item = root / name; item.parent.mkdir(parents=True, exist_ok=True); item.write_bytes(b"fixture")
            legacy = home / "migration/session-prompts/default" / (value["id"] + ".txt")
            legacy.parent.mkdir(parents=True, exist_ok=True); legacy.write_text("old prompt", encoding="utf-8")
            marker = home / "workspace-preserved.txt"; marker.write_text("keep", encoding="utf-8")
            def global_draft(owner):
                return {"schema_version": 6, "revision": 1, "text": "owned draft", "attachments": [],
                    "submissions": [], "run_admission_uncertain": False, "new_task": {
                        "project_id": "default", "session_id": owner, "title": "Owned draft",
                        "agent_id": "mdo.default", "model_id": "ornith-1.5-35b", "reasoning_effort": "medium",
                        "permission_profile": "balanced", "phase": "creating"}}
            global_path = home / "data/draft.json"
            global_path.parent.mkdir(parents=True, exist_ok=True)
            global_path.write_text(json.dumps(global_draft(value["id"])), encoding="utf-8")
            assert call("PUT", "/workspace-state", {"project_id": "default", "session_id": value["id"]})[0] == 200
            status, _, result = call("POST", route + "/purge", etag=trash_tag)
            assert status == 200 and result["data"]["deleted"] is True and not result["data"]["restart_required"], result
            assert not root.exists() and not legacy.exists()
            assert not global_path.exists()
            assert neighbour_root.is_dir() and marker.read_text() == "keep"
            assert call("GET", route)[0] == 404
            assert call("POST", route + "/restore", etag=trash_tag)[0] == 404
            assert call("POST", route + "/purge", etag=trash_tag)[0] == 404
            assert call("GET", "/workspace-state")[2]["data"] == {"project_id": "", "session_id": ""}
            assert all(item["id"] != value["id"] for item in call("GET", "/sessions")[2]["data"]["items"])
            # An unrelated saved selection must survive a different deletion.
            route2, tag2, value2 = create("second disposable target")
            trash2 = call("DELETE", route2, etag=tag2)[1]["etag"]
            assert call("PUT", "/workspace-state", {"project_id": "default", "session_id": neighbour["id"]})[0] == 200
            global_path.write_text(json.dumps(global_draft(neighbour["id"])), encoding="utf-8")
            preserved_global = global_path.read_bytes()
            assert call("POST", route2 + "/purge", etag=trash2)[0] == 200
            assert global_path.read_bytes() == preserved_global
            assert call("GET", "/workspace-state")[2]["data"]["session_id"] == neighbour["id"]
            stop(process)
            process = start(path, packed, home, dict(os.environ, MDO_HOME=str(home)))
            wait_bootstrap(process, port, path / "packed.log")
            assert call("GET", route)[0] == 404 and call("GET", neighbour_route)[0] == 200
            print("session purge API: revision/status/body/write guards, file cleanup, reference isolation and restart passed")
        finally:
            stop(process)
            release_packed_copies(path / packed.name)


STORAGE_PROBE = PROBE[:PROBE.index("void ServiceInit")] + r'''
void ServiceInit(XS_HostInfo* Host) {
    static const char* Paths[] = { "data/workspace-state.json", "sessions/probe/old-session" };
    MdoHomePurgeTarget Targets[2]; MdoHomeSnapshot Snapshot;
    bool Exists, Committed = false, Ok = false; size_t i;
    (void)Host;
    if (!MdoHomeInit()) { printf("purge_init=0\n"); fflush(stdout); return; }
    if (PurgeProbeMode("inspect")) { printf("purge_inspect=1\n"); goto done; }
    memset(Targets, 0, sizeof(Targets));
    for (i = 0u; i < 2u; ++i) {
        snprintf(Targets[i].Path, sizeof(Targets[i].Path), "%s", Paths[i]);
        if (!MdoHomeExternalStat(Paths[i], &Exists, &Targets[i].Info) || !Exists) goto done;
    }
    if (PurgeProbeMode("invalid-path")) snprintf(Targets[1].Path, sizeof(Targets[1].Path), "sessions/probe/neighbor");
    Ok = MdoHomePurgeSessionFiles("probe", "old-session", Targets, 2u, &Committed);
done:
    memset(&Snapshot, 0, sizeof(Snapshot)); Snapshot.Size = sizeof(Snapshot);
    (void)MdoHomeGetSnapshot(&Snapshot);
    printf("purge_end=%u committed=%u frozen=%u\n", (unsigned)Ok, (unsigned)Committed, (unsigned)Snapshot.RestartRequired);
    MdoHomeUnit(); printf("purge_finished=1\n"); fflush(stdout);
}
void ServiceUnit(XS_HostInfo* Host) { (void)Host; }
'''


def storage_probe(host: Path):
    with tempfile.TemporaryDirectory(prefix="mdo-session-purge-storage-") as temporary:
        base = Path(temporary); path = base / "site"; path.mkdir()
        site_fixture(path)
        (path / "probe.c").write_text(STORAGE_PROBE, encoding="utf-8")
        modes = ("success", "invalid-path", "fail-after-1", "fail-commit", "crash-ready", "crash-1", "crash-2", "crash-committed", "fail-cleanup")
        for mode in modes:
            home = base / mode
            target = home / "sessions/probe/old-session"
            target.mkdir(parents=True); (target / "meta.json").write_text("fixture", encoding="utf-8")
            selected = home / "data/workspace-state.json"
            selected.parent.mkdir(); selected.write_text("fixture", encoding="utf-8")
            neighbour = home / "sessions/probe/neighbor"
            neighbour.mkdir(); (neighbour / "meta.json").write_text("keep", encoding="utf-8")
            definitions = home / "projects/probe.json"
            definitions.parent.mkdir(); definitions.write_text("keep", encoding="utf-8")
            text = launch(host, path, home, mode, checkpoint=mode.startswith("crash-"))
            if not mode.startswith("crash-"):
                assert "purge_init=0" not in text and "purge_finished=1" in text, text
            recovered = launch(host, path, home, "inspect")
            assert "purge_inspect=1" in recovered and "frozen=0" in recovered, recovered
            deleted = mode in ("success", "crash-committed", "fail-cleanup")
            assert target.exists() is not deleted and selected.exists() is not deleted, (mode, text, recovered)
            assert (neighbour / "meta.json").read_text() == "keep" and definitions.read_text() == "keep"
        print(f"session purge storage: {len(modes)} bounded commit/rollback/interruption cases passed")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--packed", type=Path, required=True)
    parser.add_argument("--host", type=Path, required=True)
    args = parser.parse_args()
    api_probe(args.packed.resolve())
    storage_probe(args.host.resolve())
