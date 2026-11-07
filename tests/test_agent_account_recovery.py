"""A tool-using native Agent survives background login renewal without Continue.

All identity, model and filesystem data belong to this loopback fixture.
No public account, model credentials or paid model calls are used.
"""
import argparse,json,os,shutil,subprocess,tempfile,threading,time
from contextlib import ExitStack
from http.server import ThreadingHTTPServer
from pathlib import Path
from test_login_model_startup import Service,ONLINE,until
from test_api_runtime import free_port,request,wait_ready,session_events

ROOT=Path(__file__).resolve().parents[1]

class RecoveryService(Service):
    first=threading.Event();advance=threading.Event();renewing=threading.Event();renew=threading.Event()
    calls=0;refreshes=0;tool_results=0
    refresh_status=503
    refresh_failures=2

    def handle(self):
        try:super().handle()
        except (ConnectionResetError,BrokenPipeError):pass

    def raw(self,status,value,stream=False):
        payload=("data: "+json.dumps(value)+"\n\ndata: [DONE]\n\n").encode() if stream else json.dumps(value).encode()
        self.send_response(status)
        self.send_header("Content-Type","text/event-stream" if stream else "application/json")
        self.send_header("Content-Length",str(len(payload)));self.end_headers();self.wfile.write(payload)

    def do_POST(self):
        if self.path not in ("/api/v1/token/refresh","/api/v1/ai/chat/completions"):
            return super().do_POST()
        body=json.loads(self.rfile.read(int(self.headers.get("Content-Length",0))) or b"{}")
        cls=type(self)
        if self.path=="/api/v1/token/refresh":
            cls.refreshes+=1;cls.renewing.set();assert cls.renew.wait(15)
            if cls.refreshes<=cls.refresh_failures:
                return self.raw(cls.refresh_status,{"code":cls.refresh_status,"message":"Fixture renewal rejection"})
            return self.reply({"id":1,"access_token":"fresh-fixture-token","refresh_token":"b"*64,
                               "token_type":"Bearer","expires_in":900})
        cls.calls+=1
        tool=cls.calls==1
        if tool:
            cls.first.set();assert cls.advance.wait(15)
            delta={"role":"assistant","tool_calls":[{"index":0,"id":"fixture-ls","type":"function",
                   "function":{"name":"ls","arguments":'{"path":"."}'}}]}
        else:
            if self.headers.get("Authorization")!="Bearer fresh-fixture-token":
                return self.raw(401,{"error":{"code":"invalid_token","message":"Fixture bearer expired"}})
            count=sum(m.get("role")=="tool" for m in body["messages"])
            assert count==1,"A completed tool must not be replayed during authentication recovery"
            cls.tool_results=count
            delta={"role":"assistant","content":"Completed after account recovery."}
        assert body.get("stream")
        return self.raw(200,{"id":"fixture-reply","model":"startup-fixture","choices":[
            {"index":0,"delta":delta,"finish_reason":"tool_calls" if tool else "stop"}]},stream=True)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host",type=Path,default=ROOT/'.build/host/xs.exe')
    parser.add_argument("--record",type=Path)
    parser.add_argument("--refresh-status",type=int,default=503)
    parser.add_argument("--refresh-failures",type=int,default=2)
    parser.add_argument("--exhausted",action="store_true")
    args=parser.parse_args()
    RecoveryService.refresh_status=args.refresh_status
    RecoveryService.refresh_failures=args.refresh_failures
    Service.login.set();Service.catalog.set()
    for e in (RecoveryService.first,RecoveryService.advance,RecoveryService.renewing,RecoveryService.renew):e.clear()
    RecoveryService.calls=RecoveryService.refreshes=RecoveryService.tool_results=0
    server=ThreadingHTTPServer(('127.0.0.1',0),RecoveryService);server.daemon_threads=True
    worker=threading.Thread(target=server.serve_forever,daemon=True);worker.start()
    process=None
    try:
        with tempfile.TemporaryDirectory(prefix='agent-account-recovery-',dir=ROOT/'.build') as raw, ExitStack() as lifetime:
            base=Path(raw);site=base/'site';shutil.copytree(ROOT/'app',site)
            unity=site/'generated/mdo_unity.c'
            unity.write_text(f'#define MDO_ACCOUNT_SERVICE_ORIGIN "http://127.0.0.1:{server.server_port}"\n'
                '#define MDO_MODEL_RETRY_BASE_MS 10u\n#define MDO_MODEL_RETRY_MAX_MS 80u\n'
                '#define MDO_ACCOUNT_REFRESH_RETRY_BASE_MS 10u\n'+unity.read_text(encoding='utf-8'),encoding='utf-8')
            port=free_port();config=site/'xs.json'
            config.write_text(json.dumps({'services':[{'enabled':True,'class':'http','name':'mdo','ip':'127.0.0.1','port':port,
                'host_default':{'enabled':True,'name':'mdo','path':'web','devlang':'c','devfile':'generated/mdo_unity.c'}}]}))
            def call(method,path,body=None,expected=200):
                status,_,data=request(port,method,'/api/v1'+path,body=json.dumps(body).encode() if body is not None else None,
                                      headers={'Content-Type':'application/json'})
                result=json.loads(data);assert status==expected,(path,status,result)
                return result.get('data',result)
            log=base/'native.log'
            with log.open('wb') as output:
                process=subprocess.Popen([str(args.host.resolve()),str(config),'--','--home',str(base/'home')],cwd=site,
                    env={**os.environ,'USE_WEBVIEW':'0','MDO_PLUGINS_DISABLED':'1'},stdout=output,stderr=subprocess.STDOUT,
                    creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            def stop_native():
                if process.poll() is None:process.terminate();process.wait(timeout=5)
            lifetime.callback(stop_native)
            wait_ready(port,process)
            call('POST','/account/login',{'identifier':'fixture','password':'fixture-only','remember':False})
            until(lambda:call('GET','/models')['login_state']=='ready')
            session=call('POST','/sessions',{'project_id':'default','model_id':ONLINE,'reasoning_effort':'none','title':'Recovery fixture'},201)
            path='/projects/default/sessions/'+session['id']
            run=call('POST',path+'/runs',{'prompt':'List the workspace once, then summarize.'},202)
            assert RecoveryService.first.wait(10)
            call('POST','/account/refresh',{})
            assert RecoveryService.renewing.wait(5)
            RecoveryService.advance.set()
            time.sleep(.1)
            assert not call('GET','/runs/'+run['id'])['terminal'],'Background renewal interrupted the Agent'
            RecoveryService.renew.set()
            finished=until(lambda:(r if (r:=call('GET','/runs/'+run['id']))['terminal'] else None))
            events=session_events(port,'/api/v1'+path)
            retries=[e for e in events if e.get('kind')=='model_retry']
            if args.exhausted:
                assert finished['state']=='failed',finished
                until(lambda:call('GET','/account')['state']=='expired')
                assert RecoveryService.refreshes==11,RecoveryService.refreshes
                assert len(retries)==20,len(retries)  # waiting and retrying pairs
                assert sum(e.get('kind')=='error' for e in events)==1,events
                assert not call('GET','/account')['refresh_available']
                proof=dict(passed=True,run=finished['state'],renewals=RecoveryService.refreshes,
                           retry_events=len(retries),continuation_requests=0,refresh_status=args.refresh_status)
                if args.record:args.record.write_text(json.dumps(proof,indent=2)+'\n')
                print(json.dumps(proof));return
            assert finished['state']=='succeeded',finished
            assert retries and not any(e.get('kind')=='error' for e in events),events
            before=RecoveryService.refreshes
            second=call('POST',path+'/runs',{'prompt':'Confirm completion without using tools again.'},202)
            done=until(lambda:(r if (r:=call('GET','/runs/'+second['id']))['terminal'] else None))
            assert done['state']=='succeeded' and RecoveryService.refreshes==before,done
            assert sum(e.get('kind')=='model_retry' for e in session_events(port,'/api/v1'+path))==len(retries)
            proof=dict(passed=True,first_run=finished['state'],second_run=done['state'],model_calls=RecoveryService.calls,
                       renewals=RecoveryService.refreshes,retry_events=len(retries),completed_tool_results=RecoveryService.tool_results,
                       continuation_requests=0)
            if args.record:args.record.write_text(json.dumps(proof,indent=2)+'\n')
            print(json.dumps(proof))
    finally:
        for e in (RecoveryService.advance,RecoveryService.renew):e.set()
        if process and process.poll() is None:process.terminate();process.wait(timeout=5)
        server.shutdown();server.server_close();worker.join(timeout=3)

if __name__=='__main__':main()
