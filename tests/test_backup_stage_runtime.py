"""Bounded real staging bytes, independent ownership and failure cleanup.

HTTP/TLS adapters exist only in a copied source fixture. The product staging
API accepts a private directory anchor; no production restore route is exposed.
One ordinary source run and a 2 MiB artifact; no stress or high-load tests.
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
import subprocess
import tempfile
from unittest.mock import patch

from test_backup_decode_runtime import ROOT, DECODE, Probe as DecodeProbe, dump, replace, set_ui, file_entry, recount

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
        source = self.site / "src/sessions/backup_submissions.c"
        text = source.read_text(encoding="utf-8")
        hook = '        Candidate = xrtSecureStringFrom(XRT_STR_LITERAL("0123456789abcdef"), 32u);'
        assert text.count(hook) == 1
        text = text.replace(hook, hook + "\n        BackupReviewFixtureCandidate(&Candidate, Attempt);", 1)
        source.write_text('#include <xsbase.h>\nvoid BackupReviewFixtureCandidate(str*, size_t);\n' + text,
                          encoding="utf-8", newline="\n")
        source = self.site / "src/sessions/backup_input_archive.c"
        text = source.read_text(encoding="utf-8")
        hook = '    if ( !xrtSha256(Text.Data, Text.Size, Digest) ) return MdoArchiveError(Error, "cannot hash archived input bytes");'
        assert text.count(hook) == 1
        text = text.replace(hook, hook + "\n    BackupReviewFixtureArchiveHash();", 1)
        source.write_text('void BackupReviewFixtureArchiveHash(void);\n' + text, encoding="utf-8", newline="\n")
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
        decoded = self.validate(dump(source))
        assert decoded["ok"], decoded
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
        assert self.validate(legacy)["ok"]
        result = self.fixture("review-inputs")
        assert not result["ok"] and result["code"] == 1 and not result["retained"] and result["inputs"] == [], result

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
        self.check_input_review(source, expected)
        assert inventory() == before  # no source Home, queue, model or catalog writes

    def check_input_review(self, source, expected):
        document = copy.deepcopy(source)
        kinds = self.api("GET", DECODE + "kinds")[1]["data"]
        events = [json.loads(row) for row in expected["ui-events.jsonl"].splitlines()]
        start = next(row for row in events if row["kind"] == kinds["start"] and row["agent_depth"] == 0)
        start["queue_item_id"] = "d" * 32
        set_ui(document, events)
        profile = {"model_id": "backup-vision-fixture", "reasoning_effort": "medium",
                   "permission_profile": "read-only"}
        image_id = "9" * 32
        image = base64.b64decode("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+ip1sAAAAASUVORK5CYII=")
        replace(document, f"attachments/{image_id}.bin", image)
        replace(document, f"attachments/{image_id}.json", dump({"schema_version": 2, "id": image_id,
                    "mime_type": "image/png", "size": len(image), "created_at": 1, "file_name": "review 中文.png"}))

        def item(key, state, **fields):
            return {"id": key * 32, "text": f"Input {key} 中文", "state": state,
                    "attachments": [image_id], "priority": False, "profile": profile, **fields}

        queue = {"schema_version": 7, "items": [item("a", "sending", run_id="run-completed"),
                 item("b", "staged"), item("c", "sending"), item("d", "sending", text=start["text"]),
                 item("e", "pending"), item("f", "sending", run_id="run-without-receipt")],
                 "discard_images": [image_id]}

        def submission(row, state):
            return {"id": row["id"], "text": row["text"], "attachments": row["attachments"],
                    "interrupt": row["priority"], "state": state, "profile": row["profile"]}

        draft = {"schema_version": 7, "revision": 57, "text": "Keep composer 草稿",
                 "attachments": [image_id], "composer_profile": profile, "run_admission_uncertain": True,
                 "submissions": [submission(row, "posting") for row in queue["items"][:3]] +
                    [submission(item("7", "staged"), "prepared"), submission(item("8", "sending"), "posting")]}
        # The actual browser trims queue text while retaining the original intent.
        draft["submissions"][1]["text"] = "\ufeff \t" + queue["items"][1]["text"] + "\u00a0\u3000"
        replace(document, "queue.json", dump(queue)); replace(document, "draft.json", dump(draft))
        receipts = [{"schema_version": 1, "id": "a" * 32, "run_id": "run-completed"},
                    {"schema_version": 2, "id": "c" * 32, "state": "starting"},
                    {"schema_version": 3, "id": "d" * 32, "state": "starting", "run_id": "run-retained",
                     "agent_run_id": start["run_id"]},
                    {"schema_version": 3, "id": "8" * 32, "state": "starting", "run_id": "run-unconfirmed", "agent_run_id": 77}]
        for row in receipts:
            replace(document, f'queue-receipts/{row["id"]}.json', dump(row))
        original = {row["path"]: base64.b64decode(row["data"]) for row in document["files"]}
        result = self.prepare(document, "review-inputs")
        assert result["ok"] and result["verified"] and result["original_unchanged"], result
        for key, value in dict(accepted_queue=2, accepted_draft=1, duplicate_draft=2,
                               queue_review=4, draft_review=2, discard_images=1, direct_uncertain=True).items():
            assert result[key] == value, result
        mapped = {row["source_id"]: row for row in result["inputs"]}
        assert len(mapped) == 8
        fresh = [row["review_id"] for row in mapped.values() if row["review_id"]]
        assert len(set(fresh)) == 6 and not set(fresh).intersection(mapped)
        for identifier in ("a", "d"):
            assert mapped[identifier * 32]["disposition"] == 1 and not mapped[identifier * 32]["review_id"]
            assert not mapped[identifier * 32]["uncertain"]
        for identifier in ("b", "c", "f", "8"):
            assert mapped[identifier * 32]["uncertain"]
        assert not mapped["e" * 32]["uncertain"] and not mapped["7" * 32]["uncertain"]
        stage = self.parent / result["directory"]
        actual = {p.relative_to(stage).as_posix(): p.read_bytes() for p in stage.rglob("*") if p.is_file()}
        assert result["provenance_entries"] == 1
        provenance = json.loads(actual["restore-inputs.json"])
        assert provenance["schema_version"] == 1 and len(provenance["imports"]) == 1
        entry = provenance["imports"][0]
        assert entry["inputs"] == result["inputs"]
        assert entry["captured_at_us"] == document["captured_at_us"]
        assert entry["direct_run_admission_uncertain"] and entry["cleared_discard_images"] == 1
        for archived in entry["source_files"]:
            raw = archived["data"].encode()
            assert raw == original[archived["path"]]
            assert archived["bytes"] == len(raw) and archived["sha256"] == hashlib.sha256(raw).hexdigest()
        for name, raw in original.items():
            if name not in ("queue.json", "draft.json"):
                assert actual[name] == raw, name
        reviewed_queue, reviewed_draft = json.loads(actual["queue.json"]), json.loads(actual["draft.json"])
        assert reviewed_queue["schema_version"] == 7 and reviewed_queue["discard_images"] == []
        assert [row["id"] for row in reviewed_queue["items"]] == [mapped[k * 32]["review_id"] for k in "bcef"]
        assert all(row["state"] == "staged" and "run_id" not in row for row in reviewed_queue["items"])
        assert reviewed_draft["revision"] == 1 and not reviewed_draft["run_admission_uncertain"]
        for name in ("text", "attachments", "composer_profile"):
            assert reviewed_draft[name] == draft[name]
        assert [row["id"] for row in reviewed_draft["submissions"]] == [mapped[k * 32]["review_id"] for k in "78"]
        for source_row, restored in zip(draft["submissions"][3:], reviewed_draft["submissions"]):
            assert restored == {**source_row, "id": restored["id"], "state": "rejected"}
        helper = ROOT / "tests/fixtures/backup-review-ui.mjs"
        assert shutil.which("node"), "Node.js is required for live frontend controller proof"
        subprocess.run(["node", str(helper), str(stage)], cwd=ROOT, check=True, timeout=20)
        status, encoded = self.api("GET", STAGE + "export")
        assert status == 200 and encoded["data"]  # native encode/decode retains every exact byte after releasing JSON
        exported = copy.deepcopy(document)
        exported["files"] = [file_entry(name, raw) for name, raw in actual.items()]
        recount(exported)
        assert self.validate(dump(exported))["ok"]
        assert self.fixture("check")["verified"] and self.fixture("discard")["ok"]
        self.check_input_provenance(exported, provenance, original)

        for mode, code in (("review-inputs-cancel", 9), ("review-inputs-mid-cancel", 9),
                           ("review-inputs-deadline", 11), ("review-inputs-collision", 11),
                           ("review-inputs-origin-cancel", 9), ("review-inputs-origin-files", 11)):
            rejected = self.prepare(document, mode)
            assert not rejected["ok"] and not rejected["retained"] and rejected["code"] == code, rejected
            assert rejected["original_unchanged"] and rejected["inputs"] == [] and not list(self.parent.iterdir())
            assert rejected["provenance_entries"] == 0
        assert self.prepare(document, "review-inputs-null-error")["verified"] and self.fixture("discard")["ok"]
        conflicting = copy.deepcopy(document)
        replace(conflicting, "draft.json", dump({**draft, "submissions": [{**draft["submissions"][1], "text": "Different text"}]}))
        rejected = self.prepare(conflicting, "review-inputs")
        assert not rejected["ok"] and rejected["original_unchanged"] and rejected["inputs"] == []
        assert not list(self.parent.iterdir())

        legacy = copy.deepcopy(source)
        replace(legacy, "queue.json", dump({"schema_version": 1, "items": [{"id": "b" * 32, "text": "Legacy", "state": "pending"}]}))
        replace(legacy, "draft.json", dump({"schema_version": 1, "revision": 1, "text": "Legacy 草稿"}))
        rejected = self.prepare(legacy, "review-inputs-growth")
        assert not rejected["ok"] and rejected["code"] == 11 and rejected["original_unchanged"] and rejected["inputs"] == []
        assert not list(self.parent.iterdir())
        assert self.prepare(legacy, "review-inputs")["verified"] and self.fixture("discard")["ok"]

        # Ordinary fixed API capacity: keep all 20 queue + 20 draft inputs.
        capacity = copy.deepcopy(source)
        queue_rows = [{"id": f"{i:032x}", "text": f"Queue {i}", "state": "pending", "attachments": [], "priority": False}
                      for i in range(1, 21)]
        draft_rows = [{"id": f"{i:032x}", "text": f"Draft {i}", "state": "prepared", "attachments": [], "interrupt": False}
                      for i in range(21, 41)]
        replace(capacity, "queue.json", dump({"schema_version": 7, "items": queue_rows, "discard_images": []}))
        replace(capacity, "draft.json", dump({"schema_version": 7, "revision": 1, "text": "", "attachments": [],
                                             "run_admission_uncertain": False, "submissions": draft_rows}))
        result = self.prepare(capacity, "review-inputs")
        assert result["verified"] and len(result["inputs"]) == 40 and result["queue_review"] == result["draft_review"] == 20, result
        assert len({row["review_id"] for row in result["inputs"]}) == 40
        assert self.fixture("discard")["ok"]

        no_inputs = copy.deepcopy(source)
        no_inputs["files"] = [row for row in no_inputs["files"] if row["path"] not in ("draft.json", "queue.json")]
        recount(no_inputs)
        result = self.prepare(no_inputs, "review-inputs")
        assert result["verified"] and result["inputs"] == []
        assert not (self.parent / result["directory"] / "queue.json").exists()
        assert result["provenance_entries"] == 0 and not (self.parent / result["directory"] / "restore-inputs.json").exists()
        assert self.fixture("discard")["ok"]

    def check_input_provenance(self, exported, provenance, original):
        result = self.prepare(exported, "review-inputs")
        assert result["verified"] and result["provenance_entries"] == 2 and result["original_unchanged"], result
        path = self.parent / result["directory"] / "restore-inputs.json"
        imports = json.loads(path.read_bytes())["imports"]
        assert imports[0] == provenance["imports"][0]
        assert imports[1]["source_files"][0]["data"].encode() == original["meta.json"]
        for earlier in provenance["imports"][0]["inputs"]:
            if earlier["review_id"] and earlier["uncertain"]:
                inherited = next(row for row in result["inputs"] if row["source_id"] == earlier["review_id"])
                assert inherited["uncertain"], inherited
        assert self.fixture("discard")["ok"]

        def invalid(name, mutate):
            document = copy.deepcopy(exported)
            changed = copy.deepcopy(provenance)
            mutate(changed)
            replace(document, "restore-inputs.json", dump(changed))  # recompute outer SHA; inner validation must still refuse
            value = self.validate(dump(document))
            assert not value["ok"], (name, value)
            assert not list(self.parent.iterdir())

        invalid("unknown field", lambda p: p.update(extra=True))
        invalid("unknown schema", lambda p: p.update(schema_version=2))
        invalid("empty history", lambda p: p.update(imports=[]))
        invalid("length", lambda p: p["imports"][0]["source_files"][0].update(bytes=0))
        invalid("inner SHA-256", lambda p: p["imports"][0]["source_files"][0].update(sha256="0" * 64))
        invalid("source bytes hash", lambda p: p["imports"][0]["source_files"][0].update(data="{}"))
        invalid("duplicate file", lambda p: p["imports"][0]["source_files"].__setitem__(1, p["imports"][0]["source_files"][0]))
        invalid("path", lambda p: p["imports"][0]["source_files"][0].update(path="../meta.json"))
        invalid("direct admission", lambda p: p["imports"][0].update(direct_run_admission_uncertain=False))
        invalid("discard count", lambda p: p["imports"][0].update(cleared_discard_images=0))
        invalid("missing map", lambda p: p["imports"][0]["inputs"].pop())
        invalid("wrong source", lambda p: p["imports"][0]["inputs"][0].update(source_id="0" * 32))
        invalid("accepted with fresh ID", lambda p: p["imports"][0]["inputs"][0].update(review_id="1" * 32))
        invalid("unknown disposition", lambda p: p["imports"][0]["inputs"][1].update(disposition=99))
        invalid("posting uncertainty lost", lambda p: p["imports"][0]["inputs"][1].update(uncertain=False))
        invalid("fresh ID reuses source", lambda p: p["imports"][0]["inputs"][1].update(review_id="b" * 32))
        invalid("duplicate fresh ID", lambda p: p["imports"][0]["inputs"][2].update(review_id=p["imports"][0]["inputs"][1]["review_id"]))

        def bad_codec(p):
            file = next(f for f in p["imports"][0]["source_files"] if f["path"] == "queue.json")
            raw = dump({"schema_version": 99, "items": []})
            file.update(data=raw.decode(), bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest())
        invalid("rehash cannot bypass live codec", bad_codec)
        # Descriptive past acceptance cannot consume/promote a current staged ID.
        passive = copy.deepcopy(exported)
        altered = copy.deepcopy(provenance)
        altered["imports"][0]["inputs"][2].update(disposition=1, review_id="", uncertain=False)
        replace(passive, "restore-inputs.json", dump(altered))
        result = self.prepare(passive, "review-inputs")
        assert result["verified"] and result["accepted_queue"] == result["accepted_draft"] == 0, result
        assert result["queue_review"] == 4 and result["draft_review"] == 2
        assert self.fixture("discard")["ok"]
        full = copy.deepcopy(exported)
        history = {"schema_version": 1, "imports": provenance["imports"] * 16}
        replace(full, "restore-inputs.json", dump(history))
        result = self.prepare(full, "review-inputs")
        assert not result["ok"] and result["code"] == 11 and result["original_unchanged"] and result["inputs"] == []
        assert result["provenance_entries"] == 0 and not list(self.parent.iterdir())
        invalid("too many entries", lambda p: p.update(imports=p["imports"] * 17))

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
