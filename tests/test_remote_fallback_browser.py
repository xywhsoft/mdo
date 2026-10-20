"""Real browser automatic UDP -> LAN -> relay fallback against disposable mdo.

Requires an existing Chromium browser and an explicit existing Playwright tool
path. Does not add a product dependency, use public accounts, or call models.
"""
from pathlib import Path
import argparse
import json
import socket
import subprocess
import time
import uuid

from test_account_runtime import ROOT, request
from test_remote_manager import run, WebSocket
from test_remote_bridge import Rpc
from remote_website_fixture import WEBSITE_HOST
import test_remote_rudp as native
from test_rudp_discovery import Oracles

HOOK = '''
static bool RemoteRudpCountTest(MdoApiContext* C) {
    xvalue* V=xrtValueObject();
    if (!V || !MdoAccountSetUInt(V,"transports",xrtAtomic32Load(&g_MdoRudpTransports,XMEMORY_ACQUIRE))) {
        xrtValueRelease(V); return false;
    }
    return MdoApiReplySuccessTake(C,200u,V,NULL);
}
'''
ROUTE = '{"/api/v1/test-rudp-count",XHTTP_METHOD_GET,"GET",RemoteRudpCountTest,false},'


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',type=Path,required=True)
    parser.add_argument('--website-host',type=Path,default=WEBSITE_HOST)
    parser.add_argument('--browser',type=Path,required=True)
    parser.add_argument('--node',type=Path,required=True)
    parser.add_argument('--playwright',type=Path,required=True)
    args=parser.parse_args()
    for path in (args.host,args.website_host,args.browser,args.node): assert path.is_file(),path
    assert (args.playwright/'package.json').is_file()
    with Oracles() as oracle, socket.socket() as silent:
        oracle.blocked_families={4,6}
        # A real local TCP listener accepts the SYN but never completes TLS.
        # It remains bound for the case, preventing an accidental service hit.
        silent.bind(('127.0.0.1',0)); silent.listen(8)
        native.ORACLE=oracle; native.DUAL=True
        def setup(site,home):
            native.setup(site,home)
            (site/'web/__qa_remote.html').write_text('<!doctype html><title>Remote fallback QA</title>',encoding='utf-8')
        def exercise(*,client,hello,app,device,call,bearer,website_port,clients):
            port=json.loads((native.SITE/'xs.json').read_text())['services'][0]['port']
            def ticket(mode):
                end=time.monotonic()+3
                while True:
                    status,_,raw=request(website_port,'POST','/api/v1/devices/ticket',
                        {'device_id':device,'role':'controller','mode':mode},bearer)
                    if status==200: return json.loads(raw)['data']
                    assert status==409 and time.monotonic()<end,('ticket admission',status)
                    time.sleep(.05)
            # The service intentionally permits one controlling peer. Keep a
            # separate view peer as an observer; each browser owns control.
            client.close(); clients.remove(client)
            view=ticket('view')
            client=WebSocket(website_port,{'Origin':f'http://127.0.0.1:{port}'},path=view['path'],
                protocol=view['protocol']+', xadmin.ticket.'+view['ticket']); clients.append(client)
            assert json.loads(client.recv()[1])['type']=='ready'; observer=json.loads(client.recv()[1])
            cases=[]
            for lan in (True,False):
                before=dict(oracle.blocked_packets)
                data={'browser':str(args.browser.resolve()),'playwright':str(args.playwright.resolve()),
                    'url':f'http://127.0.0.1:{port}/__qa_remote.html','lan':lan,'silentPort':silent.getsockname()[1],
                    'prefix':'fallback-'+uuid.uuid4().hex[:14],
                    'connection':{'origin':f'http://127.0.0.1:{website_port}','ticket':ticket('control'),
                        'deviceId':device,'mode':'control','owner':hello['rudp']['owner_id']}}
                output=subprocess.run([str(args.node.resolve()),str(ROOT/'tests/fixtures/remote-fallback-browser.mjs')],
                    input=json.dumps(data),text=True,encoding='utf-8',capture_output=True,timeout=75)
                assert output.returncode==0,output.stderr[-6000:]
                result=json.loads(output.stdout)
                for family in (4,6):
                    assert oracle.blocked_packets[family]>before[family],('native handshake did not reach blackhole',family)
                    assert oracle.nat[family]['forwarded']==0,oracle.nat
                end=time.monotonic()+5
                while time.monotonic()<end:
                    status,_,raw=request(port,'GET','/api/v1/test-rudp-count')
                    assert status==200,status
                    count=json.loads(raw)['data']['transports']
                    if count==0: break
                    time.sleep(.02)
                assert count==0,'browser fallback/close leaked native transports'
                assert Rpc(client,observer).json('GET','/api/v1/projects')[2]==200,'browser close affected independent relay'
                result['native_transports_after_close']=count
                result['blocked_packets']={str(f):oracle.blocked_packets[f]-before[f] for f in (4,6)}
                cases.append(result)
                print('PASS real browser both UDP blackholes -> '+result['final_route']+', actual HTTP/write, no write replay and zero native transports')
            (native.SITE/'browser-fallback-receipt.json').write_text(json.dumps(cases,indent=2)+'\n',encoding='utf-8')
            print('evidence',native.SITE/'browser-fallback-receipt.json')
            return client
        run(args.host.resolve(),args.website_host.resolve(),exercise,native_hook=HOOK,native_routes=ROUTE,site_setup=setup)
        assert not oracle.errors,oracle.errors
    return 0


if __name__=='__main__': raise SystemExit(main())
