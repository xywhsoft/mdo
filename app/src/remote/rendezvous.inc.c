/* Authenticated rendezvous state belongs to the service owner, never a UDP
 * callback. Fixed limits and one-use identities apply before allocating peers. */
#if defined(XS_USE_XRUDP)
#include "rendezvous.h"
#define MDO_PEER_CANDIDATES 8u
#define MDO_PEER_ATTEMPT_US UINT64_C(10000000)
#define MDO_PEER_ALTERNATE_US UINT64_C(250000)
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
typedef struct MdoPeerLane {
    MdoRudpTransport* Raw;
    xfuture* Resolve;
    uint64 MappingUntil;
    xnetaddr Candidates[MDO_PEER_CANDIDATES];
    uint8 Kinds[MDO_PEER_CANDIDATES];
    size_t Count;
    xnetfamily Family;
    bool MappingStarted, MappingComplete, CandidatesSent, RemoteReady, Dialed, Failed;
} MdoPeerLane;
struct MdoPeerSession {
    MdoPeerInfo Info;
    MdoPeerPolicy Policy;
    MdoPeerLane Lanes[2];
    xcancel* Cancel;
    MdoPeerSignalProc Signal;
    void* SignalData;
    uint64 Until, AlternateAt;
    unsigned Families;
    xnetfamily Selected;
    unsigned char Public[32];
    bool Initiator, InitSent, SelectionAck, Taken;
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
    if (Policy->Local[0].Family || Policy->Local[1].Family)
        return (Policy->Local[0].Family==XNET_FAMILY_IPV4 ? 1u:0u) |
            (Policy->Local[1].Family==XNET_FAMILY_IPV6 ? 2u:0u);
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
static bool MdoPeerReadFamilies(const xvalue* Value, unsigned* Mask)
{
    *Mask=0;
    if (xrtValueType(Value)!=XVALUE_ARRAY || !xrtValueCount(Value) || xrtValueCount(Value)>2) return false;
    for (size_t i=0;i<xrtValueCount(Value);++i) {
        uint64 F=0; if (!MdoPeerUInt(xrtValueArrayGet(Value,i),&F) || (F!=4 && F!=6)) return false;
        unsigned Bit=F==4?1u:2u; if (*Mask&Bit) return false; *Mask|=Bit;
    }
    return true;
}
static xvalue* MdoPeerFamilyValue(unsigned Mask)
{
    xvalue* V=xrtValueArray(); if (!V) return NULL;
    if (((Mask&1u) && !xrtValueArrayAppendNew(V,xrtValueUInt(4))) ||
        ((Mask&2u) && !xrtValueArrayAppendNew(V,xrtValueUInt(6)))) { xrtValueRelease(V); return NULL; }
    return V;
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
static bool MdoPeerBindAuthority(const MdoPeerInfo* Info, MdoRudpConfig* Config)
{
    /* Bind authority explicitly, in addition to the fresh offer seed. A
     * modified mode/owner/runtime/peer/nonce/family cannot share this Noise
     * application context with the target's immutable relay-granted metadata. */
    xsha256 Hash; unsigned char Binding[32], Flags[2]={Info->ReadOnly?1u:0u,(uint8)Config->Local.Family};
    static const char Domain[]="mdo.peer.authority.v1";
    xrtSha256Init(&Hash);
    bool Bound=xrtSha256Update(&Hash,Domain,sizeof(Domain)-1) && xrtSha256Update(&Hash,Config->Binding,32) &&
        xrtSha256Update(&Hash,Info->Peer,32) && xrtSha256Update(&Hash,Info->Runtime,32) &&
        xrtSha256Update(&Hash,Info->Nonce,32) && xrtSha256Update(&Hash,Info->Owner,strlen(Info->Owner)+1) &&
        xrtSha256Update(&Hash,Flags,sizeof(Flags)) && xrtSha256Final(&Hash,Binding);
    xrtSecureZero(&Hash,sizeof(Hash));
    if (Bound) memcpy(Config->Binding,Binding,32);
    xrtSecureZero(Binding,sizeof(Binding));
    return Bound;
}
static bool MdoPeerLocal(MdoRudpConfig* C, const MdoPeerPolicy* P, xnetfamily Family)
{
    const xnetaddr* A=&P->Local[Family==XNET_FAMILY_IPV6 ? 1:0];
    if (A->Family) { if (A->Family!=Family) return false; C->Local=*A; return true; }
    return xrtNetAddrAny(&C->Local,Family,0);
}
static MdoPeerSession* MdoPeerStart(const MdoPeerInfo* Info, const MdoPeerPolicy* Policy,
    MdoRudpConfig* Config, unsigned Families, xcancel* Cancel, MdoPeerSignalProc Signal, void* Data)
{
    MdoPeerSession* S=xrtCalloc(1,sizeof(*S));
    if (!S) { xrtSecureZero(Config->Private,32); return NULL; }
    S->Info=*Info; S->Policy=*Policy; S->Initiator=!Config->Listen; S->Families=Families;
    S->Signal=Signal; S->SignalData=Data; S->Cancel=xrtCancelChild(Cancel);
    S->Until=MdoDeadlineAfterUs(MDO_PEER_ATTEMPT_US); S->AlternateAt=MdoDeadlineAfterUs(MDO_PEER_ALTERNATE_US);
    bool Any=false;
    if (S->Cancel && xrtX25519Public(Config->Private,S->Public)) for (unsigned i=0;i<2;++i) {
        MdoPeerLane* L=&S->Lanes[i]; if (!(Families&(1u<<i))) continue;
        L->Family=i ? XNET_FAMILY_IPV6:XNET_FAMILY_IPV4;
        MdoRudpConfig C=*Config;
        if (MdoPeerLocal(&C,Policy,L->Family) && MdoPeerBindAuthority(Info,&C)) L->Raw=MdoRudpStart(&C,S->Cancel);
        xrtSecureZero(&C,sizeof(C)); L->Failed=!L->Raw; Any|=L->Raw!=NULL;
    }
    xrtSecureZero(Config->Private,32);
    if (!Any) { MdoPeerSessionDestroy(S); return NULL; } return S;
}
MdoPeerSession* MdoPeerAccept(MdoPeerOffer* O, const xvalue* Init, const MdoPeerPolicy* Policy,
    xcancel* Cancel, MdoPeerSignalProc Signal, void* Data)
{
    cstr Type=MdoPeerText(Init,"type",24), Nonce=MdoPeerText(Init,"nonce",32), Runtime=MdoPeerText(Init,"runtime_id",32);
    unsigned Families=0; MdoRudpConfig C={0};
    if (!O || !Policy || !Signal || O->Used || MdoDeadlineExpired(O->Until) || xrtValueCount(Init)!=5 ||
        !Type || strcmp(Type,"rudp_init") || !Nonce || strcmp(Nonce,O->Info.Nonce) || !Runtime || strcmp(Runtime,O->Info.Runtime) ||
        !MdoPeerReadFamilies(xrtValueObjectGet(Init,XRT_STR_LITERAL("families")),&Families) ||
        (Families&~(O->Families&MdoPeerFamilies(Policy))) ||
        !MdoPeerUnhex(MdoPeerText(Init,"public_key",64),32,C.PeerPublic,true) ||
        !memcmp(C.PeerPublic,O->Public,32)) return NULL;
    memcpy(C.Private,O->Private,32); memcpy(C.Scope,O->Scope,16); memcpy(C.Binding,O->Binding,32);
    C.Listen=true; C.MessageLimit=MDO_RUDP_MESSAGE_MAX; O->Used=true; xrtSecureZero(O->Private,32);
    return MdoPeerStart(&O->Info,Policy,&C,Families,Cancel,Signal,Data);
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
        !MdoPeerReadFamilies(Families,&Mask)) return NULL;
    Mask&=MdoPeerFamilies(Policy); if (!Mask) return NULL;
    strcpy(Info.Nonce,MdoPeerText(Offer,"nonce",32));
    if (!xrtSecureRandom(C.Private,32)) { xrtSecureZero(C.Private,32); return NULL; }
    C.MessageLimit=MDO_RUDP_MESSAGE_MAX; return MdoPeerStart(&Info,Policy,&C,Mask,Cancel,Signal,Data);
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
    uint64 Family=0;
    if (!MdoPeerUInt(xrtValueObjectGet(V,XRT_STR_LITERAL("family")),&Family) || (Family!=4 && Family!=6) ||
        !(S->Families&(Family==4?1u:2u))) return false;
    if (!strcmp(Type,S->Initiator?"rudp_selected":"rudp_select")) {
        if (xrtValueCount(V)!=4 || (S->Selected && S->Selected!=(xnetfamily)Family) ||
            (S->Initiator && !S->Selected)) return false;
        S->Selected=(xnetfamily)Family;
        if (S->Initiator) S->SelectionAck=true;
        return true; /* Exact repeats never change the selected family or deadline. */
    }
    if (strcmp(Type,S->Initiator?"rudp_answer":"rudp_candidates") || xrtValueCount(V)!=5) return false;
    MdoPeerLane* L=&S->Lanes[Family==6 ? 1:0];
    const xvalue* List=xrtValueObjectGet(V,XRT_STR_LITERAL("candidates"));
    size_t Count=xrtValueCount(List); xnetaddr Addresses[8]; uint8 Kinds[8];
    size_t Limit=S->Families==3u ? MDO_PEER_CANDIDATES/2u:MDO_PEER_CANDIDATES;
    if (xrtValueType(List)!=XVALUE_ARRAY || !Count || Count>Limit) return false;
    for (size_t i=0;i<Count;++i) {
        const xvalue* Item=xrtValueArrayGet(List,i); cstr Ip=MdoPeerText(Item,"ip",64), Kind=MdoPeerText(Item,"kind",8); uint64 Port=0;
        if (xrtValueType(Item)!=XVALUE_OBJECT || xrtValueCount(Item)!=3 || !Ip || !Kind || (strcmp(Kind,"host") && strcmp(Kind,"srflx")) ||
            !MdoPeerUInt(xrtValueObjectGet(Item,XRT_STR_LITERAL("port")),&Port) || !Port || Port>65535 ||
            !xrtNetAddrParse(&Addresses[i],Ip,(uint16)Port) || Addresses[i].Family!=(xnetfamily)Family || !MdoPeerCandidate(&Addresses[i])) return false;
        Kinds[i]=!strcmp(Kind,"srflx")?1u:0u;
        for (size_t j=0;j<i;++j) if (xrtNetAddrEqual(&Addresses[i],&Addresses[j])) return false;
    }
    if (L->RemoteReady) {
        if (L->Count!=Count) return false;
        for (size_t i=0;i<Count;++i) if (Kinds[i]!=L->Kinds[i] || !xrtNetAddrEqual(&Addresses[i],&L->Candidates[i])) return false;
        return true; /* Exact duplicate, no deadline renewal or new targets. */
    }
    if (S->Selected && S->Selected!=(xnetfamily)Family) return true; /* A late loser cannot restart work. */
    memcpy(L->Candidates,Addresses,Count*sizeof(*Addresses)); memcpy(L->Kinds,Kinds,Count); L->Count=Count; L->RemoteReady=true; return true;
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
static xvalue* MdoPeerCandidates(MdoPeerSession* S, MdoPeerLane* L, const MdoRudpInfo* Info)
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
                    if (A.Family==L->Family && !MdoPeerAppendCandidate(List,&A,"host")) Ok=false;
                }
            }
            xrtNetInterfacesFree(&Interfaces); if (!Ok) goto fail;
        }
    }
    if (S->Families==3u) while (xrtValueCount(List)>MDO_PEER_CANDIDATES/2u)
        xrtValueArrayRemove(List,xrtValueCount(List)-1,1);
    if (xrtValueCount(List)) return List;
fail:
    xrtValueRelease(List); return NULL;
}
static bool MdoPeerMap(MdoPeerSession* S, MdoPeerLane* L, MdoRudpInfo* Info)
{
    if (L->MappingComplete) return true;
    if (!L->MappingStarted) {
        L->MappingStarted=true; L->MappingUntil=MdoDeadlineAfterUs(UINT64_C(1500000));
        xnetaddr Server=S->Policy.Stun[L->Family==XNET_FAMILY_IPV6 ? 1:0];
        if (!Server.Family && S->Policy.StunHost[0] && !xrtNetAddrParse(&Server,S->Policy.StunHost,S->Policy.StunPort) && S->Policy.Resolver)
            L->Resolve=xrtNetResolveAsync(S->Policy.Resolver,S->Policy.StunHost,L->Family);
        if (Server.Family==L->Family) {
            if (!Server.Port) Server.Port=S->Policy.StunPort;
            (void)MdoRudpDiscover(L->Raw,&Server,1000);
        }
    }
    if (L->Resolve && xrtFutureState(L->Resolve)!=XFUTURE_PENDING) {
        if (xrtFutureState(L->Resolve)==XFUTURE_RESOLVED && !MdoDeadlineExpired(L->MappingUntil)) {
            const xnetaddrlist* List=xrtFutureValue(L->Resolve);
            for (size_t i=0;i<xrtNetAddrListCount(List);++i) {
                xnetaddr Server=*xrtNetAddrListGet(List,i); Server.Port=S->Policy.StunPort;
                if (Server.Family==L->Family && MdoPeerCandidate(&Server) && MdoRudpDiscover(L->Raw,&Server,1000)) break;
            }
        }
        xrtFutureDestroy(L->Resolve); L->Resolve=NULL;
    }
    if (MdoDeadlineExpired(L->MappingUntil)) {
        if (L->Resolve) { (void)xrtFutureCancel(L->Resolve); xrtFutureDestroy(L->Resolve); L->Resolve=NULL; }
        MdoRudpCancelProbes(L->Raw); L->MappingComplete=true; return true;
    }
    (void)MdoRudpGetInfo(L->Raw,Info);
    L->MappingComplete=!L->Resolve && !Info->Discovery.Pending;
    return L->MappingComplete;
}
static void MdoPeerLaneDrop(MdoPeerLane* L)
{
    if (L->Resolve) { (void)xrtFutureCancel(L->Resolve); xrtFutureDestroy(L->Resolve); L->Resolve=NULL; }
    MdoRudpDestroy(L->Raw); L->Raw=NULL; L->Failed=true;
}
static bool MdoPeerLanePoll(MdoPeerSession* S, MdoPeerLane* L)
{
    if (!L->Raw || L->Failed) return false;
    MdoRudpInfo Info;
    if (!MdoRudpGetInfo(L->Raw,&Info) || Info.Closed || Info.PeerClosed) { MdoPeerLaneDrop(L); return false; }
    if (!Info.Opened) return true;
    if (S->Initiator && !S->InitSent) {
        xvalue* V=MdoPeerMessage(S,"rudp_init"); char Public[65]; MdoPeerHex(S->Public,32,Public);
        bool Ok=V && MdoPeerTextSet(V,"public_key",Public) &&
            xrtValueObjectSetNew(V,XRT_STR_LITERAL("families"),MdoPeerFamilyValue(S->Families));
        if (!Ok) { xrtValueRelease(V); return MdoPeerFail(S,"resources_unavailable"); }
        if (!MdoPeerSignal(S,V)) return MdoPeerFail(S,"signal_unavailable");
        S->InitSent=true;
    }
    bool MapReady=MdoPeerMap(S,L,&Info);
    if (!L->CandidatesSent && MapReady) {
        xvalue* List=MdoPeerCandidates(S,L,&Info), *V=MdoPeerMessage(S,S->Initiator?"rudp_candidates":"rudp_answer");
        if (!List) { xrtValueRelease(V); MdoPeerLaneDrop(L); return false; }
        if (!V) { xrtValueRelease(List); return MdoPeerFail(S,"resources_unavailable"); }
        bool Ok=xrtValueObjectSetNew(V,XRT_STR_LITERAL("candidates"),List) &&
            xrtValueObjectSetNew(V,XRT_STR_LITERAL("family"),xrtValueUInt(L->Family));
        if (!Ok) { xrtValueRelease(V); return MdoPeerFail(S,"resources_unavailable"); }
        if (!MdoPeerSignal(S,V)) return MdoPeerFail(S,"signal_unavailable");
        L->CandidatesSent=true;
    }
    if (L->RemoteReady && L->CandidatesSent && !L->Dialed && (!S->Initiator || L->Family!=XNET_FAMILY_IPV4 ||
        !(S->Families&2u) || S->Lanes[1].Failed || MdoDeadlineExpired(S->AlternateAt))) {
        /* Candidate fallback is owned by the transport, before application
         * admission; never rebuild or replay a confirmed application stream. */
        (void)MdoRudpWarmup(L->Raw,L->Candidates,L->Count,5000); L->Dialed=true;
        if (S->Initiator) {
            xnetaddr Ordered[8]; size_t Count=0;
            for (unsigned Kind=1;Kind<3;++Kind) for (size_t i=0;i<L->Count;++i)
                if (L->Kinds[i]==(Kind==1 ? 1u:0u)) Ordered[Count++]=L->Candidates[i];
            uint64 Remaining=MdoDeadlineRemainingUs(S->Until)/1000u;
            if (!Remaining || !MdoRudpDialCandidates(L->Raw,Ordered,Count,(uint32)Remaining)) {
                MdoPeerLaneDrop(L); return false;
            }
        }
    }
    return true;
}
static bool MdoPeerSelectionSignal(MdoPeerSession* S, cstr Type)
{
    xvalue* V=MdoPeerMessage(S,Type);
    if (!V) return false;
    if (!xrtValueObjectSetNew(V,XRT_STR_LITERAL("family"),xrtValueUInt(S->Selected))) { xrtValueRelease(V); return false; }
    return MdoPeerSignal(S,V);
}
bool MdoPeerPoll(MdoPeerSession* S)
{
    if (!S || S->Info.Failed) return false;
    if (xrtCancelRequested(S->Cancel)) return MdoPeerFail(S,"cancelled");
    if (S->Info.Ready || S->Taken) return true;
    if (MdoDeadlineExpired(S->Until)) return MdoPeerFail(S,"attempt_timeout");
    bool Any=false;
    /* IPv6 gets a bounded head start; neither the other family nor slow DNS
     * can renew the attempt. Only the initiator chooses the confirmed lane. */
    for (unsigned n=0;n<2;++n) {
        MdoPeerLane* L=&S->Lanes[1u-n];
        if (S->Selected && L->Family!=S->Selected) { MdoPeerLaneDrop(L); continue; }
        if (MdoPeerLanePoll(S,L)) Any=true;
        if (S->Info.Failed) return false;
        MdoRudpInfo Info;
        if (!S->Selected && S->Initiator && L->Raw && MdoRudpGetInfo(L->Raw,&Info) && Info.Confirmed && !Info.Closed && !Info.PeerClosed) {
            S->Selected=L->Family;
            if (!MdoPeerSelectionSignal(S,"rudp_select")) return MdoPeerFail(S,"signal_unavailable");
        }
    }
    if (!Any) return MdoPeerFail(S,"endpoint_unavailable");
    if (S->Selected) {
        unsigned Index=S->Selected==XNET_FAMILY_IPV6 ? 1u:0u;
        MdoPeerLaneDrop(&S->Lanes[1u-Index]);
        MdoPeerLane* L=&S->Lanes[Index]; MdoRudpInfo Info;
        if (!L->Raw || !MdoRudpGetInfo(L->Raw,&Info) || Info.Closed || Info.PeerClosed) return MdoPeerFail(S,"selected_endpoint_unavailable");
        if (Info.Confirmed) {
            if (!S->Initiator && !S->SelectionAck) {
                if (!MdoPeerSelectionSignal(S,"rudp_selected")) return MdoPeerFail(S,"signal_unavailable");
                S->SelectionAck=true;
            }
            if (S->SelectionAck) { S->Info.Family=S->Selected; S->Info.Ready=true; }
        }
    }
    return true;
}
bool MdoPeerGetInfo(MdoPeerSession* S, MdoPeerInfo* Info)
{ if (!S || !Info) return false; *Info=S->Info; return true; }
MdoRemoteSocket* MdoPeerTakeSocket(MdoPeerSession* S)
{
    if (!S || !S->Info.Ready || S->Info.Failed || S->Taken) return NULL;
    MdoPeerLane* L=&S->Lanes[S->Selected==XNET_FAMILY_IPV6 ? 1:0];
    MdoRemoteSocket* Socket=MdoRemoteSocketAdoptRudp(L->Raw);
    if (Socket) { L->Raw=NULL; S->Taken=true; } return Socket;
}
void MdoPeerSessionDestroy(MdoPeerSession* S)
{
    if (!S) return;
    xrtCancelRequest(S->Cancel);
    for (unsigned i=0;i<2;++i) MdoPeerLaneDrop(&S->Lanes[i]);
    xrtCancelDestroy(S->Cancel); xrtSecureZero(S,sizeof(*S)); xrtFree(S);
}
#endif
