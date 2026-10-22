"""Nine-system package validation in the real XS/TCC runtime, without activation."""
import argparse
import base64
import copy
import json
import os
from pathlib import Path
import subprocess
import socket
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def package(kind="commands", version="mdo.extension.v2"):
    resource = {"kind": kind, "id": "qa-resource", "content": "fixture source"}
    manifest = {"slug": "qa-package", "name": "QA package", "version": "1.0.0",
                "description": "Offline package contract", "readme": "Read before installing",
                "license": "MIT", "platforms": ["windows-x86_64"], "min_build_id": 30000126}
    if kind in ("plugins", "toolpacks"):
        resource["files"] = [{"path": "web/qa.js" if kind == "plugins" else "bin/qa.py",
                              "base64": base64.b64encode(b"fixture payload").decode()}]
        resource["content"] = json.dumps({"frontend": "web/qa.js"} if kind == "plugins" else {
            "prompt": "Use the listed program", "programs": [
                {"name": "qa", "path": "bin/qa.py", "description": "QA script"}]})
    return {"format": version, "manifest": manifest, "resources": [resource]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, default=ROOT / ".build/host/xs.exe")
    args = parser.parse_args()
    cases = []

    def add(name, value, accepted):
        cases.append({"name": name, "package": value, "accepted": accepted})

    for kind in ("agents", "subagents", "tools", "skills", "mcp", "commands", "modules", "toolpacks", "plugins"):
        value = package(kind)
        if kind == "mcp":
            value["resources"][0]["content"] = '{"transport":{"type":"http","url":"https://example.invalid/mcp"}}'
        add(kind, value, True)
    combo = package()
    combo["resources"] = [copy.deepcopy(c["package"]["resources"][0]) for c in cases]
    add("all-nine-combined", combo, True)
    add("legacy", package(version="mdo.extension.v1"), True)
    add("v2-resource-in-v1", package("plugins", "mdo.extension.v1"), False)
    value = package(); value["manifest"]["min_build_id"] = 1
    add("old-client", value, False)
    for version in ("1.01.0", "1.2", "4294967296.0.0", "1.2.3.4"):
        value = package(); value["manifest"]["version"] = version
        add("version-" + version, value, False)
    value = package("plugins"); value["resources"][0]["files"][0]["path"] = "../core.c"
    add("plugin-traversal", value, False)
    value = package("plugins"); value["resources"][0]["content"] = '{"frontend":"web/missing.js"}'
    add("missing-frontend", value, False)
    value = package("toolpacks"); value["resources"][0]["content"] = '{"prompt":"test","programs":[{"name":"test"}]}'
    add("missing-program-path-does-not-crash", value, False)
    value = package("toolpacks"); value["resources"][0]["files"] *= 2
    add("duplicate-file", value, False)
    value = package("toolpacks"); value["resources"][0]["content"] = json.dumps({"prompt": "test", "programs": [{"name": "test", "path": "../bin/test", "description": "test"}]})
    add("program-traversal", value, False)
    (ROOT / ".build").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="ecosystem-contract-", dir=ROOT / ".build") as temporary:
        base = Path(temporary); (base / "web").mkdir()
        (base / "cases.json").write_text(json.dumps(cases), encoding="utf-8")
        report = base / "report.json"
        (base / "ecosystem_package.h").write_bytes((ROOT / "app/include/mdo/ecosystem_package.h").read_bytes())
        source = '#include <xsbase.h>\n#include "ecosystem_package.h"\n' + r'''
void ServiceInit(XS_HostInfo* Host) {
    (void)Host; size_t length=0;bytes raw=xsAppReadAll("cases.json",&length);
    xvalue* cases=raw?xrtJsonParse(xrtStrViewN((cstr)raw,length)):NULL;
    xvalue* failures=xrtValueArray();char error[256];
    for(size_t i=0;i<xrtValueCount(cases);i++) {
        const xvalue* item=xrtValueArrayGet(cases,i);bool expected=false;
        xrtValueGetBool(xrtValueObjectGet(item,XRT_STR_LITERAL("accepted")),&expected);
        bool actual=MdoPackageValidate(xrtValueObjectGet(item,XRT_STR_LITERAL("package")),error,sizeof(error));
        if(actual!=expected)xrtValueArrayAppend(failures,xrtValueObjectGet(item,XRT_STR_LITERAL("name")));
    }
    if(MdoPackageVersionCompare("1.10.0","1.9.0")<=0 || strcmp(MdoPackageSystem("c-agents"),"agents"))
        xrtValueArrayAppendNew(failures,xrtValueString(XRT_STR_LITERAL("version-or-system")));
    xvalue* result=xrtValueObject();xrtValueObjectSetNew(result,XRT_STR_LITERAL("count"),xrtValueUInt(xrtValueCount(cases)));
    xrtValueObjectSetNew(result,XRT_STR_LITERAL("failures"),failures);
    char* text=xrtJsonStringify(result,false,&length);
    if(text)xrtFileWriteAtomic(REPORT_PATH,(xbytesview){(cbytes)text,length});
    xrtFree(text);xrtFree(raw);xrtValueRelease(cases);xrtValueRelease(result);
}
XS_RequestResult RequestProc(XS_HttpReq* Request){(void)Request;return XS_FALLBACK;}
'''
        source = '#define REPORT_PATH ' + json.dumps(report.as_posix()) + '\n' + source
        (base / "validator.c").write_text(source, encoding="utf-8")
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0)); port = listener.getsockname()[1]
        config = {"services": [{"enabled": True, "class": "http", "name": "validator", "ip": "127.0.0.1", "port": port,
                    "host_default": {"enabled": True, "name": "validator", "path": "web", "devlang": "c", "devfile": "validator.c"}}]}
        (base / "xs.json").write_text(json.dumps(config), encoding="utf-8")
        with (base / "host.log").open("wb") as output:
            child = subprocess.Popen([str(args.host.resolve()), str(base / "xs.json")], cwd=base,
                                     stdout=output, stderr=output, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            try:
                deadline = time.monotonic() + 12
                while not report.exists() and child.poll() is None and time.monotonic() < deadline:
                    time.sleep(.05)
                assert report.exists(), (base / "host.log").read_text(errors="replace")
                proof = json.loads(report.read_text())
                assert proof["count"] == len(cases) and proof["failures"] == [], proof
            finally:
                child.terminate(); child.wait(timeout=8)
    print(f"PASS {len(cases)} real native package checks, all nine systems, combined package and invalid paths/versions")


if __name__ == "__main__":
    main()
