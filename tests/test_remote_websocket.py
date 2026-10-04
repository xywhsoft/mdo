"""Native outbound WS/WSS through real xs/TCC and bounded local peers.

Uses a private test CA with certificate and hostname verification enabled.
No public service credentials, extra WebSocket package, or load test.
"""
from __future__ import annotations

import argparse
import base64
from datetime import datetime, timedelta, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import socket
import socketserver
import ssl
import struct
import subprocess
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[1]

FIXTURE = r'''
#include <xsbase.h>
#include <stdio.h>
#include <string.h>
#include "remote/websocket.c"
static xthread *Worker, *Canceller;
static xcancel* Stop;
static unsigned Messages;
static bool Valid = true;
static bool Message(bool binary, xbytesview data, void* unused) {
    (void)unused;
    const unsigned char raw[] = {0,255,'B'};
    const char text[] = "hello \xe2\x82\xac";
    Valid = Valid && (binary ? data.Size == sizeof(raw) && !memcmp(data.Data,raw,sizeof(raw)) :
        data.Size == sizeof(text)-1 && !memcmp(data.Data,text,sizeof(text)-1));
    ++Messages; return true;
}
static int32 CancelSoon(void* unused) {
    (void)unused; xrtSleep(250); xrtCancelRequest(Stop); return 0;
}
static int32 Run(void* data) {
    XS_ServerInfo* server = (XS_ServerInfo*)data;
    MdoRemoteNet net = {0}; xx509store* store = xrtX509StoreCreate();
    bool initialized = store && xrtX509StoreAddFile(store,TEST_CA,NULL) &&
        MdoRemoteNetInit(&net,server->Engine,store);
    xrtX509StoreFree(store);
    MdoRemoteSocketConfig config = {TEST_HOST,TEST_PORT,TEST_SECURE,"/connect",TEST_ORIGIN,
        "test.remote.v1, test.proof.runtime-only","test.remote.v1",256u};
    uint16 status = 0; uint16 closed = 0; bool opened = false, cancelled = false;
    uint64 start = xrtClock();
    if (TEST_CANCEL_OPEN) Canceller = xrtThreadCreate(CancelSoon,NULL,0);
    MdoRemoteSocket* socket = initialized ? MdoRemoteSocketOpen(&net,&config,Stop,&status) : NULL;
    if (socket) {
        opened = true;
        const unsigned char binary[] = {0,255,'B'};
        Valid = Valid && MdoRemoteSocketSend(socket,false,(xbytesview){(const uint8*)"client text",11u}) &&
            MdoRemoteSocketSend(socket,true,(xbytesview){binary,sizeof(binary)});
        if (TEST_CANCEL_POLL) xrtCancelRequest(Stop);
        uint64 until = xrtDeadlineAfter(4000000u);
        while (!xrtDeadlineExpired(until) && MdoRemoteSocketPoll(socket,Message,NULL)) xrtSleep(5);
        closed = MdoRemoteSocketCloseCode(socket);
        cancelled = xrtCancelRequested(Stop);
    }
    MdoRemoteSocketDestroy(socket);
    if (Canceller) { xrtThreadWait(Canceller); xrtThreadDestroy(Canceller); Canceller = NULL; }
    MdoRemoteNetUnit(&net); xsServerRelease(server);
    FILE* file = fopen(TEST_RESULT,"wb");
    if (file) { fprintf(file,"{\"initialized\":%s,\"opened\":%s,\"status\":%u,\"valid\":%s,"
        "\"messages\":%u,\"close_code\":%u,\"cancelled\":%s,\"elapsed_ms\":%llu}",
        initialized ? "true":"false",opened ? "true":"false",status,Valid ? "true":"false",
        Messages,closed,cancelled ? "true":"false",(unsigned long long)((xrtClock()-start)/1000u)); fclose(file); }
    return 0;
}
void ServiceInit(XS_HostInfo* host) {
    Stop = xrtCancelCreate(); XS_ServerInfo* server = xsServerRetain(host->Server);
    Worker = Stop && server ? xrtThreadCreate(Run,server,0) : NULL;
    if (!Worker) xsServerRelease(server);
}
void ServiceUnit(XS_HostInfo* host) {
    (void)host; xrtCancelRequest(Stop);
    if (Worker) { xrtThreadWait(Worker); xrtThreadDestroy(Worker); }
    Worker = NULL; xrtCancelDestroy(Stop); Stop = NULL;
}
XS_RequestResult RequestProc(XS_HttpReq* req) { (void)req; return XS_FALLBACK; }
'''


def certificates(directory):
    from cryptography import x509
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import rsa
    from cryptography.x509.oid import NameOID, ExtendedKeyUsageOID
    import ipaddress
    now = datetime.now(timezone.utc)
    key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, 'mdo remote test root')])
    ca = (x509.CertificateBuilder().subject_name(name).issuer_name(name).public_key(key.public_key())
          .serial_number(x509.random_serial_number()).not_valid_before(now-timedelta(minutes=1))
          .not_valid_after(now+timedelta(days=1)).add_extension(x509.BasicConstraints(ca=True,path_length=0), True)
          .add_extension(x509.KeyUsage(False,False,False,False,False,True,True,False,False), True)
          .add_extension(x509.SubjectKeyIdentifier.from_public_key(key.public_key()), False)
          .sign(key,hashes.SHA256()))
    leafkey = rsa.generate_private_key(public_exponent=65537,key_size=2048)
    leaf = (x509.CertificateBuilder().subject_name(x509.Name([x509.NameAttribute(NameOID.COMMON_NAME,'localhost')]))
            .issuer_name(name).public_key(leafkey.public_key()).serial_number(x509.random_serial_number())
            .not_valid_before(now-timedelta(minutes=1)).not_valid_after(now+timedelta(days=1))
            .add_extension(x509.BasicConstraints(ca=False,path_length=None),True)
            .add_extension(x509.KeyUsage(True,False,True,False,False,False,False,False,False),True)
            .add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.SERVER_AUTH]),False)
            .add_extension(x509.SubjectAlternativeName([x509.DNSName('localhost'),
                x509.IPAddress(ipaddress.ip_address('127.0.0.1'))]),False)
            .add_extension(x509.SubjectKeyIdentifier.from_public_key(leafkey.public_key()),False)
            .add_extension(x509.AuthorityKeyIdentifier.from_issuer_public_key(key.public_key()),False)
            .sign(key,hashes.SHA256()))
    (directory/'ca.pem').write_bytes(ca.public_bytes(serialization.Encoding.PEM))
    (directory/'leaf.pem').write_bytes(leaf.public_bytes(serialization.Encoding.PEM))
    (directory/'leaf.key').write_bytes(leafkey.private_bytes(serialization.Encoding.PEM,
        serialization.PrivateFormat.PKCS8,serialization.NoEncryption()))


def frame(opcode, data=b'', *, final=True, masked=False):
    length = len(data)
    head = bytes([opcode | (128 if final else 0), (128 if masked else 0) |
                  (length if length < 126 else 126 if length < 65536 else 127)])
    if length >= 126: head += struct.pack('!H' if length < 65536 else '!Q',length)
    if masked:
        mask = os.urandom(4); head += mask
        data = bytes(byte ^ mask[index % 4] for index,byte in enumerate(data))
    return head+data


class Peer(socketserver.BaseRequestHandler):
    def handle(self):
        try:
            connection = self.request
            if self.server.tls: connection = self.server.tls.wrap_socket(connection,server_side=True)
            connection.settimeout(5)
            self.connection, self.pending = connection, b''
            while b'\r\n\r\n' not in self.pending:
                data = connection.recv(4096)
                if not data: return
                self.pending += data
            header,self.pending = self.pending.split(b'\r\n\r\n',1)
            lines = header.decode().split('\r\n')
            fields = dict(line.split(': ',1) for line in lines[1:])
            assert lines[0] == 'GET /connect HTTP/1.1'
            assert fields['Origin'] == self.server.origin
            assert fields['Sec-WebSocket-Protocol'] == 'test.remote.v1, test.proof.runtime-only'
            assert 'Authorization' not in fields
            mode = self.server.mode
            if mode == 'cancel-open':
                assert not connection.recv(1), 'cancelled handshake must close'
                return
            accept = base64.b64encode(hashlib.sha1((fields['Sec-WebSocket-Key']+
                '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest()).decode()
            protocol = 'test.remote.v1'
            if mode == 'bad-accept': accept = 'unrelated-accept'
            if mode == 'bad-protocol': protocol = 'not-offered.v1'
            if mode == 'missing-protocol': protocol = ''
            response = ('HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n'
                        f'Sec-WebSocket-Accept: {accept}\r\n' +
                        (f'Sec-WebSocket-Protocol: {protocol}\r\n' if protocol else '')+'\r\n').encode()
            # Coalesce handshake and part of a fragmented UTF-8 message.
            if mode == 'echo': response += frame(1,b'hello \xe2',final=False)
            connection.sendall(response)
            if mode in ('bad-accept','bad-protocol','missing-protocol'):
                assert not connection.recv(1), 'invalid upgrade must be dropped'
                return
            assert self.recv() == (1,b'client text')
            assert self.recv() == (2,b'\0\xffB')
            if mode == 'cancel-poll':
                assert not connection.recv(1)
                return
            if mode == 'echo':
                connection.sendall(frame(9,b'probe')+frame(0,b'\x82\xac')+frame(2,b'\0\xffB'))
                assert self.recv() == (10,b'probe')
                connection.sendall(frame(8,struct.pack('!H',1000)))
                assert self.recv() == (8,struct.pack('!H',1000))
            else:
                if mode == 'invalid-utf8': wire,code = frame(1,b'\xff'),1007
                elif mode == 'masked-server': wire,code = frame(1,b'hello',masked=True),1002
                elif mode == 'oversize': wire,code = frame(2,b'x'*257),1009
                elif mode == 'fragment-oversize': wire,code = frame(2,b'x'*200,final=False)+frame(0,b'y'*100),1009
                else: raise AssertionError(mode)
                connection.sendall(wire)
                opcode,payload = self.recv()
                assert opcode == 8 and struct.unpack('!H',payload[:2])[0] == code, (mode,opcode,payload)
            self.server.success = True
        except (ssl.SSLError,ConnectionResetError,EOFError) as exc:
            if self.server.mode not in ('untrusted','bad-host','bad-accept','bad-protocol',
                                        'missing-protocol','cancel-open','cancel-poll'):
                self.server.errors.append(repr(exc))
        except Exception as exc:
            self.server.errors.append(repr(exc))

    def take(self, size):
        while len(self.pending) < size:
            data = self.connection.recv(4096)
            if not data: raise EOFError('peer closed')
            self.pending += data
        out,self.pending = self.pending[:size],self.pending[size:]
        return out

    def recv(self):
        first,length = self.take(2)
        assert length & 128, 'native client must mask every frame'
        length &= 127
        if length == 126: length = struct.unpack('!H',self.take(2))[0]
        if length == 127: length = struct.unpack('!Q',self.take(8))[0]
        mask = self.take(4)
        return first & 15, bytes(byte ^ mask[index%4] for index,byte in enumerate(self.take(length)))


class Server(socketserver.ThreadingTCPServer):
    daemon_threads = True
    allow_reuse_address = True


def run(host):
    (ROOT/'.build').mkdir(exist_ok=True)
    site = Path(tempfile.mkdtemp(prefix='remote-ws-',dir=ROOT/'.build'))
    certificates(site)
    shutil.copytree(ROOT/'app/src/remote',site/'remote')
    cases = [('echo',False),('echo',True),('invalid-utf8',True),('masked-server',False),
             ('oversize',False),('fragment-oversize',True),('bad-accept',True),('bad-protocol',False),
             ('missing-protocol',True),('untrusted',True),('bad-host',True),
             ('cancel-open',True),('cancel-poll',True)]
    for index,(mode,secure) in enumerate(cases):
        with Server(('127.0.0.2' if mode == 'bad-host' else '127.0.0.1',0),Peer) as peer:
            peer.mode,peer.errors,peer.success = mode,[],False
            peer.tls = None
            if secure:
                peer.tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
                peer.tls.load_cert_chain(site/'leaf.pem',site/'leaf.key')
            peer.origin = f'{"https" if secure else "http"}://127.0.0.1:{peer.server_address[1]}'
            thread = threading.Thread(target=peer.serve_forever,daemon=True); thread.start()
            result = site/f'result-{index}.json'
            trust = site/'ca.pem'
            if mode == 'untrusted':
                other = site/'other'; other.mkdir(exist_ok=True); certificates(other); trust = other/'ca.pem'
            # Both are loopback; only 127.0.0.1 appears in the certificate SAN.
            targethost = peer.server_address[0]
            defines = {'TEST_CA':str(trust).replace('\\','/'),'TEST_HOST':targethost,
                       'TEST_PORT':peer.server_address[1], 'TEST_SECURE':secure,
                       'TEST_ORIGIN':peer.origin,'TEST_RESULT':str(result).replace('\\','/'),
                       'TEST_CANCEL_OPEN':mode == 'cancel-open','TEST_CANCEL_POLL':mode == 'cancel-poll'}
            source = ''.join(f'#define {key} {json.dumps(value) if isinstance(value,str) else int(value)}\n'
                             for key,value in defines.items())+FIXTURE
            (site/'main.c').write_text(source,encoding='utf-8')
            with socket.socket() as reserve:
                reserve.bind(('127.0.0.1',0)); app_port = reserve.getsockname()[1]
            config = {'services':[{'name':'remote-test','class':'http','ip':'127.0.0.1','port':app_port,
                      'host_default':{'path':str(site),'devfile':'main.c','devlang':'c'}}]}
            (site/'xs.json').write_text(json.dumps(config))
            with (site/f'host-{index}.log').open('wb') as log:
                process = subprocess.Popen([str(host),str(site/'xs.json')],cwd=site,stdout=log,stderr=log)
                try:
                    for _ in range(200):
                        if result.exists(): break
                        if process.poll() is not None: raise AssertionError(f'host exited: {site}/host-{index}.log')
                        time.sleep(.05)
                    else: raise AssertionError(f'native client did not finish: {site}/host-{index}.log')
                    value = json.loads(result.read_text())
                    assert value['initialized'], value
                    assert not peer.errors,(mode,peer.errors)
                    if mode in ('bad-accept','bad-protocol','missing-protocol','untrusted','bad-host','cancel-open'):
                        assert not value['opened'],(mode,value)
                        if mode == 'cancel-open': assert value['elapsed_ms'] < 2000,value
                    elif mode == 'cancel-poll':
                        assert value['opened'] and value['cancelled'] and value['elapsed_ms'] < 2000,value
                    else:
                        assert value['opened'] and value['valid'],(mode,value)
                        assert value['close_code'] == {'echo':1000,'invalid-utf8':1007,'masked-server':1002,
                            'oversize':1009,'fragment-oversize':1009}[mode],(mode,value)
                        assert value['messages'] == (2 if mode == 'echo' else 0),value
                        assert peer.success,(mode,'peer close not acknowledged')
                    print(f'PASS native {"WSS" if secure else "WS"} {mode}')
                finally:
                    if process.poll() is None: process.terminate(); process.wait(timeout=15)
                    peer.shutdown(); thread.join(timeout=5)
    print('PASS outbound handshake binding, masked sends, fragmented UTF-8, binary, ping/close, TLS trust/name, cancellation and bounds')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',type=Path,default=ROOT/'.build/host/xs.exe')
    run(parser.parse_args().host.resolve())
