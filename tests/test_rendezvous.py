"""Native rendezvous validation and encrypted data, with local UDP oracles."""
from pathlib import Path
import argparse
import json
import os
import socket
import subprocess
import sys
import tempfile
from runtime_sources import copy_app_source
from test_rudp_discovery import Oracles

SOURCE=r'''
#include <xsbase.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "src/storage/home.c"
#include "src/remote/websocket.c"
#define CHECK(c) do { if (!(c)) { printf("FAIL rendezvous %d %s\n",__LINE__,#c); fflush(stdout); exit(5); } } while(0)
typedef struct Queue { xvalue* Values[4]; size_t Count; } Queue;
static bool Signal(const xvalue* V, void* Data)
{
    Queue* Q=Data; CHECK(Q->Count<4);
    CHECK(!xrtValueObjectGet(V,XRT_STR_LITERAL("private_key")) && !xrtValueObjectGet(V,XRT_STR_LITERAL("access_token")));
    Q->Values[Q->Count++]=xrtValueDeepClone(V); return Q->Values[Q->Count-1]!=NULL;
}
static xvalue* Pop(Queue* Q)
{
    if (!Q->Count) return NULL;
    xvalue* V=Q->Values[0]; --Q->Count;
    memmove(Q->Values,Q->Values+1,Q->Count*sizeof(*Q->Values)); return V;
}
static bool Receive(bool Binary, xbytesview Bytes, void* Data)
{
    CHECK(!Binary && Bytes.Size==8 && !memcmp(Bytes.Data,"verified",8)); *(bool*)Data=true; return true;
}
static void Session(xnetfamily Family, bool Nat, bool TamperMode)
{
    MdoPeerPolicy P[2]={{0}}; Queue Q[2]={{0}}; MdoPeerInfo I[2]; MdoPeerSession* S[2]={NULL,NULL};
    MdoRemoteSocket* Socket[2]={NULL,NULL}; xcancel* Cancel=xrtCancelCreate(); CHECK(Cancel);
    for (unsigned i=0;i<2;++i) {
        CHECK(xrtNetAddrLoopback(&P[i].Local[Family==XNET_FAMILY_IPV6],Family,0));
        if (Nat) {
            unsigned Port=Family==XNET_FAMILY_IPV4 ? (i?TEST_NATINFO1_4:TEST_NATINFO0_4):(i?TEST_NATINFO1_6:TEST_NATINFO0_6);
            CHECK(xrtNetAddrLoopback(&P[i].Stun[Family==XNET_FAMILY_IPV6],Family,(uint16)Port));
        }
    }
    MdoPeerOffer* O=MdoPeerOfferCreate("11111111111111111111111111111111","22222222222222222222222222222222",7,TamperMode,&P[0]); CHECK(O);
    xvalue* Offer=MdoPeerOfferValue(O); CHECK(Offer && xrtValueCount(Offer)==10);
    CHECK(!MdoPeerInitiate(Offer,8,&P[1],Cancel,Signal,&Q[1]));
    xvalue* Bad=xrtValueDeepClone(Offer); CHECK(Bad);
    CHECK(MdoPeerTextSet(Bad,"public_key","0000000000000000000000000000000000000000000000000000000000000000"));
    CHECK(!MdoPeerInitiate(Bad,7,&P[1],Cancel,Signal,&Q[1])); xrtValueRelease(Bad);
    if (TamperMode) CHECK(MdoPeerTextSet(Offer,"mode","control"));
    S[1]=MdoPeerInitiate(Offer,7,&P[1],Cancel,Signal,&Q[1]); CHECK(S[1]); xrtValueRelease(Offer);
    double End=xrtTimer()+8; bool ClientFailed=false;
    for (;;) {
        for (unsigned i=0;i<2;++i) if (S[i]) {
            bool Ok=MdoPeerPoll(S[i]); CHECK(MdoPeerGetInfo(S[i],&I[i]));
            if (!Ok) { CHECK(i==1 && TamperMode); ClientFailed=true; }
        }
        for (unsigned i=0;i<2;++i) {
            xvalue* V;
            while ((V=Pop(&Q[i]))) {
                cstr Type=MdoPeerText(V,"type",24); CHECK(Type);
                if (!strcmp(Type,"rudp_init")) {
                    CHECK(i==1 && !S[0]);
                    Bad=xrtValueDeepClone(V); CHECK(MdoPeerTextSet(Bad,"nonce","00000000000000000000000000000000"));
                    CHECK(!MdoPeerAccept(O,Bad,&P[0],Cancel,Signal,&Q[0])); xrtValueRelease(Bad);
                    S[0]=MdoPeerAccept(O,V,&P[0],Cancel,Signal,&Q[0]); CHECK(S[0]);
                    CHECK(!MdoPeerAccept(O,V,&P[0],Cancel,Signal,&Q[0]) && !MdoPeerOfferValue(O));
                } else {
                    CHECK(S[1-i]);
                    Bad=xrtValueDeepClone(V); CHECK(MdoPeerTextSet(Bad,"runtime_id","33333333333333333333333333333333"));
                    CHECK(!MdoPeerInput(S[1-i],Bad)); xrtValueRelease(Bad);
                    if (!strcmp(Type,"rudp_candidates") || !strcmp(Type,"rudp_answer")) {
                        Bad=xrtValueDeepClone(V); xvalue* List=xrtValueObjectEdit(Bad,XRT_STR_LITERAL("candidates")); CHECK(List);
                        xvalue* First=xrtValueArrayEdit(List,0); CHECK(First && MdoPeerTextSet(First,"ip","255.255.255.255"));
                        CHECK(!MdoPeerInput(S[1-i],Bad)); xrtValueRelease(Bad);
                    }
                    CHECK(MdoPeerInput(S[1-i],V) && MdoPeerInput(S[1-i],V));
                }
                xrtValueRelease(V);
            }
        }
        if (ClientFailed) { CHECK(!I[0].Ready && !I[1].Ready); break; }
        if (S[0] && I[0].Ready && I[1].Ready) break;
        CHECK(xrtTimer()<End); xrtSleep(1);
    }
    if (!TamperMode) {
        CHECK(I[0].Family==Family && I[1].Family==Family);
        for (unsigned i=0;i<2;++i) { Socket[i]=MdoPeerTakeSocket(S[i]); CHECK(Socket[i] && !MdoPeerTakeSocket(S[i])); }
        for (unsigned i=0;i<2;++i) {
            CHECK(MdoRemoteSocketSend(Socket[i],false,(xbytesview){(const uint8*)"verified",8})); bool Got=false;
            while (!Got) { CHECK(xrtTimer()<End); CHECK(MdoRemoteSocketPoll(Socket[1-i],Receive,&Got)); xrtSleep(1); }
        }
    }
    for (unsigned i=0;i<2;++i) { MdoRemoteSocketDestroy(Socket[i]); MdoPeerSessionDestroy(S[i]); xvalue* V; while ((V=Pop(&Q[i]))) xrtValueRelease(V); }
    MdoPeerOfferDestroy(O); xrtCancelDestroy(Cancel); CHECK(xrtAtomic32Load(&g_MdoRudpTransports,XMEMORY_ACQUIRE)==0);
    printf("PASS rendezvous family=%u nat=%u tamper-mode=%u\n",(unsigned)Family,Nat,TamperMode); fflush(stdout);
}
static void Dual(unsigned Broken, unsigned Restrict, bool BindFailure)
{
    MdoPeerPolicy P[2]={{0}}; Queue Q[2]={{0}}; MdoPeerSession* S[2]={NULL,NULL};
    MdoPeerInfo I[2]={{0}}; MdoRemoteSocket* Socket[2]={NULL,NULL}; xvalue* Held=NULL;
    xcancel* Cancel=xrtCancelCreate(); CHECK(Cancel);
    for (unsigned i=0;i<2;++i) for (unsigned f=0;f<2;++f)
        CHECK(xrtNetAddrLoopback(&P[i].Local[f],f?XNET_FAMILY_IPV6:XNET_FAMILY_IPV4,0));
    if (Restrict) memset(&P[1].Local[Restrict==1 ? 1:0],0,sizeof(xnetaddr));
    if (BindFailure) P[0].Local[1].Port=TEST_DROP_6;
    MdoPeerOffer* O=MdoPeerOfferCreate("11111111111111111111111111111111","22222222222222222222222222222222",7,false,&P[0]); CHECK(O);
    xvalue* Offer=MdoPeerOfferValue(O); CHECK(Offer);
    S[1]=MdoPeerInitiate(Offer,7,&P[1],Cancel,Signal,&Q[1]); CHECK(S[1]); xrtValueRelease(Offer);
    uint64 OriginalUntil=S[1]->Until; double Begin=xrtTimer(), HoldUntil=0, End=Begin+5;
    unsigned Expected=Restrict ? (Restrict==1 ? 4:6):((Broken&2u)||BindFailure ? 4:6); bool Selected=false;
    for (;;) {
        for (unsigned i=0;i<2;++i) if (S[i]) { CHECK(MdoPeerPoll(S[i])); CHECK(MdoPeerGetInfo(S[i],&I[i])); }
        if (Broken==3 && xrtTimer()>Begin+.6) {
            CHECK(!S[1]->Selected && !I[0].Ready && !I[1].Ready);
            CHECK(S[1]->Until==OriginalUntil); S[1]->Until=MdoMonotonicUs();
            CHECK(!MdoPeerPoll(S[1]) && !MdoPeerTakeSocket(S[1])); break;
        }
        for (unsigned i=0;i<2;++i) {
            xvalue* V;
            while ((V=Pop(&Q[i]))) {
                cstr Type=MdoPeerText(V,"type",24); CHECK(Type);
                if (!strcmp(Type,"rudp_init")) {
                    CHECK(i==1 && !S[0]);
                    xvalue* Bad=xrtValueDeepClone(V),*F=xrtValueArray(); CHECK(Bad && F);
                    CHECK(xrtValueArrayAppendNew(F,xrtValueUInt(4)) && xrtValueArrayAppendNew(F,xrtValueUInt(4)));
                    CHECK(xrtValueObjectSetNew(Bad,XRT_STR_LITERAL("families"),F));
                    CHECK(!MdoPeerAccept(O,Bad,&P[0],Cancel,Signal,&Q[0]) && !O->Used); xrtValueRelease(Bad);
                    S[0]=MdoPeerAccept(O,V,&P[0],Cancel,Signal,&Q[0]); CHECK(S[0]);
                } else {
                    CHECK(S[1-i]); uint64 Family=0; CHECK(MdoPeerUInt(xrtValueObjectGet(V,XRT_STR_LITERAL("family")),&Family));
                    if (!strcmp(Type,"rudp_answer") || !strcmp(Type,"rudp_candidates")) {
                        if (Broken&(Family==4 ? 1u:2u)) {
                            xvalue* List=xrtValueObjectEdit(V,XRT_STR_LITERAL("candidates")); CHECK(List && xrtValueCount(List)==1);
                            xvalue* First=xrtValueArrayEdit(List,0); CHECK(First);
                            CHECK(xrtValueObjectSetNew(First,XRT_STR_LITERAL("port"),xrtValueUInt(Family==4 ? TEST_DROP_4:TEST_DROP_6)));
                        }
                    } else if (!strcmp(Type,"rudp_select")) {
                        CHECK(i==1 && !Selected && Family==Expected && !I[0].Ready && !I[1].Ready);
                        CHECK(!MdoPeerTakeSocket(S[0]) && !MdoPeerTakeSocket(S[1])); Selected=true;
                        if (Expected==4 && !Restrict) CHECK(xrtTimer()-Begin>=.20 && xrtTimer()-Begin<2);
                    } else if (!strcmp(Type,"rudp_selected")) {
                        CHECK(i==0 && Selected && !Held && Family==Expected);
                        Held=xrtValueDeepClone(V); CHECK(Held); HoldUntil=xrtTimer()+.12; xrtValueRelease(V); continue;
                    } else CHECK(false);
                    uint64 Until=S[1-i]->Until;
                    CHECK(MdoPeerInput(S[1-i],V) && MdoPeerInput(S[1-i],V) && S[1-i]->Until==Until);
                }
                xrtValueRelease(V);
            }
        }
        if (Held) {
            CHECK(!I[1].Ready && !MdoPeerTakeSocket(S[1]));
            if (xrtTimer()>=HoldUntil) {
                CHECK(xrtAtomic32Load(&g_MdoRudpTransports,XMEMORY_ACQUIRE)==2);
                CHECK(MdoPeerInput(S[1],Held) && MdoPeerInput(S[1],Held));
                xvalue* Bad=xrtValueDeepClone(Held); CHECK(Bad);
                CHECK(xrtValueObjectSetNew(Bad,XRT_STR_LITERAL("family"),xrtValueUInt(Expected==4 ? 6:4)));
                CHECK(!MdoPeerInput(S[1],Bad)); xrtValueRelease(Bad); xrtValueRelease(Held); Held=NULL;
            }
        }
        if (S[0] && I[0].Ready && I[1].Ready) break;
        CHECK(xrtTimer()<End); xrtSleep(1);
    }
    if (Broken!=3) {
        CHECK(Selected && I[0].Family==Expected && I[1].Family==Expected);
        for (unsigned i=0;i<2;++i) { Socket[i]=MdoPeerTakeSocket(S[i]); CHECK(Socket[i] && !MdoPeerTakeSocket(S[i])); }
        for (unsigned i=0;i<2;++i) {
            CHECK(MdoRemoteSocketSend(Socket[i],false,(xbytesview){(const uint8*)"verified",8})); bool Got=false;
            while (!Got) { CHECK(xrtTimer()<End); CHECK(MdoRemoteSocketPoll(Socket[1-i],Receive,&Got)); xrtSleep(1); }
        }
    }
    xrtValueRelease(Held); xrtCancelRequest(Cancel);
    for (unsigned i=0;i<2;++i) { MdoRemoteSocketDestroy(Socket[i]); MdoPeerSessionDestroy(S[i]); xvalue* V; while ((V=Pop(&Q[i]))) xrtValueRelease(V); }
    MdoPeerOfferDestroy(O); xrtCancelDestroy(Cancel); CHECK(xrtAtomic32Load(&g_MdoRudpTransports,XMEMORY_ACQUIRE)==0);
    printf("PASS dual family broken=%u restrict=%u bind-failure=%u selected=%u\n",Broken,Restrict,BindFailure,Broken==3 ? 0:Expected); fflush(stdout);
}
void ServiceInit(XS_HostInfo* Host)
{
    (void)Host;
    Session(XNET_FAMILY_IPV4,false,false); Session(XNET_FAMILY_IPV6,false,false);
    Session(XNET_FAMILY_IPV4,true,false); Session(XNET_FAMILY_IPV6,true,false);
    Session(XNET_FAMILY_IPV4,false,true);
    Dual(0,0,false); Dual(2,0,false); Dual(1,0,false); Dual(3,0,false);
    Dual(0,1,false); Dual(0,2,false); Dual(0,0,true);
    printf("PASS rendezvous authority, immutable identity, exact duplicate signals, NAT and encrypted message lifecycle\n"); fflush(stdout); exit(0);
}
'''


def native(sdk, fixture, cc):
    if sys.platform!='linux': raise SystemExit('native sanitizer mode requires Linux')
    root=Path(__file__).resolve().parents[1]
    fixture=fixture.replace('#include "src/storage/home.c"\n#include "src/remote/websocket.c"',
        '#define XRT_IMPLEMENTATION\n#include <xrt.h>\n#include "app/src/remote/websocket.c"')
    fixture=fixture.replace('void ServiceInit(XS_HostInfo* Host)','int main(void)').replace('    (void)Host;\n','')
    with tempfile.TemporaryDirectory(prefix='mdo-rendezvous-asan-') as raw:
        base=Path(raw); source=base/'test.c'; binary=base/'test'; source.write_text(fixture,encoding='utf-8')
        includes=[root,sdk/'lib',sdk/'src/sdk',sdk/'lib/xllm',sdk/'lib/xrudp/include',sdk/'lib/xrtshim']
        command=[cc,'-std=gnu11','-g','-O1','-fno-omit-frame-pointer','-ffunction-sections','-fdata-sections',
            '-fsanitize=address,undefined','-D_GNU_SOURCE=1','-DXRT_MODULE_ALL=1','-DXRUDP_MODULE_ALL=1','-DXS_USE_XRUDP=1']
        for include in includes: command+=['-I',str(include)]
        subprocess.run([*command,str(source),*map(str,sorted((sdk/'lib/xrudp/src').rglob('*.c'))),
            '-Wl,--gc-sections','-o',str(binary),'-pthread','-lm','-ldl'],check=True)
        subprocess.run([str(binary)],check=True,timeout=90,env={**os.environ,
            'ASAN_OPTIONS':'detect_leaks=1:halt_on_error=1','UBSAN_OPTIONS':'halt_on_error=1:print_stacktrace=1'})


def main():
    parser=argparse.ArgumentParser(description=__doc__); mode=parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--host',type=Path); mode.add_argument('--native',type=Path); parser.add_argument('--cc',default='gcc'); args=parser.parse_args()
    with Oracles() as oracle:
        fixture=oracle.defines()+SOURCE
        if args.native:
            native(args.native.resolve(),fixture,args.cc); assert not oracle.errors,oracle.errors; return 0
        with tempfile.TemporaryDirectory(prefix='mdo-rendezvous-') as raw:
            root=Path(raw); (root/'web').mkdir(); copy_app_source('src/storage/home.c',root); copy_app_source('src/remote/websocket.c',root)
            (root/'main.c').write_text(fixture,encoding='utf-8')
            with socket.socket() as s: s.bind(('127.0.0.1',0)); port=s.getsockname()[1]
            (root/'xs.json').write_text(json.dumps({'services':[{'class':'http','name':'rendezvous','ip':'127.0.0.1','port':port,
                'host_default':{'path':'web','devlang':'c','devfile':'main.c'}}]}),encoding='utf-8')
            result=subprocess.run([str(args.host.resolve()),str(root/'xs.json')],cwd=root,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=90)
            output=result.stdout+result.stderr
            if result.returncode or 'PASS rendezvous authority' not in output:
                print(output); print('oracle errors:',oracle.errors); return 1
            assert not oracle.errors,oracle.errors
            print('\n'.join(line for line in output.splitlines() if line.startswith('PASS ')))
    return 0


if __name__=='__main__': raise SystemExit(main())
