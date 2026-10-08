#!/usr/bin/env python3
"""Stage portable Linux tools from pinned Alpine packages, without installing them.

Both glibc and musl hosts use a private musl loader and libraries. Nothing is
written to /usr, and the host's PATH/LD_LIBRARY_PATH are never modified. Run
--refresh-lock explicitly to resolve a new Alpine snapshot; normal builds only
download the immutable URLs and verify SHA-256 values in linux.lock.json.
"""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
LOCK = Path(__file__).with_name("linux.lock.json")
BASE = "https://dl-cdn.alpinelinux.org/alpine/v3.23/main/x86_64"
REQUESTED = {"busybox": "busybox", "curl": "curl", "jq": "jq",
             "openssh": "openssh-client-default", "aria2": "aria2",
             "ripgrep": "ripgrep", "7zip": "7zip", "python": "python3"}
ENTRIES = {"busybox": {"busybox": "bin/busybox"}, "curl": {"curl": "usr/bin/curl"},
           "jq": {"jq": "usr/bin/jq"}, "openssh": {n: "usr/bin/"+n for n in
           ("ssh", "scp", "sftp", "ssh-keygen", "ssh-keyscan")},
           "aria2": {"aria2c": "usr/bin/aria2c"}, "ripgrep": {"rg": "usr/bin/rg"},
           "7zip": {"7zz": "usr/bin/7z"}, "python": {"bin/python3": "usr/bin/python3"}}

def fetch(url):
    with urllib.request.urlopen(url, timeout=90) as response:
        return response.read()

def digest(data):
    return hashlib.sha256(data).hexdigest()

def refresh():
    records = {}
    for repo in ("main", "community"):
        base = BASE.replace("/main/", "/"+repo+"/")
        with tarfile.open(fileobj=io.BytesIO(fetch(base+"/APKINDEX.tar.gz"))) as tar:
            index = tar.extractfile("APKINDEX").read().decode()
        for block in index.split("\n\n"):
            item = dict(line.split(":", 1) for line in block.splitlines() if ":" in line)
            if "P" in item:
                item["base"] = base; records[item["P"]] = item
    providers = {}
    for name, item in records.items():
        for provided in item.get("p", "").split():
            providers[provided.split("=", 1)[0]] = name
    chosen = {}
    def resolve(name):
        name = name.split("=", 1)[0].split(">", 1)[0].split("<", 1)[0].split("~", 1)[0]
        if name.startswith("!"): return
        name = name if name in records else providers.get(name, name)
        if name in chosen: return
        item = records[name]; chosen[name] = item
        for dep in item.get("D", "").split(): resolve(dep)
    for name in REQUESTED.values(): resolve(name)
    rows = []
    cache = ROOT / "sources/linux-apk"; cache.mkdir(parents=True, exist_ok=True)
    for name, item in sorted(chosen.items()):
        file = name+"-"+item["V"]+".apk"; url = item["base"]+"/"+file
        data = fetch(url); (cache/file).write_bytes(data)
        rows.append(dict(name=name, version=item["V"], file=file, url=url,
                         sha256=digest(data), size=len(data), origin=item.get("o", name),
                         license=item.get("L", ""), source_commit=item.get("c", "")))
        print("Pinned", file, flush=True)
    LOCK.write_text(json.dumps(dict(platform="linux-x86_64", alpine="3.23",
                    requested=REQUESTED, packages=rows), indent=2)+"\n", encoding="utf-8")

def stage():
    lock = json.loads(LOCK.read_text(encoding="utf-8"))
    cache = ROOT / "sources/linux-apk"; cache.mkdir(parents=True, exist_ok=True)
    output = ROOT / "linux-x86_64"
    if output.resolve() != (ROOT.resolve()/"linux-x86_64"): raise ValueError("Invalid stage root")
    if output.exists(): shutil.rmtree(output)
    payload = output / ".runtime"; payload.mkdir(parents=True)
    links = {}
    for item in lock["packages"]:
        file = cache/item["file"]
        if not file.exists(): file.write_bytes(fetch(item["url"]))
        data = file.read_bytes()
        if len(data)!=item["size"] or digest(data)!=item["sha256"]:
            raise ValueError("Package checksum mismatch: "+file.name)
        # APK v2 concatenates signature, control and payload tar streams.
        with tarfile.open(fileobj=io.BytesIO(data), ignore_zeros=True) as tar:
            for member in tar:
                path = PurePosixPath(member.name)
                if path.is_absolute() or ".." in path.parts: raise ValueError("Unsafe APK member")
                if not path.parts or path.parts[0].startswith("."): continue
                target = payload.joinpath(*path.parts)
                if member.isfile():
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(tar.extractfile(member).read())
                    target.chmod(0o755 if member.mode & 0o111 else 0o644)
                elif member.issym() or member.islnk():
                    links[path.as_posix()] = (member.linkname, member.islnk())
    def resolve(name, seen=None):
        seen = set() if seen is None else seen
        if name in seen: raise ValueError("APK link cycle")
        seen.add(name)
        if name in links:
            link, hard = links[name]
            relative = PurePosixPath(link.lstrip("/")) if hard or link.startswith("/") else PurePosixPath(name).parent/link
            parts = []
            for part in relative.parts:
                if part == "..":
                    if not parts: raise ValueError("APK link escapes root")
                    parts.pop()
                elif part != ".": parts.append(part)
            return resolve("/".join(parts), seen)
        return payload/name
    # Materialize links: XRTPACK installs ordinary files only, including libraries.
    needed = {value for entries in ENTRIES.values() for value in entries.values()}
    for name in links:
        if not (name in needed or name.startswith(("lib/", "usr/lib/", "etc/ssl/"))): continue
        source = resolve(name); target = payload/name
        if source.is_file():
            target.parent.mkdir(parents=True, exist_ok=True); shutil.copyfile(source, target); target.chmod(source.stat().st_mode & 0o777)
    # Package scripts, service configs and documentation are not installed into the host.
    for directory in ("etc/init.d", "etc/conf.d", "usr/share/man", "usr/share/doc", "usr/lib/python3.12/test"):
        target = payload/directory
        if target.exists(): shutil.rmtree(target)
    catalog = {"tools": []}
    versions = {p["name"]:p for p in lock["packages"]}
    for tool, entries in ENTRIES.items():
        directory = output/tool; directory.mkdir()
        for name, binary in entries.items():
            if not (payload/binary).is_file(): raise FileNotFoundError(binary)
            script = directory/name; script.parent.mkdir(parents=True, exist_ok=True)
            prefix = "../.." if "/" in name else ".."
            body = '#!/bin/sh\nset -eu\nroot=$(CDPATH= cd -P "${0%/*}/'+prefix+'/.runtime" && pwd)\n'
            body += 'export SSL_CERT_FILE="$root/etc/ssl/cert.pem"\n'
            if tool == "python": body += 'export PYTHONHOME="$root/usr" PYTHONDONTWRITEBYTECODE=1\n'
            body += 'exec "$root/lib/ld-musl-x86_64.so.1" --library-path "$root/lib:$root/usr/lib" "$root/'+binary+'" "$@"\n'
            script.write_text(body, encoding="utf-8", newline="\n"); script.chmod(0o755)
        package = versions[REQUESTED[tool]]
        repo = "community" if "/community/" in package["url"] else "main"
        source_url = "https://gitlab.alpinelinux.org/alpine/aports/-/tree/"+package["source_commit"]+"/"+repo+"/"+package["origin"]
        catalog["tools"].append(dict(id=tool, platform=lock["platform"], version=package["version"],
                        entrypoints=list(entries), source_url=source_url, license=package["license"],
                        unpacked_bytes=sum((payload/value).stat().st_size for value in entries.values())))
    (output/"provenance.json").write_text(json.dumps(lock, indent=2)+"\n", encoding="utf-8")
    (output/"catalog.json").write_text(json.dumps(catalog, indent=2)+"\n", encoding="utf-8")
    print("Staged portable Linux tools in", output)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__); parser.add_argument("--refresh-lock", action="store_true")
    args = parser.parse_args()
    if args.refresh_lock: refresh()
    stage()
