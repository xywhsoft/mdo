#!/usr/bin/env python3
"""Pack verified portable Linux runtimes for GUI and service editions."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
from build_android import extract_pack

ROOT=Path(__file__).resolve().parents[1]

def build(packer,output):
    release=json.loads((ROOT/"app/release.json").read_text(encoding="utf-8"))
    runtime=ROOT/"tools/runtime/linux-x86_64"
    lock=json.loads((ROOT/"tools/runtime/build/linux.lock.json").read_text(encoding="utf-8"))
    for package in lock["packages"]:
        data=(ROOT/"tools/runtime/sources/linux-apk"/package["file"]).read_bytes()
        if len(data)!=package["size"] or hashlib.sha256(data).hexdigest()!=package["sha256"]:
            raise ValueError("Pinned source package changed: "+package["file"])
    catalog=json.loads((runtime/"catalog.json").read_text(encoding="utf-8"))["tools"]
    output.mkdir(parents=True,exist_ok=True);rows=[]
    for group,tools in {"core":["busybox","curl","jq","openssh","aria2","ripgrep","7zip"],"python":["python"]}.items():
        with tempfile.TemporaryDirectory(prefix="linux-toolpack-",dir=ROOT/".build") as raw:
            stage=Path(raw)/"stage";stage.mkdir()
            for tool in tools:shutil.copytree(runtime/tool,stage/tool)
            def omit(directory,names):
                return [name for name in names if group=="core" and (name.startswith("python3") or name.startswith("libpython"))]
            shutil.copytree(runtime/".runtime",stage/".runtime",ignore=omit)
            shutil.copyfile(runtime/"provenance.json",stage/"provenance.json")
            files=[]
            for path in sorted(stage.rglob("*")):
                if not path.is_file():continue
                data=path.read_bytes()
                files.append(dict(path=path.relative_to(stage).as_posix(),size=len(data),sha256=hashlib.sha256(data).hexdigest(),
                                  mode=0o755 if data.startswith((b"\x7fELF",b"#!")) else 0o644))
            details=[dict(id=t["id"],name=t["id"],version=t["version"],size=t["unpacked_bytes"],source=t["source_url"],license=t["license"])
                     for t in catalog if t["id"] in tools]
            manifest=dict(id=group,platform="linux-x86_64",revision=release["toolpack_revision"],files=files,tools=details,dependencies=[])
            (stage/"toolpack.json").write_text(json.dumps(manifest),encoding="utf-8")
            (stage/"xs.json").write_text('{"services":[]}',encoding="utf-8")
            packed=Path(raw)/"packed";subprocess.run([str(packer.resolve()),"pack",str(stage),"-o",str(packed)],check=True)
            archive=output/(group+".xrtpack");extract_pack(packed,archive)
            rows.append(dict(id=group,platform="linux-x86_64",revision=release["toolpack_revision"],
                name="Linux 常用工具包" if group=="core" else "Linux Python",status="published",
                min_build=min(release[k] for k in release if k.startswith("linux_") and k.endswith("_build_id")),max_build=0,
                sha256=hashlib.sha256(archive.read_bytes()).hexdigest(),size=archive.stat().st_size,
                notes="Private musl runtime; compatible with glibc and musl mdo. No system installation.",dependencies=[],tools=details,file=archive.name))
    (output/"catalog.json").write_text(json.dumps(dict(toolpacks=rows),ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    print("Built verified Linux toolpacks:",output)

if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--packer",type=Path,default=ROOT/(".build/host/xsw.exe" if __import__('os').name=="nt" else ".build/host/linux-server-glibc/xs"))
    parser.add_argument("--output",type=Path,default=ROOT/".build/releases/linux-toolpacks")
    args=parser.parse_args();build(args.packer,args.output)
