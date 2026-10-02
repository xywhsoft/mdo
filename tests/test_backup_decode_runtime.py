"""Bounded owning backup decoder probe over sealed HTTP/TLS upload bytes.

Real idle-session meta/snapshot/draft/queue and a normal 2 MiB artifact round
trip without Home writes. A small malformed-document corpus exercises parser,
envelope, codec/hash, path/identity/reference and budget failures. A separate
unbound model replay gate checks ownership and semantic failures. A test-owned
source session uses an in-process model to prove real writer compatibility;
inspection never executes models, shell or queues. No product restore, pressure
test or production preview route.
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
import zlib

from test_backup_upload_runtime import CHUNK, CONTROL, PREFIX, ROOT, Probe as UploadProbe

DECODE = "/__fixture/backup-decode/"
ARTIFACT = "artifacts/run-00000000000000000001/00000000000000000001-decode.txt"


def dump(value):
    return json.dumps(value, ensure_ascii=False, separators=(",", ":")).encode()


def file_entry(path, data):
    return {"path": path, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(),
            "encoding": "base64", "data": base64.b64encode(data).decode()}


def recount(document):
    document["file_count"] = len(document["files"])
    document["total_bytes"] = sum(f["bytes"] for f in document["files"])
    present = {f["path"] for f in document["files"]}
    document["absent_files"] = [p for p in ("journal.jsonl", "ui-events.jsonl", "todo.json",
                                           "draft.json", "queue.json", "feedback.json") if p not in present]
    return document


def replace(document, path, data):
    document["files"] = [f for f in document["files"] if f["path"] != path] + [file_entry(path, data)]
    return recount(document)


def set_ui(document, events):
    replace(document, "ui-events.jsonl", b"".join(dump(event) + b"\n" for event in events))
    document.update(ui_first_event_id=events[0]["event_id"] if events else 0,
                    ui_last_event_id=events[-1]["event_id"] if events else 0,
                    ui_records=len(events))
    return document


class Probe(UploadProbe):
    def __init__(self, host, base, secure):
        super().__init__(host, base, secure)
        # Keep Ling unchanged. The test-owned model advertises image input for
        # a deterministic callback; no endpoint is invoked by the source run.
        defaults_path = self.site / "default-home/config/defaults.json"
        defaults = json.loads(defaults_path.read_text(encoding="utf-8"))
        vision = copy.deepcopy(defaults["models"]["items"][0])
        vision.update(id="backup-vision-fixture", name="Backup vision fixture", builtin=False,
                      free=False, editable=True, removable=True)
        vision["capabilities"].append("media-input"); vision["attachments"] = ["image"]
        defaults["models"]["items"].append(vision)
        defaults_path.write_text(json.dumps(defaults), encoding="utf-8")
        shutil.copy2(ROOT / "tests/fixtures/backup-decode.c", self.site / "src/bootstrap/backup-decode.c")
        relations = self.site / "src/sessions/backup_relations.c"
        source = relations.read_text(encoding="utf-8")
        hook = "    memcpy(Fact->QueueId, Event->QueueItemId, sizeof(Fact->QueueId));"
        assert source.count(hook) == 1
        source = source.replace(hook, hook + "\n    if ( g_BackupHistoryProbeCancel != NULL )\n"
                                "        (void)xrtCancelRequest(g_BackupHistoryProbeCancel);", 1)
        source = "static xcancel* g_BackupHistoryProbeCancel;\n" + source
        relations.write_text(source, encoding="utf-8", newline="\n")
        snapshot = self.site / "src/sessions/backup_model.c"
        source = snapshot.read_text(encoding="utf-8")
        hook = "    for ( i = 0u; i < Prefix; ) {"
        assert source.count(hook) == 1
        source = source.replace(hook, hook + "\n        if ( i != 0u && g_BackupSnapshotProbeCancel != NULL )\n"
                                "            (void)xrtCancelRequest(g_BackupSnapshotProbeCancel);", 1)
        hook = "        Offset += Framed + 1u;"
        assert source.count(hook) == 1
        source = source.replace(hook, hook + "\n        if ( g_BackupJournalProbeCancel != NULL )\n"
                                "            (void)xrtCancelRequest(g_BackupJournalProbeCancel);", 1)
        snapshot.write_text("static xcancel* g_BackupSnapshotProbeCancel;\n"
                            "static xcancel* g_BackupJournalProbeCancel;\n" + source,
                            encoding="utf-8", newline="\n")
        history = self.site / "src/sessions/backup_model_history.c"
        source = history.read_text(encoding="utf-8")
        hook = "    ++Index->Facts.MatchedUiRecords;"
        assert source.count(hook) == 1
        source = source.replace(hook, hook + "\n    if ( g_BackupModelHistoryProbeCancel != NULL )\n"
                                "        (void)xrtCancelRequest(g_BackupModelHistoryProbeCancel);", 1)
        history.write_text("static xcancel* g_BackupModelHistoryProbeCancel;\n" + source,
                           encoding="utf-8", newline="\n")
        images = self.site / "src/sessions/backup_images.c"
        source = images.read_text(encoding="utf-8")
        hook = "    MdoBackupImageCheck* Check = Context;"
        assert source.count(hook) == 1
        source = source.replace(hook, hook + "\n    if ( g_BackupImageProbeCancel != NULL && ++g_BackupImageProbeSteps == 2u )\n"
                                "        (void)xrtCancelRequest(g_BackupImageProbeCancel);", 1)
        images.write_text("static xcancel* g_BackupImageProbeCancel;\nstatic size_t g_BackupImageProbeSteps;\n" + source,
                          encoding="utf-8", newline="\n")
        service = self.site / "src/bootstrap/service.c"
        text = service.read_text(encoding="utf-8")
        text = text.replace('#include "backup-upload.c"', '#include "backup-upload.c"\n#include "backup-decode.c"', 1)
        text = text.replace("    BackupUploadFixtureUnit();", "    BackupDecodeFixtureUnit();\n    BackupUploadFixtureUnit();", 1)
        text = text.replace("    if (BackupUploadFixtureControl", "    if (BackupDecodeFixtureControl(pRequest)) return XS_OK;\n    if (BackupUploadFixtureControl", 1)
        service.write_text(text, encoding="utf-8", newline="\n")

    def validate(self, data, mode="decode"):
        self.control("release")
        upload_id = "e" * 32
        path = PREFIX + "/" + upload_id
        assert self.api("DELETE", path)[0] in (200, 404)
        assert self.api("POST", PREFIX, {"id": upload_id, "bytes": len(data)})[0] == 201
        for offset in range(0, len(data), CHUNK):
            assert self.api("PUT", path + f"/chunks/{offset}", data[offset:offset + CHUNK],
                            {"Content-Type": "application/octet-stream"})[0] == 200
        assert self.api("POST", path + "/seal")[0] == 200
        assert self.control("pin/" + upload_id)["pinned"]
        status, value = self.api("GET", DECODE + mode)
        assert status == 200, value
        self.control("release")
        assert self.api("DELETE", path)[0] == 200
        assert value["data"]["preview_size_safe"], value
        return value["data"]

    def replay(self, data, mode="replay"):
        decoded = self.validate(data)
        assert decoded["ok"], decoded
        # The upload pin and original bytes have been released by validate.
        status, response = self.api("GET", DECODE + mode)
        assert status == 200, response
        value = response["data"]
        if mode != "replay-release":
            retained = self.api("GET", DECODE + "state")[1]["data"]
            assert retained["ok"] and retained["files"] == decoded["files"], retained
        return value

    def images(self, document, mode="images"):
        decoded = self.validate(dump(document))
        assert decoded["ok"], decoded
        status, response = self.api("GET", DECODE + mode)
        assert status == 200, response
        value = response["data"]
        assert value["size_safe"] and not value["restore_ready"], value
        retained = self.api("GET", DECODE + "state")[1]["data"]
        assert retained["files"] == decoded["files"], retained
        if not value["ok"]:
            assert value["attachments"] == value["inline_images"] == value["unverified"] == value["rgba_bytes"] == value["peak_memory"] == 0
        return value

    def check(self):
        status, body = self.api("POST", "/api/v1/sessions", {
            "project_id": "default", "title": "Offline decode 便携", "agent_id": "mdo.default",
            "model_id": "ling-3.0-tiny", "protocol": "openai-responses", "reasoning_effort": "medium",
            "max_output_tokens": 1024,
        })
        assert status == 201, body
        session_id = body["data"]["id"]
        session_path = f"/api/v1/projects/default/sessions/{session_id}"
        assert self.api("PUT", session_path + "/draft", {"revision": 0, "text": "Saved draft 草稿"})[0] == 200
        assert self.api("POST", session_path + "/queue", {
            "id": "d" * 32, "text": "User confirmation required", "first": False, "stage": True})[0] == 201
        artifact = self.home / "sessions/default" / session_id / ARTIFACT
        artifact.parent.mkdir(parents=True)
        payload = bytes(range(256)) * 8192
        artifact.write_bytes(payload)
        assert self.api("GET", DECODE + "seed/" + session_id)[1]["data"] is True
        status, _, raw = self.call("GET", session_path + "/backup")
        assert status == 200 and len(raw) > CHUNK
        source = json.loads(raw)
        status, written = self.api("GET", DECODE + "journal")
        assert status == 200 and written["data"]["ok"], written
        library_journal = written["data"]["journal"].encode()
        library_records = [json.loads(line) for line in library_journal.splitlines()]
        assert {r["type"] for r in library_records} == {"begin_turn", "add_message", "ledger", "rewind", "clear"}
        assert [r["sequence"] for r in library_records] == list(range(1, len(library_records) + 1))
        status, body = self.api("POST", "/api/v1/sessions", {
            "project_id": "default", "title": "Actual writer compatibility", "agent_id": "mdo.default",
            "model_id": "backup-vision-fixture", "protocol": "openai-responses", "reasoning_effort": "medium",
            "max_output_tokens": 1024,
        })
        assert status == 201, body
        writer_id = body["data"]["id"]
        seeded = self.api("GET", DECODE + "seed-runtime/" + writer_id)[1]["data"]
        assert seeded["ok"] and seeded["calls"] == 3, seeded
        status, _, writer_raw = self.call("GET", f"/api/v1/projects/default/sessions/{writer_id}/backup")
        assert status == 200
        writer_source = json.loads(writer_raw)
        def inventory():
            return {str(p.relative_to(self.home)): p.read_bytes() for p in self.home.rglob("*")
                    if p.is_file() and p != self.home / ".mdo.lock"}
        before = inventory()
        expected_pixel = base64.b64decode("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+ip1sAAAAASUVORK5CYII=")
        writer_images = self.images(writer_source)
        assert writer_images["ok"] and writer_images["inline_images"] == 1 and writer_images["rgba_bytes"] == 4, writer_images
        assert writer_images["attachments"] == writer_images["unverified"] == 0
        for mode, code in [("images-deadline", 11), ("images-file", 11), ("images-cancel", 9), ("images-step-cancel", 9)]:
            value = self.images(writer_source, mode)
            assert not value["ok"] and value["code"] == code, (mode, value)
        assert self.images(writer_source, "images-null-error")["ok"]
        writer_model = self.replay(writer_raw, "replay-release")
        assert writer_model["ok"] and writer_model["released"] and writer_model["unbound"], writer_model
        part_message = next(m for m in writer_model["messages"] if m["part_count"] == 2)
        assert part_message["parts"][0]["text"] == "Writer question 中文", part_message
        assert part_message["parts"][1]["bytes"] == len(expected_pixel), part_message
        assert part_message["parts"][1]["sha256"] == hashlib.sha256(expected_pixel).hexdigest(), part_message
        assistants = [m for m in writer_model["messages"] if m["role"] == 2]
        assert len(assistants) == 3, assistants
        for index, message in enumerate(assistants):
            assert message["content"] == "" and message["part_count"] == 6, message
            assert [(p["kind"], p["text"]) for p in message["parts"]] == [
                (1, "fixture reasoning"), (5, "fixture-signature"), (0, "Writer answer "),
                (0, "one" if index == 0 else "two"), (1, ""), (5, "fixture-empty-signature")], message
            assert message["parts"][1]["native_type"] == message["parts"][5]["native_type"] == "thinking_signature", message
        writer_history = self.model_history(writer_source)
        assert writer_history["ok"] and writer_history["matched"] == 6, writer_history
        assert writer_history["unverified"] == writer_history["unprojected"] == 0, writer_history
        writer_events = [json.loads(line) for line in base64.b64decode(next(
            f for f in writer_source["files"] if f["path"] == "ui-events.jsonl")["data"]).splitlines()]
        # Retention may drop UI projections without deleting model entries.
        # That is a declared coverage gap, not contradictory model bytes.
        retained_writer = copy.deepcopy(writer_source)
        first_writer_turn = min(e["agent_turn"] for e in writer_events if e["agent_turn"] > 0)
        set_ui(retained_writer, [e for e in writer_events if e["agent_turn"] > first_writer_turn])
        gap = self.model_history(retained_writer)
        assert gap["ok"] and gap["matched"] == 4 and gap["unprojected"] == 2, gap
        value = self.validate(raw)
        assert value["ok"] and value["schema"] == 2 and value["encodable"] and not value["restore_ready"], value
        assert value["project_id"] == "default" and value["session_id"] == session_id
        expected = sorted(({k: f[k] for k in ("path", "bytes", "sha256")} for f in source["files"]),
                          key=lambda f: f["path"])
        assert value["files"] == expected
        # Source pin and upload are gone. Decoded content is independently owned.
        assert self.api("GET", DECODE + "state")[1]["data"]["files"] == expected
        self.api("GET", DECODE + "release")
        small = copy.deepcopy(source)
        # Keep the resource referenced by real UI events, with a small payload
        # for the malformed-document cases below.
        replace(small, ARTIFACT, b"fixture artifact\x00\xff\n")
        raw_small = dump(small)
        real_snapshot = base64.b64decode(next(f for f in small["files"]
                                             if f["path"] == "snapshot.json")["data"])
        snapshot_root = json.loads(real_snapshot)
        def snapshot_bytes(root):
            root = dict(root)
            root.pop("checksum", None)
            if root.get("version", 0) < 3:
                return dump(root)
            prefix = dump(root)[:-1]
            return prefix + b',"checksum":"' + f"{zlib.crc32(prefix):08x}".encode() + b'"}'
        def with_snapshot(root):
            document = copy.deepcopy(small)
            root = copy.deepcopy(root)
            if root.get("version", 0) < 4:
                for entry in root.get("entries", []):
                    entry.pop("parts", None); entry.pop("native", None)
            replace(document, "snapshot.json", snapshot_bytes(root))
            return document
        populated = copy.deepcopy(snapshot_root)
        populated.update(current_turn=1, next_sequence=3, entries=[
            {"sequence": 1, "turn": 1, "flags": 0, "role": 1, "content": "Question 中文",
             "reasoning": None, "tool_call_id": None, "tool_calls": [], "parts": [], "native": None},
            {"sequence": 2, "turn": 1, "flags": 0, "role": 2, "content": "Answer",
             "reasoning": "Retained reasoning", "tool_call_id": None, "tool_calls": [], "parts": [], "native": None}])
        image_part = {"kind": 2, "text": None, "native_type": None, "media_type": "image/png",
                      "source_url": None, "detail": None, "data": base64.b64encode(expected_pixel).decode(), "data_bytes": len(expected_pixel)}
        image_model = copy.deepcopy(populated)
        image_model["entries"][0].update(content=None, parts=[image_part])
        value = self.images(with_snapshot(image_model))
        assert value["ok"] and value["inline_images"] == 1 and value["rgba_bytes"] == 4, value
        image_model["entries"][0]["parts"][0].update(data=None, data_bytes=0, source_url="https://image.invalid/must-never-fetch.png")
        value = self.images(with_snapshot(image_model))
        assert value["ok"] and value["unverified"] == 1 and value["rgba_bytes"] == 0, value
        image_model["entries"][0]["parts"][0].update(data=base64.b64encode(expected_pixel[:-1]).decode(), data_bytes=len(expected_pixel) - 1)
        value = self.images(with_snapshot(image_model))
        assert not value["ok"] and value["code"] == 6, value
        image_fixtures = json.loads((ROOT / "tests/fixtures/image-codec.json").read_text())
        for name, item in image_fixtures.items():
            document = with_snapshot(snapshot_root)
            data = base64.b64decode(item["base64"]); image_id = "c" * 32
            replace(document, f"attachments/{image_id}.bin", data)
            replace(document, f"attachments/{image_id}.json", dump({"schema_version": 2, "id": image_id,
                "mime_type": item["mime"], "size": len(data), "created_at": 1, "file_name": name + " 中文"}))
            value = self.images(document)
            assert value["ok"] and value["attachments"] == 1 and value["rgba_bytes"] == 24, (name, value)
            replace(document, f"attachments/{image_id}.bin", data[:-1])
            replace(document, f"attachments/{image_id}.json", dump({"schema_version": 1, "id": image_id,
                "mime_type": item["mime"], "size": len(data) - 1, "created_at": 1}))
            value = self.images(document)
            assert not value["ok"] and value["code"] == 6 and image_id in value["error"], (name, value)
        assert self.validate(dump(with_snapshot(populated)))["ok"]
        part = {"kind": 0, "text": "Question 中文", "native_type": None, "media_type": None,
                "source_url": None, "detail": None, "data_bytes": 0, "data": None}
        multimodal = copy.deepcopy(populated)
        multimodal["entries"][0].update(content=None, parts=[part, {**part, "kind": 2, "text": None,
            "media_type": "image/png", "data_bytes": len(expected_pixel), "data": base64.b64encode(expected_pixel).decode()}])
        assert self.replay(dump(with_snapshot(multimodal)))["messages"][0]["parts"][1]["sha256"] == hashlib.sha256(expected_pixel).hexdigest()
        value = self.replay(dump(with_snapshot(populated)), "replay-release")
        assert value["ok"] and value["rendered"] and value["unbound"] and value["released"], value
        assert not value["restore_ready"] and value["last_sequence"] == 2 and value["turn"] == 1
        assert [(m["role"], m["content"]) for m in value["messages"]] == [(1, "Question 中文"), (2, "Answer")], value
        assert value["messages"][1]["reasoning"] == "Retained reasoning"
        assert not self.api("GET", DECODE + "state")[1]["data"]["ok"]
        real_ledger = with_snapshot(snapshot_root)
        replace(real_ledger, "journal.jsonl", library_journal)
        assert self.validate(dump(real_ledger))["ok"]
        value = self.replay(dump(real_ledger))
        assert value["ok"] and value["rendered"] and value["unbound"] and value["messages"] == [], value
        assert value["last_sequence"] == 0 and value["turn"] == 0
        assert value["read_files"] == value["modified_files"] == 0
        for mode, code in (("replay-deadline", 11), ("replay-cancel", 9), ("replay-file", 11), ("replay-files", 11)):
            value = self.replay(dump(with_snapshot(populated)), mode)
            assert not value["ok"] and not value["rendered"] and value["code"] == code, (mode, value)
        assert self.replay(dump(with_snapshot(populated)), "replay-null-error")["ok"]
        # Older formats omit the v3 checksum and sequence-array additions.
        for version in (1, 2, 3):
            older = copy.deepcopy(populated)
            older["version"] = version
            if version < 3:
                older["journal_sequence"] = older.pop("checkpoint_sequence")
                for key in ("read_file_sequences", "modified_file_sequences", "config"):
                    older.pop(key, None)
            assert self.validate(dump(with_snapshot(older)))["ok"], version
            value = self.replay(dump(with_snapshot(older)))
            assert value["ok"] and value["rendered"] and value["last_sequence"] == 2, (version, value)
        # Keep model arguments opaque: malformed input can legitimately remain
        # in history after a rejected tool call. An empty provider ID is allowed.
        opaque = copy.deepcopy(populated)
        opaque["entries"][1]["tool_calls"] = [{"id": "", "name": "read", "arguments": "{invalid"}]
        assert self.validate(dump(with_snapshot(opaque)))["ok"]
        value = self.replay(dump(with_snapshot(opaque)))
        assert not value["ok"] and value["code"] == 6 and not value["restore_ready"], value
        large_snapshot = copy.deepcopy(populated)
        large_snapshot["entries"][0]["content"] = "Retained text " * 8192
        assert self.validate(dump(with_snapshot(large_snapshot)), "snapshot-cancel")["code"] == 9
        for mode, code in (("files", 11), ("file", 11), ("total", 11), ("document", 11),
                           ("deadline", 11), ("cancel", 9), ("history-cancel", 9), ("budget", 1)):
            value = self.validate(raw_small, mode)
            assert not value["ok"] and value["code"] == code and value["files"] == [], (mode, value)
        cases = []
        def altered(label, change):
            doc = copy.deepcopy(small)
            change(doc)
            cases.append((label, dump(doc)))
        def bad_snapshot(label, change):
            root = copy.deepcopy(populated)
            change(root)
            cases.append((label, dump(with_snapshot(root))))
        for key, bad in (("format", "other"), ("version", 5), ("next_sequence", 0),
                         ("next_sequence", True), ("current_turn", -1), ("entries", {}),
                         ("summary", 7), ("fill_seen", 2), ("summary_generation", 2**32),
                         ("compacted_through", 3), ("tail_floor", 3), ("checkpoint_sequence", "1")):
            bad_snapshot(f"snapshot {key}={bad}", lambda root, key=key, bad=bad: root.update({key: bad}))
        bad_snapshot("snapshot unknown field", lambda root: root.update(secret_path="ignored"))
        bad_snapshot("snapshot unknown configuration", lambda root: root["config"].update(client="ignored"))
        bad_snapshot("snapshot invalid config type", lambda root: root.update(config=None))
        for key in ("parts", "native"):
            bad_snapshot("v4 missing " + key, lambda root, key=key: root["entries"][0].pop(key))
        for key, value in (("parts", None), ("native", 3)):
            bad_snapshot("v4 invalid " + key, lambda root, key=key, value=value: root["entries"][0].update({key: value}))
        for key, value in (("kind", 6), ("kind", True), ("text", "nul\x00text"), ("data_bytes", 1),
                           ("data", "AB=="), ("data_bytes", 2**64-1), ("media_type", False)):
            bad = copy.deepcopy(multimodal); bad["entries"][0]["parts"][0][key] = value
            cases.append(("v4 part " + key, dump(with_snapshot(bad))))
        for key in part:
            bad = copy.deepcopy(multimodal); bad["entries"][0]["parts"][0].pop(key)
            cases.append(("v4 part missing " + key, dump(with_snapshot(bad))))
        bad = copy.deepcopy(multimodal); bad["entries"][0]["content"] = "Competing fast path"
        cases.append(("v4 conflicting text path", dump(with_snapshot(bad))))
        for key, bad in (("max_output_tokens", 2**32), ("max_input_tokens", -1),
                         ("window_mode", 3), ("journal_durability", 2), ("prune_trigger", "0.75"),
                         ("compact_trigger", 0.1), ("summary_style", "unknown"),
                         ("context_window_tokens", 0), ("summary_min_tokens", 2**32-1)):
            bad_snapshot(f"snapshot config {key}", lambda root, key=key, bad=bad: root["config"].update({key: bad}))
        for key, bad in (("sequence", 0), ("sequence", 3), ("turn", 2), ("role", 4),
                         ("flags", 4), ("content", {}), ("reasoning", False),
                         ("tool_call_id", 1), ("tool_calls", None), ("content", "before\x00after")):
            bad_snapshot(f"snapshot entry {key}", lambda root, key=key, bad=bad: root["entries"][1].update({key: bad}))
        bad_snapshot("snapshot duplicate sequence", lambda root: root["entries"][1].update(sequence=1))
        bad_snapshot("snapshot reversed entries", lambda root: root["entries"].reverse())
        bad_snapshot("snapshot file list mismatch", lambda root: root.update(read_files=["source.c"], read_file_sequences=[]))
        bad_snapshot("snapshot future file sequence", lambda root: root.update(read_files=["source.c"], read_file_sequences=[3]))
        bad_snapshot("snapshot invalid file name", lambda root: root.update(read_files=[None], read_file_sequences=[0]))
        bad_snapshot("snapshot tool name", lambda root: root["entries"][1].update(tool_calls=[{"name": ""}]))
        bad_snapshot("snapshot tool args", lambda root: root["entries"][1].update(tool_calls=[{"name": "read", "arguments": {}}]))
        damaged = copy.deepcopy(small)
        # Mutate an existing checksum digit, independent of initial turn state.
        damaged_bytes = real_snapshot[:-10] + (b"0" if real_snapshot[-10:-9] != b"0" else b"1") + real_snapshot[-9:]
        assert damaged_bytes != real_snapshot
        replace(damaged, "snapshot.json", damaged_bytes)
        cases.append(("snapshot CRC mismatch", dump(damaged)))
        empty_snapshot = copy.deepcopy(small)
        replace(empty_snapshot, "snapshot.json", b"{}")
        cases.append(("snapshot has no format", dump(empty_snapshot)))

        def journal_bytes(record):
            record = dict(record)
            record.pop("checksum", None)
            if record.get("version", 0) < 3:
                return dump(record)
            prefix = dump(record)[:-1]
            return prefix + b',"checksum":"' + f"{zlib.crc32(prefix):08x}".encode() + b'"}'

        def with_journal(records, checkpoint=0, newline=b"\n"):
            root = copy.deepcopy(populated)
            root["checkpoint_sequence"] = checkpoint
            document = with_snapshot(root)
            replace(document, "journal.jsonl", b"".join(journal_bytes(r) + newline for r in records))
            return document

        def record(operation, sequence=1, version=4, **fields):
            result = {"format": "xllm-session-journal", "version": version}
            result.update({"sequence" if version >= 3 else "journal_sequence": sequence,
                           "type" if version >= 3 else "operation": operation})
            fields = copy.deepcopy(fields)
            if version < 4 and "entry" in fields:
                fields["entry"].pop("parts", None); fields["entry"].pop("native", None)
            return {**result, **fields}

        # A real semantic tool round is distinct from schema-only samples. An
        # unmatched result and a duplicate call must remain inspectable while
        # failing the replay gate. Neither path can run the recorded tool.
        call_entry = {"sequence": 3, "turn": 1, "flags": 0, "role": 2, "content": "Calling read",
                      "reasoning": None, "tool_call_id": None,
                      "tool_calls": [{"id": "call-1", "name": "read", "arguments": "{\"path\":\"source.c\"}"}], "parts": [], "native": None}
        result_entry = {"sequence": 4, "turn": 1, "flags": 0, "role": 3, "content": "Recorded tool output",
                        "reasoning": None, "tool_call_id": "call-1", "tool_calls": [], "parts": [], "native": None}
        round_records = [record("add_message", entry=call_entry), record("add_message", sequence=2, entry=result_entry),
                         record("ledger", sequence=3, kind="read", path="source.c", after_sequence=4)]
        value = self.replay(dump(with_journal(round_records)))
        assert value["ok"] and value["rendered"] and value["last_sequence"] == 4 and value["read_files"] == 1, value
        assert value["messages"][-2]["tool_calls"] == call_entry["tool_calls"]
        assert value["messages"][-1]["tool_call_id"] == "call-1" and value["messages"][-1]["content"] == "Recorded tool output"
        value = self.replay(dump(with_journal(round_records, newline=b"\r\n")))
        assert value["ok"] and value["last_sequence"] == 4, value
        for version in (1, 2, 3):
            old_round = [record("add_message", version=version, entry=call_entry),
                         record("add_message", sequence=2, version=version, entry=result_entry)]
            value = self.replay(dump(with_journal(old_round)))
            assert value["ok"] and value["rendered"] and value["last_sequence"] == 4, (version, value)
        value = self.replay(dump(with_journal([record("add_message", entry={**result_entry, "sequence": 3})])))
        assert not value["ok"] and value["code"] == 6, value
        value = self.replay(dump(with_journal([record("add_message", entry={**call_entry,
            "tool_calls": [call_entry["tool_calls"][0], call_entry["tool_calls"][0]]})])))
        assert not value["ok"] and value["code"] == 6, value
        value = self.replay(dump(with_journal([record("begin_turn", turn=9)])))
        assert not value["ok"] and value["code"] == 6, value
        value = self.replay(dump(with_journal([record("begin_turn", turn=2),
            record("add_message", sequence=2, entry={**populated["entries"][0], "sequence": 3, "turn": 2})])))
        assert value["ok"] and value["turn"] == 2 and value["last_sequence"] == 3, value
        checkpoint_records = [record("begin_turn", turn=1),
                              record("add_message", sequence=2, entry=populated["entries"][0]),
                              record("add_message", sequence=3, entry=populated["entries"][1]),
                              record("begin_turn", sequence=4, turn=2),
                              record("add_message", sequence=5,
                                     entry={**populated["entries"][0], "sequence": 3, "turn": 2})]
        value = self.replay(dump(with_journal(checkpoint_records, checkpoint=3)))
        assert value["ok"] and value["rendered"] and value["turn"] == 2 and value["last_sequence"] == 3, value
        assert [m["content"] for m in value["messages"]] == ["Question 中文", "Answer", "Question 中文"], value
        compact_summary = "\n".join("## " + heading + "\nPersisted summary" for heading in (
            "Objective", "Constraints", "Architecture and decisions", "Completed work",
            "Current repository state", "Verification evidence", "Open issues and risks", "Exact next actions",
            "Goal", "Constraints & Preferences", "Progress", "Key Decisions", "Next Steps", "Critical Context"))
        compact_record = record("compact", through_sequence=2, generation=1, compaction_count=1,
                                usage={"prompt_tokens": 10, "output_tokens": 2}, summary=compact_summary)
        value = self.replay(dump(with_journal([compact_record])))
        assert value["ok"] and any("Persisted summary" in m["content"] for m in value["messages"]), value
        value = self.replay(dump(with_journal([{**compact_record, "summary": "Missing required sections"}])))
        assert not value["ok"] and value["code"] == 6, value
        value = self.replay(dump(with_journal([record("truncate", from_sequence=1, to_sequence=2, reason="overflow_l2")])))
        assert value["ok"] and value["rendered"] and not any(m["content"] == "Answer" for m in value["messages"]), value

        journal_samples = [
            record("begin_turn", turn=2),
            record("add_message", entry={**populated["entries"][1], "sequence": 3}),
            record("compact", through_sequence=2, generation=1, compaction_count=1,
                   usage={"prompt_tokens": 10, "output_tokens": 2}, summary="Persisted summary"),
            record("ledger", kind="read", path="C:\\source\\中文.c", after_sequence=2),
            record("ledger", kind="modified", path="../source.c", after_sequence=0),
            record("truncate", from_sequence=1, to_sequence=2, reason="overflow_l2"),
            record("rewind", through_sequence=1, previous_next_sequence=3),
            record("clear", through_sequence=0, previous_next_sequence=3),
        ]
        # Individual schema samples are not a claim that every operation can
        # replay on the same context; real library replay remains a later gate.
        for sample in journal_samples:
            assert self.validate(dump(with_journal([sample])))["ok"], sample
        for version in (1, 2):
            legacy_record = record("begin_turn", version=version, turn=2)
            assert self.validate(dump(with_journal([legacy_record])))["ok"], version
            legacy_compact = record("compact", version=version, through_sequence=2,
                                    compaction_count=1, summary="Legacy summary")
            assert self.validate(dump(with_journal([legacy_compact])))["ok"], version
        legacy_v3 = record("compact", version=3, through_sequence=2, compaction_count=1, summary="Legacy v3 summary")
        assert self.validate(dump(with_journal([legacy_v3])))["ok"]
        assert self.validate(dump(with_journal([record("rewind", through_sequence=0,
                                                          previous_next_sequence=3)])))["ok"]
        covered = [record("begin_turn", sequence=2, turn=1), record("begin_turn", sequence=4, turn=2),
                   record("ledger", sequence=5, kind="read", path="source.c")]
        assert self.validate(dump(with_journal(covered, checkpoint=4)))["ok"]
        assert self.validate(dump(with_journal(covered, checkpoint=4, newline=b"\r\n")))["ok"]
        assert self.validate(dump(with_journal([], checkpoint=4)))["ok"]
        value = self.validate(dump(with_journal(covered, checkpoint=4)), "journal-cancel")
        assert not value["ok"] and value["code"] == 9 and value["files"] == [], value

        def bad_journal(label, change, operation="begin_turn"):
            sample = copy.deepcopy(next(r for r in journal_samples if r["type"] == operation))
            change(sample)
            cases.append((label, dump(with_journal([sample]))))

        for key, bad in (("format", "other"), ("version", 5), ("sequence", 0),
                         ("sequence", True), ("sequence", -1), ("sequence", "1"),
                         ("type", "execute"), ("type", "begin_turn\x00ignored"), ("turn", 0),
                         ("turn", False), ("turn", "2"), ("turn", -1), ("unknown", 1),
                         ("operation", "begin_turn"), ("journal_sequence", 1)):
            bad_journal(f"journal {key}={bad}", lambda r, k=key, v=bad: r.update({k: v}))
        for key in ("format", "version", "sequence", "type", "turn"):
            bad_journal("journal missing " + key, lambda r, k=key: r.pop(k))
        for key, bad in (("role", 4), ("sequence", 0), ("sequence", 2**64-1),
                         ("flags", 4), ("content", "nul\x00text"), ("tool_calls", {})):
            bad_journal("journal entry " + key,
                        lambda r, k=key, v=bad: r["entry"].update({k: v}), "add_message")
        for key, bad in (("through_sequence", 0), ("generation", 2**32),
                         ("compaction_count", 0), ("summary", ""), ("summary", None),
                         ("usage", None), ("usage", {"prompt_tokens": True}),
                         ("usage", {"other_tokens": 1})):
            bad_journal("journal compact " + key,
                        lambda r, k=key, v=bad: r.update({k: v}), "compact")
        for key, bad in (("kind", "write"), ("path", ""), ("path", "nul\x00path"),
                         ("path", {}), ("after_sequence", -1), ("after_sequence", 2**64-1)):
            bad_journal("journal ledger " + key,
                        lambda r, k=key, v=bad: r.update({k: v}), "ledger")
        for key, bad in (("from_sequence", 0), ("to_sequence", 1), ("reason", {})):
            bad_journal("journal truncate " + key,
                        lambda r, k=key, v=bad: r.update({k: v}), "truncate")
        for operation, key, bad in (("rewind", "through_sequence", 3),
                                    ("rewind", "previous_next_sequence", 1),
                                    ("clear", "through_sequence", 1),
                                    ("clear", "previous_next_sequence", 0)):
            bad_journal(f"journal {operation} {key}",
                        lambda r, k=key, v=bad: r.update({k: v}), operation)
        for label, records, checkpoint in (
                ("tail gap", [record("begin_turn", sequence=6, turn=2)], 4),
                ("duplicate covered", [covered[0], covered[0]], 4),
                ("reverse covered", [covered[1], covered[0]], 4),
                ("gap after first tail", [covered[2], record("begin_turn", sequence=7, turn=3)], 4)):
            cases.append(("journal " + label, dump(with_journal(records, checkpoint))))
        framed = journal_bytes(journal_samples[0])
        damaged_line = framed[:-10] + (b"0" if framed[-10:-9] != b"0" else b"1") + framed[-9:]
        for label, payload, checkpoint in (
                ("CRC mismatch", damaged_line + b"\n", 0),
                ("covered CRC mismatch", damaged_line + b"\n", 4),
                ("missing CRC", dump(journal_samples[0]) + b"\n", 0),
                ("CRC wrong trailer", framed + b" \n", 0),
                ("torn valid record", framed, 0), ("blank line", b"\n", 0),
                ("CR blank line", b"\r\n", 0), ("array line", b"[]\n", 0),
                ("duplicate key", b'{"turn":2,' + framed[1:] + b"\n", 0)):
            document = with_journal([], checkpoint)
            replace(document, "journal.jsonl", payload)
            cases.append(("journal " + label, dump(document)))

        for key, new in (("export_schema", 99), ("format", "other"), ("file_count", 99),
                         ("total_bytes", 1), ("captured_at_us", -1), ("captured_at_us", 1.5),
                         ("restore_ready", True), ("queue_restore_policy", "resume"),
                         ("revision", 99), ("source_workspace", "C:/different"),
                         ("project_id", "../escape"), ("session_id", "other"),
                         ("ui_records", 99), ("ui_last_event_id", 99), ("extra", "unknown")):
            altered(key, lambda doc, k=key, n=new: doc.update({k: n}))
        altered("missing envelope field", lambda doc: doc.pop("scope"))
        altered("missing snapshot", lambda doc: recount(doc.update(files=[f for f in doc["files"]
                                                                 if f["path"] != "snapshot.json"]) or doc))
        altered("lying absent", lambda doc: doc["absent_files"].append("draft.json"))
        altered("duplicate absent", lambda doc: doc["absent_files"].append(doc["absent_files"][0]))
        altered("unknown absent", lambda doc: doc["absent_files"].append("secrets.json"))
        altered("duplicate file", lambda doc: recount(doc["files"].append(copy.deepcopy(doc["files"][0])) or doc))
        altered("portable case alias", lambda doc: recount(doc["files"].append(
            file_entry(ARTIFACT.replace("-decode.txt", "-DECODE.txt"), b"aliased artifact")) or doc))
        for path in ("../meta.json", "/meta.json", "C:/meta.json", "attachments\\a.bin",
                     "meta.json\x00suffix", "META.JSON", "draft.json.tmp", "attachments/events/01.json"):
            altered("path " + path, lambda doc, p=path: doc["files"][0].update(path=p))
        for key, new in (("bytes", 0), ("bytes", 0.1), ("bytes", 2**64), ("sha256", "0" * 64),
                         ("sha256", "A" * 64), ("encoding", "hex"), ("data", "YQ"),
                         ("data", "YR=="), ("data", "YQ==\n"), ("data", "!!!!"), ("unknown", 1)):
            altered("file " + key, lambda doc, k=key, n=new: doc["files"][0].update({k: n}))
        altered("missing file data", lambda doc: doc["files"][0].pop("data"))
        altered("syntax", lambda doc: replace(doc, "draft.json", b"{"))
        altered("partial journal", lambda doc: replace(doc, "journal.jsonl", b"{}"))
        altered("missing image", lambda doc: replace(doc, "draft.json", dump({"attachments": ["a" * 32]})))
        altered("missing artifact", lambda doc: replace(doc, "ui-events.jsonl", dump({"event_id": 1,
                                "artifact_path": "old/" + ARTIFACT}) + b"\n"))
        raw_ui = base64.b64decode(next(f for f in small["files"] if f["path"] == "ui-events.jsonl")["data"])
        ui = [json.loads(line) for line in raw_ui.splitlines()]
        assert len(ui) == 2 and source["ui_records"] == 2
        for key, new in (("schema_version", 99), ("project_id", "other"), ("session_id", "other"),
                         ("kind", 999), ("success", "yes"), ("unknown", 1)):
            def change_ui(doc, k=key, n=new):
                lines = copy.deepcopy(ui)
                lines[0][k] = n
                replace(doc, "ui-events.jsonl", b"".join(dump(line) + b"\n" for line in lines))
            altered("UI " + key, change_ui)
        raw_todo = base64.b64decode(next(f for f in small["files"] if f["path"] == "todo.json")["data"])
        todo = json.loads(raw_todo)
        for key, new in (("schema_version", 2), ("event_id", -1), ("items", [{"text": "", "done": True}]),
                         ("items", [{"text": "item", "done": "yes"}]), ("unknown", 1)):
            altered("todo " + key, lambda doc, k=key, n=new: replace(doc, "todo.json", dump({**todo, k: n})))
        # Offline and live readers now share owned product codecs. Historical
        # session schemas remain readable; project/global draft formats do not
        # acquire session meaning merely because their JSON is valid.
        profile = {"model_id": "uninstalled-model", "reasoning_effort": "future-effort",
                   "permission_profile": "balanced"}
        draft = {"schema_version": 7, "revision": 1, "text": "Saved draft", "attachments": [],
                 "run_admission_uncertain": False, "submissions": []}
        item = {"id": "b" * 32, "text": "Pending input", "state": "pending",
                "attachments": [], "priority": False, "profile": profile}
        queue = {"schema_version": 7, "items": [item], "discard_images": []}
        receipt_path = "queue-receipts/" + "c" * 32 + ".json"
        receipt = {"schema_version": 3, "id": "c" * 32, "state": "starting",
                   "run_id": "run-prepared", "agent_run_id": 7}
        feedback = {"schema_version": 1, "items": [{"event_id": 2, "value": "good"}]}
        binding = {"schema_version": 1, "run_id": 7, "attachments": []}
        # Shape/maximum cases refer to the evicted prefix. Surviving IDs have
        # stronger type/evidence requirements, tested separately below.
        historical = copy.deepcopy(small)
        set_ui(historical, [{**event, "event_id": i + 1001} for i, event in enumerate(ui)])
        replace(historical, "todo.json", dump({**todo, "event_id": 1002}))
        samples = (("draft.json", draft), ("queue.json", queue), (receipt_path, receipt),
                   ("feedback.json", feedback), ("attachments/runs/7.json", binding),
                   ("attachments/events/2.json", binding))
        for path, sample in samples:
            valid = copy.deepcopy(historical)
            replace(valid, path, dump(sample))
            assert self.validate(dump(valid))["ok"], path
            for key in ("schema_version", "unknown"):
                altered(path + " " + key, lambda doc, p=path, s=sample, k=key:
                        replace(doc, p, dump({**s, k: 99})))
            encoded = dump(sample)
            altered(path + " duplicate key", lambda doc, p=path, data=encoded:
                    replace(doc, p, b'{"schema_version":1,' + data[1:]))
        for key, new in (("schema_version", 8), ("revision", 0), ("revision", 1.5),
                         ("text", "nul\x00text"), ("run_admission_uncertain", "no"),
                         ("submissions", [{"id": "a" * 32, "text": "", "attachments": [],
                                           "interrupt": False, "state": "prepared"}]),
                         ("composer_profile", {**profile, "model_id": ""}),
                         ("composer_profile", {**profile, "permission_profile": "arbitrary"})):
            altered("draft " + key, lambda doc, k=key, n=new:
                    replace(doc, "draft.json", dump({**draft, k: n})))
        for key, new in (("id", "A" * 32), ("text", ""), ("state", "running"),
                         ("priority", 1), ("profile", None), ("run_id", "run-invalid-state")):
            altered("queue item " + key, lambda doc, k=key, n=new:
                    replace(doc, "queue.json", dump({**queue, "items": [{**item, k: n}]})))
        altered("duplicate queue IDs", lambda doc: replace(doc, "queue.json",
                dump({**queue, "items": [item, item]})))
        altered("duplicate discard IDs", lambda doc: replace(doc, "queue.json",
                dump({**queue, "discard_images": ["d" * 32, "d" * 32]})))
        for key, new in (("id", "a" * 32), ("state", "started"), ("run_id", "wrong-prefix"),
                         ("agent_run_id", 0), ("agent_run_id", True)):
            altered("receipt " + key, lambda doc, k=key, n=new:
                    replace(doc, receipt_path, dump({**receipt, k: n})))
        for new in ([{"event_id": 0, "value": "good"}], [{"event_id": 2, "value": "like"}],
                    feedback["items"] * 2):
            altered("feedback item", lambda doc, n=new:
                    replace(doc, "feedback.json", dump({**feedback, "items": n})))
        altered("binding run filename mismatch", lambda doc: replace(doc, "attachments/runs/7.json",
                dump({**binding, "run_id": 8})))
        altered("binding duplicate images", lambda doc: replace(doc, "attachments/runs/7.json",
                dump({**binding, "attachments": ["a" * 32, "a" * 32]})))
        altered("binding event filename overflow", lambda doc:
                replace(doc, "attachments/events/18446744073709551616.json", dump(binding)))
        for schema in range(1, 8):
            legacy_draft = {"schema_version": schema, "revision": 1, "text": "Legacy draft"}
            if schema >= 2:
                legacy_draft["attachments"] = []
            if schema >= 3:
                legacy_draft["run_admission_uncertain"] = False
            if schema == 4:
                legacy_draft["submission"] = None
            elif schema >= 5:
                legacy_draft["submissions"] = []
            old_item = {k: item[k] for k in ("id", "text", "state")}
            if schema >= 2:
                old_item["attachments"] = []
            if schema >= 3:
                old_item["priority"] = False
            old_queue = {"schema_version": schema, "items": [old_item]}
            if schema >= 6:
                old_queue["discard_images"] = []
            valid = copy.deepcopy(historical)
            replace(valid, "draft.json", dump(legacy_draft))
            replace(valid, "queue.json", dump(old_queue))
            assert self.validate(dump(valid))["ok"], schema
        for historical_receipt in ({"schema_version": 1, "id": "c" * 32, "run_id": "run-completed"},
                           {"schema_version": 2, "id": "c" * 32, "state": "starting"}):
            valid = copy.deepcopy(historical)
            replace(valid, receipt_path, dump(historical_receipt))
            assert self.validate(dump(valid))["ok"], historical_receipt
        # Declared maxima are ordinary bounded format cases, not load tests.
        # Prior node caps rejected the 512th feedback or a combined full queue.
        maximum = copy.deepcopy(historical)
        replace(maximum, "feedback.json", dump({"schema_version": 1, "items": [
            {"event_id": i + 1, "value": "good"} for i in range(512)]}))
        images = [f"{i + 1000:032x}" for i in range(4)]
        for image_id in images:
            # Pair/schema fixture only. It deliberately does not claim actual
            # image decoding, which remains a required restoration gate.
            replace(maximum, f"attachments/{image_id}.bin", b"\x89PNG\r\n\x1a\n")
            replace(maximum, f"attachments/{image_id}.json", dump({"schema_version": 1,
                "id": image_id, "mime_type": "image/png", "size": 8, "created_at": 1}))
        replace(maximum, "queue.json", dump({**queue, "items": [
            {**item, "id": f"{i + 1:032x}", "attachments": images} for i in range(20)],
            "discard_images": [f"{i + 2000:032x}" for i in range(256)]}))
        submission = {"text": "Prepared input", "attachments": [], "interrupt": False,
                      "state": "prepared", "profile": profile}
        altered("duplicate submission IDs", lambda doc: replace(doc, "draft.json", dump({**draft,
            "submissions": [{**submission, "id": "a" * 32}, {**submission, "id": "a" * 32}]})))
        altered("last submission invalid", lambda doc: replace(doc, "draft.json", dump({**draft,
            "submissions": [{**submission, "id": "a" * 32},
                            {**submission, "id": "b" * 32, "profile": None}]})))
        replace(maximum, "draft.json", dump({**draft, "composer_profile": profile,
            "submissions": [{**submission, "id": f"{i + 1:032x}"} for i in range(20)]}))
        assert self.validate(dump(maximum))["ok"]
        altered("over feedback count", lambda doc: replace(doc, "feedback.json", dump({
            "schema_version": 1, "items": [{"event_id": i + 1, "value": "good"} for i in range(513)]})))
        altered("over queue count", lambda doc: replace(doc, "queue.json", dump({**queue,
            "items": [{**item, "id": f"{i + 1:032x}"} for i in range(21)]})))
        altered("over submissions count", lambda doc: replace(doc, "draft.json", dump({**draft,
            "submissions": [{**submission, "id": f"{i + 1:032x}"} for i in range(21)]})))
        kinds = self.api("GET", DECODE + "kinds")[1]["data"]
        queue_id = "c" * 32
        start = {**ui[0], "event_id": 1, "kind": kinds["start"], "run_id": 7,
                 "artifact_id": 0, "artifact_path": "", "tool_name": "", "text": "Prompt",
                 "queue_item_id": queue_id}
        model = {**start, "event_id": 2, "kind": kinds["model"], "text": "Answer", "queue_item_id": ""}
        plan = {**ui[1], "event_id": 3, "run_id": 7, "artifact_id": 0, "artifact_path": ""}
        related = copy.deepcopy(small)
        set_ui(related, [start, model, plan])
        replace(related, "todo.json", dump({**todo, "event_id": 3}))
        replace(related, "feedback.json", dump(feedback))
        replace(related, "attachments/events/1.json", dump(binding))
        replace(related, receipt_path, dump(receipt))
        value = self.validate(dump(related))
        assert value["ok"] and value["unverified_refs"] == 0 and value["removed_refs"] == 0, value
        def related_bad(label, change):
            doc = copy.deepcopy(related)
            change(doc)
            cases.append((label, dump(doc)))
        for event_id in (1, 4):
            related_bad("feedback wrong/missing target", lambda doc, n=event_id:
                        replace(doc, "feedback.json", dump({**feedback, "items": [{"event_id": n, "value": "good"}]})))
        related_bad("unsuccessful model feedback", lambda doc: set_ui(doc, [start, {**model, "success": False}, plan]))
        related_bad("binding wrong run", lambda doc: replace(doc, "attachments/events/1.json", dump({**binding, "run_id": 8})))
        related_bad("binding wrong target kind", lambda doc: replace(doc, "attachments/events/2.json", dump(binding)))
        related_bad("todo wrong event", lambda doc: replace(doc, "todo.json", dump({**todo, "event_id": 1})))
        related_bad("todo different items", lambda doc: replace(doc, "todo.json", dump({**todo, "event_id": 3,
                    "items": [{"text": "Altered projection", "done": False}]})))
        related_bad("nonempty reset todo", lambda doc: replace(doc, "todo.json", dump({**todo, "event_id": 0})))
        for key, new in (("tool_name", "other"), ("agent_depth", 1), ("text_truncated", True)):
            related_bad("todo projection " + key, lambda doc, k=key, n=new:
                        set_ui(doc, [start, model, {**plan, k: n}]))
        sending = {**item, "id": queue_id, "state": "sending", "run_id": "run-prepared"}
        related_bad("queue run conflicts with receipt", lambda doc: replace(doc, "queue.json", dump({**queue,
                    "items": [{**sending, "run_id": "run-other"}]})))
        related_bad("pending queue already receipted", lambda doc: replace(doc, "queue.json", dump({**queue,
                    "items": [{**item, "id": queue_id}]})))
        related_bad("receipt contradicts retained start", lambda doc: replace(doc, receipt_path,
                    dump({**receipt, "agent_run_id": 8})))
        related_bad("retained queue start missing receipt", lambda doc: recount(doc.update(
                    files=[f for f in doc["files"] if f["path"] != receipt_path]) or doc))
        related_bad("queue ID starts twice", lambda doc: set_ui(doc, [start, model, plan, {**start, "event_id": 4}]))
        marker = {**start, "event_id": 4, "kind": kinds["removed"], "source_event_id": 1,
                  "queue_item_id": "", "run_id": 0, "text": "History removed"}
        related_bad("marker contradicts surviving events", lambda doc: set_ui(doc, [start, model, plan, marker]))
        related_bad("marker inverted range", lambda doc: set_ui(doc, [{**marker, "source_event_id": 5}]))
        related_bad("event ID cannot advance", lambda doc: set_ui(doc, [start, model, {**plan, "event_id": 2**64 - 1}]))
        valid = copy.deepcopy(related)
        replace(valid, "queue.json", dump({**queue, "items": [sending]}))
        assert self.validate(dump(valid))["ok"]  # prepared + matching durable start
        replace(valid, receipt_path, dump({"schema_version": 1, "id": queue_id, "run_id": "run-prepared"}))
        assert self.validate(dump(valid))["ok"]
        replace(valid, receipt_path, dump({"schema_version": 2, "id": queue_id, "state": "starting"}))
        assert not self.validate(dump(valid))["ok"]  # a claim cannot prove an explicit accepted run
        sending.pop("run_id")
        replace(valid, "queue.json", dump({**queue, "items": [sending]}))
        value = self.validate(dump(valid))
        assert value["ok"] and value["unverified_refs"] == 1, value
        set_ui(valid, [{**start, "queue_item_id": ""}, model, plan])
        replace(valid, receipt_path, dump(receipt))
        value = self.validate(dump(valid))
        assert value["ok"] and value["unverified_refs"] == 1, value  # interrupted prepared receipt, no promotion
        replace(valid, "queue.json", dump({**queue, "items": [{**sending, "run_id": "run-prepared"}]}))
        assert not self.validate(dump(valid))["ok"]
        prefix = copy.deepcopy(related)
        set_ui(prefix, [{**start, "event_id": 101}, {**model, "event_id": 102}, {**plan, "event_id": 103}])
        replace(prefix, "todo.json", dump({**todo, "event_id": 103}))
        value = self.validate(dump(prefix))
        assert value["ok"] and value["unverified_refs"] == 2 and value["removed_refs"] == 0, value
        removed = copy.deepcopy(related)
        set_ui(removed, [marker])
        value = self.validate(dump(removed))
        assert value["ok"] and value["removed_refs"] == 3 and value["unverified_refs"] == 1, value
        set_ui(removed, [start, {**marker, "source_event_id": 2}])
        value = self.validate(dump(removed))
        assert value["ok"] and value["removed_refs"] == 2 and value["unverified_refs"] == 0, value
        # A zero-source legacy marker has special todo reset semantics, not an
        # invented image/feedback removal interval.
        legacy_clear = copy.deepcopy(historical)
        set_ui(legacy_clear, [{**marker, "event_id": 1003, "source_event_id": 0}])
        value = self.validate(dump(legacy_clear))
        assert value["ok"] and value["removed_refs"] == 1 and value["unverified_refs"] == 0, value
        reuse = copy.deepcopy(related)
        image_id = images[0]
        for file in maximum["files"]:
            if file["path"] in (f"attachments/{image_id}.bin", f"attachments/{image_id}.json"):
                replace(reuse, file["path"], base64.b64decode(file["data"]))
        replace(reuse, "attachments/runs/7.json", dump({**binding, "attachments": [image_id]}))
        value = self.validate(dump(reuse))
        assert value["ok"] and value["unverified_refs"] == 1, value  # current event binding overrides legacy run reuse
        gap = copy.deepcopy(related)
        set_ui(gap, [{**start, "event_id": 10}, {**model, "event_id": 12}, {**plan, "event_id": 13}])
        replace(gap, "todo.json", dump({**todo, "event_id": 13}))
        replace(gap, "feedback.json", dump({**feedback, "items": [{"event_id": 11, "value": "good"}]}))
        assert not self.validate(dump(gap))["ok"]
        altered("duplicate UI key", lambda doc: replace(doc, "ui-events.jsonl",
                raw_ui.replace(b'"schema_version":5', b'"schema_version":5,"schema_version":5', 1)))
        altered("duplicate todo key", lambda doc: replace(doc, "todo.json",
                raw_todo.replace(b'"schema_version":1', b'"schema_version":1,"schema_version":1', 1)))
        # Duplicates include escaped aliases; SAX supplies decoded member names.
        cases += [("root duplicate", b'{"export_schema":2,' + raw_small[1:]),
                  ("escaped root duplicate", b'{"export_sch\\u0065ma":2,' + raw_small[1:]),
                  ("file duplicate", raw_small.replace(b'"encoding":"base64"', b'"encoding":"base64","encoding":"base64"', 1)),
                  ("truncated", raw_small[:-1]), ("trailing JSON", raw_small + b"{}"),
                  ("array root", b"[]")]
        for label, data in cases:
            value = self.validate(data)
            assert not value["ok"] and value["code"] != 0 and value["files"] == [], (label, value)
        assert not self.validate(cases[0][1], "null-error")["ok"]
        # Arbitrary member/file order is safe. Unknown model identity is data,
        # not permission to invoke a model during offline inspection.
        reorder = copy.deepcopy(small)
        meta_entry = next(f for f in reorder["files"] if f["path"] == "meta.json")
        meta = json.loads(base64.b64decode(meta_entry["data"]))
        meta["model_id"] = "uninstalled-model"
        replace(reorder, "meta.json", dump(meta))
        reorder["files"] = [dict(reversed(list(f.items()))) for f in reversed(reorder["files"])]
        reorder = dict(reversed(list(reorder.items())))
        value = self.validate(dump(reorder))
        assert value["ok"] and value["model_id"] == "uninstalled-model", value
        # Legacy v1 is only the two raw embedded JSON objects. Keep exact raw
        # snapshot bytes (its checksum is over bytes, not reserialized JSON).
        meta_raw = base64.b64decode(next(f for f in small["files"] if f["path"] == "meta.json")["data"])
        snap_raw = base64.b64decode(next(f for f in small["files"] if f["path"] == "snapshot.json")["data"])
        legacy = b'{"snapshot":' + snap_raw + b',"exported_at_us":1,"meta":' + meta_raw + b',"export_schema":1}'
        value = self.validate(legacy)
        assert value["ok"] and value["schema"] == 1 and not value["encodable"] and not value["restore_ready"], value
        assert value["files"] == [f for f in expected if f["path"] in ("meta.json", "snapshot.json")]
        value = self.replay(legacy, "replay-release")
        assert value["ok"] and value["unbound"] and value["released"] and not value["restore_ready"], value
        value = self.replay(legacy, "model-history")
        assert value["ok"] and value["matched"] == value["unverified"] == 0, value
        self.check_model_history(with_snapshot, populated, ui[0], kinds)
        bad_legacy = legacy.replace(b'"export_schema":1', b'"export_schema":1,"files":[]')
        assert not self.validate(bad_legacy)["ok"]
        version_field = b'"version":' + str(snapshot_root["version"]).encode()
        assert not self.validate(legacy.replace(version_field, version_field + b',' + version_field, 1))["ok"]
        assert self.validate(raw_small)["ok"]  # errors never poison later input
        self.api("GET", DECODE + "release")
        assert before == inventory()
        print(f"backup decode {'TLS' if self.secure else 'HTTP'}: owned v2/v1, exact bytes, "
              f"{len(cases)} malformed inputs, separate unbound model replay, budgets/cancel, "
              "model/UI relations and real writers, retries and zero Home writes PASS", flush=True)

    def model_history(self, document, mode="model-history"):
        decoded = self.validate(dump(document))
        assert decoded["ok"], decoded
        status, response = self.api("GET", DECODE + mode)
        assert status == 200, response
        value = response["data"]
        assert value["size_safe"] and not value["restore_ready"], value
        retained = self.api("GET", DECODE + "state")[1]["data"]
        assert retained["ok"] and retained["files"] == decoded["files"], retained
        if not value["ok"]:
            assert value["matched"] == value["unverified"] == value["unprojected"] == 0, value
        return value

    def check_model_history(self, with_snapshot, populated, template, kinds):
        base = with_snapshot(populated)
        base["files"] = [f for f in base["files"] if f["path"] in ("meta.json", "snapshot.json")]
        recount(base)
        prompt = {**template, "event_id": 1, "run_id": 7, "kind": kinds["start"],
                  "agent_turn": 1, "agent_depth": 0, "user_message_sequence": 1,
                  "tool_name": "", "tool_call_id": "", "artifact_id": 0,
                  "artifact_path": "", "queue_item_id": "", "text": "Question 中文"}
        answer = {**prompt, "event_id": 2, "kind": kinds["model"], "success": True,
                  "user_message_sequence": 0, "text": "Answer"}
        set_ui(base, [prompt, answer])
        value = self.model_history(base)
        assert value["ok"] and value["matched"] == 2 and value["unverified"] == value["unprojected"] == 0, value
        # The display projection joins only TEXT parts, in order. Reasoning and
        # native signatures are retained model data, not visible answer text.
        def text_part(text, kind=0):
            return {"kind": kind, "text": text, "native_type": None, "media_type": None,
                    "source_url": None, "detail": None, "data_bytes": 0, "data": None}
        segmented = copy.deepcopy(populated)
        segmented["entries"][0].update(content=None, parts=[text_part("Question "), text_part("中文")])
        segmented["entries"][1].update(content=None, parts=[
            text_part("private reasoning", 1), text_part("An"), text_part(""),
            {**text_part("opaque-signature", 5), "native_type": "thinking_signature"}, text_part("swer")])
        parts_doc = with_snapshot(segmented)
        parts_doc["files"] = [f for f in parts_doc["files"] if f["path"] in ("meta.json", "snapshot.json")]
        recount(parts_doc); set_ui(parts_doc, [prompt, answer])
        value = self.model_history(parts_doc)
        assert value["ok"] and value["matched"] == 2 and value["unverified"] == value["unprojected"] == 0, value
        set_ui(parts_doc, [{**prompt, "text": "Question 中", "text_truncated": True},
                           {**answer, "text": "Answ", "text_truncated": True}])
        value = self.model_history(parts_doc)
        assert value["ok"] and value["matched"] == 2 and value["unverified"] == 2, value
        for records in ([prompt, {**answer, "text": "An"}],
                        [prompt, {**answer, "text": "Wrong", "text_truncated": True}],
                        [{**prompt, "text": "中文Question "}, answer],
                        [prompt, {**answer, "text": "private reasoningAnswer"}]):
            set_ui(parts_doc, records)
            value = self.model_history(parts_doc)
            assert not value["ok"] and value["code"] == 6, (records, value)
        for mode, code in (("model-history-deadline", 11), ("model-history-file", 11),
                           ("model-history-cancel", 9), ("model-history-step-cancel", 9)):
            value = self.model_history(base, mode)
            assert not value["ok"] and value["code"] == code, (mode, value)
        assert self.model_history(base, "model-history-null-error")["ok"]
        contradictions = [
            [{**prompt, "user_message_sequence": 2}, answer],
            [{**prompt, "user_message_sequence": 999}, answer],
            [{**prompt, "agent_turn": 2}, answer],
            [{**prompt, "text": "Changed question"}, answer],
            [prompt, {**answer, "text": "Changed answer"}],
            [prompt, {**answer, "agent_turn": 999}],
            [prompt, answer, {**prompt, "event_id": 3}],
            [prompt, answer, {**answer, "event_id": 3}],
        ]
        for records in contradictions:
            doc = copy.deepcopy(base); set_ui(doc, records)
            value = self.model_history(doc)
            assert not value["ok"] and value["code"] == 6, (records, value)
        assert not self.model_history(doc, "model-history-null-error")["ok"]
        doc = copy.deepcopy(base); set_ui(doc, [{**prompt, "user_message_sequence": 0}, answer])
        value = self.model_history(doc)
        assert value["ok"] and value["unverified"] == 1 and value["unprojected"] == 1, value
        doc = copy.deepcopy(base); set_ui(doc, [answer])
        value = self.model_history(doc)
        assert value["ok"] and value["matched"] == 1 and value["unprojected"] == 1, value
        doc = copy.deepcopy(base); set_ui(doc, [prompt, {**answer, "text": "Ans", "text_truncated": True}])
        value = self.model_history(doc)
        assert value["ok"] and value["matched"] == 2 and value["unverified"] == 1, value
        set_ui(doc, [prompt, {**answer, "text": "Wrong prefix", "text_truncated": True}])
        assert not self.model_history(doc)["ok"]
        doc = copy.deepcopy(base); set_ui(doc, [prompt, answer, {**answer, "event_id": 3,
                                                       "agent_depth": 1, "agent_turn": 999, "text": "Child context"}])
        assert self.model_history(doc)["matched"] == 2
        ambiguous = copy.deepcopy(populated)
        ambiguous["entries"].append({**ambiguous["entries"][1], "sequence": 3, "content": "Other assistant"})
        ambiguous["next_sequence"] = 4
        doc = with_snapshot(ambiguous); doc["files"] = copy.deepcopy(base["files"])
        snap = next(f for f in with_snapshot(ambiguous)["files"] if f["path"] == "snapshot.json")
        doc["files"] = [f for f in doc["files"] if f["path"] != "snapshot.json"] + [snap]; recount(doc)
        value = self.model_history(doc)
        assert value["ok"] and value["unverified"] == 1 and value["unprojected"] == 2, value

        tool_model = copy.deepcopy(populated)
        tool_model.update(current_turn=2, next_sequence=5)
        tool_model["entries"][1].update(content="Inspect", tool_calls=[
            {"id": "inspect", "name": "read", "arguments": '{"path":"file.c"}'}])
        tool_model["entries"] += [
            {"sequence": 3, "turn": 1, "flags": 0, "role": 3, "content": "status: success\nmodel framing",
             "reasoning": None, "tool_call_id": "inspect", "tool_calls": [], "parts": [], "native": None},
            {**populated["entries"][1], "sequence": 4, "turn": 2, "content": "Completed"}]
        def tool_document(root=tool_model):
            doc = with_snapshot(root)
            doc["files"] = [f for f in doc["files"] if f["path"] in ("meta.json", "snapshot.json")]
            return recount(doc)
        tool_start = {**answer, "event_id": 3, "kind": kinds["tool_start"], "tool_name": "read",
                      "tool_call_id": "inspect", "text": '{"path":"file.c"}'}
        tool_done = {**tool_start, "event_id": 4, "kind": kinds["tool_done"], "text": "inline display"}
        tool_records = [prompt, {**answer, "text": "Inspect"}, tool_start, tool_done,
                        {**answer, "event_id": 5, "agent_turn": 2, "text": "Completed"}]
        doc = tool_document(); set_ui(doc, tool_records)
        value = self.model_history(doc)
        assert value["ok"] and value["matched"] == 5 and value["unverified"] == value["unprojected"] == 0, value
        for key, bad in (("tool_call_id", "different"), ("tool_name", "write"),
                         ("agent_turn", 2), ("text", "{}")):
            doc = tool_document(); records = copy.deepcopy(tool_records); records[2][key] = bad; set_ui(doc, records)
            value = self.model_history(doc)
            assert not value["ok"] and value["code"] == 6, (key, value)
        pending = copy.deepcopy(tool_model); pending["entries"] = pending["entries"][:2]
        pending.update(current_turn=1, next_sequence=3)
        doc = tool_document(pending); set_ui(doc, tool_records[:4])
        value = self.model_history(doc)
        assert value["ok"] and value["unverified"] == 1, value
        doc = tool_document(); records = copy.deepcopy(tool_records); records[3]["kind"] = kinds["recovery"]
        set_ui(doc, records)
        assert self.model_history(doc)["unverified"] == 0

        # A normal old multimodal snapshot loses its TEXT/IMAGE parts. Positive
        # binding evidence reports that gap, while a retained conflicting text
        # cannot hide behind the same binding.
        image_id = "a" * 32
        image_model = copy.deepcopy(populated); image_model["version"] = 3
        image_model["entries"][0]["content"] = None
        doc = tool_document(image_model); set_ui(doc, [{**prompt, "text": "Prompt with image"}, answer])
        replace(doc, f"attachments/{image_id}.bin", b"\x89PNG\r\n\x1a\n")
        replace(doc, f"attachments/{image_id}.json", dump({"schema_version": 1, "id": image_id,
            "mime_type": "image/png", "size": 8, "created_at": 1}))
        replace(doc, "attachments/events/1.json", dump({"schema_version": 1, "run_id": 7, "attachments": [image_id]}))
        value = self.model_history(doc)
        assert value["ok"] and value["matched"] == 2 and value["unverified"] == 1, value
        original_snapshot = next(f for f in base["files"] if f["path"] == "snapshot.json")
        replace(doc, "snapshot.json", base64.b64decode(original_snapshot["data"]))
        assert not self.model_history(doc)["ok"]
        assert self.model_history(base)["unverified"] == 0  # failure/cancel never poison a retry


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, required=True)
    host = parser.parse_args().host.resolve()
    for secure in (False, True):
        with tempfile.TemporaryDirectory(prefix="backup-decode-", dir=ROOT / ".build") as raw:
            probe = Probe(host, Path(raw), secure)
            try:
                probe.start()
                probe.check()
            finally:
                probe.stop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
