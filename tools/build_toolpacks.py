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
    catalog=json.loads((ROOT/"tools/runtime/catalog.json").read_text(encoding="utf-8"))
    licenses={"busybox":"GPL-2.0-only", "curl":"curl", "jq":"MIT", "openssh":"BSD; bundled dependency licenses included", "python":"PSF-2.0", "aria2":"GPL-2.0-or-later; bundled dependency licenses included", "ripgrep":"MIT OR Unlicense", "7zip":"LGPL-2.1-or-later AND BSD-3-Clause; unRAR restriction"}
    for package, tools in {"core": ["busybox", "curl", "jq", "openssh", "aria2", "ripgrep", "7zip"], "python": ["python"]}.items():
        with tempfile.TemporaryDirectory(prefix="toolpack-", dir=ROOT / ".build") as raw:
            stage = Path(raw) / "stage"; stage.mkdir()
            for tool in tools: shutil.copytree(ROOT / "tools/runtime/windows-x86_64" / tool, stage / tool)
            files = [{"path": p.relative_to(stage).as_posix(), "size": p.stat().st_size,
                      "sha256": hashlib.sha256(p.read_bytes()).hexdigest()} for p in sorted(stage.rglob("*")) if p.is_file() and p.stat().st_size]
            (stage / "xs.json").write_text("{\"services\": []}")
            details=[]
            for tool in tools:
                info=next(t for t in catalog["tools"] if t["id"]==tool and t["platform"]=="windows-x86_64")
                source=info.get("source_url") or info.get("provenance") or info["sources"][0]["url"]
                details.append({"id":tool,"name":tool,"version":info["version"],"size":info["unpacked_bytes"],"source":source,"license":licenses[tool]})
            manifest = {"id": package, "platform": "windows-x86_64", "revision": release["toolpack_revision"], "files": files,"tools":details,"dependencies":[]}
            (stage / "toolpack.json").write_text(json.dumps(manifest), encoding="utf-8")
            packed = Path(raw) / "packed.exe"
            subprocess.run([str(packer.resolve()), "pack", str(stage), "-o", str(packed)], check=True)
            archive = output / (package + ".xrtpack"); extract_pack(packed, archive)
            digest = hashlib.sha256(archive.read_bytes()).hexdigest()
            rows.append({"id": package, "platform": "windows-x86_64", "revision": release["toolpack_revision"],
                         "sha256": digest, "size": archive.stat().st_size,"name":"常用工具包" if package=="core" else "Python", "min_build":release['windows_build_id'],"max_build":0,"dependencies":[],"tools":details,"status":"published", "notes": "BusyBox, curl, jq, SSH/SCP/SFTP, aria2c, ripgrep, 7-Zip" if package == "core" else "CPython embeddable runtime", "file": archive.name})
    (output / "catalog.json").write_text(json.dumps({"toolpacks": rows}, indent=2), encoding="utf-8")
    print("Built verified tool packages: " + str(output))
if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--packer", type=Path, default=ROOT / ".build/host/xsw.exe")
    parser.add_argument("--output", type=Path, default=ROOT / ".build/releases/toolpacks")
    args = parser.parse_args(); build(args.packer, args.output)
