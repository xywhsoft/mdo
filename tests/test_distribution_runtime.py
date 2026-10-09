"""Real xs/TCC + HTTP test; isolated Home and publication server."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import threading
import tempfile
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from test_api_runtime import free_port, request, wait_ready
ROOT=Path(__file__).resolve().parents[1]

class Published(BaseHTTPRequestHandler):
    protocol_version="HTTP/1.1"
    damaged=False
    slow=False
    dependencies=False
    truncated=False
    manifest_mismatch=False
    def log_message(self,*args): pass
    def do_GET(self):
        if self.path.startswith("/mdo/catalog"):
            rows=json.loads((ROOT/".build/releases/toolpacks/catalog.json").read_text())["toolpacks"]
            for row in rows: row.pop("file",None)
            if self.manifest_mismatch:next(row for row in rows if row['id']=='core')['revision']=3
            if self.dependencies:next(row for row in rows if row['id']=='python')['dependencies']=[dict(id='core',revision=2)]
            data=json.dumps(dict(revision=1,notices=[],toolpacks=rows)).encode()
            self.send_response(200);self.send_header("Content-Length",str(len(data)));self.end_headers();self.wfile.write(data)
        elif self.path.startswith("/mdo/blob/"):
            rows=json.loads((ROOT/".build/releases/toolpacks/catalog.json").read_text())["toolpacks"]
            row=next((p for p in rows if p["sha256"]==self.path.split("/")[-1]),None)
            if not row:self.send_error(404);return
            self.send_response(200);self.send_header("Transfer-Encoding","chunked");self.end_headers()
            sent=0
            with (ROOT/".build/releases/toolpacks"/row["file"]).open("rb") as file:
                while block:=file.read(65536):
                    if self.damaged:block=bytes(len(block))
                    if self.slow:time.sleep(.08)
                    try:self.wfile.write(f"{len(block):x}\r\n".encode()+block+b"\r\n")
                    except (BrokenPipeError,ConnectionResetError,ConnectionAbortedError):return
                    sent+=len(block)
                    if self.truncated and sent>=131072:self.close_connection=True;return
            self.wfile.write(b"0\r\n\r\n")
        else:self.send_error(404)

def run():
    publisher=ThreadingHTTPServer(("127.0.0.1",0),Published);threading.Thread(target=publisher.serve_forever,daemon=True).start()
    base=Path(tempfile.mkdtemp(prefix="distribution-test-",dir=ROOT/".build"));site=base/"site";home=base/"home"
    assert base.resolve().is_relative_to(ROOT.resolve()) and site.resolve().is_relative_to(base.resolve())
    if site.exists():shutil.rmtree(site)
    if home.exists():shutil.rmtree(home)
    shutil.copytree(ROOT/"app",site)
    # Fault injection is confined to the copied site, never production source.
    manager=site/"src/distribution/manager.c"
    manager.write_text(manager.read_text(encoding="utf-8").replace(
        '    xprocessconfig Config; xprocessrunoptions Options; xprocessresult Result={0};',
        '    xfile forbid=MdoHomeOpenRead("probe.forbid");if(forbid){xrtClose(forbid);MdoHomeAtomicWrite("probe.called","1",1,false);return false;}\n    xprocessconfig Config; xprocessrunoptions Options; xprocessresult Result={0};'),encoding="utf-8")
    port=free_port();origin=f"http://127.0.0.1:{publisher.server_port}"
    probe=f'#define MDO_DISTRIBUTION_ORIGIN "{origin}"\n#define MDO_UPDATE_ORIGIN "{origin}"\n#define MDO_DISTRIBUTION_HOST "127.0.0.1"\n#define MDO_DISTRIBUTION_PORT {publisher.server_port}\n#define MDO_DISTRIBUTION_SECURE false\n#define RequestProc ProductRequestProc\n#include "generated/mdo_unity.c"\n#undef RequestProc\n'
    probe+=r'XS_RequestResult RequestProc(XS_HttpReq* req){if(xrtStrEqual(req->head->Target,XRT_STR_LITERAL("/fixture/prompt"))){MdoApiContext context={0};context.Request=req;str prompt=xrtStrDup("AGENT_SENTINEL\n\n<mdo_tool_environment>\nstale\n</mdo_tool_environment>\n");bool ok=MdoToolPrompt(&prompt)&&MdoToolPrompt(&prompt);if(ok)MdoApiReplySuccessTake(&context,200,xrtValueString(xrtStrView(prompt)),NULL);xrtFree(prompt);return XS_OK;}return ProductRequestProc(req);}'
    (site/"probe.c").write_text(probe,encoding="utf-8")
    config=dict(services=[dict(enabled=True,name="mdo-test",ip="127.0.0.1",port=port,recv_limit=8454144,body_limit=8388608,host_default=dict(enabled=True,name="mdo",path=str(site/"web"),devlang="c",devfile=str(site/"probe.c")))])
    config["services"][0]["class"]="http"
    (site/"xs.json").write_text(json.dumps(config),encoding="utf-8")
    log=(base/"native.log").open("wb")
    proc=subprocess.Popen([str(ROOT/".build/host/xs.exe"),str(site/"xs.json"),"--","--home",str(home)],cwd=ROOT,env={**os.environ,"MDO_HOME":str(home)},stdout=log,stderr=log,creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        wait_ready(port,proc)
        def get():
            status,headers,body=request(port,"GET","/api/v1/distribution");assert status==200,(status,body);return json.loads(body)["data"]
        deadline=time.monotonic()+30
        while not get().get("toolpacks") and time.monotonic()<deadline:time.sleep(.1)
        assert get().get("toolpacks"),get()
        def install(id,action="install"):
            status,_,body=request(port,"POST","/api/v1/distribution",body=json.dumps(dict(action=action,id=id)).encode(),headers={"Content-Type":"application/json"});assert status==200,(status,body)
            deadline=time.monotonic()+90
            while get()["busy"] and time.monotonic()<deadline:time.sleep(.1)
            assert not get()["busy"]
            return get()
        Published.damaged=True
        state=install("core");assert not state["tools"],"corrupt package must not activate"
        error=state['error'];assert error['stage']=='downloading' and error['code']=='checksum',state
        failed=home/error['download'];assert failed.exists()
        assert hashlib.sha256(failed.read_bytes()).hexdigest()==error['actual_sha256']!=error['expected_sha256']
        assert json.loads((home/'data/toolpacks/last-error.json').read_text())==error
        state=install('','check');assert state['stage']=='failed' and state['error']==error,'catalog refresh must retain installation failure'
        proc.terminate();proc.wait(10)
        proc=subprocess.Popen([str(ROOT/".build/host/xs.exe"),str(site/"xs.json"),"--","--home",str(home)],cwd=ROOT,env={**os.environ,"MDO_HOME":str(home)},stdout=log,stderr=log,creationflags=subprocess.CREATE_NO_WINDOW)
        wait_ready(port,proc);assert get()['error']==error and get()['stage']=='failed','failure diagnosis must survive restart'
        Published.damaged=False
        Published.truncated=True
        state=install('core');assert not state['tools'] and state['error']['code']=='download',state
        assert 0<state['error']['actual_bytes']<state['error']['expected_bytes']
        Published.truncated=False
        Published.manifest_mismatch=True;install('','check')
        state=install('core');assert not state['tools'] and state['error']['stage']=='extracting' and state['error']['code']=='manifest',state
        assert state['error']['actual_sha256']==state['error']['expected_sha256'],'archive hash passed before manifest failure'
        Published.manifest_mismatch=False;install('','check')
        Published.slow=True
        status,_,_=request(port,"POST","/api/v1/distribution",body=b'{"action":"install","id":"core"}',headers={"Content-Type":"application/json"});assert status==200
        deadline=time.monotonic()+10
        progress=get()
        while progress['busy'] and not (progress['stage']=='downloading' and progress['done']>0) and time.monotonic()<deadline:
            time.sleep(.02);progress=get()
        assert progress['busy'] and progress['stage']=='downloading' and 0<progress['done']<progress['total'],progress
        status,_,_=request(port,"POST","/api/v1/distribution",body=b'{"action":"cancel"}',headers={"Content-Type":"application/json"});assert status==200
        deadline=time.monotonic()+20
        while get()["busy"] and time.monotonic()<deadline:time.sleep(.1)
        assert not get()["tools"],"cancel must not activate"
        assert get()['stage']=='cancelled' and not get().get('error') and not (home/'data/toolpacks/download.pending').exists()
        Published.slow=False
        (home/'probe.forbid').write_bytes(b'1')
        state=install("core");assert {t["id"] for t in state["tools"]}=={"busybox","curl","jq","ssh","scp","sftp","aria2c","rg","7zip"},state
        assert state['stage']=='complete' and not (home/'probe.called').exists(),'install must not start any tool process'
        assert all(t['verification']=='file-integrity' for t in state['tools'])
        assert next(t for t in state['tools'] if t['id']=='7zip')['version']=='26.04'
        assert len({next(t for t in state['tools'] if t['id']==name)['version'] for name in ['ssh','scp','sftp']})==1
        assert not state.get('error') and not failed.exists() and not (home/'data/toolpacks/last-error.json').exists(),'successful retry must clear failure artifacts'
        Published.dependencies=True
        install("","check")
        state=install("python");assert len(state["tools"])==10,state
        blocked=install("core","uninstall");assert blocked['installed']['core'] and len(blocked['tools'])==10 and blocked['stage']=='failed',blocked
        python=next(t for t in state["tools"] if t["id"]=="python")
        assert python['verification']=='file-integrity' and 'sqlite-memory' not in python['verified']
        assert python['version']=='3.13.7' and not (home/'probe.called').exists(),'Python installation must not start any tool process'
        assert state["installed"]["core_revision"]==2
        status,_,body=request(port,"GET","/fixture/prompt");prompt=json.loads(body)["data"]
        assert prompt.count("<mdo_tool_environment>")==1 and "AGENT_SENTINEL" in prompt and "stale" not in prompt and "python" in prompt
        assert 'file-integrity' in prompt and 'without executing the tools' in prompt
        (home/'probe.forbid').unlink()
        proc.terminate();proc.wait(10)
        proc=subprocess.Popen([str(ROOT/".build/host/xs.exe"),str(site/"xs.json"),"--","--home",str(home)],cwd=ROOT,env={**os.environ,"MDO_HOME":str(home)},stdout=log,stderr=log,creationflags=subprocess.CREATE_NO_WINDOW)
        wait_ready(port,proc);assert len(get()["tools"])==10 and get()["installed"]["core_revision"]==2
        (home/'probe.forbid').write_bytes(b'1')
        old=home/get()["installed"]["core"];(old/"jq/jq.exe").write_bytes(b"damaged")
        state=install("core","repair");assert len(state["tools"])==10 and home/state["installed"]["core"]!=old
        assert (old/"jq/jq.exe").read_bytes()==b"damaged","repair must not overwrite paths retained by existing runs"
        good=home/state["installed"]["core"]
        state=install("core","repair");assert len(state["tools"])==10
        state=install("core","rollback");assert home/state["installed"]["core"]==good and len(state["tools"])==10
        assert not (home/'probe.called').exists(),'repair and rollback must not start tool processes'
        state=install("python","uninstall");assert "python" not in state["installed"] and all(t["id"]!="python" for t in state["tools"])
        state=install("","cleanup");assert good.exists() and (home/state['installed']['retired'][-1]['path']).exists(),"generations retired during this run must survive cleanup"
        assert state["capability_revision"]>=6
        (home/'probe.forbid').unlink()
        proc.terminate();proc.wait(10)
        proc=subprocess.Popen([str(ROOT/".build/host/xs.exe"),str(site/"xs.json"),"--","--home",str(home)],cwd=ROOT,env={**os.environ,"MDO_HOME":str(home)},stdout=log,stderr=log,creationflags=subprocess.CREATE_NO_WINDOW)
        wait_ready(port,proc);assert get()["cleanup_available"]>=2
        state=install("","cleanup");assert not old.exists() and good.exists() and not state["installed"]["retired"]
        print("PASS install/Python/repair/rollback without executing tools, metadata versions and truthful prompt verification; checksum/truncation/manifest diagnostics, cancellation, restart/refresh persistence, repair/uninstall/cleanup")
    except Exception:
        log.flush();print((base/"native.log").read_text(encoding="utf-8",errors="replace")[-6000:]);raise
    finally:
        proc.terminate()
        try:proc.wait(10)
        except subprocess.TimeoutExpired:proc.kill();proc.wait()
        log.close();publisher.shutdown()
if __name__=="__main__":run()
