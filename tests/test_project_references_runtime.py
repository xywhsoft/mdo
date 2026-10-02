"""Bounded global reference coordination in an isolated real HTTP/TCC Home.

Reuse the copied API fixture, but run before damaged-record and recovery cases
from the broad API probe. No external model endpoint is contacted.
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import tempfile
import threading
import time
from pathlib import Path

from test_api_runtime import (ROOT, free_port, request,
                              wait_ready, write_site)


def project_references_probe(port: int, home: Path, log_path: Path) -> None:
    """Conditional references, one-thread guards and ordinary HTTP writers."""
    control = "/__fixture/project-references/"
    draft_path = "/api/v1/draft"
    selection_path = "/api/v1/workspace-state"
    global_files = [home / "data/draft.json", home / "data/workspace-state.json"]
    saved = {path: path.read_bytes() if path.exists() else None for path in global_files}

    def api(method: str, path: str, payload: dict | None = None,
            expected: int = 200) -> dict:
        status, _, body = request(port, method, path,
            body=json.dumps(payload).encode() if payload is not None else None,
            headers={"Content-Type": "application/json"})
        assert status == expected, (method, path, status, body)
        return json.loads(body)

    def snapshot(expected: set[str]) -> None:
        data = api("GET", control + "snapshot")["data"]
        assert data["count"] == len(expected), data
        assert {data[key] for key in ("first", "second") if key in data} == expected, data

    def draft(owner: str | None, text: str = "reference input") -> dict:
        revision = api("GET", draft_path)["data"]["revision"]
        new_task = None if owner is None else {
            "project_id": owner, "session_id": "f" * 32,
            "title": "Reference fixture", "agent_id": "mdo.default",
            "model_id": "ornith-1.5-35b", "reasoning_effort": "medium",
            "permission_profile": "balanced", "phase": "creating"}
        return api("PUT", draft_path, {"revision": revision, "text": text,
            "new_task": new_task, "submissions": [], "attachments": [],
            "run_admission_uncertain": False})["data"]

    def files() -> dict[str, bytes]:
        return {p.relative_to(home).as_posix(): p.read_bytes()
                for p in home.rglob("*") if p.is_file() and
                p.relative_to(home).as_posix() not in {
                    ".mdo.lock", "memory/.writer.lock", "schedules/.writer.lock"}}

    def start(mode: str) -> dict:
        api("POST", control + mode)
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            data = api("GET", control + "status")["data"]
            if data["ready"]:
                assert data["ready"] == 1 and data["done"] == 0, data
                return data
            time.sleep(0.01)
        raise AssertionError("reference guard did not start")

    def stop(ok: bool, committed: bool) -> dict:
        api("POST", control + "release")
        result = api("GET", control + "status")["data"]
        assert result["done"] == 1 and result["ok"] is ok and (
            result["committed"] is committed), result
        return result

    def create(project: str) -> dict:
        api("POST", "/api/v1/projects", {"id": project, "name": project}, 201)
        return api("POST", "/api/v1/sessions",
            {"project_id": project, "title": project}, 201)["data"]

    own = create("reference-probe")
    other = create("reference-other")
    own_selection = {"project_id": "reference-probe", "session_id": own["id"]}
    other_selection = {"project_id": "reference-other", "session_id": other["id"]}
    try:
        # First launch has no registered default definition. Existing sessions
        # also keep an unregistered bucket available to the original picker.
        draft("default", "default workspace input")
        status, _, body = request(port, "DELETE", "/api/v1/projects/reference-other",
            headers={"If-Match": '"mdo-project-reference-other-1"'})
        assert status == 200, (status, body)
        api("GET", control + "invalid-owner")
        api("PUT", selection_path, other_selection)
        draft("reference-other")
        snapshot(set())
        api("PUT", selection_path, own_selection)
        snapshot({"data/workspace-state.json"})
        draft(None, "legacy text without an attributable project")
        snapshot({"data/workspace-state.json"})
        draft("reference-probe")
        snapshot({"data/draft.json", "data/workspace-state.json"})
        # Damage either global record: no partial guard, writes or leaked lock.
        for path in global_files:
            original = path.read_bytes()
            before = files()
            path.write_bytes(b'{"unknown":true}')
            damaged = files()
            api("GET", control + "snapshot", expected=503)
            assert files() == damaged
            path.write_bytes(original)
            assert files() == before
            snapshot({"data/draft.json", "data/workspace-state.json"})

        # A partial global PUT retains the project pin through publication.
        current = api("GET", draft_path)["data"]
        api("PUT", draft_path, {"revision": current["revision"], "text": "partial input"})
        snapshot({"data/draft.json", "data/workspace-state.json"})
        before = files()
        start("rollback")
        stop(False, False)
        assert files() == before
        snapshot({"data/draft.json", "data/workspace-state.json"})

        # A real HTTP writer waits against the native guard's thread. Its
        # original lease was already released; the guard pin still blocks PUT.
        start("hold-draft")
        current = json.loads(global_files[0].read_text(encoding="utf-8"))
        own_task = current["new_task"]
        api("PUT", draft_path, {"revision": current["revision"], "text": "blocked",
            "new_task": own_task}, expected=409)
        assert files() == before
        outcomes: dict[str, tuple] = {}
        errors: list[BaseException] = []

        def writer(name: str, path: str, value: dict) -> None:
            try:
                outcomes[name] = request(port, "PUT", path,
                    body=json.dumps(value).encode(), headers={"Content-Type": "application/json"})
            except BaseException as error:
                errors.append(error)

        incoming_other = {**own_task, "project_id": "reference-other"}
        thread = threading.Thread(target=writer, args=("draft", draft_path,
            {"revision": current["revision"], "text": "other pending input",
             "new_task": incoming_other}))

        def wait_and_check(thread: threading.Thread, marker: str, before: dict) -> None:
            thread.start()
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline:
                if marker in log_path.read_text(encoding="utf-8", errors="replace"):
                    break
                time.sleep(0.01)
            else:
                raise AssertionError("writer never reached its final lock")
            assert not outcomes and not errors
            assert files() == before
            thread.join(timeout=5)
            assert not thread.is_alive()
            stop(True, False)

        wait_and_check(thread, "reference_waiter=1", before)
        assert not errors and outcomes["draft"][0] == 200, (errors, outcomes)
        assert api("GET", draft_path)["data"]["text"] == "other pending input"
        draft("reference-probe")
        before = files()
        outcomes.clear()
        start("hold-selection")
        thread = threading.Thread(target=writer, args=("selection", selection_path, other_selection))
        wait_and_check(thread, "reference_waiter=2", before)
        assert not errors and outcomes["selection"][0] == 200, (errors, outcomes)
        assert api("GET", selection_path)["data"] == other_selection
        draft("reference-other", "other pending input")
        snapshot(set())
        assert api("GET", draft_path)["data"]["text"] == "other pending input"
        assert api("GET", selection_path)["data"] == other_selection

        # Move only current owned references in the same storage transaction.
        draft("reference-probe")
        api("PUT", selection_path, own_selection)
        before = files()
        start("move")
        result = stop(True, True)
        expected = {p: b for p, b in before.items() if p not in {
            "projects/reference-probe.json", "data/draft.json", "data/workspace-state.json"}
            and not p.startswith("sessions/reference-probe/")}
        assert files() == expected
        assert api("GET", draft_path)["data"]["revision"] == 0
        assert api("GET", selection_path)["data"] == {"project_id": "", "session_id": ""}
        # A delayed request cannot resurrect the removed project at revision 0.
        api("PUT", draft_path, {"revision": 0, "text": "late",
            "new_task": own_task}, expected=404)
        assert files() == expected
        assert result["checks"] >= 3 and result["violations"] == 0, result

        # Recreate a test definition/session. Unassociated text and another
        # project's selection must survive an otherwise identical transaction.
        create("reference-probe")
        draft(None, "retain unattributed input")
        api("PUT", selection_path, other_selection)
        snapshot(set())
        before = files()
        start("move")
        stop(True, True)
        assert files() == {p: b for p, b in before.items()
            if p != "projects/reference-probe.json" and not p.startswith("sessions/reference-probe/")}
    finally:
        api("POST", control + "release")
        for path, content in saved.items():
            if content is None:
                path.unlink(missing_ok=True)
            else:
                path.write_bytes(content)


def run_probe(host: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="project-references-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        site, home, log_path = base / "site", base / "home", base / "xs.log"
        port = free_port()
        config = write_site(site, port)
        environment = dict(os.environ, MDO_HOME=str(home),
            MDO_ORNITH_RESPONSES_URL="https://example.invalid/v1",
            MDO_ORNITH_API_KEY="bounded-reference-fixture")
        with log_path.open("wb") as log:
            process = subprocess.Popen([str(host), str(config)], cwd=site,
                env=environment, stdout=log, stderr=subprocess.STDOUT,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
            try:
                wait_ready(port, process)
                status, _, body = request(port, "GET", "/__fixture/project-references/snapshot")
                assert status == 200 and json.loads(body)["data"]["count"] == 0, (status, body)
                assert not home.exists(), home
                project_references_probe(port, home, log_path)
            except BaseException as error:
                output = log_path.read_text(encoding="utf-8", errors="replace")
                raise RuntimeError(f"{error}\n--- xs log ---\n{output[-6000:]}") from error
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=3)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path,
        default=ROOT / ".build/host" / ("xs.exe" if os.name == "nt" else "xs"))
    run_probe(parser.parse_args().host.resolve())
    print("Project global reference coordination runtime probe: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
