"""Two disposable real hosts: role publishing/review/download and model delegation."""
import argparse
import base64
import copy
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import threading
import time

from test_api_runtime import ROOT, free_port
from test_role_package_runtime import package
HOME = ROOT.parent / "home"
sys.path.insert(0, str(HOME / "tests"))
from test_mdo_delivery import EXE, fixture, request


class Model(BaseHTTPRequestHandler):
    calls = []
    def log_message(self, *args): pass
    def do_POST(self):
        p = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        type(self).calls.append(p)
        text = json.dumps(p)
        child = "ONLINE_CHILD_MARKER" in text
        done = any(r.get("type") == "function_call_output" for r in p.get("input", []))
        output = ([dict(type="message", role="assistant", content=[dict(type="output_text", text="CHILD_OK" if child else "PARENT_OK")])]
                  if child or done else [dict(type="function_call", call_id="online-delegation", name="agent",
                    arguments=json.dumps(dict(name="subagent.online-child", prompt="Review the fixture task")))])
        body = json.dumps(dict(id="role-" + str(len(type(self).calls)), model="fixture", status="completed",
            output=output, usage=dict(input_tokens=30, output_tokens=10, total_tokens=40))).encode()
        self.send_response(200); self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body))); self.end_headers(); self.wfile.write(body)


def run():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, default=ROOT / ".build/host/xs.exe")
    parser.add_argument("--website-host", type=Path, default=EXE)
    parser.add_argument("--ui", action="store_true")
    args = parser.parse_args()
    base, website, port, user, password = fixture("mdo-role-stores")
    origin = f"http://127.0.0.1:{port}"
    (website / "db/identity.json").write_text(json.dumps(dict(public_origin=origin)), encoding="utf-8")
    isolated = Path(tempfile.mkdtemp(prefix="native-", dir=base)); app = isolated / "app"; home = isolated / "home"
    shutil.copytree(ROOT / "app", app); home.mkdir()
    unity = app / "generated/mdo_unity.c"
    unity.write_text('#define MDO_ACCOUNT_SERVICE_ORIGIN ' + json.dumps(origin) + '\n' + unity.read_text(encoding="utf-8"), encoding="utf-8")
    native_port = free_port()
    (app / "xs.json").write_text(json.dumps(dict(services=[dict(enabled=True,name="roles",ip="127.0.0.1",port=native_port,
        **{"class":"http"},host_default=dict(enabled=True,name="mdo",path="web",devlang="c",devfile="generated/mdo_unity.c"))])))
    processes, logs = [], []
    model = ThreadingHTTPServer(("127.0.0.1", 0), Model); thread = threading.Thread(target=model.serve_forever, daemon=True); thread.start()
    def start(host, config, cwd, mdo_home=None):
        log = (base / ("native.log" if mdo_home else "site.log")).open("wb"); logs.append(log)
        env = dict(os.environ, USE_WEBVIEW="0", MDO_ROLE_TEST_KEY="fixture-not-a-secret")
        if mdo_home: env.update(MDO_HOME=str(mdo_home), MDO_BUILD_ID="70000001")
        p = subprocess.Popen([str(host.resolve()), str(config)], cwd=cwd, env=env, stdout=log, stderr=log,
            creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0)); processes.append(p); return p
    def ready(p, target, path):
        for _ in range(300):
            assert p.poll() is None, str(base)
            try:
                if request(target, "GET", path)[0] == 200: return
            except OSError: pass
            time.sleep(.1)
        raise AssertionError("Host not ready: " + str(base))
    def web(method, path, body=None, auth=None, cookie=None, status=200):
        code, head, raw = request(port, method, path, body, cookie, auth)
        assert code == status, (path, code, raw[:1800]); return json.loads(raw).get("data"), head
    def client(method, path, body=None, status=200, revision=None):
        headers = {}
        if method != "GET": headers["X-Mdo-Write-Token"] = request(native_port,"GET","/api/v1/account")[1]["X-Mdo-Write-Token"]
        if revision: headers["If-Match"] = '"' + revision + '"'
        code, _, raw = request(native_port,method,"/api/v1" + path,body,headers=headers)
        assert code == status, (path,code,raw[:1800]); return json.loads(raw).get("data")
    def store(kind, action, status=200, **kw):
        return client("POST","/ecosystem",dict(action=action,single_kind=kind,**kw),status)
    try:
        server = start(args.website_host, website/"xs.json", HOME); ready(server,port,"/api/v1/mdo/ecosystem/packages")
        web("POST","/api/v1/register",dict(username="roles_author",password="Fixture-2026-password"),status=201)
        login,_ = web("POST","/api/v1/login",dict(identifier="roles_author",password="Fixture-2026-password"))
        auth = {"Authorization":"Bearer " + login["access_token"]}
        _,h = web("POST","/admin/login",dict(username=user,password=password)); cookie="; ".join(v.split(";",1)[0] for v in h["_cookies"])
        inventory,_ = web("GET","/admin/api/mdo/ecosystem",cookie=cookie); csrf={"X-CSRF-Token":inventory["csrf"]}
        native=start(args.host,app/"xs.json",app,home); ready(native,native_port,"/api/v1/account")
        client("POST","/account/login",dict(identifier="roles_author",password="Fixture-2026-password",remember=False))
        for _ in range(100):
            if client("GET","/account")["state"]=="signed_in": break
            time.sleep(.1)
        ids={}
        for kind,identity,code in [("agents","online-main",False),("subagents","online-child",True)]:
            p=package(kind,identity,code); r=p["resources"][0]; d=json.loads(r["content"])
            d["mode"]="primary" if kind=="agents" else "subagent"
            if kind=="agents": d.update(tools=["agent"],allow_delegation=True,delegation=dict(allow=["subagent.online-child"]))
            r["content"]=json.dumps(d); r["files"][0]["base64"]=base64.b64encode(("ONLINE_MAIN_MARKER" if kind=="agents" else "ONLINE_CHILD_MARKER").encode()).decode()
            p["manifest"].update(author="Role developer",copyright="Copyright Role developer")
            submitted=store(kind,"submit",package=p,publish=False,status=201); identity_id=submitted["id"]
            assert store(kind,"catalog")["items"]==[]
            web("GET",f"/api/v1/mdo/ecosystem/package?format=2&id={identity_id}",status=404)
            web("POST","/admin/api/mdo/ecosystem",dict(action="approve",id=identity_id,revision=1,reason="Cannot publish private work"),auth=csrf,cookie=cookie,status=409)
            web("POST","/api/v1/mdo/creator/publish",dict(source="package",id=identity_id,revision=1,publish=True,review_notes="Complete source and documents; no external network calls."),auth=auth)
            queue,_=web("GET",f"/admin/api/mdo/creator/works?kind={kind}&state=pending",cookie=cookie)
            assert queue["total"]==1 and queue["rows"][0]["version_id"]==identity_id
            web("POST","/admin/api/mdo/ecosystem",dict(action="approve",id=identity_id,revision=2,reason="Reviewed portable source"),auth=csrf,cookie=cookie)
            row=store(kind,"catalog")["items"][0]; assert row["submitter"]=="roles_author"
            assert store(kind,"detail",id=identity_id)["package"]["resources"]==p["resources"]
            wrong="subagents" if kind=="agents" else "agents"; store(wrong,"detail",id=identity_id,status=404)
            plan=store(kind,"plan",id=identity_id); assert plan["can_install"]
            if code: store(kind,"install",id=identity_id,status=409)
            store(kind,"install",id=identity_id,trust_code=True,expected_plan=plan["sha256"])
            path=f"/extensions/{kind}/{identity}"; item=client("GET",path)
            assert item["store_managed"] and item["copyright"]==p["manifest"]["copyright"]
            client("PUT",path,dict(content=r["content"]),status=403,revision=item["revision"])
            client("DELETE",path,status=403,revision=item["revision"])
            store(kind,"export",references=[dict(kind=kind,id=identity)],status=403)
            # Updating a version keeps the published version until the new review.
            update=copy.deepcopy(p); update["manifest"]["version"]="1.1.0"
            pending=store(kind,"submit",package=update,publish=True,review_notes="Same role with revised documentation; complete source provided.",status=201)
            assert store(kind,"latest",id=identity_id)["id"]==identity_id
            web("POST","/admin/api/mdo/ecosystem",dict(action="approve",id=pending["id"],revision=1,reason="Version update inspected"),auth=csrf,cookie=cookie)
            assert store(kind,"latest",id=identity_id)["id"]==pending["id"]
            store(kind,"install",id=pending["id"],trust_code=True)
            assert len(store(kind,"history",id=identity_id)["items"])==2
            old=copy.deepcopy(p);old["manifest"].update(slug="old-"+identity,min_build_id=60000016)
            web("POST","/api/v1/mdo/ecosystem/submit",dict(package=old,kind=kind,single_resource=True),auth=auth,status=422)
            multiple=copy.deepcopy(p);multiple["resources"]*=2
            store(kind,"submit",package=multiple,publish=False,status=422)
            ids[kind]=(pending["id"],identity)
        # A downloaded configuration main invokes a downloaded C SubAgent.
        cfg=client("GET","/models/config");cfg.pop("runtime_override",None)
        provider=copy.deepcopy(cfg["providers"][0]);provider.update(id="role-fixture",name="Role loopback",builtin=False,editable=True,removable=True,
            endpoints=dict(responses=f"http://127.0.0.1:{model.server_port}/v1"),credential=dict(secret_ref="env:MDO_ROLE_TEST_KEY"))
        profile=copy.deepcopy(cfg["items"][0]);profile.update(id="role-fixture",name="Role loopback",provider="role-fixture",builtin=False,free=False,
            editable=True,removable=True,protocols=["openai-responses"],default_protocol="openai-responses")
        cfg["providers"].append(provider);cfg["items"].append(profile)
        _,head,_=request(native_port,"GET","/api/v1/models/config")
        client("PUT","/settings/models",dict(schema_version=1,patch=cfg),revision=head["ETag"].strip('"'))
        session=client("POST","/sessions",dict(project_id="default",agent_id="agent.online-main",model_id="role-fixture",protocol="openai-responses",permission_profile="full-access"),status=201)
        run=client("POST",f'/projects/default/sessions/{session["id"]}/runs',dict(prompt="Complete the role store fixture",timeout_ms=10000),status=202)
        for _ in range(200):
            run=client("GET","/runs/"+run["id"])
            if run["terminal"]:break
            time.sleep(.05)
        assert run["state"]=="succeeded",run
        assert len(Model.calls)==3 and any(p.get("temperature")==.2 for p in Model.calls),Model.calls
        assert "CHILD_OK" in json.dumps(Model.calls[-1])
        for kind,(identity_id,identity) in ids.items():
            store(kind,"uninstall",entry=dict(id=identity_id));assert not(home/kind/(identity+".md")).exists()
            works,_=web("GET",f"/api/v1/mdo/creator/works?kind={kind}",auth=auth)
            row=works["rows"][0]
            web("POST","/api/v1/mdo/creator/work",dict(action="withdraw",work=row["work"],revision=row["work_revision"]),auth=auth)
            assert store(kind,"catalog")["items"]==[]
        for route in ("/mdo/role-package.js","/mdo/role-preview.js"):
            status,_,body=request(port,"GET",route);assert status==200 and b"export" in body
        status,_,body=request(port,"GET","/admin/mdo/extensions/agents",cookie=cookie,headers={"X-Requested-With":"XMLHttpRequest"})
        assert status==200 and b"<!doctype" not in body.lower() and b"data-creator-table" in body
        print("PASS roles: private/publish/review, typed catalogs, downloads, protected source, update/history, downloaded main -> downloaded C child, uninstall and author withdrawal")
        print("Evidence:",base)
        if args.ui:
            print(f"UI_NATIVE=http://127.0.0.1:{native_port}/ UI_SITE={origin}/mdo/creator?kind=agents",flush=True)
            while True:time.sleep(1)
    finally:
        for p in reversed(processes):
            if p.poll() is None:p.terminate();p.wait(15)
        for f in logs:f.close()
        model.shutdown();model.server_close();thread.join(3)


if __name__=="__main__":run()
