#!/usr/bin/env python3
"""Write a reviewable local publication manifest; does not publish anything."""
import hashlib
import json
from pathlib import Path
import zipfile
ROOT=Path(__file__).resolve().parents[1];OUTPUT=ROOT/".build/releases"
def main():
    release=json.loads((ROOT/"app/release.json").read_text());rows=[]
    for platform,edition,name,key in [("windows-x86_64","desktop","mdo-windows-x64.exe","windows_build_id"),("android-arm64-v8a","lite","mdo-android-lite-arm64-v8a.apk","android_lite_build_id"),("android-arm64-v8a","full","mdo-android-full-arm64-v8a.apk","android_full_build_id")]:
        file=OUTPUT/name;data=file.read_bytes()
        if edition!="desktop":
            with zipfile.ZipFile(file) as apk:
                metadata=json.loads(apk.read("assets/mdo-runtime.json"));assert int(metadata["build_id"])==release[key] and metadata["edition"]==edition
        else:assert data[:2]==b"MZ" and data[-32:-24]==b"XRTPEND\0"
        row={"id":edition,"platform":platform,"edition":edition,"build_id":release[key],"sha256":hashlib.sha256(data).hexdigest(),"size":len(data),"notes":release["version_name"],"required":False,"file":name}
        if edition=="full":row["toolpack_revision"]=release["toolpack_revision"]
        rows.append(row)
    packages=json.loads((OUTPUT/"toolpacks/catalog.json").read_text())["toolpacks"]
    for row in packages:row["file"]="toolpacks/"+row["file"]
    result={"version_name":release["version_name"],"status":"built-isolated-tested-not-published","releases":rows,"toolpacks":packages}
    (OUTPUT/"manifest.json").write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding="utf-8")
    print(OUTPUT/"manifest.json")
if __name__=="__main__":main()
