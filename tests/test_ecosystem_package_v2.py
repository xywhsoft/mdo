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
            from mcp_template_fixture import template
            value["resources"][0]["content"] = json.dumps(template("qa-resource"))
        add(kind, value, True)
    for mode in ("key", "oauth", "stdio"):
        from mcp_template_fixture import template
        value = package("mcp"); value["resources"][0]["content"] = json.dumps(template("qa-resource", mode))
        add("mcp-" + mode, value, True)
    def invalid_template(name, mutation):
        value = package("mcp"); t = template("qa-resource", "key"); mutation(t)
        value["resources"][0]["content"] = json.dumps(t); add("mcp-" + name, value, False)
    invalid_template("unknown-key-separator", lambda t: t.update({"format||id":"private"}))
    invalid_template("inline-secret", lambda t: t["variants"][0]["config"]["transport"]["headers"][0].update(secret_ref="Bearer secret"))
    invalid_template("vault-ref", lambda t: t["variants"][0]["config"]["transport"]["headers"][0].update(secret_ref="keychain:private"))
    invalid_template("secret-default", lambda t: t["inputs"][0].update(default="private"))
    invalid_template("userinfo", lambda t: t["variants"][0]["config"]["transport"].update(endpoint="https://name:secret@example.invalid/mcp"))
    invalid_template("url-token", lambda t: t["variants"][0]["config"]["transport"].update(endpoint="https://example.invalid/mcp?token=private"))
    invalid_template("oauth-token", lambda t: t["variants"][0]["config"].update(oauth=dict(enabled=True, access_token="secret")))
    invalid_template("hook", lambda t: t.update(install="curl run.sh | sh"))
    invalid_template("extra-config", lambda t: t["variants"][0]["config"].update(secrets=dict(token="secret")))
    invalid_template("security-extra", lambda t: t["variants"][0]["config"]["security"].update(token="secret"))
    invalid_template("unknown-input", lambda t: t["variants"][0]["config"]["transport"]["headers"][0].update(secret_ref="input:unknown"))
    combo = package()
    combo["resources"] = [copy.deepcopy(c["package"]["resources"][0]) for c in cases[:9]]
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
    from knowledge_fixture import package as knowledge_package, file as knowledge_file
    kb=knowledge_package();add("knowledge-valid",kb,True)
    value=copy.deepcopy(kb);value["resources"][0]["files"]=[];add("knowledge-needs-index",value,False)
    value=copy.deepcopy(kb);value["resources"][0]["files"][0]["base64"]="AA==";add("knowledge-index-text",value,False)
    value=copy.deepcopy(kb);value["resources"][0]["files"].append(knowledge_file("INDEX.md","duplicate"));add("knowledge-case-alias",value,False)
    value=copy.deepcopy(kb);value["resources"][0]["files"].extend([knowledge_file("docs","file"),knowledge_file("docs/topic.md","child")]);add("knowledge-file-directory-alias",value,False)
    value=copy.deepcopy(kb);value["resources"][0]["files"].append(knowledge_file("Knowledge.json/topic.md","reserved"));add("knowledge-reserved-manifest",value,False)
    value=copy.deepcopy(kb);meta=json.loads(value["resources"][0]["content"]);meta["install"]="run a command";value["resources"][0]["content"]=json.dumps(meta);add("knowledge-inert-only",value,False)
    value=copy.deepcopy(kb);meta=json.loads(value["resources"][0]["content"]);meta["prompt"]="x"*8193;value["resources"][0]["content"]=json.dumps(meta);add("knowledge-prompt-limit",value,False)
    role=package("agents");role["manifest"]["min_build_id"]=70000009
    role["resources"][0]["content"]=json.dumps(dict(schema="mdo.agent.v1",name="Reviewer",mode="both",prompt="Inspect"))
    add("role-portable-config",role,True)
    def bad_role(name, mutate):
        value=copy.deepcopy(role);mutate(value);add("role-"+name,value,False)
    bad_role("wrong-mode",lambda p:p["resources"][0].update(content='{"schema":"mdo.agent.v1","mode":"subagent"}'))
    bad_role("unknown-source",lambda p:p["resources"][0].update(content='{"schema":"mdo.agent.v1","context":{"unknown":true}}'))
    bad_role("missing-program",lambda p:p["resources"][0].update(content='{"schema":"mdo.agent.v1","code":true}'))
    bad_role("missing-prompt",lambda p:p["resources"][0].update(content='{"schema":"mdo.agent.v1","prompt_file":"qa-resource/assets/a.md"}'))
    bad_role("unowned-reference",lambda p:p["resources"][0].update(content='{"schema":"mdo.agent.v1","prompt_file":"neighbor.md"}'))
    code_role=copy.deepcopy(role);code_role["resources"][0].update(content='{"schema":"mdo.agent.v1","code":true}',files=[dict(path="program.c",base64=base64.b64encode(b"/* Reviewed C source */").decode())])
    add("role-c-source",code_role,True)
    value=copy.deepcopy(code_role);value["resources"][0]["files"][0]["base64"]="AA==";add("role-binary-source",value,False)
    value=copy.deepcopy(role);value["resources"][0].update(content='{"schema":"mdo.agent.v1","prompt_file":"qa-resource/assets/a.md"}',files=[dict(path="assets/a.md",base64="SGVsbG8=")]);add("role-owned-prompt",value,True)
    value=copy.deepcopy(role);value["resources"][0]["files"]=[dict(path="assets/docs",base64="SGVsbG8="),dict(path="assets/docs/index.md",base64="SGVsbG8=")];add("role-file-directory-collision",value,False)
    value=copy.deepcopy(role);value["resources"][0]["files"]=[dict(path="program.c",base64="LyogYyAqLw==")];add("role-code-without-declaration",value,False)
    value=copy.deepcopy(role);value["resources"][0]["files"]=[dict(path="assets/large.md",base64=base64.b64encode(b"x"*131073).decode())];add("role-document-size",value,False)
    (ROOT / ".build").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="ecosystem-contract-", dir=ROOT / ".build") as temporary:
        base = Path(temporary); (base / "web").mkdir()
        (base / "cases.json").write_text(json.dumps(cases), encoding="utf-8")
        report = base / "report.json"
        (base / "ecosystem_package.h").write_bytes((ROOT / "app/include/mdo/ecosystem_package.h").read_bytes())
        (base / "mcp_template.h").write_bytes((ROOT / "app/include/mdo/mcp_template.h").read_bytes())
        (base / "knowledge_format.h").write_bytes((ROOT / "app/include/mdo/knowledge_format.h").read_bytes())
        for name in ("agent_package.h", "agent_config_format.h", "prompt_file.h", "extension_identity.h"):
            (base / name).write_bytes((ROOT / "app/include/mdo" / name).read_bytes())
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
