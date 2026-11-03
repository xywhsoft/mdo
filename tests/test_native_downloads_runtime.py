"""Real native single-connection downloads; synthetic files and loopback only.

Uses the project sources and existing host in place, without copying the app,
SDK, or runtime. Includes a process restart against the same disposable Home.
"""
import argparse,hashlib,json,os,subprocess,tempfile,threading,time,uuid
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
from pathlib import Path
from test_api_runtime import ROOT,free_port,request,wait_ready

PAYLOAD=bytes(range(256))*8192
HASH=hashlib.sha256(PAYLOAD).hexdigest()
class Publisher(BaseHTTPRequestHandler):
    requests=[]
    def log_message(self,*_):pass
    def do_GET(self):
        mode=self.path.removeprefix('/qa/');start=0
        field=self.headers.get('Range','')
        if field:start=int(field.split('=')[1].split('-')[0])
        type(self).requests.append((mode,start))
        if mode=='ignore':start=0
        self.send_response(206 if start else 200)
        if start:self.send_header('Content-Range',f'bytes {start}-{len(PAYLOAD)-1}/{len(PAYLOAD)}')
        self.send_header('Content-Length',str(len(PAYLOAD)-start));self.send_header('ETag','"fixture-v1"');self.end_headers()
        body=PAYLOAD if mode!='corrupt' else b'x'*len(PAYLOAD)
        try:
            for at in range(start,len(body),16384):
                self.wfile.write(body[at:at+16384]);self.wfile.flush();time.sleep(.008)
                if mode=='disconnect' and not start and at>=262144:return
        except (BrokenPipeError,ConnectionResetError,ConnectionAbortedError):pass

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--host',type=Path,required=True);args=parser.parse_args()
    with ThreadingHTTPServer(('127.0.0.1',0),Publisher) as publisher,tempfile.TemporaryDirectory(dir=ROOT/'.build',prefix='native-download-') as raw:
        thread=threading.Thread(target=publisher.serve_forever,daemon=True);thread.start()
        base=Path(raw);home=base/'home';port=free_port();stem='.download-qa-'+uuid.uuid4().hex
        source=ROOT/'app'/(stem+'.c');config=ROOT/'app'/(stem+'.json')
        source.write_text(f'''#define MDO_DISTRIBUTION_HOST "127.0.0.1"
#define MDO_DISTRIBUTION_PORT {publisher.server_port}
#define MDO_DISTRIBUTION_SECURE false
#define MDO_ACCOUNT_SERVICE_ORIGIN "http://127.0.0.1:9"
#define ServiceInit QaBaseInit
#define ServiceUnit QaBaseUnit
#define RequestProc QaBaseRequest
#include "generated/mdo_unity.c"
#undef ServiceInit
#undef ServiceUnit
#undef RequestProc
static xnetengine* QaEngine;
static xthread* QaThread;
static char QaPath[80];
void ServiceInit(XS_HostInfo* H){{QaEngine=H->Server->Engine;QaBaseInit(H);}}
static int32 QaTransfer(void* Unused){{
 MdoTransferReport Report={{0}};bool Ok=MdoTransferDownloadReport(QaEngine,QaPath,"data/downloads/fixture.bin",{len(PAYLOAD)},"{HASH}",NULL,NULL,NULL,&Report);
 xvalue* V=xrtValueObject();MdoApiValueSetBool(V,"success",Ok);MdoApiValueSetString(V,"code",Report.Code);MdoApiValueSetString(V,"detail",Report.Detail);
 char* Text=xrtJsonStringify(V,false,NULL);if(Text)MdoHomeAtomicWrite("data/downloads/qa-result.json",Text,strlen(Text),false);xrtFree(Text);xrtValueRelease(V);return 0;
}}
void ServiceUnit(XS_HostInfo* H){{MdoDownloadsStop();if(QaThread){{xrtThreadWait(QaThread);xrtThreadDestroy(QaThread);QaThread=NULL;}}QaBaseUnit(H);}}
XS_RequestResult RequestProc(XS_HttpReq* R){{
 if(R->head->Target.Size>4&&!memcmp(R->head->Target.Data,"/qa/",4)){{
  if(QaThread){{xrtThreadWait(QaThread);xrtThreadDestroy(QaThread);QaThread=NULL;}}
  MdoHomeRemove("data/downloads/qa-result.json",false);snprintf(QaPath,sizeof(QaPath),"%.*s",(int)R->head->Target.Size,R->head->Target.Data);QaThread=xrtThreadCreate(QaTransfer,NULL,0);
  MdoApiContext C={{0}};C.Request=R;MdoApiRequestId(C.RequestId);xvalue* V=xrtValueObject();
  MdoApiValueSetBool(V,"accepted",QaThread!=NULL);MdoApiReplySuccessTake(&C,200,V,NULL);return XS_OK;
 }}return QaBaseRequest(R);
}}
''',encoding='utf-8')
        config.write_text(json.dumps({'engine':{'workers':3},'services':[{'enabled':True,'class':'http','name':'download-qa','ip':'127.0.0.1','port':port,'host_default':{'enabled':True,'name':'mdo','path':'web','devlang':'c','devfile':source.name}}]}),encoding='utf-8')
        log=(base/'host.log').open('wb');process=None
        def start():
            p=subprocess.Popen([str(args.host.resolve()),str(config),'--','--home',str(home)],cwd=ROOT/'app',env=dict(os.environ,MDO_HOME=str(home)),stdout=log,stderr=log,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            try:wait_ready(port,p)
            except Exception:print((base/'host.log').read_text(encoding='utf-8',errors='replace'));p.terminate();p.wait();raise
            return p
        def api(method,path,body=None):
            st,_,data=request(port,method,'/api/v1'+path,body=None if body is None else json.dumps(body).encode(),headers={'Content-Type':'application/json'})
            assert st==200,(st,data);return json.loads(data)['data']
        def rows():return api('GET','/download-tasks')
        def until(check):
            deadline=time.monotonic()+15
            while time.monotonic()<deadline:
                value=check()
                if value:return value
                time.sleep(.025)
            raise AssertionError('download state timeout')
        def transfer(mode):
            st,_,data=request(port,'GET','/qa/'+mode);assert st==200,(st,data)
            assert json.loads(data)['data']['accepted']
            result=home/'data/downloads/qa-result.json'
            until(lambda:result.exists())
            return json.loads(result.read_text(encoding='utf-8'))
        def begin(mode,wait=True):
            result=[];t=threading.Thread(target=lambda:result.append(transfer(mode) if wait else request(port,'GET','/qa/'+mode)));t.start()
            row=until(lambda:next((r for r in rows() if r['identity']=='/qa/'+mode and r['state']=='downloading' and r.get('received',0)>0),None))
            return row,t,result
        try:
            process=start()
            row,t,result=begin('normal');id=row['id'];api('POST',f'/download-tasks/{id}/pause',{})
            time.sleep(.15);paused=next(r for r in rows() if r['id']==id);assert paused['state']=='paused',paused
            api('POST',f'/download-tasks/{id}/resume',{});t.join(15);assert result and result[0]['success'],result
            assert hashlib.sha256((home/'data/downloads/fixture.bin').read_bytes()).hexdigest()==HASH
            # Drop the completed test artifact before the next independent case.
            (home/'data/downloads/fixture.bin').unlink()
            disconnected=transfer('disconnect');assert disconnected['success'],(disconnected,Publisher.requests)
            assert any(m=='disconnect' and n>0 for m,n in Publisher.requests),Publisher.requests
            (home/'data/downloads/fixture.bin').unlink()
            row,t,result=begin('ignore',wait=False);api('POST',f"/download-tasks/{row['id']}/pause",{})
            process.kill();process.wait();t.join(10);process=start()
            restored=next(r for r in rows() if r['id']==row['id']);assert restored['state']=='paused',restored
            api('POST',f"/download-tasks/{row['id']}/resume",{})
            until(lambda:next((r for r in rows() if r['id']==row['id'] and r['state']=='ready'),None))
            assert any(m=='ignore' and n>0 for m,n in Publisher.requests),Publisher.requests
            assert hashlib.sha256((home/'data/downloads/fixture.bin').read_bytes()).hexdigest()==HASH
            (home/'data/downloads/fixture.bin').unlink()
            failed=transfer('corrupt');assert not failed['success'] and failed['code']=='checksum',failed
            assert not (home/'data/downloads/fixture.bin').exists()
            print('PASS native pause/resume, transport retry + Range, process restart, ignored Range restart, SHA-256 rejection')
        finally:
            if process and process.poll() is None:process.terminate();process.wait(timeout=10)
            source.unlink(missing_ok=True);config.unlink(missing_ok=True);log.close();publisher.shutdown();thread.join(timeout=5)

if __name__=='__main__':main()
