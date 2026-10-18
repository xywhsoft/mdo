/* Authenticated rendezvous state belongs to the service owner, never a UDP
 * callback. Fixed limits and one-use identities apply before allocating peers. */
#if defined(XS_USE_XRUDP)
#include "rendezvous.h"
#define MDO_PEER_CANDIDATES 8u
#define MDO_PEER_ATTEMPT_US UINT64_C(10000000)
#ifndef MDO_REMOTE_STUN_PORT
#define MDO_REMOTE_STUN_PORT 3478u
#endif
struct MdoPeerOffer {
    MdoPeerInfo Info;
    unsigned char Private[32], Public[32], Scope[16], Binding[32];
    uint64 Until;
    unsigned Families;
    bool Used;
};
struct MdoPeerSession {
    MdoPeerInfo Info;
    MdoPeerPolicy Policy;
    MdoRudpTransport* Raw;
    xcancel* Cancel;
    xfuture* Resolve;
    MdoPeerSignalProc Signal;
    void* SignalData;
    uint64 Until, MappingUntil;
    xnetaddr Candidates[MDO_PEER_CANDIDATES];
    uint8 Kinds[MDO_PEER_CANDIDATES];
    size_t Count;
    xnetfamily Family;
    unsigned char Public[32];
    bool Initiator, InitSent, MappingStarted, MappingComplete, CandidatesSent, RemoteReady, Dialed, Taken;
};
static cstr MdoPeerText(const xvalue* Value, cstr Key, size_t Limit)
{
    xstrview Text;
    return xrtValueGetString(xrtValueObjectGet(Value,xrtStrView(Key)),&Text) && Text.Size<=Limit &&
        !memchr(Text.Data,0,Text.Size) ? Text.Data:NULL;
}
static bool MdoPeerTextSet(xvalue* Value, cstr Key, cstr Text)
{ return xrtValueObjectSetNew(Value,xrtStrView(Key),xrtValueString(xrtStrView(Text))); }
static bool MdoPeerUInt(const xvalue* Value, uint64* Number)
{
    int64 Signed;
    if (xrtValueType(Value)==XVALUE_UINT) return xrtValueGetUInt(Value,Number);
    if (xrtValueType(Value)!=XVALUE_INT || !xrtValueGetInt(Value,&Signed) || Signed<0) return false;
    *Number=(uint64)Signed; return true;
}
static void MdoPeerHex(const unsigned char* Bytes, size_t Size, char* Text)
{
    static const char Digits[]="0123456789abcdef";
    for (size_t i=0;i<Size;++i) { Text[2*i]=Digits[Bytes[i]>>4]; Text[2*i+1]=Digits[Bytes[i]&15]; }
    Text[2*Size]=0;
}
static bool MdoPeerUnhex(cstr Text, size_t Size, unsigned char* Bytes, bool Nonzero)
{
    unsigned char Any=0;
    if (!Text || strlen(Text)!=Size*2) return false;
    for (size_t i=0;i<Size;++i) {
        unsigned Value=0;
        for (size_t j=0;j<2;++j) {
            char C=Text[i*2+j];
            if (C>='0' && C<='9') Value=(Value<<4)|(unsigned)(C-'0');
            else if (C>='a' && C<='f') Value=(Value<<4)|(unsigned)(C-'a'+10);
            else return false;
        }
        if (Bytes) Bytes[i]=(unsigned char)Value;
        Any|=(unsigned char)Value;
    }
    return !Nonzero || Any!=0;
}
static unsigned MdoPeerFamilies(const MdoPeerPolicy* Policy)
{
    if (Policy->Local.Family==XNET_FAMILY_IPV4) return 1u;
    if (Policy->Local.Family==XNET_FAMILY_IPV6) return 2u;
    unsigned Mask=1u; xnetinterfacelist Interfaces={0};
    if (xrtNetInterfaces(&Interfaces)) {
        for (size_t i=0;i<Interfaces.Count;++i) {
            const xnetinterface* N=&Interfaces.Items[i];
            if (!(N->Flags&XNET_INTERFACE_UP) || (N->Flags&XNET_INTERFACE_LOOPBACK)) continue;
            for (size_t j=0;j<N->AddressCount;++j) {
                const xnetaddr* A=&N->Addresses[j].Address;
                if (A->Family==XNET_FAMILY_IPV6 && !xrtNetAddrIsLinkLocal(A) && !xrtNetAddrIsUnspecified(A)) Mask|=2u;
            }
        }
        xrtNetInterfacesFree(&Interfaces);
    }
    return Mask;
}
void MdoPeerPolicyInit(MdoPeerPolicy* Policy, MdoRemoteNet* Net, cstr TrustedOrigin)
{
    memset(Policy,0,sizeof(*Policy)); Policy->Resolver=Net ? Net->Resolver:NULL; Policy->StunPort=MDO_REMOTE_STUN_PORT;
    /* This origin is compile-time local policy, never an offer field. */
    if (!TrustedOrigin) return;
    cstr Host=!strncmp(TrustedOrigin,"https://",8) ? TrustedOrigin+8:
        (!strncmp(TrustedOrigin,"http://",7) ? TrustedOrigin+7:NULL);
    if (!Host) return;
    cstr End;
    if (*Host=='[') { ++Host; End=strchr(Host,']'); }
    else { End=Host; while (*End && *End!=':' && *End!='/') ++End; }
    if (!End || End==Host || (size_t)(End-Host)>=sizeof(Policy->StunHost)) return;
    memcpy(Policy->StunHost,Host,(size_t)(End-Host));
}
static bool MdoPeerMetadata(MdoPeerInfo* Info, cstr Peer, cstr Runtime, uint64 Owner, bool ReadOnly)
{
    if (!Owner || !MdoPeerUnhex(Peer,16,NULL,false) || !MdoPeerUnhex(Runtime,16,NULL,false)) return false;
    memset(Info,0,sizeof(*Info)); strcpy(Info->Peer,Peer); strcpy(Info->Runtime,Runtime);
    snprintf(Info->Owner,sizeof(Info->Owner),"%llu",(unsigned long long)Owner); Info->ReadOnly=ReadOnly; Info->Error=""; return true;
}
MdoPeerOffer* MdoPeerOfferCreate(cstr Peer, cstr Runtime, uint64 Owner, bool ReadOnly, const MdoPeerPolicy* Policy)
{
    MdoPeerOffer* O=xrtCalloc(1,sizeof(*O)); unsigned char Nonce[16];
    if (!O) return NULL;
    if (!Policy || !MdoPeerMetadata(&O->Info,Peer,Runtime,Owner,ReadOnly) ||
        !xrtSecureRandom(O->Private,32) || !xrtX25519Public(O->Private,O->Public) ||
        !xrtSecureRandom(O->Scope,16) || !xrtSecureRandom(O->Binding,32) || !xrtSecureRandom(Nonce,16)) {
        xrtSecureZero(Nonce,sizeof(Nonce)); MdoPeerOfferDestroy(O); return NULL;
    }
    MdoPeerHex(Nonce,16,O->Info.Nonce); xrtSecureZero(Nonce,sizeof(Nonce));
    O->Families=MdoPeerFamilies(Policy); O->Until=MdoDeadlineAfterUs(UINT64_C(60000000)); return O;
}
void MdoPeerOfferDestroy(MdoPeerOffer* Offer)
{ if (Offer) { xrtSecureZero(Offer,sizeof(*Offer)); xrtFree(Offer); } }
xvalue* MdoPeerOfferValue(const MdoPeerOffer* O)
{
    if (!O || O->Used || MdoDeadlineExpired(O->Until)) return NULL;
    char Public[65], Scope[33], Binding[65]; MdoPeerHex(O->Public,32,Public); MdoPeerHex(O->Scope,16,Scope); MdoPeerHex(O->Binding,32,Binding);
    xvalue* V=xrtValueObject(); xvalue* Families=xrtValueArray();
    bool Ok=V && Families && xrtValueObjectSetNew(V,XRT_STR_LITERAL("version"),xrtValueUInt(1)) &&
        MdoPeerTextSet(V,"peer_id",O->Info.Peer) && MdoPeerTextSet(V,"runtime_id",O->Info.Runtime) &&
        MdoPeerTextSet(V,"owner_id",O->Info.Owner) && MdoPeerTextSet(V,"mode",O->Info.ReadOnly?"view":"control") &&
        MdoPeerTextSet(V,"nonce",O->Info.Nonce) && MdoPeerTextSet(V,"public_key",Public) &&
        MdoPeerTextSet(V,"scope",Scope) && MdoPeerTextSet(V,"binding",Binding);
    if ((O->Families&1u) && !xrtValueArrayAppendNew(Families,xrtValueUInt(4))) Ok=false;
    if ((O->Families&2u) && !xrtValueArrayAppendNew(Families,xrtValueUInt(6))) Ok=false;
    if (Ok) { Ok=xrtValueObjectSetNew(V,XRT_STR_LITERAL("families"),Families); Families=NULL; }
    xrtValueRelease(Families); if (!Ok) { xrtValueRelease(V); V=NULL; } return V;
}
static MdoPeerSession* MdoPeerStart(const MdoPeerInfo* Info, const MdoPeerPolicy* Policy,
    MdoRudpConfig* Config, xcancel* Cancel, MdoPeerSignalProc Signal, void* Data)
{
    MdoPeerSession* S=xrtCalloc(1,sizeof(*S));
    if (!S) { xrtSecureZero(Config->Private,32); return NULL; }
    S->Info=*Info; S->Policy=*Policy; S->Family=Config->Local.Family; S->Initiator=!Config->Listen;
    S->Signal=Signal; S->SignalData=Data; S->Cancel=xrtCancelChild(Cancel); S->Until=MdoDeadlineAfterUs(MDO_PEER_ATTEMPT_US);
    if (!xrtX25519Public(Config->Private,S->Public) || !S->Cancel) { xrtSecureZero(Config->Private,32); MdoPeerSessionDestroy(S); return NULL; }
    /* Bind authority explicitly, in addition to the fresh offer seed. A
     * modified mode/owner/runtime/peer/nonce/family cannot share this Noise
     * application context with the target's immutable relay-granted metadata. */
    xsha256 Hash; unsigned char Binding[32], Flags[2]={Info->ReadOnly?1u:0u,(uint8)S->Family};
    static const char Domain[]="mdo.peer.authority.v1";
    xrtSha256Init(&Hash);
    bool Bound=xrtSha256Update(&Hash,Domain,sizeof(Domain)-1) && xrtSha256Update(&Hash,Config->Binding,32) &&
        xrtSha256Update(&Hash,Info->Peer,32) && xrtSha256Update(&Hash,Info->Runtime,32) &&
        xrtSha256Update(&Hash,Info->Nonce,32) && xrtSha256Update(&Hash,Info->Owner,strlen(Info->Owner)+1) &&
        xrtSha256Update(&Hash,Flags,sizeof(Flags)) && xrtSha256Final(&Hash,Binding);
    xrtSecureZero(&Hash,sizeof(Hash));
    if (Bound) memcpy(Config->Binding,Binding,32);
    xrtSecureZero(Binding,sizeof(Binding));
    if (Bound) S->Raw=MdoRudpStart(Config,S->Cancel);
    xrtSecureZero(Config->Private,32);
    if (!S->Raw) { MdoPeerSessionDestroy(S); return NULL; } return S;
}
static bool MdoPeerLocal(MdoRudpConfig* C, const MdoPeerPolicy* P, xnetfamily Family)
{
    if (P->Local.Family) { if (P->Local.Family!=Family) return false; C->Local=P->Local; return true; }
    return xrtNetAddrAny(&C->Local,Family,0);
}
MdoPeerSession* MdoPeerAccept(MdoPeerOffer* O, const xvalue* Init, const MdoPeerPolicy* Policy,
    xcancel* Cancel, MdoPeerSignalProc Signal, void* Data)
{
    cstr Type=MdoPeerText(Init,"type",24), Nonce=MdoPeerText(Init,"nonce",32), Runtime=MdoPeerText(Init,"runtime_id",32);
    uint64 Family=0; MdoRudpConfig C={0};
    if (!O || !Policy || !Signal || O->Used || MdoDeadlineExpired(O->Until) || xrtValueCount(Init)!=5 ||
        !Type || strcmp(Type,"rudp_init") || !Nonce || strcmp(Nonce,O->Info.Nonce) || !Runtime || strcmp(Runtime,O->Info.Runtime) ||
        !MdoPeerUInt(xrtValueObjectGet(Init,XRT_STR_LITERAL("family")),&Family) ||
        (Family!=4 && Family!=6) || !(O->Families&(Family==4?1u:2u)) ||
        !MdoPeerUnhex(MdoPeerText(Init,"public_key",64),32,C.PeerPublic,true) ||
        !memcmp(C.PeerPublic,O->Public,32) || !MdoPeerLocal(&C,Policy,(xnetfamily)Family)) return NULL;
    memcpy(C.Private,O->Private,32); memcpy(C.Scope,O->Scope,16); memcpy(C.Binding,O->Binding,32);
    C.Listen=true; C.MessageLimit=MDO_RUDP_MESSAGE_MAX; O->Used=true; xrtSecureZero(O->Private,32);
    return MdoPeerStart(&O->Info,Policy,&C,Cancel,Signal,Data);
}
MdoPeerSession* MdoPeerInitiate(const xvalue* Offer, uint64 Owner, const MdoPeerPolicy* Policy,
    xcancel* Cancel, MdoPeerSignalProc Signal, void* Data)
{
    MdoPeerInfo Info; MdoRudpConfig C={0}; uint64 Version=0; char OwnerText[21];
    const xvalue* Families=xrtValueObjectGet(Offer,XRT_STR_LITERAL("families")); unsigned Mask=0;
    cstr Mode=MdoPeerText(Offer,"mode",8), AdvertisedOwner=MdoPeerText(Offer,"owner_id",20);
    snprintf(OwnerText,sizeof(OwnerText),"%llu",(unsigned long long)Owner);
    if (!Policy || !Signal || xrtValueType(Offer)!=XVALUE_OBJECT || xrtValueCount(Offer)!=10 ||
        !MdoPeerUInt(xrtValueObjectGet(Offer,XRT_STR_LITERAL("version")),&Version) || Version!=1 ||
        !Mode || (strcmp(Mode,"view") && strcmp(Mode,"control")) || !AdvertisedOwner || strcmp(AdvertisedOwner,OwnerText) ||
        !MdoPeerMetadata(&Info,MdoPeerText(Offer,"peer_id",32),MdoPeerText(Offer,"runtime_id",32),Owner,!strcmp(Mode,"view")) ||
        !MdoPeerUnhex(MdoPeerText(Offer,"nonce",32),16,NULL,true) ||
        !MdoPeerUnhex(MdoPeerText(Offer,"public_key",64),32,C.PeerPublic,true) ||
        !MdoPeerUnhex(MdoPeerText(Offer,"scope",32),16,C.Scope,true) ||
        !MdoPeerUnhex(MdoPeerText(Offer,"binding",64),32,C.Binding,true) ||
        xrtValueType(Families)!=XVALUE_ARRAY || !xrtValueCount(Families) || xrtValueCount(Families)>2) return NULL;
    for (size_t i=0;i<xrtValueCount(Families);++i) {
        uint64 F=0; if (!MdoPeerUInt(xrtValueArrayGet(Families,i),&F) || (F!=4 && F!=6)) return NULL;
        unsigned Bit=F==4?1u:2u; if (Mask&Bit) return NULL; Mask|=Bit;
    }
    Mask&=MdoPeerFamilies(Policy); if (!Mask) return NULL;
    strcpy(Info.Nonce,MdoPeerText(Offer,"nonce",32));
    if (!MdoPeerLocal(&C,Policy,(Mask&2u)?XNET_FAMILY_IPV6:XNET_FAMILY_IPV4)) return NULL;
    if (!xrtSecureRandom(C.Private,32)) { xrtSecureZero(C.Private,32); return NULL; }
    C.MessageLimit=MDO_RUDP_MESSAGE_MAX; return MdoPeerStart(&Info,Policy,&C,Cancel,Signal,Data);
}
static xvalue* MdoPeerMessage(MdoPeerSession* S, cstr Type)
{
    xvalue* V=xrtValueObject();
    if (V && MdoPeerTextSet(V,"type",Type) && MdoPeerTextSet(V,"nonce",S->Info.Nonce) && MdoPeerTextSet(V,"runtime_id",S->Info.Runtime)) return V;
    xrtValueRelease(V); return NULL;
}
static bool MdoPeerSignal(MdoPeerSession* S, xvalue* V)
{ bool Ok=V && S->Signal(V,S->SignalData); xrtValueRelease(V); return Ok; }
static bool MdoPeerFail(MdoPeerSession* S, cstr Error)
{ if (!S->Info.Failed) { S->Info.Failed=true; S->Info.Error=Error; xrtCancelRequest(S->Cancel); } return false; }
static bool MdoPeerCandidate(const xnetaddr* A)
{
    return A && A->Port && (A->Family==XNET_FAMILY_IPV4 || A->Family==XNET_FAMILY_IPV6) &&
        !A->Scope && !xrtNetAddrIsUnspecified(A) && !xrtNetAddrIsMapped(A) && !xrtNetAddrIsMulticast(A) &&
        !xrtNetAddrIsLinkLocal(A) && !(A->Family==XNET_FAMILY_IPV4 && (!A->Address[0] || A->Address[0]>=224));
}
bool MdoPeerInput(MdoPeerSession* S, const xvalue* V)
{
    cstr Type=MdoPeerText(V,"type",24), Nonce=MdoPeerText(V,"nonce",32), Runtime=MdoPeerText(V,"runtime_id",32);
    if (!S || S->Info.Failed || !Type || !Nonce || strcmp(Nonce,S->Info.Nonce) || !Runtime || strcmp(Runtime,S->Info.Runtime)) return false;
    if (!strcmp(Type,"rudp_cancel") && xrtValueCount(V)==3) return MdoPeerFail(S,"cancelled");
    if (S->Initiator && !strcmp(Type,"rudp_failed") && xrtValueCount(V)==4 && MdoPeerText(V,"code",32)) return MdoPeerFail(S,"remote_unavailable");
    if (strcmp(Type,S->Initiator?"rudp_answer":"rudp_candidates") || xrtValueCount(V)!=4) return false;
    const xvalue* List=xrtValueObjectGet(V,XRT_STR_LITERAL("candidates"));
    size_t Count=xrtValueCount(List); xnetaddr Addresses[8]; uint8 Kinds[8];
    if (xrtValueType(List)!=XVALUE_ARRAY || !Count || Count>MDO_PEER_CANDIDATES) return false;
    for (size_t i=0;i<Count;++i) {
        const xvalue* Item=xrtValueArrayGet(List,i); cstr Ip=MdoPeerText(Item,"ip",64), Kind=MdoPeerText(Item,"kind",8); uint64 Port=0;
        if (xrtValueType(Item)!=XVALUE_OBJECT || xrtValueCount(Item)!=3 || !Ip || !Kind || (strcmp(Kind,"host") && strcmp(Kind,"srflx")) ||
            !MdoPeerUInt(xrtValueObjectGet(Item,XRT_STR_LITERAL("port")),&Port) || !Port || Port>65535 ||
            !xrtNetAddrParse(&Addresses[i],Ip,(uint16)Port) || Addresses[i].Family!=S->Family || !MdoPeerCandidate(&Addresses[i])) return false;
        Kinds[i]=!strcmp(Kind,"srflx")?1u:0u;
        for (size_t j=0;j<i;++j) if (xrtNetAddrEqual(&Addresses[i],&Addresses[j])) return false;
    }
    if (S->RemoteReady) {
        if (S->Count!=Count) return false;
        for (size_t i=0;i<Count;++i) if (Kinds[i]!=S->Kinds[i] || !xrtNetAddrEqual(&Addresses[i],&S->Candidates[i])) return false;
        return true; /* Exact duplicate, no deadline renewal or new targets. */
    }
    memcpy(S->Candidates,Addresses,Count*sizeof(*Addresses)); memcpy(S->Kinds,Kinds,Count); S->Count=Count; S->RemoteReady=true; return true;
}
static bool MdoPeerAppendCandidate(xvalue* List, const xnetaddr* Address, cstr Kind)
{
    if (!MdoPeerCandidate(Address) || xrtValueCount(List)>=MDO_PEER_CANDIDATES) return true;
    char Ip[64]; if (!xrtNetAddrText(Address,Ip,sizeof(Ip))) return false;
    for (size_t i=0;i<xrtValueCount(List);++i) {
        const xvalue* Item=xrtValueArrayGet(List,i); uint64 Port=0; cstr Existing=MdoPeerText(Item,"ip",64);
        (void)MdoPeerUInt(xrtValueObjectGet(Item,XRT_STR_LITERAL("port")),&Port);
        if (Existing && !strcmp(Existing,Ip) && Port==Address->Port) return true;
    }
    xvalue* Item=xrtValueObject();
    bool Ok=Item && MdoPeerTextSet(Item,"ip",Ip) && MdoPeerTextSet(Item,"kind",Kind) &&
        xrtValueObjectSetNew(Item,XRT_STR_LITERAL("port"),xrtValueUInt(Address->Port));
    if (!Ok) { xrtValueRelease(Item); return false; } return xrtValueArrayAppendNew(List,Item);
}
static xvalue* MdoPeerCandidates(MdoPeerSession* S, const MdoRudpInfo* Info)
{
    xvalue* List=xrtValueArray(); if (!List) return NULL;
    if (Info->Discovery.Complete && !MdoPeerAppendCandidate(List,&Info->Discovery.Mapped,"srflx")) goto fail;
    if (!xrtNetAddrIsUnspecified(&Info->Local)) {
        if (!MdoPeerAppendCandidate(List,&Info->Local,"host")) goto fail;
    } else {
        xnetinterfacelist Interfaces={0};
        if (xrtNetInterfaces(&Interfaces)) {
            bool Ok=true;
            for (size_t i=0;i<Interfaces.Count && xrtValueCount(List)<MDO_PEER_CANDIDATES;++i) {
                const xnetinterface* N=&Interfaces.Items[i];
                if (!(N->Flags&XNET_INTERFACE_UP) || (N->Flags&XNET_INTERFACE_LOOPBACK)) continue;
                for (size_t j=0;j<N->AddressCount && xrtValueCount(List)<MDO_PEER_CANDIDATES;++j) {
                    xnetaddr A=N->Addresses[j].Address; A.Port=Info->Local.Port;
                    if (A.Family==S->Family && !MdoPeerAppendCandidate(List,&A,"host")) Ok=false;
                }
            }
            xrtNetInterfacesFree(&Interfaces); if (!Ok) goto fail;
        }
    }
    if (xrtValueCount(List)) return List;
fail:
    xrtValueRelease(List); return NULL;
}
static bool MdoPeerMap(MdoPeerSession* S, MdoRudpInfo* Info)
{
    if (S->MappingComplete) return true;
    if (!S->MappingStarted) {
        S->MappingStarted=true; S->MappingUntil=MdoDeadlineAfterUs(UINT64_C(1500000));
        xnetaddr Server=S->Policy.Stun;
        if (!Server.Family && S->Policy.StunHost[0] && !xrtNetAddrParse(&Server,S->Policy.StunHost,S->Policy.StunPort) && S->Policy.Resolver)
            S->Resolve=xrtNetResolveAsync(S->Policy.Resolver,S->Policy.StunHost,S->Family);
        if (Server.Family==S->Family) {
            if (!Server.Port) Server.Port=S->Policy.StunPort;
            (void)MdoRudpDiscover(S->Raw,&Server,1000);
        }
    }
    if (S->Resolve && xrtFutureState(S->Resolve)!=XFUTURE_PENDING) {
        if (xrtFutureState(S->Resolve)==XFUTURE_RESOLVED && !MdoDeadlineExpired(S->MappingUntil)) {
            const xnetaddrlist* List=xrtFutureValue(S->Resolve);
            for (size_t i=0;i<xrtNetAddrListCount(List);++i) {
                xnetaddr Server=*xrtNetAddrListGet(List,i); Server.Port=S->Policy.StunPort;
                if (Server.Family==S->Family && MdoPeerCandidate(&Server) && MdoRudpDiscover(S->Raw,&Server,1000)) break;
            }
        }
        xrtFutureDestroy(S->Resolve); S->Resolve=NULL;
    }
    if (MdoDeadlineExpired(S->MappingUntil)) {
        if (S->Resolve) { (void)xrtFutureCancel(S->Resolve); xrtFutureDestroy(S->Resolve); S->Resolve=NULL; }
        MdoRudpCancelProbes(S->Raw); S->MappingComplete=true; return true;
    }
    (void)MdoRudpGetInfo(S->Raw,Info);
    S->MappingComplete=!S->Resolve && !Info->Discovery.Pending;
    return S->MappingComplete;
}
bool MdoPeerPoll(MdoPeerSession* S)
{
    if (!S || S->Info.Failed) return false;
    if (xrtCancelRequested(S->Cancel)) return MdoPeerFail(S,"cancelled");
    if (S->Info.Ready || S->Taken) return true;
    if (MdoDeadlineExpired(S->Until)) return MdoPeerFail(S,"attempt_timeout");
    MdoRudpInfo Info; if (!MdoRudpGetInfo(S->Raw,&Info) || Info.Closed) return MdoPeerFail(S,"endpoint_unavailable");
    if (!Info.Opened) return true;
    if (S->Initiator && !S->InitSent) {
        xvalue* V=MdoPeerMessage(S,"rudp_init"); char Public[65]; MdoPeerHex(S->Public,32,Public);
        bool Ok=V && MdoPeerTextSet(V,"public_key",Public) && xrtValueObjectSetNew(V,XRT_STR_LITERAL("family"),xrtValueUInt(S->Family));
        if (!Ok) { xrtValueRelease(V); return MdoPeerFail(S,"resources_unavailable"); }
        if (!MdoPeerSignal(S,V)) return MdoPeerFail(S,"signal_unavailable");
        S->InitSent=true;
    }
    bool MapReady=MdoPeerMap(S,&Info);
    if (!S->CandidatesSent && MapReady) {
        xvalue* List=MdoPeerCandidates(S,&Info), *V=MdoPeerMessage(S,S->Initiator?"rudp_candidates":"rudp_answer");
        if (!List || !V) { xrtValueRelease(List); xrtValueRelease(V); return MdoPeerFail(S,"no_candidates"); }
        if (!xrtValueObjectSetNew(V,XRT_STR_LITERAL("candidates"),List)) { xrtValueRelease(V); return MdoPeerFail(S,"resources_unavailable"); }
        if (!MdoPeerSignal(S,V)) return MdoPeerFail(S,"signal_unavailable");
        S->CandidatesSent=true;
    }
    if (S->RemoteReady && S->CandidatesSent && !S->Dialed) {
        /* Candidate fallback is owned by the transport, before application
         * admission; never rebuild or replay a confirmed application stream. */
        (void)MdoRudpWarmup(S->Raw,S->Candidates,S->Count,5000); S->Dialed=true;
        if (S->Initiator) {
            xnetaddr Ordered[8]; size_t Count=0;
            for (unsigned Kind=1;Kind<3;++Kind) for (size_t i=0;i<S->Count;++i)
                if (S->Kinds[i]==(Kind==1 ? 1u:0u)) Ordered[Count++]=S->Candidates[i];
            uint64 Remaining=MdoDeadlineRemainingUs(S->Until)/1000u;
            if (!Remaining || !MdoRudpDialCandidates(S->Raw,Ordered,Count,(uint32)Remaining)) return MdoPeerFail(S,"dial_unavailable");
        }
    }
    if (Info.Confirmed) S->Info.Ready=true;
    return true;
}
bool MdoPeerGetInfo(MdoPeerSession* S, MdoPeerInfo* Info)
{ if (!S || !Info) return false; *Info=S->Info; return true; }
MdoRemoteSocket* MdoPeerTakeSocket(MdoPeerSession* S)
{
    if (!S || !S->Info.Ready || S->Info.Failed || S->Taken) return NULL;
    MdoRemoteSocket* Socket=MdoRemoteSocketAdoptRudp(S->Raw);
    if (Socket) { S->Raw=NULL; S->Taken=true; } return Socket;
}
void MdoPeerSessionDestroy(MdoPeerSession* S)
{
    if (!S) return;
    xrtCancelRequest(S->Cancel);
    if (S->Resolve) { (void)xrtFutureCancel(S->Resolve); xrtFutureDestroy(S->Resolve); }
    MdoRudpDestroy(S->Raw); xrtCancelDestroy(S->Cancel); xrtSecureZero(S,sizeof(*S)); xrtFree(S);
}
#endif
