/* The native callback closures below are owned by our private Engine, never
 * installed on the host Engine. No browser/RPC callback runs on its worker. */
#if defined(XS_USE_XRUDP)
#ifndef XRUDP_MODULE_RUDP_ENDPOINT
#define XRUDP_MODULE_RUDP_ENDPOINT
#endif
#include <xrudp.h>
#include "rudp.h"
#define MDO_RUDP_HEADER 8u
#define MDO_RUDP_NATIVE_BYTES (4u * 1024u * 1024u)
#define MDO_RUDP_IO_BURST 16u
#define MDO_RUDP_IO_CHUNK 16384u

typedef struct MdoRudpRecord {
    struct MdoRudpRecord* Next;
    size_t Size, Offset;
    unsigned char Header[MDO_RUDP_HEADER];
    unsigned char Bytes[];
} MdoRudpRecord;
typedef struct MdoRudpQueue {
    MdoRudpRecord *Head, *Tail;
    size_t Count, Bytes;
} MdoRudpQueue;
struct MdoRudpTransport {
    xmutex* Lock;
    xcond* Changed;
    xcancel* Cancel;
    xnetengine* Engine;
    xrudpendpoint* Endpoint;
    xrudppeer* Peer;
    MdoRudpConfig Config;
    MdoRudpInfo Info;
    xnetaddr Remote;
    unsigned char Context[32], Header[MDO_RUDP_HEADER];
    unsigned char* Incoming;
    size_t HeaderSize, IncomingSize, Expected;
    MdoRudpQueue Tx, Rx;
    bool Counted, Started, InitPosted, Initialized, Opened, EndpointClosed;
    bool Stopping, Failed, Confirmed, ReadEnded, WakePending, ConnectPending, ConnectIssued;
};
static xatomic32 g_MdoRudpTransports;
static void MdoRudpPump(MdoRudpTransport* R);

static bool MdoRudpNonzero(const unsigned char* Bytes, size_t Size)
{
    unsigned char Any=0;
    for (size_t i=0;i<Size;++i) Any|=Bytes[i];
    return Any!=0;
}
static void MdoRudpRecordFree(MdoRudpRecord* Record)
{
    if (Record) { xrtSecureZero(Record,sizeof(*Record)+Record->Size); xrtFree(Record); }
}
static void MdoRudpQueuePush(MdoRudpQueue* Queue, MdoRudpRecord* Record)
{
    Record->Next=NULL;
    if (Queue->Tail) Queue->Tail->Next=Record; else Queue->Head=Record;
    Queue->Tail=Record; ++Queue->Count; Queue->Bytes+=sizeof(*Record)+Record->Size;
}
static MdoRudpRecord* MdoRudpQueuePop(MdoRudpQueue* Queue)
{
    MdoRudpRecord* Record=Queue->Head;
    if (Record) {
        Queue->Head=Record->Next; if (!Queue->Head) Queue->Tail=NULL;
        --Queue->Count; Queue->Bytes-=sizeof(*Record)+Record->Size; Record->Next=NULL;
    }
    return Record;
}
static bool MdoRudpQueueRoom(const MdoRudpQueue* Queue, size_t Size)
{
    return Queue->Count<MDO_RUDP_QUEUE_RECORDS &&
        sizeof(MdoRudpRecord)+Size<=MDO_RUDP_QUEUE_BYTES-Queue->Bytes;
}
static void MdoRudpFail(MdoRudpTransport* R, uint16 Code, xrudpresult Reason)
{
    xrudpendpoint* Endpoint;
    xrtMutexLock(R->Lock);
    if (!R->Failed) { R->Failed=true; R->Info.CloseCode=Code; R->Info.NativeReason=Reason; }
    Endpoint=R->Endpoint;
    xrtCondBroadcast(R->Changed); xrtMutexUnlock(R->Lock);
    if (Endpoint) (void)xrtRudpEndpointClose(Endpoint);
}
static void MdoRudpOpened(xrudpendpoint* Endpoint, void* Data)
{
    MdoRudpTransport* R=Data; xrudpendpointinfo Info;
    if (xrtRudpEndpointInfo(Endpoint,&Info)!=XRUDP_OK) { MdoRudpFail(R,1006,XRUDP_IO_ERROR); return; }
    xrtMutexLock(R->Lock);
    R->Info.Local=Info.Local; memcpy(R->Info.Public,Info.Public,32); memcpy(R->Info.Scope,Info.Scope,16);
    R->Info.NativeReservedBytes=Info.ReservedBytes; R->Opened=true;
    xrtCondBroadcast(R->Changed); xrtMutexUnlock(R->Lock);
}
static void MdoRudpConfirmed(xrudpendpoint* Endpoint, xrudppeer* Peer, void* Data)
{
    MdoRudpTransport* R=Data; bool Extra=false; xrudpendpointinfo Info={0};
    (void)xrtRudpEndpointInfo(Endpoint,&Info);
    xrtMutexLock(R->Lock);
    if (R->Stopping || R->Failed || (R->Peer && R->Peer!=Peer)) Extra=true;
    else {
        if (!R->Peer) R->Peer=xrtRudpPeerRef(Peer);
        R->Confirmed=true; R->Info.NativeReservedBytes=Info.ReservedBytes;
    }
    xrtCondBroadcast(R->Changed); xrtMutexUnlock(R->Lock);
    if (Extra) (void)xrtRudpPeerAbort(Peer); else MdoRudpPump(R);
}
static void MdoRudpPeerClosed(xrudpendpoint* Endpoint, xrudppeer* Peer, xrudpresult Reason, void* Data)
{
    MdoRudpTransport* R=Data; bool Ours;
    (void)Endpoint;
    xrtMutexLock(R->Lock); Ours=R->Peer==Peer;
    xrtMutexUnlock(R->Lock);
    /* Failed, unconfirmed listener handshakes do not kill the pinned listener. */
    if (Ours) MdoRudpFail(R,1006,Reason);
}
static void MdoRudpClosed(xrudpendpoint* Endpoint, xrudpresult Reason, const xerror* Error, void* Data)
{
    MdoRudpTransport* R=Data;
    (void)Endpoint; (void)Error;
    xrtMutexLock(R->Lock); R->EndpointClosed=true;
    if (!R->Stopping && !R->Failed) {
        R->Failed=true; R->Info.CloseCode=1006; R->Info.NativeReason=Reason;
    }
    xrtCondBroadcast(R->Changed); xrtMutexUnlock(R->Lock);
}
static void MdoRudpChanged(xrudpendpoint* Endpoint, xrudppeer* Peer, uint32 Events, void* Data)
{
    MdoRudpTransport* R=Data;
    (void)Endpoint; (void)Events;
    if (Peer==R->Peer) MdoRudpPump(R);
}
static void MdoRudpBindWorker(xnetworker* Worker, ptr Data)
{
    MdoRudpTransport* R=Data; xrudpendpointconfig Config;
    xrudpendpointevents Events={0}; xrudpendpoint* Endpoint=NULL;
    bool Stop;
    xrtMutexLock(R->Lock); Stop=R->Stopping; xrtMutexUnlock(R->Lock);
    xrudpresult Result=XRUDP_CANCELLED;
    if (!Stop) {
        xrtRudpEndpointConfigInit(&Config);
        Config.StaticPrivate=R->Config.Private; Config.PeerPublic=R->Config.PeerPublic;
        Config.Scope=R->Config.Scope; Config.ApplicationContext=R->Context;
        Config.Listen=R->Config.Listen; Config.MaxPeers=Config.MaxHalfOpen=1;
        Config.HandshakesPerSecond=4; Config.AddressHandshakesPerSecond=2;
        Config.HandshakeTimeoutUs=UINT64_C(3000000); Config.MemoryLimit=MDO_RUDP_NATIVE_BYTES;
        Config.Session.StreamCapacity=Config.Session.Local.MaxStreams=1;
        Config.Session.ReceiveCapacity=Config.Session.TransmitCapacity=256u*1024u;
        Config.Session.Local.StreamCredit=256u*1024u; Config.Session.Local.ConnectionCredit=256u*1024u;
        Config.Session.PacketCapacity=128; Config.Session.MaximumWindow=256u*1024u;
        Config.Session.Local.IdleTimeoutMs=60000; Config.Session.ProgressTimeoutUs=UINT64_C(10000000);
        Events.Open=MdoRudpOpened; Events.Confirmed=MdoRudpConfirmed; Events.Changed=MdoRudpChanged;
        Events.PeerClose=MdoRudpPeerClosed; Events.Close=MdoRudpClosed;
        Result=xrtRudpEndpointBind(Worker,&R->Config.Local,&Config,&Events,R,&Endpoint);
    }
    xrtMutexLock(R->Lock); R->Endpoint=Endpoint; R->Initialized=true; Stop=R->Stopping;
    xrtSecureZero(R->Config.Private,sizeof(R->Config.Private));
    if (Result!=XRUDP_OK) { R->Failed=true; R->Info.NativeReason=Result; R->Info.CloseCode=1006; }
    xrtCondBroadcast(R->Changed); xrtMutexUnlock(R->Lock);
    if (Stop && Endpoint) (void)xrtRudpEndpointClose(Endpoint);
}
static void MdoRudpWakeWorker(xnetworker* Worker, ptr Data)
{
    MdoRudpTransport* R=Data; xnetaddr Remote; bool Connect, Stop;
    (void)Worker;
    xrtMutexLock(R->Lock); R->WakePending=false;
    Connect=R->ConnectPending; R->ConnectPending=false; Remote=R->Remote;
    Stop=R->Stopping || R->Failed; xrtMutexUnlock(R->Lock);
    if (Stop) return;
    if (Connect) {
        xrudpconnectconfig Config={R->Config.PeerPublic,R->Config.Scope,R->Context}; xrudppeer* Peer=NULL;
        xrudpresult Result=xrtRudpEndpointConnect(R->Endpoint,&Remote,&Config,&Peer);
        if (Result!=XRUDP_OK) { MdoRudpFail(R,1006,Result); return; }
        xrtMutexLock(R->Lock); R->Peer=Peer; xrtMutexUnlock(R->Lock);
    }
    MdoRudpPump(R);
}
static void MdoRudpKick(MdoRudpTransport* R)
{
    bool Post=false;
    xrtMutexLock(R->Lock);
    if (!R->Stopping && !R->Failed && !R->WakePending) { R->WakePending=true; Post=true; }
    xrtMutexUnlock(R->Lock);
    if (Post && !xrtNetEnginePost(R->Engine,0,MdoRudpWakeWorker,R)) {
        xrtMutexLock(R->Lock); R->WakePending=false; xrtMutexUnlock(R->Lock);
        /* One coalesced command at most. Poll/waits retry bounded admission. */
    }
}
static bool MdoRudpWorkerLive(MdoRudpTransport* R)
{
    bool Live;
    xrtMutexLock(R->Lock); Live=R->Confirmed && !R->Failed && !R->Stopping;
    xrtMutexUnlock(R->Lock); return Live;
}
static void MdoRudpPump(MdoRudpTransport* R)
{
    if (!MdoRudpWorkerLive(R)) return;
    for (unsigned i=0;i<MDO_RUDP_IO_BURST && MdoRudpWorkerLive(R);++i) {
        MdoRudpRecord* Record; size_t Written=0, Size; const unsigned char* Bytes;
        xrtMutexLock(R->Lock); Record=R->Tx.Head; xrtMutexUnlock(R->Lock);
        if (!Record) break;
        if (Record->Offset<MDO_RUDP_HEADER) { Bytes=Record->Header+Record->Offset; Size=MDO_RUDP_HEADER-Record->Offset; }
        else { Bytes=Record->Bytes+Record->Offset-MDO_RUDP_HEADER; Size=Record->Size-(Record->Offset-MDO_RUDP_HEADER); }
        if (Size>MDO_RUDP_IO_CHUNK) Size=MDO_RUDP_IO_CHUNK;
        /* No mutex across a native operation: terminal errors may synchronously
         * invoke PeerClose. Queued buffers stay ours until worker consumption. */
        xrudpresult Result=xrtRudpPeerWrite(R->Peer,Bytes,Size,&Written);
        if (Result==XRUDP_BACKPRESSURE) break;
        if (Result!=XRUDP_OK || !Written || Written>Size) { MdoRudpFail(R,1006,Result); return; }
        xrtMutexLock(R->Lock); Record->Offset+=Written;
        bool Complete=Record->Offset==MDO_RUDP_HEADER+Record->Size;
        if (Complete) (void)MdoRudpQueuePop(&R->Tx);
        xrtCondBroadcast(R->Changed); xrtMutexUnlock(R->Lock);
        if (Complete) MdoRudpRecordFree(Record);
    }
    for (unsigned i=0;i<MDO_RUDP_IO_BURST && MdoRudpWorkerLive(R);++i) {
        bool Room;
        xrtMutexLock(R->Lock);
        Room=!R->ReadEnded && MdoRudpQueueRoom(&R->Rx,R->HeaderSize==MDO_RUDP_HEADER ? R->Expected : 0);
        xrtMutexUnlock(R->Lock);
        if (!Room) break;
        if (R->HeaderSize==MDO_RUDP_HEADER && R->IncomingSize==R->Expected) {
            if (R->Header[4]==1 && !xrtUtf8Valid(xrtStrViewN((const char*)R->Incoming,R->Expected),NULL)) {
                MdoRudpFail(R,1007,XRUDP_MALFORMED); return;
            }
            MdoRudpRecord* Record=xrtMalloc(sizeof(*Record)+R->Expected);
            if (!Record) { MdoRudpFail(R,1011,XRUDP_NO_MEMORY); return; }
            memset(Record,0,sizeof(*Record)); Record->Size=R->Expected; memcpy(Record->Header,R->Header,MDO_RUDP_HEADER);
            if (Record->Size) memcpy(Record->Bytes,R->Incoming,Record->Size);
            xrtMutexLock(R->Lock); MdoRudpQueuePush(&R->Rx,Record);
            xrtCondBroadcast(R->Changed); xrtMutexUnlock(R->Lock);
            xrtSecureZero(R->Incoming,R->Expected); R->HeaderSize=R->IncomingSize=R->Expected=0;
            continue;
        }
        size_t Read=0, Capacity=R->HeaderSize<MDO_RUDP_HEADER ? MDO_RUDP_HEADER-R->HeaderSize : R->Expected-R->IncomingSize;
        unsigned char* Output=R->HeaderSize<MDO_RUDP_HEADER ? R->Header+R->HeaderSize : R->Incoming+R->IncomingSize;
        if (Capacity>MDO_RUDP_IO_CHUNK) Capacity=MDO_RUDP_IO_CHUNK;
        xrudpresult Result=xrtRudpPeerRead(R->Peer,Output,Capacity,&Read);
        if (Result==XRUDP_BACKPRESSURE) break;
        if (Result==XRUDP_DONE) {
            if (R->HeaderSize || R->IncomingSize) { MdoRudpFail(R,1002,XRUDP_TRUNCATED); return; }
            xrtMutexLock(R->Lock); R->ReadEnded=true; R->Info.CloseCode=1000; R->Info.PeerClosed=true;
            xrtMutexUnlock(R->Lock); break;
        }
        if (Result!=XRUDP_OK || !Read || Read>Capacity) { MdoRudpFail(R,1006,Result); return; }
        if (R->HeaderSize<MDO_RUDP_HEADER) {
            R->HeaderSize+=Read;
            if (R->HeaderSize==MDO_RUDP_HEADER) {
                R->Expected=((uint32)R->Header[0]<<24)|((uint32)R->Header[1]<<16)|((uint32)R->Header[2]<<8)|R->Header[3];
                if (R->Expected>R->Config.MessageLimit) { MdoRudpFail(R,1009,XRUDP_LIMIT); return; }
                if ((R->Header[4]!=1 && R->Header[4]!=2) || R->Header[5] || R->Header[6] || R->Header[7]) {
                    MdoRudpFail(R,1002,XRUDP_MALFORMED); return;
                }
            }
        } else R->IncomingSize+=Read;
    }
}
MdoRudpTransport* MdoRudpBind(const MdoRudpConfig* Config, xcancel* Cancel)
{
    MdoRudpTransport* R=NULL; xnetengineconfig Engine; xnetbufpoolconfig Pool; xsha256 Hash;
    static const char Domain[]="mdo.remote.xrudp.v1";
    if (!Config || !Config->MessageLimit || Config->MessageLimit>MDO_RUDP_MESSAGE_MAX ||
        (Config->Local.Family!=XNET_FAMILY_IPV4 && Config->Local.Family!=XNET_FAMILY_IPV6) ||
        !MdoRudpNonzero(Config->Private,32) || !MdoRudpNonzero(Config->PeerPublic,32) ||
        !MdoRudpNonzero(Config->Scope,16) || !MdoRudpNonzero(Config->Binding,32) ||
        xrtCancelRequested(Cancel)) return NULL;
    if (xrtAtomic32FetchAdd(&g_MdoRudpTransports,1,XMEMORY_ACQ_REL)>=MDO_RUDP_TRANSPORTS_MAX) {
        xrtAtomic32FetchSub(&g_MdoRudpTransports,1,XMEMORY_ACQ_REL); return NULL;
    }
    R=xrtCalloc(1,sizeof(*R));
    if (!R) { xrtAtomic32FetchSub(&g_MdoRudpTransports,1,XMEMORY_ACQ_REL); return NULL; }
    R->Counted=true; R->Config=*Config;
    R->Lock=xrtMutexCreate(); R->Changed=xrtCondCreate(); R->Cancel=xrtCancelChild(Cancel);
    R->Incoming=xrtCalloc(1,Config->MessageLimit);
    if (!R->Lock || !R->Changed || !R->Cancel || !R->Incoming) goto fail;
    xrtSha256Init(&Hash);
    bool HasContext=xrtSha256Update(&Hash,Domain,sizeof(Domain)-1) && xrtSha256Update(&Hash,Config->Binding,32) && xrtSha256Final(&Hash,R->Context);
    xrtSecureZero(&Hash,sizeof(Hash)); if (!HasContext) goto fail;
    xrtNetEngineConfigInit(&Engine); xrtNetBufPoolConfigInit(&Pool);
    Pool.MaxCacheBytes=256u*1024u; Engine.BufferPool=&Pool;
    Engine.Workers=1; Engine.CommandCapacity=32; Engine.TimerLimit=32; Engine.EventBatch=16;
    Engine.NodeCacheBytes=65536; Engine.PortPostLimit=32; Engine.PortWatchLimit=8;
    Engine.PortOperationLimit=128; Engine.PortOperationCache=32;
    R->Engine=xrtNetEngineCreate(&Engine);
    if (!R->Engine || !xrtNetEngineStart(R->Engine)) goto fail;
    R->Started=true;
    if (!xrtNetEnginePost(R->Engine,0,MdoRudpBindWorker,R)) goto fail;
    R->InitPosted=true;
    uint64 Until=MdoDeadlineAfterUs(UINT64_C(5000000));
    xrtMutexLock(R->Lock);
    while (!R->Opened && !R->Failed && !xrtCancelRequested(R->Cancel) && !MdoDeadlineExpired(Until))
        (void)xrtCondWaitFor(R->Changed,R->Lock,10);
    bool Opened=R->Opened && !R->Failed && !xrtCancelRequested(R->Cancel);
    xrtMutexUnlock(R->Lock);
    if (Opened) return R;
fail:
    MdoRudpDestroy(R); return NULL;
}
bool MdoRudpGetInfo(MdoRudpTransport* R, MdoRudpInfo* Info)
{
    if (!R || !Info) return false;
    xrtMutexLock(R->Lock); *Info=R->Info;
    Info->Confirmed=R->Confirmed; Info->Closed=R->Failed || R->Stopping || R->ReadEnded || xrtCancelRequested(R->Cancel);
    Info->SendBytes=R->Tx.Bytes; Info->SendRecords=R->Tx.Count;
    Info->ReceiveBytes=R->Rx.Bytes; Info->ReceiveRecords=R->Rx.Count;
    xrtMutexUnlock(R->Lock); return true;
}
bool MdoRudpConnected(MdoRudpTransport* R)
{
    bool Ready;
    if (!R) return false;
    xrtMutexLock(R->Lock); Ready=R->Confirmed && !R->Failed && !R->Stopping && !xrtCancelRequested(R->Cancel);
    xrtMutexUnlock(R->Lock); return Ready;
}
bool MdoRudpConnect(MdoRudpTransport* R, const xnetaddr* Remote, uint32 TimeoutMs)
{
    if (!R || !Remote || !Remote->Port || Remote->Family!=R->Config.Local.Family ||
        xrtNetAddrIsUnspecified(Remote) || xrtNetAddrIsMulticast(Remote) ||
        (Remote->Family==XNET_FAMILY_IPV4 && Remote->Address[0]==255 && Remote->Address[1]==255 && Remote->Address[2]==255 && Remote->Address[3]==255) ||
        !TimeoutMs || TimeoutMs>5000) return false;
    xrtMutexLock(R->Lock);
    if (R->Stopping || R->Failed || R->Config.Listen || R->ConnectIssued) { xrtMutexUnlock(R->Lock); return false; }
    R->Remote=*Remote; R->ConnectIssued=R->ConnectPending=true; xrtMutexUnlock(R->Lock);
    uint64 Until=MdoDeadlineAfterUs((uint64)TimeoutMs*1000u);
    for (;;) {
        MdoRudpKick(R); xrtMutexLock(R->Lock);
        bool Ready=R->Confirmed && !R->Failed && !xrtCancelRequested(R->Cancel);
        bool Done=Ready || R->Failed || R->Stopping || xrtCancelRequested(R->Cancel) || MdoDeadlineExpired(Until);
        if (!Done) (void)xrtCondWaitFor(R->Changed,R->Lock,10);
        xrtMutexUnlock(R->Lock);
        if (Done) { if (!Ready) MdoRudpFail(R,1006,xrtCancelRequested(R->Cancel)?XRUDP_CANCELLED:XRUDP_TIMEOUT); return Ready; }
    }
}
bool MdoRudpSend(MdoRudpTransport* R, bool Binary, xbytesview Message)
{
    if (!R || (Message.Size && !Message.Data)) return false;
    if (Message.Size>R->Config.MessageLimit) { MdoRudpFail(R,1009,XRUDP_LIMIT); return false; }
    if (!Binary && !xrtUtf8Valid(xrtStrViewN((const char*)Message.Data,Message.Size),NULL)) {
        MdoRudpFail(R,1007,XRUDP_MALFORMED); return false;
    }
    uint64 Until=MdoDeadlineAfterUs(UINT64_C(2000000));
    for (;;) {
        MdoRudpKick(R); xrtMutexLock(R->Lock);
        bool Ready=R->Confirmed && !R->Failed && !R->Stopping && !xrtCancelRequested(R->Cancel);
        if (!Ready || MdoDeadlineExpired(Until)) {
            xrtMutexUnlock(R->Lock); if (Ready) MdoRudpFail(R,1006,XRUDP_TIMEOUT); return false;
        }
        if (MdoRudpQueueRoom(&R->Tx,Message.Size)) break;
        (void)xrtCondWaitFor(R->Changed,R->Lock,10); xrtMutexUnlock(R->Lock);
    }
    MdoRudpRecord* Record=xrtCalloc(1,sizeof(*Record)+Message.Size);
    if (!Record) { xrtMutexUnlock(R->Lock); MdoRudpFail(R,1011,XRUDP_NO_MEMORY); return false; }
    Record->Size=Message.Size;
    Record->Header[0]=(uint8)(Message.Size>>24); Record->Header[1]=(uint8)(Message.Size>>16);
    Record->Header[2]=(uint8)(Message.Size>>8); Record->Header[3]=(uint8)Message.Size; Record->Header[4]=Binary?2:1;
    if (Message.Size) memcpy(Record->Bytes,Message.Data,Message.Size);
    MdoRudpQueuePush(&R->Tx,Record); xrtMutexUnlock(R->Lock); MdoRudpKick(R); return true;
}
bool MdoRudpPoll(MdoRudpTransport* R, MdoRudpMessageProc Proc, void* Data)
{
    if (!R || !Proc || !MdoRudpConnected(R)) return false;
    MdoRudpKick(R);
    for (unsigned i=0;i<MDO_RUDP_QUEUE_RECORDS;++i) {
        xrtMutexLock(R->Lock); MdoRudpRecord* Record=MdoRudpQueuePop(&R->Rx);
        xrtCondBroadcast(R->Changed); xrtMutexUnlock(R->Lock);
        if (!Record) break;
        bool Accepted=Proc(Record->Header[4]==2,(xbytesview){Record->Bytes,Record->Size},Data);
        MdoRudpRecordFree(Record);
        if (!Accepted) { MdoRudpFail(R,1008,XRUDP_CANCELLED); return false; }
        if (!MdoRudpConnected(R)) return false;
    }
    MdoRudpKick(R); xrtMutexLock(R->Lock);
    bool Live=!R->Failed && !R->Stopping && !xrtCancelRequested(R->Cancel) && (!R->ReadEnded || R->Rx.Count);
    xrtMutexUnlock(R->Lock); return Live;
}
void MdoRudpDestroy(MdoRudpTransport* R)
{
    if (!R) return;
    if (R->Lock) {
        xrtMutexLock(R->Lock); R->Stopping=true;
        while (R->InitPosted && !R->Initialized) (void)xrtCondWaitFor(R->Changed,R->Lock,10);
        xrtMutexUnlock(R->Lock);
    }
    xrtCancelRequest(R->Cancel);
    if (R->Endpoint) {
        (void)xrtRudpEndpointClose(R->Endpoint);
        xrtMutexLock(R->Lock);
        while (!R->EndpointClosed) (void)xrtCondWaitFor(R->Changed,R->Lock,10);
        xrtMutexUnlock(R->Lock);
    }
    /* Stop joins callback tails, including the final endpoint/UDP release.
     * No native callback can reference this TCC generation after this join. */
    xrtRudpPeerDestroy(R->Peer); xrtRudpEndpointDestroy(R->Endpoint);
    if (R->Started) {
        while (xrtNetEngineState(R->Engine)==XNET_ENGINE_RUNNING && !xrtNetEngineStop(R->Engine)) xrtSleep(1);
    }
    if (R->Engine) while (!xrtNetEngineDestroy(R->Engine)) xrtSleep(1);
    MdoRudpRecord* Record;
    while ((Record=MdoRudpQueuePop(&R->Tx))) MdoRudpRecordFree(Record);
    while ((Record=MdoRudpQueuePop(&R->Rx))) MdoRudpRecordFree(Record);
    if (R->Incoming) { xrtSecureZero(R->Incoming,R->Config.MessageLimit); xrtFree(R->Incoming); }
    xrtCancelDestroy(R->Cancel); xrtCondDestroy(R->Changed); xrtMutexDestroy(R->Lock);
    if (R->Counted) xrtAtomic32FetchSub(&g_MdoRudpTransports,1,XMEMORY_ACQ_REL);
    xrtSecureZero(R,sizeof(*R)); xrtFree(R);
}
#endif
