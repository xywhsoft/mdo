"""Bounded production backup preview routes over HTTP and TLS.

One ordinary source bundle, small malformed variants and deterministic gate
pauses prove async ownership, cancellation, quota, cleanup and shutdown. The
preview never executes a model, tools or pending queues or changes Home.
"""
from __future__ import annotations

import argparse
import base64
import copy
import hashlib
import json
import shutil
import tempfile
import time
import zlib
from pathlib import Path

from test_backup_upload_runtime import CHUNK, PREFIX, ROOT, Probe as UploadProbe
from test_backup_decode_runtime import dump, replace

PREVIEWS = "/api/v1/session-backups/previews"
FIXTURE = "/__fixture/backup-preview/"
UPLOAD_ID = "a" * 32
PIXEL = base64.b64decode("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+ip1sAAAAASUVORK5CYII=")


class Probe(UploadProbe):
    def __init__(self, host, base, secure):
        super().__init__(host, base, secure)
        shutil.copy2(ROOT / "tests/fixtures/backup-preview.c", self.site / "src/bootstrap/backup-preview.c")
        service = self.site / "src/bootstrap/service.c"
        text = service.read_text(encoding="utf-8")
        for old, new in (
            ('#include "backup-upload.c"', '#include "backup-upload.c"\n#include "backup-preview.c"'),
            ("    bool ready = MdoBootstrapInit(pHost);", "    BackupPreviewFixtureInit();\n    bool ready = MdoBootstrapInit(pHost);"),
            ("    MdoApiUnit();", "    MdoApiUnit();\n    BackupPreviewFixtureUnit();"),
            ("    if (BackupUploadFixtureControl(pRequest))", "    if (BackupPreviewFixtureControl(pRequest)) return XS_OK;\n    if (BackupUploadFixtureControl(pRequest))"),
        ):
            assert text.count(old) == 1, old
            text = text.replace(old, new, 1)
        service.write_text(text, encoding="utf-8", newline="\n")
        preview = self.site / "src/api/backup_preview.c"
        text = preview.read_text(encoding="utf-8")
        for old, new in (
            ("    xrtMutexUnlock(Store->Lock);\n    if ( !Live", "    xrtMutexUnlock(Store->Lock);\n    BackupPreviewFixturePause(Completed, Cancel, Job->Limits.Deadline);\n    if ( !Live"),
            ("    Job->Info.ExpiresAt = Job->Limits.Deadline;", "    BackupPreviewFixtureBudget(&Job->Limits);\n    Job->Info.ExpiresAt = Job->Limits.Deadline;"),
            ("    Future = xrtTaskSubmit(Store->Pool", "    BackupPreviewFixtureBeforeSubmit(Store->Cancel, Store->Pool);\n    Future = xrtTaskSubmit(Store->Pool"),
            ("    MdoSessionBackupRelease(Document->Backup);", "    BackupPreviewFixtureRetirePause();\n    MdoSessionBackupRelease(Document->Backup);"),
        ):
            assert text.count(old) == 1, old
            text = text.replace(old, new, 1)
        text = ('void BackupPreviewFixturePause(unsigned, xcancel*, xdeadline);\n'
                'void BackupPreviewFixtureBudget(MdoSessionBackupLimits*);\n'
                'void BackupPreviewFixtureBeforeSubmit(xcancel*, xtaskpool*);\n'
                'void BackupPreviewFixtureRetirePause(void);\n'
                'bool BackupPreviewFixtureLock(xmutex*);\n') + text
        # Prototypes need their declared SDK/product types first.
        text = '#include "../../include/mdo/session_backup.h"\n' + text
        text = text.replace("xrtMutexLock(Store->Lock)", "BackupPreviewFixtureLock(Store->Lock)")
        preview.write_text(text, encoding="utf-8", newline="\n")

    def fixture(self, action):
        status, body = self.api("GET", FIXTURE + action)
        assert status == 200, body
        return body["data"]

    def wait(self, check, timeout=4):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            assert self.process.poll() is None, (self.base / "xs.log").read_text(errors="replace")
            result = check()
            if result:
                return result
            time.sleep(0.01)
        raise AssertionError("bounded preview condition did not complete")

    def upload(self, data):
        self.api("DELETE", PREFIX + "/" + UPLOAD_ID)
        status, body = self.api("POST", PREFIX, {"id": UPLOAD_ID, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()})
        assert status == 201, body
        for offset in range(0, len(data), CHUNK):
            assert self.api("PUT", PREFIX + "/" + UPLOAD_ID + f"/chunks/{offset}",
                            data[offset:offset + CHUNK], {"Content-Type": "application/octet-stream"})[0] == 200
        assert self.api("POST", PREFIX + "/" + UPLOAD_ID + "/seal")[0] == 200
        return PREFIX + "/" + UPLOAD_ID + "/preview"

    def begin(self, data):
        path = self.upload(data)
        status, body = self.api("POST", path)
        assert status == 202, body
        return path, body["data"], PREVIEWS + "/" + body["data"]["id"]

    def terminal(self, path):
        def poll():
            status, body = self.api("GET", path)
            assert status == 200, body
            return body["data"] if body["data"]["terminal"] else None
        return self.wait(poll)

    def check(self):
        assert self.api("GET", PREVIEWS)[1]["data"]["preview"] is None
        assert self.fixture("state")["retained_files"] == 0
        assert self.api("POST", PREFIX + "/" + UPLOAD_ID + "/preview", token=False)[0] == 428
        for path in (PREFIX + "/bad/preview", PREVIEWS + "/bad"):
            assert self.api("POST" if path.endswith("preview") else "GET", path)[0] == 400
        assert self.api("GET", PREVIEWS + "/" + UPLOAD_ID)[0] == 404
        assert self.api("POST", PREFIX + "/" + UPLOAD_ID + "/preview")[0] == 404
        assert self.api("POST", PREFIX, {"id": UPLOAD_ID, "bytes": 1})[0] == 201
        assert self.api("POST", PREFIX + "/" + UPLOAD_ID + "/preview")[1]["error"]["code"] == "backup_upload_incomplete"
        assert self.api("DELETE", PREFIX + "/" + UPLOAD_ID)[0] == 200
        status, body = self.api("POST", "/api/v1/sessions", {
            "project_id": "default", "title": "Offline preview 中文", "agent_id": "mdo.default",
            "model_id": "ornith-1.5-35b", "protocol": "openai-responses", "reasoning_effort": "medium",
            "max_output_tokens": 1024})
        assert status == 201, body
        session = body["data"]["id"]
        session_path = f"/api/v1/projects/default/sessions/{session}"
        assert self.api("PUT", session_path + "/draft", {"revision": 0, "text": "Preserved draft"})[0] == 200
        assert self.api("POST", session_path + "/queue", {"id": "b" * 32, "text": "Must not execute", "stage": True, "first": False})[0] == 201
        artifact_path = "artifacts/run-00000000000000000001/00000000000000000001-preview.txt"
        artifact = self.home / "sessions/default" / session / artifact_path
        artifact.parent.mkdir(parents=True)
        artifact.write_bytes(bytes(range(256)) * 8192)
        status, _, data = self.call("GET", session_path + "/backup")
        assert status == 200 and len(data) > CHUNK
        source = json.loads(data)
        image_id = "c" * 32
        replace(source, f"attachments/{image_id}.bin", PIXEL)
        replace(source, f"attachments/{image_id}.json", dump({"schema_version": 1, "id": image_id,
            "mime_type": "image/png", "size": len(PIXEL), "created_at": 1}))
        data = dump(source)
        def inventory():
            return {str(p.relative_to(self.home)): p.read_bytes() for p in self.home.rglob("*")
                    if p.is_file() and p != self.home / ".mdo.lock"}
        before = inventory()

        self.fixture("hold-0")
        start, accepted, preview = self.begin(data)
        self.wait(lambda: self.fixture("state")["entered"] == 1)
        value = self.api("GET", preview)[1]["data"]
        assert value["state"] == "running" and value["phase"] == "decode" and value["completed_steps"] == 0
        assert self.fixture("pin/" + accepted["id"])["access"] == 2  # not ready
        assert self.fixture("state")["upload_pins"] == 1
        status, same = self.api("POST", start)
        assert status == 200 and same["data"]["id"] == accepted["id"]
        assert self.api("POST", PREFIX + "/" + "d" * 32 + "/preview")[0] == 409
        assert self.api("POST", start, b"x")[0] == 400
        assert self.api("POST", start, [b"x"], chunked=True)[0] == 400
        assert self.api("DELETE", preview, token=False)[0] == 428
        for path, allow in ((start, "POST"), (PREVIEWS, "GET"), (preview, "DELETE")):
            assert allow in self.call("OPTIONS", path)[1]["allow"]
        status, headers, empty = self.call("HEAD", preview)
        assert status == 200 and not empty and int(headers["content-length"]) > 0
        self.fixture("resume")
        result = self.terminal(preview)
        facts = result["result"]
        assert result["state"] == "succeeded" and result["result_available"] and not result["restore_ready"]
        assert result["sha256"] == hashlib.sha256(data).hexdigest() and result["completed_steps"] == 3
        assert facts["session_id"] == session and facts["title"] == "Offline preview 中文"
        assert facts["file_count"] == len(source["files"]) and facts["total_bytes"] == source["total_bytes"]
        assert facts["image_attachments"] == 1 and facts["inline_images"] == 0 and facts["rgba_bytes"] == 4
        assert self.fixture("state")["upload_pins"] == 0
        assert self.fixture("state")["retained_files"] == len(source["files"])
        assert self.api("POST", start)[1]["data"]["id"] == result["id"]
        held = self.fixture("pin/" + result["id"])
        assert held["access"] == 0 and held["pins_held"] == 1
        assert held["pin_hash"] == hashlib.sha256(data).hexdigest()
        assert held["pin_files"] == len(source["files"]) and held["pin_bytes"] == source["total_bytes"]
        digest = hashlib.sha256()
        # The decoder exposes files in portable path order; the fixture added
        # its PNG after capture, so the input manifest itself is not sorted.
        for entry in sorted(source["files"], key=lambda entry: entry["path"]):
            digest.update(entry["path"].encode() + b"\0")
            digest.update(base64.b64decode(entry["data"]))
        assert held["pin_content_sha256"] == digest.hexdigest()
        assert held["pin_title"] == "Offline preview 中文"
        assert self.fixture("pin-second/" + result["id"])["pins_held"] == 2
        # Reusing an upload ID with DIFFERENT bytes must not reuse old success.
        assert self.upload(b"different") == start
        assert self.api("POST", start)[0] == 409
        assert self.api("GET", preview)[1]["data"]["sha256"] == hashlib.sha256(data).hexdigest()
        assert self.api("DELETE", preview)[1]["data"]["discarded"]
        assert not self.api("GET", preview)[1]["data"]["result_available"]
        pinned = self.fixture("state")
        assert pinned["pins_held"] == 2 and pinned["pin_content_sha256"] == held["pin_content_sha256"]
        assert self.api("POST", start)[1]["error"]["code"] == "backup_preview_busy"
        # Dropping one acquisition cannot invalidate the other. A deleted
        # preview cannot issue a new pin even though its immutable bytes live.
        pinned = self.fixture("pin/" + result["id"])
        assert pinned["access"] == 1 and pinned["pins_held"] == 1
        assert pinned["pin_content_sha256"] == held["pin_content_sha256"]
        assert self.api("POST", start)[0] == 409
        self.fixture("release-second")
        assert self.fixture("state")["retained_files"] == 0

        # Ownership remains valid after the upload has been removed and its
        # quota reused. Continue both semantic gates using only decoded bytes.
        self.fixture("hold-1")
        _, _, preview = self.begin(data)
        self.wait(lambda: self.fixture("state")["entered"] == 2)
        assert self.api("GET", preview)[1]["data"]["phase"] == "model_ui"
        assert self.fixture("state")["upload_pins"] == 0
        assert self.api("DELETE", PREFIX + "/" + UPLOAD_ID)[0] == 200
        assert self.api("POST", PREFIX, {"id": "d" * 32, "bytes": 1})[0] == 201
        self.fixture("resume")
        assert self.terminal(preview)["result"]["session_id"] == session
        assert self.api("DELETE", preview)[0] == 200
        assert self.api("DELETE", PREFIX + "/" + "d" * 32)[0] == 200

        # Cancel before decode, before pixels and after the last gate but before
        # publication. Terminal cancellation never retains a decoded payload.
        for step in (0, 2, 3):
            self.fixture(f"hold-{step}")
            start, _, preview = self.begin(data)
            self.wait(lambda: self.fixture("state")["entered"] == step + 1)
            assert self.api("GET", preview)[1]["data"]["completed_steps"] == step
            assert self.api("DELETE", PREFIX + "/" + UPLOAD_ID)[0] == 200
            if step == 0:
                assert self.api("POST", PREFIX, {"id": "d" * 32, "bytes": 1})[0] == 503
            value = self.api("DELETE", preview)[1]["data"]
            assert value["cancel_requested"] and value["discarded"]
            done = self.terminal(preview)
            assert done["state"] == "cancelled" and done["error_code"] == 9 and not done["result_available"]
            assert self.fixture("state")["upload_pins"] == self.fixture("state")["retained_files"] == 0
            self.fixture("resume")
        self.fixture("precancel")
        _, _, preview = self.begin(data)
        assert self.terminal(preview)["state"] == "cancelled"
        assert self.fixture("state")["upload_pins"] == 0
        self.fixture("refuse")
        start = self.upload(data)
        status, refused = self.api("POST", start)
        assert status == 503 and refused["error"]["code"] == "backup_preview_unavailable", refused
        failed = self.api("GET", PREVIEWS)[1]["data"]["preview"]
        assert failed["state"] == "failed" and failed["terminal"] and failed["error_code"] == 6
        assert self.fixture("state")["upload_pins"] == self.fixture("state")["retained_files"] == 0
        self.fixture("reset")
        _, _, preview = self.begin(data)
        assert self.terminal(preview)["state"] == "succeeded"
        assert self.api("DELETE", preview)[0] == 200
        self.fixture("short")
        _, _, preview = self.begin(data)
        failed = self.terminal(preview)
        assert failed["state"] == "failed" and failed["error_code"] == 11 and not failed["result_available"]

        _, _, preview = self.begin(b"not JSON")
        failed = self.terminal(preview)
        assert failed["state"] == "failed" and failed["error_code"] == 6
        assert failed["validation"] == "incomplete" and "result" not in failed
        assert self.fixture("state")["retained_files"] == 0
        # Schema-shaped messages are insufficient: the native library rejects
        # an empty provider call ID during semantic restore, before pixels.
        broken_model = copy.deepcopy(source)
        snapshot = json.loads(base64.b64decode(next(f for f in source["files"] if f["path"] == "snapshot.json")["data"]))
        snapshot.pop("checksum", None)
        snapshot.update(current_turn=1, next_sequence=2, entries=[{
            "sequence": 1, "turn": 1, "flags": 0, "role": 2, "content": "Inspect",
            "reasoning": None, "tool_call_id": None, "parts": [], "native": None,
            "tool_calls": [{"id": "", "name": "read", "arguments": "{}"}]}])
        prefix = dump(snapshot)[:-1]
        replace(broken_model, "snapshot.json", prefix + b',"checksum":"' + f"{zlib.crc32(prefix):08x}".encode() + b'"}')
        _, _, preview = self.begin(dump(broken_model))
        failed = self.terminal(preview)
        assert failed["state"] == "failed" and failed["error_code"] == 6 and failed["completed_steps"] == 1, failed
        assert "model context" in failed["message"] and "result" not in failed
        broken = copy.deepcopy(source)
        replace(broken, f"attachments/{image_id}.bin", PIXEL[:8])
        replace(broken, f"attachments/{image_id}.json", dump({"schema_version": 1, "id": image_id,
            "mime_type": "image/png", "size": 8, "created_at": 1}))
        _, _, preview = self.begin(dump(broken))
        failed = self.terminal(preview)
        assert failed["state"] == "failed" and failed["error_code"] == 6 and failed["completed_steps"] == 2, failed
        assert image_id in failed["message"] and "result" not in failed
        assert self.fixture("pin/" + failed["id"])["access"] == 2

        # Legacy v1 is explicitly partial; a successful semantic inspection is
        # never an assertion that missing UI/images/sidecars have been restored.
        status, _, legacy = self.call("GET", session_path + "/export")
        assert status == 200
        _, _, preview = self.begin(legacy)
        result = self.terminal(preview)
        assert result["state"] == "succeeded" and result["result"]["legacy_partial"]
        assert result["result"]["file_count"] == 2 and not result["restore_ready"]
        held = self.fixture("pin/" + result["id"])
        assert held["pin_files"] == 2 and held["access"] == 0
        self.fixture("expire")
        assert self.api("GET", preview)[0] == 404
        pinned = self.fixture("state")
        assert pinned["retained_files"] == 2 and pinned["pin_content_sha256"] == held["pin_content_sha256"]
        assert self.api("GET", PREVIEWS)[1]["data"]["preview"] is None
        assert self.api("POST", PREFIX + "/" + UPLOAD_ID + "/preview")[0] == 409
        assert self.fixture("pin-second/" + result["id"])["access"] == 1
        self.fixture("release-first")
        assert self.fixture("state")["retained_files"] == 0

        # Unit must cancel/join the live worker before Init publishes a new
        # store. Late cleanup cannot write into the new store generation.
        self.fixture("hold-1")
        _, _, preview = self.begin(data)
        self.wait(lambda: self.fixture("state")["entered"] == 2)
        self.fixture("reset")
        assert self.api("GET", preview)[0] == 404
        assert self.fixture("state")["retained_files"] == 0
        self.fixture("resume")
        _, _, preview = self.begin(data)
        result = self.terminal(preview)
        assert result["state"] == "succeeded"
        held = self.fixture("pin/" + result["id"])
        assert self.fixture("pin-second/" + result["id"])["pins_held"] == 2
        self.fixture("reset")
        pinned = self.fixture("state")
        assert pinned["retained_files"] == 0 and pinned["pin_content_sha256"] == held["pin_content_sha256"]
        start, _, new_preview = self.begin(legacy)
        new_result = self.terminal(new_preview)
        assert new_result["state"] == "succeeded" and new_result["result"]["file_count"] == 2
        assert self.fixture("release-first")["pin_content_sha256"] == held["pin_content_sha256"]
        pinned = self.fixture("release-second")
        assert pinned["pins_held"] == 0 and pinned["retained_files"] == 2
        assert self.api("GET", new_preview)[1]["data"]["result_available"]

        # A native consumer's last release is held during large out-of-lock
        # cleanup. Queries remain responsive and quota stays busy. Unit/Init
        # can retire the owning generation without destroying its held mutex;
        # resuming old cleanup must not clear or free the new store's result.
        self.fixture("pin/" + new_result["id"])
        self.fixture("hold-release")
        assert self.api("DELETE", new_preview)[0] == 200
        self.fixture("release-async")
        self.wait(lambda: self.fixture("state")["release_entered"])
        assert self.api("GET", new_preview)[1]["data"]["discarded"]
        assert self.api("POST", start)[0] == 409
        self.fixture("reset")
        _, _, preview = self.begin(data)
        result = self.terminal(preview)
        assert result["state"] == "succeeded" and result["result"]["file_count"] == len(source["files"])
        self.fixture("resume-release")
        self.wait(lambda: self.fixture("state")["release_done"])
        self.fixture("join-release")
        pinned = self.fixture("pin/" + result["id"])
        assert pinned["pin_content_sha256"] == digest.hexdigest() and pinned["retained_files"] == len(source["files"])
        self.fixture("release-first")
        assert self.api("DELETE", preview)[0] == 200
        assert self.api("DELETE", PREFIX + "/" + UPLOAD_ID)[0] == 200
        assert before == inventory(), "offline preview changed Home or executed queued work"
        assert self.fixture("state")["lock_failures"] == 0, "preview tried to reacquire its non-recursive status mutex"
        print(f"backup preview {'TLS' if self.secure else 'HTTP'}: production async gates, immutable multi-reader pins, deletion/expiry quota, native last-release/Unit generations, cancel/retry and zero Home writes PASS", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, required=True)
    host = parser.parse_args().host.resolve()
    (ROOT / ".build").mkdir(exist_ok=True)
    for secure in (False, True):
        with tempfile.TemporaryDirectory(prefix="backup-preview-", dir=ROOT / ".build") as raw:
            probe = Probe(host, Path(raw), secure)
            try:
                probe.start()
                probe.check()
            finally:
                probe.stop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
