#include "../../include/mdo/time.h"
#include "rendezvous.h"
/* Browser/WebView only talks to its own loopback xs. The native gateway
 * verifies the leaf against the ephemeral CA learned through the account relay,
 * preserving TLS on the LAN without installing a machine/browser CA. */
typedef struct MdoLanGateway {
    MdoRemoteSocket *Browser,*Target; MdoRemoteNet Net; XS_ServerInfo* Server; xcancel* Cancel; bool Opened;
    MdoAccountLease Lease;
#if defined(XS_USE_XRUDP)
    MdoPeerSession* Rudp;
#endif
} MdoLanGateway;
static struct { xmutex* Lock; xtaskpool* Pool; xfuture* Futures[4]; xcancel* Stop; XS_ServerInfo* Server; bool Stopping; } g_MdoLanGateway;
static bool MdoLanGatewayTarget(bool Binary, xbytesview Bytes, void* Data)
{ return MdoRemoteSocketSend(((MdoLanGateway*)Data)->Browser,Binary,Bytes); }
#if defined(XS_USE_XRUDP)
static bool MdoLanGatewaySignal(const xvalue* Signal, void* Data)
{
    MdoLanGateway* Gateway=Data; xvalue* V=xrtValueObject();
    bool Ok=V && MdoAccountSetString(V,"type","rudp_forward") &&
        xrtValueObjectSetNew(V,XRT_STR_LITERAL("payload"),xrtValueDeepClone(Signal));
    size_t Size=0; char* Json=Ok ? xrtJsonStringify(V,false,&Size):NULL;
    Ok=Json && Size<=4096 && MdoRemoteSocketSend(Gateway->Browser,false,(xbytesview){(const uint8*)Json,Size});
    xrtFree(Json); xrtValueRelease(V); return Ok;
}
#endif
static bool MdoLanGatewayBrowser(bool Binary, xbytesview Bytes, void* Data)
{
    MdoLanGateway* Gateway=Data;
#if defined(XS_USE_XRUDP)
    if (Gateway->Rudp && !Binary && Bytes.Size<=4096) {
        xjsonreadconfig Limits; xrtJsonReadConfigInit(&Limits); Limits.MaxInputBytes=4096; Limits.MaxValues=128; Limits.MaxDepth=5;
        xvalue* V=xrtJsonRead(xrtStrViewN((cstr)Bytes.Data,Bytes.Size),&Limits); cstr Type=MdoAccountText(V,"type",24);
        if (Type && !strcmp(Type,"rudp_signal")) {
            bool Ok=xrtValueCount(V)==2 && MdoPeerInput(Gateway->Rudp,xrtValueObjectGet(V,XRT_STR_LITERAL("payload")));
            xrtValueRelease(V); return Ok;
        }
        xrtValueRelease(V);
    }
#endif
    if (Gateway->Target) return MdoRemoteSocketSend(Gateway->Target,Binary,Bytes);
#if defined(XS_USE_XRUDP)
    if (Gateway->Rudp) return false;
#endif
    if (Gateway->Opened || Binary || Bytes.Size>4096u) return false;
    Gateway->Opened=true;
    xjsonreadconfig Limits; xrtJsonReadConfigInit(&Limits); Limits.MaxInputBytes=4096u; Limits.MaxValues=32u; Limits.MaxDepth=3u;
    xvalue* Value=xrtJsonRead(xrtStrViewN((cstr)Bytes.Data,Bytes.Size),&Limits);
#if defined(XS_USE_XRUDP)
    cstr Transport=MdoAccountText(Value,"transport",16);
    if (Transport && !strcmp(Transport,"xrudp")) {
        bool Ready=xrtValueType(Value)==XVALUE_OBJECT && xrtValueCount(Value)==2 &&
            MdoRemoteNetInit(&Gateway->Net,Gateway->Server->Engine,NULL);
        MdoPeerPolicy Policy; MdoPeerPolicyInit(&Policy,&Gateway->Net,MDO_ACCOUNT_SERVICE_ORIGIN);
        if (Ready) Gateway->Rudp=MdoPeerInitiate(xrtValueObjectGet(Value,XRT_STR_LITERAL("offer")),Gateway->Lease.MemberId,
            &Policy,Gateway->Lease.Cancel,MdoLanGatewaySignal,Gateway);
        xrtValueRelease(Value); return Gateway->Rudp!=NULL;
    }
#endif
    cstr Certificate=MdoAccountText(Value,"certificate",1536u), Token=MdoAccountText(Value,"token",64u), Peer=MdoAccountText(Value,"peer_id",32u);
    const xvalue* Addresses=xrtValueObjectGet(Value,XRT_STR_LITERAL("addresses")); uint64 Port=0u, Version=0u;
    uint8 Der[1024]; size_t Size=0u; xx509store* Trust=NULL; bool Ok=false;
    if (xrtValueType(Value)!=XVALUE_OBJECT || xrtValueCount(Value)!=6u || !MdoAccountHex(Token,64u) || !MdoRemoteIdValid(Peer) ||
        !MdoAccountGetUInt(xrtValueObjectGet(Value,XRT_STR_LITERAL("version")),&Version) || Version!=1u ||
        !MdoAccountGetUInt(xrtValueObjectGet(Value,XRT_STR_LITERAL("port")),&Port) || !Port || Port>65535u ||
        xrtValueType(Addresses)!=XVALUE_ARRAY || !xrtValueCount(Addresses) || xrtValueCount(Addresses)>4u || !Certificate ||
        !xrtBase64Decode(Certificate,strlen(Certificate),Der,sizeof(Der),&Size,NULL)) goto done;
    for (size_t i=0u;i<xrtValueCount(Addresses);++i)
        if (!MdoLanAddress(MdoLanText(xrtValueArrayGet(Addresses,i)))) goto done;
    Trust=xrtX509StoreCreate();
    if (!Trust || xrtX509StoreAdd(Trust,Der,Size)<0 || !MdoRemoteNetInit(&Gateway->Net,Gateway->Server->Engine,Trust)) goto done;
    for (size_t i=0u;i<xrtValueCount(Addresses) && !Gateway->Target && !xrtCancelRequested(Gateway->Lease.Cancel);++i) {
        char Offers[128]; snprintf(Offers,sizeof(Offers),MDO_LAN_PROTOCOL ", mdo.grant.%s",Token);
        MdoRemoteSocketConfig Config={MdoLanText(xrtValueArrayGet(Addresses,i)),(uint16)Port,true,"/mdo-lan","http://mdo.local",
            Offers,MDO_LAN_PROTOCOL,262123u}; uint16 Status=0u;
        Gateway->Target=MdoRemoteSocketOpenTrusted(&Gateway->Net,&Config,Gateway->Lease.Cancel,&Status,"mdo-lan",1200000u);
        xrtSecureZero(Offers,sizeof(Offers));
    }
    Ok=Gateway->Target!=NULL;
done:
    xrtX509StoreFree(Trust); MdoAccountSecretValueRelease(Value); return Ok;
}
static xtaskoutcome MdoLanGatewayRun(xcancel* Cancel, ptr Data, xtaskvalue* Value)
{
    (void)Cancel; (void)Value;
    MdoLanGateway* Gateway=Data; uint64 Until=MdoDeadlineAfterUs(2000000u);
    /* A retained browser WS cannot outlive the controller's local account.
     * This is the existing native logout/account-switch cancellation authority;
     * no token is sent on the LAN or added to the handshake. */
    if (!MdoAccountAcquireService(Gateway->Cancel,&Gateway->Lease)) return XTASK_FAILED;
    while (!xrtCancelRequested(Gateway->Lease.Cancel) && (Gateway->Opened || !MdoDeadlineExpired(Until)) &&
        MdoRemoteSocketPoll(Gateway->Browser,MdoLanGatewayBrowser,Gateway) &&
        (!Gateway->Target || MdoRemoteSocketPoll(Gateway->Target,MdoLanGatewayTarget,Gateway))) {
#if defined(XS_USE_XRUDP)
        if (Gateway->Rudp && !Gateway->Target) {
            MdoPeerInfo Info;
            if (!MdoPeerPoll(Gateway->Rudp)) break;
            if (MdoPeerGetInfo(Gateway->Rudp,&Info) && Info.Ready) {
                Gateway->Target=MdoPeerTakeSocket(Gateway->Rudp); if (!Gateway->Target) break;
            }
        }
#endif
        xrtSleep(5u);
    }
    return XTASK_SUCCESS;
}
static void MdoLanGatewayDrop(ptr Value, ptr Data)
{
    (void)Data; MdoLanGateway* Gateway=Value;
    MdoRemoteSocketDestroy(Gateway->Target);
#if defined(XS_USE_XRUDP)
    MdoPeerSessionDestroy(Gateway->Rudp);
#endif
    MdoRemoteSocketDestroy(Gateway->Browser); MdoRemoteNetUnit(&Gateway->Net);
    MdoAccountRelease(&Gateway->Lease);
    xrtCancelDestroy(Gateway->Cancel); xsServerRelease(Gateway->Server); xrtFree(Gateway);
}
bool MdoLanGatewayInit(XS_ServerInfo* Server)
{
    if (g_MdoLanGateway.Lock) return true;
    g_MdoLanGateway.Lock=xrtMutexCreate(); g_MdoLanGateway.Stop=xrtCancelCreate(); g_MdoLanGateway.Server=xsServerRetain(Server);
    if (g_MdoLanGateway.Lock && g_MdoLanGateway.Stop && g_MdoLanGateway.Server) return true;
    MdoLanGatewayUnit(); return false;
}
void MdoLanGatewayUnit(void)
{
    if (g_MdoLanGateway.Lock) {
        xrtMutexLock(g_MdoLanGateway.Lock); g_MdoLanGateway.Stopping=true; xrtCancelRequest(g_MdoLanGateway.Stop);
        xrtMutexUnlock(g_MdoLanGateway.Lock);
    }
    if (g_MdoLanGateway.Pool) { xrtTaskPoolCancel(g_MdoLanGateway.Pool); xrtTaskPoolWait(g_MdoLanGateway.Pool); xrtTaskPoolDestroy(g_MdoLanGateway.Pool); }
    for (size_t i=0u;i<4u;++i) xrtFutureDestroy(g_MdoLanGateway.Futures[i]);
    xsServerRelease(g_MdoLanGateway.Server); xrtCancelDestroy(g_MdoLanGateway.Stop); xrtMutexDestroy(g_MdoLanGateway.Lock);
    memset(&g_MdoLanGateway,0,sizeof(g_MdoLanGateway));
}
bool MdoLanGatewayAccept(XS_HttpReq* Request)
{
    if (!g_MdoLanGateway.Lock) return false;
    xrtMutexLock(g_MdoLanGateway.Lock); size_t Slot=4u; bool Ok=false;
    for (size_t i=0u;i<4u;++i) if (!g_MdoLanGateway.Futures[i] || xrtFutureState(g_MdoLanGateway.Futures[i])!=XFUTURE_PENDING) { Slot=i; break; }
    if (Slot==4u || g_MdoLanGateway.Stopping) goto done;
    if (!g_MdoLanGateway.Pool) { xtaskpoolconfig Config={0}; Config.Threads=4u; Config.QueueLimit=4u; g_MdoLanGateway.Pool=xrtTaskPoolCreate(&Config); }
    if (!g_MdoLanGateway.Pool) goto done;
    MdoLanGateway* Gateway=xrtCalloc(1u,sizeof(*Gateway)); if (!Gateway) goto done;
    Gateway->Cancel=xrtCancelChild(g_MdoLanGateway.Stop); Gateway->Server=xsServerRetain(g_MdoLanGateway.Server);
    if (Gateway->Cancel && Gateway->Server) Gateway->Browser=MdoRemoteSocketAcceptHttp(Request,"mdo.direct.v1",Gateway->Cancel);
    if (!Gateway->Browser) { MdoLanGatewayDrop(Gateway,NULL); goto done; }
    xrtFutureDestroy(g_MdoLanGateway.Futures[Slot]); g_MdoLanGateway.Futures[Slot]=NULL;
    xtaskargs Args={0}; Args.Destroy=MdoLanGatewayDrop;
    g_MdoLanGateway.Futures[Slot]=xrtTaskSubmit(g_MdoLanGateway.Pool,MdoLanGatewayRun,Gateway,&Args);
    /* The upgrade already owns the stream, even if task submission fails. */
    Ok=true; if (!g_MdoLanGateway.Futures[Slot]) MdoLanGatewayDrop(Gateway,NULL);
done:
    xrtMutexUnlock(g_MdoLanGateway.Lock); return Ok;
}
