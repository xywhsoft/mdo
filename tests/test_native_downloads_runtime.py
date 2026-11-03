"""Real native single-connection downloads; synthetic files and loopback only.

Uses a small owned source fixture and the existing host, without copying the
SDK or runtime. Includes a process restart against the same disposable Home.
"""
import argparse,hashlib,io,json,os,subprocess,tempfile,threading,time,uuid,zipfile,shutil
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
    clients=[]
    protocol_version='HTTP/1.1'
    def send_response(self,*args):
        super().send_response(*args);self.send_header('Connection','close')
    def log_message(self,*_):pass
    def do_GET(self):
        if '/extensions/detail?' in self.path:
            body=json.dumps({'code':0,'data':dict(id='skill:qa.download',kind='skills',xid='W'*32,version_id=VERSION,state='published',name='Download fixture',alias='download-fixture',bytes=len(ARCHIVE),sha256=ARCHIVE_HASH)}).encode()
            self.send_response(200);self.send_header('Content-Length',str(len(body)));self.send_header('Content-Type','application/json');self.end_headers();self.wfile.write(body);return
        if '/extensions/archive?' in self.path:
            from urllib.parse import parse_qs,urlsplit
            query=parse_qs(urlsplit(self.path).query);sliced='offset' in query;start=int(query.get('offset',['0'])[0]);end=min(start+1048576,len(ARCHIVE))-1 if sliced else len(ARCHIVE)-1
            field=self.headers.get('Range','')
            if field:
                first,last=field.split('=')[1].split('-');start=int(first);end=int(last) if last else len(ARCHIVE)-1
            body=ARCHIVE[start:end+1];type(self).requests.append(('extension',start));type(self).clients.append(('extension',self.headers.get('User-Agent','')))
            chunked=not sliced
            self.send_response(206 if field else 200);self.send_header('Transfer-Encoding' if chunked else 'Content-Length','chunked' if chunked else str(len(body)));self.send_header('Content-Type','application/zip')
            if field:self.send_header('Content-Range',f'bytes {start}-{end}/{len(ARCHIVE)}')
            self.end_headers()
            try:
                for at in range(0,len(body),16384):
                    piece=body[at:at+16384]
                    self.wfile.write((f'{len(piece):x}\r\n'.encode()+piece+b'\r\n') if chunked else piece);self.wfile.flush();time.sleep(.012)
                if chunked:self.wfile.write(b'0\r\n\r\n');self.wfile.flush()
            except (BrokenPipeError,ConnectionResetError,ConnectionAbortedError):pass
            return
        mode=self.path.removeprefix('/qa/');start=0;end=len(PAYLOAD)-1
        if self.path.startswith('/mdo/blob/'):mode='update'
        field=self.headers.get('Range','')
        if field:
            first,last=field.split('=')[1].split('-');start=int(first);end=int(last) if last else end
        type(self).requests.append((mode,start))
        type(self).clients.append((mode,self.headers.get('User-Agent','')))
        if mode=='auth':self.send_response(401);self.send_header('Content-Length','0');self.end_headers();return
        if mode=='ignore':start=0;end=len(PAYLOAD)-1
        partial=bool(field) and mode!='ignore'
        self.send_response(206 if partial else 200)
        if partial:self.send_header('Content-Range',f'bytes {start}-{end}/{len(PAYLOAD)}')
        self.send_header('Content-Length',str(end-start+1));self.send_header('ETag','"fixture-v1"');self.end_headers()
        body=PAYLOAD if mode!='corrupt' else b'x'*len(PAYLOAD)
        try:
            for at in range(start,end+1,16384):
                self.wfile.write(body[at:min(at+16384,end+1)]);self.wfile.flush();time.sleep(.008 if mode!='crash' else .025)
                if mode=='disconnect' and not start and at>=262144:return
        except (BrokenPipeError,ConnectionResetError,ConnectionAbortedError):pass

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--host',type=Path,required=True);parser.add_argument('--serve-ui',action='store_true');parser.add_argument('--aria2',action='store_true');parser.add_argument('--aria2-extension-only',action='store_true');parser.add_argument('--policy-failure-only',action='store_true');args=parser.parse_args()
    # POSIX atomic no-replace directory publication is not supported by WSL's
    # Windows mounts. Keep source in the project and use a tiny native-FS Home.
    with ThreadingHTTPServer(('127.0.0.1',0),Publisher) as publisher,tempfile.TemporaryDirectory(dir=ROOT/'.build',prefix='native-download-') as raw,tempfile.TemporaryDirectory(dir=ROOT/'.build' if os.name=='nt' else None,prefix='native-download-home-') as raw_home:
        thread=threading.Thread(target=publisher.serve_forever,daemon=True);thread.start()
        base=Path(raw);home=Path(raw_home);port=free_port();stem='.download-qa-'+uuid.uuid4().hex
        if args.aria2:
            tool=home/'data/qa-aria2.zip';tool.parent.mkdir(parents=True)
            with zipfile.ZipFile(tool,'w',compression=zipfile.ZIP_DEFLATED) as z:
                z.writestr('extension.json',json.dumps(dict(kind='external-tools',id='external-tool:mdo.aria2',alias='aria2',name='aria2',entry='tool.json',platforms=['windows-x86_64'])))
                z.writestr('tool.json',json.dumps(dict(program='aria2c.exe',commands=[dict(name='aria2c',entry='aria2c.exe')],prompt='Fixture downloader')))
                z.write(ROOT/'tools/runtime/windows-x86_64/aria2/aria2c.exe','aria2c.exe')
        (base/'old.bin').write_bytes(b'Synthetic old application')
        site=base/'site';shutil.copytree(ROOT/'app',site)
        source=site/(stem+'.c');config=site/(stem+'.json')
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
 QaEngine=H->Server->Engine;QaBaseInit(H);
 if({str(args.aria2).lower()}){{char E[512]={{0}};xvalue* Existing=MdoExtensionsResolve("external-tool:mdo.aria2",E),*Installed=NULL;if(!Existing&&!MdoExtensionsInstall("external-tools","{'A'*32}","data/qa-aria2.zip",NULL,true,NULL,&Installed,E))fprintf(stderr,"QA install: %s\\n",E);xrtValueRelease(Existing);xrtValueRelease(Installed);MdoCliReload();}}
 MdoUpdateUnit();g_MdoUpdate.Engine=QaEngine;g_MdoUpdate.Lock=xrtMutexCreate();g_MdoUpdate.Source=xrtStrDup("{(base/'old.bin').as_posix()}");
 MdoUpdateStatus* S=&g_MdoUpdate.Status;S->Enabled=true;S->Available=true;S->Bytes={len(PAYLOAD)};S->BuildId=90000001;S->HasVersionCode=true;S->VersionCode=0x{json.loads((ROOT/"app/release.json").read_text(encoding="utf-8"))["version_code"]}u;
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
 if(xrtStrEqual(R->head->Target,XRT_STR_LITERAL("/qa/disable-aria"))||xrtStrEqual(R->head->Target,XRT_STR_LITERAL("/qa/enable-aria"))){{char E[512]={{0}};bool Ok=MdoExtensionsSetEnabled("external-tools","{'A'*32}",xrtStrEqual(R->head->Target,XRT_STR_LITERAL("/qa/enable-aria")),E)&&MdoCliReload();MdoApiContext C={{0}};C.Request=R;MdoApiRequestId(C.RequestId);if(Ok)MdoApiReplySuccessTake(&C,200,xrtValueObject(),NULL);else MdoApiReplyError(&C,409,"qa",E,NULL);return XS_OK;}}
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
            p=subprocess.Popen([str(args.host.resolve()),str(config),'--','--home',str(home)],cwd=site,env=dict(os.environ,MDO_HOME=str(home)),stdout=log,stderr=log,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
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
            if args.policy_failure_only:
                task_id='d'*32;path='/mdo/blob/'+HASH
                index=home/'data/downloads/tasks.json';index.parent.mkdir(parents=True,exist_ok=True)
                index.write_text(json.dumps([dict(id=task_id,source='update',identity=path,name='mdo',sha256=HASH,bytes=len(PAYLOAD),received=0,state='paused',updated_at=1,context=dict(path=path,relative='data/update/new.exe',build_id=90000001))]),encoding='utf-8')
                blocked=home/'data/update/policy.json';blocked.mkdir(parents=True)
            process=start()
            if args.policy_failure_only:
                api('POST',f'/download-tasks/{task_id}/resume',{})
                failed=until(lambda:next((r for r in rows() if r['id']==task_id and r['state']=='failed'),None))
                assert not failed['active'] and failed['error_code']=='resume' and 'persist' in failed['error'],failed
                until(lambda:not api('GET','/update')['busy'])
                blocked.rmdir()
                api('POST',f'/download-tasks/{task_id}/retry',{})
                ready=until(lambda:next((r for r in rows() if r['id']==task_id and r['state']=='ready'),None))
                assert hashlib.sha256((home/'data/update/new.exe').read_bytes()).hexdigest()==HASH
                assert not ready['active']
                api('POST',f'/download-tasks/{task_id}/forget',{})
                assert not any(r['id']==task_id for r in rows())
                print('PASS update preflight failure settles reserved tasks; repaired storage permits retry and releases dispatch ownership')
                return
            if args.aria2_extension_only:
                assert transfer('extension')['success']
                extension=next(r for r in rows() if r['source']=='extension');assert extension['engine']=='aria2',extension
                print('PASS focused chunked aria2 extension download');return
            if args.aria2:
                row,t,result=begin('normal',wait=False);assert row['engine']=='aria2',row
                api('POST',f"/download-tasks/{row['id']}/pause",{});time.sleep(.6)
                paused=next(r for r in rows() if r['id']==row['id']);assert paused['state']=='paused',paused
                time.sleep(.35);assert next(r for r in rows() if r['id']==row['id'])['received']==paused['received'],'aria2 pause must stop actual progress'
                process.kill();process.wait();t.join(10);process=start()
                restored=next(r for r in rows() if r['id']==row['id']);assert restored['state']=='paused' and restored['engine']=='aria2',restored
                api('POST',f"/download-tasks/{row['id']}/resume",{})
                until(lambda:next((r for r in rows() if r['id']==row['id'] and r['state']=='ready'),None))
                assert hashlib.sha256((home/'data/downloads/fixture.bin').read_bytes()).hexdigest()==HASH
                assert any(m=='normal' and n>0 for m,n in Publisher.requests),Publisher.requests
                (home/'data/downloads/fixture.bin').unlink()
                row,t,result=begin('crash')
                command=f'Get-CimInstance Win32_Process -Filter "ParentProcessId={process.pid}" | Where-Object Name -eq "aria2c.exe" | Select-Object -ExpandProperty ProcessId'
                children=subprocess.check_output(['powershell','-NoProfile','-Command',command],text=True).split()
                assert len(children)==1,children
                for child in children:os.kill(int(child),__import__('signal').SIGTERM)
                t.join(20);assert result and result[0]['success'],result
                fallback=next(r for r in rows() if r['id']==row['id']);assert fallback['engine']=='native' and fallback['fallback_reason']=='aria2_failed',fallback
                assert hashlib.sha256((home/'data/downloads/fixture.bin').read_bytes()).hexdigest()==HASH
                (home/'data/downloads/fixture.bin').unlink()
                result=transfer('auth');assert not result['success'],result
                assert all('aria2' in agent for mode,agent in Publisher.clients if mode=='auth'),'Auth failures must not fall back'
                result=transfer('corrupt');assert not result['success'] and result['code']=='checksum',result
                assert all('aria2' in agent for mode,agent in Publisher.clients if mode=='corrupt'),'Integrity failures must not fall back'
                row,t,result=begin('cancel');api('POST',f"/download-tasks/{row['id']}/cancel",{});t.join(15)
                assert result and not result[0]['success'],result
                assert not (home/f"data/downloads/aria2/{row['id']}").exists()
                assert transfer('extension')['success']
                extension=next(r for r in rows() if r['source']=='extension');assert extension['engine']=='aria2',extension
                assert any('aria2' in agent for mode,agent in Publisher.clients if mode=='extension')
                archives=api('GET','/extension-tasks');assert len(archives)==1 and archives[0]['state']=='ready',archives
                assert not (home/'skills'/('W'*32)).exists()
                api('POST','/update/download',{})
                until(lambda:api('GET','/update').get('ready'))
                update=next(r for r in rows() if r['source']=='update');assert update['engine']=='aria2' and update['state']=='ready',update
                st,_,data=request(port,'GET','/qa/disable-aria');assert st==200,(st,data)
                result=transfer('disabled');assert result['success'],result
                assert next(r for r in rows() if r['identity']=='/qa/disabled')['engine']=='native'
                command=f'Get-CimInstance Win32_Process -Filter "ParentProcessId={process.pid}" | Where-Object Name -eq "aria2c.exe" | Select-Object -ExpandProperty ProcessId'
                assert not subprocess.check_output(['powershell','-NoProfile','-Command',command],text=True).strip(),'Completed downloads must stop owned child processes'
                print('PASS real aria2 auto-detection, pause/restart resume, child crash fallback, auth/checksum no fallback, cancel cleanup, chunked whole extension seal, updater, disabled-tool native selection and child cleanup',flush=True)
                if args.serve_ui:
                    st,_,data=request(port,'GET','/qa/enable-aria');assert st==200,(st,data)
                    (home/'data/downloads/fixture.bin').unlink(missing_ok=True)
                    row,t,result=begin('preview',wait=False);api('POST',f"/download-tasks/{row['id']}/pause",{})
                    receipt=ROOT/'.build/download-ui-fixture.json';stop=base/'stop'
                    receipt.write_text(json.dumps(dict(url=f'http://127.0.0.1:{port}',home=str(home),stop=str(stop),id=row['id'])),encoding='utf-8')
                    print('UI fixture: '+str(receipt),flush=True)
                    until_stop=time.monotonic()+900
                    while not stop.exists() and time.monotonic()<until_stop:time.sleep(.25)
                    receipt.unlink(missing_ok=True)
                return
            row,t,result=begin('normal');id=row['id'];api('POST',f'/download-tasks/{id}/pause',{})
            time.sleep(.15);paused=next(r for r in rows() if r['id']==id);assert paused['state']=='paused',paused
            index=home/'data/downloads/tasks.json';saved=index.read_bytes();index.unlink();index.mkdir()
            st,_,_=request(port,'POST',f'/api/v1/download-tasks/{id}/resume',body=b'{}',headers={'Content-Type':'application/json'})
            assert st!=200,'An unsaved resume must not be accepted'
            time.sleep(.15);assert next(r for r in rows() if r['id']==id)['state']=='paused'
            index.rmdir();index.write_bytes(saved)
            api('POST',f'/download-tasks/{id}/resume',{});t.join(15);assert result and result[0]['success'],result
            assert hashlib.sha256((home/'data/downloads/fixture.bin').read_bytes()).hexdigest()==HASH
            # Drop the completed test artifact before the next independent case.
            (home/'data/downloads/fixture.bin').unlink()
            saved=index.read_bytes();index.unlink();index.mkdir()
            assert not transfer('save-failure')['success']
            assert not any(r['identity']=='/qa/save-failure' for r in rows()),'Failed registration must not leak a half-built task'
            index.rmdir();index.write_bytes(saved)
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
            api('POST',f"/extension-tasks/{owned[0]['id']}/install",dict(trust_code=True,expected_revision=''))
            assert next(r for r in rows() if r['id']==row['id'])['state']=='complete','Installation receipt must reconcile native history'
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
            # One damaged record cannot hide healthy tasks; preserve original
            # bytes in quarantine before publishing the repaired registry.
            index=home/'data/downloads/tasks.json'
            process.kill();process.wait();healthy=json.loads(index.read_text(encoding='utf-8'))
            # Simulate the subsequent application build consuming its update.
            installed=next(r for r in healthy if r['source']=='update');installed['context']['build_id']=1
            broken=json.dumps([{},*healthy,healthy[0]]).encode();index.write_bytes(broken);process=start()
            assert len(rows())==len(healthy)
            assert any(p.read_bytes()==broken for p in index.parent.glob('tasks.rejected-*.json'))
            # Simulate a file which cannot be removed (an unexpected directory).
            process.kill();process.wait();record=next(r for r in healthy if r['source']=='transfer');record['state']='paused'
            index.write_text(json.dumps([record]),encoding='utf-8')
            partial=home/record['context']['relative'];partial.unlink(missing_ok=True);partial.mkdir()
            meta=Path(str(partial)+'.resume.json');meta.write_text(json.dumps(dict(path=record['identity'],sha256=record['sha256'],size=record['bytes'])))
            process=start();api('POST',f"/download-tasks/{record['id']}/cancel",{})
            assert rows()[0]['state']=='cancelled' and rows()[0]['cleanup_pending']
            partial.rmdir();api('POST',f"/download-tasks/{record['id']}/cleanup",{})
            assert not rows()[0]['cleanup_pending'] and not meta.exists()
            for broken in (b'[invalid',json.dumps([{}]*129).encode()):
                process.kill();process.wait();index.write_bytes(broken);process=start()
                assert rows()==[]
                assert any(p.read_bytes()==broken for p in index.parent.glob('tasks.rejected-*.json'))
            assert transfer('repaired')['success']
            # Retain unfinished and ready installation work at capacity; only
            # completed file history may make room for another download.
            process.kill();process.wait();template=next(r for r in healthy if r['source']=='transfer')
            history=[]
            for i in range(128):
                record=json.loads(json.dumps(template));record.update(id=str(i).zfill(32),identity=f'/qa/history-{i}',state='ready',received=record['bytes'],updated_at=int(time.time()*1000000))
                record['context']=dict(path=record['identity'],relative=f'data/downloads/history-{i}.bin');history.append(record)
            history[-2]['state']='paused'
            history[-1].update(source='update',identity='/mdo/blob/'+HASH)
            history[-1]['context']=dict(path=history[-1]['identity'],relative='data/update/new.exe',build_id=90000001)
            index.write_text(json.dumps(history),encoding='utf-8');process=start()
            assert transfer('capacity')['success'];assert len(rows())==128
            assert any(r['id']==history[-2]['id'] and r['state']=='paused' for r in rows())
            assert any(r['id']==history[-1]['id'] and r['state']=='ready' for r in rows())
            process.kill();process.wait();history=json.loads(index.read_text(encoding='utf-8'))
            expired=[r for r in history if r['source']=='transfer' and r['state']=='ready'][:2]
            for record,state in zip(expired,('complete','cancelled')):record.update(state=state,updated_at=1,cleanup_pending=False)
            index.write_text(json.dumps(history),encoding='utf-8');process=start()
            assert transfer('retention')['success']
            assert not {r['id'] for r in expired}&{r['id'] for r in rows()}
            print('PASS isolated bad/duplicate records, preserved corrupt indexes, new downloads after repair, cleanup failure + retry')
            print('PASS failed task registration/resume rollback, completed history pruning at capacity and 30-day retention without discarding unfinished packages')
        except Exception:
            print((base/'host.log').read_text(encoding='utf-8',errors='replace')[-6000:])
            print('Publisher requests:',Publisher.requests)
            raise
        finally:
            if process and process.poll() is None:process.terminate();process.wait(timeout=10)
            if args.aria2:time.sleep(2)
            source.unlink(missing_ok=True);config.unlink(missing_ok=True);log.close();publisher.shutdown();thread.join(timeout=5)

if __name__=='__main__':main()
