"""Bounded owning backup decoder probe over sealed HTTP/TLS upload bytes.

Real idle-session meta/snapshot/draft/queue and a normal 2 MiB artifact round
trip without Home writes. A small malformed-document corpus exercises parser,
envelope, codec/hash, path/identity/reference and budget failures. No restore,
model/shell/queue execution, pressure test or production preview route.
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


class Probe(UploadProbe):
    def __init__(self, host, base, secure):
        super().__init__(host, base, secure)
        shutil.copy2(ROOT / "tests/fixtures/backup-decode.c", self.site / "src/bootstrap/backup-decode.c")
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
        return value["data"]

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
        def inventory():
            return {str(p.relative_to(self.home)): p.read_bytes() for p in self.home.rglob("*")
                    if p.is_file() and p != self.home / ".mdo.lock"}
        before = inventory()
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
        for mode, code in (("files", 11), ("file", 11), ("total", 11), ("document", 11),
                           ("deadline", 11), ("cancel", 9), ("budget", 1)):
            value = self.validate(raw_small, mode)
            assert not value["ok"] and value["code"] == code and value["files"] == [], (mode, value)
        cases = []
        def altered(label, change):
            doc = copy.deepcopy(small)
            change(doc)
            cases.append((label, dump(doc)))
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
        samples = (("draft.json", draft), ("queue.json", queue), (receipt_path, receipt),
                   ("feedback.json", feedback), ("attachments/runs/7.json", binding),
                   ("attachments/events/2.json", binding))
        for path, sample in samples:
            valid = copy.deepcopy(small)
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
            valid = copy.deepcopy(small)
            replace(valid, "draft.json", dump(legacy_draft))
            replace(valid, "queue.json", dump(old_queue))
            assert self.validate(dump(valid))["ok"], schema
        for historical in ({"schema_version": 1, "id": "c" * 32, "run_id": "run-completed"},
                           {"schema_version": 2, "id": "c" * 32, "state": "starting"}):
            valid = copy.deepcopy(small)
            replace(valid, receipt_path, dump(historical))
            assert self.validate(dump(valid))["ok"], historical
        # Declared maxima are ordinary bounded format cases, not load tests.
        # Prior node caps rejected the 512th feedback or a combined full queue.
        maximum = copy.deepcopy(small)
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
        bad_legacy = legacy.replace(b'"export_schema":1', b'"export_schema":1,"files":[]')
        assert not self.validate(bad_legacy)["ok"]
        assert not self.validate(legacy.replace(b'"version":3', b'"version":3,"version":3', 1))["ok"]
        assert self.validate(raw_small)["ok"]  # errors never poison later input
        self.api("GET", DECODE + "release")
        assert before == inventory()
        print(f"backup decode {'TLS' if self.secure else 'HTTP'}: owned v2/v1, exact bytes, "
              f"{len(cases)} malformed inputs, budgets/cancel, retries and zero Home writes PASS", flush=True)


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
