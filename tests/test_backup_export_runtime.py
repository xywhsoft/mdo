"""One real frontend complete backup per HTTP/TLS protocol; no load testing."""
from __future__ import annotations

import argparse
import base64
import json
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
from urllib.parse import quote

from test_backup_decode_runtime import ROOT, DECODE, Probe as DecodeProbe


class Probe(DecodeProbe):
    def check(self):
        status, value = self.api("POST", "/api/v1/sessions", {
            "project_id": "default", "title": "Frontend complete backup", "agent_id": "mdo.default",
            "model_id": "backup-vision-fixture", "protocol": "openai-responses", "reasoning_effort": "medium",
            "max_output_tokens": 1024,
        })
        assert status == 201, value
        session = value["data"]["id"]
        assert self.api("GET", DECODE + "seed-runtime/" + session)[1]["data"]["ok"]
        prefix = f"/api/v1/projects/default/sessions/{session}"
        png = base64.b64decode("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+ip1sAAAAASUVORK5CYII=")
        status, value = self.api("POST", prefix + "/attachments", png, {
            "Content-Type": "image/png", "X-Mdo-File-Name": quote('截图 "1" 100%.png', safe=""),
        })
        assert status == 201, value
        image = value["data"]["id"]
        assert self.api("PUT", prefix + "/draft", {"revision": 0, "text": "Export saved 草稿", "attachments": [image]})[0] == 200
        assert self.api("POST", prefix + "/queue", {"id": "b" * 32, "text": "Wait for user", "first": False,
                "stage": True, "attachments": [image]})[0] == 201
        directory = self.home / "sessions/default" / session
        artifact = directory / "artifacts/run-00000000000000000001/00000000000000000001-export.txt"
        artifact.parent.mkdir(parents=True, exist_ok=True)
        artifact.write_bytes(bytes(range(256)) * 8192)
        before = {p.relative_to(directory).as_posix(): p.read_bytes() for p in directory.rglob("*") if p.is_file()}
        output = self.base / "frontend.backup.json"
        env = dict(os.environ)
        if self.secure:
            env["NODE_TLS_REJECT_UNAUTHORIZED"] = "0"  # Own loopback certificate only; helper enforces the origin.
        assert shutil.which("node"), "Node.js is required for frontend release checks"
        subprocess.run(["node", str(ROOT / "tests/fixtures/backup-download-ui.mjs"),
            f"{'https' if self.secure else 'http'}://127.0.0.1:{self.port}/", session, str(output)],
            cwd=ROOT, env=env, timeout=30, check=True)
        decoded = self.validate(output.read_bytes())
        assert decoded["ok"], decoded
        history = self.api("GET", DECODE + "model-history")[1]["data"]
        assert history["ok"] and history["matched"] >= 6, history
        pixels = self.api("GET", DECODE + "images")[1]["data"]
        assert pixels["ok"] and pixels["attachments"] == pixels["inline_images"] == 1, pixels
        after = {p.relative_to(directory).as_posix(): p.read_bytes()
            for p in directory.rglob("*") if p.is_file()}
        changed = sorted(name for name in before.keys() | after.keys()
            if before.get(name) != after.get(name))
        def json_changes(left, right, path=""):
            if left == right:
                return []
            if isinstance(left, dict) and isinstance(right, dict):
                return [item for key in left.keys() | right.keys()
                    for item in json_changes(left.get(key), right.get(key), path + "/" + key)]
            return [path]
        if "snapshot.json" in changed:
            previous = json.loads(before["snapshot.json"])
            current = json.loads(after["snapshot.json"])
            fields = json_changes(previous, current)
            # Cold capture recovers the ledger and reapplies its committed
            # model profile. That invalidates prior exact-usage feedback by
            # contract; checkpointing persists fill_seen=0 and its checksum.
            # Allow only that projection change, never messages or budgets.
            assert set(fields) == {"/fill_seen", "/checksum"}, fields
            assert (previous["fill_seen"], current["fill_seen"]) == (1, 0)
            changed.remove("snapshot.json")
        assert not changed, {"changed_files": changed,
            "snapshot_fields": json_changes(json.loads(before["snapshot.json"]),
                json.loads(after["snapshot.json"])),
            "sizes": {name: [len(before.get(name, b"")), len(after.get(name, b""))]
                for name in changed}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, required=True)
    host = parser.parse_args().host.resolve()
    for secure in (False, True):
        with tempfile.TemporaryDirectory(prefix="backup-export-", dir=ROOT / ".build") as raw:
            probe = Probe(host, Path(raw), secure)
            try:
                probe.start(); probe.check()
            finally:
                probe.stop()
    print("formal frontend complete backup transfer/readback probe: PASS")


if __name__ == "__main__":
    main()
