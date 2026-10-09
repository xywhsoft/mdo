#!/usr/bin/env python3
"""Verify eight root products and stage a publication manifest without networking."""
import hashlib
import json
from pathlib import Path
import zipfile
import re
import shutil
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1];OUTPUT=ROOT/".build/releases"

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
    release=json.loads((ROOT/"app/release.json").read_text(encoding="utf-8"));rows=[]
    records={p["filename"]:p for p in json.loads((ROOT/"mdo-builds.json").read_text(encoding="utf-8"))["products"]}
    ids=[release[t[4]] for t in TARGETS]
    assert len(set(ids))==8 and all(type(i) is int and 10000000<=i<=99999999 for i in ids)
    OUTPUT.mkdir(parents=True,exist_ok=True)
    for id,platform,edition,name,key in TARGETS:
        file=ROOT/name;data=file.read_bytes();sha=hashlib.sha256(data).hexdigest();record=records[name]
        assert (len(data),sha,release[key])==(record["bytes"],record["sha256"],record["build_id"]),name
        if platform.startswith("android-"):
            with zipfile.ZipFile(file) as apk:
                metadata=json.loads(apk.read("assets/mdo-runtime.json"));assert int(metadata["build_id"])==release[key] and metadata["edition"]==edition
        else:
            assert (data[:2]==b"MZ" if platform.startswith("windows-") else data[:6]==b"\x7fELF\x02\x01"),name
            assert data[-32:-24]==b"XRTPEND\0",name
            with tempfile.TemporaryDirectory(prefix="verify-release-",dir=OUTPUT) as folder:
                subprocess.run([str(ROOT/".build/host/xs.exe"),"pack","--extract",str(file),"-d",folder],check=True)
                unity=(Path(folder)/"generated/mdo_unity.c").read_text(encoding="utf-8")
                assert int(re.search(r"^#define MDO_BUILD_ID (\d+)u",unity,re.M)[1])==release[key],name
                assert f'#define MDO_PRODUCT_EDITION "{edition}"' in unity,name
        shutil.copyfile(file,OUTPUT/name)
        row={"id":id,"platform":platform,"edition":edition,"build_id":release[key],"sha256":sha,"size":len(data),"notes":"2026-10-09：同步最新 xs；优化会话与远程同步；支持八种发行版独立在线更新。","required":False,"status":"published","file":name}
        if edition=="full":row["toolpack_revision"]=release["toolpack_revision"]
        rows.append(row)
    result={"version_name":release["version_name"],"status":"verified-not-published","releases":rows}
    if (OUTPUT/"toolpacks/catalog.json").exists():
        packages=json.loads((OUTPUT/"toolpacks/catalog.json").read_text(encoding="utf-8"))["toolpacks"]
        for row in packages:row["file"]="toolpacks/"+row["file"]
        result["toolpacks"]=packages
    (OUTPUT/"manifest.json").write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding="utf-8")
    print(OUTPUT/"manifest.json")
if __name__=="__main__":main()
