"""Opt-in two-machine mdo test: real public UDP, private account relay via SSH.

The public Binding service must already be explicitly deployed. No production
accounts, projects, website or model calls are used. Remote test processes have
a bounded supervisor and own private Home; SSH carries signaling/control only.
"""
from pathlib import Path
import argparse
import hashlib
import ipaddress
import json
import os
import re
import select
import shlex
import shutil
import subprocess
import tarfile
import time
import uuid

from test_account_runtime import ROOT, free_port, request
from test_remote_manager import WebSocket, PASSWORD
from test_remote_bridge import Rpc
from test_remote_live import LiveRpc
from test_remote_rudp import gateway, negotiate
from remote_website_fixture import remote_website_fixture, WEBSITE_HOST


def api(port,method='GET',path='/remote',body=None,status=200):
    headers={}
    if method!='GET':
        _,headers,_=request(port,'GET','/api/v1/remote')
        headers={'X-Mdo-Write-Token':headers['X-Mdo-Write-Token']}
    actual,_,raw=request(port,method,'/api/v1'+path,body,headers)
    assert actual==status,(path,actual,raw[:800])
    value=json.loads(raw); return value.get('data',value)


def until(port,predicate,path='/remote',seconds=20):
    end=time.monotonic()+seconds; value=None
    while time.monotonic()<end:
        try:
            value=api(port,path=path)
            if predicate(value): return value
        except OSError: pass
        time.sleep(.05)
    raise AssertionError(('condition deadline',path,value))


def source(site, port, origin, stun, edition):
    shutil.copytree(ROOT/'app',site,dirs_exist_ok=True,ignore=shutil.ignore_patterns('mdo-home','*.log'))
    unity=site/'generated/mdo_unity.c'
    unity.write_text('#define MDO_ACCOUNT_SERVICE_ORIGIN '+json.dumps(origin)+'\n#define MDO_SERVER_BUILD 1\n'+unity.read_text(encoding='utf-8'),encoding='utf-8')
    for relative,needle,prefix in (
        ('src/remote/direct_rudp.inc.c','MdoPeerPolicyInit(&Direct->Policy,Net,MDO_ACCOUNT_SERVICE_ORIGIN);','Direct->Policy'),
        ('src/remote/lan_gateway.inc.c','MdoPeerPolicyInit(&Policy,&Gateway->Net,MDO_ACCOUNT_SERVICE_ORIGIN);','Policy')):
        path=site/relative; text=path.read_text(encoding='utf-8'); assert text.count(needle)==1
        # Only the locally trusted test policy changes. Actual interface,
        # resolver, STUN, candidates, Noise, bridge and RPC remain production.
        text=text.replace(needle,needle+f' strcpy({prefix}.StunHost,{json.dumps(stun)});')
        path.write_text(text,encoding='utf-8')
    router=site/'src/api/router.c'; text=router.read_text(encoding='utf-8')
    needle='static const MdoApiRoute g_MdoApiRoutes[] = {'; assert text.count(needle)==1
    text=text.replace(needle,'static bool PublicFixtureStop(MdoApiContext* C) { bool Ok=MdoApiReplySuccessTake(C,200u,xrtValueObject(),NULL); xsAppRequestStop(); return Ok; }\n'+needle+
        '\n {"/api/v1/public-fixture-stop",XHTTP_METHOD_GET,"GET",PublicFixtureStop,false},')
    router.write_text(text,encoding='utf-8')
    config=json.loads((site/'xs.json').read_text(encoding='utf-8')); service=config['services'][0]
    service.update({'class':'http','name':'public-'+edition,'ip':'127.0.0.1','port':port}); service.pop('window',None)
    (site/'xs.json').write_text(json.dumps(config),encoding='utf-8')


SUPERVISOR=r'''
from pathlib import Path
import json,os,signal,subprocess,sys
base=Path(__file__).resolve().parent
def interrupted(number,frame): raise InterruptedError('test supervisor interrupted')
for number in (signal.SIGINT,signal.SIGTERM,signal.SIGHUP): signal.signal(number,interrupted)
with (base/'runtime.log').open('wb') as log:
    child=subprocess.Popen([sys.argv[1],str(base/'xs.json')],cwd=base,stdout=log,stderr=log,
        env={**os.environ,'MDO_HOME':str(base/'mdo-home')},start_new_session=True)
    (base/'test-process.json').write_text(json.dumps({'pid':child.pid,'config':str(base/'xs.json')}))
    try: result=child.wait(timeout=240)
    finally:
        if child.poll() is None:
            os.killpg(child.pid,signal.SIGTERM)
            try: child.wait(timeout=8)
            except subprocess.TimeoutExpired: os.killpg(child.pid,signal.SIGKILL); child.wait(timeout=3)
    raise SystemExit(result)
'''


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',required=True,type=Path,help='local native mdo SDK host')
    parser.add_argument('--website-host',type=Path,default=WEBSITE_HOST)
    parser.add_argument('--ssh-bin',type=Path,required=True)
    parser.add_argument('--ssh-key',type=Path,required=True)
    parser.add_argument('--ssh',required=True,help='explicit user@host for the second test device')
    parser.add_argument('--remote-exe',required=True,help='prepared SDK host on the remote machine')
    parser.add_argument('--remote-root',required=True,help='existing private test workspace on the remote machine')
    parser.add_argument('--stun',required=True,help='trusted public Binding hostname, UDP3478')
    parser.add_argument('--reverse',action='store_true',help='remote Linux controller punches into the local Windows target')
    parser.add_argument('--idle-seconds',type=int,choices=range(61,91),default=0,metavar='61..90')
    args=parser.parse_args()
    assert re.fullmatch(r'[a-z][a-z0-9_-]*@[A-Za-z0-9.-]+',args.ssh)
    assert re.fullmatch(r'[A-Za-z0-9.-]{1,253}',args.stun)
    assert args.remote_root.startswith('/home/') and args.remote_exe.startswith(args.remote_root+'/')
    ssh=[str(args.ssh_bin),'-o','BatchMode=yes','-o','StrictHostKeyChecking=yes','-o','ConnectTimeout=10','-i',str(args.ssh_key),args.ssh]
    def remote(code,*values):
        return subprocess.check_output([*ssh,shlex.join(['python3','-',*values])],input=code.encode(),timeout=20).decode()
    def upload(path,target):
        subprocess.run([str(args.ssh_bin.with_name('scp.exe' if os.name=='nt' else 'scp')),
            '-q',*ssh[1:-1],str(path),args.ssh+':'+target],check=True,timeout=40)

    website,website_port,_,_=remote_website_fixture()
    remote_port,local_port=free_port(),free_port()
    target_port,controller_port=(local_port,remote_port) if args.reverse else (remote_port,local_port)
    remote('import socket,sys\ns=[]\nfor p in map(int,sys.argv[1:]):\n a=socket.socket(); a.bind(("127.0.0.1",p)); s.append(a)\n',str(website_port),str(remote_port))
    case=uuid.uuid4().hex; remote_site=args.remote_root+'/case-'+case
    remote('from pathlib import Path\nimport sys\nPath(sys.argv[1]).mkdir(mode=0o700)\n',remote_site)
    local=ROOT/'.build'/('public-remote-'+case); local.mkdir()
    remote_source=local/'remote'; local_source=local/'local'
    origin=f'http://127.0.0.1:{website_port}'
    source(remote_source,remote_port,origin,args.stun,'controller' if args.reverse else 'target')
    source(local_source,local_port,origin,args.stun,'target' if args.reverse else 'controller')
    (remote_source/'supervise.py').write_text(SUPERVISOR,encoding='utf-8')
    archive=local/'target.tar.gz'
    with tarfile.open(archive,'w:gz') as tar:
        for path in sorted(remote_source.rglob('*')):
            if path.is_file(): tar.add(path,arcname=path.relative_to(remote_source).as_posix(),recursive=False)
    upload(archive,remote_site+'/source.tar.gz')
    remote('''from pathlib import Path
import hashlib,sys,tarfile
base=Path(sys.argv[1]); archive=base/'source.tar.gz'
assert hashlib.sha256(archive.read_bytes()).hexdigest()==sys.argv[2]
with tarfile.open(archive) as tar:
 for item in tar.getmembers():
  assert item.isfile() and (base/item.name).resolve().is_relative_to(base.resolve())
 tar.extractall(base)
''',remote_site,hashlib.sha256(archive.read_bytes()).hexdigest())

    processes=[]; files=[]; sockets=[]; tunnel=None; remote_process=None; local_process=None; success=False
    def launch(exe,site):
        log=(site/'test.log').open('ab'); files.append(log)
        process=subprocess.Popen([str(exe.resolve()),str(site/'xs.json')],cwd=site,stdout=log,stderr=log,
            env={**os.environ,'MDO_HOME':str(site/'mdo-home')})
        processes.append(process); return process
    try:
        launch(args.website_host,website)
        end=time.monotonic()+15
        while time.monotonic()<end:
            try:
                if request(website_port,'GET','/admin/login')[0]==200: break
            except OSError: pass
            time.sleep(.05)
        else: raise AssertionError('private website did not start')
        user='public_'+case[:12]
        status,_,_=request(website_port,'POST','/api/v1/register',{'username':user,'password':PASSWORD})
        assert status==201,status
        status,_,raw=request(website_port,'POST','/api/v1/login',{'identifier':user,'password':PASSWORD}); assert status==200
        bearer={'Authorization':'Bearer '+json.loads(raw)['data']['access_token']}

        tunnel_log=(local/'tunnel.log').open('ab'); files.append(tunnel_log)
        tunnel=subprocess.Popen([*ssh[:-1],'-o','ExitOnForwardFailure=yes','-N',
            '-L',f'127.0.0.1:{remote_port}:127.0.0.1:{remote_port}',
            '-R',f'127.0.0.1:{website_port}:127.0.0.1:{website_port}',args.ssh],stdout=tunnel_log,stderr=tunnel_log)
        processes.append(tunnel)
        time.sleep(.4); assert tunnel.poll() is None,'SSH forwarding failed'
        log=(local/'remote-supervisor.log').open('ab'); files.append(log)
        remote_process=subprocess.Popen([*ssh,shlex.join(['python3',remote_site+'/supervise.py',args.remote_exe])],stdout=log,stderr=log)
        processes.append(remote_process)
        local_process=launch(args.host,local_source)
        for port in (target_port,controller_port):
            until(port,lambda value:value['stage']=='disabled')
            api(port,'POST','/account/login',{'identifier':user,'password':PASSWORD,'remember':False})
            until(port,lambda value:value['state']=='signed_in' and not value['busy'],path='/account')
        job=api(target_port,'POST','/remote',{'allow_remote':True,'name':'public UDP target'},202)['job']['id']
        online=until(target_port,lambda value:value['stage']=='online' and value['job']['id']==job)
        device=online['device_id']

        def relay(mode='control'):
            status,_,raw=request(website_port,'POST','/api/v1/devices/ticket',{'device_id':device,'role':'controller','mode':mode},bearer)
            assert status==200,status; ticket=json.loads(raw)['data']
            ws=WebSocket(website_port,{'Origin':f'http://127.0.0.1:{controller_port}'},path=ticket['path'],
                protocol=ticket['protocol']+', xadmin.ticket.'+ticket['ticket']); sockets.append(ws)
            assert json.loads(ws.recv()[1])['type']=='ready'
            hello=json.loads(ws.recv()[1]); assert hello['mode']==mode and 'rudp' in hello
            return ws,hello
        control,hello=relay()
        _,headers,_=request(controller_port,'GET','/api/v1/project-purge-intent'); token=headers['X-Mdo-Write-Token']
        direct=gateway(controller_port,token,hello['rudp']); sockets.append(direct)
        observations=[]; started=time.monotonic()
        ready=negotiate(direct,control,hello['rudp'],observe=lambda who,value:observations.append((who,value)))
        assert ready['transport']=='xrudp'; handshake_ms=round((time.monotonic()-started)*1000,2)
        mappings={}
        for who,value in observations:
            if value['type'] not in ('rudp_candidates','rudp_answer'): continue
            public=[v for v in value['candidates'] if v['kind']=='srflx' and ipaddress.ip_address(v['ip']).is_global]
            assert public,(who,'native STUN must publish a real public reflexive candidate')
            mappings[who]={'public_reflexive':True,'candidate_count':len(value['candidates']),'port':public[0]['port']}
        assert set(mappings)=={'controller','target'},mappings
        rpc=Rpc(direct,hello); fallback=Rpc(control,hello,client_id=rpc.client)
        _,head,code,_=rpc.json('GET','/api/v1/bootstrap'); assert code==200
        target_token=head['X-Mdo-Write-Token']; assert target_token!=token,'two devices must have independent write authority'
        project='public-'+case[:16]
        made,write=rpc.call('POST','/api/v1/projects',json.dumps({'id':project,'name':'public UDP test'}).encode(),
            [['Content-Type','application/json'],['X-Mdo-Write-Token',target_token]])
        assert made['head']['status']==201,made
        assert any(item['id']==project for item in api(target_port,path='/projects')['items'])
        assert not any(item['id']==project for item in api(controller_port,path='/projects')['items'])
        live=LiveRpc(direct,hello); live.live=uuid.uuid4().hex
        live.send({'type':'live_open','id':live.live,'runtime_id':hello['runtime_id'],'token':target_token})
        assert Rpc.receive(live)['type']=='live_opened'; assert live.next_event()['type']=='ready'; live.close_live()

        viewer,view=relay('view'); readonly=gateway(controller_port,token,view['rudp']); sockets.append(readonly)
        negotiate(readonly,viewer,view['rudp'])
        refused,_=Rpc(readonly,view).call('POST','/api/v1/projects',b'{}'); assert refused['code']=='read_only',refused
        if args.idle_seconds:
            end=time.monotonic()+args.idle_seconds
            while time.monotonic()<end:
                available=[ws for ws in sockets if ws.pending]
                if not available:
                    readable,_,_=select.select([ws.socket for ws in sockets],[],[],min(.25,max(0,end-time.monotonic())))
                    available=[ws for ws in sockets if ws.socket in readable]
                for ws in available:
                    opcode,payload=ws.recv(); assert opcode in (9,10),(opcode,payload)
                    if opcode==9: ws.send(10,payload)
            assert rpc.json('GET','/api/v1/projects')[2]==200
        control.send(1,json.dumps({'type':'rudp_cancel','nonce':hello['rudp']['nonce'],'runtime_id':hello['runtime_id']}).encode())
        direct.close(); sockets.remove(direct)
        fallback.send({'type':'receipt','id':write['id'],'runtime_id':hello['runtime_id'],'client_id':rpc.client,'sequence':write['sequence']})
        receipt=fallback.receive(); assert receipt['type']=='receipt' and receipt['state']=='done' and receipt['status']==201,receipt
        assert fallback.json('GET','/api/v1/projects')[2]==200
        api(controller_port,'POST','/account/logout',{})
        until(controller_port,lambda value:value['state']=='signed_out',path='/account')
        try: assert readonly.recv()[0]==8
        except (EOFError,ConnectionResetError): pass
        assert fallback.json('GET','/api/v1/projects')[2]==200 and api(target_port)['stage']=='online'
        api(target_port,'POST','/remote',{'allow_remote':False},202)
        until(target_port,lambda value:not value['allow_remote'])
        api(target_port,'POST','/account/logout',{})
        until(target_port,lambda value:value['state']=='signed_out',path='/account')
        result={'case':case,'stun':args.stun,'transport':'xrudp','two_native_devices':True,'private_relay_only':True,
            'http_write':True,'live':True,'readonly':True,'receipt_after_direct_cancel':True,'controller_logout':True,
            'target_only_project':True,'remote_source_archive_sha256':hashlib.sha256(archive.read_bytes()).hexdigest()}
        result.update(handshake_ms=handshake_ms,native_public_mappings=mappings,reverse=args.reverse,idle_seconds=args.idle_seconds)
        success=True
    finally:
        for ws in sockets: ws.close()
        for port in (controller_port,target_port):
            try: api(port,path='/public-fixture-stop')
            except (OSError,AssertionError): pass
        clean_native=True
        for process in (local_process,remote_process):
            if process:
                try: clean_native=process.wait(timeout=12)==0 and clean_native
                except subprocess.TimeoutExpired: clean_native=False
        try:
            text=remote('from pathlib import Path\nimport sys\np=Path(sys.argv[1])/"runtime.log"\nprint(p.read_text(errors="replace")[-12000:] if p.exists() else "no remote log")\n',remote_site)
            (local/'remote-runtime.log').write_text(text,encoding='utf-8')
            if not success: print('remote log tail',text[-4500:])
        finally:
            for process in reversed(processes):
                if process.poll() is None: process.terminate()
                process.wait(timeout=15)
            for f in files: f.close()
    assert clean_native,'test native hosts must exit successfully and join their engines'
    result['clean_native_exit']=True
    (local/'receipt.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print('PASS real two-machine public xrudp: authenticated private relay, DNS/STUN, Noise, target HTTP/write/live, readonly, receipt/fallback, controller logout and clean shutdown')
    print('evidence',local/'receipt.json')
    return 0


if __name__=='__main__': raise SystemExit(main())
