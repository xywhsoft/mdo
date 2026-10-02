"""Bounded real staging bytes, independent ownership and failure cleanup.

HTTP/TLS adapters exist only in a copied source fixture. The product staging
API accepts a private directory anchor; no production restore route is exposed.
One ordinary source run and a 2 MiB artifact; no stress or high-load tests.
"""
from __future__ import annotations

import argparse
import base64
import copy
import json
import os
from pathlib import Path
import shutil
import tempfile
from unittest.mock import patch

from test_backup_decode_runtime import ROOT, DECODE, Probe as DecodeProbe, dump, replace, set_ui

STAGE = "/__fixture/backup-stage/"
ARTIFACT = "artifacts/run-00000000000000000001/00000000000000000001-stage.txt"


class Probe(DecodeProbe):
    def __init__(self, host, base, secure):
        super().__init__(host, base, secure)
        self.parent = base / "private-staging"
        self.parent.mkdir()
        shutil.copy2(ROOT / "tests/fixtures/backup-stage.c", self.site / "src/bootstrap/backup-stage.c")
        source = self.site / "src/sessions/backup_stage.c"
        text = source.read_text(encoding="utf-8")
        hook = "        Offset += Chunk;"
        assert text.count(hook) == 2
        text = text.replace(hook, hook + "\n        BackupStageFixtureAfterWrite(Source->Path, Offset);", 1)
        hook = "    if ( MdoStageVerify(Stage, Backup, &Budget, Cancel, Error) ) return true;"
        assert text.count(hook) == 1
        text = text.replace(hook, "    BackupStageFixtureBeforeVerify(Stage);\n" + hook)
        source.write_text('#include "../../include/mdo/session_backup.h"\n'
                          'void BackupStageFixtureAfterWrite(const char*, size_t);\n'
                          'void BackupStageFixtureBeforeVerify(MdoSessionBackupStage*);\n' + text,
                          encoding="utf-8", newline="\n")
        service = self.site / "src/bootstrap/service.c"
        text = service.read_text(encoding="utf-8")
        text = text.replace('#include "backup-decode.c"', '#include "backup-decode.c"\n#include "backup-stage.c"', 1)
        text = text.replace("    BackupDecodeFixtureUnit();", "    (void)MdoSessionBackupStageDiscard(&g_StageFixture, NULL);\n    BackupDecodeFixtureUnit();", 1)
        text = text.replace("    if (BackupDecodeFixtureControl", "    if (BackupStageFixtureControl(pRequest)) return XS_OK;\n    if (BackupDecodeFixtureControl", 1)
        service.write_text(text, encoding="utf-8", newline="\n")

    def fixture(self, mode):
        status, response = self.api("GET", STAGE + mode)
        assert status == 200, response
        value = response["data"]
        assert value["size_safe"] and not value["restore_ready"], value
        return value

    def prepare(self, source, mode="prepare"):
        assert self.validate(dump(source))["ok"]
        return self.fixture(mode)

    def check(self):
        status, response = self.api("POST", "/api/v1/sessions", {
            "project_id": "default", "title": "Staging source 中文", "agent_id": "mdo.default",
            "model_id": "backup-vision-fixture", "protocol": "openai-responses",
            "reasoning_effort": "medium", "max_output_tokens": 1024,
        })
        assert status == 201, response
        session = response["data"]["id"]
        assert self.api("GET", DECODE + "seed-runtime/" + session)[1]["data"]["ok"]
        path = f"/api/v1/projects/default/sessions/{session}"
        assert self.api("PUT", path + "/draft", {"revision": 0, "text": "Draft retained 草稿"})[0] == 200
        assert self.api("POST", path + "/queue", {"id": "b" * 32, "text": "Wait for confirmation",
                                                   "first": False, "stage": True})[0] == 201
        status, _, raw = self.call("GET", path + "/backup")
        assert status == 200
        source = json.loads(raw)
        replace(source, ARTIFACT, bytes(range(256)) * 8192)
        expected = {f["path"]: base64.b64decode(f["data"]) for f in source["files"]}

        def inventory():
            return {p.relative_to(self.home).as_posix(): p.read_bytes() for p in self.home.rglob("*")
                    if p.is_file() and p != self.home / ".mdo.lock"}

        before = inventory()
        result = self.prepare(source)
        assert result["ok"] and result["verified"] and result["source_id"] == session, result
        assert result["files"] == len(expected) and result["bytes"] == sum(map(len, expected.values()))
        assert result["matched"] >= 6 and result["inline_images"] == 1
        assert result["ui_records"] == source["ui_records"]
        stage = self.parent / result["directory"]
        assert {p.relative_to(stage).as_posix(): p.read_bytes() for p in stage.rglob("*") if p.is_file()} == expected
        assert self.api("GET", DECODE + "state")[1]["data"]["files"] == []
        assert self.fixture("check")["verified"]  # original decode and caller anchor were closed
        duplicate = self.prepare(source)
        assert not duplicate["ok"] and duplicate["verified"] and duplicate["directory"] == result["directory"]
        revoked = self.fixture("check-budget")
        assert not revoked["verified"] and revoked["matched"] == revoked["inline_images"] == 0
        assert self.fixture("check")["verified"]

        # An anchored parent can move. Replacing its old path cannot redirect IO.
        moved = self.parent.with_name("moved-staging")
        assert self.fixture("move-parent")["ok"]  # native no-replace handles work across platforms
        self.parent.mkdir()
        (self.parent / "foreign.txt").write_bytes(b"replacement parent")
        assert self.fixture("check")["verified"]
        assert self.fixture("discard")["ok"]
        assert not list(moved.iterdir()) and (self.parent / "foreign.txt").read_bytes() == b"replacement parent"
        (self.parent / "foreign.txt").unlink()

        for mode in ("deadline", "files", "file", "total", "cancel", "write-cancel", "corrupt", "null-parent"):
            result = self.prepare(source, mode)
            assert not result["ok"] and not result["retained"], (mode, result)
            if mode in ("cancel", "write-cancel"):
                assert result["code"] == 9, result
            if mode in ("deadline", "files", "file", "total"):
                assert result["code"] == 11, result
            assert not list(self.parent.iterdir()), mode
        assert self.prepare(source, "null-error")["verified"]
        assert self.fixture("discard")["ok"]
        assert self.fixture("discard")["ok"]

        legacy = (b'{"export_schema":1,"exported_at_us":1,"meta":' + expected["meta.json"] +
                  b',"snapshot":' + expected["snapshot.json"] + b'}')
        assert self.validate(legacy)["ok"]  # keep the snapshot's exact-byte CRC framing
        result = self.fixture("prepare")
        assert not result["ok"] and not result["retained"] and not list(self.parent.iterdir()), result

        # Unexpected content is rejected and left for the caller who owns it.
        result = self.prepare(source, "foreign")
        assert not result["ok"] and result["retained"] and not result["verified"], result
        stage = self.parent / result["directory"]
        assert {p.name: p.read_bytes() for p in stage.iterdir()} == {"unowned.txt": b"!"}
        stage.joinpath("unowned.txt").unlink()
        assert self.fixture("discard")["ok"] and not list(self.parent.iterdir())

        result = self.prepare(source)
        stage = self.parent / result["directory"]
        original = stage / ARTIFACT
        saved = self.base / "owned-artifact.txt"
        original.rename(saved); original.write_bytes(expected[ARTIFACT])
        assert not self.fixture("check")["ok"]  # identical bytes, foreign identity
        assert not self.fixture("discard")["ok"] and original.read_bytes() == expected[ARTIFACT]
        original.unlink(); saved.rename(original)
        assert self.fixture("discard")["ok"] and not list(self.parent.iterdir())

        # Schema-valid but contradictory UI must fail the actual model gate.
        bad = copy.deepcopy(source)
        start_kind = self.api("GET", DECODE + "kinds")[1]["data"]["start"]
        events = [json.loads(line) for line in expected["ui-events.jsonl"].splitlines()]
        for event in events:
            if event["kind"] == start_kind and event["agent_depth"] == 0:
                event["text"] = "Contradictory imported user text"; break
        else:
            raise AssertionError("fixture has no main Agent start")
        set_ui(bad, events)
        result = self.prepare(bad)
        assert not result["ok"] and not result["retained"], result
        assert not list(self.parent.iterdir())
        bad = copy.deepcopy(source)
        image_id = "d" * 32
        replace(bad, f"attachments/{image_id}.bin", b"\x89PNG\r\n\x1a\n")
        replace(bad, f"attachments/{image_id}.json", dump({"schema_version": 1, "id": image_id,
                    "mime_type": "image/png", "size": 8, "created_at": 1}))
        result = self.prepare(bad)
        assert not result["ok"] and not result["retained"] and not list(self.parent.iterdir()), result
        assert self.prepare(source)["verified"] and self.fixture("discard")["ok"]
        self.check_projections(source, expected)
        assert inventory() == before  # no source Home, queue, model or catalog writes

    def check_projections(self, source, expected):
        kinds = self.api("GET", DECODE + "kinds")[1]["data"]
        events = [json.loads(line) for line in expected["ui-events.jsonl"].splitlines()]
        last = events[-1]["event_id"]
        completed = next(e["event_id"] for e in events if e["kind"] == kinds["model"] and e["success"])

        def event(identifier, kind, **fields):
            row = {**events[-1], "event_id": identifier, "kind": kind, "source_event_id": 0,
                   "agent_turn": 0, "user_message_sequence": 0, "agent_depth": 0, "agent_id": 0,
                   "run_id": 0, "parent_run_id": 0, "task_id": 0, "artifact_id": 0,
                   "artifact_path": "", "queue_item_id": "", "text": "", "tool_name": "",
                   "tool_call_id": "", "success": True, "effect_applied": False, "text_truncated": False}
            row.update(fields)
            return row

        def files(document):
            return {f["path"]: base64.b64decode(f["data"]) for f in document["files"]}

        def repaired(document, **counts):
            result = self.prepare(document, "reconcile")
            assert result["ok"] and result["verified"] and result["original_unchanged"], result
            for key, value in counts.items():
                assert result[key] == value, result
            if any(e["kind"] == kinds["tool_done"] and e["tool_call_id"] == "" and e["agent_turn"] == 0
                   for e in map(json.loads, files(document)["ui-events.jsonl"].splitlines())):
                assert result["model_unverified"] > 0, result  # legacy relation remains explicitly unverified
            stage = self.parent / result["directory"]
            actual = {p.relative_to(stage).as_posix(): p.read_bytes() for p in stage.rglob("*") if p.is_file()}
            assert actual["snapshot.json"] == expected["snapshot.json"]
            for name in ("journal.jsonl", "draft.json", "queue.json", ARTIFACT):
                if name in expected:
                    assert actual[name] == expected[name], name
            assert actual["ui-events.jsonl"] == files(document)["ui-events.jsonl"]
            assert self.fixture("check")["verified"] and self.fixture("discard")["ok"]
            return actual

        removed = last + 2
        document = copy.deepcopy(source)
        set_ui(document, events + [event(last + 5, kinds["removed"], source_event_id=last + 1)])
        binding = f"attachments/events/{removed}.json"
        replace(document, binding, dump({"schema_version": 1, "run_id": 1, "attachments": []}))
        replace(document, "feedback.json", dump({"schema_version": 1, "items": [
            {"event_id": completed, "value": "good"}, {"event_id": removed, "value": "bad"}]}))
        replace(document, "todo.json", dump({"schema_version": 1, "event_id": removed,
                                             "items": [{"text": "Removed plan", "done": False}]}))
        actual = repaired(document, repaired_bindings=1, repaired_feedback=1, repaired_todo=True, removed_refs=0)
        assert binding not in actual
        assert json.loads(actual["feedback.json"])["items"] == [{"event_id": completed, "value": "good"}]
        assert json.loads(actual["todo.json"]) == {"schema_version": 1, "event_id": 0, "items": []}

        # The original input survives cancellation/failure with no repair facts.
        for mode, code in (("reconcile-cancel", 9), ("reconcile-deadline", 11)):
            result = self.prepare(document, mode)
            assert not result["ok"] and not result["retained"] and result["code"] == code, result
            assert result["original_unchanged"] and not result["repaired_todo"]
            assert result["repaired_bindings"] == result["repaired_feedback"] == 0
            assert not list(self.parent.iterdir())

        # Evicted prefix evidence is unknown, not a removal authorization.
        document = copy.deepcopy(source)
        retained = [e for e in events if e["event_id"] > completed]
        set_ui(document, retained)
        binding = "attachments/events/1.json"
        replace(document, binding, dump({"schema_version": 1, "run_id": 1, "attachments": []}))
        replace(document, "feedback.json", dump({"schema_version": 1, "items": [{"event_id": completed, "value": "good"}]}))
        replace(document, "todo.json", dump({"schema_version": 1, "event_id": 1,
                                             "items": [{"text": "Unverified older plan", "done": False}]}))
        actual = repaired(document, repaired_bindings=0, repaired_feedback=0, repaired_todo=False, unverified_refs=3)
        for name in (binding, "feedback.json", "todo.json"):
            assert actual[name] == files(document)[name]

        # Legacy zero-source clear resets todo, preserving unrelated feedback.
        document = copy.deepcopy(source)
        set_ui(document, events + [event(last + 5, kinds["removed"])])
        replace(document, "feedback.json", dump({"schema_version": 1, "items": [{"event_id": completed, "value": "bad"}]}))
        replace(document, "todo.json", dump({"schema_version": 1, "event_id": removed,
                                             "items": [{"text": "Pre-clear plan", "done": False}]}))
        actual = repaired(document, repaired_bindings=0, repaired_feedback=0, repaired_todo=True)
        assert json.loads(actual["todo.json"])["items"] == []
        assert actual["feedback.json"] == files(document)["feedback.json"]

        # Current sidecars can lag a successful tool event after a write error.
        first = event(last + 1, kinds["tool_done"], tool_name="mdo.todo",
                      text=dump({"items": [{"text": "Latest plan 中文", "done": False}]}).decode())
        second = event(last + 2, kinds["tool_done"], tool_name="mdo.todo",
                       text=dump({"items": [{"text": "Latest plan 中文", "done": True}]}).decode())
        document = copy.deepcopy(source); set_ui(document, events + [first, second])
        replace(document, "todo.json", dump({"schema_version": 1, "event_id": first["event_id"],
                                             "items": json.loads(first["text"])["items"]}))
        actual = repaired(document, repaired_todo=True)
        assert json.loads(actual["todo.json"]) == {"schema_version": 1, "event_id": second["event_id"],
                                                  "items": json.loads(second["text"])["items"]}
        document = copy.deepcopy(source); set_ui(document, events + [second])
        result = self.prepare(document, "reconcile-growth")
        assert not result["ok"] and result["code"] == 11 and result["original_unchanged"], result
        assert not result["repaired_todo"] and not list(self.parent.iterdir())
        actual = repaired(document, repaired_todo=True)
        assert json.loads(actual["todo.json"])["event_id"] == second["event_id"]

        malformed = copy.deepcopy(source)
        set_ui(malformed, events + [{**second, "text": "{"}])
        result = self.prepare(malformed, "reconcile")
        assert not result["ok"] and not result["retained"] and result["original_unchanged"], result
        assert not result["repaired_todo"] and not list(self.parent.iterdir())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, required=True)
    host = parser.parse_args().host.resolve()
    for secure in (False, True):
        with tempfile.TemporaryDirectory(prefix="backup-stage-", dir=ROOT / ".build") as raw:
            probe = Probe(host, Path(raw), secure)
            try:
                with patch.dict(os.environ, MDO_STAGE_FIXTURE_PARENT=str(probe.parent.resolve())):
                    probe.start()
                probe.check()
            finally:
                probe.stop()
    print("private session backup staging HTTP/TLS probe: PASS")


if __name__ == "__main__":
    main()
