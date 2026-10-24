#!/usr/bin/env python3
"""Package independently staged optional tools; never discover system programs.

Stage layout: <stage>/<platform>/<id>/<payload>, plus an input.json containing
explicit manifest metadata and commands. Dependencies belong to each artifact.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct

SYSTEM_DLLS = set("advapi32 bcrypt bcryptprimitives cabinet cfgmgr32 comctl32 comdlg32 crypt32 cryptbase cryptnet cryptsp d3d11 d3d12 dbghelp dnsapi dwmapi dxgi gdi32 gdiplus hid imagehlp imm32 iphlpapi kernel32 kernelbase mpr msvcrt ncrypt netapi32 normaliz ntdll ntmarta ole32 oleaut32 powrprof profapi propsys psapi rpcrt4 secur32 setupapi shell32 shlwapi sspicli ucrtbase user32 userenv usp10 uxtheme version winhttp wininet winmm winspool wintrust wldap32 ws2_32 wtsapi32".split())
SYSTEM_DLLS.update(("d3dcompiler_47", "msimg32", "wsock32", "avicap32", "msvfw32"))


def digest(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def pe_imports(path):
    data = path.read_bytes()
    if data[:2] != b"MZ": return []
    pe = struct.unpack_from("<I", data, 60)[0]
    if data[pe:pe+4] != b"PE\0\0": raise ValueError("Invalid PE image")
    count, optional_size = struct.unpack_from("<H12xH", data, pe+6)
    opt = pe+24
    magic = struct.unpack_from("<H", data, opt)[0]
    directory = opt + (112 if magic == 0x20b else 96)
    sections = []
    for i in range(count):
        off = opt+optional_size+i*40
        virtual_size, addr, size, raw = struct.unpack_from("<IIII", data, off+8)
        sections.append((addr, max(size, virtual_size), raw))
    def offset(rva):
        for addr, size, raw in sections:
            if addr <= rva < addr+size: return raw+rva-addr
        raise ValueError("Invalid PE import RVA")
    names = []
    for index, stride, name_offset in ((1,20,12),(13,32,4)):
        rva, size = struct.unpack_from("<II", data, directory+index*8)
        if not rva: continue
        start = offset(rva)
        for off in range(start, min(start+size, len(data)), stride):
            if not any(data[off:off+stride]): break
            name = struct.unpack_from("<I", data, off+name_offset)[0]
            if index == 13 and not (struct.unpack_from("<I", data, off)[0] & 1):
                base = struct.unpack_from("<Q" if magic == 0x20b else "<I", data, opt+(24 if magic==0x20b else 28))[0]
                name -= base
            pos = offset(name)
            names.append(data[pos:data.index(0,pos)].decode("ascii"))
    return names


def pe_closure(programs, roots, target):
    candidates = {}
    for root in roots:
        for p in root.rglob("*"):
            if p.is_file() and p.suffix.lower()==".dll":
                name=p.name.lower()
                if name in candidates and digest(candidates[name])!=digest(p):
                    raise ValueError("Ambiguous DLL: "+p.name)
                candidates[name]=p
    target.mkdir(parents=True,exist_ok=True)
    pending=list(programs);copied={}
    while pending:
        source=pending.pop();key=source.name.lower()
        if key in copied: continue
        shutil.copy2(source,target/source.name);copied[key]=source
        for name in pe_imports(source):
            lower=name.lower()
            if lower.startswith(("api-ms-win-","ext-ms-win-")) or (lower.rsplit(".",1)[-1] in ("dll","drv") and lower.rsplit(".",1)[0] in SYSTEM_DLLS): continue
            if lower not in candidates: raise ValueError(f"Missing DLL {name} required by {source.name}")
            pending.append(candidates[lower])
    return copied


def elf_needed(path):
    """Read ELF64 DT_NEEDED entries without executing target-platform programs."""
    data = path.read_bytes()
    if data[:4] != b"\x7fELF":
        return []
    if data[4:6] != b"\x02\x01":
        raise ValueError("Only little-endian ELF64 is supported: " + str(path))
    phoff = struct.unpack_from("<Q", data, 32)[0]
    stride, count = struct.unpack_from("<HH", data, 54)
    loads, dynamic = [], None
    for i in range(count):
        kind, flags, offset, addr, phys, size, mem, align = struct.unpack_from("<IIQQQQQQ", data, phoff+i*stride)
        if kind == 1:
            loads.append((addr, size, offset))
        if kind == 2:
            dynamic = (offset, size)
    if dynamic is None:
        return []
    needed, string_addr = [], None
    for pos in range(dynamic[0], dynamic[0]+dynamic[1], 16):
        kind, value = struct.unpack_from("<QQ", data, pos)
        if kind == 0:
            break
        if kind == 1:
            needed.append(value)
        elif kind == 5:
            string_addr = value
    if not needed:
        return []
    for addr, size, offset in loads:
        if string_addr is not None and addr <= string_addr < addr+size:
            table = offset+string_addr-addr
            return [data[table+n:data.index(0, table+n)].decode("ascii") for n in needed]
    raise ValueError("Missing ELF string table: " + str(path))


def elf_closure(programs, roots, target, system=()):
    """Copy an explicit dependency closure; never consult host libraries."""
    candidates = {}
    for root in roots:
        for path in root.rglob("*"):
            if path.is_file() and ".so" in path.name:
                if path.name in candidates and digest(candidates[path.name]) != digest(path):
                    raise ValueError("Ambiguous ELF library: " + path.name)
                candidates[path.name] = path
    target.mkdir(parents=True, exist_ok=True)
    pending, copied, inspected = list(programs), {}, set()
    while pending:
        path = pending.pop()
        if path in inspected:
            continue
        inspected.add(path)
        for name in elf_needed(path):
            if name in system:
                continue
            if name not in candidates:
                raise ValueError(f"Missing ELF library {name} required by {path}")
            if name not in copied:
                source = candidates[name]
                shutil.copy2(source, target/name)
                copied[name] = source
                pending.append(source)
    return copied


def valid_path(name):
    if not name or len(name)>220 or "\\" in name or ":" in name: return False
    for part in name.split("/"):
        if not part or len(part)>64 or part.startswith(".") or part.endswith(("."," ")): return False
        if any(ord(c)<32 or ord(c)==127 or c in '<>"|?*' for c in part): return False
        stem=part.lower().split(".")[0]
        if stem in {"con","prn","aux","nul"} or re.fullmatch(r"(?:com|lpt)[1-9]",stem): return False
    return True


def package(stage, metadata, output):
    if not re.fullmatch(r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)",metadata["version"]):
        raise ValueError("A three-component package version is required")
    files=[];seen=set()
    for path in sorted(stage.rglob("*")):
        if not path.is_file(): continue
        name=path.relative_to(stage).as_posix()
        if not valid_path(name) or name.lower() in seen: raise ValueError("Unsafe or aliased file: "+name)
        if not path.resolve().is_relative_to(stage.resolve()): raise ValueError("File leaves staging directory")
        seen.add(name.lower())
        with path.open("rb") as source: magic=source.read(4)
        files.append(dict(path=name,size=path.stat().st_size,sha256=digest(path),mode=493 if magic.startswith((b"#!",b"\x7fELF",b"MZ")) else 420))
    manifest={k:metadata[k] for k in ("id","name","version","platform","prompt","entry","license","links","commands")}
    manifest["files"]=files
    assert any(f["path"]==manifest["entry"] and f["size"] for f in files)
    assert all(any(f["path"]==c["entry"] and f["size"] for f in files) for c in manifest["commands"])
    encoded=json.dumps(manifest,ensure_ascii=False,separators=(",",":")).encode()
    assert 0<len(files)<=4096 and len(encoded)<=1048576 and sum(f["size"] for f in files)+len(encoded)+12<=512*1048576
    output.mkdir(parents=True,exist_ok=True)
    path=output/f"{metadata['id']}-{metadata['version']}-{metadata['platform']}.mdo-cli"
    pending=path.with_suffix(".pending")
    with pending.open("wb") as target:
        target.write(b"MDOCLI1\n"+struct.pack("<I",len(encoded))+encoded)
        for row in files:
            with (stage/row["path"]).open("rb") as source: shutil.copyfileobj(source,target,1048576)
    pending.replace(path)
    return {**manifest,"file":path.name,"sha256":digest(path),"size":path.stat().st_size,"tool_id":"official."+manifest["id"],"recommended":False,"upstream_version":metadata.get("upstream_version",metadata["version"])}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage",type=Path,required=True);parser.add_argument("--output",type=Path,required=True)
    args=parser.parse_args();rows=[]
    for item in json.loads((args.stage/"input.json").read_text(encoding="utf-8")):
        stage=args.stage/item["platform"]/item["id"]
        row=package(stage,item,args.output);rows.append(row)
        print(row["file"],row["size"],flush=True)
    (args.output/"catalog.json").write_text(json.dumps({"tools":rows},ensure_ascii=False,indent=2)+"\n",encoding="utf-8")


if __name__=="__main__": main()
