"""Real mdo message transport over pinned native xrudp, loopback only."""
from pathlib import Path
import argparse
import json
import subprocess
import tempfile
import socket
import os
import sys

from runtime_sources import copy_app_source

SOURCE = r'''
#include <xsbase.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "src/storage/home.c"
#include "src/remote/websocket.c"
#if !defined(XS_USE_XRUDP)
#error this regression requires the real xrudp-enabled host
#endif
#define CHECK(c) do { if (!(c)) { const xerror* e=xrtGetError(); printf("FAIL rudp line=%d expression=%s error=%s\n",__LINE__,#c,e?xrtErrorMessage(e):"none"); fflush(stdout); exit(5); } } while (0)
typedef struct Pair {
    MdoRudpConfig Config[2]; MdoRudpTransport* Raw[2]; MdoRemoteSocket* Socket[2];
} Pair;
static unsigned Cases;
static void Setup(Pair* P, xnetfamily Family, bool BadPin, bool BadContext)
{
    memset(P,0,sizeof(*P));
    unsigned char Public[2][32];
    for (unsigned i=0;i<2;i++) {
        MdoRudpConfig* C=&P->Config[i];
        for (unsigned j=0;j<32;j++) C->Private[j]=(unsigned char)(21+i*71+j);
        CHECK(xrtX25519Public(C->Private,Public[i]));
        memset(C->Scope,31+Cases,16); memset(C->Binding,65+Cases,32);
        C->MessageLimit=MDO_RUDP_MESSAGE_MAX; C->Listen=i==0;
        CHECK(xrtNetAddrLoopback(&C->Local,Family,0));
    }
    memcpy(P->Config[0].PeerPublic,Public[1],32); memcpy(P->Config[1].PeerPublic,Public[0],32);
    if (BadPin) P->Config[1].PeerPublic[0]^=1;
    if (BadContext) P->Config[1].Binding[0]^=1;
    for (unsigned i=0;i<2;i++) { P->Raw[i]=MdoRudpBind(&P->Config[i],NULL); CHECK(P->Raw[i]); }
    xrtSecureZero(P->Config[0].Private,32); xrtSecureZero(P->Config[1].Private,32);
}
static void Cleanup(Pair* P)
{
    for (unsigned i=0;i<2;i++) {
        if (P->Socket[i]) MdoRemoteSocketDestroy(P->Socket[i]); else MdoRudpDestroy(P->Raw[i]);
        P->Socket[i]=NULL; P->Raw[i]=NULL;
    }
    CHECK(xrtAtomic32Load(&g_MdoRudpTransports,XMEMORY_ACQUIRE)==0);
    ++Cases;
}
static void Connect(Pair* P)
{
    MdoRudpInfo Info; CHECK(MdoRudpGetInfo(P->Raw[0],&Info));
    CHECK(MdoRudpConnect(P->Raw[1],&Info.Local,3000));
    double End=xrtTimer()+2;
    while (!MdoRudpConnected(P->Raw[0])) { CHECK(xrtTimer()<End); xrtSleep(1); }
    for (unsigned i=0;i<2;i++) {
        CHECK(MdoRudpGetInfo(P->Raw[i],&Info));
        CHECK(Info.Confirmed && !Info.Closed && Info.NativeReservedBytes<=MDO_RUDP_NATIVE_BYTES);
        P->Socket[i]=MdoRemoteSocketAdoptRudp(P->Raw[i]); CHECK(P->Socket[i]);
    }
}
typedef struct Receiver { MdoRudpTransport* Raw; unsigned Count; bool Inside, Reject; } Receiver;
static bool Receive(bool Binary, xbytesview Bytes, void* Data)
{
    Receiver* R=Data;
    CHECK(R->Inside && xrtNetEngineCurrent(R->Raw->Engine)==NULL);
    if (R->Reject) return false;
    if (R->Count==0) {
        static const char Text[]="hello \xe2\x82\xac";
        CHECK(!Binary && Bytes.Size==sizeof(Text)-1 && !memcmp(Bytes.Data,Text,sizeof(Text)-1));
    } else if (R->Count==1) CHECK(Binary && Bytes.Size==0);
    else if (R->Count==2) {
        CHECK(Binary && Bytes.Size==MDO_RUDP_MESSAGE_MAX);
        for (size_t i=0;i<Bytes.Size;i++) CHECK(Bytes.Data[i]==(unsigned char)(i*29u));
    } else CHECK(R->Count==3 && !Binary && Bytes.Size==0);
    ++R->Count; return true;
}
static bool ReceiveAny(bool Binary, xbytesview Bytes, void* Data)
{
    (void)Binary; (void)Bytes; ++*(unsigned*)Data; return true;
}
static void Payloads(xnetfamily Family)
{
    Pair P; Setup(&P,Family,false,false); Connect(&P);
    unsigned char* Large=xrtMalloc(MDO_RUDP_MESSAGE_MAX); CHECK(Large);
    for (size_t i=0;i<MDO_RUDP_MESSAGE_MAX;i++) Large[i]=(unsigned char)(i*29u);
    Receiver R[2]={{P.Raw[0],0,false,false},{P.Raw[1],0,false,false}};
    for (unsigned i=0;i<2;i++) {
        static const char Text[]="hello \xe2\x82\xac";
        CHECK(MdoRemoteSocketSend(P.Socket[i],false,(xbytesview){(const uint8*)Text,sizeof(Text)-1}));
        CHECK(MdoRemoteSocketSend(P.Socket[i],true,(xbytesview){NULL,0}));
        CHECK(MdoRemoteSocketSend(P.Socket[i],true,(xbytesview){Large,MDO_RUDP_MESSAGE_MAX}));
        CHECK(MdoRemoteSocketSend(P.Socket[i],false,(xbytesview){NULL,0}));
    }
    double End=xrtTimer()+8;
    while (R[0].Count<4 || R[1].Count<4) {
        CHECK(xrtTimer()<End);
        for (unsigned i=0;i<2;i++) { R[i].Inside=true; CHECK(MdoRemoteSocketPoll(P.Socket[i],Receive,&R[i])); R[i].Inside=false; }
        xrtSleep(1);
    }
    xrtFree(Large); Cleanup(&P);
    printf("PASS xrudp message adapter family=%u ordered text/binary/empty/max-size and owner-only callbacks\n",(unsigned)Family); fflush(stdout);
}
typedef struct RawWrite {
    MdoRudpTransport* R; const unsigned char* Bytes; size_t Size; bool Fin; xatomic32 Done;
} RawWrite;
static void RawWorker(xnetworker* Worker, ptr Data)
{
    RawWrite* W=Data; size_t Written=0; (void)Worker;
    if (W->Size) CHECK(xrtRudpPeerWrite(W->R->Peer,W->Bytes,W->Size,&Written)==XRUDP_OK && Written==W->Size);
    if (W->Fin) CHECK(xrtRudpPeerFinish(W->R->Peer)==XRUDP_OK);
    xrtAtomic32Store(&W->Done,1,XMEMORY_RELEASE);
}
static void Raw(MdoRudpTransport* R, const unsigned char* Bytes, size_t Size, bool Fin)
{
    RawWrite W; memset(&W,0,sizeof(W)); W.R=R; W.Bytes=Bytes; W.Size=Size; W.Fin=Fin;
    CHECK(xrtNetEnginePost(R->Engine,0,RawWorker,&W));
    double End=xrtTimer()+2;
    while (!xrtAtomic32Load(&W.Done,XMEMORY_ACQUIRE)) { CHECK(xrtTimer()<End); xrtSleep(1); }
}
static void Framing(unsigned Kind)
{
    Pair P; Setup(&P,XNET_FAMILY_IPV4,false,false); Connect(&P);
    unsigned char Wire[12]={0,0,0,0,1,0,0,0,0xff}; size_t Size=8;
    uint16 Code=1002; bool Fin=false; unsigned Received=0;
    if (Kind==0) { Wire[0]=1; Code=1009; }
    if (Kind==1) Wire[4]=3;
    if (Kind==2) Wire[5]=1;
    if (Kind==3) { Wire[3]=1; Size=9; Code=1007; }
    if (Kind==4) { Size=4; Fin=true; }
    if (Kind==5) { Wire[3]=4; Wire[4]=2; Size=9; Fin=true; }
    if (Kind==6) { Code=1000; Fin=true; }
    /* An independent eight-byte producer bypasses the adapter encoder. */
    Raw(P.Raw[1],Wire,Size,Fin);
    double End=xrtTimer()+3;
    while (MdoRemoteSocketPoll(P.Socket[0],ReceiveAny,&Received)) { CHECK(xrtTimer()<End); xrtSleep(1); }
    CHECK(Received==(Kind==6 ? 1u:0u));
    CHECK(MdoRemoteSocketCloseCode(P.Socket[0])==Code);
    CHECK(MdoRemoteSocketPeerClosed(P.Socket[0])==(Kind==6));
    Cleanup(&P);
}
static void Identity(void)
{
    for (unsigned Which=0;Which<2;Which++) {
        Pair P; Setup(&P,XNET_FAMILY_IPV4,Which==0,Which==1);
        MdoRudpInfo Info; CHECK(MdoRudpGetInfo(P.Raw[0],&Info));
        CHECK(!MdoRudpConnect(P.Raw[1],&Info.Local,250));
        CHECK(MdoRudpGetInfo(P.Raw[0],&Info) && !Info.Confirmed && !Info.Closed && Info.ReceiveRecords==0);
        Cleanup(&P);
    }
    printf("PASS mismatched peer pin/context admit no application data and preserve the listener\n");
}
static void Rejection(void)
{
    Pair P; Setup(&P,XNET_FAMILY_IPV4,false,false); Connect(&P);
    Receiver R={P.Raw[0],0,true,true}; const char Text[]="reject";
    CHECK(MdoRemoteSocketSend(P.Socket[1],false,(xbytesview){(const uint8*)Text,sizeof(Text)-1}));
    double End=xrtTimer()+2;
    while (MdoRemoteSocketPoll(P.Socket[0],Receive,&R)) { CHECK(xrtTimer()<End); xrtSleep(1); }
    CHECK(MdoRemoteSocketCloseCode(P.Socket[0])==1008 && !MdoRemoteSocketPeerClosed(P.Socket[0])); Cleanup(&P);
}
static int32 CancelSoon(ptr Data) { xrtSleep(20); xrtCancelRequest(Data); return 0; }
static void Cancellation(void)
{
    Pair P; Setup(&P,XNET_FAMILY_IPV4,false,false);
    xcancel* Cancel=xrtCancelCreate(); CHECK(Cancel);
    /* Rebind a non-listening black hole and the initiator with caller cancel. */
    MdoRudpDestroy(P.Raw[1]); P.Raw[1]=NULL;
    for (unsigned j=0;j<32;j++) P.Config[1].Private[j]=(unsigned char)(21+71+j);
    P.Raw[1]=MdoRudpBind(&P.Config[1],Cancel); CHECK(P.Raw[1]);
    MdoRudpInfo Info; CHECK(MdoRudpGetInfo(P.Raw[1],&Info));
    xthread* Thread=xrtThreadCreate(CancelSoon,Cancel,0); CHECK(Thread);
    double Start=xrtTimer(); CHECK(!MdoRudpConnect(P.Raw[1],&Info.Local,3000)); CHECK(xrtTimer()-Start<1);
    CHECK(xrtThreadWait(Thread)==0); xrtThreadDestroy(Thread); Cleanup(&P); xrtCancelDestroy(Cancel);
}
static void Backpressure(void)
{
    Pair P; Setup(&P,XNET_FAMILY_IPV4,false,false); Connect(&P);
    unsigned char* Bytes=xrtCalloc(1,MDO_RUDP_MESSAGE_MAX); CHECK(Bytes);
    bool Failed=false; double Start=xrtTimer();
    for (unsigned i=0;i<16;i++) if (!MdoRemoteSocketSend(P.Socket[1],true,(xbytesview){Bytes,MDO_RUDP_MESSAGE_MAX})) { Failed=true; break; }
    CHECK(Failed && xrtTimer()-Start<5);
    for (unsigned i=0;i<2;i++) {
        MdoRudpInfo Info; CHECK(MdoRudpGetInfo(P.Raw[i],&Info));
        CHECK(Info.SendBytes<=MDO_RUDP_QUEUE_BYTES && Info.ReceiveBytes<=MDO_RUDP_QUEUE_BYTES);
        CHECK(Info.SendRecords<=MDO_RUDP_QUEUE_RECORDS && Info.ReceiveRecords<=MDO_RUDP_QUEUE_RECORDS);
    }
    xrtFree(Bytes); Cleanup(&P); printf("PASS receive backpressure bounds both queues and send wait\n");
}
static void ResourceLimit(void)
{
    Pair P; Setup(&P,XNET_FAMILY_IPV4,false,false);
    MdoRudpTransport* Others[MDO_RUDP_TRANSPORTS_MAX-2]={0};
    for (unsigned j=0;j<32;j++) P.Config[1].Private[j]=(unsigned char)(21+71+j);
    for (unsigned i=0;i<MDO_RUDP_TRANSPORTS_MAX-2;i++) { Others[i]=MdoRudpBind(&P.Config[1],NULL); CHECK(Others[i]); }
    CHECK(!MdoRudpBind(&P.Config[1],NULL));
    for (unsigned i=0;i<MDO_RUDP_TRANSPORTS_MAX-2;i++) MdoRudpDestroy(Others[i]);
    Cleanup(&P);
}
typedef struct SendGate { xatomic32 Entered, Release; } SendGate;
static void HoldSend(xnetworker* Worker, ptr Data)
{
    (void)Worker; SendGate* G=Data; double End=xrtTimer()+2;
    xrtAtomic32Store(&G->Entered,1,XMEMORY_RELEASE);
    while (!xrtAtomic32Load(&G->Release,XMEMORY_ACQUIRE)) { CHECK(xrtTimer()<End); xrtSleep(1); }
}
static void CancelQueuedSend(void)
{
    Pair P; Setup(&P,XNET_FAMILY_IPV4,false,false); xcancel* Cancel=xrtCancelCreate(); CHECK(Cancel);
    MdoRudpDestroy(P.Raw[1]);
    for (unsigned j=0;j<32;++j) P.Config[1].Private[j]=(unsigned char)(21+71+j);
    P.Raw[1]=MdoRudpBind(&P.Config[1],Cancel); CHECK(P.Raw[1]); xrtSecureZero(P.Config[1].Private,32); Connect(&P);
    SendGate G={0}; CHECK(xrtNetEnginePost(P.Raw[1]->Engine,0,HoldSend,&G)); double End=xrtTimer()+1;
    while (!xrtAtomic32Load(&G.Entered,XMEMORY_ACQUIRE)) { CHECK(xrtTimer()<End); xrtSleep(1); }
    CHECK(MdoRemoteSocketSend(P.Socket[1],false,(xbytesview){(const uint8*)"queued",6}));
    xrtCancelRequest(Cancel); xrtAtomic32Store(&G.Release,1,XMEMORY_RELEASE);
    unsigned Count=0; End=xrtTimer()+.15;
    /* Only poll the receiver. Cancellation must stop the sender's pending
     * worker command even if that owner never calls another API. */
    while (xrtTimer()<End) { (void)MdoRemoteSocketPoll(P.Socket[0],ReceiveAny,&Count); xrtSleep(1); }
    CHECK(!Count); Cleanup(&P); xrtCancelDestroy(Cancel);
    printf("PASS cancelled parent stops queued application sends without sender polling\n");
}
void ServiceInit(XS_HostInfo* Host)
{
    (void)Host;
    Payloads(XNET_FAMILY_IPV4); Payloads(XNET_FAMILY_IPV6);
    for (unsigned i=0;i<7;i++) Framing(i);
    Identity(); Rejection(); Cancellation(); Backpressure(); ResourceLimit(); CancelQueuedSend();
    for (unsigned i=0;i<4;i++) { Pair P; Setup(&P,XNET_FAMILY_IPV4,false,false); Connect(&P); Cleanup(&P); }
    printf("PASS mdo embedded xrudp: %u pair lifecycles, close barriers and global admission\n",Cases); fflush(stdout); exit(0);
}
'''

def native_lower(sdk: Path, cc: str, fixture=SOURCE):
    """Instrument the actual lower adapter and libraries, using the same cases.

    The --host path checks TCC exports and the socket facade. This mode calls
    the lower adapter directly so sanitizers cover worker callbacks too.
    """
    if sys.platform != 'linux':
        raise SystemExit('--native-lower requires Linux ASan/UBSan')
    root = Path(__file__).resolve().parents[1]
    preamble = r'''
#define XRT_IMPLEMENTATION
#include <xrt.h>
#include "app/include/mdo/time.h"
#include "app/src/remote/rudp.inc.c"
typedef MdoRudpTransport MdoRemoteSocket;
#define MdoRemoteSocketSend MdoRudpSend
#define MdoRemoteSocketPoll MdoRudpPoll
#define MdoRemoteSocketDestroy MdoRudpDestroy
static MdoRemoteSocket* MdoRemoteSocketAdoptRudp(MdoRudpTransport* R) { return R; }
static uint16 MdoRemoteSocketCloseCode(MdoRemoteSocket* R) { MdoRudpInfo I; MdoRudpGetInfo(R,&I); return I.CloseCode; }
static bool MdoRemoteSocketPeerClosed(MdoRemoteSocket* R) { MdoRudpInfo I; MdoRudpGetInfo(R,&I); return I.PeerClosed; }
'''
    source = fixture.replace('#include "src/storage/home.c"\n#include "src/remote/websocket.c"', preamble)
    source = source.replace('void ServiceInit(XS_HostInfo* Host)', 'int main(void)').replace('    (void)Host;\n', '')
    sources = sorted((sdk / 'lib/xrudp/src').rglob('*.c'))
    if len(sources) != 20:
        raise AssertionError('unexpected native xrudp source set')
    with tempfile.TemporaryDirectory(prefix='mdo-rudp-asan-') as raw:
        base = Path(raw); unit = base / 'test.c'; binary = base / 'test'
        unit.write_text(source, encoding='utf-8')
        includes = [root, sdk/'lib', sdk/'src/sdk', sdk/'lib/xllm', sdk/'lib/xrudp/include', sdk/'lib/xrtshim']
        command = [cc, '-std=gnu11', '-g', '-O1', '-fno-omit-frame-pointer',
            '-fsanitize=address,undefined', '-D_GNU_SOURCE=1', '-DXRT_MODULE_ALL=1', '-DXRUDP_MODULE_ALL=1', '-DXS_USE_XRUDP=1']
        for include in includes:
            command.extend(['-I', str(include)])
        subprocess.run([*command, str(unit), *map(str,sources), '-o', str(binary), '-pthread', '-lm', '-ldl'], check=True)
        env = {**os.environ, 'ASAN_OPTIONS': 'detect_leaks=1:halt_on_error=1', 'UBSAN_OPTIONS': 'halt_on_error=1:print_stacktrace=1'}
        subprocess.run([str(binary)], env=env, check=True, timeout=90)
    return 0


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    mode=parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--host',type=Path)
    mode.add_argument('--native-lower',type=Path,help='Linux: run native adapter under ASan/UBSan')
    parser.add_argument('--cc',default='gcc')
    args=parser.parse_args()
    if args.native_lower:
        return native_lower(args.native_lower.resolve(),args.cc)
    with tempfile.TemporaryDirectory(prefix='mdo-rudp-') as raw:
        root=Path(raw); (root/'web').mkdir()
        copy_app_source('src/storage/home.c',root)
        copy_app_source('src/remote/websocket.c',root)
        (root/'main.c').write_text(SOURCE,encoding='utf-8')
        with socket.socket() as listener:
            listener.bind(('127.0.0.1',0)); port=listener.getsockname()[1]
        config={'services':[{'class':'http','name':'rudp-test','ip':'127.0.0.1','port':port,
            'host_default':{'path':'web','devlang':'c','devfile':'main.c'}}]}
        (root/'xs.json').write_text(json.dumps(config),encoding='utf-8')
        result=subprocess.run([str(args.host.resolve()),str(root/'xs.json')],cwd=root,
            capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=90)
        output=result.stdout+result.stderr
        if result.returncode or 'PASS mdo embedded xrudp:' not in output:
            print(output);return 1
        print('\n'.join(line for line in result.stdout.splitlines() if line.startswith('PASS ')))
    return 0

if __name__=='__main__':raise SystemExit(main())
