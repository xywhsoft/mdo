#!/usr/bin/env python3
"""Verify eight root products and stage a publication manifest without networking."""
import hashlib
import argparse
import json
from pathlib import Path
import zipfile
import re
import shutil
import subprocess
import tempfile
from version_code import release_code
ROOT=Path(__file__).resolve().parents[1];OUTPUT=ROOT/".build/releases"

def require(condition, message):
    # Release checks must still run under python -O.
    if not condition:
        raise ValueError(message)

def application_fingerprint():
    files = {p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
             for p in sorted((ROOT / "app").rglob("*"))
             if p.is_file() and "generated" not in p.relative_to(ROOT / "app").parts}
    return hashlib.sha256(json.dumps(files, sort_keys=True).encode()).hexdigest()

def verify_record(record, release, lock, source_hash, profile_hash):
    require(record.get("version_name") == release["version_name"], "stale release version in build record")
    if "version_code" in release:
        require(record.get("version_code") == release_code(release), "stale release version code in build record")
    require(record.get("xs_commit") == lock["xserver"]["commit"], "stale native dependency in build record")
    require(record.get("xrt_commit") == lock["xrt"]["commit"], "stale runtime dependency in build record")
    require(record.get("application_source_sha256") == source_hash, "application changed after the recorded build")
    require(record.get("host_profile_sha256") == profile_hash, "host profile changed after the recorded build")
    require(record.get("toolpack_revision") == release["toolpack_revision"], "stale toolpack revision in build record")
    names = [row.get("filename") for row in record.get("products", [])]
    require(len(names) == len(TARGETS) and set(names) == {row[3] for row in TARGETS}, "build record must cover all eight products exactly once")

TARGETS = [
    ("desktop", "windows-x86_64", "desktop", "mdo.exe", "windows_build_id"),
    ("windows-server", "windows-x86_64", "server", "mdo-server.exe", "windows_server_build_id"),
    *[(f"linux-{libc}-{kind}", f"linux-x86_64-{libc}", "desktop" if kind == "gui" else "server",
       f"{'mdo' if kind == 'gui' else 'mdo-server'}-linux-x86_64-{libc}", f"linux_{libc}_{kind}_build_id")
      for libc in ("glibc", "musl") for kind in ("gui", "server")],
    ("lite", "android-arm64-v8a", "lite", "mdo-arm64-v8a.apk", "android_lite_build_id"),
    ("full", "android-arm64-v8a", "full", "mdo-full-arm64-v8a.apk", "android_full_build_id"),
]
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--notes-file',type=Path,help='UTF-8 public update notes for all eight channels')
    args=parser.parse_args()
    release=json.loads((ROOT/"app/release.json").read_text(encoding="utf-8"));rows=[]
    notes=args.notes_file.read_text(encoding='utf-8').strip() if args.notes_file else f"mdo {release['version_name']} 更新。完整说明请参阅官网下载页面。"
    require(notes and len(notes.encode('utf-8'))<=4096,'update notes must contain 1 to 4096 UTF-8 bytes')
    code=release_code(release)
    record=json.loads((ROOT/"mdo-builds.json").read_text(encoding="utf-8"))
    lock=json.loads((ROOT/"deps.lock").read_text(encoding="utf-8"))
    verify_record(record, release, lock, application_fingerprint(),
                  hashlib.sha256((ROOT / "tools/host-profile.json").read_bytes()).hexdigest())
    records={p["filename"]:p for p in record["products"]}
    ids=[release[t[4]] for t in TARGETS]
    require(len(set(ids))==8 and all(type(i) is int and 10000000<=i<=99999999 for i in ids), "invalid or duplicate build IDs")
    OUTPUT.mkdir(parents=True,exist_ok=True)
    for id,platform,edition,name,key in TARGETS:
        file=ROOT/name;data=file.read_bytes();sha=hashlib.sha256(data).hexdigest();record=records[name]
        require((len(data),sha,release[key])==(record["bytes"],record["sha256"],record["build_id"]), "stale or changed product: " + name)
        require(record.get("xs_commit") == lock["xserver"]["commit"], "stale product dependency: " + name)
        if platform.startswith("android-"):
            with zipfile.ZipFile(file) as apk:
                metadata=json.loads(apk.read("assets/mdo-runtime.json"))
                require(int(metadata["build_id"])==release[key] and metadata["edition"]==edition, "APK edition or build ID mismatch: " + name)
                require(metadata.get("version_code")==code, "APK release version code mismatch: " + name)
                require(hashlib.sha256(apk.read("lib/arm64-v8a/libxs.so")).hexdigest() == record["host_sha256"], "APK native host mismatch: " + name)
        else:
            require(data[:2]==b"MZ" if platform.startswith("windows-") else data[:6]==b"\x7fELF\x02\x01", "invalid executable: " + name)
            require(data[-32:-24]==b"XRTPEND\0", "missing pack: " + name)
            require(hashlib.sha256(data[:record["host_bytes"]]).hexdigest() == record["host_sha256"], "packed native host mismatch: " + name)
            with tempfile.TemporaryDirectory(prefix="verify-release-",dir=OUTPUT) as folder:
                subprocess.run([str(ROOT/".build/host/xs.exe"),"pack","--extract",str(file),"-d",folder],check=True)
                unity=(Path(folder)/"generated/mdo_unity.c").read_text(encoding="utf-8")
                require(int(re.search(r"^#define MDO_BUILD_ID (\d+)u",unity,re.M)[1])==release[key], "embedded build ID mismatch: " + name)
                require(f'#define MDO_VERSION_CODE 0x{code}u' in unity, "embedded release version code mismatch: " + name)
                require(f'#define MDO_PRODUCT_EDITION "{edition}"' in unity, "embedded edition mismatch: " + name)
                require(f'#define MDO_BUILD_XSERVER_COMMIT "{lock["xserver"]["commit"]}"' in unity, "embedded dependency mismatch: " + name)
        shutil.copyfile(file,OUTPUT/name)
        row={"id":id,"platform":platform,"edition":edition,"build_id":release[key],"sha256":sha,"size":len(data),"notes":notes,"required":False,"status":"staged","file":name}
        row["version_code"]=code;row["version_name"]=release["version_name"]
        if edition=="full":row["toolpack_revision"]=release["toolpack_revision"]
        rows.append(row)
    result={"version_name":release["version_name"],"version_code":code,"status":"verified-not-published","releases":rows,
            "android_release_signed": all(records[t[3]].get("release_signed", False) for t in TARGETS if t[1].startswith("android-"))}
    (OUTPUT/"manifest.json").write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding="utf-8")
    print(OUTPUT/"manifest.json")
if __name__=="__main__":main()
