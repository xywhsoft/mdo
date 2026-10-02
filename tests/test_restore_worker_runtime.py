"""Bounded production review/admission/restore worker over HTTP and TLS.

One actual v2 model/UI/image bundle with a 2 MiB artifact. Copied-source hooks
hold one worker or refuse one submission; no stress/high-load or live provider.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import tempfile
import time

from test_restore_coordinator_runtime import ROOT, Probe as CoordinatorProbe, dump, ARTIFACT
from test_backup_upload_runtime import PREFIX, CHUNK

PREVIEWS = "/api/v1/session-backups/previews/"
RESTORES = "/api/v1/session-backups/restores/"
CONTROL = "/__fixture/restore-worker/"


class Probe(CoordinatorProbe):
    def __init__(self, host, base, secure):
        super().__init__(host, base, secure)
        shutil.copy2(ROOT / "tests/fixtures/restore-worker.c", self.site / "src/bootstrap/restore-worker.c")
        service = self.site / "src/bootstrap/service.c"
        text = service.read_text(encoding="utf-8")
        text = text.replace('#include "restore-coordinator.c"', '#include "restore-coordinator.c"\n#include "restore-worker.c"', 1)
        text = text.replace("    if (RestoreFixtureControl", "    if (RestoreWorkerFixtureControl(pRequest)) return XS_OK;\n    if (RestoreFixtureControl", 1)
        service.write_text(text, encoding="utf-8", newline="\n")
        api = self.site / "src/api/backup_restore.c"
        text = api.read_text(encoding="utf-8")
        for before, after in (
            ("    Ok = MdoSessionRestoreExecute", "    RestoreWorkerFixturePause(Cancel, false);\n    Ok = MdoSessionRestoreExecute"),
            ("    Future = xrtTaskSubmit", "    RestoreWorkerFixtureBeforeSubmit(Job);\n    Future = xrtTaskSubmit"),
            ("    MdoApiBackupPreviewRelease(Job->Document); Job->Document = NULL;",
             "    RestoreWorkerFixturePause(Job->Cancel, true);\n    MdoApiBackupPreviewRelease(Job->Document); Job->Document = NULL;"),
        ):
            assert text.count(before) == 1, before
            text = text.replace(before, after, 1)
        # A Job prototype must follow the concrete private declaration.
        hook = "static MdoBackupRestoreStore* g_MdoBackupRestores;"
        text = text.replace(hook, hook + "\nvoid RestoreWorkerFixturePause(xcancel*, bool);\n"
                            "void RestoreWorkerFixtureBeforeSubmit(MdoBackupRestoreJob*);", 1)
        api.write_text(text, encoding="utf-8", newline="\n")
        storage = self.site / "src/storage/home_restore.inc.c"
        text = storage.read_text(encoding="utf-8")
        text = text.replace("return RestoreFixtureRetire(Expected);", "return RestoreWorkerFixtureRetire(Expected);", 1)
        storage.write_text("bool RestoreWorkerFixtureRetire(const xfileinfo*);\n" + text,
                           encoding="utf-8", newline="\n")

    def fixture(self, action="state"):
        status, value = self.api("GET", CONTROL + action)
        assert status == 200, value
        return value["data"]

    def until(self, check, seconds=5):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            result = check()
            if result:
                return result
            assert self.process.poll() is None
            time.sleep(0.01)
        raise AssertionError("bounded worker condition did not finish")

    def preview(self, document):
        data, identifier = dump(document), "e" * 32
        self.control("release")
        path = PREFIX + "/" + identifier
        assert self.api("DELETE", path)[0] in (200, 404)
        assert self.api("POST", PREFIX, {"id": identifier, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()})[0] == 201
        for offset in range(0, len(data), CHUNK):
            assert self.api("PUT", path + f"/chunks/{offset}", data[offset:offset + CHUNK],
                            {"Content-Type": "application/octet-stream"})[0] == 200
        assert self.api("POST", path + "/seal")[0] == 200
        status, value = self.api("POST", path + "/preview")
        assert status == 202, value
        preview = value["data"]["id"]
        def poll():
            result = self.api("GET", PREVIEWS + preview)[1]["data"]
            return result if result["terminal"] else None
        result = self.until(poll)
        assert result["state"] == "succeeded", result
        return preview, hashlib.sha256(data).hexdigest()

    def review(self, preview, project="restore-target"):
        status, value = self.api("POST", PREVIEWS + preview + "/restore-review", {"project_id": project})
        assert status in (200, 201), value
        data = value["data"]
        assert not data["accepted"] and not data["committed"] and not data["terminal"], data
        assert data["project_id"] == project and data["session_id"] == data["id"]
        return data

    def state(self, identifier):
        status, value = self.api("GET", RESTORES + identifier)
        assert status == 200, value
        return value["data"]

    def apply(self, identifier):
        status, value = self.api("POST", RESTORES + identifier + "/apply")
        assert status in (200, 202), value
        return value["data"]

    def finished(self, identifier):
        return self.until(lambda: (result if (result := self.state(identifier)).get("worker_finished") else None))

    def discard(self, identifier):
        assert self.api("DELETE", RESTORES + identifier)[0] == 200

    def check(self):
        assert not self.home.exists()
        assert self.api("GET", RESTORES.rstrip("/"))[1]["data"] == {"empty": True}
        assert self.call("HEAD", RESTORES.rstrip("/"))[0] == 200
        assert self.api("GET", RESTORES + "a" * 32)[0] == 404
        assert self.call("HEAD", RESTORES + "a" * 32)[0] == 404
        assert self.api("POST", RESTORES + "a" * 32 + "/apply")[0] == 404
        assert not self.home.exists()
        document, payload = self.setup_source()
        preview, digest = self.preview(document)
        review_path = PREVIEWS + preview + "/restore-review"
        assert self.api("POST", review_path, {"project_id": "restore-target"}, token=False)[0] == 428
        for body in ({}, {"project_id": "missing"}, {"project_id": "restore-target", "session_id": "a" * 32}):
            assert self.api("POST", review_path, body)[0] >= 400
        review = self.review(preview)
        identifier = review["id"]
        assert self.api("GET", RESTORES.rstrip("/"))[1]["data"] == review
        assert self.review(preview) == review
        assert review["source_sha256"] == digest and review["source_session_id"] == document["session_id"]
        assert not (self.home / ".mdo-session-restore").exists()
        assert self.api("POST", RESTORES + identifier + "/apply", token=False)[0] == 428
        assert self.api("POST", RESTORES + identifier + "/apply", {"confirm": True})[0] == 400
        self.discard(identifier)
        assert self.api("GET", RESTORES.rstrip("/"))[1]["data"] == {"empty": True}
        assert self.api("GET", RESTORES + identifier)[0] == 404
        assert self.api("POST", RESTORES + identifier + "/apply")[0] == 404
        assert not (self.home / "data/session-restores").exists()

        virtual = self.review(preview, "default")
        assert virtual["project_revision"] == virtual["project_created_at"] == 0
        self.discard(virtual["id"])

        review = self.review(preview)
        self.fixture("expire")
        assert self.api("POST", RESTORES + review["id"] + "/apply")[0] == 404
        assert self.api("GET", RESTORES + review["id"])[0] == 404

        # Pre-cancelled submission skips Run but Drop settles the accepted owner.
        for mode, expected_code in ((2, 9), (4, 11), (3, 6)):
            review = self.review(preview)
            identifier = review["id"]
            before = self.fixture(f"mode/{mode}")
            self.apply(identifier)
            state = self.finished(identifier)
            assert state["accepted"] and state["state"] == "aborted" and not state["committed"], state
            assert state["error_code"] == expected_code, state
            after = self.fixture()
            assert after["generation"] == before["generation"] and after["reservations"] == 0
            assert after["checks"] == before["checks"] + 1
            if mode in (2, 3):
                assert after["runs"] == before["runs"]
            self.discard(identifier)
            assert self.state(identifier)["state"] == "aborted"
            assert self.apply(identifier)["state"] == "aborted"
            assert not self.target(identifier).exists()
            if mode == 3:
                self.fixture("reset")

        # Accepted queued work owns a preview pin, query stays responsive, and
        # Unit joins/aborts it before the preview or managers can retire.
        review = self.review(preview)
        before = self.fixture("mode/1")
        self.apply(review["id"])
        self.until(lambda: self.fixture()["ready"])
        assert self.state(review["id"])["state"] == "pending"
        assert self.call("HEAD", RESTORES + review["id"])[0] == 200
        self.fixture("reset")
        assert self.state(review["id"])["state"] == "aborted"
        assert self.fixture()["reservations"] == 0

        # Replacing the reviewed project rejects admission without accepting
        # or silently re-capturing its revision on confirmation.
        review = self.review(preview)
        assert self.api("PUT", "/api/v1/projects/restore-target", {
            "name": "Changed", "workspace_root": str(self.workspace.resolve()), "default_model_id": "ling-3.0-tiny",
        }, {"If-Match": f'"mdo-project-restore-target-{review["project_revision"]}"'})[0] == 200
        state = self.apply(review["id"])
        assert not state["accepted"] and state["worker_finished"] and state["error_code"] == 7, state
        self.discard(review["id"])

        # The queued worker repeats the accepted binding before expensive work.
        # Both a definition update and a same-path physical replacement abort.
        for change in ("revision", "workspace"):
            review = self.review(preview)
            self.fixture("mode/1")
            self.apply(review["id"])
            self.until(lambda: self.fixture()["ready"])
            if change == "revision":
                assert self.api("PUT", "/api/v1/projects/restore-target", {
                    "name": "Changed while queued", "workspace_root": str(self.workspace.resolve()),
                    "default_model_id": "ling-3.0-tiny",
                }, {"If-Match": f'"mdo-project-restore-target-{review["project_revision"]}"'})[0] == 200
            else:
                saved = self.workspace.with_name("saved-workspace")
                self.workspace.rename(saved)
                self.workspace.mkdir()
            self.fixture("resume")
            state = self.finished(review["id"])
            assert state["accepted"] and state["state"] == "aborted" and state["error_code"] == 7, state
            assert not self.target(review["id"]).exists()
            self.discard(review["id"])
            if change == "workspace":
                self.workspace.rmdir()
                saved.rename(self.workspace)

        # TTL/delete of the preview/upload cannot invalidate accepted work.
        review = self.review(preview)
        identifier = review["id"]
        before = self.fixture("mode/1")
        self.apply(identifier)
        self.until(lambda: self.fixture()["ready"])
        pending = self.apply(identifier)
        assert pending["accepted"] and pending["state"] == "pending"
        assert self.api("DELETE", PREVIEWS + preview)[0] == 200
        assert self.api("DELETE", PREFIX + "/" + "e" * 32)[0] == 200
        assert self.api("POST", review_path, {"project_id": "restore-target"})[0] == 404
        self.fixture("resume")
        state = self.finished(identifier)
        assert state["state"] == "committed" and state["committed"] and state["error_code"] == 0, state
        assert self.fixture()["generation"] == before["generation"] + 1
        actual = self.inventory(self.target(identifier))
        assert actual[ARTIFACT] == payload
        assert json.loads(actual["queue.json"])["items"][0]["state"] == "staged"
        assert self.inventory(self.source_directory) == self.source_bytes
        catalog = self.api("GET", "/api/v1/sessions?project_id=restore-target")[1]["data"]["items"]
        assert next(item for item in catalog if item["id"] == identifier)["runtime_open"] is False
        assert self.apply(identifier)["committed"]
        assert self.inventory(self.target(identifier)) == actual
        self.fixture("expire")
        assert self.state(identifier)["committed"] and "phase" not in self.state(identifier)
        self.fixture("reset")
        shutil.rmtree(self.target(identifier))
        revision = self.api("GET", "/api/v1/projects/restore-target")[1]["data"]["revision"]
        status, value = self.api("DELETE", "/api/v1/projects/restore-target", headers={
            "If-Match": f'"mdo-project-restore-target-{revision}"'})
        assert status == 200, value
        assert self.state(identifier)["committed"] and self.apply(identifier)["committed"]

        record = self.home / "data/session-restores" / (identifier + ".json")
        record.write_bytes(b"damaged result")
        assert self.api("GET", RESTORES + identifier)[0] == 503
        assert self.api("POST", RESTORES + identifier + "/apply")[0] == 503
        assert record.read_bytes() == b"damaged result"

    def check_interruption(self, mode):
        document, payload = self.setup_source()
        preview, _ = self.preview(document)
        review = self.review(preview)
        identifier = review["id"]
        before = self.fixture(f"mode/{mode}")
        self.apply(identifier)
        if mode in (1, 6):
            self.until(lambda: self.fixture()["ready"])
            state = self.state(identifier)
            assert state["committed"] == (mode == 6), state
            if mode == 6:
                self.discard(identifier)  # cancellation after commit preserves it
                state = self.finished(identifier)
                assert state["committed"] and state["error_code"] == 0
            else:
                assert state["state"] == "pending" and not self.target(identifier).exists()
        else:
            state = self.finished(identifier)
            assert state["committed"] and state["restart_required"] and state["error_code"] == 6, state
            assert self.apply(identifier)["committed"]  # frozen Home replay
        if mode in (1, 5):
            # Kill only this isolated helper, so both platforms prove startup
            # recovery instead of relying on a graceful Linux SIGTERM cleanup.
            self.process.kill()
            self.process.wait(timeout=5)
        self.stop()
        self.start()  # requested journal recovery never re-executes the worker
        state = self.state(identifier)
        assert state["accepted"] and state["state"] == ("aborted" if mode == 1 else "committed"), state
        assert state["committed"] == (mode != 1)
        assert self.apply(identifier) == state
        assert self.fixture()["runs"] == self.fixture()["submits"] == 0
        assert self.target(identifier).exists() == (mode != 1)
        if mode != 1:
            assert self.inventory(self.target(identifier))[ARTIFACT] == payload
        assert self.inventory(self.source_directory) == self.source_bytes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, required=True)
    host = parser.parse_args().host.resolve()
    for secure in (False, True):
        for mode in (0, 1, 5, 6):
            with tempfile.TemporaryDirectory(prefix="restore-worker-", dir=ROOT / ".build") as raw:
                probe = Probe(host, Path(raw), secure)
                try:
                    probe.start()
                    probe.check() if mode == 0 else probe.check_interruption(mode)
                except Exception:
                    print(f"restore worker failure: tls={secure} mode={mode}", flush=True)
                    print((Path(raw) / "xs.log").read_text(errors="replace"), flush=True)
                    raise
                finally:
                    probe.stop()
    print("production restore review/durable admission/worker HTTP/TLS probe: PASS")


if __name__ == "__main__":
    main()
