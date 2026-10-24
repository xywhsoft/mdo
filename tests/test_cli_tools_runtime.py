"""Real native external-tool lifecycle; no uploaded program is ever executed."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from test_api_runtime import free_port, request, wait_ready

ROOT=Path(__file__).resolve().parents[1]
PLATFORM="windows-x86_64" if os.name=="nt" else "linux-x86_64"
PAYLOAD=b"Not an executable; must never be run\n"
MANIFEST=dict(id="fixture",name="Fixture CLI",version="1.0.0",platform=PLATFORM,description="USER_DESCRIPTION_SENTINEL",prompt="CLI_PROMPT_SENTINEL",
              entry="bin/fixture.exe",license="MIT",links=["https://example.org"],
              files=[dict(path="bin/fixture.exe",size=len(PAYLOAD),sha256=hashlib.sha256(PAYLOAD).hexdigest(),mode=493)])
TEXT=json.dumps(MANIFEST).encode();ARCHIVE=b"MDOCLI1\n"+struct.pack("<I",len(TEXT))+TEXT+PAYLOAD
PRODUCT=dict(MANIFEST,id=1,slug="fixture",tool_id="official.fixture",sha256=hashlib.sha256(ARCHIVE).hexdigest(),size=len(ARCHIVE),state="published",author="Fixture")

def stage_forwarder(site):
    if os.name!="nt":return
    sys.path.insert(0,str(ROOT/"tools"))
    import build_mdo,build_cli_forwarder
    xs=build_mdo.find_xserver(None,build_mdo.load_object(build_mdo.LOCK_PATH))
    build_cli_forwarder.build(xs,site/"generated/native/windows/cli-forwarder.exe")

LEGACY_MANIFEST={k:v for k,v in MANIFEST.items() if k!="description"}
LEGACY_TEXT=json.dumps(LEGACY_MANIFEST).encode();LEGACY_ARCHIVE=b"MDOCLI1\n"+struct.pack("<I",len(LEGACY_TEXT))+LEGACY_TEXT+PAYLOAD
class Store(BaseHTTPRequestHandler):
    legacy=False
    wrong_description=False
    damaged=False
    slow=False
    def log_message(self,*args):pass
    def do_GET(self):
        archive=LEGACY_ARCHIVE if self.legacy else ARCHIVE
        product=dict(PRODUCT,sha256=hashlib.sha256(archive).hexdigest(),size=len(archive))
        if self.legacy:product["description"]="Legacy catalog user description"
        if self.wrong_description:product["description"]="Wrong catalog description"
        if self.path.startswith("/api/v1/mdo/cli/blob"):
            data=bytes(len(archive)) if self.damaged else archive
        else:
            data=json.dumps(dict(code=0,data=product if "/detail?" in self.path else dict(items=[product],next_before=0))).encode()
        self.send_response(200);self.send_header("Content-Length",str(len(data)));self.end_headers()
        try:
            if self.slow and "/blob?" in self.path:
                for offset in range(0,len(data),16):self.wfile.write(data[offset:offset+16]);self.wfile.flush();time.sleep(.02)
            else:self.wfile.write(data)
        except (BrokenPipeError,ConnectionResetError,ConnectionAbortedError):pass

def run(host):
    server=ThreadingHTTPServer(("127.0.0.1",0),Store);threading.Thread(target=server.serve_forever,daemon=True).start()
    base=Path(tempfile.mkdtemp(prefix="cli-tools-",dir=ROOT/".build"));site=base/"site";home=base/"home";shutil.copytree(ROOT/"app",site)
    stage_forwarder(site)
    origin=f"http://127.0.0.1:{server.server_port}"
    probe=f'#define MDO_ACCOUNT_SERVICE_ORIGIN "{origin}"\n#define MDO_DISTRIBUTION_ORIGIN "{origin}"\n#define MDO_DISTRIBUTION_HOST "127.0.0.1"\n#define MDO_DISTRIBUTION_PORT {server.server_port}\n#define MDO_DISTRIBUTION_SECURE false\n#define RequestProc ProductRequestProc\n#include "generated/mdo_unity.c"\n#undef RequestProc\n'
    probe+=r'XS_RequestResult RequestProc(XS_HttpReq* req){if(xrtStrEqual(req->head->Target,XRT_STR_LITERAL("/fixture/prompt"))){MdoApiContext c={0};c.Request=req;str p=xrtStrDup("AGENT_SENTINEL");bool ok=MdoToolPrompt(&p)&&MdoToolPrompt(&p);if(ok)MdoApiReplySuccessTake(&c,200,xrtValueString(xrtStrView(p)),NULL);xrtFree(p);return XS_OK;}return ProductRequestProc(req);}'
    probe=probe.replace('return ProductRequestProc(req);}',r'''if(xrtStrEqual(req->head->Target,XRT_STR_LITERAL("/fixture/isolation"))){MdoApiContext c={0};c.Request=req;xvalue* before=MdoCliSnapshot(),*command=xrtValueObject();xrtValueObjectSetNew(command,XRT_STR_LITERAL("action"),xrtValueString(XRT_STR_LITERAL("enable")));xrtValueObjectSetNew(command,XRT_STR_LITERAL("id"),xrtValueString(XRT_STR_LITERAL("official.fixture")));xrtValueObjectSetNew(command,XRT_STR_LITERAL("enabled"),xrtValueBool(false));char error[256]={0};bool ok=MdoCliCommand(command,error,sizeof(error));xrtValueRelease(command);xvalue* out=xrtValueObject();xrtValueObjectSetNew(out,XRT_STR_LITERAL("before"),before);xrtValueObjectSetNew(out,XRT_STR_LITERAL("after"),MdoCliSnapshot());xrtValueObjectSetNew(out,XRT_STR_LITERAL("ok"),xrtValueBool(ok));MdoApiReplySuccessTake(&c,200,out,NULL);return XS_OK;}return ProductRequestProc(req);}''')
    fixture=site/"src/distribution/cli_bindings.inc.c";text=fixture.read_text(encoding="utf-8");text=text.replace('if(Ok)Ok=MdoDistSave("data/cli-tools/state.json",Saved);','if(Ok){xfile f=MdoHomeOpenRead("fail-cli-save");if(f){xrtClose(f);Ok=false;}else Ok=MdoDistSave("data/cli-tools/state.json",Saved);}');fixture.write_text(text,encoding="utf-8")
    (site/"probe.c").write_text(probe,encoding="utf-8");port=free_port()
    cfg={"services":[{"class":"http","name":"cli-fixture","enabled":True,"ip":"127.0.0.1","port":port,"host_default":{"name":"cli-fixture","enabled":True,"path":str(site/"web"),"devlang":"c","devfile":str(site/"probe.c")}}]}
    (site/"xs.json").write_text(json.dumps(cfg));log=(base/"native.log").open("wb")
    def start():
        p=subprocess.Popen([str(host),str(site/"xs.json"),"--","--home",str(home)],cwd=ROOT,env={**os.environ,"MDO_HOME":str(home)},stdout=log,stderr=log,creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0));wait_ready(port,p);return p
    def get():
        status,_,raw=request(port,"GET","/api/v1/cli-tools");assert status==200,(status,raw);return json.loads(raw)["data"]
    def post(action,status=200,**fields):
        code,_,raw=request(port,"POST","/api/v1/cli-tools",body=json.dumps(dict(action=action,**fields)).encode(),headers={"Content-Type":"application/json"});assert code==status,(code,raw);return json.loads(raw).get("data")
    def wait_task():
        end=time.monotonic()+25
        while time.monotonic()<end:
            s=get()
            if not any(t["state"] in ("queued","running","installing","uploading") for t in s["tasks"]):return s
            time.sleep(.05)
        raise AssertionError(get())
    def prompt():return json.loads(request(port,"GET","/fixture/prompt")[2])["data"]
    proc=start()
    try:
        assert get()["guide"]=="new" and not get()["items"]
        exe=base/"manual.exe";exe.write_bytes(PAYLOAD)
        tool=dict(id="custom.fixture",name="Manual",version="1.0.0",path=str(exe),description="MANUAL_USER_DESCRIPTION",prompt="MANUAL_PROMPT_SENTINEL",license="MIT",platform=PLATFORM,enabled=True,links=[])
        post("save",tool=tool);assert "MANUAL_PROMPT_SENTINEL" in prompt() and prompt().count("<mdo_tool_environment>")==1
        assert get()["items"][0]["description"]==tool["description"] and tool["description"] not in prompt()
        old_edit={k:v for k,v in tool.items() if k!="description"};post("save",tool=old_edit,revision=get().get("revision",0));assert get()["items"][0]["description"]==tool["description"]
        for invalid in [None,[],"x"*2049,"nul\0text"]:post("save",status=422,tool={**tool,"description":invalid},revision=get().get("revision",0))
        assert get()["writable"] and get()["items"][0]["description"]==tool["description"]
        post("enable",id=tool["id"],enabled=False);assert "MANUAL_PROMPT_SENTINEL" not in prompt()
        post("enable",id=tool["id"],enabled=True);exe.unlink();assert "MANUAL_PROMPT_SENTINEL" not in prompt()
        post("save",status=422,tool=tool,revision=get().get("revision",0));exe.write_bytes(PAYLOAD)
        post("remove",id=tool["id"]);assert exe.read_bytes()==PAYLOAD
        post("dismiss");post("catalog");s=wait_task();assert s["store"]["items"][0]["id"]==1
        post("install",store_id=1,name="Fixture CLI");s=wait_task();assert s["tasks"][-1]["state"]=="complete",s
        t=s["items"][0];installed=Path(t["path"]);assert installed.read_bytes()==PAYLOAD and "CLI_PROMPT_SENTINEL" in prompt() and t["description"]=="USER_DESCRIPTION_SENTINEL" and "USER_DESCRIPTION_SENTINEL" not in prompt()
        isolated=json.loads(request(port,"GET","/fixture/isolation")[2])["data"];assert isolated["ok"] and isolated["before"]["items"][0]["enabled"] and not isolated["after"]["items"][0]["enabled"],"snapshots must not alias live nested containers"
        (home/"fail-cli-save").write_bytes(b"1");post("enable",status=422,id=t["id"],enabled=True);assert not get()["items"][0]["enabled"],"failed state writes must restore nested values";(home/"fail-cli-save").unlink()
        post("enable",id=t["id"],enabled=False);post("install",store_id=1);s=wait_task();assert s["tasks"][-1]["state"]=="complete" and not s["items"][0]["enabled"]
        original=s["items"][0]["path"];Store.wrong_description=True;post("install",store_id=1);s=wait_task();assert s["tasks"][-1]["state"]=="failed" and s["items"][0]["path"]==original;Store.wrong_description=False
        Store.legacy=True;post("install",store_id=1);s=wait_task();assert s["tasks"][-1]["state"]=="complete" and s["items"][0]["description"]=="Legacy catalog user description";Store.legacy=False
        post("install",store_id=1);s=wait_task();original=s["items"][0]["path"];Store.damaged=True;post("install",store_id=1);s=wait_task();assert s["tasks"][-1]["state"]=="failed" and s["items"][0]["path"]==original
        Store.damaged=False;Store.slow=True;post("install",store_id=1)
        deadline=time.monotonic()+10
        while get()["tasks"][-1]["state"]=="queued" and time.monotonic()<deadline:time.sleep(.01)
        cancelled=get()["tasks"][-1]["id"];post("cancel",id=cancelled);s=wait_task();assert s["tasks"][-1]["state"]=="cancelled" and s["items"][0]["path"]==original,s
        Store.slow=False;post("retry",id=cancelled);s=wait_task();assert s["tasks"][-1]["state"]=="complete",s;original=s["items"][0]["path"]
        Store.damaged=False;post("recommend");s=wait_task();assert s["items"][0]["enabled"] and s["guide"]=="accepted"
        post("remove",id=t["id"]);assert Path(original).exists(),"running tasks retain retired files until restart"
        proc.terminate();proc.wait(10);proc=start();s=get();assert s["guide"]=="accepted" and not s["items"] and not Path(original).exists()
        assert not exe.with_suffix(".called").exists()
        proc.terminate();proc.wait(10);state=home/"data/cli-tools/state.json";state.write_bytes(b"broken");proc=start();assert not get()["writable"];post("dismiss",status=422);assert state.read_bytes()==b"broken"
        print("PASS CLI native manual paths, injection, missing files, individual install/update, hash failure rollback, guide, retirement, restart and corrupt-state preservation")
    finally:
        proc.terminate();proc.wait(15);log.close();server.shutdown();server.server_close()

if __name__=="__main__":
    p=argparse.ArgumentParser();p.add_argument("--host",type=Path,default=ROOT/".build/host/xs.exe");run(p.parse_args().host)
