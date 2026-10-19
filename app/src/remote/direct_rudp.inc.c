#if defined(XS_USE_XRUDP)
#include "direct_rudp.h"
#define MDO_DIRECT_RUDP_PEERS 4u
typedef struct MdoDirectRudpPeer {
    struct MdoDirectRudp* Direct;
    char Peer[33], Nonce[33];
    uint64 Until, Route;
    bool ReadOnly, Ready;
    MdoPeerOffer* Offer;
    MdoPeerSession* Session;
    MdoRemoteSocket* Socket;
} MdoDirectRudpPeer;
struct MdoDirectRudp {
    MdoRemoteBridge* Bridge;
    xcancel* Cancel;
    uint64 Owner;
    MdoPeerPolicy Policy;
    MdoDirectRudpPeer Peers[MDO_DIRECT_RUDP_PEERS];
};
static void MdoDirectRudpChannelClose(MdoDirectRudpPeer* Peer)
{
    if (Peer->Route) MdoRemoteBridgeRouteClose(Peer->Direct->Bridge,Peer->Route);
    MdoRemoteSocketDestroy(Peer->Socket); Peer->Socket=NULL;
    MdoPeerSessionDestroy(Peer->Session); Peer->Session=NULL; Peer->Ready=false;
}
void MdoDirectRudpPeerClose(MdoDirectRudp* Direct, cstr Peer)
{
    if (!Direct || !Peer) return;
    for (size_t i=0;i<MDO_DIRECT_RUDP_PEERS;++i) if (!strcmp(Direct->Peers[i].Peer,Peer)) {
        MdoDirectRudpPeer* Row=&Direct->Peers[i]; MdoDirectRudpChannelClose(Row);
        MdoPeerOfferDestroy(Row->Offer); xrtSecureZero(Row,sizeof(*Row));
    }
}
static bool MdoDirectRudpSignal(const xvalue* Value, void* Data)
{
    MdoDirectRudpPeer* Row=Data;
    return MdoRemoteBridgeControl(Row->Direct->Bridge,Row->Peer,Value);
}
static void MdoDirectRudpFailure(MdoDirectRudpPeer* Row, cstr Code)
{
    xvalue* V=xrtValueObject();
    bool Ok=V && MdoAccountSetString(V,"type","rudp_failed") && MdoAccountSetString(V,"nonce",Row->Nonce) &&
        MdoAccountSetString(V,"runtime_id",MdoRemoteBridgeRuntime(Row->Direct->Bridge)) && MdoAccountSetString(V,"code",Code);
    if (Ok) (void)MdoRemoteBridgeControl(Row->Direct->Bridge,Row->Peer,V);
    xrtValueRelease(V);
}
static xvalue* MdoDirectRudpOffer(cstr Peer, bool ReadOnly, void* Data)
{
    MdoDirectRudp* Direct=Data;
    if (xrtCancelRequested(Direct->Cancel)) return NULL;
    for (size_t i=0;i<MDO_DIRECT_RUDP_PEERS;++i) if (!Direct->Peers[i].Peer[0]) {
        MdoDirectRudpPeer* Row=&Direct->Peers[i]; Row->Direct=Direct;
        Row->Offer=MdoPeerOfferCreate(Peer,MdoRemoteBridgeRuntime(Direct->Bridge),Direct->Owner,ReadOnly,&Direct->Policy);
        xvalue* Value=MdoPeerOfferValue(Row->Offer); cstr Nonce=MdoAccountText(Value,"nonce",32);
        if (!Value || !Nonce) { xrtValueRelease(Value); MdoPeerOfferDestroy(Row->Offer); memset(Row,0,sizeof(*Row)); return NULL; }
        strcpy(Row->Peer,Peer); strcpy(Row->Nonce,Nonce); Row->ReadOnly=ReadOnly;
        Row->Until=MdoDeadlineAfterUs(UINT64_C(60000000)); return Value;
    }
    return NULL;
}
MdoDirectRudp* MdoDirectRudpCreate(MdoRemoteNet* Net, MdoRemoteBridge* Bridge, uint64 Owner, xcancel* Cancel)
{
    if (!Net || !Bridge || !Owner || xrtCancelRequested(Cancel)) return NULL;
    MdoDirectRudp* Direct=xrtCalloc(1,sizeof(*Direct)); if (!Direct) return NULL;
    Direct->Bridge=Bridge; Direct->Owner=Owner; Direct->Cancel=xrtCancelChild(Cancel);
    MdoPeerPolicyInit(&Direct->Policy,Net,MDO_ACCOUNT_SERVICE_ORIGIN);
    if (!Direct->Cancel) { MdoDirectRudpDestroy(Direct); return NULL; }
    MdoRemoteBridgeRendezvous(Bridge,MdoDirectRudpOffer,Direct); return Direct;
}
void MdoDirectRudpDestroy(MdoDirectRudp* Direct)
{
    if (!Direct) return;
    MdoRemoteBridgeRendezvous(Direct->Bridge,NULL,NULL); xrtCancelRequest(Direct->Cancel);
    for (size_t i=0;i<MDO_DIRECT_RUDP_PEERS;++i) {
        MdoDirectRudpChannelClose(&Direct->Peers[i]); MdoPeerOfferDestroy(Direct->Peers[i].Offer);
    }
    xrtCancelDestroy(Direct->Cancel); xrtSecureZero(Direct,sizeof(*Direct)); xrtFree(Direct);
}
bool MdoDirectRudpInput(MdoDirectRudp* Direct, cstr Peer, bool Binary, xbytesview Bytes, bool* Handled)
{
    *Handled=false; if (!Direct || Binary || Bytes.Size>4096) return true;
    xjsonreadconfig Limits; xrtJsonReadConfigInit(&Limits); Limits.MaxInputBytes=4096; Limits.MaxStringBytes=128; Limits.MaxValues=128; Limits.MaxDepth=4;
    xvalue* V=xrtJsonRead(xrtStrViewN((cstr)Bytes.Data,Bytes.Size),&Limits);
    cstr Type=MdoAccountText(V,"type",24), Nonce=MdoAccountText(V,"nonce",32), Runtime=MdoAccountText(V,"runtime_id",32);
    if (!Type || (strcmp(Type,"rudp_init") && strcmp(Type,"rudp_candidates") && strcmp(Type,"rudp_dial") && strcmp(Type,"rudp_select") && strcmp(Type,"rudp_cancel"))) { xrtValueRelease(V); return true; }
    *Handled=true;
    for (size_t i=0;i<MDO_DIRECT_RUDP_PEERS;++i) {
        MdoDirectRudpPeer* Row=&Direct->Peers[i];
        if (strcmp(Row->Peer,Peer) || !Nonce || strcmp(Nonce,Row->Nonce) || !Runtime || strcmp(Runtime,MdoRemoteBridgeRuntime(Direct->Bridge))) continue;
        if (!strcmp(Type,"rudp_init")) {
            if (!Row->Session && Row->Offer && !MdoDeadlineExpired(Row->Until)) {
                Row->Session=MdoPeerAccept(Row->Offer,V,&Direct->Policy,Direct->Cancel,MdoDirectRudpSignal,Row);
                if (!Row->Session) MdoDirectRudpFailure(Row,"rendezvous_unavailable");
            }
        } else if (!strcmp(Type,"rudp_cancel")) {
            if (xrtValueCount(V)==3) MdoDirectRudpChannelClose(Row);
        } else if (Row->Session && !Row->Ready && !MdoPeerInput(Row->Session,V)) {
            MdoDirectRudpFailure(Row,"invalid_signal"); MdoDirectRudpChannelClose(Row);
        }
        break;
    }
    xrtValueRelease(V); return true;
}
static bool MdoDirectRudpMessage(bool Binary, xbytesview Bytes, void* Data)
{
    MdoDirectRudpPeer* Row=Data;
    return MdoRemoteBridgeInputRouted(Row->Direct->Bridge,Row->Peer,Row->Route,Binary,Bytes);
}
bool MdoDirectRudpPoll(MdoDirectRudp* Direct)
{
    if (!Direct) return true;
    if (xrtCancelRequested(Direct->Cancel)) return false;
    for (size_t i=0;i<MDO_DIRECT_RUDP_PEERS;++i) {
        MdoDirectRudpPeer* Row=&Direct->Peers[i];
        if (!Row->Session) {
            if (Row->Offer && MdoDeadlineExpired(Row->Until)) { MdoPeerOfferDestroy(Row->Offer); Row->Offer=NULL; }
            continue;
        }
        if (!Row->Ready) {
            MdoPeerInfo Info;
            if (!MdoPeerPoll(Row->Session)) {
                (void)MdoPeerGetInfo(Row->Session,&Info); MdoDirectRudpFailure(Row,Info.Error); MdoDirectRudpChannelClose(Row); continue;
            }
            if (!MdoPeerGetInfo(Row->Session,&Info) || !Info.Ready) continue;
            Row->Socket=MdoPeerTakeSocket(Row->Session); Row->Route=MdoRemoteBridgeRoute(Direct->Bridge);
            if (!Row->Socket || !Row->Route) { MdoDirectRudpFailure(Row,"resources_unavailable"); MdoDirectRudpChannelClose(Row); continue; }
            char Ready[320]; int Size=snprintf(Ready,sizeof(Ready),
                "{\"type\":\"direct_ready\",\"version\":1,\"runtime_id\":\"%s\",\"peer_id\":\"%s\",\"mode\":\"%s\",\"transport\":\"xrudp\"}",
                MdoRemoteBridgeRuntime(Direct->Bridge),Row->Peer,Row->ReadOnly?"view":"control");
            if (!MdoRemoteSocketSend(Row->Socket,false,(xbytesview){(const uint8*)Ready,(size_t)Size})) { MdoDirectRudpChannelClose(Row); continue; }
            Row->Ready=true;
        }
        if (!MdoRemoteSocketPoll(Row->Socket,MdoDirectRudpMessage,Row)) MdoDirectRudpChannelClose(Row);
    }
    return true;
}
bool MdoDirectRudpEmit(MdoDirectRudp* Direct, uint64 Route, xbytesview Envelope, bool* Handled)
{
    *Handled=false; if (!Direct || !Route || Envelope.Size<21) return true;
    for (size_t i=0;i<MDO_DIRECT_RUDP_PEERS;++i) {
        MdoDirectRudpPeer* Row=&Direct->Peers[i]; if (!Row->Ready || Row->Route!=Route) continue;
        uint8 Peer[16];
        if (!MdoPeerUnhex(Row->Peer,16,Peer,false) || memcmp(Peer,Envelope.Data+4,16)) continue;
        *Handled=true;
        if (!MdoRemoteSocketSend(Row->Socket,Envelope.Data[20]==2,(xbytesview){Envelope.Data+21,Envelope.Size-21})) MdoDirectRudpChannelClose(Row);
        return true;
    }
    return true;
}
#endif
