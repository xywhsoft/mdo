#!/usr/bin/env python3
"""Build installable XRTPACK bundles from the verified per-tool staging tree."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
from build_android import extract_pack

ROOT = Path(__file__).resolve().parents[1]
def build(packer: Path, output: Path):
    release = json.loads((ROOT / "app/release.json").read_text(encoding="utf-8"))
    output.mkdir(parents=True, exist_ok=True)
    rows = []
    for package, tools in {"core": ["busybox", "curl", "jq", "openssh"], "python": ["python"]}.items():
        with tempfile.TemporaryDirectory(prefix="toolpack-", dir=ROOT / ".build") as raw:
            stage = Path(raw) / "stage"; stage.mkdir()
            for tool in tools: shutil.copytree(ROOT / "tools/runtime/windows-x86_64" / tool, stage / tool)
            files = [{"path": p.relative_to(stage).as_posix(), "size": p.stat().st_size,
                      "sha256": hashlib.sha256(p.read_bytes()).hexdigest()} for p in sorted(stage.rglob("*")) if p.is_file() and p.stat().st_size]
            (stage / "xs.json").write_text("{\"services\": []}")
            manifest = {"id": package, "platform": "windows-x86_64", "revision": release["toolpack_revision"], "files": files}
            (stage / "toolpack.json").write_text(json.dumps(manifest), encoding="utf-8")
            packed = Path(raw) / "packed.exe"
            subprocess.run([str(packer.resolve()), "pack", str(stage), "-o", str(packed)], check=True)
            archive = output / (package + ".xrtpack"); extract_pack(packed, archive)
            digest = hashlib.sha256(archive.read_bytes()).hexdigest()
            rows.append({"id": package, "platform": "windows-x86_64", "revision": release["toolpack_revision"],
                         "sha256": digest, "size": archive.stat().st_size, "notes": "BusyBox, curl, jq, SSH/SCP/SFTP" if package == "core" else "CPython embeddable runtime", "file": archive.name})
    (output / "catalog.json").write_text(json.dumps({"toolpacks": rows}, indent=2), encoding="utf-8")
    print("Built verified tool packages: " + str(output))
if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--packer", type=Path, default=ROOT / ".build/host/xsw.exe")
    parser.add_argument("--output", type=Path, default=ROOT / ".build/releases/toolpacks")
    args = parser.parse_args(); build(args.packer, args.output)
