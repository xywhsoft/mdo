"""Two real native mdo devices and a disposable authenticated relay.

No production data, public model calls, shell tools or load tests. Covers a
single session, a project larger than the backup limit, and restart recovery.
"""
from pathlib import Path
import argparse
import hashlib
import http.client
import io
import json
import os
import shutil
import subprocess
import tempfile
import threading
import time
import uuid
from http.server import ThreadingHTTPServer

from remote_website_fixture import remote_website_fixture, WEBSITE_HOST
from test_account_runtime import request, free_port, PASSWORD
from test_live_runtime import Model
from test_packed_queue_start_recovery import configure_model

ROOT=Path(__file__).resolve().parents[1]

class RecordingModel(Model):
    requests=[]
    def do_POST(self):
        raw=self.rfile.read(int(self.headers['Content-Length']))
        self.requests.append(json.loads(raw))
        self.rfile=io.BytesIO(raw)
        super().do_POST()

class Device:
    def __init__(self, base, name, host, origin, crash_commit=False, relay_only=False):
        self.site=base/name; shutil.copytree(ROOT/'app',self.site)
        self.home=self.site/'mdo-home'; self.host=host; self.port=free_port()
        if crash_commit:
            commit=self.site/'src/transfers/commit.c'
            code=commit.read_text(encoding='utf-8')
            hook='    if (!MdoAccountSetString(Task,"destination_workspace",Workspace)) goto done;'
            assert code.count(hook)==1
            code=code.replace(hook,'''    if (Full) {
        bool FixtureSeen=false;
        (void)MdoHomeExternalStat("data/fixture-commit-crash",&FixtureSeen,NULL);
        if (!FixtureSeen) {
            (void)MdoDtWrite("data/fixture-commit-crash","ready",5u);
            while (!xrtCancelRequested(Cancel)) xrtSleep(10u);
        }
    }
'''+hook)
            commit.write_text(code,encoding='utf-8')
        if relay_only:
            rpc=self.site/'src/transfers/rpc.c'
            code=rpc.read_text(encoding='utf-8')
            assert code.count('    if (Rpc->Offer) {')==1
            rpc.write_text(code.replace('    if (Rpc->Offer) {','    if (false && Rpc->Offer) {'),encoding='utf-8')
        unity=self.site/'generated/mdo_unity.c'
        unity.write_text('#define MDO_ACCOUNT_SERVICE_ORIGIN '+json.dumps(origin)+'\n'+unity.read_text(encoding='utf-8'),encoding='utf-8')
        (self.site/'xs.json').write_text(json.dumps({'services':[{'enabled':True,'class':'http','name':'mdo','ip':'127.0.0.1','port':self.port,
            'recv_limit':8454144,'body_limit':8388608,'host_default':{'enabled':True,'name':'mdo','path':'web','devlang':'c','devfile':'generated/mdo_unity.c'}}]}))
        self.process=None
    def start(self):
        self.log=(self.site/'native.log').open('ab')
        self.process=subprocess.Popen([str(self.host),str(self.site/'xs.json'),'--','--home',str(self.home)],cwd=self.site,
            env=dict(os.environ,MDO_QUEUE_FIXTURE_KEY='local-transfer-fixture'),stdout=self.log,stderr=self.log)
        self.wait(lambda:self.api('GET','/bootstrap'), 'native startup', timeout=25)
    def stop(self):
        if self.process:
            self.process.terminate()
            try:self.process.wait(timeout=10)
            except subprocess.TimeoutExpired:self.process.kill();self.process.wait(timeout=5)
            self.process=None;self.log.close()
    def api(self,method,path,body=None,status=200):
        headers={}
        if method!='GET':
            s,h,b=request(self.port,'GET','/api/v1/bootstrap'); assert s==200,b
            headers['X-Mdo-Write-Token']=h['X-Mdo-Write-Token']
            if method in ('PATCH','DELETE'):
                s,h,b=request(self.port,'GET','/api/v1'+path); assert s==200,b
                headers['If-Match']=h.get('ETag',h.get('etag'))
        actual,head,raw=request(self.port,method,'/api/v1'+path,body,headers)
        assert actual==status,(method,path,actual,raw[:2000])
        value=json.loads(raw)
        return value.get('data',value)
    def upload(self,path,data,status=200):
        _,head,_=request(self.port,'GET','/api/v1/bootstrap')
        connection=http.client.HTTPConnection('127.0.0.1',self.port,timeout=10)
        try:
            connection.request('PUT','/api/v1'+path,data,{'Content-Type':'application/octet-stream','X-Mdo-Write-Token':head['X-Mdo-Write-Token']})
            reply=connection.getresponse(); raw=reply.read();assert reply.status==status,(reply.status,raw)
            return json.loads(raw)
        finally:connection.close()
    def wait(self,predicate,label,timeout=30):
        end=time.monotonic()+timeout; last=None
        while time.monotonic()<end:
            if self.process and self.process.poll() is not None:
                raise AssertionError((label,(self.site/'native.log').read_text(errors='replace')[-6000:]))
            try:
                last=predicate()
                if last:return last
            except (OSError,AssertionError) as error:last=error
            time.sleep(.1)
        raise AssertionError((label,last,(self.site/'native.log').read_text(errors='replace')[-2000:]))
    def login(self):
        if self.api('GET','/account')['state']!='signed_in':
            self.api('POST','/account/login',{'identifier':'transfer_fixture','password':PASSWORD,'remember':True})
        self.wait(lambda:self.api('GET','/account')['state']=='signed_in','login')
        remote=self.api('GET','/remote')
        if not remote['allow_remote']:
            self.api('POST','/remote',{'allow_remote':True,'name':self.site.name},202)
        self.wait(lambda:self.api('GET','/remote')['stage']=='online','remote online')
        return self.api('GET','/remote')['device_id']

def run(host, website_host, base, size_mib=65, interrupt=False, crash_commit=False, relay_only=False):
    site,port,user,password=remote_website_fixture()
    log=(site/'transfer-relay.log').open('ab')
    website=subprocess.Popen([str(website_host),str(site/'xs.json')],cwd=site,stdout=log,stderr=log)
    RecordingModel.requests=[]
    model=ThreadingHTTPServer(('127.0.0.1',0),RecordingModel)
    threading.Thread(target=model.serve_forever,daemon=True).start()
    devices=[]
    try:
        for _ in range(200):
            try:
                if request(port,'GET','/admin/login')[0]==200:break
            except OSError:pass
            time.sleep(.05)
        actual,_,body=request(port,'POST','/api/v1/register',{'username':'transfer_fixture','password':PASSWORD})
        assert actual==201,body
        for name in ('source','target'):
            device=Device(base,name,host,f'http://127.0.0.1:{port}',crash_commit and name=='target',relay_only);devices.append(device);device.start();device.login()
        a,b=devices; bid=b.api('GET','/remote')['device_id']
        assert not b.api('GET','/device-transfers/capabilities')['allow_receive']
        b.api('POST','/device-transfers/incoming/'+uuid.uuid4().hex,{},403)
        b.api('PUT','/device-transfers/policy',{'allow_receive':True})
        assert b.api('GET','/device-transfers/capabilities')['allow_receive']
        configure_model(a.port,f'http://127.0.0.1:{model.server_port}/v1')
        workspace=base/'project-files';workspace.mkdir();(workspace/'chapter-中文.txt').write_text('Transfer project\n',encoding='utf-8')
        (workspace/'.hidden').write_bytes(b'keep hidden');(workspace/'empty').mkdir()
        # One boundary-size file proves the project is not a 64 MiB JSON backup.
        with (workspace/'large.bin').open('wb') as output:
            output.write(bytes(range(256))*4096)
            output.truncate(size_mib*1024*1024+17)
        a.api('POST','/projects',{'id':'transfer-source','name':'传输项目','workspace_root':str(workspace)},201)
        sessions=[]
        for title in ('Main context','Trash context'):
            session=a.api('POST','/sessions',{'project_id':'transfer-source','title':title,'agent_id':'mdo.default',
                'model_id':'queue-fixture','protocol':'openai-chat-completions','permission_profile':'read-only'},201)
            sessions.append(session['id'])
        sid=sessions[0];prefix=f'/projects/transfer-source/sessions/{sid}'
        run=a.api('POST',prefix+'/runs',{'prompt':'Preserve this transfer context','timeout_ms':20000},202)
        a.wait(lambda:next((r for r in a.api('GET','/runs')['items'] if r['id']==run['id'] and r['terminal']),None),'model done')
        a.api('PUT',prefix+'/draft',{'revision':0,'text':'Preserve unsent 草稿'})
        a.api('POST',prefix+'/queue',{'id':uuid.uuid4().hex,'text':'Do not automatically execute this queued prompt','first':False,'stage':True},201)
        a.api('DELETE',f'/projects/transfer-source/sessions/{sessions[1]}')
        memory=a.home/'memory/projects/transfer-source';memory.mkdir(parents=True,exist_ok=True)
        (memory/'MEMORY.md').write_text('# Project memory\n- keep.md\n',encoding='utf-8')
        (memory/'keep.md').write_text('Preserve project facts.',encoding='utf-8')
        source_before={p.relative_to(a.home/'sessions/transfer-source').as_posix():p.read_bytes()
            for p in (a.home/'sessions/transfer-source').rglob('*') if p.is_file() and p.name!='.runtime.lock'}
        def transfer(kind, restart=False):
            identifier=uuid.uuid4().hex
            body={'id':identifier,'kind':kind,'project_id':'transfer-source','device_id':bid}
            if kind=='session':body['session_id']=sid
            a.api('POST','/device-transfers',body,202)
            assert a.api('POST','/device-transfers',body)['id']==identifier
            a.api('POST','/device-transfers',{**body,'title':'different identity'},409)
            a.wait(lambda:(v if (v:=a.api('GET','/device-transfers/'+identifier))['state'] in ('awaiting_confirmation','paused') else None),'planning')
            state=a.api('GET','/device-transfers/'+identifier);assert state['state']=='awaiting_confirmation',state
            confirmation={'action':'confirm','target_project_id':'default','target_directory':''}
            a.api('POST','/device-transfers/'+identifier,confirmation,202)
            a.api('POST','/device-transfers/'+identifier,confirmation)
            a.api('POST','/device-transfers/'+identifier,{**confirmation,'target_directory':str(base)},409)
            if restart:
                progress=a.wait(lambda:(v if (v:=a.api('GET','/device-transfers/'+identifier)).get('current_file','').endswith('/large.bin') and v.get('current_file_bytes',0)>=1048576 else None),'partial upload',60)
                assert progress['state']!='completed'
                b.stop();a.stop();b.start();bid_after=b.login();a.start();a.login()
                if bid_after!=bid:
                    a.wait(lambda:not a.api('GET','/device-transfers/'+identifier).get('working',False),'worker idle')
                    a.api('POST','/device-transfers/'+identifier,{'action':'resume','device_id':bid_after},202)
            if kind=='project' and crash_commit:
                b.wait(lambda:(b.home/'data/fixture-commit-crash').exists(),'commit publication fault',120)
                assert not any(p['id']=='received-'+identifier for p in b.api('GET','/projects')['items']),'partial project visible'
                b.stop();b.start();b.login()
            state=a.wait(lambda:(v if (v:=a.api('GET','/device-transfers/'+identifier))['state'] in ('completed','paused') else None),'transfer '+kind,timeout=120)
            assert state['state']=='completed',state
            assert state.get('route')==('relay' if relay_only else 'lan'),state
            return identifier,state
        single_id,single=transfer('session')
        assert (b.home/'sessions/default'/single['destination_session_id']/'snapshot.json').is_file()
        copied=json.loads((b.home/'sessions/default'/single['destination_session_id']/'draft.json').read_text())
        assert copied['text']=='Preserve unsent 草稿'
        queue=b.api('GET',f"/projects/default/sessions/{single['destination_session_id']}/queue")
        assert 'Do not automatically execute' in json.dumps(queue),queue
        assert len(RecordingModel.requests)==1,'restored queue auto-executed'
        project_id,project=transfer('project',interrupt)
        target=Path(project['destination_workspace'])
        if os.name=='nt':
            manifest=json.loads((a.home/'data/device-transfers/outgoing'/project_id/'manifest.json').read_text(encoding='utf-8'))
            for entry in manifest['files']:
                assert entry['mode']==(0o700 if entry['type']=='directory' else 0o600),entry
        else:
            assert (target/'chapter-中文.txt').stat().st_mode & 0o777 == (workspace/'chapter-中文.txt').stat().st_mode & 0o777
            assert (target/'empty').stat().st_mode & 0o777 == (workspace/'empty').stat().st_mode & 0o777
        assert (target/'chapter-中文.txt').read_bytes()==(workspace/'chapter-中文.txt').read_bytes()
        assert (target/'large.bin').stat().st_size==size_mib*1024*1024+17
        assert hashlib.sha256((target/'large.bin').read_bytes()).digest()==hashlib.sha256((workspace/'large.bin').read_bytes()).digest()
        received=b.api('GET','/sessions')['items'];received=[s for s in received if s['project_id']==project['destination_project_id']]
        assert len(received)==2 and {s['status'] for s in received}=={'active','trash'},received
        assert (b.home/'memory/projects'/project['destination_project_id']/'keep.md').read_bytes()==(memory/'keep.md').read_bytes()
        assert source_before=={p.relative_to(a.home/'sessions/transfer-source').as_posix():p.read_bytes()
            for p in (a.home/'sessions/transfer-source').rglob('*') if p.is_file() and p.name!='.runtime.lock'}
        b.stop();b.start();b.login()
        state=b.api('GET','/device-transfers/incoming/'+project_id)
        assert state['state']=='completed'
        assert len([s for s in b.api('GET','/sessions')['items'] if s['project_id']==project['destination_project_id']])==2
        assert len(RecordingModel.requests)==1,'transfer invoked a model'
        configure_model(b.port,f'http://127.0.0.1:{model.server_port}/v1')
        follow=b.api('POST',f"/projects/default/sessions/{single['destination_session_id']}/runs",{'prompt':'Continue after device transfer','timeout_ms':20000},202)
        b.wait(lambda:next((r for r in b.api('GET','/runs')['items'] if r['id']==follow['id'] and r['terminal']),None),'target continuation')
        assert 'Preserve this transfer context' in json.dumps(RecordingModel.requests[-1]),'source history missing from continued model context'
        # Target validates paths and binds the manifest kind to the admitted offer.
        member=a.api('GET','/account')['profile']['id']
        manifest={'schema_version':1,'kind':'project','project_id':'x','name':'test','source_workspace':'x','captured_at':0,'total_bytes':0,
            'files':[{'path':'workspace/../outside','type':'file','bytes':0,'mode':384,'sha256':hashlib.sha256(b'').hexdigest()}]}
        def invalid_manifest(document, offered_kind='project'):
            raw=json.dumps(document).encode();identifier=uuid.uuid4().hex
            offer={'member_id':member,'manifest_bytes':len(raw),'manifest_sha256':hashlib.sha256(raw).hexdigest(),'total_bytes':document['total_bytes'],
                'kind':offered_kind,'target_project_id':'default','target_directory':'','source_device':''}
            b.api('POST','/device-transfers/incoming/'+identifier,offer,201)
            b.upload('/device-transfers/incoming/'+identifier+'/manifest/0',raw,422)
            assert not (b.home/'projects'/('received-'+identifier+'.json')).exists()
            b.api('POST','/device-transfers/incoming/'+identifier+'/cancel',{})
        invalid_manifest(manifest)
        manifest['files'][0]['path']='workspace/CON.txt'
        invalid_manifest(manifest)
        manifest['files'][0]['path']='workspace/valid'
        invalid_manifest(manifest,'session')
        manifest['files'] += [{**manifest['files'][0],'path':'workspace/valid/child'}]
        invalid_manifest(manifest)
        print(f'PASS native transfers: {size_mib} MiB workspace, drafts/staged queue/trash/memory, duplicate/conflict protection, source unchanged, continuation, invalid paths/kind, durable completion'+(', both-device mid-upload restart' if interrupt else '')+(', interrupted hidden publication recovered' if crash_commit else '')+(', forced relay' if relay_only else ''))
    finally:
        for device in devices:device.stop()
        model.shutdown();model.server_close();website.terminate()
        try:website.wait(timeout=10)
        except subprocess.TimeoutExpired:website.kill();website.wait(timeout=5)
        log.close()

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--host',type=Path,required=True);parser.add_argument('--website-host',type=Path,default=WEBSITE_HOST)
    parser.add_argument('--work',type=Path);parser.add_argument('--size-mib',type=int,default=65);parser.add_argument('--interrupt',action='store_true')
    parser.add_argument('--crash-commit',action='store_true');parser.add_argument('--relay-only',action='store_true');args=parser.parse_args()
    base=args.work or Path(tempfile.mkdtemp(prefix='device-transfer-',dir=ROOT/'.build'))
    base.mkdir(parents=True,exist_ok=True)
    run(args.host.resolve(),args.website_host.resolve(),base.resolve(),args.size_mib,args.interrupt,args.crash_commit,args.relay_only)
