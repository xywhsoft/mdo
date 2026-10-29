"""Regenerate the reviewed SDK overlay from the existing SDK source cache."""
from __future__ import annotations
import argparse
import difflib
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import build_mdo


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sdk", type=Path)
    parser.add_argument("--base-repository", type=Path, default=ROOT.parent / "xserver")
    args = parser.parse_args()
    sdk = args.sdk.resolve()
    lock = json.loads((ROOT / "deps.lock").read_text(encoding="utf-8"))
    base = lock["xserver"]["commit"]
    subprocess.run(["git", "cat-file", "-e", base + "^{commit}"], cwd=args.base_repository, check=True)
    patch = ROOT / lock["sdk_overlay"]["path"]
    names = set(re.findall(r"^\+\+\+ b/(.+)$", patch.read_text(encoding="utf-8"), re.M))
    names.update("lib/xwork/" + name for name in (
        "xwork.h", "src/xwork_internal.h", "src/xwork_runtime.c",
        "src/xwork_core.c", "src/xwork_agent.c", "src/xwork_subagent.c",
        "xwork-xrt-roots.h", "src/xwork_process_output.inc.c",
        "src/xwork_process_tools.inc.c", "src/xwork_task.c",
        "src/xwork_schedule.c", "src/xwork_read.inc.c"))
    native = ROOT / 'tools/native'
    output = sdk / 'lib/xwork/src/xwork_process_output.inc.c'
    output.write_bytes((native / output.name).read_bytes())
    process_tools = sdk / 'lib/xwork/src/xwork_process_tools.inc.c'
    process_tools.write_bytes((native / process_tools.name).read_bytes())
    header = (sdk / 'lib/xwork/xwork.h').read_text(encoding='utf-8')
    declarations = (native / 'xwork_process_output.inc.h').read_text(encoding='utf-8')
    if declarations not in header:
        raise ValueError('SDK output declarations differ from the maintained native header')
    result = []
    for name in sorted(names):
        old = subprocess.run(["git", "show", base + ":" + name],
                             cwd=args.base_repository, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if old.returncode and b"does not exist" not in old.stderr and b"exists on disk, but not in" not in old.stderr:
            raise RuntimeError("Cannot read SDK baseline: " + name)
        before = old.stdout.decode("utf-8").replace("\r\n", "\n") if old.returncode == 0 else ""
        after = (sdk / name).read_text(encoding="utf-8")
        result.extend(difflib.unified_diff(before.splitlines(keepends=True), after.splitlines(keepends=True),
                      fromfile="a/" + name if old.returncode == 0 else "/dev/null", tofile="b/" + name))
    patch.write_text("".join(result), encoding="utf-8", newline="\n")
    lock["libraries"]["xwork"]["production_tree_sha256"] = build_mdo.production_tree_sha256(sdk / "lib/xwork")
    lock["sdk_overlay"]["sha256"] = hashlib.sha256(patch.read_bytes()).hexdigest()
    (ROOT / "deps.lock").write_text(json.dumps(lock, indent=2) + "\n", encoding="utf-8", newline="\n")
    print("SDK overlay:", lock["sdk_overlay"]["sha256"])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
