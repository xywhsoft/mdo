"""Real website + native client publishing/install/update/recovery round trip."""
import base64
import argparse
import copy
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
from test_api_runtime import free_port

ROOT=Path(__file__).resolve().parents[1]
HOME=ROOT.parent/"home"
sys.path.insert(0,str(HOME/"tests"))
from test_mdo_delivery import fixture,request

def run(ui=False):
    assert (ROOT/"app/include/mdo/ecosystem_package.h").read_bytes()==(HOME/"host/xywhsoft_ai/plugin/mdo/modules/ecosystem/ecosystem_package.h").read_bytes()
    base,website,port,user,password=fixture("mdo-ecosystem-ui" if ui else "mdo-ecosystem-native")
    origin=f"http://127.0.0.1:{port}"
    (website/"db/identity.json").write_text(json.dumps({"public_origin":origin}),encoding="utf-8")
    running=[];logs=[]
    def start(exe,config,cwd,home=None):
        log=(base/("client.log" if home else "site.log")).open("wb");logs.append(log)
        env=dict(os.environ,USE_WEBVIEW="0",MDO_ORNITH_API_KEY="fixture-not-a-real-key")
        if home:env["MDO_HOME"]=str(home)
        p=subprocess.Popen([str(exe),str(config)],cwd=cwd,env=env,stdout=log,stderr=log,creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0));running.append(p);return p
    def ready(p,port,path):
        for _ in range(200):
            if p.poll() is not None:raise RuntimeError("Host exited; inspect "+str(base))
            try:
                if request(port,"GET",path)[0]==200:return
            except OSError:pass
            time.sleep(.1)
        raise RuntimeError("Host not ready; inspect "+str(base))
    def web(method,path,body=None,headers=None,cookie=None,status=200):
        actual,head,raw=request(port,method,path,body,cookie,headers);assert actual==status,(path,actual,raw)
        return json.loads(raw).get("data"),head
    try:
        server=start(HOME/"xs.exe",website/"xs.json",HOME);ready(server,port,"/mdo/catalog")
        web("POST","/api/v1/register",{"username":"eco_native","password":"Fixture-2026-password"},status=201)
        _,head=web("POST","/admin/login",{"username":user,"password":password});admin="; ".join(v.split(";",1)[0] for v in head["_cookies"])
        review,_=web("GET","/admin/api/mdo/ecosystem",cookie=admin);csrf={"X-CSRF-Token":review["csrf"]}
        isolated=Path(tempfile.mkdtemp(prefix="ecosystem-",dir=base))
        client=isolated/"native-app";shutil.copytree(ROOT/"app",client)
        unity=client/"generated/mdo_unity.c";unity.write_text("#define MDO_ACCOUNT_SERVICE_ORIGIN "+json.dumps(origin)+"\n"+unity.read_text(encoding="utf-8"),encoding="utf-8")
        app_port=free_port();home=isolated/"native-home";home.mkdir()
        config={"services":[{"class":"http","enabled":True,"name":"eco-native","ip":"127.0.0.1","port":app_port,"host_default":{"enabled":True,"name":"mdo","path":"web","devlang":"c","devfile":"generated/mdo_unity.c"}}]}
        (client/"xs.json").write_text(json.dumps(config),encoding="utf-8")
        native=start(ROOT/".build/host/xs.exe",client/"xs.json",client,home);ready(native,app_port,"/api/v1/account")
        def app(method,path,body=None,status=200):
            headers={}
            if method!="GET":headers["X-Mdo-Write-Token"]=request(app_port,"GET","/api/v1/account")[1]["X-Mdo-Write-Token"]
            actual,_,raw=request(app_port,method,"/api/v1"+path,body,headers=headers)
            assert actual==status,(path,actual,raw)
            return json.loads(raw).get("data")
        app("GET","/ecosystem",status=401)
        app("POST","/account/login",{"identifier":"eco_native","password":"Fixture-2026-password","remember":False})
        for _ in range(100):
            account=app("GET","/account")
            if account["state"]=="signed_in" and not account["busy"]:break
            time.sleep(.1)
        assert account["state"]=="signed_in",account
        def store(action,**kw):return app("POST","/ecosystem",dict(action=action,**kw),status=201 if action=="submit" else 200)
        p={"format":"mdo.extension.v1","manifest":{"slug":"native-suite","name":"Native Suite","version":"1.0.0","description":"Integration fixture","readme":"Portable source package","license":"MIT","platforms":["windows-x86_64","android-arm64-v8a"]},"resources":[
            {"kind":"commands","id":"eco-review","content":"---\ndescription: Review\n---\nReview $ARGUMENTS."},
            {"kind":"skills","id":"eco-skill","content":"---\nname: eco-skill\ndescription: Check a repository\n---\nRead references/check.md when needed.","files":[{"path":"references/check.md","base64":base64.b64encode(b"Check the build.").decode()}]},
            {"kind":"subagents","id":"eco-reviewer","content":"---\nname: Review\ndescription: Review files\ntools: [read]\n---\nReport findings with evidence."},
            {"kind":"agents","id":"eco-agent","content":"---\nname: Custom\ndescription: Read project files\ntools: [read]\n---\nInspect files carefully."},
            {"kind":"tools","id":"eco-tool","content":(ROOT/"tests/fixtures/modules/local-tool.c").read_text(encoding="utf-8")} ]}
        submitted=store("submit",package=p);identity=submitted["id"]
        assert store("catalog")["items"]==[]
        assert store("mine")["items"][0]["id"]==identity
        app("POST","/ecosystem",{"action":"install","id":identity,"trust_code":True},status=409)
        def approve(id):web("POST","/admin/api/mdo/ecosystem",{"id":id,"revision":1,"action":"approve","reason":"Fixture approval"},cookie=admin,headers=csrf)
        approve(identity)
        app("POST","/ecosystem",{"action":"install","id":identity},status=409)
        store("install",id=identity,trust_code=True)
        assert (home/"skills/eco-skill/references/check.md").read_bytes()==b"Check the build."
        assert str(identity) in app("GET","/ecosystem")
        exported=store("export",references=[{"kind":"skills","id":"eco-skill"},{"kind":"commands","id":"eco-review"}]);assert len(exported["resources"])==2 and exported["resources"][0]["files"]
        # Partial publication drafts are portable and protected by revision CAS.
        draft={"id":"fixture-draft","fields":{"slug":"draft-test","name":"","version":"1.0.0","description":"","readme":"Draft work","license":"MIT","changelog":"","platforms":[]},"references":[]}
        saved=store("draft_save",draft=draft,revision="")
        assert store("drafts")["items"][0]["id"]==draft["id"]
        loaded=store("draft_read",id=draft["id"]);assert loaded["fields"]==draft["fields"]
        draft["fields"]["readme"]="Continued work"
        updated=store("draft_save",draft=draft,revision=saved["revision"])
        app("POST","/ecosystem",{"action":"draft_save","draft":draft,"revision":saved["revision"]},status=412)
        assert store("draft_read",id=draft["id"])["fields"]["readme"]=="Continued work"
        app("POST","/ecosystem",{"action":"draft_read","id":"../outside"},status=422)
        store("draft_delete",id=draft["id"],revision=updated["revision"]);assert not store("drafts")["items"]
        # Offline files are distinct from reviewed online receipts, and use the
        # same update conflict/rollback policy. Preview/import cannot skip trust.
        app("POST","/ecosystem",{"action":"import","package":p},status=409)
        offline=copy.deepcopy(p);offline["manifest"]["slug"]="local-fixture";offline["resources"]=[{"kind":"commands","id":"eco-offline","content":"Offline $ARGUMENTS"}]
        store("import",package=offline)
        receipt=app("GET","/ecosystem")["local:local-fixture"]
        assert receipt["source"]=="local" and receipt["resources"]==[{"kind":"commands","id":"eco-offline"}]
        offline["manifest"]["version"]="1.1.0";offline["resources"][0]["content"]="Updated offline $ARGUMENTS"
        store("import",package=offline)
        (home/"commands/eco-offline.md").write_text("Local edit")
        app("POST","/ecosystem",{"action":"import","package":offline},status=409)
        store("uninstall",entry={"source":"local","slug":"local-fixture"})
        assert (home/"commands/eco-offline.md").read_text()=="Local edit"
        (home/"commands/eco-offline.md").unlink()
        p2=copy.deepcopy(p);p2["manifest"]["version"]="1.1.0";p2["resources"][0]["content"]+=" Updated."
        second=store("submit",package=p2)["id"];approve(second)
        original=(home/"commands/eco-review.md").read_text();(home/"commands/eco-review.md").write_text(original+" Local edit.")
        app("POST","/ecosystem",{"action":"install","id":second,"trust_code":True},status=409)
        assert "Local edit." in (home/"commands/eco-review.md").read_text()
        (home/"commands/eco-review.md").write_bytes(original.encode())
        bad=copy.deepcopy(p2);bad["manifest"]["version"]="broken";bad["resources"][-1]["content"]="invalid C source"
        bad_id=store("submit",package=bad)["id"];approve(bad_id)
        app("POST","/ecosystem",{"action":"install","id":bad_id,"trust_code":True},status=409)
        assert (home/"commands/eco-review.md").read_text()==original
        assert str(identity) in app("GET","/ecosystem") and not (home/"data/extensions/pending.json").exists()
        web("POST","/admin/api/mdo/ecosystem",{"id":bad_id,"revision":2,"action":"hide","reason":"Invalid fixture withdrawn"},cookie=admin,headers=csrf)
        assert store("latest",id=identity)["id"]==second
        store("install",id=second,trust_code=True);assert str(second) in app("GET","/ecosystem") and str(identity) not in app("GET","/ecosystem")
        (home/"commands/eco-review.md").write_text(original+" Local edit.")
        (home/"skills/eco-skill/references/check.md").write_bytes(b"Locally maintained checklist")
        store("uninstall",entry={"id":second})
        assert (home/"commands/eco-review.md").exists() and not (home/"tools/eco-tool.c").exists()
        assert (home/"skills/eco-skill/SKILL.md").exists() and (home/"skills/eco-skill/references/check.md").read_bytes()==b"Locally maintained checklist"
        assert not app("GET","/ecosystem")
        # Simulate power loss after a file replacement. Startup must undo it
        # before resource managers compile/load the pending extension state.
        native.terminate();native.wait(timeout=10);running.remove(native)
        target=home/"commands/recovery.md";target.write_text("Interrupted write")
        journal={"receipts":{},"files":[{"path":"commands/recovery.md","old":base64.b64encode(b"Original draft").decode(),"new":base64.b64encode(b"Interrupted write").decode()}]}
        (home/"data/extensions/pending.json").write_text(json.dumps(journal))
        native=start(ROOT/".build/host/xs.exe",client/"xs.json",client,home);ready(native,app_port,"/api/v1/account")
        assert target.read_text()=="Original draft" and not (home/"data/extensions/pending.json").exists()
        print("PASS native login, publish/review/install bundle, C trust, Skill attachments, edited-file conflict, compile rollback, update, uninstall preservation and startup recovery")
        if ui:
            app("POST","/account/login",{"identifier":"eco_native","password":"Fixture-2026-password","remember":False})
            for _ in range(100):
                if app("GET","/account")["state"]=="signed_in":break
                time.sleep(.1)
            state=ROOT/".build/ecosystem/ui.json";state.parent.mkdir(exist_ok=True,parents=True)
            state.write_text(json.dumps({"app":app_port,"website":port,"admin_user":user}),encoding="utf-8")
            print("UI fixture ready: "+str(state),flush=True)
            stop=state.with_suffix(".stop");stop.unlink(missing_ok=True)
            until=time.monotonic()+1200
            while time.monotonic()<until and not stop.exists():time.sleep(.5)
    finally:
        for p in running:
            if p.poll() is None:p.terminate();p.wait(timeout=10)
        for log in logs:log.close()

if __name__=="__main__":
    parser=argparse.ArgumentParser();parser.add_argument("--ui",action="store_true");run(parser.parse_args().ui)
