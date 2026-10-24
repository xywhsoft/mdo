#!/usr/bin/env python3
"""Build independent, data-only CLI artifacts from the pinned runtime staging."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import tempfile
from runtime.build.python_runtime import prepare_windows_python

ROOT=Path(__file__).resolve().parents[1]

def digest(path):
    h=hashlib.sha256()
    with path.open("rb") as f:
        while b:=f.read(1048576):h.update(b)
    return h.hexdigest()

def version(value):
    numbers=[int(v) for v in re.findall(r"\d+",value)[:3]]
    return ".".join(map(str,(numbers+[0,0,0])[:3]))

def elf_needed(path):
    data=path.read_bytes()
    if data[:6]!=b"\x7fELF\x02\x01":return []
    offset=struct.unpack_from("<Q",data,32)[0];size,count=struct.unpack_from("<HH",data,54)
    loads=[];dynamic=None
    for i in range(count):
        kind,flags,off,addr,phys,filesz,memsz,align=struct.unpack_from("<IIQQQQQQ",data,offset+i*size)
        if kind==1:loads.append((addr,filesz,off))
        if kind==2:dynamic=(off,filesz)
    needed=[];strings=None
    if not dynamic:return []
    for i in range(dynamic[0],sum(dynamic),16):
        tag,value=struct.unpack_from("<qQ",data,i)
        if tag==0:break
        if tag==1:needed.append(value)
        if tag==5:
            strings=next(off+value-addr for addr,n,off in loads if addr<=value<addr+n)
    return [data[strings+n:data.index(0,strings+n)].decode("ascii") for n in needed]

def linux_files(stage,tool,runtime):
    """Each artifact owns its loader and recursive ELF closure; no shared install."""
    source=runtime/".runtime";pending=[]
    for p in (stage/tool).rglob("*"):
        if not p.is_file():continue
        text=p.read_text();text=text.replace("/.runtime","/runtime");p.write_text(text,newline="\n")
        pending.extend(re.findall(r'"\$root/([^"\n]+)" "\$@"',text))
    if tool=="python":pending.extend(p.relative_to(source).as_posix() for p in (source/"usr/lib").glob("python*/**/*.so"))
    pending.append("lib/ld-musl-x86_64.so.1");copied=set()
    while pending:
        relative=pending.pop()
        if relative in copied:continue
        p=source/relative
        if not p.is_file():raise ValueError("Missing portable runtime: "+relative)
        dest=stage/"runtime"/relative;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,dest);copied.add(relative)
        for needed in elf_needed(p):
            match=next((str((source/d/needed).relative_to(source)).replace("\\","/") for d in ("lib","usr/lib") if (source/d/needed).is_file()),None)
            if not match:raise ValueError("Missing ELF dependency: "+needed)
            pending.append(match)
    for directory in ("etc/ssl","usr/share/licenses"):
        if (source/directory).exists():shutil.copytree(source/directory,stage/"runtime"/directory,dirs_exist_ok=True)
    if tool=="python":
        for p in (source/"usr/lib").glob("python*"):
            if p.is_dir():shutil.copytree(p,stage/"runtime/usr/lib"/p.name,dirs_exist_ok=True)
    shutil.copy2(runtime/"provenance.json",stage/"provenance.json")

def build(output,platforms):
    definitions=json.loads((ROOT/"app/default-home/cli-tools/official.json").read_text(encoding="utf-8"))["tools"]
    catalog=json.loads((ROOT/"tools/runtime/catalog.json").read_text(encoding="utf-8"))["tools"]
    output.mkdir(parents=True,exist_ok=True);rows=[]
    for platform in platforms:
        runtime=ROOT/"tools/runtime"/platform
        if platform=="linux-x86_64":info=json.loads((runtime/"catalog.json").read_text())["tools"]
        else:info=[r for r in catalog if r["platform"]==platform]
        for definition in definitions:
            tool=definition["id"];record=next(r for r in info if r["id"]==tool)
            with tempfile.TemporaryDirectory(prefix="cli-artifact-",dir=ROOT/".build") as raw:
                stage=Path(raw);shutil.copytree(runtime/tool,stage/tool)
                if platform=="windows-x86_64" and tool=="python":prepare_windows_python(stage/tool)
                if platform=="linux-x86_64":linux_files(stage,tool,runtime)
                files=[]
                for p in sorted(stage.rglob("*")):
                    if not p.is_file():continue
                    path=p.relative_to(stage).as_posix()
                    if any(v.startswith(".") for v in path.split("/")):raise ValueError("Hidden archive component: "+path)
                    start=p.read_bytes()[:4]
                    files.append(dict(path=path,size=p.stat().st_size,sha256=digest(p),mode=493 if start[:2]==b"#!" or start==b"\x7fELF" or p.suffix.lower()==".exe" else 420))
                entry=tool+"/"+record["entrypoints"][0]
                manifest=dict(id=tool,name=definition["name"],version=version(record["version"]),platform=platform,
                              description=definition["description"],prompt=definition["prompt"],entry=entry,license=record.get("license",definition["license"]),links=definition["links"],files=files)
                manifest["commands"]=[dict(name=Path(p).stem if p.lower().endswith('.exe') else Path(p).name,entry=tool+'/'+p,args=[]) for p in record["entrypoints"]]
                if tool=="python":
                    names={c['name'] for c in manifest['commands']}
                    for name in ('python','python3'):
                        if name not in names:manifest['commands'].append(dict(name=name,entry=entry,args=[]))
                if tool=="busybox":
                    for name in ('sh','ash','awk','sed','grep','find','cat','head','tail','sort','wc','cp','mv','rm','mkdir','rmdir','touch','tar','gzip','gunzip','unzip','xargs','tee','tr','which','basename','dirname','ls','pwd','echo'):
                        manifest['commands'].append(dict(name=name,entry=entry,args=[name]))
                encoded=json.dumps(manifest,ensure_ascii=False,separators=(",",":")).encode()
                path=output/f"{tool}-{manifest['version']}-{platform}.mdo-cli"
                with path.open("wb") as f:
                    f.write(b"MDOCLI1\n"+struct.pack("<I",len(encoded))+encoded)
                    for row in files:
                        with (stage/row["path"]).open("rb") as source:shutil.copyfileobj(source,f,1048576)
                assert path.stat().st_size<=512*1048576 and len(encoded)<=1048576 and len(files)<=4096
                rows.append(dict(manifest,tool_id="official."+tool,sha256=digest(path),size=path.stat().st_size,
                                 recommended=definition["recommended"],file=path.name,upstream_version=record["version"]))
                print(f"Built {path.name}: {path.stat().st_size} bytes, {len(files)} files",flush=True)
    (output/"catalog.json").write_text(json.dumps(dict(tools=rows),ensure_ascii=False,indent=2)+"\n",encoding="utf-8")

if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__);p.add_argument("--output",type=Path,default=ROOT/".build/releases/cli-tools")
    p.add_argument("--platform",action="append",choices=["windows-x86_64","linux-x86_64","android-arm64-v8a"])
    a=p.parse_args();build(a.output,a.platform or ["windows-x86_64","linux-x86_64","android-arm64-v8a"])
