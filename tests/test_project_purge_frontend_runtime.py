"""Production frontend request state against tiny synthetic HTTP/xs/TCC Homes.

No browser automation, external model calls, user data, stress or load tests.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

from test_project_purge_receipts_runtime import ROOT, running


def run_probe(host: Path) -> None:
    node = shutil.which("node")
    if not node:
        raise RuntimeError("Node is needed for this QA probe, not for building mdo.exe")
    with tempfile.TemporaryDirectory(prefix="purge-frontend-", dir=ROOT / ".build") as raw:
        for mode in ("success", "lost-prepare", "lost-execute", "lost-ack", "rollback", "cancel", "drain-fail"):
            with running(host, Path(raw) / mode) as probe:
                targets = probe.seed()
                if mode == "rollback":
                    probe.control("fault-rollback")
                before = probe.files()
                result = subprocess.run([node, str(ROOT / "tests/fixtures/project-purge-frontend-runtime.mjs"),
                    f"http://127.0.0.1:{probe.port}", mode], cwd=ROOT, check=True,
                    capture_output=True, text=True, encoding="utf-8", timeout=15)
                summary = json.loads(result.stdout.strip())
                assert summary["mode"] == mode, summary
                after = probe.files()
                committed = mode in ("success", "lost-prepare", "lost-execute", "lost-ack")
                for path, content in before.items():
                    removed = any(path == target or path.startswith(target + "/") for target in targets)
                    if committed and removed:
                        assert path not in after, (mode, path)
                    else:
                        assert after.get(path) == content, (mode, path)
                assert (probe.base / "workspace/user-file.txt").read_bytes() == b"outside Home; never remove\n"
                assert not (probe.home / "data/project-purge-intent.json").exists(), mode
    print("Frontend original-ID/drain/execute/ACK/lost-reply HTTP/xs/TCC probe: PASS")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, required=True)
    run_probe(parser.parse_args().host.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
