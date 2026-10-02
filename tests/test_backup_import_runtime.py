"""Bounded real frontend import and lost-response recovery over HTTP/TLS."""
from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

from test_restore_worker_runtime import Probe as WorkerProbe
from test_restore_coordinator_runtime import ROOT, ARTIFACT, dump


class Probe(WorkerProbe):
    def check(self):
        document, payload = self.setup_source()
        source = self.base / "source.json"
        source.write_bytes(dump(document))
        before_catalog = {item["id"] for item in self.api("GET", "/api/v1/sessions")[1]["data"]["items"]}
        output = self.base / "import-result.json"
        env = dict(os.environ)
        if self.secure:
            env["NODE_TLS_REJECT_UNAUTHORIZED"] = "0"  # Owned loopback cert only.
        subprocess.run(["node", str(ROOT / "tests/fixtures/backup-import-ui.mjs"),
            f"{'https' if self.secure else 'http'}://127.0.0.1:{self.port}/",
            str(source), str(output)], cwd=ROOT, env=env, timeout=90, check=True)
        result = json.loads(output.read_text(encoding="utf-8"))
        receipt = result["result"]
        assert receipt["committed"] and receipt["terminal"], receipt
        assert receipt["source_sha256"] == hashlib.sha256(source.read_bytes()).hexdigest()
        target = self.target(receipt["id"])
        files = self.inventory(target)
        original = {f["path"]: base64.b64decode(f["data"]) for f in document["files"]}
        for name in ("snapshot.json", "journal.jsonl", ARTIFACT):
            if name in original:
                assert files[name] == original[name], name
        assert files[ARTIFACT] == payload
        assert json.loads(files["draft.json"])["text"] == "Preserve 草稿"
        assert json.loads(files["queue.json"])["items"][0]["state"] == "staged"
        catalog = self.api("GET", "/api/v1/sessions")[1]["data"]["items"]
        assert {item["id"] for item in catalog} == before_catalog | {receipt["id"]}
        restored = [item for item in catalog if item["id"] == receipt["id"]]
        assert len(restored) == 1 and restored[0]["project_id"] == "restore-target"
        assert not restored[0]["runtime_open"]
        assert self.inventory(self.source_directory) == self.source_bytes
        assert self.api("GET", "/api/v1/session-backups/restores")[1]["data"]["id"] == receipt["id"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, required=True)
    host = parser.parse_args().host.resolve()
    for secure in (False, True):
        with tempfile.TemporaryDirectory(prefix="backup-import-", dir=ROOT / ".build") as raw:
            probe = Probe(host, Path(raw), secure)
            try:
                probe.start(); probe.check()
            finally:
                probe.stop()
    print("formal frontend import and same-ID response recovery: PASS")


if __name__ == "__main__":
    main()
