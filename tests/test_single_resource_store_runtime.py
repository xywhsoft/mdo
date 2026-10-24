"""Independent Skill/Agent-tool creator, review and native install round trips.

Uses two disposable real TCC hosts; no production accounts or model calls.
"""
import base64
import copy
import json
import os
from pathlib import Path
import shutil
import sqlite3
import subprocess
import sys
import tempfile
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
HOME = ROOT.parent / "home"
sys.path.insert(0, str(HOME / "tests"))
from test_mdo_delivery import fixture, request, EXE
from test_api_runtime import free_port


def run():
    base, website, port, user, password = fixture("mdo-single-resources")
    origin = f"http://127.0.0.1:{port}"
    (website / "db/identity.json").write_text(json.dumps({"public_origin": origin}))
    isolated = Path(tempfile.mkdtemp(prefix="native-", dir=base))
    app, home = isolated / "app", isolated / "home"
    shutil.copytree(ROOT / "app", app); home.mkdir()
    unity = app / "generated/mdo_unity.c"
    unity.write_text('#define MDO_ACCOUNT_SERVICE_ORIGIN ' + json.dumps(origin) + '\n' + unity.read_text(encoding="utf-8"), encoding="utf-8")
    native_port = free_port()
    (app / "xs.json").write_text(json.dumps({"services": [{"class": "http", "enabled": True, "name": "single", "ip": "127.0.0.1", "port": native_port, "host_default": {"enabled": True, "name": "mdo", "path": "web", "devlang": "c", "devfile": "generated/mdo_unity.c"}}]}))
    running, logs = [], []
    def start(exe, config, cwd, mdo_home=None):
        log = (base / ("native.log" if mdo_home else "site.log")).open("wb"); logs.append(log)
        env = dict(os.environ, USE_WEBVIEW="0")
        if mdo_home: env["MDO_HOME"] = str(mdo_home)
        p = subprocess.Popen([str(exe), str(config)], cwd=cwd, env=env, stdout=log, stderr=log, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0)); running.append(p); return p
    def ready(p, n, path):
        for _ in range(240):
            if p.poll() is not None: raise AssertionError("Fixture exited: " + str(base))
            try:
                if request(n, "GET", path)[0] == 200: return
            except OSError: pass
            time.sleep(.1)
        raise AssertionError("Fixture not ready: " + str(base))
    def web(method, path, body=None, auth=None, cookie=None, status=200):
        code, head, raw = request(port, method, path, body, cookie, auth)
        assert code == status, (path, code, raw[:1600])
        return json.loads(raw).get("data"), head
    def client(method, path, body=None, status=200):
        headers = {}
        if method != "GET": headers["X-Mdo-Write-Token"] = request(native_port, "GET", "/api/v1/account")[1]["X-Mdo-Write-Token"]
        code, _, raw = request(native_port, method, "/api/v1" + path, body, headers=headers)
        assert code == status, (path, code, raw[:1600])
        return json.loads(raw).get("data")
    def store(kind, action, status=200, **body):
        return client("POST", "/ecosystem", dict(action=action, single_kind=kind, **body), status=status)
    prefix = "/api/v1/mdo/ecosystem/"
    try:
        server = start(EXE, website / "xs.json", HOME); ready(server, port, prefix + "packages")
        web("POST", "/api/v1/register", dict(username="single_author",nickname="Account nickname",password="Fixture-2026-password"), status=201)
        login, _ = web("POST", "/api/v1/login", dict(identifier="single_author", password="Fixture-2026-password")); auth = {"Authorization": "Bearer " + login["access_token"]}
        _, head = web("POST", "/admin/login", dict(username=user, password=password)); cookie = "; ".join(v.split(";", 1)[0] for v in head["_cookies"])
        inventory, _ = web("GET", "/admin/api/mdo/ecosystem", cookie=cookie); csrf = {"X-CSRF-Token": inventory["csrf"]}
        native = start(ROOT / ".build/host/xs.exe", app / "xs.json", app, home); ready(native, native_port, "/api/v1/account")
        client("POST", "/account/login", dict(identifier="single_author", password="Fixture-2026-password", remember=False))
        for _ in range(100):
            account = client("GET", "/account")
            if account["state"] == "signed_in" and not account["busy"]: break
            time.sleep(.1)
        assert account["state"] == "signed_in"
        c = (ROOT / "tests/fixtures/modules/local-tool.c").read_text(encoding="utf-8")
        skill = dict(kind="skills", id="single-skill", content="---\nname: single-skill\ndescription: Single resource fixture\n---\nUse references/check.md. Review $ARGUMENTS.", files=[dict(path="references/check.md", base64=base64.b64encode(b"Check the source.").decode())])
        tool = dict(kind="tools", id="single-tool", content=c)
        from test_commands_api_runtime import fixture as command_source
        command = dict(kind="commands", id="single-command", content=command_source("single-command", 'mdo_result mdoCommandRun(const mdo_command_context_v1* C,const xvalue* A,xvalue** R){char* J=C->Host->Json(A);*R=C->Host->Parse(J);C->Host->Free(J);return MDO_RESULT_OK;}'))
        ids = {}
        for kind, resource in [("skills", skill), ("tools", tool), ("commands", command)]:
            p = dict(format="mdo.extension.v1", manifest=dict(slug="work-" + kind, name="Single " + kind, version="1.0.0", description="One independent resource",author="Original Developers",copyright="Copyright 2026 Original Developers",submitter="forged_username",readme="Source and build instructions", license="MIT", platforms=["windows-x86_64", "android-arm64-v8a", "linux-x86_64"]), resources=[resource])
            # Private works are hidden from all public download paths.
            private = store(kind, "submit", package=p, publish=False, status=201); identity = private["id"]
            assert not store(kind, "catalog")["items"]
            assert [r["id"] for r in store(kind, "mine")["items"]] == [identity]
            assert not store("tools" if kind == "skills" else "skills", "mine")["items"] or kind != "skills"
            web("GET", prefix + "package?id=" + str(identity), status=404)
            detail, _ = web("GET", "/api/v1/mdo/creator/detail?source=package&id=" + str(identity), auth=auth)
            web("POST", "/api/v1/mdo/creator/publish", dict(source="package", id=identity, revision=1, publish=True, review_notes="Complete source supplied. Read-only fixture with no network access."), auth=auth)
            store(kind, "install", id=identity, trust_code=True, status=409)
            web("POST", "/admin/api/mdo/ecosystem", dict(action="approve", id=identity, revision=2, reason="Inspected full source and fixture behavior"), auth=csrf, cookie=cookie)
            catalog=store(kind,"catalog")["items"][0]
            assert catalog["id"]==identity and catalog["submitter"]=="single_author" and catalog["author"]==p["manifest"]["author"] and catalog["copyright"]==p["manifest"]["copyright"] and catalog["license"]=="MIT"
            detail=store(kind,"detail",id=identity);assert detail["submitter"]=="single_author" and detail["copyright"]==catalog["copyright"]
            wrong = "tools" if kind == "skills" else "skills"
            store(wrong, "plan", id=identity, status=404)
            if kind != "skills": store(kind, "install", id=identity, status=409)
            plan = store(kind, "plan", id=identity)
            store(kind, "install", id=identity, trust_code=True, expected_plan="0" * 64, status=409)
            store(kind, "install", id=identity, trust_code=True, expected_plan=plan["sha256"])
            store(kind,"export",references=[dict(kind=kind,id=resource["id"])],status=403)
            native_path=f'/extensions/{kind}/{resource["id"]}'
            installed_item=client("GET",native_path);assert installed_item["store_managed"] and installed_item["management_known"] and installed_item["submitter"]=="single_author" and installed_item["copyright"]==catalog["copyright"]
            revision={"If-Match":'"'+installed_item["revision"]+'"'}
            def protected(method,suffix='',body=None,status=403):
                headers={**revision,"X-Mdo-Write-Token":request(native_port,"GET","/api/v1/account")[1]["X-Mdo-Write-Token"]}
                code,_,raw=request(native_port,method,"/api/v1"+native_path+suffix,body,headers=headers);assert code==status,(code,raw[:1000])
            protected("PUT",body=dict(content=resource["content"]));protected("DELETE")
            protected("POST","/enabled",dict(enabled=False),200);protected("POST","/enabled",dict(enabled=True),200)
            receipts_file=home/'data/extensions/installed.json';saved_receipts=receipts_file.read_bytes();receipts_file.write_bytes(b'broken')
            assert client("GET",native_path)["management_known"] is False
            protected("PUT",body=dict(content=resource["content"]),status=503);receipts_file.write_bytes(saved_receipts)
            legacy=json.loads(saved_receipts);legacy[str(identity)].pop("submitter",None);legacy[str(identity)]["sha256"]="0"*64;receipts_file.write_text(json.dumps(legacy))
            store(kind,"catalog");assert not client("GET",native_path)["submitter"],"mismatched receipt hashes must never borrow catalog attribution"
            legacy[str(identity)]["sha256"]=json.loads(saved_receipts)[str(identity)]["sha256"];receipts_file.write_text(json.dumps(legacy))
            store(kind,"catalog");assert client("GET",native_path)["submitter"]=="single_author"
            if kind == "skills":
                protected("GET","/bundle")
                source=next(r for r in client("GET","/skills/sources?project=default")["items"] if r["origin"]=="personal" and r["id"]==resource["id"]);assert source["store_managed"]
                client("POST","/skills/migrate?project=default",dict(origin="personal",provider="mdo",id=resource["id"],target_id="store-copy",scope="user"),status=403)
                assert (home / "skills/single-skill/references/check.md").read_bytes() == b"Check the source."
                invoked = client("POST", "/skills/invoke", dict(id="single-skill", arguments="sample")); assert "sample" in invoked["text"]
            elif kind == "commands":
                palette = client("GET", "/commands?project=default")
                assert palette["single-command"]["store_managed"] and palette["single-command"]["valid"]
                for raw, expected in [('"two words" tail', ["two words", "tail"]), ('["",true,3,{"x":[1]}]', ["", True, 3, {"x":[1]}])]:
                    invocation = "cmd-" + uuid.uuid4().hex
                    client("POST", "/command-invocations?project=default", dict(invocation_id=invocation, command_id="single-command", raw_arguments=raw, session_id="fixture"), status=202)
                    for _ in range(250):
                        result = client("GET", "/command-invocations/" + invocation)
                        if result["terminal"]: break
                        time.sleep(.03)
                    assert result["state"] == "succeeded" and result["result"] == expected, result
            else:
                assert any(t["id"] == "user.fixture" for t in client("GET", "/tools")["items"])
            # Updates remain hidden while under review and count as one work.
            update = copy.deepcopy(p); update["manifest"]["version"] = "1.1.0"; update["resources"][0]["content"] += "\n" if kind != "skills" else "\nUpdated instructions."
            pending = store(kind, "submit", package=update, publish=True, review_notes="Updated source and instructions; no added permissions.", status=201)
            assert store(kind, "latest", id=identity)["id"] == identity
            web("POST", "/admin/api/mdo/ecosystem", dict(action="approve", id=pending["id"], revision=1, reason="Updated source inspected"), auth=csrf, cookie=cookie)
            assert store(kind, "latest", id=identity)["id"] == pending["id"]
            target = home / ("tools/single-tool.c" if kind == "tools" else "commands/single-command.c" if kind == "commands" else "skills/single-skill/SKILL.md")
            original = target.read_bytes(); target.write_bytes(original + b"\n/* local edit */")
            store(kind, "install", id=pending["id"], trust_code=True, status=409)
            assert target.read_bytes().endswith(b"/* local edit */")
            target.write_bytes(original)
            store(kind, "install", id=pending["id"], trust_code=True)
            assert len(store(kind, "history", id=identity)["items"]) == 2
            receipt = client("GET", "/ecosystem")[str(pending["id"])]
            assert receipt["resources"] == [dict(kind=kind, id=resource["id"])] and receipt["submitter"]=="single_author" and receipt["manifest"]["copyright"]==p["manifest"]["copyright"]
            store(kind, "uninstall", entry=dict(id=pending["id"]))
            assert not target.exists()
            # Strict boundaries also apply to uploaded JSON and offline import.
            store(wrong, "submit", package=p, publish=False, status=422)
            combined = copy.deepcopy(p); combined["manifest"]["version"] = "2.0.0"; combined["resources"].append(tool if kind == "skills" else skill)
            store(kind, "submit", package=combined, publish=False, status=422)
            store(kind, "plan_import", package=combined, status=409)
            web("POST", prefix + "submit", dict(package=combined, kind=kind, publish=False), auth=auth, status=422)
            ids[kind] = identity
        # Project sharing exports the selected rooted Skill, including files,
        # without copying other project content or exposing the workspace path.
        project_root = home / "workspace/.mdo/skills/project-only"
        project_root.mkdir(parents=True); (project_root / "SKILL.md").write_text(skill["content"].replace("single-skill", "project-only"))
        (project_root / "references").mkdir(); (project_root / "references/check.md").write_bytes(b"Check the source.")
        shared = store("skills", "export", references=[dict(kind="skills", id="project-only", project="default")])
        assert shared["resources"][0]["id"] == "project-only" and shared["resources"][0]["files"] == skill["files"]
        store("skills", "export", references=[dict(kind="skills", id="project-only", project="../outside")], status=422)
        project_commands = home / "workspace/.mdo/commands"; project_commands.mkdir(parents=True)
        source = command["content"].replace("single-command", "project-command")
        (project_commands / "project-command.c").write_text(source, encoding="utf-8", newline="\n")
        (project_commands / "neighbor-private.txt").write_text("Never export this file")
        shared = store("commands", "export", references=[dict(kind="commands", id="project-command", project="default")])
        assert shared == dict(resources=[dict(kind="commands", id="project-command", content=source)]), shared
        store("commands", "export", references=[dict(kind="commands", id="project-command", project="../outside")], status=422)
        store("commands", "export", references=[dict(kind="skills", id="project-only", project="default")], status=422)
        store("commands", "export", references=[dict(kind="commands", id="project-command", project="default")]*2, status=422)
        # The default quota admitted three distinct works and all their versions.
        # Grant the isolated fixture creator capacity before the legacy work.
        web("POST", "/admin/api/mdo/creator/grant", dict(owner=catalog["owner"], reason="Isolated legacy interoperability fixture"), auth=csrf, cookie=cookie)
        # A previously supported combined work remains accessible through the
        # legacy catalog, but never appears in either independent store.
        from test_ecosystem_runtime import c_command
        combined = copy.deepcopy(p); combined["manifest"].update(slug="legacy-combined",version="1.0.0")
        combined["resources"] = [dict(kind="agents",id="legacy-agent",content="---\nid: legacy-agent\ndescription: Legacy agent\n---\nRead the project."), dict(kind="commands",id="legacy-command",content=c_command("legacy-command","Legacy")), skill, tool]
        legacy, _ = web("POST",prefix+"submit",dict(package=combined,kind="agents",publish=True,review_notes="Legacy combined catalog fixture, complete source included."),auth=auth,status=201)
        web("POST","/admin/api/mdo/ecosystem",dict(action="approve",id=legacy["id"],revision=1,reason="Legacy source inspected"),auth=csrf,cookie=cookie)
        for k in ids:
            assert all(r["id"] != legacy["id"] for r in store(k,"catalog")["items"])
            store(k,"detail",id=legacy["id"],status=404)
        # Active metadata is migrated exactly once without changing source bytes.
        dbfile = website / "plugin_data/mdo/cli-tools/catalog.db"
        server.terminate(); server.wait(15); running.remove(server)
        with sqlite3.connect(dbfile) as db:
            assert db.execute("SELECT count(*) FROM package WHERE resource_count=1").fetchone()[0] == 6
            db.execute("UPDATE package SET resource_count=-1,license='',copyright=''")
        server = start(EXE, website / "xs.json", HOME); ready(server, port, prefix + "packages")
        with sqlite3.connect(dbfile) as db: assert db.execute("SELECT count(*) FROM package WHERE resource_count=1").fetchone()[0] == 6
        for kind in ids:
            result, _ = web("GET", prefix + "packages?single=1&kind=" + kind); assert len(result["items"]) == 1
            assert result["items"][0]["copyright"]=="Copyright 2026 Original Developers" and result["items"][0]["license"]=="MIT" and result["items"][0]["submitter"]=="single_author"
        print("PASS independent Skill/C-tool publication, private access, review, typed lists, source download, native install, Skill invocation, C registration, editor-command argument/worker invocation and exact project export, updates, conflicts, uninstall and migration")
        print("Evidence: " + str(base))
        if "--ui" in sys.argv:
            evidence = {"app": f"http://127.0.0.1:{native_port}/#/settings/skills", "site": origin, "admin": user, "password": password}
            (base / "ui.json").write_text(json.dumps(evidence))
            print("UI fixture ready: " + str(base / "ui.json"), flush=True)
            while True: time.sleep(1)
    finally:
        for p in reversed(running):
            if p.poll() is None: p.terminate(); p.wait(15)
        for log in logs: log.close()


if __name__ == "__main__": run()
