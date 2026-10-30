"""Pause one of two owned mdo processes; the other device and relay stay live.

Real process suspension is deliberately not labeled OS sleep or Doze. Only
disposable local accounts, source copies, projects and Home directories are used.
"""
from __future__ import annotations

import argparse
import hashlib
import http.client
import json
import os
from pathlib import Path
import select
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import traceback
import uuid

from test_remote_process_pause import SuspendedChild

HOOK = '''
static unsigned AsymmetricWrites;
static bool AsymmetricStats(MdoApiContext* C) {
    xvalue* V=xrtValueObject();
    if (C->Request->head->MethodCode==XHTTP_METHOD_POST) ++AsymmetricWrites;
    if (!V || !MdoAccountSetUInt(V,"writes",AsymmetricWrites) ||
        !MdoAccountSetUInt(V,"transports",xrtAtomic32Load(&g_MdoRudpTransports,XMEMORY_ACQUIRE))) {
        xrtValueRelease(V); return false;
    }
    return MdoApiReplySuccessTake(C,200u,V,NULL);
}
static bool AsymmetricStop(MdoApiContext* C) {
    bool Ok=MdoApiReplySuccessTake(C,200u,xrtValueObject(),NULL);
    xsAppRequestStop(); return Ok;
}
'''
ROUTES = '''
 {"/api/v1/test-asymmetric-stats",XHTTP_METHOD_GET|XHTTP_METHOD_POST,"GET, POST",AsymmetricStats,false},
 {"/api/v1/test-asymmetric-stop",XHTTP_METHOD_GET,"GET",AsymmetricStop,false},
'''


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,required=True)
    parser.add_argument('--host',type=Path,required=True)
    parser.add_argument('--website-host',type=Path,required=True)
    parser.add_argument('--website-source',type=Path,
        help='immutable website fixture seed matching the supplied host; never a running site')
    parser.add_argument('--family',type=int,choices=(4,6),default=4)
    parser.add_argument('--paused-role',choices=('controller','target'),required=True)
    parser.add_argument('--mode',choices=('short','expired'),default='short')
    parser.add_argument('--receipt',type=Path,required=True)
    parser.add_argument('--debugger',type=Path,help='optional existing GDB; on failure inspect only the owned paused child')
    parser.add_argument('--debugger-early',action='store_true',
        help='attach the supplied debugger before suspension to catch a native crash')
    args=parser.parse_args();source=args.source.resolve();host=args.host.resolve()
    assert not args.debugger_early or args.debugger
    assert not args.receipt.exists(),'new receipt path required'
    assert not subprocess.check_output(['git','status','--porcelain'],cwd=source).strip()
    digest=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    sys.path.insert(0,str(source/'tests'))
    import test_remote_rudp as scenario
    from test_remote_manager import run, WebSocket, PASSWORD
    from test_remote_bridge import Rpc
    from test_remote_live import LiveRpc
    from test_account_runtime import request, free_port
    import test_mdo_delivery as website_fixture
    original_website_source=website_fixture.SOURCE
    if args.website_source:
        website_fixture.SOURCE=args.website_source.resolve()
        assert (website_fixture.SOURCE/'db/main.db').is_file()
    scenario.FAMILY=args.family
    target=None;controller=None;controller_log=None
    result={'phase':'preparing','family':args.family,'paused_role':args.paused_role,
        'mode':args.mode,'requested_pause_seconds':3 if args.mode=='short' else 65,
        'two_native_processes':True,'os_sleep_acceptance':False,'acceptance_72h':False}

    def launched(process,path):
        nonlocal target
        if path.resolve()==scenario.SITE.resolve():
            assert target is None and Path(process.args[0]).resolve()==host
            assert path.resolve().is_relative_to(source/'.build')
            target=process

    def wait(predicate,label,seconds=15):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            try:
                value=predicate()
                if value:return value
            except (OSError,ValueError):pass
            time.sleep(.05)
        raise AssertionError(label)

    def stats(port):
        status,_,body=request(port,'GET','/api/v1/test-asymmetric-stats')
        assert status==200
        return json.loads(body)['data']

    def exercise(*,client,hello,app,device,call,bearer,website_port,clients):
        nonlocal controller,controller_log
        assert target is not None and target.poll() is None
        target_site=scenario.SITE
        target_port=json.loads((target_site/'xs.json').read_text())['services'][0]['port']
        controller_site=Path(tempfile.mkdtemp(prefix='remote-asymmetric-',dir=source/'.build'))
        shutil.copytree(source/'app',controller_site,dirs_exist_ok=True)
        controller_home=controller_site/'mdo-home';controller_port=free_port()
        try:scenario.setup(controller_site,controller_home)
        finally:scenario.SITE=target_site
        unity=controller_site/'generated/mdo_unity.c'
        unity.write_text('#define MDO_ACCOUNT_SERVICE_ORIGIN '+json.dumps(f'http://127.0.0.1:{website_port}')+'\n'+unity.read_text(encoding='utf-8'),encoding='utf-8')
        router=controller_site/'src/api/router.c';text=router.read_text(encoding='utf-8')
        anchor='static const MdoApiRoute g_MdoApiRoutes[] = {';assert text.count(anchor)==1
        router.write_text(text.replace(anchor,HOOK+'\n'+anchor+ROUTES),encoding='utf-8')
        config=json.loads((controller_site/'xs.json').read_text());server=config['services'][0]
        server.update({'class':'http','port':controller_port});server.pop('window',None)
        (controller_site/'xs.json').write_text(json.dumps(config),encoding='utf-8')
        controller_log=(controller_site/'test.log').open('ab')
        controller=subprocess.Popen([str(host),str(controller_site/'xs.json')],cwd=controller_site,
            env={**os.environ,'MDO_HOME':str(controller_home)},stdout=controller_log,stderr=controller_log,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        assert controller.pid!=target.pid
        result.update(target_pid=target.pid,controller_pid=controller.pid,
            target_port=target_port,controller_port=controller_port,
            target_site=str(target_site),controller_site=str(controller_site))
        wait(lambda:controller.poll() is None and request(controller_port,'GET','/api/v1/bootstrap')[0]==200,'controller readiness')
        _,headers,_=request(controller_port,'GET','/api/v1/bootstrap');controller_token=headers['X-Mdo-Write-Token']
        status,_,_=request(controller_port,'POST','/api/v1/account/login',
            {'identifier':'remote_manager_one','password':PASSWORD,'remember':True},
            {'X-Mdo-Write-Token':controller_token})
        assert status==200
        wait(lambda:json.loads(request(controller_port,'GET','/api/v1/account')[2])['data']['state']=='signed_in','controller sign in')
        direct=None;rpc=None;paused=None;debugger=None;debugger_output=None

        def open_direct():
            nonlocal direct,rpc
            direct=scenario.gateway(controller_port,controller_token,hello['rudp']);clients.append(direct)
            scenario.negotiate(direct,client,hello['rudp'])
            rpc=LiveRpc(direct,hello)
            _,head,status,_=rpc.json('GET','/api/v1/bootstrap');assert status==200
            assert head['X-Mdo-Write-Token']!=controller_token,'devices must not share page-write authority'
            wait(lambda:stats(target_port)['transports']==stats(controller_port)['transports']==1,'one endpoint per process')
            return head['X-Mdo-Write-Token']

        try:
            target_token=open_direct();relay=Rpc(client,hello,client_id=rpc.client)
            write_headers=[['X-Mdo-Write-Token',target_token]]
            assert rpc.json('POST','/api/v1/test-asymmetric-stats',{},write_headers)[0]['data']['writes']==1
            project='asymmetric-'+uuid.uuid4().hex[:12]
            assert rpc.json('POST','/api/v1/projects',{'id':project,'name':'asymmetric pause'},write_headers)[2]==201
            session=rpc.json('POST','/api/v1/sessions',{'project_id':'default','title':'pause live'},write_headers)[0]['data']['id']
            rpc.live=uuid.uuid4().hex
            rpc.send({'type':'live_open','id':rpc.live,'runtime_id':hello['runtime_id'],'token':target_token})
            assert Rpc.receive(rpc)['type']=='live_opened';assert rpc.next_event()['type']=='ready'
            rpc.select(session);assert rpc.until(lambda e:e['type']=='events')['next_cursor']==0
            paused=controller if args.paused_role=='controller' else target
            active=target if paused is controller else controller
            paused_port=controller_port if paused is controller else target_port
            active_port=target_port if paused is controller else controller_port
            if args.debugger_early:
                diagnostic=args.receipt.with_suffix('.gdb.log');diagnostic.parent.mkdir(parents=True,exist_ok=True)
                debugger_output=diagnostic.open('w',encoding='utf-8')
                debugger=subprocess.Popen([str(args.debugger.resolve()),'-nx','-q','-batch',
                    '-ex','set pagination off','-ex','set print thread-events off',
                    '-ex','set print frame-arguments none','-ex',f'attach {paused.pid}',
                    '-ex','echo ASYMMETRIC_DEBUGGER_ATTACHED\\n','-ex','continue',
                    '-ex','thread apply all bt 16','-ex','info registers','-ex','x/12i $pc',
                    '-ex','detach','-ex','quit'],stdout=debugger_output,stderr=subprocess.STDOUT,
                    creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
                wait(lambda:debugger.poll() is None and 'ASYMMETRIC_DEBUGGER_ATTACHED' in
                    diagnostic.read_text(encoding='utf-8',errors='replace') and
                    request(paused_port,'GET','/api/v1/bootstrap')[0]==200,'debugger attached and inferior continuing')
            result['phase']='suspended';print(json.dumps(result),flush=True)
            pending=None;control_closed=False
            with SuspendedChild(paused) as stopped:
                connection=http.client.HTTPConnection('127.0.0.1',paused_port,timeout=.25)
                try:
                    connection.request('GET','/api/v1/bootstrap')
                    try:connection.getresponse()
                    except TimeoutError:result['paused_endpoint_http_unresponsive']=True
                    else:raise AssertionError('paused endpoint still served HTTP')
                finally:connection.close()
                assert active.poll() is None and request(active_port,'GET','/api/v1/bootstrap')[0]==200
                assert request(website_port,'GET','/admin/login')[0]==200
                result['other_endpoint_and_website_responded']=True
                if args.mode=='short':
                    pending=rpc.begin('POST','/api/v1/test-asymmetric-stats',b'{}',
                        [['Content-Type','application/json'],*write_headers])
                end=stopped.started+result['requested_pause_seconds']
                while time.monotonic()<end:
                    assert paused.poll() is None and active.poll() is None
                    if not control_closed and (client.pending or select.select([client.socket],[],[],.1)[0]):
                        try:
                            opcode,body=client.recv()
                            if opcode==9:client.send(10,body)
                            elif opcode==8:control_closed=True
                            else:assert opcode in (1,10)
                        except (EOFError,ConnectionResetError):control_closed=True
                    else:time.sleep(.05)
                result['actual_pause_seconds']=time.monotonic()-stopped.started
            assert paused.poll() is None and active.poll() is None
            resumed=time.monotonic();result['resume_http_attempts']=0
            result['resume_http_transport_errors']=[]
            result['resume_http_statuses']={}
            def resumed_http():
                result['resume_http_attempts']+=1
                try:
                    status=request(paused_port,'GET','/api/v1/bootstrap')[0]
                    key=str(status);result['resume_http_statuses'][key]=result['resume_http_statuses'].get(key,0)+1
                    return status==200
                except OSError as error:
                    result['resume_http_transport_errors'].append(type(error).__name__)
                    return False
            # Socket deadlines and queued accepts can expire while the entire
            # event loop is stopped. Wait for fresh read-only HTTP readiness;
            # never retry the pending mutation or reopen its direct channel.
            wait(resumed_http,'resumed endpoint HTTP readiness')
            result['resume_http_ready_seconds']=time.monotonic()-resumed
            if args.mode=='short':
                assert not control_closed,'short pause lost relay authority'
                completed=rpc.collect(pending,b'{}');assert completed['head']['status']==200
                assert json.loads(completed['body'])['data']['writes']==2
                relay.send({'type':'receipt','id':pending['id'],'runtime_id':hello['runtime_id'],
                    'client_id':rpc.client,'sequence':pending['sequence']})
                receipt=relay.receive();assert receipt['type']=='receipt' and receipt['state']=='done' and receipt['status']==200
                rpc.select(session,selection=2);assert rpc.until(lambda e:e['type']=='events')['next_cursor']==0
                rpc.close_live()
                result.update(same_direct_recovered=True,pending_write_completed_once=True,live_recovered=True)
            else:
                end=time.monotonic()+12;direct.socket.settimeout(1)
                while True:
                    assert time.monotonic()<end,'expired native route stayed open'
                    try:opcode,body=direct.recv()
                    except (EOFError,ConnectionResetError):break
                    except TimeoutError:continue
                    if opcode==8:break
                    if opcode==9:direct.send(10,body)
                direct.close();clients.remove(direct);direct=None
                client.close();clients.remove(client)
                wait(lambda:app()['stage']=='online','target relay recovery',25)
                def slot_released():
                    listing,_=call(website_port,'GET','/api/v1/devices',headers=bearer)
                    item=next(v for v in listing['data']['devices'] if v['id']==device)
                    return item['online'] and not item['controlled']
                wait(slot_released,'controller slot release')
                ticket,_=call(website_port,'POST','/api/v1/devices/ticket',
                    {'device_id':device,'role':'controller','mode':'control'},bearer)
                ticket=ticket['data']
                client=WebSocket(website_port,{'Origin':f'http://127.0.0.1:{controller_port}'},
                    path=ticket['path'],protocol=ticket['protocol']+', xadmin.ticket.'+ticket['ticket'])
                clients.append(client);assert json.loads(client.recv()[1])['type']=='ready'
                hello=json.loads(client.recv()[1])
                wait(lambda:stats(target_port)['transports']==stats(controller_port)['transports']==0,'expired endpoints close')
                assert stats(target_port)['writes']==1,'old route replayed a completed write'
                target_token=open_direct();relay=Rpc(client,hello,client_id=rpc.client)
                write_headers=[['X-Mdo-Write-Token',target_token]]
                assert rpc.json('POST','/api/v1/test-asymmetric-stats',{},write_headers)[0]['data']['writes']==2
                result.update(expired_direct_closed=True,fresh_ticket_and_direct_recovered=True,old_write_not_replayed=True)
            assert stats(target_port)['writes']==2 and stats(controller_port)['writes']==0
            assert any(v['id']==project for v in rpc.json('GET','/api/v1/projects')[0]['data']['items'])
            assert not any(v['id']==project for v in json.loads(request(controller_port,'GET','/api/v1/projects')[2])['data']['items'])
            client.send(1,json.dumps({'type':'rudp_cancel','nonce':hello['rudp']['nonce'],'runtime_id':hello['runtime_id']}).encode())
            direct.close();clients.remove(direct);direct=None
            assert relay.json('GET','/api/v1/projects')[2]==200
            wait(lambda:stats(target_port)['transports']==stats(controller_port)['transports']==0,'both native endpoint counts return to zero')
            result.update(target_only_effects=True,both_native_counts_zero=True)
            return client
        except BaseException as error:
            result.update(phase='failed',error_type=type(error).__name__,
                target_exit_before_cleanup=target.poll(),controller_exit_before_cleanup=controller.poll())
            result['error_frames']=[{'file':Path(f.filename).name,'line':f.lineno,'function':f.name}
                for f in traceback.extract_tb(error.__traceback__)]
            failed=args.receipt.with_suffix('.failed.json');failed.parent.mkdir(parents=True,exist_ok=True)
            failed.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
            if args.debugger and debugger is None and paused is not None and paused.poll() is None:
                # This Popen is still retained and belongs exclusively to this
                # fixture. Never inspect a PID supplied by an external caller.
                diagnostic=args.receipt.with_suffix('.gdb.log')
                try:
                    with diagnostic.open('w',encoding='utf-8') as output:
                        subprocess.run([str(args.debugger.resolve()),'-nx','-q','-batch',
                            '-ex','set pagination off','-ex','set print frame-arguments none',
                            '-ex',f'attach {paused.pid}','-ex','thread apply all bt 10',
                            '-ex','detach','-ex','quit'],stdout=output,stderr=subprocess.STDOUT,timeout=25,
                            creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
                except (OSError,subprocess.TimeoutExpired) as diagnostic_error:
                    result['debugger_error_type']=type(diagnostic_error).__name__
                    failed.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
            raise
        finally:
            if direct is not None:
                direct.close()
                if direct in clients:clients.remove(direct)
            if controller is not None and controller.poll() is None:
                try:request(controller_port,'GET','/api/v1/test-asymmetric-stop')
                except OSError:pass
                try:controller.wait(timeout=12)
                except subprocess.TimeoutExpired:controller.terminate();controller.wait(timeout=10);raise
            if controller_log:controller_log.close()
            if debugger is not None:
                if debugger.poll() is None and paused is not None and paused.poll() is None:
                    if os.name=='nt':
                        import ctypes
                        from ctypes import wintypes
                        kernel=ctypes.WinDLL('kernel32',use_last_error=True)
                        kernel.DebugBreakProcess.argtypes=[wintypes.HANDLE]
                        kernel.DebugBreakProcess.restype=wintypes.BOOL
                        assert kernel.DebugBreakProcess(wintypes.HANDLE(int(paused._handle)))
                    else:
                        import signal
                        debugger.send_signal(signal.SIGINT)
                    # End the debugger's `continue` command and let its queued
                    # detach finish. Killing an attached debugger can kill its
                    # inferior and must never masquerade as a product crash.
                try:debugger.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    debugger.terminate();debugger.wait(timeout=5)
                    raise AssertionError('owned debugger failed to detach')
            if debugger_output:debugger_output.close()

    try:
        run(host,args.website_host.resolve(),exercise,native_hook=HOOK,native_routes=ROUTES,
            site_setup=scenario.setup,on_launch=launched)
    finally:website_fixture.SOURCE=original_website_source
    assert target is not None and target.poll() is not None
    assert controller is not None and controller.poll() is not None and controller.returncode==0
    assert hashlib.sha256(Path(__file__).read_bytes()).hexdigest()==digest
    assert not subprocess.check_output(['git','status','--porcelain'],cwd=source).strip()
    result.update(phase='passed',children_reaped=True,controller_graceful_exit=True,
        same_device_unit_revoke_account_switch_logout=True,test_sha256=digest,
        source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=source,text=True).strip(),
        host_sha256=hashlib.sha256(host.read_bytes()).hexdigest(),
        suspend_helper_sha256=hashlib.sha256(Path(sys.modules['test_remote_process_pause'].__file__).read_bytes()).hexdigest())
    args.receipt.parent.mkdir(parents=True,exist_ok=True)
    args.receipt.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result),flush=True)


if __name__=='__main__':main()
