#include <stdio.h>
#include <string.h>
#include "lan.h"
#include "service_client.h"
#include "lan_identity.inc.c"
#define MDO_LAN_PEERS 4u
#define MDO_LAN_PROTOCOL "mdo.lan.v1"
typedef struct MdoLanGrant { char Peer[33], Token[65]; bool ReadOnly; uint64 Until; } MdoLanGrant;
typedef struct MdoLanChannel { MdoRemoteSocket* Socket; size_t Grant; bool Ready; char Requests[8][33]; } MdoLanChannel;
struct MdoLan {
    MdoRemoteBridge* Bridge; xcancel* Cancel; xtlslistener* Listener;
    char Certificate[1537]; uint16 Port; xvalue* Addresses;
    MdoLanGrant Grants[MDO_LAN_PEERS]; MdoLanChannel Channels[MDO_LAN_PEERS];
};
static bool MdoLanAddress(cstr Text)
{
    xnetaddr Address;
    if (!Text || !xrtNetAddrParse(&Address,Text,0u) || Address.Family!=XNET_FAMILY_IPV4) return false;
    const uint8* Ip=Address.Address;
    return Ip[0]==10u || (Ip[0]==172u && Ip[1]>=16u && Ip[1]<=31u) || (Ip[0]==192u && Ip[1]==168u)
        || (Ip[0]==127u); /* Loopback is useful for one-machine functional QA. */
}
static cstr MdoLanText(const xvalue* Value)
{
    xstrview Text;
    return xrtValueGetString(Value,&Text) && Text.Size<64u && strlen(Text.Data)==Text.Size ? Text.Data : NULL;
}
static void MdoLanChannelClose(MdoLan* Lan, size_t Slot)
{
    MdoLanChannel* Channel=&Lan->Channels[Slot];
    if (Channel->Ready) {
        for (size_t i=0u;i<8u;++i) if (Channel->Requests[i][0]) {
            char Json[128]; int Size=snprintf(Json,sizeof(Json),"{\"type\":\"cancel\",\"id\":\"%s\"}",Channel->Requests[i]);
            (void)MdoRemoteBridgeInput(Lan->Bridge,Lan->Grants[Channel->Grant].Peer,false,
                (xbytesview){(const uint8*)Json,(size_t)Size});
        }
    }
    MdoRemoteSocketDestroy(Channel->Socket); memset(Channel,0,sizeof(*Channel));
}
void MdoLanPeerClose(MdoLan* Lan, cstr Peer)
{
    if (!Lan) return;
    for (size_t i=0u;i<MDO_LAN_PEERS;++i) if (!strcmp(Lan->Grants[i].Peer,Peer)) {
        for (size_t j=0u;j<MDO_LAN_PEERS;++j) if (Lan->Channels[j].Ready && Lan->Channels[j].Grant==i) MdoLanChannelClose(Lan,j);
        xrtSecureZero(&Lan->Grants[i],sizeof(Lan->Grants[i]));
    }
}
void MdoLanDestroy(MdoLan* Lan)
{
    if (!Lan) return;
    MdoRemoteBridgeDirect(Lan->Bridge,NULL,NULL);
    if (Lan->Listener) { (void)xrtTlsListenerClose(Lan->Listener); xrtTlsListenerDestroy(Lan->Listener); }
    for (size_t i=0u;i<MDO_LAN_PEERS;++i) MdoLanChannelClose(Lan,i);
    xrtValueRelease(Lan->Addresses); xrtCancelDestroy(Lan->Cancel); xrtSecureZero(Lan,sizeof(*Lan)); xrtFree(Lan);
}
MdoLan* MdoLanCreate(MdoRemoteNet* Net, MdoRemoteBridge* Bridge, xcancel* Cancel)
{
    MdoLan* Lan=xrtCalloc(1u,sizeof(*Lan)); xtlsidentity* Identity=NULL;
    if (!Lan) return NULL;
    Lan->Bridge=Bridge; Lan->Cancel=xrtCancelChild(Cancel); Lan->Addresses=xrtValueArray();
    Identity=MdoLanIdentity(Lan->Certificate);
    if (!Lan->Cancel || !Lan->Addresses || !Identity) goto fail;
    xtlslistenerconfig Config; xrtTlsListenerConfigInit(&Config);
    Config.Tls.Context=Net->Tls; Config.Tls.Identity=Identity;
    Config.AcceptQueueLimit=4u; Config.HandshakeLimit=4u; Config.Stream.HandshakeTimeout=2000000u;
    Config.Listen.AcceptConcurrency=2u; Config.Listen.AcceptQueueLimit=4u;
    Lan->Listener=xrtTlsListenerStart(Net->Engine,&Config,NULL,NULL,NULL);
    xrtTlsIdentityRelease(Identity); Identity=NULL;
    xnetaddr Local;
    if (!Lan->Listener || !xrtTlsListenerLocal(Lan->Listener,&Local)) goto fail;
    Lan->Port=Local.Port;
    xnetinterfacelist Interfaces={0};
    if (xrtNetInterfaces(&Interfaces)) {
        for (size_t i=0u;i<Interfaces.Count && xrtValueCount(Lan->Addresses)<4u;++i) {
            const xnetinterface* Interface=&Interfaces.Items[i];
            if (!(Interface->Flags&XNET_INTERFACE_UP) || (Interface->Flags&XNET_INTERFACE_LOOPBACK)) continue;
            for (size_t j=0u;j<Interface->AddressCount && xrtValueCount(Lan->Addresses)<4u;++j) {
                char Ip[64]; const xnetaddr* Addr=&Interface->Addresses[j].Address;
                if (Addr->Family==XNET_FAMILY_IPV4 && xrtNetAddrText(Addr,Ip,sizeof(Ip)) && MdoLanAddress(Ip))
                    (void)xrtValueArrayAppendNew(Lan->Addresses,xrtValueString(xrtStrView(Ip)));
            }
        }
        xrtNetInterfacesFree(&Interfaces);
    }
    MdoRemoteBridgeDirect(Bridge,MdoLanOffer,Lan); return Lan;
fail:
    xrtTlsIdentityRelease(Identity); MdoLanDestroy(Lan); xrtClearError(); return NULL;
}
xvalue* MdoLanOffer(cstr Peer, bool ReadOnly, void* Data)
{
    MdoLan* Lan=Data; size_t Slot=MDO_LAN_PEERS;
    if (!Lan || !xrtValueCount(Lan->Addresses)) return NULL;
    for (size_t i=0u;i<MDO_LAN_PEERS;++i) if (!Lan->Grants[i].Peer[0]) { Slot=i; break; }
    if (Slot==MDO_LAN_PEERS) return NULL;
    MdoLanGrant* Grant=&Lan->Grants[Slot];
    if (!MdoAccountRandom(Grant->Token)) return NULL;
    strcpy(Grant->Peer,Peer); Grant->ReadOnly=ReadOnly; Grant->Until=xrtDeadlineAfter(60000000u);
    xvalue* Offer=xrtValueObject();
    bool Ok=Offer && MdoAccountSetUInt(Offer,"version",1u) && MdoAccountSetUInt(Offer,"port",Lan->Port) &&
        MdoAccountSetString(Offer,"peer_id",Peer) && MdoAccountSetString(Offer,"token",Grant->Token) &&
        MdoAccountSetString(Offer,"certificate",Lan->Certificate) &&
        xrtValueObjectSetNew(Offer,XRT_STR_LITERAL("addresses"),xrtValueDeepClone(Lan->Addresses));
    if (!Ok) { xrtValueRelease(Offer); xrtSecureZero(Grant,sizeof(*Grant)); return NULL; }
    return Offer;
}
typedef struct MdoLanContext { MdoLan* Lan; size_t Slot; } MdoLanContext;
static bool MdoLanAuthorize(const xhttp1head* Head, void* Data)
{
    MdoLanContext* Context=Data; MdoLan* Lan=Context->Lan; const xhttpfield* Protocols=NULL;
    if (!xrtStrEqual(Head->Target,XRT_STR_LITERAL("/mdo-lan")) ||
        xrtHttpFieldGetUnique(Head->Fields,Head->FieldCount,XRT_STR_LITERAL("Sec-WebSocket-Protocol"),&Protocols)!=XHTTP_NEXT_ITEM) return false;
    for (size_t i=0u;i<MDO_LAN_PEERS;++i) {
        MdoLanGrant* Grant=&Lan->Grants[i]; char Token[96];
        if (!Grant->Token[0] || xrtDeadlineExpired(Grant->Until)) continue;
        snprintf(Token,sizeof(Token),"mdo.grant.%s",Grant->Token);
        if (xrtWsProtocolsHas(Protocols->Value,xrtStrView(Token))) {
            xrtSecureZero(Grant->Token,sizeof(Grant->Token)); Lan->Channels[Context->Slot].Grant=i; return true;
        }
    }
    return false;
}
static bool MdoLanMessage(bool Binary, xbytesview Bytes, void* Data)
{
    MdoLanContext* Context=Data; MdoLan* Lan=Context->Lan; MdoLanChannel* Channel=&Lan->Channels[Context->Slot];
    if (!Binary) {
        xjsonreadconfig Limits; xrtJsonReadConfigInit(&Limits); Limits.MaxInputBytes=16384u; Limits.MaxValues=128u;
        xvalue* Value=xrtJsonRead(xrtStrViewN((cstr)Bytes.Data,Bytes.Size),&Limits);
        cstr Type=MdoAccountText(Value,"type",32u), Id=MdoAccountText(Value,"id",32u);
        if (Type && Id && MdoRemoteIdValid(Id) && (!strcmp(Type,"request") || !strcmp(Type,"live_open") || !strcmp(Type,"receipt"))) {
            bool Exists=false;
            for (size_t i=0u;i<8u;++i) if (!strcmp(Channel->Requests[i],Id)) Exists=true;
            if (!Exists) for (size_t i=0u;i<8u;++i) if (!Channel->Requests[i][0]) { strcpy(Channel->Requests[i],Id); Exists=true; break; }
            /* Never admit a request whose response cannot be routed back. */
            if (!Exists) { xrtValueRelease(Value); return false; }
        }
        /* Explicit cancellation has no terminal reply. Reclaim its route
         * slot immediately; the controller ignores any late cancelled IDs. */
        if (Type && Id && (!strcmp(Type,"cancel") || !strcmp(Type,"live_close")))
            for (size_t i=0u;i<8u;++i) if (!strcmp(Channel->Requests[i],Id)) Channel->Requests[i][0]=0;
        xrtValueRelease(Value);
    }
    return MdoRemoteBridgeInput(Lan->Bridge,Lan->Grants[Channel->Grant].Peer,Binary,Bytes);
}
bool MdoLanPoll(MdoLan* Lan)
{
    if (!Lan) return true;
    if (xrtCancelRequested(Lan->Cancel)) return false;
    for (size_t i=0u;i<MDO_LAN_PEERS;++i) {
        MdoLanChannel* Channel=&Lan->Channels[i]; MdoLanContext Context={Lan,i};
        if (!Channel->Socket) {
            xtlsstream* Stream=xrtTlsListenerAccept(Lan->Listener);
            if (Stream) { Channel->Socket=MdoRemoteSocketAcceptTls(Stream,Lan->Cancel); xrtTlsStreamDestroy(Stream); }
        }
        if (!Channel->Socket) continue;
        if (!Channel->Ready) {
            int Result=MdoRemoteSocketUpgrade(Channel->Socket,MDO_LAN_PROTOCOL,MdoLanAuthorize,&Context);
            if (Result<0) { MdoLanChannelClose(Lan,i); continue; }
            if (!Result) continue;
            Channel->Ready=true;
            char Ready[256]; MdoLanGrant* Grant=&Lan->Grants[Channel->Grant];
            int Size=snprintf(Ready,sizeof(Ready),"{\"type\":\"direct_ready\",\"version\":1,\"runtime_id\":\"%s\",\"peer_id\":\"%s\",\"mode\":\"%s\"}",
                MdoRemoteBridgeRuntime(Lan->Bridge),Grant->Peer,Grant->ReadOnly ? "view" : "control");
            if (!MdoRemoteSocketSend(Channel->Socket,false,(xbytesview){(const uint8*)Ready,(size_t)Size})) { MdoLanChannelClose(Lan,i); continue; }
        }
        if (!MdoRemoteSocketPoll(Channel->Socket,MdoLanMessage,&Context)) MdoLanChannelClose(Lan,i);
    }
    return true;
}
bool MdoLanEmit(MdoLan* Lan, xbytesview Envelope, bool* Handled)
{
    *Handled=false; if (!Lan || Envelope.Size<21u) return true;
    char Peer[33]; static const char Hex[]="0123456789abcdef";
    for (size_t i=0u;i<16u;++i) { Peer[i*2u]=Hex[Envelope.Data[4u+i]>>4u]; Peer[i*2u+1u]=Hex[Envelope.Data[4u+i]&15u]; } Peer[32]=0;
    for (size_t i=0u;i<MDO_LAN_PEERS;++i) {
        MdoLanChannel* Channel=&Lan->Channels[i];
        if (!Channel->Ready || strcmp(Peer,Lan->Grants[Channel->Grant].Peer)) continue;
        /* HTTP/live streams remain on the route where they were admitted.
         * Control-plane notices and old in-flight relay requests stay relay. */
        char Id[33]=""; bool Terminal=false;
        if (Envelope.Data[20u]==2u && Envelope.Size>=50u) {
            for (size_t j=0u;j<16u;++j) { Id[j*2u]=Hex[Envelope.Data[26u+j]>>4u]; Id[j*2u+1u]=Hex[Envelope.Data[26u+j]&15u]; }
        } else if (Envelope.Data[20u]==1u) {
            xjsonreadconfig Limits; xrtJsonReadConfigInit(&Limits); Limits.MaxInputBytes=16384u; Limits.MaxValues=128u;
            xvalue* Value=xrtJsonRead(xrtStrViewN((cstr)Envelope.Data+21u,Envelope.Size-21u),&Limits);
            cstr Name=MdoAccountText(Value,"id",32u), Type=MdoAccountText(Value,"type",32u);
            if (MdoRemoteIdValid(Name)) strcpy(Id,Name);
            Terminal=Type && (!strcmp(Type,"end") || !strcmp(Type,"error") || !strcmp(Type,"receipt") || !strcmp(Type,"live_closed"));
            xrtValueRelease(Value);
        }
        for (size_t j=0u;j<8u;++j) if (Id[0] && !strcmp(Channel->Requests[j],Id)) {
            *Handled=true;
            bool Ok=MdoRemoteSocketSend(Channel->Socket,Envelope.Data[20u]==2u,
                (xbytesview){Envelope.Data+21u,Envelope.Size-21u});
            if (Terminal) Channel->Requests[j][0]=0;
            if (!Ok) MdoLanChannelClose(Lan,i);
            return true; /* A direct failure must not tear down the relay. */
        }
    }
    return true;
}
#include "lan_gateway.inc.c"
