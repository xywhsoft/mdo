"""One actual TCC host replacement with open remote HTTP/live connections."""
import argparse,json,socket,time
from pathlib import Path
from test_remote_manager import run,ROOT,XADMIN,PASSWORD
from test_remote_live import LiveRpc,setup
from channel_e2e import WebSocket

HOOK='''static bool RemoteFullReloadTest(MdoApiContext* c) {
    if (!xsReloadHostSubmit((XS_HostInfo*)c->Request->host))
        return MdoApiReplyError(c,500u,"reload_failed","Host reload was rejected",NULL);
    return MdoApiReplySuccessTake(c,202u,xrtValueObject(),NULL);
}
static bool RemoteFullReloadGeneration(MdoApiContext* c) {
    static char marker[32];
    if (!marker[0]) snprintf(marker,sizeof(marker),"%llu",(unsigned long long)xrtNow());
    xvalue* value=xrtValueObject(); MdoAccountSetString(value,"marker",marker);
    MdoAccountSetUInt(value,"port_bound",c->Request->server->PortBound);
    return MdoApiReplySuccessTake(c,200u,value,NULL);
}
'''
ROUTE=' {"/api/v1/test-host-reload",XHTTP_METHOD_POST,"POST",RemoteFullReloadTest,false}, {"/api/v1/test-host-generation",XHTTP_METHOD_GET,"GET",RemoteFullReloadGeneration,false},'

def exercise(*,client,hello,app,device,call,bearer,website_port,clients):
    rpc=LiveRpc(client,hello)
    _,heads,_,_=rpc.json('GET','/api/v1/settings');token=heads['X-Mdo-Write-Token']
    assert rpc.open_live(token) is None
    persistent=app()['persistent']
    generation=app(path='/test-host-generation');marker=generation['marker']
    assert generation['port_bound']>0
    app('POST','/test-host-reload',{},202)
    # Reading the new API nonce proves that initialization of a distinct TCC
    # generation finished, rather than merely restarting the Remote subsystem.
    until=time.monotonic()+30;fresh=None
    while time.monotonic()<until:
        try:
            value=app()
            if app(path='/test-host-generation')['marker']==marker:time.sleep(.05);continue
            if value['stage'] not in ('online','disabled'):time.sleep(.05);continue
            fresh=value;break
        except OSError:pass
        except AssertionError as error:
            response=error.args[0]
            if not isinstance(response,tuple) or response[1]!=503 or b'home_reload_pending' not in response[2]:raise
        time.sleep(.05)
    assert fresh is not None,'new TCC generation did not become available'
    assert app(path='/test-host-generation')['port_bound']==generation['port_bound'], 'retained endpoint metadata was lost'
    client.socket.settimeout(5)
    try:
        closed=False
        for _ in range(100):
            opcode,_=client.recv()
            if opcode==8:closed=True;break
        assert closed,'old peer survived host unload'
    except socket.timeout:raise AssertionError('old peer did not close after host unload')
    except (EOFError,ConnectionError,OSError):pass
    client.close();clients.remove(client)
    if not persistent:
        # Platforms without a credential protector deliberately retain no
        # account/device secret across a complete code-generation replacement.
        assert not fresh['allow_remote']
        app('POST','/account/login',{'identifier':'remote_manager_one','password':PASSWORD,'remember':False})
        for _ in range(200):
            account=app(path='/account')
            if account['state']=='signed_in' and not account['busy']:break
            time.sleep(.05)
        else:raise AssertionError('account restoration failed')
        app('POST','/remote',{'allow_remote':True,'name':'explicit post-reload activation'},202)
    for _ in range(400):
        fresh=app()
        if fresh['stage']=='online':break
        time.sleep(.05)
    else:raise AssertionError(('post-reload reconnect',fresh))
    if persistent:assert fresh['device_id']==device
    device=fresh['device_id']
    listing,_=call(website_port,'GET','/api/v1/devices',headers=bearer)
    metadata=next(v for v in listing['data']['devices'] if v['id']==device)
    queued=app('POST','/connector/devices',{'action':'connect','device_id':device,'mode':'control'},202)
    for _ in range(200):
        state=app(path='/connector/state')
        if state['job']['id']==queued['job']['id'] and state['job']['state'] in ('done','failed'):break
        time.sleep(.05)
    assert state['job']['state']=='done',state
    ticket=app('POST','/connector/ticket',{'job_id':queued['job']['id']})
    peer=WebSocket(website_port,{'Origin':'http://127.0.0.1:12345'},path=ticket['path'],
        protocol=ticket['protocol']+', xadmin.ticket.'+ticket['ticket']);clients.append(peer)
    assert json.loads(peer.recv()[1])['type']=='ready'
    new_hello=json.loads(peer.recv()[1]);assert new_hello['runtime_id']!=hello['runtime_id']
    new=LiveRpc(peer,new_hello)
    _,new_heads,_,_=new.json('GET','/api/v1/settings')
    assert new_heads['X-Mdo-Write-Token']!=token
    assert new.open_live(new_heads['X-Mdo-Write-Token']) is None
    print('PASS actual TCC generation unload joins open remote/live channels; new nonce/runtime and explicit ephemeral identity boundary')
    return {'client':peer,'device':device,'updated_at':metadata['updated_at']}

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',type=Path,default=ROOT/'.build/host/xs.exe')
    parser.add_argument('--website-host',type=Path,default=XADMIN/'xs.exe')
    args=parser.parse_args()
    run(args.host.resolve(),args.website_host.resolve(),exercise,native_hook=HOOK,native_routes=ROUTE,site_setup=setup)
