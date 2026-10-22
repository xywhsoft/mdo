"""Real-wallclock persistent mdo xrudp HTTP/live/binary/reconnect soak.

Only disposable local accounts, website and Home. No models or public services.
The 72h receipt is impossible in the short smoke mode. A stop file cancels the
run; a process/clock gap or uncertain write fails it rather than replaying work.
This is the mdo layer gate; native multi/message/game soak is a separate gate.
"""
from pathlib import Path
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
import select
import struct
import subprocess
import sys
import time
import traceback
import uuid
import zlib

from test_account_runtime import ROOT, request
from test_remote_manager import run, WebSocket, PASSWORD
from test_remote_bridge import Rpc
from test_remote_live import LiveRpc
from remote_website_fixture import WEBSITE_HOST
import test_remote_rudp as native
from test_rudp_discovery import Oracles

sys.path.insert(0,str(ROOT/'tools'))
import build_mdo

HOOK='''static bool SoakStats(MdoApiContext* C) {
    xvalue* V=xrtValueObject();
    if (!V || !MdoAccountSetUInt(V,"transports",xrtAtomic32Load(&g_MdoRudpTransports,XMEMORY_ACQUIRE)) ||
        !MdoAccountSetBool(V,"account_session",MdoAccountHasSession())) { xrtValueRelease(V); return false; }
    return MdoApiReplySuccessTake(C,200u,V,NULL);
}
static bool SoakExpireAccess(MdoApiContext* C) {
    xrtMutexLock(g_MdoAccount.Lock); g_MdoAccount.Tokens.Expires=0;
    xrtMutexUnlock(g_MdoAccount.Lock); return SoakStats(C);
}'''
ROUTE=' {"/api/v1/test-soak-stats",XHTTP_METHOD_GET,"GET",SoakStats,false},'
EXPIRY_ROUTE=' {"/api/v1/test-soak-expire",XHTTP_METHOD_POST,"POST",SoakExpireAccess,false},'
_WINDOWS_MEMORY=None


class SoakGateError(AssertionError):
    """Record fixed gate codes instead of potentially sensitive error text."""
    def __init__(self, code):
        super().__init__(code)
        self.code=code


def observe_clock(state, now, previous, started=None, wall=None, wall_now=None):
    clock={}
    if previous is not None:
        gap=now-previous
        clock['tick_gap_seconds']=gap
        state['largest_tick_gap']=max(state.get('largest_tick_gap',0),gap)
    if started is not None:
        clock.update(monotonic_elapsed_seconds=now-started,wall_elapsed_seconds=wall_now-wall)
        clock['offset_seconds']=clock['wall_elapsed_seconds']-clock['monotonic_elapsed_seconds']
    state['clock']=clock
    if previous is not None and not 0<=clock['tick_gap_seconds']<15:
        raise SoakGateError('observation_gap')
    if started is not None and not abs(clock['offset_seconds'])<5:
        raise SoakGateError('clock_diverged')


def record_failure(state, error):
    state.update(status='cancelled' if isinstance(error,InterruptedError) else 'failed',
        acceptance_72h=False,error_type=type(error).__name__,
        failure_code=error.code if isinstance(error,SoakGateError) else 'unclassified')
    state['error_frames']=[{'file':Path(v.filename).name,'line':v.lineno,'function':v.name}
        for v in traceback.extract_tb(error.__traceback__)]


def windows_memory_backend():
    global _WINDOWS_MEMORY
    if _WINDOWS_MEMORY is not None: return _WINDOWS_MEMORY
    class Counters(ctypes.Structure):
        _fields_=[('cb',wintypes.DWORD),('faults',wintypes.DWORD)]+[(name,ctypes.c_size_t) for name in
            ('peak_rss','rss','peak_paged','paged','peak_nonpaged','nonpaged','pagefile','peak_pagefile','private')]
    kernel=ctypes.WinDLL('kernel32',use_last_error=True); psapi=ctypes.WinDLL('psapi',use_last_error=True)
    kernel.OpenProcess.argtypes=[wintypes.DWORD,wintypes.BOOL,wintypes.DWORD]; kernel.OpenProcess.restype=wintypes.HANDLE
    kernel.CloseHandle.argtypes=[wintypes.HANDLE]; kernel.CloseHandle.restype=wintypes.BOOL
    psapi.GetProcessMemoryInfo.argtypes=[wintypes.HANDLE,ctypes.POINTER(Counters),wintypes.DWORD]
    psapi.GetProcessMemoryInfo.restype=wintypes.BOOL
    kernel.GetProcessHandleCount.argtypes=[wintypes.HANDLE,ctypes.POINTER(wintypes.DWORD)]
    kernel.GetProcessHandleCount.restype=wintypes.BOOL
    _WINDOWS_MEMORY=kernel,psapi,Counters
    return _WINDOWS_MEMORY


def source_hash(path):
    digest=hashlib.sha256()
    for folder in ('src','include','generated','web'):
        for item in sorted((path/folder).rglob('*')):
            if item.is_file(): digest.update(item.relative_to(path).as_posix().encode()+b'\0'+item.read_bytes()+b'\0')
    return digest.hexdigest()


def memory(pid):
    if os.name!='nt':
        values={line.split(':')[0]:line.split(':')[1].strip() for line in Path(f'/proc/{pid}/status').read_text().splitlines() if ':' in line}
        return {'rss':int(values['VmRSS'].split()[0])*1024,'private':int(values['VmData'].split()[0])*1024,
            'handles':len(list(Path(f'/proc/{pid}/fd').iterdir()))}
    kernel,psapi,Counters=windows_memory_backend()
    handle=kernel.OpenProcess(0x400|0x10,False,pid); assert handle,'resource query open failed'
    try:
        value=Counters(); value.cb=ctypes.sizeof(value); count=wintypes.DWORD()
        assert psapi.GetProcessMemoryInfo(handle,ctypes.byref(value),value.cb),'resource query failed'
        assert kernel.GetProcessHandleCount(handle,ctypes.byref(count)),'handle query failed'
        return {'rss':value.rss,'private':value.private,'handles':count.value}
    finally: kernel.CloseHandle(handle)


def png():
    def chunk(kind,body): return struct.pack('!I',len(body))+kind+body+struct.pack('!I',zlib.crc32(kind+body)&0xffffffff)
    row=bytes(range(256))*8; pixels=(b'\0'+row)*160
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('!IIBBBBB',512,160,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(pixels,0))+chunk(b'IEND',b'')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',type=Path,required=True); parser.add_argument('--website-host',type=Path,default=WEBSITE_HOST)
    parser.add_argument('--state',type=Path,required=True)
    parser.add_argument('--seconds',type=int,default=72*3600)
    parser.add_argument('--period',type=float,default=5); parser.add_argument('--upload-every',type=float,default=600)
    parser.add_argument('--idle-every',type=float,default=1800); parser.add_argument('--idle-seconds',type=float,default=70)
    parser.add_argument('--reconnect-every',type=float,default=3600)
    parser.add_argument('--exercise-expiry',action='store_true',help='short regression only: make local access due while retaining the session')
    parser.add_argument('--refresh-delay-ms',type=int,default=0,help='short fixture only: delay its native refresh client, never production or long acceptance')
    args=parser.parse_args(); assert 20<=args.seconds<=7*86400
    lock=build_mdo.load_object(ROOT/'deps.lock')
    build_mdo.verify_host_receipt(args.host,lock,False)
    app_hash=source_hash(ROOT/'app')
    assert not args.exercise_expiry or args.seconds<3600,'never accelerate expiry in 72h acceptance'
    assert 0<=args.refresh_delay_ms<=2000 and (not args.refresh_delay_ms or args.seconds<3600)
    assert min(args.period,args.upload_every,args.idle_every,args.idle_seconds,args.reconnect_every)>0
    state_path=args.state.resolve(); state_path.parent.mkdir(parents=True,exist_ok=True)
    assert not state_path.exists(),'use a new private receipt path; interrupted acceptance is never silently resumed'
    stop=state_path.with_name('stop'); log=state_path.with_name('progress.jsonl')
    state={'schema':1,'status':'starting','pid':os.getpid(),'requested_seconds':args.seconds,
        'mdo_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
        'host_sha256':hashlib.sha256(args.host.read_bytes()).hexdigest(),'hosts':[],
        'reads':0,'writes':0,'receipts':0,'uploads':0,'live_events':0,'reconnects':0,'idle_checks':0,
        'largest_tick_gap':0,'acceptance_72h':False,'stage':'fixture setup','is_private_fixture':True}
    state['forced_expiry_enabled']=args.exercise_expiry
    state['refresh_delay_ms']=args.refresh_delay_ms
    host_receipt=json.loads(args.host.with_name(args.host.name+'.build.json').read_text(encoding='utf-8'))
    assert host_receipt['binary_sha256']==state['host_sha256'],'host receipt does not match actual bytes'
    state['host_sdk_revision']=host_receipt['revision']
    state['xrt_commit']=lock['xrt']['commit']
    state['xs_commit']=lock['xserver']['commit']
    processes=[]; started=None; wall=None; last_tick=None; saved=0; baselines={}; inhibited=False
    def save(force=False):
        nonlocal saved
        now=time.monotonic()
        if not force and now-saved<60: return
        state['updated_unix']=time.time()
        if started is not None: state['elapsed_seconds']=now-started
        text=json.dumps(state,indent=2)+'\n'; temporary=state_path.with_suffix('.tmp')
        temporary.write_text(text,encoding='utf-8',newline='\n'); os.replace(temporary,state_path)
        with log.open('a',encoding='utf-8') as stream:
            stream.write(json.dumps({k:state.get(k) for k in ('status','stage','updated_unix','elapsed_seconds','reads','writes','uploads','reconnects','idle_checks','resources','largest_tick_gap')})+'\n')
        saved=now
        print(json.dumps({k:state.get(k) for k in ('status','stage','elapsed_seconds','reads','writes','uploads','reconnects')}),flush=True)
    def launched(process,site):
        processes.append(process)
        state['hosts'].append({'pid':process.pid,'binary':str(process.args[0]),'site':str(site),
            'sha256':hashlib.sha256(Path(process.args[0]).read_bytes()).hexdigest()})
    def tick():
        nonlocal last_tick
        now=time.monotonic()
        if stop.exists(): raise InterruptedError('stop file requested')
        observe_clock(state,now,last_tick,started,wall,time.time())
        last_tick=now
        for process in processes: assert process.poll() is None,'owned host exited'
        if started is not None:
            resources={str(p.pid):memory(p.pid) for p in processes}
            resources[str(os.getpid())]=memory(os.getpid())
            state['resources']=resources; state['observer_pid']=os.getpid()
            for pid,value in resources.items():
                assert value['private']<768*1024*1024 and value['handles']<2048,'resource ceiling exceeded'
                if now-started>=1800:
                    if pid not in baselines: baselines[pid]=value
                    assert value['private']-baselines[pid]['private']<128*1024*1024,'private memory growth exceeded'
                    assert value['handles']-baselines[pid]['handles']<256,'handle growth exceeded'
        save()
    def setup(site,home):
        assert source_hash(ROOT/'app')==app_hash==source_hash(site),'application changed while copying soak fixture'
        # The root cache may be from an older SDK. Stamp only this disposable
        # copy with the same locked dependency that the actual host verified.
        unity=site/'generated/mdo_unity.c'
        unity.write_text(build_mdo.generated_unity(lock,build_mdo.source_list()),encoding='utf-8')
        (site/'generated/deps.lock').write_text(json.dumps(lock,indent=2)+'\n',encoding='utf-8')
        state['generated_unity_sha256']=hashlib.sha256(unity.read_bytes()).hexdigest()
        assert source_hash(ROOT/'app')==app_hash,'root application must remain unchanged'
        native.setup(site,home)
        if args.refresh_delay_ms:
            client=site/'src/account/client.c'
            needle='    return MdoAccountJsonClient(Origin,Path,Method,Body,Access,Cancel,Status,false);'
            source=client.read_text(encoding='utf-8'); assert source.count(needle)==1
            source=source.replace(needle,
                f'    if (!strcmp(Path,"/api/v1/token/refresh")) xrtSleep({args.refresh_delay_ms});\n'+needle)
            client.write_text(source,encoding='utf-8')
        state['site']=str(site); state['home']=str(home)
    def exercise(*,client,hello,app,device,call,bearer,website_port,clients):
        nonlocal started,wall,last_tick
        port=json.loads((native.SITE/'xs.json').read_text())['services'][0]['port']
        state['source_sha256']=source_hash(native.SITE); state['port']=port; state['website_port']=website_port
        direct=None; rpc=None; token=None; sid=None; sequence=0; client_id=None
        payload=png(); assert len(payload)>262123
        def events():
            if rpc:
                state['live_events']+=len(rpc.events); rpc.events.clear()
        def pump(seconds):
            end=time.monotonic()+seconds
            while time.monotonic()<end:
                tick(); sockets=[s for s in (client,direct) if s is not None]
                ready=[s for s in sockets if s.pending]
                if not ready:
                    readable,_,_=select.select([s.socket for s in sockets],[],[],min(.25,max(0,end-time.monotonic())))
                    ready=[s for s in sockets if s.socket in readable]
                for source in ready:
                    if source is direct:
                        value=Rpc.receive(rpc); assert rpc.consume(value),'unexpected unowned direct reply'; events()
                    else:
                        opcode,body=source.recv()
                        if opcode==9: source.send(10,body)
                        else: assert opcode==10,'relay failed while direct was alive'
        def connect(offer):
            nonlocal direct,rpc,token
            _,header,_=request(port,'GET','/api/v1/project-purge-intent')
            direct=native.gateway(port,header['X-Mdo-Write-Token'],offer['rudp']); clients.append(direct)
            native.negotiate(direct,client,offer['rudp'])
            rpc=LiveRpc(direct,offer); rpc.client=client_id or rpc.client; rpc.sequence=sequence
            _,headers,status,_=rpc.json('GET','/api/v1/bootstrap'); assert status==200
            token=headers['X-Mdo-Write-Token']
            rpc.live=uuid.uuid4().hex
            rpc.send({'type':'live_open','id':rpc.live,'runtime_id':offer['runtime_id'],'token':token})
            assert Rpc.receive(rpc)['type']=='live_opened'; assert rpc.next_event()['type']=='ready'
            if sid: rpc.select(sid)
        def reconnect():
            nonlocal client,hello,direct,rpc,sequence,client_id
            sequence=rpc.sequence; client_id=rpc.client
            rpc.close_live(); direct.close(); clients.remove(direct); direct=None
            # Closing TCP is asynchronous at the relay. Complete the WS close
            # and observe the released controller slot before asking for a new
            # one-use ticket. A 409 remains authoritative, never bypassed.
            client.send(8,struct.pack('!H',1000))
            while True:
                opcode,body=client.recv()
                if opcode==9: client.send(10,body)
                elif opcode==8: break
                else: assert opcode==10,'unexpected relay data during close'
            client.close(); clients.remove(client)
            released=time.monotonic()+5
            while True:
                refresh=app('POST','/connector/devices',{'action':'refresh'},202)['job']['id']
                while True:
                    value=app()
                    if value['job']['id']==refresh and value['job']['state'] in ('done','failed'): break
                    assert time.monotonic()<released,'controller release listing stalled'; time.sleep(.025)
                state['release_job_status']=value.get('status')
                error=value.get('error')
                state['release_job_error']=error if isinstance(error,str) and error.isascii() and error.replace('_','').isalnum() else ''
                if value['job']['state']!='done':
                    account=app(path='/account')
                    state['failed_job_account_state']=account.get('state')
                    state['failed_job_refresh_available']=account.get('refresh_available')
                    save(True)
                assert value['job']['state']=='done','native controller listing failed'
                item=next(v for v in value['listing']['devices'] if v['id']==device)
                if not item['controlled']: break
                assert time.monotonic()<released,'closed controller slot did not release'; time.sleep(.05)
            job=app('POST','/connector/devices',{'action':'connect','device_id':device,'mode':'control'},202)['job']['id']
            end=time.monotonic()+20
            while True:
                value=app()
                if value['job']['id']==job and value['job']['state'] in ('done','failed'): break
                assert time.monotonic()<end,'native ticket job stalled'; time.sleep(.05)
            state['ticket_job_status']=value.get('status')
            error=value.get('error')
            state['ticket_job_error']=error if isinstance(error,str) and error.isascii() and error.replace('_','').isalnum() else ''
            assert value['job']['state']=='done'
            ticket=app('POST','/connector/ticket',{'job_id':job})
            client=WebSocket(website_port,{'Origin':f'http://127.0.0.1:{port}'},path=ticket['path'],
                protocol=ticket['protocol']+', xadmin.ticket.'+ticket['ticket']); clients.append(client)
            opcode,body=client.recv()
            state['controller_start_opcode']=opcode
            first=json.loads(body)
            kind=first.get('type')
            state['controller_start_type']=kind if isinstance(kind,str) and kind.isascii() and kind.replace('_','').isalnum() else 'invalid'
            assert opcode==1 and first['type']=='ready'
            hello=json.loads(client.recv()[1]); connect(hello)
            assert app(path='/account')['state']=='signed_in','native ticket did not renew access'
            state['reconnects']+=1
        def write_title():
            state['stage']='title write + cross-route receipt'
            path=f'/api/v1/projects/default/sessions/{sid}'
            _,headers,status,_=rpc.json('GET',path); assert status==200
            value,_,status,meta=rpc.json('PATCH',path,
                {'title':'xrudp soak '+str(state['writes'])},
                [['X-Mdo-Write-Token',token],['If-Match',headers['ETag']]])
            assert status==200; state['writes']+=1
            relay=Rpc(client,hello,client_id=rpc.client)
            relay.send({'type':'receipt','id':meta['id'],'runtime_id':hello['runtime_id'],
                'client_id':rpc.client,'sequence':meta['sequence']})
            receipt=relay.receive(); assert receipt['type']=='receipt' and receipt['state']=='done' and receipt['status']==200
            state['receipts']+=1; events()
        def upload():
            state['stage']='large binary upload/download with consumer backpressure'
            path=f'/api/v1/projects/default/sessions/{sid}/attachments'
            result,_=rpc.call('POST',path,payload,[['Content-Type','image/png'],['X-Mdo-Write-Token',token],['X-Mdo-File-Name','soak.png']])
            assert result['head']['status']==201; image=json.loads(result['body'])['data']
            meta=rpc.begin('GET',image['url']); output=bytearray(); status=None
            while True:
                value=rpc.receive(); assert value['id']==meta['id']
                if value['type']=='response': status=value['status']
                elif value['type']=='chunk':
                    assert value['offset']==len(output); output+=value['body']; time.sleep(.05)
                    rpc.send({'type':'download_ack','id':meta['id'],'offset':len(output)})
                elif value['type']=='end': assert value['bytes']==len(output); break
                else: raise AssertionError('unexpected binary response')
            assert status==200 and bytes(output)==payload
            assert rpc.json('DELETE',image['url'],headers=[['X-Mdo-Write-Token',token]])[2]==200
            state['uploads']+=1; state['binary_bytes']=state.get('binary_bytes',0)+2*len(payload); events()
        try:
            connect(hello)
            created,_,status,_=rpc.json('POST','/api/v1/sessions',{'project_id':'default','title':'xrudp persistent soak'},[['X-Mdo-Write-Token',token]])
            assert status==201; sid=created['data']['id']; rpc.select(sid)
            started=time.monotonic(); wall=time.time(); last_tick=started
            state.update(status='running',started_unix=wall,stage='initial business check',session_id=sid)
            write_title(); upload(); tick(); save(True)
            if args.exercise_expiry:
                assert app('POST','/test-soak-expire',{})['account_session']
                assert app(path='/account')['state']=='refreshing'
                state['short_expiry_exercised']=True
            next_upload=started+args.upload_every; next_idle=started+args.idle_every; next_reconnect=started+args.reconnect_every
            while time.monotonic()-started<args.seconds:
                tick(); state['stage']='persistent direct HTTP/live'
                assert rpc.json('GET',f'/api/v1/projects/default/sessions/{sid}')[2]==200; state['reads']+=1; events()
                stats=app(path='/test-soak-stats'); assert stats['transports']==2,'native transport leak or direct loss'
                assert stats['account_session'],'native account session expired during active soak'
                account=app(path='/account'); state['native_account_state']=account['state']
                assert account['refresh_available'] and account['state'] in ('signed_in','refreshing'),'native credentials were lost'
                now=time.monotonic()
                if now>=next_upload: write_title(); upload(); next_upload=now+args.upload_every
                if now>=next_idle:
                    state['stage']='no business traffic; native keepalive + live/WS control only'
                    pump(min(args.idle_seconds,max(0,args.seconds-(now-started))))
                    assert rpc.json('GET','/api/v1/projects')[2]==200; state['idle_checks']+=1; next_idle=time.monotonic()+args.idle_every
                if now>=next_reconnect:
                    state['stage']='controlled fresh peer + native account ticket renewal'; reconnect(); next_reconnect=time.monotonic()+args.reconnect_every
                pump(min(args.period,max(0,args.seconds-(time.monotonic()-started))))
            state['stage']='final business verification'; write_title()
            # A genuine native authenticated read must refresh a due access
            # token itself; no test code installs native tokens or logs in.
            refresh=app('POST','/connector/devices',{'action':'refresh'},202)['job']['id']
            end=time.monotonic()+20
            while True:
                value=app()
                if value['job']['id']==refresh and value['job']['state'] in ('done','failed'): break
                assert time.monotonic()<end,'final native account renewal stalled'; time.sleep(.05)
            assert value['job']['state']=='done' and app(path='/account')['state']=='signed_in','native account could not renew itself'
            state['duration_passed']=time.monotonic()-started>=args.seconds
            state['acceptance_72h']=args.seconds>=72*3600 and state['duration_passed']
            # Only the independent fixture observer signs in again for the
            # post-soak revoke/reload checks. The native account above has to
            # survive the entire soak on its own persisted refresh path.
            tokens,_=call(website_port,'POST','/api/v1/login',{'identifier':'remote_manager_one','password':PASSWORD})
            bearer['Authorization']='Bearer '+tokens['data']['access_token']
            state['status']='passed' if state['acceptance_72h'] else 'smoke_passed'
            save(True)
        finally:
            if rpc and direct:
                rpc.close_live(); direct.close()
                if direct in clients: clients.remove(direct)
        return client
    save(True)
    try:
        if os.name=='nt' and args.seconds>=3600:
            kernel=ctypes.WinDLL('kernel32',use_last_error=True)
            kernel.SetThreadExecutionState.argtypes=[wintypes.DWORD]; kernel.SetThreadExecutionState.restype=wintypes.DWORD
            assert kernel.SetThreadExecutionState(0x80000001),'temporary sleep inhibition failed'; inhibited=True
        with Oracles() as oracle:
            native.ORACLE=oracle; native.DUAL=True
            run(args.host.resolve(),args.website_host.resolve(),exercise,native_hook=HOOK,
                native_routes=ROUTE+(EXPIRY_ROUTE if args.exercise_expiry else ''),site_setup=setup,on_launch=launched)
            assert not oracle.errors,'UDP fixture failure'
        state['fixture_lifecycle_passed']=True; save(True)
        return 0
    except BaseException as error:
        record_failure(state,error)
        save(True)
        print('soak failed; inspect sanitized receipt stage and frames',flush=True); return 1
    finally:
        if inhibited: kernel.SetThreadExecutionState(0x80000000)


if __name__=='__main__': raise SystemExit(main())
