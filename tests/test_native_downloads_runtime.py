"""Real native single-connection downloads; synthetic files and loopback only.

Uses the project sources and existing host in place, without copying the app,
SDK, or runtime. Includes a process restart against the same disposable Home.
"""
import argparse,hashlib,io,json,os,subprocess,tempfile,threading,time,uuid,zipfile
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
from pathlib import Path
from test_api_runtime import ROOT,free_port,request,wait_ready

PAYLOAD=bytes(range(256))*8192+b'XRTPEND\0'+b'\0'*24
HASH=hashlib.sha256(PAYLOAD).hexdigest()
VERSION='V'*32
archive=io.BytesIO()
with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_STORED) as z:
    z.writestr('extension.json',json.dumps(dict(kind='skills',id='skill:qa.download',alias='download-fixture',name='Download fixture',entry='SKILL.md',platforms=['any'])))
    z.writestr('SKILL.md','---\nname: download-fixture\ndescription: Synthetic download fixture\n---\nNo executable steps.\n')
    z.writestr('sample.bin',PAYLOAD)
ARCHIVE=archive.getvalue();ARCHIVE_HASH=hashlib.sha256(ARCHIVE).hexdigest()
class Publisher(BaseHTTPRequestHandler):
    requests=[]
    def log_message(self,*_):pass
    def do_GET(self):
        if '/extensions/detail?' in self.path:
            body=json.dumps({'code':0,'data':dict(id='skill:qa.download',kind='skills',xid='W'*32,version_id=VERSION,state='published',name='Download fixture',alias='download-fixture',bytes=len(ARCHIVE),sha256=ARCHIVE_HASH)}).encode()
            self.send_response(200);self.send_header('Content-Length',str(len(body)));self.send_header('Content-Type','application/json');self.end_headers();self.wfile.write(body);return
        if '/extensions/archive?' in self.path:
            from urllib.parse import parse_qs,urlsplit
            start=int(parse_qs(urlsplit(self.path).query)['offset'][0]);body=ARCHIVE[start:start+1048576];type(self).requests.append(('extension',start))
            self.send_response(200);self.send_header('Content-Length',str(len(body)));self.send_header('Content-Type','application/zip');self.end_headers()
            try:
                for at in range(0,len(body),16384):self.wfile.write(body[at:at+16384]);self.wfile.flush();time.sleep(.012)
            except (BrokenPipeError,ConnectionResetError,ConnectionAbortedError):pass
            return
        mode=self.path.removeprefix('/qa/');start=0
        if self.path.startswith('/mdo/blob/'):mode='update'
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
    parser=argparse.ArgumentParser();parser.add_argument('--host',type=Path,required=True);parser.add_argument('--serve-ui',action='store_true');args=parser.parse_args()
    with ThreadingHTTPServer(('127.0.0.1',0),Publisher) as publisher,tempfile.TemporaryDirectory(dir=ROOT/'.build',prefix='native-download-') as raw:
        thread=threading.Thread(target=publisher.serve_forever,daemon=True);thread.start()
        base=Path(raw);home=base/'home';port=free_port();stem='.download-qa-'+uuid.uuid4().hex
        (base/'old.bin').write_bytes(b'Synthetic old application')
        source=ROOT/'app'/(stem+'.c');config=ROOT/'app'/(stem+'.json')
        source.write_text(f'''#define MDO_DISTRIBUTION_HOST "127.0.0.1"
#define MDO_DISTRIBUTION_PORT {publisher.server_port}
#define MDO_DISTRIBUTION_SECURE false
#define MDO_ACCOUNT_SERVICE_ORIGIN "http://127.0.0.1:{publisher.server_port}"
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
void ServiceInit(XS_HostInfo* H){{
 QaEngine=H->Server->Engine;QaBaseInit(H);MdoUpdateUnit();g_MdoUpdate.Engine=QaEngine;g_MdoUpdate.Lock=xrtMutexCreate();g_MdoUpdate.Source=xrtStrDup("{(base/'old.bin').as_posix()}");
 MdoUpdateStatus* S=&g_MdoUpdate.Status;S->Enabled=true;S->Available=true;S->Bytes={len(PAYLOAD)};S->BuildId=90000001;S->HasVersionCode=true;S->VersionCode=0x05000806u;
 snprintf(S->Platform,sizeof(S->Platform),"windows-x86_64");snprintf(S->Edition,sizeof(S->Edition),"%s",MdoEdition());snprintf(S->State,sizeof(S->State),"available");
 snprintf(S->Hash,sizeof(S->Hash),"{HASH}");snprintf(S->DownloadPath,sizeof(S->DownloadPath),"/mdo/blob/{HASH}");
 MdoUpdatePolicyLoad(S);g_MdoUpdate.NextCheck=MdoDeadlineAfterUs(3600000000ULL);g_MdoUpdate.Thread=xrtThreadCreate(MdoUpdateThread,NULL,0);
}}
static int32 QaTransfer(void* Unused){{
 MdoTransferReport Report={{0}};xvalue* Archive=NULL;char Error[512]={{0}};
 bool Ok=!strcmp(QaPath,"/qa/extension")?MdoExtensionServiceDownload("skills","{VERSION}",NULL,&Archive,Error):MdoTransferDownloadReport(QaEngine,QaPath,"data/downloads/fixture.bin",{len(PAYLOAD)},"{HASH}",NULL,NULL,NULL,&Report);
 xrtValueRelease(Archive);if(Error[0])snprintf(Report.Detail,sizeof(Report.Detail),"%.255s",Error);
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
            failed_row=next(r for r in rows() if r['identity']=='/qa/corrupt')
            assert failed_row['error_code']=='checksum',failed_row
            row,t,result=begin('cancel');api('POST',f"/download-tasks/{row['id']}/pause",{})
            time.sleep(.15)
            partial=home/'data/downloads/fixture.bin';receipt=home/'data/downloads/fixture.bin.resume.json'
            assert partial.exists() and receipt.exists()
            api('POST',f"/download-tasks/{failed_row['id']}/forget",{})
            assert partial.exists() and receipt.exists(),'Clearing an old failure must preserve the newer download'
            api('POST',f"/download-tasks/{row['id']}/cancel",{});t.join(15)
            assert result and not result[0]['success'],result
            assert not (home/'data/downloads/fixture.bin').exists()
            # Extension archives keep the same task and its first verified chunk.
            result=[];t=threading.Thread(target=lambda:result.append(request(port,'GET','/qa/extension')));t.start()
            def extension_row():
                return next((r for r in rows() if r['source']=='extension' and r.get('received',0)>=1048576),None)
            row=until(extension_row)
            api('POST',f"/download-tasks/{row['id']}/pause",{})
            time.sleep(.9);assert next(r for r in rows() if r['id']==row['id'])['state']=='paused'
            process.kill();process.wait();t.join(10);process=start()
            restored=next(r for r in rows() if r['id']==row['id']);assert restored['state']=='paused',restored
            api('POST',f"/download-tasks/{row['id']}/resume",{})
            until(lambda:next((r for r in rows() if r['id']==row['id'] and r['state']=='ready'),None))
            assert any(m=='extension' and n==1048576 for m,n in Publisher.requests),Publisher.requests
            owned=api('GET','/extension-tasks');assert len([a for a in owned if a.get('store',{}).get('version_id')==VERSION])==1,owned
            assert not (home/'skills'/('W'*32)).exists(),'Downloading an archive must not install it'
            # The updater must restore its accepted policy and same byte offset.
            api('POST','/update/download',{})
            update=until(lambda:next((r for r in rows() if r['source']=='update' and r.get('received',0)>0),None))
            api('POST',f"/download-tasks/{update['id']}/pause",{})
            process.kill();process.wait();process=start()
            assert next(r for r in rows() if r['id']==update['id'])['state']=='paused'
            api('POST',f"/download-tasks/{update['id']}/resume",{})
            until(lambda:api('GET','/update').get('ready'))
            assert any(m=='update' and n>0 for m,n in Publisher.requests),Publisher.requests
            print('PASS native pause/resume, retry + Range, restart, ignored Range, checksum rejection, destination ownership cleanup, cancel cleanup, sealed extension, updater policy + restart resume without install')
            if args.serve_ui:
                transfer('corrupt')
                row,t,result=begin('preview',wait=False);api('POST',f"/download-tasks/{row['id']}/pause",{})
                receipt=ROOT/'.build/download-ui-fixture.json';stop=base/'stop'
                receipt.write_text(json.dumps(dict(url=f'http://127.0.0.1:{port}',home=str(home),stop=str(stop),id=row['id'])),encoding='utf-8')
                print('UI fixture: '+str(receipt),flush=True)
                until_stop=time.monotonic()+900
                while not stop.exists() and time.monotonic()<until_stop:time.sleep(.25)
                receipt.unlink(missing_ok=True)
            # Corrupt/oversized indexes remain intact and cannot break app boot.
            index=home/'data/downloads/tasks.json'
            for broken in (b'[invalid',json.dumps([{}]*129).encode()):
                process.kill();process.wait();index.write_bytes(broken);process=start()
                st,_,_=request(port,'GET','/api/v1/download-tasks');assert st==503,st
                assert index.read_bytes()==broken,'Unreadable task index must not be overwritten'
            print('PASS unreadable/oversized indexes preserved; unrelated app APIs remain available')
        finally:
            if process and process.poll() is None:process.terminate();process.wait(timeout=10)
            source.unlink(missing_ok=True);config.unlink(missing_ok=True);log.close();publisher.shutdown();thread.join(timeout=5)

if __name__=='__main__':main()
