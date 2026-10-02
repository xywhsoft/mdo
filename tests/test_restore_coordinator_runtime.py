"""Bounded production restore transaction on one owned native worker.

Real v2 model/UI/pixels, a normal 2 MiB artifact, target reservations,
project revalidation, cancellation, catalog publication and post-commit errors
over loopback HTTP/TLS. All controls/faults exist only in copied test sources.
No Agent execution during restore, stress, or production restore route.
"""
from __future__ import annotations

import argparse
import base64
import copy
import hashlib
import json
import os
from pathlib import Path
import shutil
import tempfile
import time

from test_backup_decode_runtime import ROOT, DECODE, Probe as DecodeProbe, dump, replace, set_ui

CONTROL = "/__fixture/restore-coordinator/"
ARTIFACT = "artifacts/run-00000000000000000001/00000000000000000001-restore.txt"


class Probe(DecodeProbe):
    def __init__(self, host, base, secure):
        super().__init__(host, base, secure)
        self.workspace = base / "workspace-中文"
        self.workspace.mkdir()
        shutil.copy2(ROOT / "tests/fixtures/restore-coordinator.c",
                     self.site / "src/bootstrap/restore-coordinator.c")
        source = self.site / "src/sessions/restore.c"
        text = source.read_text(encoding="utf-8")
        for before, after in (
            ("    (void)Current;", "    (void)Current;\n    RestoreFixtureCommit();"),
            ("    Publication.Reservation = Reservation;", "    RestoreFixturePause();\n    Publication.Reservation = Reservation;"),
        ):
            assert text.count(before) == 1
            text = text.replace(before, after, 1)
        source.write_text("void RestoreFixturePause(void);\nvoid RestoreFixtureCommit(void);\n" + text,
                          encoding="utf-8", newline="\n")
        source = self.site / "src/sessions/restore_reservation.inc.c"
        text = source.read_text(encoding="utf-8")
        hook = "        Restore->Data = MdoSessionDataAcquire(ProjectId, SessionId, MDO_SESSION_DATA_CAPTURE, Error);"
        assert text.count(hook) == 1
        source.write_text("void RestoreFixtureGap(cstr, cstr);\n" + text.replace(hook,
            "        RestoreFixtureGap(ProjectId, SessionId);\n" + hook, 1), encoding="utf-8", newline="\n")
        source = self.site / "src/projects/binding.c"
        text = source.read_text(encoding="utf-8")
        assert text.count("!xrtClose(File)") == 1
        source.write_text("#include <xsbase.h>\nbool RestoreFixtureClose(xfile);\n" +
            text.replace("!xrtClose(File)", "!RestoreFixtureClose(File)"), encoding="utf-8", newline="\n")
        source = self.site / "src/storage/home_restore.inc.c"
        text = source.read_text(encoding="utf-8")
        hook = "    return MdoHomePurgeMove(MDO_HOME_RESTORE_DIR, MDO_HOME_RESTORE_GC, Expected) && MdoHomeRestoreGc();"
        assert text.count(hook) == 1
        source.write_text("bool RestoreFixtureRetire(const xfileinfo*);\n" +
                          text.replace(hook, "    return RestoreFixtureRetire(Expected);", 1),
                          encoding="utf-8", newline="\n")
        service = self.site / "src/bootstrap/service.c"
        text = service.read_text(encoding="utf-8")
        text = text.replace('#include "backup-decode.c"', '#include "backup-decode.c"\n#include "restore-coordinator.c"', 1)
        text = text.replace("    BackupDecodeFixtureUnit();", "    RestoreFixtureUnit();\n    BackupDecodeFixtureUnit();", 1)
        text = text.replace("    if (BackupDecodeFixtureControl", "    if (RestoreFixtureControl(pRequest)) return XS_OK;\n    if (BackupDecodeFixtureControl", 1)
        service.write_text(text, encoding="utf-8", newline="\n")

    def restore(self, mode):
        status, value = self.api("GET", CONTROL + mode)
        assert status == 200, value
        assert not value["data"]["restore_ready"], value
        return value["data"]

    def wait(self, field):
        deadline = time.monotonic() + 5
        while True:
            state = self.restore("state")
            if state.get(field):
                return state
            assert not state.get("done") and time.monotonic() < deadline, state
            time.sleep(0.01)

    def target(self, identifier):
        return self.home / "sessions/restore-target" / identifier

    def start_restore(self, document, mode, identifier):
        assert self.validate(dump(document))["ok"]
        state = self.restore(f"start/{mode}/{identifier}")
        assert state["ok"], state
        assert self.api("GET", DECODE + "state")[1]["data"]["files"] == []
        return self.wait("ready" if mode == "hold" else "done")

    def clean_failed(self, state, identifier, code=None):
        assert not state["published"] and not state["committed"] and not state["restart"], state
        assert not state["verified"] and state["facts_size"] == state["matched"] == state["inline_images"] == 0, state
        assert state["result_generation"] == 0 and state["generation"] == state["before"], state
        assert state["reservations"] == 0 and not self.target(identifier).exists(), state
        if code is not None:
            assert state["code"] == code, state
        for name in (".mdo-session-restore", ".mdo-session-restore-cleanup"):
            assert not (self.home / name).exists(), state

    def setup_source(self):
        status, response = self.api("POST", "/api/v1/projects", {
            "id": "restore-target", "name": "Restore target", "workspace_root": str(self.workspace.resolve()),
            "default_model_id": "ornith-1.5-35b",
        })
        assert status == 201, response
        status, response = self.api("POST", "/api/v1/sessions", {
            "project_id": "default", "title": "Restore source 中文", "agent_id": "mdo.default",
            "model_id": "backup-vision-fixture", "protocol": "openai-responses",
            "reasoning_effort": "medium", "max_output_tokens": 1024,
        })
        assert status == 201, response
        session = response["data"]["id"]
        seeded = self.api("GET", DECODE + "seed-runtime/" + session)[1]["data"]
        assert seeded["ok"] and seeded["calls"] == 3, seeded
        prefix = f"/api/v1/projects/default/sessions/{session}"
        assert self.api("PUT", prefix + "/draft", {"revision": 0, "text": "Preserve 草稿"})[0] == 200
        queued = self.api("POST", prefix + "/queue", {"id": "b" * 32, "text": "Review before run", "first": False, "stage": True})
        assert queued[0] == 201, queued
        status, _, raw = self.call("GET", prefix + "/backup")
        assert status == 200, raw[:1024]
        document = json.loads(raw)
        payload = bytes(range(256)) * 8192
        replace(document, ARTIFACT, payload)
        events = [json.loads(row) for row in base64.b64decode(next(f["data"] for f in document["files"]
                    if f["path"] == "ui-events.jsonl")).splitlines()]
        kind = self.api("GET", DECODE + "kinds")[1]["data"]["artifact"]
        self.artifact_event = events[-1]["event_id"] + 1
        events.append({**events[-1], "event_id": self.artifact_event, "kind": kind, "run_id": 1,
                       "artifact_id": 1, "artifact_path": "D:/old-home/sessions/default/old/" + ARTIFACT,
                       "text": "Restored artifact", "tool_name": "", "tool_call_id": "", "queue_item_id": ""})
        set_ui(document, events)
        self.source_directory = self.home / "sessions/default" / session
        self.source_bytes = self.inventory(self.source_directory)
        return document, payload

    @staticmethod
    def inventory(root):
        return {p.relative_to(root).as_posix(): p.read_bytes() for p in root.rglob("*") if p.is_file()}

    def check_committed(self, state, identifier, document, payload, *, fault=False):
        assert state["committed"] and state["published"] == (not fault) and state["restart"] == fault, state
        assert state["verified"] and state["matched"] >= 6 and state["inline_images"] == 1, state
        assert state["result_generation"] == state["generation"] == state["before"] + 1, state
        assert state["reservations"] == 0 and state["facts_size"] > 0, state
        target = self.target(identifier)
        actual = self.inventory(target)
        expected = {f["path"]: base64.b64decode(f["data"]) for f in document["files"]}
        for name in ("snapshot.json", "journal.jsonl", ARTIFACT):
            if name in expected:
                assert actual[name] == expected[name], name
        meta = json.loads(actual["meta.json"])
        assert meta["id"] == identifier and meta["project_id"] == "restore-target" and meta["revision"] == 1
        assert meta["workspace_root"] == state["workspace"]
        assert os.path.samefile(meta["workspace_root"], self.workspace), (meta["workspace_root"], str(self.workspace))
        assert json.loads(actual["queue.json"])["items"][0]["state"] == "staged"
        assert json.loads(actual["restore-origin.json"])["imports"][0]["target_meta"]["data"].encode() == actual["meta.json"]
        assert "restore-inputs.json" in actual
        status, catalog = self.api("GET", "/api/v1/sessions?project_id=restore-target")
        assert status == 200, catalog
        item = next(item for item in catalog["data"]["items"] if item["id"] == identifier)
        assert not item["runtime_open"], item
        prefix = f"/api/v1/projects/restore-target/sessions/{identifier}"
        status, response = self.api("GET", prefix + f"/artifacts/{self.artifact_event}?offset=0&limit=65536")
        assert status == 200, response
        data = response["data"]
        assert data["total_size"] == len(payload) and base64.b64decode(data["data"]) == payload[:65536]
        assert data["sha256"] == hashlib.sha256(payload).hexdigest() and data["event_id"] == self.artifact_event
        assert self.inventory(self.source_directory) == self.source_bytes
        return actual

    def check(self):
        # Pure reservation/capacity and wrong-size output checks create no Home.
        assert not self.home.exists()
        state = self.restore("capacity")
        assert state["ok"] and state["checks"] >= 22 and state["reservations"] == 0, state
        assert not self.home.exists()
        document, payload = self.setup_source()
        for mode, code, identifier in (("cancel", 9, "1" * 32), ("deadline", 11, "2" * 32),
                                      ("cancel-commit", 9, "3" * 32), ("generation", 11, "4" * 32)):
            self.clean_failed(self.start_restore(document, mode, identifier), identifier, code)

        # One live worker pauses after closing Stage anchors. No partial target
        # or catalog item is visible; aliases and all data writers are excluded.
        identifier = "a" * 32
        self.start_restore(document, "hold", identifier)
        assert not self.target(identifier).exists()
        state = self.restore("reserved")
        assert state["ok"] and state["checks"] >= 35 and state["reservations"] == 1, state
        self.restore("cancel")
        self.clean_failed(self.wait("done"), identifier, 9)

        identifier = "5" * 32
        self.start_restore(document, "hold", identifier)
        status, response = self.api("PUT", "/api/v1/projects/restore-target", {
            "name": "Changed after review", "workspace_root": str(self.workspace.resolve()),
            "default_model_id": "ornith-1.5-35b",
        }, {"If-Match": '"mdo-project-restore-target-1"'})
        assert status == 200 and response["data"]["revision"] == 2, response
        self.restore("resume")
        self.clean_failed(self.wait("done"), identifier, 7)

        identifier = "6" * 32
        self.start_restore(document, "hold", identifier)
        moved = self.workspace.with_name("workspace-saved")
        self.workspace.rename(moved); self.workspace.mkdir()
        (self.workspace / "foreign.txt").write_bytes(b"new workspace")
        self.restore("resume")
        self.clean_failed(self.wait("done"), identifier, 7)
        assert (self.workspace / "foreign.txt").read_bytes() == b"new workspace"

        identifier = "7" * 32
        state = self.start_restore(document, "gap-writer", identifier)
        assert not state["committed"] and state["generation"] == state["before"] and state["reservations"] == 0, state
        assert self.inventory(self.target(identifier)) == {"foreign.txt": b"foreign bytes"}

        bad = copy.deepcopy(document)
        image = "d" * 32
        replace(bad, f"attachments/{image}.bin", b"\x89PNG\r\n\x1a\n")
        replace(bad, f"attachments/{image}.json", dump({"schema_version": 1, "id": image,
                "mime_type": "image/png", "size": 8, "created_at": 1}))
        self.clean_failed(self.start_restore(bad, "normal", "8" * 32), "8" * 32)

        identifier = "9" * 32
        self.start_restore(document, "hold", identifier)
        self.restore("resume")
        state = self.wait("done")
        actual = self.check_committed(state, identifier, document, payload)
        assert not (self.home / ".mdo-session-restore").exists()
        assert not (self.home / ".mdo-session-restore-cleanup").exists()
        again = self.start_restore(document, "normal", identifier)
        assert not again["committed"] and again["generation"] == again["before"] == state["generation"], again
        assert self.inventory(self.target(identifier)) == actual
        assert self.inventory(self.source_directory) == self.source_bytes

    def check_fault(self, fault):
        document, payload = self.setup_source()
        identifier = "c" * 32
        state = self.start_restore(document, fault, identifier)
        self.check_committed(state, identifier, document, payload, fault=True)
        assert state["code"] == 6, state
        # Same owned result remains queryable, and committed bytes stay intact.
        actual = self.inventory(self.target(identifier))
        assert self.restore("state") == state
        assert self.inventory(self.target(identifier)) == actual


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, required=True)
    host = parser.parse_args().host.resolve()
    for secure in (False, True):
        for fault in (None, "close-fault", "gc-fault"):
            with tempfile.TemporaryDirectory(prefix="restore-coordinator-", dir=ROOT / ".build") as raw:
                probe = Probe(host, Path(raw), secure)
                try:
                    probe.start()
                    probe.check_fault(fault) if fault else probe.check()
                except Exception:
                    print(f"restore fixture failure: tls={secure} fault={fault}", flush=True)
                    print((Path(raw) / "xs.log").read_text(errors="replace"), flush=True)
                    raise
                finally:
                    probe.stop()
    print("production restore coordinator/reservation HTTP/TLS probe: PASS")


if __name__ == "__main__":
    main()
