"""Real native account -> device API -> relay integration in isolated homes.

Test-only hooks drive the internal components from a background thread. They
do not implement a production dispatcher or add bypasses to the application.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import time

from test_account_runtime import ROOT, XADMIN, free_port, request
sys.path.insert(0,str(XADMIN/'tests'))
from smoke import PASSWORD
from channel_e2e import WebSocket
from remote_website_fixture import remote_website_fixture, WEBSITE_HOST

HOOK = r'''
static bool RemoteTestQuery(xstrview query, cstr text) { return xrtStrEqual(query,xrtStrView(text)); }
static struct {
    xmutex* Lock;
    xthread* Thread;
    xcancel* Cancel;
    xvalue* Result;
    MdoRemoteSocket* Socket;
    bool Reactivate;
    unsigned Messages;
    uint16 RegistrationStatus;
} RemoteTest;
static void RemoteTestPublish(cstr stage, const MdoRemoteIdentity* identity, uint16 status, uint16 code) {
    xvalue* out = xrtValueObject();
    MdoAccountSetString(out,"stage",stage); MdoAccountSetUInt(out,"status",status);
    MdoAccountSetUInt(out,"registration_status",RemoteTest.RegistrationStatus);
    MdoAccountSetUInt(out,"close_code",code); MdoAccountSetUInt(out,"messages",RemoteTest.Messages);
    MdoAccountSetString(out,"device_id",identity ? identity->Id : "");
    MdoAccountSetUInt(out,"member_id",identity ? identity->MemberId : 0u);
    MdoAccountSetBool(out,"persistent",identity && identity->Persistent);
    xrtMutexLock(RemoteTest.Lock); xrtValueRelease(RemoteTest.Result); RemoteTest.Result = out;
    xrtMutexUnlock(RemoteTest.Lock);
}
static bool RemoteTestMessage(bool binary, xbytesview message, void* data) {
    MdoRemoteIdentity* identity = (MdoRemoteIdentity*)data;
    if (binary) {
        if (message.Size < 21u || memcmp(message.Data,"MDR1",4u)) return false;
        if (!MdoRemoteSocketSend(RemoteTest.Socket,true,message)) return false;
        RemoteTest.Messages++; RemoteTestPublish("ready",identity,200u,0u); return true;
    }
    xvalue* notice = xrtJsonParse(xrtStrViewN((const char*)message.Data,message.Size));
    cstr type = MdoAccountText(notice,"type",32u);
    bool ok = type && (!strcmp(type,"ready") || !strcmp(type,"peer_open") || !strcmp(type,"peer_close"));
    if (ok && !strcmp(type,"ready")) RemoteTestPublish("ready",identity,200u,0u);
    xrtValueRelease(notice); return ok;
}
static int32 RemoteTestRun(void* data) {
    XS_ServerInfo* server = (XS_ServerInfo*)data;
    MdoAccountLease lease = {0}; MdoRemoteIdentity identity = {0}, again = {0};
    MdoRemoteNet net = {0}; MdoRemoteConfig config = {0}; xvalue *body = NULL, *reply = NULL, *ticket = NULL;
    uint16 status = 0u, code = 0u; bool ok = MdoAccountAcquireService(RemoteTest.Cancel,&lease);
    ok = ok && MdoRemoteIdentityLoad(lease.MemberId,true,&identity) &&
        MdoRemoteIdentityLoad(lease.MemberId,false,&again) && !strcmp(identity.Id,again.Id) &&
        !strcmp(identity.Secret,again.Secret);
    if (!ok) goto done;
    body = xrtValueObject();
    ok = body && MdoAccountSetString(body,"device_id",identity.Id) &&
        MdoAccountSetString(body,"device_secret",identity.Secret) && MdoAccountSetString(body,"name","native test") &&
        MdoAccountSetString(body,"platform","windows") && MdoAccountSetString(body,"app_version","test-1") &&
        MdoAccountSetBool(body,"allow_remote",true) &&
        (!RemoteTest.Reactivate || MdoAccountSetBool(body,"reactivate",true));
    if (!ok) goto done;
    reply = MdoRemoteServiceCall("/api/v1/devices/register","POST",body,&lease,&status);
    MdoAccountSecretValueRelease(body); body = NULL;
    cstr id = MdoAccountText(reply,"device_id",32u);
    ok = reply && id && !strcmp(id,identity.Id) && (status == 200u || status == 201u);
    if (!ok) goto done;
    RemoteTest.RegistrationStatus = status;
    RemoteTestPublish("registered",&identity,status,0u);
    xrtValueRelease(reply); reply = NULL;
    strcpy(config.Name,"native test"); config.MemberId = lease.MemberId; config.AllowRemote = true;
    ok = MdoRemoteConfigSave(&config); if (!ok) goto done;
    reply = MdoRemoteServiceCall("/api/v1/devices","GET",NULL,&lease,&status);
    ok = reply && xrtValueCount(xrtValueObjectGet(reply,XRT_STR_LITERAL("devices"))) == 1u;
    xrtValueRelease(reply); reply = NULL; if (!ok) goto done;
    /* Invalid service paths never carry a token to a different route. */
    reply = MdoRemoteServiceCall("/api/v1/profile","GET",NULL,&lease,&status);
    ok = !reply && status == 0u; if (!ok) goto done;
    body = xrtValueObject();
    ok = body && MdoAccountSetString(body,"device_id",identity.Id) &&
        MdoAccountSetString(body,"device_secret",identity.Secret) && MdoAccountSetString(body,"role","device");
    if (!ok) goto done;
    ticket = MdoRemoteServiceCall("/api/v1/devices/ticket","POST",body,&lease,&status);
    MdoAccountSecretValueRelease(body); body = NULL;
    ok = ticket && status == 200u && MdoRemoteNetInit(&net,server->Engine,NULL);
    if (!ok) goto done;
    RemoteTest.Socket = MdoRemoteServiceConnect(&net,ticket,&lease,&status);
    MdoAccountSecretValueRelease(ticket); ticket = NULL;
    ok = RemoteTest.Socket != NULL; if (!ok) goto done;
    uint64 until = xrtDeadlineAfter(15000000u);
    while (!xrtDeadlineExpired(until) && MdoRemoteSocketPoll(RemoteTest.Socket,RemoteTestMessage,&identity)) xrtSleep(5);
    code = MdoRemoteSocketCloseCode(RemoteTest.Socket);
done:
    if (identity.MemberId) { config.AllowRemote = false; (void)MdoRemoteConfigSave(&config); }
    MdoRemoteSocketDestroy(RemoteTest.Socket); RemoteTest.Socket = NULL;
    MdoRemoteNetUnit(&net);
    MdoAccountSecretValueRelease(body); MdoAccountSecretValueRelease(ticket); xrtValueRelease(reply);
    MdoAccountRelease(&lease); MdoRemoteIdentityClear(&again);
    RemoteTestPublish(ok ? "closed" : "failed",&identity,status,code);
    MdoRemoteIdentityClear(&identity); xsServerRelease(server); return 0;
}
static void RemoteTestUnit(void) {
    xrtCancelRequest(RemoteTest.Cancel);
    if (RemoteTest.Thread) { xrtThreadWait(RemoteTest.Thread); xrtThreadDestroy(RemoteTest.Thread); }
    xrtCancelDestroy(RemoteTest.Cancel); xrtValueRelease(RemoteTest.Result); xrtMutexDestroy(RemoteTest.Lock);
    memset(&RemoteTest,0,sizeof(RemoteTest));
}
static bool RemoteTestRoute(MdoApiContext* c) {
    if (!RemoteTest.Lock) RemoteTest.Lock = xrtMutexCreate();
    bool create = RemoteTestQuery(c->Target.Query,"start") || RemoteTestQuery(c->Target.Query,"reactivate");
    if (create) {
        if (RemoteTest.Thread && xrtThreadState(RemoteTest.Thread) != XTHREAD_FINISHED)
            return MdoApiReplyError(c,409,"test_busy","Wait for this test job",NULL);
        if (RemoteTest.Thread) { xrtThreadWait(RemoteTest.Thread); xrtThreadDestroy(RemoteTest.Thread); RemoteTest.Thread = NULL; }
        xrtCancelDestroy(RemoteTest.Cancel); RemoteTest.Cancel = xrtCancelCreate(); RemoteTest.Messages = 0u;
        RemoteTest.RegistrationStatus = 0u;
        RemoteTest.Reactivate = RemoteTestQuery(c->Target.Query,"reactivate");
        RemoteTestPublish("starting",NULL,0u,0u);
        XS_ServerInfo* server = xsServerRetain(c->Request->server);
        RemoteTest.Thread = RemoteTest.Cancel && server ? xrtThreadCreate(RemoteTestRun,server,0) : NULL;
        if (!RemoteTest.Thread) xsServerRelease(server);
    }
    xvalue* out = NULL;
    if (RemoteTestQuery(c->Target.Query,"storage")) {
        MdoRemoteConfig config; MdoRemoteIdentity one,copy,two;
        bool valid = MdoRemoteConfigLoad(&config) && !config.AllowRemote;
        bool missing = !MdoRemoteIdentityLoad(100u,false,&one);
        bool stored = MdoRemoteIdentityLoad(100u,true,&one) && one.Persistent &&
            MdoRemoteIdentityLoad(100u,false,&copy) && !strcmp(one.Id,copy.Id) && !strcmp(one.Secret,copy.Secret);
        bool separate = MdoRemoteIdentityLoad(101u,true,&two) && strcmp(one.Id,two.Id) && strcmp(one.Secret,two.Secret);
        strcpy(config.Name,"portable test"); config.AllowRemote = true; config.MemberId = 0u;
        bool owner_gate = !MdoRemoteConfigSave(&config);
        config.AllowRemote = false;
        bool saved = MdoRemoteConfigSave(&config) && MdoRemoteConfigLoad(&config) && !config.AllowRemote;
        strcpy(config.Name,"invalid\nname"); bool name_gate = !MdoRemoteConfigSave(&config);
        out = xrtValueObject(); MdoAccountSetBool(out,"default_off",valid); MdoAccountSetBool(out,"missing",missing);
        MdoAccountSetBool(out,"stable",stored); MdoAccountSetBool(out,"separate",separate);
        MdoAccountSetBool(out,"owner_gate",owner_gate); MdoAccountSetBool(out,"saved",saved); MdoAccountSetBool(out,"name_gate",name_gate);
        MdoRemoteIdentityClear(&one); MdoRemoteIdentityClear(&copy); MdoRemoteIdentityClear(&two);
    } else if (RemoteTestQuery(c->Target.Query,"invalid")) {
        MdoRemoteConfig config; MdoRemoteIdentity identity;
        bool config_ok = MdoRemoteConfigLoad(&config);
        bool identity_ok = MdoRemoteIdentityLoad(100u,true,&identity);
        out = xrtValueObject(); MdoAccountSetBool(out,"config_accepted",config_ok);
        MdoAccountSetBool(out,"allow_remote",config.AllowRemote); MdoAccountSetBool(out,"identity_accepted",identity_ok);
        MdoRemoteIdentityClear(&identity);
    } else {
        xrtMutexLock(RemoteTest.Lock); out = xrtValueClone(RemoteTest.Result); xrtMutexUnlock(RemoteTest.Lock);
    }
    return MdoApiReplySuccessTake(c,200,out,NULL);
}
'''


def run(host, website_host):
    # Devices now belong to the unified mdo plugin, not xadmin's generic
    # plugin catalogue. Reuse its isolated activation fixture; never enable
    # plugins or register test accounts in the maintained website directory.
    website, website_port, admin_user, admin_password = remote_website_fixture()
    app_port = free_port()
    origin = f'http://127.0.0.1:{website_port}'
    processes,logs,clients = [],[],[]

    def launch(site,executable,home=None):
        log = (site/'test.log').open('ab'); logs.append(log)
        env = dict(os.environ)
        if home: env['MDO_HOME'] = str(home)
        process = subprocess.Popen([str(executable),str(site/'xs.json')],cwd=site,env=env,stdout=log,stderr=log)
        processes.append(process); return process

    def wait(port,path,process):
        for _ in range(150):
            if process.poll() is not None: raise AssertionError(f'host exited: {process.args}')
            try:
                if request(port,'GET',path)[0] == 200: return
            except OSError: pass
            time.sleep(.05)
        raise AssertionError('host readiness timeout')

    def call(port,method,path,body=None,headers=None,status=200):
        actual,head,raw = request(port,method,path,body,headers)
        assert actual == status,(path,actual,raw)
        return json.loads(raw),head

    def app(method='GET',path='/test-remote',body=None):
        headers = {}
        if method != 'GET':
            _,head = call(app_port,'GET','/api/v1/account')
            headers['X-Mdo-Write-Token'] = head['X-Mdo-Write-Token']
        return call(app_port,method,'/api/v1'+path,body,headers)[0]['data']

    def stage(expected):
        for _ in range(200):
            value = app()
            if value and value.get('stage') in expected: return value
            time.sleep(.025)
        raise AssertionError(value)

    try:
        web_process = launch(website,website_host); wait(website_port,'/admin/login',web_process)
        login,head = call(website_port,'POST','/admin/login',{'username':admin_user,'password':admin_password})
        assert login['result']
        call(website_port,'POST','/api/v1/register',{'username':'remote_native_test','password':PASSWORD},status=201)
        tokens,_ = call(website_port,'POST','/api/v1/login',{'identifier':'remote_native_test','password':PASSWORD})
        bearer = {'Authorization':'Bearer '+tokens['data']['access_token']}
        (ROOT/'.build').mkdir(exist_ok=True)
        site = Path(tempfile.mkdtemp(prefix='remote-service-',dir=ROOT/'.build'))
        shutil.copytree(ROOT/'app',site,dirs_exist_ok=True); home = site/'mdo-home'
        config = json.loads((site/'xs.json').read_text()); service = config['services'][0]
        service.update({'class':'http','port':app_port}); service.pop('window',None)
        (site/'xs.json').write_text(json.dumps(config))
        unity = site/'generated/mdo_unity.c'
        unity.write_text('#define MDO_ACCOUNT_SERVICE_ORIGIN '+json.dumps(origin)+'\n'+unity.read_text(encoding='utf-8'),encoding='utf-8')
        router = site/'src/api/router.c'; source = router.read_text()
        source = source.replace('static const MdoApiRoute g_MdoApiRoutes[]',HOOK+'\nstatic const MdoApiRoute g_MdoApiRoutes[]')
        source = source.replace('static const MdoApiRoute g_MdoApiRoutes[] = {',
            'static const MdoApiRoute g_MdoApiRoutes[] = {\n {"/api/v1/test-remote",XHTTP_METHOD_GET,"GET",RemoteTestRoute,false},')
        router.write_text(source)
        bootstrap = site/'src/bootstrap/service.c'; source = bootstrap.read_text()
        source = source.replace('MdoApiUnit();','RemoteTestUnit();\n    MdoApiUnit();'); bootstrap.write_text(source)
        process = launch(site,host,home); wait(app_port,'/api/v1/account',process)
        assert not (home/'config/remote.json').exists()
        storage = app(path='/test-remote?storage'); assert all(storage.values()),storage
        for owner in (100,101):
            raw = (home/f'data/remote/accounts/{owner}.bin').read_bytes()
            assert raw.startswith(b'MDOACCOUNT1\n') and b'device_secret' not in raw
        # Corrupt existing identities must not be replaced even on explicit create.
        broken = home/'data/remote/accounts/100.bin'; broken.write_bytes(b'invalid envelope')
        (home/'config/remote.json').write_text(json.dumps({'schema_version':2,'name':'bad','member_id':1,'allow_remote':True}))
        invalid = app(path='/test-remote?invalid'); assert not any(invalid.values()),invalid
        assert broken.read_bytes() == b'invalid envelope'
        print('PASS default off, portable config, encrypted stable account-specific identities, invalid config and corrupt-credential preservation')
        app('POST','/account/login',{'identifier':'remote_native_test','password':PASSWORD,'remember':False})
        for _ in range(100):
            account = app(path='/account')
            if account['state'] == 'signed_in' and not account['busy']: break
            time.sleep(.025)
        else: raise AssertionError(account)
        app(path='/test-remote?start'); value = stage({'ready','failed'}); assert value['stage'] == 'ready',value
        assert value['registration_status'] == 201,value
        id = value['device_id']; owner = value['member_id']
        listing,_ = call(website_port,'GET','/api/v1/devices',headers=bearer)
        assert listing['data']['devices'][0]['online'] and listing['data']['devices'][0]['id'] == id
        proof,_ = call(website_port,'POST','/api/v1/devices/ticket',
            {'device_id':id,'role':'controller','mode':'control'},bearer)
        controller = WebSocket(website_port,{'Origin':'http://127.0.0.1:12345'},path='/api/v1/devices/connect',
            protocol=proof['data']['protocol']+', xadmin.ticket.'+proof['data']['ticket']); clients.append(controller)
        assert json.loads(controller.recv()[1])['type'] == 'ready'
        marker = b'{"type":"native-component-test","body":"transient-payload"}'
        controller.send(1,marker); assert controller.recv() == (1,marker)
        binary = bytes(range(256)); controller.send(2,binary); assert controller.recv() == (2,binary)
        call(website_port,'POST','/api/v1/devices/revoke',{'device_id':id},bearer)
        opcode,payload = controller.recv(); assert opcode == 8 and struct.unpack('!H',payload[:2])[0] == 1008
        value = stage({'closed','failed'}); assert value['stage'] == 'closed' and value['close_code'] == 1008,value
        assert value['messages'] == 2,value
        app(path='/test-remote?start'); value = stage({'failed','ready'}); assert value['stage'] == 'failed' and value['status'] == 403,value
        app(path='/test-remote?reactivate'); value = stage({'ready','failed'})
        assert value['stage'] == 'ready' and value['device_id'] == id,value
        assert value['registration_status'] == 200,value
        # A borrowed/cancelled account lease cannot remain connected after logout.
        app('POST','/account/logout',{})
        value = stage({'closed','failed'}); assert value['stage'] == 'closed',value
        stored = json.loads((home/'config/remote.json').read_text()); assert not stored['allow_remote']
        raw = (home/f'data/remote/accounts/{owner}.bin').read_bytes(); assert b'device_secret' not in raw
        print('PASS native registration (201), device listing, one-use ticket/WS connection, text/binary relay, revoke, explicit reactivation and account-lease cancellation')
    finally:
        for client in clients: client.close()
        for process in reversed(processes):
            if process.poll() is None: process.terminate(); process.wait(timeout=20)
        for log in logs: log.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',type=Path,default=ROOT/'.build/host/xs.exe')
    parser.add_argument('--website-host',type=Path,default=WEBSITE_HOST,
        help='Host matching the unified website plugin SDK, independent of the client host')
    args = parser.parse_args()
    run(args.host.resolve(),args.website_host.resolve())
