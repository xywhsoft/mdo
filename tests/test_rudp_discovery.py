"""Real mdo endpoint jobs against an independent, local-only UDP oracle."""
from pathlib import Path
import argparse
import binascii
import json
import select
import socket
import struct
import subprocess
import tempfile
import threading
import time

from runtime_sources import copy_app_source
from test_rudp_transport import native_lower

SOURCE = r'''
#include <xsbase.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "src/storage/home.c"
#include "src/remote/websocket.c"
#define CHECK(c) do { if (!(c)) { printf("FAIL probe line=%d %s\n",__LINE__,#c); fflush(stdout); exit(5); } } while(0)
static unsigned Cases;
typedef struct Pair { MdoRudpConfig C[2]; MdoRudpTransport* R[2]; } Pair;
static void SetupWithCancel(Pair* P, xnetfamily Family, xcancel* Cancel)
{
    unsigned char Public[2][32]; memset(P,0,sizeof(*P));
    for (unsigned i=0;i<2;++i) {
        MdoRudpConfig* C=&P->C[i];
        for (unsigned j=0;j<32;++j) C->Private[j]=(unsigned char)(11+i*65+j);
        CHECK(xrtX25519Public(C->Private,Public[i]));
        memset(C->Scope,15+Cases,16); memset(C->Binding,50+Cases,32);
        C->MessageLimit=MDO_RUDP_MESSAGE_MAX; C->Listen=i==0;
        CHECK(xrtNetAddrLoopback(&C->Local,Family,0));
    }
    for (unsigned i=0;i<2;++i) {
        memcpy(P->C[i].PeerPublic,Public[1-i],32);
        P->R[i]=MdoRudpStart(&P->C[i],i ? Cancel:NULL); CHECK(P->R[i]);
        xrtSecureZero(P->C[i].Private,32);
    }
    double End=xrtTimer()+2;
    for (unsigned i=0;i<2;++i) for (;;) {
        MdoRudpInfo Info; CHECK(MdoRudpGetInfo(P->R[i],&Info)); CHECK(!Info.Closed);
        if (Info.Opened) break;
        CHECK(xrtTimer()<End); xrtSleep(1);
    }
}
static void Setup(Pair* P, xnetfamily Family) { SetupWithCancel(P,Family,NULL); }
static void Cleanup(Pair* P)
{
    MdoRudpDestroy(P->R[0]); MdoRudpDestroy(P->R[1]);
    CHECK(xrtAtomic32Load(&g_MdoRudpTransports,XMEMORY_ACQUIRE)==0); ++Cases;
}
static MdoRudpInfo Wait(MdoRudpTransport* R, bool Warmup)
{
    MdoRudpInfo Info; double End=xrtTimer()+3;
    for (;;) {
        CHECK(MdoRudpGetInfo(R,&Info)); CHECK(!Info.Closed);
        if ((Warmup ? Info.Warmup : Info.Discovery).Complete) return Info;
        CHECK(xrtTimer()<End); xrtSleep(1);
    }
}
static unsigned Port(xnetfamily Family, unsigned Which)
{
    static const unsigned V4[]={TEST_GOOD_4,TEST_DROP_4,TEST_BAD_4,TEST_WARM_4,TEST_WARM2_4,TEST_LATE_4};
    static const unsigned V6[]={TEST_GOOD_6,TEST_DROP_6,TEST_BAD_6,TEST_WARM_6,TEST_WARM2_6,TEST_LATE_6};
    return (Family==XNET_FAMILY_IPV4 ? V4:V6)[Which];
}
static xnetaddr Address(xnetfamily Family, unsigned Which)
{
    xnetaddr A; CHECK(xrtNetAddrLoopback(&A,Family,(uint16)Port(Family,Which))); return A;
}
static void Mapping(xnetfamily Family)
{
    Pair P; Setup(&P,Family); xnetaddr Server=Address(Family,0);
    CHECK(!MdoRudpDiscover(P.R[0],&Server,999));
    CHECK(!MdoRudpDiscover(P.R[0],&Server,60001));
    CHECK(MdoRudpDiscover(P.R[0],&Server,1500));
    MdoRudpInfo I=Wait(P.R[0],false);
    CHECK(I.Discovery.NativeReason==XRUDP_DONE && !I.Confirmed && !I.ReceiveRecords);
    CHECK(xrtNetAddrEqual(&I.Local,&I.Discovery.Mapped));
    /* A late cancellation cannot replace a completed mapping. */
    MdoRudpCancelProbes(P.R[0]); xrtSleep(2); CHECK(MdoRudpGetInfo(P.R[0],&I));
    CHECK(I.Discovery.Complete && I.Discovery.NativeReason==XRUDP_DONE);
    Server=Address(Family,2); CHECK(MdoRudpDiscover(P.R[0],&Server,1000));
    CHECK(!MdoRudpDiscover(P.R[0],&Server,1000));
    /* The private deadline timer, not repeated owner polling, cancels noise. */
    xrtSleep(1300); CHECK(MdoRudpGetInfo(P.R[0],&I));
    CHECK(I.Discovery.Complete && I.Discovery.NativeReason==XRUDP_TIMEOUT && !I.Closed && !I.Confirmed);
    CHECK(!I.Discovery.Mapped.Family && !I.Discovery.Mapped.Port);
    Server=Address(Family,0); CHECK(MdoRudpDiscover(P.R[0],&Server,1500));
    CHECK(Wait(P.R[0],false).Discovery.NativeReason==XRUDP_DONE);
    Cleanup(&P); printf("PASS mapping family=%u exact source/transaction/fingerprint, same port, timeout and restart\n",(unsigned)Family);
}
static void Warmup(xnetfamily Family)
{
    Pair P; Setup(&P,Family); xnetaddr Candidates[2]={Address(Family,3),Address(Family,4)};
    CHECK(!MdoRudpWarmup(P.R[0],Candidates,0,1000)); CHECK(!MdoRudpWarmup(P.R[0],Candidates,9,1000));
    CHECK(!MdoRudpWarmup(P.R[0],Candidates,2,999));
    xnetaddr Invalid[2]={Candidates[0],Candidates[0]}; CHECK(!MdoRudpWarmup(P.R[0],Invalid,2,1000));
    CHECK(MdoRudpWarmup(P.R[0],Candidates,2,1000));
    CHECK(!MdoRudpWarmup(P.R[0],Candidates,2,1000));
    MdoRudpInfo I=Wait(P.R[0],true);
    CHECK(I.Warmup.NativeReason==XRUDP_TIMEOUT && I.Warmup.Sent>0 && I.Warmup.Sent<=6);
    CHECK(!I.Confirmed && !I.Closed);
    printf("WARM_SOURCE %u %u\n",(unsigned)Family,I.Local.Port); fflush(stdout);
    CHECK(MdoRudpWarmup(P.R[0],Candidates,2,5000)); MdoRudpCancelProbes(P.R[0]);
    CHECK(Wait(P.R[0],true).Warmup.NativeReason==XRUDP_CANCELLED);
    Cleanup(&P);
}
static bool Received(bool Binary, xbytesview Bytes, void* Data)
{
    CHECK(!Binary && Bytes.Size==8 && !memcmp(Bytes.Data,"survived",8)); *(bool*)Data=true; return true;
}
static void NatFilter(xnetfamily Family, bool Warm)
{
    Pair P; Setup(&P,Family); MdoRudpInfo I[2];
    unsigned Ports[2]={Family==XNET_FAMILY_IPV4 ? TEST_NATINFO0_4:TEST_NATINFO0_6,
        Family==XNET_FAMILY_IPV4 ? TEST_NATINFO1_4:TEST_NATINFO1_6};
    for (unsigned i=0;i<2;++i) {
        xnetaddr Server; CHECK(xrtNetAddrLoopback(&Server,Family,(uint16)Ports[i]));
        CHECK(MdoRudpDiscover(P.R[i],&Server,1500)); I[i]=Wait(P.R[i],false);
        if (I[i].Discovery.NativeReason!=XRUDP_DONE) printf("NAT mapping family=%u side=%u reason=%u local=%u mapped=%u\n",(unsigned)Family,i,I[i].Discovery.NativeReason,I[i].Local.Port,I[i].Discovery.Mapped.Port);
        CHECK(I[i].Discovery.NativeReason==XRUDP_DONE && I[i].Local.Port!=I[i].Discovery.Mapped.Port);
    }
    if (Warm) CHECK(MdoRudpWarmup(P.R[0],&I[1].Discovery.Mapped,1,5000));
    CHECK(MdoRudpDial(P.R[1],&I[0].Discovery.Mapped,Warm ? 2000:200));
    if (!Warm) {
        xrtSleep(350); CHECK(MdoRudpGetInfo(P.R[1],&I[1]));
        CHECK(I[1].Closed && I[1].NativeReason==XRUDP_TIMEOUT && !MdoRudpConnected(P.R[0]));
    } else {
        double End=xrtTimer()+3;
        while (!MdoRudpConnected(P.R[0]) || !MdoRudpConnected(P.R[1])) { CHECK(xrtTimer()<End); xrtSleep(1); }
        CHECK(Wait(P.R[0],true).Warmup.NativeReason==XRUDP_CANCELLED);
        for (unsigned i=0;i<2;++i) {
            CHECK(MdoRudpSend(P.R[i],false,(xbytesview){(const uint8*)"survived",8})); bool Got=false;
            while (!Got) { CHECK(xrtTimer()<End); CHECK(MdoRudpPoll(P.R[1-i],Received,&Got)); xrtSleep(1); }
        }
    }
    Cleanup(&P); printf("PASS NAT address/port filter family=%u %s\n",(unsigned)Family,Warm ? "warmup admits pinned encrypted bidirectional data":"dial without warmup times out");
}
static void Confirmation(xnetfamily Family)
{
    Pair P; Setup(&P,Family); xnetaddr Server=Address(Family,1), Candidate=Address(Family,3);
    CHECK(MdoRudpDiscover(P.R[0],&Server,5000)); CHECK(MdoRudpWarmup(P.R[0],&Candidate,1,5000));
    MdoRudpInfo I; CHECK(MdoRudpGetInfo(P.R[0],&I));
    double Start=xrtTimer(); CHECK(MdoRudpDial(P.R[1],&I.Local,2000)); CHECK(xrtTimer()-Start<.2);
    CHECK(!MdoRudpDial(P.R[1],&I.Local,2000));
    double End=xrtTimer()+3;
    while (!MdoRudpConnected(P.R[0]) || !MdoRudpConnected(P.R[1])) { CHECK(xrtTimer()<End); xrtSleep(1); }
    I=Wait(P.R[0],false); CHECK(I.Discovery.NativeReason==XRUDP_CANCELLED);
    I=Wait(P.R[0],true); CHECK(I.Warmup.NativeReason==XRUDP_CANCELLED);
    CHECK(MdoRudpSend(P.R[1],false,(xbytesview){(const uint8*)"survived",8})); bool Got=false;
    while (!Got) { CHECK(xrtTimer()<End); CHECK(MdoRudpPoll(P.R[0],Received,&Got)); xrtSleep(1); }
    Cleanup(&P); printf("PASS asynchronous confirmed family=%u cancels mapping/warmup without disrupting messages\n",(unsigned)Family);
}
typedef struct Gate { xatomic32 Entered, Release; } Gate;
static void Hold(xnetworker* Worker, ptr Data)
{
    (void)Worker; Gate* G=Data; xrtAtomic32Store(&G->Entered,1,XMEMORY_RELEASE);
    double End=xrtTimer()+3;
    while (!xrtAtomic32Load(&G->Release,XMEMORY_ACQUIRE)) { CHECK(xrtTimer()<End); xrtSleep(1); }
}
static void Noop(xnetworker* Worker, ptr Data) { (void)Worker; (void)Data; }
static bool CountMessage(bool Binary, xbytesview Bytes, void* Data)
{ (void)Binary; (void)Bytes; ++*(unsigned*)Data; return true; }
static void CancelQueuedSend(void)
{
    Pair P; xcancel* Cancel=xrtCancelCreate(); CHECK(Cancel); SetupWithCancel(&P,XNET_FAMILY_IPV4,Cancel);
    MdoRudpInfo I; CHECK(MdoRudpGetInfo(P.R[0],&I)); CHECK(MdoRudpConnect(P.R[1],&I.Local,2000));
    double End=xrtTimer()+1;
    while (!MdoRudpConnected(P.R[0])) { CHECK(xrtTimer()<End); xrtSleep(1); }
    Gate G={0}; CHECK(xrtNetEnginePost(P.R[1]->Engine,0,Hold,&G));
    while (!xrtAtomic32Load(&G.Entered,XMEMORY_ACQUIRE)) { CHECK(xrtTimer()<End); xrtSleep(1); }
    CHECK(MdoRudpSend(P.R[1],false,(xbytesview){(const uint8*)"survived",8}));
    xrtCancelRequest(Cancel); xrtAtomic32Store(&G.Release,1,XMEMORY_RELEASE);
    unsigned Count=0; End=xrtTimer()+.15;
    /* Never call a sender-side API to enforce cancellation: only the worker
     * can stop the already queued application frame in this interleaving. */
    while (xrtTimer()<End) { (void)MdoRudpPoll(P.R[0],CountMessage,&Count); xrtSleep(1); }
    CHECK(!Count); Cleanup(&P); xrtCancelDestroy(Cancel);
    printf("PASS parent cancellation prevents queued application send without sender polling\n");
}
static void QueuePressure(bool Expire)
{
    Pair P; Setup(&P,XNET_FAMILY_IPV4); Gate G={0}; MdoRudpInfo I;
    CHECK(xrtNetEnginePost(P.R[0]->Engine,0,Hold,&G)); double End=xrtTimer()+1;
    while (!xrtAtomic32Load(&G.Entered,XMEMORY_ACQUIRE)) { CHECK(xrtTimer()<End); xrtSleep(1); }
    unsigned Count=0; while (xrtNetEnginePost(P.R[0]->Engine,0,Noop,NULL)) { CHECK(++Count<=32); }
    CHECK(Count>0); xnetaddr Server=Address(XNET_FAMILY_IPV4,Expire ? 5:0);
    CHECK(MdoRudpDiscover(P.R[0],&Server,1000));
    if (Expire) xrtSleep(1100);
    xrtAtomic32Store(&G.Release,1,XMEMORY_RELEASE);
    I=Wait(P.R[0],false); CHECK(I.Discovery.NativeReason==(Expire ? XRUDP_TIMEOUT:XRUDP_DONE));
    Cleanup(&P); printf("PASS full worker queue %s without renewing mapping admission deadline\n",Expire?"expires":"recovers");
}
static void AsyncCloseAndDeadline(void)
{
    Pair P; Setup(&P,XNET_FAMILY_IPV4); MdoRudpInfo I; CHECK(MdoRudpGetInfo(P.R[1],&I));
    CHECK(MdoRudpDial(P.R[1],&I.Local,80)); xrtSleep(200);
    CHECK(MdoRudpGetInfo(P.R[1],&I) && I.Closed && I.NativeReason==XRUDP_TIMEOUT); Cleanup(&P);
    for (unsigned k=0;k<8;++k) {
        Setup(&P,XNET_FAMILY_IPV4); xnetaddr S=Address(XNET_FAMILY_IPV4,1), C=Address(XNET_FAMILY_IPV4,3);
        CHECK(MdoRudpDiscover(P.R[0],&S,5000)); CHECK(MdoRudpWarmup(P.R[0],&C,1,5000)); Cleanup(&P);
    }
    Setup(&P,XNET_FAMILY_IPV4); MdoRudpConfig C=P.C[1];
    for (unsigned j=0;j<32;++j) C.Private[j]=(unsigned char)(11+65+j);
    C.Local=Address(XNET_FAMILY_IPV4,0); MdoRudpTransport* Conflict=MdoRudpStart(&C,NULL); CHECK(Conflict);
    double End=xrtTimer()+2;
    do { CHECK(MdoRudpGetInfo(Conflict,&I)); CHECK(xrtTimer()<End); xrtSleep(1); } while (!I.Closed);
    CHECK(!I.Opened && !I.Confirmed); MdoRudpDestroy(Conflict); Cleanup(&P);
    for (unsigned k=0;k<8;++k) { C.Local.Port=0; Conflict=MdoRudpStart(&C,NULL); CHECK(Conflict); MdoRudpDestroy(Conflict); }
    CHECK(xrtAtomic32Load(&g_MdoRudpTransports,XMEMORY_ACQUIRE)==0); xrtSecureZero(C.Private,32);
}
typedef struct PeerAbort { MdoRudpTransport* R; xatomic32 Done; } PeerAbort;
static void AbortConfirmed(xnetworker* Worker, ptr Data)
{
    (void)Worker; PeerAbort* A=Data; (void)xrtRudpPeerAbort(A->R->Peer);
    xrtAtomic32Store(&A->Done,1,XMEMORY_RELEASE);
}
static void Candidates(xnetfamily Family)
{
    Pair P; Setup(&P,Family); MdoRudpInfo Before, Target, I;
    CHECK(MdoRudpGetInfo(P.R[0],&Target)); CHECK(MdoRudpGetInfo(P.R[1],&Before));
    xnetaddr Choices[3]={Address(Family,1),Target.Local,Address(Family,0)};
    CHECK(!MdoRudpDialCandidates(P.R[1],Choices,0,5000));
    CHECK(!MdoRudpDialCandidates(P.R[1],Choices,9,5000));
    CHECK(!MdoRudpDialCandidates(P.R[1],Choices,3,15001));
    CHECK(MdoRudpDialCandidates(P.R[1],Choices,3,7000)); double End=xrtTimer()+6;
    while (!MdoRudpConnected(P.R[0]) || !MdoRudpConnected(P.R[1])) { CHECK(xrtTimer()<End); xrtSleep(1); }
    CHECK(MdoRudpGetInfo(P.R[1],&I) && I.DialAttempts==2 && I.LastDialReason==XRUDP_TIMEOUT);
    CHECK(xrtNetAddrEqual(&I.Local,&Before.Local) && !memcmp(I.Public,Before.Public,32) && !memcmp(I.Scope,Before.Scope,16));
    CHECK(MdoRudpSend(P.R[1],false,(xbytesview){(const uint8*)"survived",8})); bool Got=false;
    while (!Got) { CHECK(xrtTimer()<End); CHECK(MdoRudpPoll(P.R[0],Received,&Got)); xrtSleep(1); }
    PeerAbort A={P.R[1],{0}}; CHECK(xrtNetEnginePost(P.R[1]->Engine,0,AbortConfirmed,&A));
    while (!xrtAtomic32Load(&A.Done,XMEMORY_ACQUIRE)) { CHECK(xrtTimer()<End); xrtSleep(1); }
    xrtSleep(100); CHECK(MdoRudpGetInfo(P.R[1],&I) && I.Closed && I.DialAttempts==2);
    Cleanup(&P); printf("PASS candidates family=%u retain mapped socket and pins, retry before admission only\n",(unsigned)Family);
}
static void CandidateDeadline(void)
{
    Pair P; Setup(&P,XNET_FAMILY_IPV4); xnetaddr Choices[2]={Address(XNET_FAMILY_IPV4,1),Address(XNET_FAMILY_IPV4,0)};
    double Start=xrtTimer(); CHECK(MdoRudpDialCandidates(P.R[1],Choices,2,100)); MdoRudpInfo I;
    do { CHECK(MdoRudpGetInfo(P.R[1],&I)); CHECK(xrtTimer()-Start<1); xrtSleep(1); } while (!I.Closed);
    CHECK(I.NativeReason==XRUDP_TIMEOUT && I.DialAttempts==1); Cleanup(&P);
}
void ServiceInit(XS_HostInfo* Host)
{
    (void)Host;
    Mapping(XNET_FAMILY_IPV4); Mapping(XNET_FAMILY_IPV6);
    Warmup(XNET_FAMILY_IPV4); Warmup(XNET_FAMILY_IPV6);
    Confirmation(XNET_FAMILY_IPV4); Confirmation(XNET_FAMILY_IPV6);
    NatFilter(XNET_FAMILY_IPV4,false); NatFilter(XNET_FAMILY_IPV4,true);
    NatFilter(XNET_FAMILY_IPV6,false); NatFilter(XNET_FAMILY_IPV6,true);
    QueuePressure(false); QueuePressure(true); CancelQueuedSend(); AsyncCloseAndDeadline();
    Candidates(XNET_FAMILY_IPV4); Candidates(XNET_FAMILY_IPV6); CandidateDeadline();
    printf("PASS mdo same-socket probes: %u pairs, async startup/dial, cancellation and close barriers\n",Cases); fflush(stdout); exit(0);
}
'''


class Oracles:
    def __enter__(self):
        self.sockets = {}; self.ports = {}; self.warm = []; self.late = 0
        self.blocked_families=set(); self.blocked_packets={4:0,6:0}
        self.stop = threading.Event(); self.errors = []
        self.nat = {4: {'local': [None,None], 'allow': [False,False], 'forwarded': 0, 'warm': 0},
                    6: {'local': [None,None], 'allow': [False,False], 'forwarded': 0, 'warm': 0}}
        for family, host, number in ((socket.AF_INET,'127.0.0.1',4),(socket.AF_INET6,'::1',6)):
            for mode in ('good','drop','bad','warm','warm2','late','wrong-source','nat0','nat1','natinfo0','natinfo1'):
                sock = socket.socket(family,socket.SOCK_DGRAM)
                sock.bind((host,0)); self.sockets[sock]=(number,mode)
                self.ports[(number,mode)]=sock.getsockname()[1]
        self.thread=threading.Thread(target=self.run,daemon=True); self.thread.start(); return self

    def defines(self):
        return ''.join(f'#define TEST_{mode.upper()}_{family} {port}\n' for (family,mode),port in self.ports.items() if mode!='wrong-source')

    @staticmethod
    def reply(request, source, family):
        cookie=b'\x21\x12\xa4\x42'; transaction=request[8:20]
        ip=socket.inet_pton(socket.AF_INET if family==4 else socket.AF_INET6,source[0]); mask=cookie+transaction
        body=bytes((0,1 if family==4 else 2))+struct.pack('!H',source[1]^0x2112)+bytes(a^b for a,b in zip(ip,mask))
        attr=struct.pack('!HH',0x0020,len(body))+body
        head=struct.pack('!HH',0x0101,len(attr)+8)+cookie+transaction
        prefix=head+attr; fingerprint=(binascii.crc32(prefix)&0xffffffff)^0x5354554e
        return prefix+struct.pack('!HHI',0x8028,4,fingerprint)

    @staticmethod
    def send(sock, packet, source):
        # Windows can report an earlier closed test peer's UDP ICMP error on
        # the next send/receive. It is per datagram, not an oracle shutdown.
        try: sock.sendto(packet,source)
        except ConnectionResetError: pass

    def run(self):
        try:
            while not self.stop.is_set():
                ready,_,_=select.select(list(self.sockets),[],[],.05)
                for sock in ready:
                    try: packet,source=sock.recvfrom(2048)
                    except ConnectionResetError: continue
                    family,mode=self.sockets[sock]
                    if mode in ('nat0','nat1'):
                        # Socket N is the public mapping of endpoint N. A send
                        # from endpoint 1-N to that public port opens its own
                        # filter. Forward only if N has sent to its peer too,
                        # using socket 1-N to rewrite the observed source.
                        side=int(mode[-1]); state=self.nat[family]; sender=1-side
                        if source!=state['local'][sender]: continue
                        state['allow'][sender]=True
                        if packet[:2]==b'\x00\x11':
                            state['warm']+=1; continue  # drop every warmup packet
                        if family in self.blocked_families:
                            self.blocked_packets[family]+=1; continue
                        if not state['allow'][side]: continue
                        out=next(s for s,(f,m) in self.sockets.items() if f==family and m==f'nat{sender}')
                        self.send(out,packet,state['local'][side]); state['forwarded']+=1; continue
                    if mode in ('natinfo0','natinfo1'):
                        if len(packet)!=28 or packet[:2]!=b'\x00\x01': continue
                        side=int(mode[-1]); state=self.nat[family]
                        if state['local'][side]!=source:
                            state['local'][side]=source; state['allow']=[False,False]
                        mapping=(source[0],self.ports[(family,f'nat{side}')])
                        self.send(sock,self.reply(packet,mapping,family),source); continue
                    if mode=='late': self.late+=1
                    if mode in ('warm','warm2') and packet[:2]==b'\x00\x11':
                        assert len(packet)==28 and packet[4:8]==b'\x21\x12\xa4\x42'
                        self.warm.append((family,source[1],mode,time.monotonic(),len(packet)))
                    if mode not in ('good','bad') or len(packet)!=28 or packet[:2]!=b'\x00\x01': continue
                    response=self.reply(packet,source,family)
                    wrong=bytearray(packet); wrong[8]^=1
                    self.send(sock,self.reply(bytes(wrong),source,family),source)
                    bad=bytearray(response); bad[-1]^=1; self.send(sock,bad,source)
                    stray=next(s for s,(f,m) in self.sockets.items() if f==family and m=='wrong-source')
                    self.send(stray,response,source)
                    if mode=='good': self.send(sock,response,source)
        except BaseException as error: self.errors.append(error)

    def verify(self, output):
        assert not self.errors,self.errors
        assert self.late==0,'expired queued discovery sent UDP after its admission deadline'
        assert all(state['forwarded']>0 and state['warm']>0 for state in self.nat.values()),self.nat
        for line in output.splitlines():
            if not line.startswith('WARM_SOURCE '): continue
            _,family,port=line.split(); events=[e for e in self.warm if e[:2]==(int(family),int(port))]
            assert 4<=len(events)<=7,events
            assert {e[2] for e in events}=={'warm','warm2'},events
            # Native aggregate pacing is 50ms. Permit clock/recv scheduling
            # jitter; exact pacing/state-machine gates also live in xrudp.
            assert all(b[3]-a[3]>=.025 for a,b in zip(events,events[1:])),events

    def __exit__(self,*args):
        self.stop.set(); self.thread.join(timeout=2)
        for sock in self.sockets: sock.close()
        assert not self.thread.is_alive()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    mode=parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--host',type=Path); mode.add_argument('--native-lower',type=Path)
    parser.add_argument('--cc',default='gcc'); args=parser.parse_args()
    with Oracles() as oracle:
        fixture=oracle.defines()+SOURCE
        if args.native_lower:
            # Sanitizer output stays visible; UDP oracle exceptions propagate.
            result=native_lower(args.native_lower.resolve(),args.cc,fixture)
            assert not oracle.errors,oracle.errors
            assert oracle.late==0
            return result
        with tempfile.TemporaryDirectory(prefix='mdo-rudp-probes-') as raw:
            root=Path(raw); (root/'web').mkdir()
            copy_app_source('src/storage/home.c',root); copy_app_source('src/remote/websocket.c',root)
            (root/'main.c').write_text(fixture,encoding='utf-8')
            with socket.socket() as listener:
                listener.bind(('127.0.0.1',0)); port=listener.getsockname()[1]
            config={'services':[{'class':'http','name':'probe-test','ip':'127.0.0.1','port':port,
                'host_default':{'path':'web','devlang':'c','devfile':'main.c'}}]}
            (root/'xs.json').write_text(json.dumps(config),encoding='utf-8')
            result=subprocess.run([str(args.host.resolve()),str(root/'xs.json')],cwd=root,
                capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=90)
            output=result.stdout+result.stderr
            if result.returncode or 'PASS mdo same-socket probes:' not in output:
                print(output)
                if oracle.errors: print('UDP oracle errors:',oracle.errors)
                return 1
            oracle.verify(output)
            print('\n'.join(line for line in output.splitlines() if line.startswith('PASS ')))
    return 0


if __name__=='__main__': raise SystemExit(main())
