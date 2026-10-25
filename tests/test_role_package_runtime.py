"""Real TCC role export, dependency plans, C activation and atomic rollback."""
import argparse
import base64
import copy
import json
import os
from pathlib import Path
import subprocess
import tempfile

from test_api_runtime import ROOT, free_port, request, wait_ready, write_site


def package(kind="agents", identity="portable-reviewer", code=False):
    config = dict(schema="mdo.agent.v1", name="Portable reviewer", description="Review a local project",
                  mode="both", tools=["read"], prompt_file=identity + "/assets/review.md",
                  context={"custom.guide": dict(file=identity + "/assets/guide.md")})
    files = [dict(path="assets/" + name, base64=base64.b64encode(text.encode()).decode())
             for name, text in [("review.md", "PORTABLE_REVIEW_PROMPT"), ("guide.md", "PORTABLE_GUIDE")]]
    if code:
        config["code"] = True
        source = (ROOT / "examples/agents/code-reviewer.c").read_text(encoding="utf-8")
        source = source.replace("agent.code-reviewer", ("agent." if kind == "agents" else "subagent.") + identity)
        files.append(dict(path="program.c", base64=base64.b64encode(source.encode()).decode()))
    return dict(format="mdo.extension.v2", manifest=dict(slug=identity, name="Role fixture", version="1.0.0",
                description="Portable role fixture", readme="Inspect this role before use", license="MIT",
                platforms=["windows-x86_64", "linux-x86_64"], min_build_id=70000009),
                resources=[dict(kind=kind, id=identity, content=json.dumps(config), files=files)])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, default=ROOT / ".build/host/xs.exe")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="role-package-", dir=ROOT / ".build") as raw:
        base = Path(raw); port = free_port(); config = write_site(base, port); home = base / "home"; home.mkdir()
        program=home/"modules/agents/standalone.c";program.parent.mkdir(parents=True)
        source=(ROOT/"examples/agents/code-reviewer.c").read_text(encoding="utf-8").replace("agent.code-reviewer","agent.standalone")
        program.write_text(source,encoding="utf-8",newline="\n")
        def app(method, path, body=None, status=200, revision=None):
            headers = {}
            if revision: headers["If-Match"] = '"' + revision + '"'
            actual, _, text = request(port, method, "/api/v1" + path,
                body=json.dumps(body).encode() if body is not None else None,
                headers={**headers, **({"Content-Type": "application/json"} if body is not None else {})})
            result = json.loads(text); assert actual == status, (path, actual, result)
            return result.get("data")
        def store(action, p, status=200, **kw):
            return app("POST", "/ecosystem", dict(action=action, single_kind=p["resources"][0]["kind"], package=p, **kw), status)
        with (base / "host.log").open("wb") as log:
            proc = subprocess.Popen([str(args.host.resolve()), str(config), "--", "--home", str(home)], cwd=base,
                env=dict(os.environ, USE_WEBVIEW="0", MDO_BUILD_ID="70000009"), stdout=log, stderr=log,
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            try:
                wait_ready(port, proc)
                standalone=app("GET","/extensions/agents/standalone")
                assert standalone["code_only"] and standalone["valid"] and standalone["program_source"]==source,standalone
                assert any(i["id"]=="standalone" for i in app("GET","/extensions/agents")["items"])
                exported=app("POST","/ecosystem",dict(action="export",single_kind="agents",references=[dict(kind="agents",id="standalone")]))["resources"][0]
                assert json.loads(exported["content"])["code"] and exported["files"][0]["path"]=="program.c"
                app("POST","/extensions/agents/standalone/enabled",dict(enabled=False),revision=standalone["revision"])
                assert not app("GET","/extensions/agents/standalone")["shareable"]
                app("POST","/extensions/agents/standalone/enabled",dict(enabled=True),revision=standalone["revision"])
                app("DELETE","/extensions/agents/standalone",revision=standalone["revision"])
                assert not program.exists()
                for kind in ("agents", "subagents"):
                    p = package(kind, "portable-" + kind, True); role = p["resources"][0]; identity = role["id"]
                    plan = store("plan_import", p)
                    assert plan["can_install"] and len(plan["files"]) == 4
                    assert any(f["path"] == f"modules/{kind}/{identity}.c" for f in plan["files"])
                    assert not (home / kind / (identity + ".md")).exists()
                    store("import", p, status=409)
                    store("import", p, trust_code=True, expected_plan=plan["sha256"])
                    item = app("GET", f"/extensions/{kind}/{identity}")
                    assert item["valid"] and item["name"] == "Portable reviewer"
                    preview = app("GET", f"/extensions/{kind}/{identity}/plan")
                    assert "PORTABLE_REVIEW_PROMPT" in json.dumps(preview)
                    exported = app("POST", "/ecosystem", dict(action="export", single_kind=kind,
                        references=[dict(kind=kind, id=identity)]))["resources"][0]
                    assert exported["files"] == role["files"] and json.loads(exported["content"])["schema"] == "mdo.agent.v1"
                    assert "config/account" not in json.dumps(exported) and str(home) not in json.dumps(exported)
                    before = (home / "data/extensions/installed.json").read_bytes()
                    broken = copy.deepcopy(p); broken["manifest"]["version"] = "1.1.0"
                    broken["resources"][0]["files"][-1]["base64"] = base64.b64encode(b"INVALID C SOURCE").decode()
                    store("import", broken, status=409, trust_code=True)
                    assert (home / "data/extensions/installed.json").read_bytes() == before
                    assert not (home / "data/extensions/pending.json").exists()
                    app("POST", f"/extensions/{kind}/{identity}/enabled", dict(enabled=False), revision=item["revision"])
                    update = copy.deepcopy(p); update["manifest"]["version"] = "1.1.0"
                    store("import", update, trust_code=True)
                    assert not app("GET", f"/extensions/{kind}/{identity}")["enabled"]
                    # Editing any companion retains the complete role on uninstall.
                    asset = home / kind / identity / "assets/guide.md"; asset.write_text("User edits")
                    app("POST", "/ecosystem", dict(action="uninstall", entry=dict(source="local", slug=identity)))
                    assert asset.read_text() == "User edits" and (home / kind / (identity + ".md")).exists()
                    assert (home / "modules" / kind / (identity + ".c")).exists()
                retained=app("GET","/extensions/agents/portable-agents")
                app("POST","/extensions/agents/portable-agents/enabled",dict(enabled=True),revision=retained["revision"])
                selectors=package(identity="selectors");sd=json.loads(selectors["resources"][0]["content"])
                sd.update(delegation=dict(allow=["agent.portable-agents"]))
                sd["context"].update(knowledge=dict(include=["xrt*"]),external_tools=dict(include=["git"]))
                selectors["resources"][0]["content"]=json.dumps(sd);sp=store("plan_import",selectors)
                assert sp["can_install"] and next(d for d in sp["dependencies"] if d["kind"]=="roles")["available"]
                assert all(not d["checked"] for d in sp["dependencies"] if d["kind"] in ("knowledge","external-tools"))
                p = package(identity="missing-deps"); d = json.loads(p["resources"][0]["content"])
                d.update(extends="agent.absent", skills=["absent-skill"], model="absent-model", mcp_servers=["absent-mcp"])
                p["resources"][0]["content"] = json.dumps(d)
                plan = store("plan_import", p)
                assert not plan["can_install"] and len(plan["dependencies"]) == 4
                assert all(not r["available"] and r["required"] for r in plan["dependencies"])
                store("import", p, trust_code=True, status=409)
                assert not (home / "agents/missing-deps.md").exists()
                old = package(); old["manifest"]["min_build_id"] = 60000016
                store("plan_import", old, status=409)
                future = package(); future["manifest"]["min_build_id"] = 70000010
                store("plan_import", future, status=409)
            finally:
                proc.terminate(); proc.wait(15)
    print("PASS portable Agent/SubAgent C + assets, preview, trust, dependencies, rollback, disabled updates and complete-resource preservation")


if __name__ == "__main__": main()
