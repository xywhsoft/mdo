"""Bounded production purge coordinator probe in isolated xs/TCC Homes.

All fault controls modify copied application sources. No live model, workload,
user Home or workspace is touched. Storage's individual rename/crash edges have
their own probe; this exercises business validation and catalog convergence.
"""
from __future__ import annotations

import argparse
from contextlib import contextmanager
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import threading
import time

from test_api_runtime import ROOT, free_port, request, wait_ready, write_site

CONTROL = "/__fixture/project-purge/"
OK, INVALID, NOT_FOUND, REVISION_CONFLICT, BUSY, UNAVAILABLE, ABORTED, RESTART = range(8)


def patch_site(site: Path) -> None:
    shutil.copy2(ROOT / "tests/fixtures/project-purge.c", site / "src/bootstrap/project-purge-probe.c")

    def change(relative: str, old: str, new: str) -> None:
        path = site / relative
        source = path.read_text(encoding="utf-8")
        assert source.count(old) == 1, (relative, old)
        path.write_text(source.replace(old, new, 1), encoding="utf-8", newline="\n")

    change("src/bootstrap/service.c", "void ServiceInit(XS_HostInfo* pHost)",
           '#include "project-purge-probe.c"\nvoid ServiceInit(XS_HostInfo* pHost)')
    change("src/bootstrap/service.c", "    MdoApiReferenceProbeUnit();",
           "    MdoPurgeFixtureUnit();\n    MdoApiReferenceProbeUnit();")
    change("src/bootstrap/service.c", "    if ( MdoApiProbeLeaseControl(pRequest) ) return XS_OK;",
           "    if ( MdoPurgeFixtureControl(pRequest) ) return XS_OK;\n"
           "    if ( MdoApiProbeLeaseControl(pRequest) ) return XS_OK;")
    change("src/storage/home_purge.inc.c", "bool MdoApiReferenceProbeCommitAllowed(void);",
           "bool MdoApiReferenceProbeCommitAllowed(void);\n"
           "bool MdoPurgeFixtureCommitAllowed(void);\nbool MdoPurgeFixtureRollbackAllowed(void);\n"
           "bool MdoPurgeFixtureCleanupAllowed(void);")
    change("src/storage/home_purge.inc.c", "    if ( !MdoApiReferenceProbeCommitAllowed() ||",
           "    if ( !MdoPurgeFixtureCommitAllowed() || !MdoApiReferenceProbeCommitAllowed() ||")
    change("src/storage/home_purge.inc.c",
           "!MdoHomePurgeMove(Slot, Manifest.Targets[i].Path, &Manifest.Targets[i].Info))",
           "(!MdoPurgeFixtureRollbackAllowed() ||\n"
           "                     !MdoHomePurgeMove(Slot, Manifest.Targets[i].Path, &Manifest.Targets[i].Info)))")
    gc_check = "    if ( Info.Type != XFILE_TYPE_DIRECTORY ||\n         !MdoHomePurgeJournal(MDO_HOME_PURGE_GC, &Empty) )"
    change("src/storage/home_purge.inc.c", gc_check,
           "    if ( !MdoPurgeFixtureCleanupAllowed() ) return false;\n" + gc_check)
    change("src/schedules/manager.c", '#include "internal.h"',
           '#include "internal.h"\nbool MdoPurgeFixtureUnregister(xwork_runtime*, const char*, xwork_error*);')
    change("src/schedules/manager.c",
           "!xworkRuntimeUnregisterSchedule(g_MdoSchedules.Runtime, Entry->Info.Id, &Failure)",
           "!MdoPurgeFixtureUnregister(g_MdoSchedules.Runtime, Entry->Info.Id, &Failure)")
    change("src/projects/purge.c", '#include "../sessions/internal.h"',
           '#include "../sessions/internal.h"\n'
           "bool MdoPurgeFixtureCacheCommit(MdoSchedulePurgeGuard*, xwork_error*);\n"
           "void MdoPurgeFixtureAfterStorage(bool);")
    change("src/projects/purge.c", "    }\n    memset(&Home, 0, sizeof(Home)); Home.Size = sizeof(Home);",
           "    }\n    MdoPurgeFixtureAfterStorage(Result->Committed);\n"
           "    memset(&Home, 0, sizeof(Home)); Home.Size = sizeof(Home);")
    change("src/projects/purge.c", "MdoSchedulesPurgeCommit(Schedules, &Failure)",
           "MdoPurgeFixtureCacheCommit(Schedules, &Failure)")


class Probe:
    def __init__(self, host: Path, base: Path):
        self.host, self.base = host, base
        self.site, self.home = base / "site", base / "home"
        self.log_path, self.port = base / "xs.log", free_port()
        self.config = write_site(self.site, self.port)
        patch_site(self.site)
        self.process: subprocess.Popen | None = None
        self.log = None

    def start(self) -> None:
        self.log = self.log_path.open("ab")
        self.process = subprocess.Popen([str(self.host), str(self.config)], cwd=self.site,
            env=dict(os.environ, MDO_HOME=str(self.home),
                     MDO_ORNITH_RESPONSES_URL="https://example.invalid/v1",
                     MDO_ORNITH_API_KEY="bounded-purge-fixture"),
            stdout=self.log, stderr=subprocess.STDOUT,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        wait_ready(self.port, self.process)

    def stop(self) -> None:
        if self.process is not None and self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=3)
        if self.log is not None:
            self.log.close()

    def api(self, method: str, path: str, value: dict | None = None,
            expected: int = 200, extra: dict | None = None) -> dict:
        status, _, body = request(self.port, method, path,
            body=json.dumps(value).encode() if value is not None else None,
            headers={"Content-Type": "application/json", **(extra or {})})
        assert status == expected, (method, path, status, body)
        return json.loads(body)["data"] if status < 400 else json.loads(body)

    def control(self, mode: str) -> dict:
        return self.api("POST", CONTROL + mode)

    def files(self) -> dict[str, bytes]:
        return {p.relative_to(self.home).as_posix(): p.read_bytes()
            for p in self.home.rglob("*") if p.is_file() and p.relative_to(self.home).as_posix()
            not in {".mdo.lock", "memory/.writer.lock", "schedules/.writer.lock"}}

    def seed(self, own_references: bool = True) -> set[str]:
        workspace = self.base / "workspace"
        workspace.mkdir()
        (workspace / "user-file.txt").write_bytes(b"outside Home; never remove\n")
        own = None
        for project, session in (("purge-probe", "a" * 32), ("purge-other", "b" * 32)):
            self.api("POST", "/api/v1/projects", {"id": project, "name": project,
                "workspace_root": str(workspace)}, 201)
            created = self.api("POST", "/api/v1/sessions", {"project_id": project,
                "title": project, "client_session_id": session}, 201)
            if project == "purge-probe":
                own = created
        for schedule, project, enabled in (("purge-own-a", "purge-probe", True),
                ("purge-own-b", "purge-probe", False), ("purge-other", "purge-other", True)):
            self.api("POST", "/api/v1/schedules", {
                "id": schedule, "label": schedule, "notify": "", "project_id": project,
                "agent_id": "mdo.default", "model_id": "ornith-1.5-35b", "protocol": "openai-responses",
                "reasoning_effort": "medium", "max_output_tokens": 1024,
                "workspace_root": str(workspace), "input": "Local future metadata only",
                "frequency": "once", "interval": 1, "start_at": 4102444800000000,
                "weekday_mask": 0, "timezone": "utc", "utc_offset_seconds": 0,
                "fold_policy": "earlier", "misfire_policy": "run_once", "misfire_grace_seconds": 60,
                "max_catch_up": 1, "overlap_policy": "skip", "max_concurrent_runs": 1,
                "enabled": enabled}, 201)
        memory = "/api/v1/memory/projects/purge-probe"
        status, headers, _ = request(self.port, "GET", memory)
        assert status == 200
        self.api("PUT", memory, {"id": "local-note", "title": "Local note",
            "content": "Purge this owned memory", "tags": [], "pinned": False},
            extra={"If-Match": headers["etag"]})
        project_draft = "/api/v1/projects/purge-probe/draft"
        revision = self.api("GET", project_draft)["revision"]
        self.api("PUT", project_draft, {"revision": revision, "text": "Owned project input"})
        global_revision = self.api("GET", "/api/v1/draft")["revision"]
        new_task = {"project_id": "purge-probe", "session_id": "c" * 32, "title": "Pending task",
            "agent_id": "mdo.default", "model_id": "ornith-1.5-35b", "reasoning_effort": "medium",
            "permission_profile": "balanced", "phase": "creating"} if own_references else None
        self.api("PUT", "/api/v1/draft", {"revision": global_revision, "text": "Retained unowned input",
            "new_task": new_task, "submissions": [], "attachments": [], "run_admission_uncertain": False})
        self.api("PUT", "/api/v1/workspace-state", {"project_id": "purge-probe" if own_references else "purge-other",
            "session_id": own["id"] if own_references else "b" * 32})
        for path in ("projects/purge-probe.json", "memory/projects/purge-probe.json",
                     "data/project-drafts/purge-probe.json", "schedules/purge-own-a.json",
                     "schedules/purge-own-b.json"):
            (self.home / (path + ".bak")).write_bytes((self.home / path).read_bytes())
        # Namespace ownership follows the current schedule, including an old
        # backup whose embedded project differs and opaque execution history.
        backup = self.home / "schedules/purge-own-a.json.bak"
        document = json.loads(backup.read_text(encoding="utf-8"))
        document["project_id"] = "purge-other"
        backup.write_text(json.dumps(document), encoding="utf-8")
        for path, content in {
            "schedules/history/purge-own-a.jsonl": b"{\"old_execution\":true}\n",
            "schedules/history/purge-own-b.jsonl": b"{\"disabled_execution\":true}\n",
            "migration/session-prompts/purge-probe/提示.txt": b"owned legacy hint\n",
            "migration/report-preserved.json": b"shared report\n",
            "data/cache/webview2/preserved.bin": b"portable browser cache\n",
        }.items():
            file = self.home / path
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_bytes(content)
        preview = self.api("GET", "/api/v1/projects/purge-probe/purge-preview")
        # The API returns generated candidate roots; no test constructs a
        # deletion path or supplies a client path to the production core.
        targets = {target["path"] for target in preview["targets"]}
        if own_references:
            targets |= {"data/draft.json", "data/workspace-state.json"}
        return targets

    def expected_after(self, before: dict[str, bytes], targets: set[str]) -> dict[str, bytes]:
        return {path: value for path, value in before.items()
            if not any(path == target or path.startswith(target + "/") for target in targets)}

    def check_after(self, before: dict[str, bytes], targets: set[str], expected_status: int = OK) -> dict:
        result = self.control("purge")
        assert result["status"] == expected_status and result["committed"] is True, result
        assert result["targets"] == len(targets) and result["schedules"] == 2, result
        assert self.files() == self.expected_after(before, targets)
        assert (self.base / "workspace/user-file.txt").read_bytes() == b"outside Home; never remove\n"
        assert not (self.home / ".mdo-purge").exists()
        assert not (self.home / ".mdo-purge-cleanup").exists()
        return result


@contextmanager
def running(host: Path, base: Path):
    probe = Probe(host, base)
    try:
        probe.start()
        yield probe
    except BaseException as error:
        output = probe.log_path.read_text(encoding="utf-8", errors="replace") if probe.log_path.exists() else ""
        raise RuntimeError(f"{error}\n--- xs log ---\n{output[-6500:]}") from error
    finally:
        probe.stop()


def run_probe(host: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="project-purge-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        with running(host, base / "normal") as probe:
            assert not probe.home.exists()
            assert probe.control("invalid")["status"] == INVALID
            assert probe.control("missing")["status"] == NOT_FOUND
            assert not probe.home.exists()
            targets = probe.seed()
            before, state = probe.files(), probe.control("state")
            assert state["native_owned"] == 2 and state["native_enabled"] == 1, state
            assert probe.control("stale")["status"] == REVISION_CONFLICT
            probe.control("guard-preconditions")
            probe.control("hold-session")
            assert probe.control("purge")["status"] == BUSY
            probe.control("release-session")
            assert probe.files() == before
            probe.control("native-start")
            assert probe.control("purge-no-error")["status"] == BUSY
            probe.control("native-finish")
            assert probe.files() == before
            damaged = probe.home / "data/draft.json"
            original = damaged.read_bytes()
            damaged.write_bytes(b'{"broken":true}')
            corrupted = probe.files()
            assert probe.control("purge")["status"] == UNAVAILABLE
            assert probe.files() == corrupted and probe.control("state")["exclusive_available"]
            damaged.write_bytes(original)
            probe.control("fault-rollback")
            result = probe.control("purge")
            assert result["status"] == ABORTED and not result["committed"] and not result["restart_required"], result
            assert probe.files() == before and probe.control("state") == state
            probe.control("fault-none")
            probe.control("retain-snapshots")
            result = probe.check_after(before, targets)
            assert result["selection_removed"] and result["global_draft_removed"], result
            current = probe.control("state")
            assert current["native_owned"] == current["cached_owned"] == 0 and current["native_total"] == 1, current
            assert current["exclusive_available"] and not current["restart_required"], current
            assert current["repeat_violations"] == 0 and current["unregister_calls"] == 2, current
            assert current["old_sessions"] == 2 and current["old_schedules"] == 3 and current["old_memory"] == 1, current
            for key in ("session_generation", "memory_generation", "schedule_generation"):
                assert current[key] == state[key] + 1, (key, state, current)
            probe.control("trigger-removed")
            assert probe.control("purge")["status"] == NOT_FOUND
            assert probe.files() == probe.expected_after(before, targets)
            assert probe.api("GET", "/api/v1/draft")["revision"] == 0
            assert probe.api("GET", "/api/v1/workspace-state") == {"project_id": "", "session_id": ""}
            probe.api("PUT", "/api/v1/draft", {"revision": 0, "text": "late",
                "new_task": {"project_id": "purge-probe", "session_id": "c" * 32,
                    "title": "Late task", "agent_id": "mdo.default", "model_id": "ornith-1.5-35b",
                    "reasoning_effort": "medium", "permission_profile": "balanced", "phase": "creating"}}, 404)
            probe.api("PUT", "/api/v1/workspace-state", {"project_id": "purge-probe", "session_id": "a" * 32}, 404)
            assert probe.files() == probe.expected_after(before, targets)

        with running(host, base / "unowned") as probe:
            targets = probe.seed(own_references=False)
            before = probe.files()
            result = probe.check_after(before, targets)
            assert not result["selection_removed"] and not result["global_draft_removed"], result

        with running(host, base / "unregistered") as probe:
            probe.seed()
            before = probe.files()
            probe.control("native-unregister-disabled")
            assert probe.control("purge")["status"] == BUSY
            assert probe.files() == before

        with running(host, base / "native-config") as probe:
            probe.seed()
            before = probe.files()
            probe.control("native-enable-disabled")
            assert probe.control("purge")["status"] == BUSY
            assert probe.files() == before

        with running(host, base / "bare-project") as probe:
            probe.api("POST", "/api/v1/projects", {"id": "purge-probe", "name": "Only definition"}, 201)
            before = probe.files()
            state = probe.control("state")
            result = probe.control("purge-no-error")
            assert result["status"] == OK and result["committed"] and result["targets"] == 1, result
            assert result["schedules"] == 0 and not result["restart_required"], result
            assert not result["selection_removed"] and not result["global_draft_removed"], result
            current = probe.control("state")
            for key in ("session_generation", "memory_generation", "schedule_generation"):
                assert current[key] == state[key], (state, current)
            assert probe.files() == probe.expected_after(before, {"projects/purge-probe.json"})

        with running(host, base / "unresolved") as probe:
            targets = probe.seed()
            before = probe.files()
            probe.control("fault-unresolved")
            result = probe.control("purge")
            assert result["status"] == RESTART and not result["committed"] and result["restart_required"], result
            state = probe.control("state")
            assert state["cached_owned"] == 2 and state["native_enabled"] == 0, state
            assert state["restart_required"] and not state["exclusive_available"], state
            frozen = probe.files()
            probe.control("frozen-write")
            assert probe.files() == frozen
            assert (probe.home / ".mdo-purge/ready").is_file()
            probe.stop(); probe.start()
            assert probe.files() == before
            state = probe.control("state")
            assert state["native_owned"] == 2 and state["native_enabled"] == 1, state
            assert state["exclusive_available"] and not state["restart_required"], state
            probe.check_after(before, targets)

        with running(host, base / "cache-failure") as probe:
            targets = probe.seed()
            before = probe.files()
            probe.control("fault-cache")
            result = probe.check_after(before, targets, RESTART)
            assert result["restart_required"], result
            state = probe.control("state")
            assert state["cached_owned"] == 0 and state["native_owned"] == 1 and state["native_enabled"] == 0, state
            assert state["native_total"] == 2 and state["repeat_violations"] == 0, state
            assert state["restart_required"] and not state["exclusive_available"], state
            probe.control("frozen-write")
            probe.stop(); probe.start()
            state = probe.control("state")
            assert state["cached_owned"] == state["native_owned"] == 0 and state["native_total"] == 1, state
            assert not state["restart_required"] and state["exclusive_available"], state
            assert probe.files() == probe.expected_after(before, targets)

        with running(host, base / "cleanup-failure") as probe:
            targets = probe.seed()
            before = probe.files()
            probe.control("fault-cleanup")
            result = probe.control("purge")
            assert result["status"] == RESTART and result["committed"] and result["restart_required"], result
            state = probe.control("state")
            assert state["cached_owned"] == state["native_owned"] == 0 and state["native_total"] == 1, state
            assert state["restart_required"] and not state["exclusive_available"], state
            assert (probe.home / ".mdo-purge-cleanup/ready").is_file()
            frozen = probe.files()
            probe.control("frozen-write")
            assert probe.files() == frozen
            published = {path: value for path, value in frozen.items()
                         if not path.startswith(".mdo-purge-cleanup/")}
            assert published == probe.expected_after(before, targets)
            probe.stop(); probe.start()
            assert probe.files() == probe.expected_after(before, targets)
            assert not probe.control("state")["restart_required"]

        with running(host, base / "cleanup-transient") as probe:
            targets = probe.seed()
            before = probe.files()
            probe.control("fault-cleanup-once")
            # Storage returned false after its first cleanup failure, but its
            # second recovery finished. A durable commit is still successful.
            probe.check_after(before, targets)
            state = probe.control("state")
            assert state["cached_owned"] == state["native_owned"] == 0 and state["exclusive_available"], state
            assert not state["restart_required"]

        with running(host, base / "commit-crash") as probe:
            targets = probe.seed()
            before = probe.files()
            probe.control("fault-crash")
            errors: list[BaseException] = []

            def purge() -> None:
                try:
                    probe.control("purge")
                except BaseException as error:
                    errors.append(error)  # Expected: process interrupted, response not delivered.

            thread = threading.Thread(target=purge)
            thread.start()
            deadline = time.monotonic() + 3
            while "project_purge_after_commit=1" not in probe.log_path.read_text(encoding="utf-8", errors="replace"):
                assert time.monotonic() < deadline, "storage commit checkpoint was not reached"
                time.sleep(0.01)
            probe.stop()
            thread.join(timeout=5)
            assert not thread.is_alive() and errors
            probe.start()
            state = probe.control("state")
            assert state["native_owned"] == state["cached_owned"] == 0 and state["native_total"] == 1, state
            assert not state["restart_required"] and state["exclusive_available"], state
            assert probe.files() == probe.expected_after(before, targets)
    print("Project purge business coordinator runtime probe: PASS")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path,
        default=ROOT / ".build/host" / ("xs.exe" if os.name == "nt" else "xs"))
    run_probe(parser.parse_args().host.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
