"""Real relay -> target routes with binary flow control and write dedup.

Single functional fixture, two peers at most; no pressure/high-load testing.
"""
from __future__ import annotations
import argparse
import base64
import hashlib
import json
import socket
import struct
import time
import uuid
from urllib.parse import quote
from pathlib import Path

from test_remote_manager import run, ROOT, XADMIN, WebSocket
from remote_website_fixture import WEBSITE_HOST
from test_account_runtime import request as http_request

HOOK = r'''
static unsigned BridgeTestWrites;
static bool BridgeTestRoute(MdoApiContext* c) {
    if (xrtStrEqual(c->Target.Path,XRT_STR_LITERAL("/api/v1/test-bridge-file"))) {
        const size_t size = 257123u; uint8* bytes = xrtMalloc(size);
        if (!bytes) return false;
        for (size_t i = 0u; i < size; i++) bytes[i] = (uint8)(i*7u);
        bool ok = MdoApiReplyRaw(c,200u,bytes,size,NULL,"\"bridge-file\"","application/octet-stream","attachment; filename=bridge.bin");
        xrtFree(bytes); return ok;
    }
    if (c->Request->head->MethodCode == XHTTP_METHOD_POST) { ++BridgeTestWrites; xrtSleep(250u); }
    xvalue* value = xrtValueObject(); MdoAccountSetUInt(value,"writes",BridgeTestWrites);
    return MdoApiReplySuccessTake(c,200u,value,NULL);
}
'''
ROUTES = '''
 {"/api/v1/test-bridge-file",XHTTP_METHOD_GET|XHTTP_METHOD_HEAD,"GET, HEAD",BridgeTestRoute,false},
 {"/api/v1/test-bridge-write",XHTTP_METHOD_GET|XHTTP_METHOD_POST,"GET, POST",BridgeTestRoute,false},
'''


class Rpc:
    def __init__(self, socket, hello, client_id=None):
        self.socket, self.hello = socket, hello
        self.client = client_id or uuid.uuid4().hex
        self.sequence = 0

    def send(self, value):
        self.socket.send(1,json.dumps(value,separators=(',',':')).encode())

    def receive(self):
        while True:
            opcode,payload = self.socket.recv()
            if opcode == 9: self.socket.send(10,payload); continue
            assert opcode in (1,2),(opcode,payload)
            if opcode == 1: return json.loads(payload)
            assert len(payload) > 29 and payload[:4] == b'MDP1' and payload[4] in (2,3)
            return {'type':'chunk' if payload[4] == 2 else 'live_chunk','id':payload[5:21].hex(),
                'offset':struct.unpack('!Q',payload[21:29])[0],'body':payload[29:]}

    def begin(self, method, path, body=b'', headers=(), request_id=None, sequence=None, runtime=None, digest=None, window=None):
        write = method not in ('GET','HEAD')
        if sequence is None:
            if write: self.sequence += 1
            sequence = self.sequence if write else 0
        value = {'type':'request','id':request_id or uuid.uuid4().hex,'runtime_id':runtime or self.hello['runtime_id'],
            'client_id':self.client,'sequence':sequence,'method':method,'path':path,'headers':list(headers),
            'bytes':len(body),'sha256':digest or hashlib.sha256(body).hexdigest()}
        if window is not None: value['window_bytes'] = window
        self.send(value)
        return value

    def collect(self, request, body=b'', pause=False):
        result = {'body':bytearray(),'head':None}; sent = 0
        while True:
            value = self.receive(); assert value['id'] == request['id'],value
            kind = value['type']
            if kind in ('request_ready','upload_ack'):
                assert value['offset'] == sent
                if sent < len(body):
                    part = body[sent:sent+65536]
                    self.socket.send(2,b'MDP1\x01'+bytes.fromhex(request['id'])+struct.pack('!Q',sent)+part)
                    sent += len(part)
            elif kind == 'response': result['head'] = value
            elif kind == 'chunk':
                assert value['offset'] == len(result['body'])
                result['body'] += value['body']
                if not pause: self.send({'type':'download_ack','id':request['id'],'offset':len(result['body'])})
            elif kind == 'end':
                assert value['bytes'] == len(result['body']); return result
            elif kind in ('receipt','error'): return value
            else: raise AssertionError(value)

    def call(self, method, path, body=b'', headers=(), **options):
        value = self.begin(method,path,body,headers,**options)
        return self.collect(value,body),value

    def json(self, method, path, body=None, headers=(), **options):
        encoded = b'' if body is None else json.dumps(body).encode()
        fields = list(headers)
        if body is not None: fields.append(['Content-Type','application/json'])
        result,value = self.call(method,path,encoded,fields,**options)
        assert result.get('head'),result
        return json.loads(result['body']),dict(result['head']['headers']),result['head']['status'],value


def exercise(*, client, hello, app, device, call, bearer, website_port, clients):
    rpc = Rpc(client,hello)
    settings,headers,status,_ = rpc.json('GET','/api/v1/settings')
    assert status == 200 and settings['ok']
    nonce = headers['X-Mdo-Write-Token']
    project = {'id':'remote-bridge-project','name':'远程项目'}
    made,_,status,original = rpc.json('POST','/api/v1/projects',project,[['X-Mdo-Write-Token',nonce]])
    assert status == 201 and made['data']['id'] == project['id'],made
    created,_,status,_ = rpc.json('POST','/api/v1/sessions',{'project_id':project['id'],'title':'远程会话'},
        [['X-Mdo-Write-Token',nonce]])
    assert status == 201,created
    session_id = created['data']['id']
    attachment_path = f'/api/v1/projects/{project["id"]}/sessions/{session_id}/attachments'
    png = base64.b64decode('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jJ1cAAAAASUVORK5CYII=')
    filename = '图'*250+'.png'
    uploaded,_ = rpc.call('POST',attachment_path,png,[['Content-Type','image/png'],['X-Mdo-Write-Token',nonce],['X-Mdo-File-Name',quote(filename)]])
    image = json.loads(uploaded['body'])['data']
    assert uploaded['head']['status'] == 201 and image['file_name'] == filename,image
    downloaded,_ = rpc.call('GET',image['url']); assert bytes(downloaded['body']) == png
    removed,_,status,_ = rpc.json('DELETE',image['url'],headers=[['X-Mdo-Write-Token',nonce]])
    assert status == 200,removed
    # Same ID/sequence/body returns its receipt, never the route's duplicate-ID
    # error. Changed content with that same ID is a fingerprint conflict.
    repeated,_ = rpc.call('POST','/api/v1/projects',json.dumps(project).encode(),original['headers'],
        request_id=original['id'],sequence=original['sequence'])
    assert repeated['type'] == 'receipt' and repeated['state'] == 'done' and repeated['status'] == 201,repeated
    changed,_ = rpc.call('POST','/api/v1/projects',b'{}',original['headers'],
        request_id=original['id'],sequence=original['sequence'])
    assert changed['result'] == 'request_conflict',changed
    # The native write token is passed through; remote control adds no bypass.
    _,_,status,_ = rpc.json('POST','/api/v1/projects',{'id':'nonce-refused','name':'refused'},[['X-Mdo-Write-Token','wrong']])
    assert status == 412
    for path,fields in (('/api/v1/connector/ticket',()),('/api/v1/%63onnector/ticket',()),
        ('/api/v1/settings',[['Host','other.invalid']]),('/api/v1/settings',[['Authorization','Bearer untrusted']])):
        invalid,_ = rpc.call('GET',path,headers=fields); assert invalid['code'] == 'invalid_request',invalid
    longest = '/api/v1/unknown?' + 'x'*(4096-len('/api/v1/unknown?'))
    boundary,_ = rpc.call('GET',longest,headers=[['Accept','x'*996],['X-Mdo-File-Name',quote('图'*340+'.png')]])
    assert boundary['head']['status'] == 404,'valid target+header limits must fit generated HTTP framing'
    corrupt,_ = rpc.call('POST','/api/v1/projects',b'{}',[['Content-Type','application/json'],['X-Mdo-Write-Token',nonce]],digest='0'*64)
    assert corrupt['code'] == 'body_digest_mismatch' and corrupt['outcome'] == 'not_started',corrupt
    invalid_runtime,_ = rpc.call('POST','/api/v1/projects',b'{}',runtime=uuid.uuid4().hex)
    assert invalid_runtime['code'] == 'runtime_changed',invalid_runtime
    # Invalid upload offset fails before the route. Query is still not_started.
    pending = rpc.begin('POST','/api/v1/projects',b'{}',[['Content-Type','application/json'],['X-Mdo-Write-Token',nonce]])
    assert rpc.receive()['type'] == 'request_ready'
    client.send(2,b'MDP1\x01'+bytes.fromhex(pending['id'])+struct.pack('!Q',1)+b'{}')
    assert rpc.receive()['code'] == 'invalid_chunk'
    rpc.send({'type':'receipt','id':pending['id'],'runtime_id':hello['runtime_id'],'client_id':rpc.client,'sequence':pending['sequence']})
    assert rpc.receive()['state'] == 'not_started'
    # Bounded pause in a file read. The unacknowledged window stops data while
    # a second ordinary read remains usable over the same connection.
    file_request = rpc.begin('GET','/api/v1/test-bridge-file'); file_bytes = bytearray(); file_head = None
    client.socket.settimeout(.25)
    while True:
        try: value = rpc.receive()
        except socket.timeout: break
        assert value['id'] == file_request['id'],value
        if value['type'] == 'response': file_head = value
        else:
            assert value['type'] == 'chunk' and value['offset'] == len(file_bytes),value
            file_bytes += value['body']
    assert file_head and 0 < len(file_bytes) <= hello['window_bytes'],len(file_bytes)
    client.socket.settimeout(5)
    read_request = rpc.begin('GET','/api/v1/test-bridge-write')
    second_bytes = bytearray(); second_head = None
    while True:
        value = rpc.receive(); assert value['id'] == read_request['id'],value
        if value['type'] == 'response': second_head = value
        elif value['type'] == 'chunk':
            second_bytes += value['body']; rpc.send({'type':'download_ack','id':read_request['id'],'offset':len(second_bytes)})
        else: assert value['type'] == 'end'; break
    assert second_head['status'] == 200 and json.loads(second_bytes)['data']['writes'] == 0
    rpc.send({'type':'download_ack','id':file_request['id'],'offset':len(file_bytes)})
    while True:
        value = rpc.receive(); assert value['id'] == file_request['id'],value
        if value['type'] == 'end': assert value['bytes'] == len(file_bytes); break
        assert value['type'] == 'chunk' and value['offset'] == len(file_bytes),value
        file_bytes += value['body']; rpc.send({'type':'download_ack','id':file_request['id'],'offset':len(file_bytes)})
    assert bytes(file_bytes) == bytes((i*7)&255 for i in range(257123))
    assert hello['window_max'] == 262144
    larger = rpc.begin('GET','/api/v1/test-bridge-file',window=hello['window_max'])
    transfer = rpc.collect(larger,pause=True)
    assert len(transfer['body']) == 257123, 'negotiated window completes without an ACK round trip'
    head,_ = rpc.call('HEAD','/api/v1/test-bridge-file'); assert head['head']['status'] == 200 and not head['body']
    # Trusted relay view role rejects mutations in native admission.
    ticket,_ = call(website_port,'POST','/api/v1/devices/ticket',{'device_id':device,'role':'controller','mode':'view'},bearer)
    viewer = WebSocket(website_port,{'Origin':'http://127.0.0.1:12345'},path=ticket['data']['path'],
        protocol=ticket['data']['protocol']+', xadmin.ticket.'+ticket['data']['ticket']); clients.append(viewer)
    assert json.loads(viewer.recv()[1])['type'] == 'ready'; view_hello = json.loads(viewer.recv()[1])
    readonly = Rpc(viewer,view_hello); assert view_hello['mode'] == 'view'
    refused,_ = readonly.call('POST','/api/v1/projects',b'{}'); assert refused['code'] == 'read_only',refused
    assert readonly.json('GET','/api/v1/projects')[2] == 200
    viewer.close(); clients.remove(viewer)
    multi_body = b'\x00\xffremote-upload'*6000
    posted,_ = rpc.call('POST','/api/v1/test-bridge-write',multi_body,
        [['Content-Type','application/octet-stream'],['X-Mdo-Write-Token',nonce]])
    assert posted['head']['status'] == 200 and json.loads(posted['body'])['data']['writes'] == 1
    # Drop a response after a second write has actually reached the target route.
    _,latest,_,_ = rpc.json('GET','/api/v1/settings'); nonce = latest['X-Mdo-Write-Token']
    lost = rpc.begin('POST','/api/v1/test-bridge-write',headers=[['X-Mdo-Write-Token',nonce]])
    time.sleep(.1); client.close(); clients.remove(client); time.sleep(.5)

    def reconnect():
        for _ in range(100):
            status,_,raw = http_request(website_port,'POST','/api/v1/devices/ticket',
                {'device_id':device,'role':'controller','mode':'control'},bearer)
            if status == 200: break
            time.sleep(.04)
        else: raise AssertionError(('controller reconnect',status,raw))
        ticket = json.loads(raw)['data']
        connected = WebSocket(website_port,{'Origin':'http://127.0.0.1:12345'},path=ticket['path'],
            protocol=ticket['protocol']+', xadmin.ticket.'+ticket['ticket']); clients.append(connected)
        assert json.loads(connected.recv()[1])['type'] == 'ready'
        return connected,json.loads(connected.recv()[1])

    connected,again = reconnect(); assert again['runtime_id'] == hello['runtime_id']
    resumed = Rpc(connected,again,rpc.client); resumed.sequence = rpc.sequence
    result,_ = resumed.call('POST','/api/v1/test-bridge-write',headers=lost['headers'],request_id=lost['id'],sequence=lost['sequence'])
    assert result['type'] == 'receipt' and result['state'] in ('uncertain','done'),result
    assert resumed.json('GET','/api/v1/test-bridge-write')[0]['data']['writes'] == 2,'lost response must not repeat write'
    # Temporary platform identities intentionally turn permission off at Unit.
    # Persistent-identity platforms additionally exercise real runtime restore.
    if not app()['persistent']:
        print('PASS real remote routes, nonce/digest/scope/view guards, bounded binary/HEAD, and lost-write receipt/reconnect (temporary platform identity)')
        return connected
    # Actual Unit recreates the runtime. Old admitted writes cannot execute.
    app(path='/test-remote-lifecycle'); connected.close(); clients.remove(connected)
    connected,new_hello = reconnect(); assert new_hello['runtime_id'] != hello['runtime_id']
    restarted = Rpc(connected,new_hello,rpc.client)
    result,_ = restarted.call('POST','/api/v1/test-bridge-write',headers=lost['headers'],
        request_id=lost['id'],sequence=lost['sequence'],runtime=hello['runtime_id'])
    assert result['code'] == 'runtime_changed'
    assert restarted.json('GET','/api/v1/test-bridge-write')[0]['data']['writes'] == 2
    print('PASS real remote project creation/target nonce, payload digest and offset, scope/headers/view role, bounded binary download with independent reads, HEAD, lost-write receipt/reconnect and runtime change')
    return connected


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',type=Path,default=ROOT/'.build/host/xs.exe')
    parser.add_argument('--website-host',type=Path,default=WEBSITE_HOST)
    args = parser.parse_args()
    run(args.host.resolve(),args.website_host.resolve(),exercise,HOOK,ROUTES)
