"""Real account relay, one-use rendezvous, native xrudp and actual mdo routes.

Uses disposable local accounts only. Test policy binds loopback explicitly;
this proves integration/authority/route behavior, not public Internet access.
"""
from pathlib import Path
import argparse
import json
import select
import struct
import time
import uuid
from test_account_runtime import ROOT, request
from test_remote_manager import run, WebSocket
from test_remote_bridge import Rpc
from test_remote_live import LiveRpc
from remote_website_fixture import WEBSITE_HOST
from test_rudp_discovery import Oracles

SITE=None
FAMILY=4
ORACLE=None
IDLE_SECONDS=0
DUAL=False
BLOCK_FAMILY=0


def setup(site, home):
    global SITE
    SITE=site
    # Exercise the public native local-bind policy, without depending on the
    # test machine's physical adapter/firewall or publishing its real IPs.
    for relative, needle, prefix in (
        ('src/remote/direct_rudp.inc.c',
         'MdoPeerPolicyInit(&Direct->Policy,Net,MDO_ACCOUNT_SERVICE_ORIGIN);',
         'Direct->Policy'),
        ('src/remote/lan_gateway.inc.c',
         'MdoPeerPolicyInit(&Policy,&Gateway->Net,MDO_ACCOUNT_SERVICE_ORIGIN);',
         'Policy')):
        replacement=needle+f' {prefix}.StunHost[0]=0;'
        for family in ((4,6) if DUAL else (FAMILY,)):
            replacement+=f' xrtNetAddrLoopback(&{prefix}.Local[{family==6:d}],XNET_FAMILY_IPV{family},0);'
            if ORACLE:
                side=0 if 'direct_rudp' in relative else 1
                replacement+=f' xrtNetAddrLoopback(&{prefix}.Stun[{family==6:d}],XNET_FAMILY_IPV{family},{ORACLE.ports[(family,f"natinfo{side}")]});'
        path=site/relative; text=path.read_text(encoding='utf-8'); assert needle in text
        path.write_text(text.replace(needle,replacement),encoding='utf-8')
    if DUAL and ORACLE:
        # Distinct private LANs: only independently discovered public mappings
        # can reach the other endpoint, never the oracle's shared loopback.
        path=site/'src/remote/rendezvous.inc.c'; text=path.read_text(encoding='utf-8')
        needle='    if (!xrtNetAddrIsUnspecified(&Info->Local)) {'; assert text.count(needle)==1
        path.write_text(text.replace(needle,'    if (Info->Discovery.Complete) return List;\n'+needle),encoding='utf-8')


def gateway(port, token, offer):
    result=WebSocket(port,{'Origin':f'http://127.0.0.1:{port}'},path='/api/v1/connector/direct',
        protocol=f'mdo.direct.v1, mdo.token.{token}')
    result.send(1,json.dumps({'transport':'xrudp','offer':offer}).encode()); return result


def negotiate(native, relay, offer, *, misplaced=None, observe=None):
    end=time.monotonic()+12; sent=0; selected=None
    while time.monotonic()<end:
        available=[s for s in (native,relay) if s.pending]
        if not available:
            ready,_,_=select.select([native.socket,relay.socket],[],[],.1)
            available=[s for s in (native,relay) if s.socket in ready]
        for source in available:
            opcode,payload=source.recv()
            if opcode==9: source.send(10,payload); continue
            assert opcode==1,(opcode,payload)
            value=json.loads(payload)
            if source is native:
                if value.get('type')=='direct_ready':
                    assert value['transport']=='xrudp' and value['peer_id']==offer['peer_id']
                    assert value['runtime_id']==offer['runtime_id'] and value['mode']==offer['mode']
                    assert sent>=3 and selected is not None
                    # The native gateway has already taken its selected socket.
                    # A late repeated acknowledgement must remain a control
                    # message, never leak into the target's business protocol.
                    native.send(1,json.dumps({'type':'rudp_signal','payload':selected}).encode())
                    return value
                assert value['type']=='rudp_forward',value
                control=value['payload']; assert control['nonce']==offer['nonce']
                if observe: observe('controller',control)
                if misplaced and control['type']=='rudp_init':
                    # Same account, different authenticated peer: the relay
                    # attaches its real peer identity, so this offer is unusable.
                    misplaced.send(1,json.dumps(control).encode())
                relay.send(1,json.dumps(control).encode()); sent+=1
            else:
                assert value['type'] in ('rudp_answer','rudp_selected','rudp_failed'),value
                assert value['nonce']==offer['nonce'],value
                assert value['type']!='rudp_failed',value
                if value['type']=='rudp_selected': selected=value
                if observe: observe('target',value)
                native.send(1,json.dumps({'type':'rudp_signal','payload':value}).encode())
    raise AssertionError('xrudp rendezvous deadline')


class BufferedRpc(Rpc):
    def __init__(self,*args,**kwargs): super().__init__(*args,**kwargs); self.inbox=[]
    def receive(self): return self.inbox.pop(0) if self.inbox else super().receive()


def exercise(*,client,hello,app,device,call,bearer,website_port,clients):
    assert 'rudp' in hello,hello
    offer=hello['rudp']; assert offer['runtime_id']==hello['runtime_id'] and offer['mode']=='control'
    assert set(offer)=={'version','peer_id','runtime_id','owner_id','mode','nonce','public_key','scope','binding','families'}
    assert 'private' not in json.dumps(offer)
    port=json.loads((SITE/'xs.json').read_text())['services'][0]['port']
    _,headers,_=request(port,'GET','/api/v1/project-purge-intent'); token=headers['X-Mdo-Write-Token']
    ticket,_=call(website_port,'POST','/api/v1/devices/ticket',{'device_id':device,'role':'controller','mode':'view'},bearer)
    ticket=ticket['data']; viewer=WebSocket(website_port,{'Origin':f'http://127.0.0.1:{port}'},
        path=ticket['path'],protocol=ticket['protocol']+', xadmin.ticket.'+ticket['ticket'])
    clients.append(viewer); viewer.recv(); view_hello=json.loads(viewer.recv()[1])
    # A different local account owner cannot use the offer at the native gate.
    bad=gateway(port,token,{**offer,'owner_id':str(int(offer['owner_id'])+1)})
    try:
        opcode,_=bad.recv(); assert opcode==8
    except EOFError: pass
    bad.close()
    direct=gateway(port,token,offer); clients.append(direct)
    observations=[]
    negotiate(direct,client,offer,misplaced=viewer,observe=lambda who,value:observations.append((who,value)))
    if DUAL:
        chosen=[value['family'] for _,value in observations if value['type'] in ('rudp_select','rudp_selected')]
        assert chosen==[4 if BLOCK_FAMILY==6 else 6]*2,chosen
    rpc=BufferedRpc(direct,hello); relay=Rpc(client,hello,client_id=rpc.client)
    assert rpc.json('GET','/api/v1/projects')[2]==200
    assert relay.json('GET','/api/v1/settings')[2]==200
    # Deliberately collide IDs while the direct upload is still pending. The
    # relay's conflict reply must not be stolen by the direct route's ID table.
    body=json.dumps({'id':'rudp-route-project','name':'RUDP project'}).encode()
    original=rpc.begin('POST','/api/v1/projects',body,[['Content-Type','application/json'],['X-Mdo-Write-Token',token]])
    ready=rpc.receive(); assert ready['type']=='request_ready' and ready['id']==original['id'],ready
    conflict,_=relay.call('GET','/api/v1/projects',request_id=original['id'])
    assert conflict['type']=='error' and conflict['code']=='request_conflict',conflict
    relay.send({'type':'cancel','id':original['id']})
    rpc.inbox.append(ready); made=rpc.collect(original,body)
    assert made['head']['status']==201 and json.loads(made['body'])['data']['id']=='rudp-route-project',made
    relay.send({'type':'receipt','id':original['id'],'runtime_id':hello['runtime_id'],'client_id':rpc.client,'sequence':original['sequence']})
    receipt=relay.receive(); assert receipt['type']=='receipt' and receipt['state']=='done' and receipt['status']==201,receipt
    live=LiveRpc(direct,hello)
    for _ in range(9):
        live.live=uuid.uuid4().hex; live.offset=live.event_sequence=0
        live.send({'type':'live_open','id':live.live,'runtime_id':hello['runtime_id'],'token':token})
        assert Rpc.receive(live)['type']=='live_opened'; assert live.next_event()['type']=='ready'; live.close_live()
    readonly=gateway(port,token,view_hello['rudp']); clients.append(readonly)
    negotiate(readonly,viewer,view_hello['rudp'])
    refused,_=Rpc(readonly,view_hello).call('POST','/api/v1/projects',b'{}')
    assert refused['code']=='read_only',refused
    if IDLE_SECONDS:
        # No application RPC/live traffic during the negotiated idle lease.
        # Native encrypted PING must keep both channels and NAT filters alive.
        end=time.monotonic()+IDLE_SECONDS
        sockets=(client,direct,viewer,readonly)
        while time.monotonic()<end:
            available=[s for s in sockets if s.pending]
            if not available:
                ready,_,_=select.select([s.socket for s in sockets],[],[],min(.25,max(0,end-time.monotonic())))
                available=[s for s in sockets if s.socket in ready]
            for source in available:
                opcode,payload=source.recv()
                assert opcode in (9,10),(opcode,payload)
                if opcode==9: source.send(10,payload)
        assert rpc.json('GET','/api/v1/projects')[2]==200
        assert relay.json('GET','/api/v1/projects')[2]==200
        print(f'PASS native xrudp direct RPC after {IDLE_SECONDS}s without application traffic')
    # The offer was consumed; a second native gateway cannot rebind its key.
    replay=gateway(port,token,offer); replay.socket.settimeout(3)
    first=json.loads(replay.recv()[1]); assert first['type']=='rudp_forward'
    client.send(1,json.dumps(first['payload']).encode()); replay.close()
    # Native direct cancellation affects only that route, keeping relay and its
    # receipt identity live. New writes are not silently moved or replayed.
    client.send(1,json.dumps({'type':'rudp_cancel','nonce':offer['nonce'],'runtime_id':hello['runtime_id']}).encode())
    direct.close(); clients.remove(direct)
    assert relay.json('GET','/api/v1/projects')[2]==200
    viewer.close(); clients.remove(viewer)
    readonly.close(); clients.remove(readonly)
    if ORACLE:
        assert not ORACLE.errors,ORACLE.errors
        used=chosen[0] if DUAL else FAMILY
        assert ORACLE.nat[used]['forwarded']>0 and ORACLE.nat[used]['warm']>0,ORACLE.nat[used]
        if BLOCK_FAMILY:
            assert ORACLE.nat[BLOCK_FAMILY]['warm']>0 and ORACLE.nat[BLOCK_FAMILY]['forwarded']==0,ORACLE.nat
            if BLOCK_FAMILY==6: assert ORACLE.blocked_packets[6]>0,'preferred IPv6 handshake must meet the blackhole'
    print(f'PASS authenticated xrudp family={"dual" if DUAL else FAMILY} blocked={BLOCK_FAMILY} nat={bool(ORACLE)}, owner/peer/nonce pinning, real HTTP/live/receipts, collision route isolation, readonly, one-use and relay fallback')
    return client


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__); parser.add_argument('--host',type=Path,required=True)
    parser.add_argument('--website-host',type=Path,default=WEBSITE_HOST)
    parser.add_argument('--family',type=int,choices=(4,6),default=4); parser.add_argument('--nat',action='store_true')
    parser.add_argument('--dual',action='store_true')
    parser.add_argument('--block-family',type=int,choices=(4,6),default=0)
    parser.add_argument('--idle-seconds',type=int,choices=range(61,91),metavar='61..90',default=0)
    args=parser.parse_args(); FAMILY=args.family; IDLE_SECONDS=args.idle_seconds; DUAL=args.dual; BLOCK_FAMILY=args.block_family
    if BLOCK_FAMILY and not (args.dual and args.nat): parser.error('--block-family requires --dual --nat')
    if IDLE_SECONDS and args.nat: parser.error('idle gate uses independent loopback peers; the NAT fixture models one pair only')
    if args.nat:
        with Oracles() as ORACLE:
            ORACLE.blocked_families={BLOCK_FAMILY} if BLOCK_FAMILY else set()
            run(args.host.resolve(),args.website_host.resolve(),exercise,site_setup=setup)
    else: run(args.host.resolve(),args.website_host.resolve(),exercise,site_setup=setup)
