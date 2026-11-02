"""Install the actual adb ZIP through mdo's native manager and run its alias."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import uuid
from native_product_runner import product

ROOT = Path(__file__).resolve().parents[1]
CONTRACT = r'''
static void QaAdbFlag(xvalue* R,cstr K,bool V){xrtValueObjectSetNew(R,xrtStrView(K),xrtValueBool(V));}
xvalue* QaProductContract(const xvalue* P,xvfs V){
 xvalue* R=xrtValueObject();char Error[512]={0};
 bool Home=QaProductHome(SdkField(P,"root"),SdkField(P,"app"),V);
 bool Ready=Home&&MdoExtensionsInit();xvalue* Installed=NULL;
 bool Ok=Ready&&MdoExtensionsInstall("external-tools","0123456789abcdef0123456789abcdef","adb.zip",NULL,true,NULL,&Installed,Error);
 QaAdbFlag(R,"native_zip_installed",Ok);xrtValueRelease(Installed);
 bool Cli=Ok&&MdoCliInit(NULL);xvalue* Snapshot=Cli?MdoCliSnapshot():NULL;bool Valid=false;
 const xvalue* Rows=SdkGet(Snapshot,"items");
 for(size_t I=0;I<xrtValueCount(Rows);I++){
  const xvalue* Row=xrtValueArrayGet(Rows,I);cstr Id=CliText(Row,"id",160);
  if(Id&&!strcmp(Id,"external-tool:mdo.adb"))Valid=CliBool(Row,"valid",false);
 }
 QaAdbFlag(R,"tool_discovered",Valid);SdkPut(R,"binding_error",CliText(Snapshot,"binding_error",256));
 SdkPut(R,"error",Error);xrtValueRelease(Snapshot);if(Cli)MdoCliUnit();if(Ready)MdoExtensionsUnit();if(Home)MdoHomeUnit();return R;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", required=True)
    parser.add_argument("--home", type=Path, required=True)
    parser.add_argument("--project", default="default")
    args = parser.parse_args()
    out = ROOT / ".build/adb-20261103/native"
    fixture = out / ("ADB 中文 Home " + uuid.uuid4().hex[:10])
    fixture.mkdir(parents=True)
    archive = ROOT / "tools/runtime/packages/adb/adb-37.0.1-windows-x86_64.zip"
    shutil.copyfile(archive, fixture / "adb.zip")
    result = product(args, fixture, CONTRACT, out / "native-install.json")
    assert result.get("native_zip_installed") and result.get("tool_discovered"), result
    assert not result.get("binding_error"), result
    def version(root):
        entry = root / "data/external-tools/bin/adb.exe"
        response = subprocess.run([str(entry), "version"], capture_output=True, timeout=15)
        assert response.returncode == 0 and b"37.0.1-" in response.stdout, response.stderr
        return response.stdout.decode("utf-8", errors="replace")
    before = version(fixture)
    moved = fixture.with_name(fixture.name + "-moved")
    assert moved.parent.resolve() == out.resolve() and not moved.exists()
    fixture.rename(moved)
    after = version(moved)
    receipt = {"native_zip_install": True, "discovery": True, "alias_executes": True,
               "unicode_home": True, "home_move": True, "version_before": before, "version_after": after,
               "sha256": hashlib.sha256(archive.read_bytes()).hexdigest()}
    (out / "package-acceptance.json").write_text(json.dumps(receipt, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print("PASS: actual ZIP native install, tool discovery, adb alias, Unicode Home and portable Home move")


if __name__ == "__main__":
    main()
