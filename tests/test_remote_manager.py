"""Bounded functional tests of the production remote lifecycle and API.

Uses real member sessions, device registration and relay channels. No model
requests, public services, payload echo, load test or production-data writes.
"""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import time
import sys

from test_account_runtime import ROOT, XADMIN, free_port, request
sys.path.insert(0,str(XADMIN/'tests'))
from smoke import fixture, USER, PASSWORD, client_hash
from channel_e2e import WebSocket


def run(host: Path, website_host: Path):
    port, website_port = free_port(), free_port()
    website = fixture(website_port,register_interval=0)
    (ROOT/'.build').mkdir(exist_ok=True)
    site = Path(tempfile.mkdtemp(prefix='remote-manager-',dir=ROOT/'.build'))
    shutil.copytree(ROOT/'app',site,dirs_exist_ok=True)
    home = site/'mdo-home'
    origin = f'http://127.0.0.1:{website_port}'
    unity = site/'generated/mdo_unity.c'
    unity.write_text('#define MDO_ACCOUNT_SERVICE_ORIGIN '+json.dumps(origin)+'\n'+unity.read_text(encoding='utf-8'),encoding='utf-8')
    config = json.loads((site/'xs.json').read_text()); server = config['services'][0]
    server.update({'class':'http','port':port}); server.pop('window',None)
    (site/'xs.json').write_text(json.dumps(config))
    # Exercise the actual manager Unit/init path while retaining the surrounding
    # routes. This hook exists only in the disposable test application.
    router = site/'src/api/router.c'; source = router.read_text()
    hook = '''static bool RemoteLifecycleTest(MdoApiContext* c) {
        MdoRemoteUnit(); bool ok = MdoRemoteInit(c->Request->server);
        return ok ? MdoApiReplySuccessTake(c,200u,MdoRemoteSnapshot(),NULL) :
            MdoApiReplyError(c,500u,"test_failed","Remote restart failed",NULL);
    }
    '''
    source = source.replace('static const MdoApiRoute g_MdoApiRoutes[]',hook+'\nstatic const MdoApiRoute g_MdoApiRoutes[]')
    source = source.replace('static const MdoApiRoute g_MdoApiRoutes[] = {',
        'static const MdoApiRoute g_MdoApiRoutes[] = {\n'
        ' {"/api/v1/test-remote-lifecycle",XHTTP_METHOD_GET,"GET",RemoteLifecycleTest,false},')
    router.write_text(source)
    processes,logs,clients = [],[],[]

    def launch(path,executable,portable=None):
        log = (path/'test.log').open('ab'); logs.append(log)
        env = dict(os.environ)
        if portable: env['MDO_HOME'] = str(portable)
        process = subprocess.Popen([str(executable),str(path/'xs.json')],cwd=path,env=env,stdout=log,stderr=log)
        processes.append(process); return process

    def ready(which,path,process):
        for _ in range(160):
            if process.poll() is not None: raise AssertionError(f'host exited; see {process.args}')
            try:
                if request(which,'GET',path)[0] == 200: return
            except OSError: pass
            time.sleep(.05)
        raise AssertionError('host did not become ready')

    def call(which,method,path,body=None,headers=None,status=200):
        actual,head,raw = request(which,method,path,body,headers)
        assert actual == status,(path,actual,raw)
        return json.loads(raw),head

    def app(method='GET',path='/remote',body=None,status=200):
        head = {}
        if method != 'GET':
            _,nonce = call(port,'GET','/api/v1/remote')
            head['X-Mdo-Write-Token'] = nonce['X-Mdo-Write-Token']
        value,_ = call(port,method,'/api/v1'+path,body,head,status)
        return value.get('data',value)

    def until(predicate,label='condition',timeout=12):
        end = time.monotonic()+timeout; value = None
        while time.monotonic() < end:
            value = app()
            if predicate(value): return value
            time.sleep(.04)
        raise AssertionError((label,value))

    def action(path,body,success=True):
        queued = app('POST',path,body,202); job_id = queued['job']['id']
        value = until(lambda v: v['job']['id'] == job_id and v['job']['state'] in ('done','failed'),'job completion')
        assert (value['job']['state'] == 'done') == success,value
        return value

    def login(username):
        app('POST','/account/login',{'identifier':username,'password':PASSWORD,'remember':True})
        for _ in range(150):
            value = app(path='/account')
            if value['state'] == 'signed_in' and not value['busy']: return
            time.sleep(.04)
        raise AssertionError(value)

    try:
        web_process = launch(website,website_host); ready(website_port,'/admin/login',web_process)
        signed,head = call(website_port,'POST','/admin/login',{'username':USER,'password':client_hash(USER,PASSWORD)})
        assert signed['result']; admin = {'Cookie':head['Cookies']}
        enabled,_ = call(website_port,'POST','/admin/plugin/enable',{'name':'device-relay'},admin)
        assert enabled['result']
        for username in ('remote_manager_one','remote_manager_two'):
            call(website_port,'POST','/api/v1/register',{'username':username,'password':PASSWORD},status=201)
        tokens,_ = call(website_port,'POST','/api/v1/login',{'identifier':'remote_manager_one','password':PASSWORD})
        bearer = {'Authorization':'Bearer '+tokens['data']['access_token']}
        process = launch(site,host,home); ready(port,'/api/v1/remote',process)
        initial = app(); assert not initial['allow_remote'] and initial['stage'] == 'disabled',initial
        assert not (home/'config/remote.json').exists()
        # Core write/Origin gates apply to the connector too.
        call(port,'POST','/api/v1/remote',{'allow_remote':True,'name':'bad'},status=428)
        app('POST','/remote',{'allow_remote':True,'name':'not logged in'},202)
        failed = until(lambda v: v['job']['state'] == 'failed','login required')
        assert failed['error'] == 'remote_login_required' and not failed['allow_remote'],failed
        assert not action('/remote',{'allow_remote':False})['allow_remote'],'disabling needs no account'
        login('remote_manager_one')
        value = action('/remote',{'allow_remote':True,'name':'native manager'})
        online = until(lambda v: v['stage'] == 'online','native online')
        device = online['device_id']; assert len(device) == 32 and online['allow_remote'],online
        stored = json.loads((home/'config/remote.json').read_text())
        assert stored['allow_remote'] == online['persistent']
        listing = action('/connector/devices',{'action':'refresh'})['listing']['devices']
        assert len(listing) == 1 and listing[0]['online'] and listing[0]['id'] == device,listing
        updated = listing[0]['updated_at']
        job = action('/connector/devices',{'action':'connect','device_id':device,'mode':'control'})['job']['id']
        # Public snapshots must never contain connection credentials.
        assert 'ticket' not in json.dumps(app()) and 'device_secret' not in json.dumps(app())
        ticket = app('POST','/connector/ticket',{'job_id':job})
        app('POST','/connector/ticket',{'job_id':job},409)
        client = WebSocket(website_port,{'Origin':'http://127.0.0.1:12345'},path=ticket['path'],
            protocol=ticket['protocol']+', xadmin.ticket.'+ticket['ticket']); clients.append(client)
        assert json.loads(client.recv()[1])['type'] == 'ready'
        # Independently running listing job leaves the native connection alive.
        assert action('/connector/devices',{'action':'refresh'})['stage'] == 'online'
        client.close(); clients.remove(client)
        # Reloading the relay is a transport interruption, not authorization.
        reload,_ = call(website_port,'POST','/admin/plugin/reload',{'name':'device-relay'},admin)
        assert reload['result']
        until(lambda v: v['stage'] == 'online','relay reconnect')
        for _ in range(150):
            listing,_ = call(website_port,'GET','/api/v1/devices',headers=bearer)
            if listing['data']['devices'][0]['online']: break
            time.sleep(.04)
        else: raise AssertionError(('relay reconnect',listing))
        assert listing['data']['devices'][0]['updated_at'] == updated,'reconnect must not register'
        # Unit joins both workers; restore is ticket-only and keeps the ID.
        restart = app(path='/test-remote-lifecycle')
        if online['persistent']:
            assert restart['allow_remote']
            again = until(lambda v: v['stage'] == 'online','restore after native Unit')
            assert again['device_id'] == device
        else:
            assert not restart['allow_remote']
            action('/remote',{'allow_remote':True,'name':'native manager'})
            device = until(lambda v: v['stage'] == 'online')['device_id']
        call(website_port,'POST','/api/v1/devices/revoke',{'device_id':device},bearer)
        revoked = until(lambda v: not v['allow_remote'] and v['stage'] == 'revoked','revoke closes permission')
        assert revoked['error'] == 'remote_revoked',revoked
        assert not json.loads((home/'config/remote.json').read_text())['allow_remote']
        time.sleep(.6); assert not app()['allow_remote'],'revoke must not reactivate in background'
        action('/remote',{'allow_remote':True,'name':'explicit reactivation'})
        reactivated = until(lambda v: v['stage'] == 'online')
        assert reactivated['device_id'] == device
        # Disabling cancels a just-admitted operation without waiting for it.
        app('POST','/connector/devices',{'action':'refresh'},202)
        action('/remote',{'allow_remote':False})
        assert not app()['allow_remote']
        action('/remote',{'allow_remote':True,'name':'account switch test'})
        until(lambda v: v['stage'] == 'online')
        login('remote_manager_two')
        switched = until(lambda v: not v['allow_remote'],'account switch closes device')
        assert switched['error'] == 'remote_account_changed',switched
        assert 'devices' not in app()['listing'],'old account list must be hidden'
        action('/remote',{'allow_remote':True,'name':'second account'})
        second = until(lambda v: v['stage'] == 'online')
        assert second['device_id'] != device
        app('POST','/account/logout',{})
        until(lambda v: not v['allow_remote'],'logout closes device')
        assert not json.loads((home/'config/remote.json').read_text())['allow_remote']
        if online['persistent']:
            for binary in (home/'data/remote/accounts').glob('*.bin'):
                raw = binary.read_bytes(); assert b'device_secret' not in raw and b'"device_id"' not in raw
        print('PASS default-off/write gate, nonblocking jobs, device list/ticket consume, relay reconnect without register, native Unit/restore, revoke/explicit reactivation, disable supersedes work, account isolation/logout')
    finally:
        for client in clients: client.close()
        for process in reversed(processes):
            if process.poll() is None: process.terminate(); process.wait(timeout=20)
        for log in logs: log.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',type=Path,default=ROOT/'.build/host/xs.exe')
    parser.add_argument('--website-host',type=Path,default=XADMIN/'xs.exe')
    args = parser.parse_args()
    run(args.host.resolve(),args.website_host.resolve())
