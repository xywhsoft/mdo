#include <stdio.h>
#include <string.h>

#include "internal.h"
#include "write_admission.h"
#include "../sessions/internal.h"
#include "../../include/mdo/approvals.h"
#include "../../include/mdo/asks.h"
#include "../../include/mdo/runs.h"

/* Same-port WebSocket over xs' documented pull/TAKEOVER interface. xrt owns
 * RFC 6455 handshake, frame, fragmentation and UTF-8 validation. Never replace
 * xs' stream event table: its close callback owns the TCC generation lease.
 * Each admitted client has an executor, so a stalled reader cannot stall any
 * other client or a model callback. The pool is created only on first use. */
#define MDO_LIVE_CLIENTS 8u
#define MDO_LIVE_RECORDS 512u
#define MDO_LIVE_BYTES (2u * 1024u * 1024u)
#define MDO_LIVE_BATCH 32u
#define MDO_LIVE_INPUT 4096u
#define MDO_LIVE_TICK_US 25000u
#define MDO_LIVE_SEND_US 2000000u
#define MDO_LIVE_PING_US 20000000u
#define MDO_LIVE_IDLE_US 60000000u

typedef struct MdoLiveRecord {
    uint64 Revision;
    char Project[MDO_PROJECT_ID_CAPACITY];
    char Session[MDO_SESSION_ID_CAPACITY];
    char* Json;
    size_t Size;
} MdoLiveRecord;

typedef struct MdoLiveClient {
    XS_HttpReq Request;
    MdoApiContext Context;
    char Head[512];
    size_t HeadSize;
    char Project[MDO_PROJECT_ID_CAPACITY];
    char Session[MDO_SESSION_ID_CAPACITY];
    uint64 Selection, Cursor, Revision, StateRevision;
    char Epoch[65];
    uint64 LastRead, LastPing;
    size_t Slot;
    bool Replay;
    bool ReplayFirst;
    bool CloseSent;
    xwsmessagestate MessageState;
    char Input[MDO_LIVE_INPUT + XWS_FRAME_HEAD_MAX];
    size_t InputSize;
    char Message[MDO_LIVE_INPUT];
    size_t MessageSize;
} MdoLiveClient;

typedef struct MdoLiveState {
    xmutex* Lock;
    xcond* Changed;
    xtaskpool* Pool;
    MdoLiveClient* Clients[MDO_LIVE_CLIENTS];
    xfuture* Futures[MDO_LIVE_CLIENTS];
    MdoLiveRecord Records[MDO_LIVE_RECORDS];
    size_t First, Count, Bytes;
    uint64 Revision, StateRevision;
    bool Stopping;
} MdoLiveState;

static MdoLiveState g_MdoLive;

void MdoApiLiveChanged(void* Data)
{
    (void)Data;
    if ( g_MdoLive.Lock == NULL ) return;
    xrtMutexLock(g_MdoLive.Lock);
    if ( !g_MdoLive.Stopping ) {
        ++g_MdoLive.StateRevision;
        (void)xrtCondBroadcast(g_MdoLive.Changed);
    }
    xrtMutexUnlock(g_MdoLive.Lock);
}

static void MdoLiveEvict(void)
{
    MdoLiveRecord* Record = &g_MdoLive.Records[g_MdoLive.First];
    g_MdoLive.Bytes -= Record->Size;
    xrtFree(Record->Json); memset(Record, 0, sizeof(*Record));
    g_MdoLive.First = (g_MdoLive.First + 1u) % MDO_LIVE_RECORDS;
    --g_MdoLive.Count;
}

static void MdoLivePublish(cstr Project, cstr Session, xstrview Json,
    bool StateChanged, void* Data)
{
    MdoLiveRecord* Record;
    char* Copy;
    (void)Data;
    xrtMutexLock(g_MdoLive.Lock);
    if ( g_MdoLive.Stopping ) goto done;
    ++g_MdoLive.Revision;
    if ( StateChanged ) ++g_MdoLive.StateRevision;
    Copy = Json.Size <= MDO_LIVE_BYTES ? (char*)xrtMalloc(Json.Size) : NULL;
    /* Missing cache data is an explicit gap. Readers replay from their last
     * durable event ID; they never silently advance past a failed allocation. */
    if ( Copy == NULL ) {
        while ( g_MdoLive.Count != 0u ) MdoLiveEvict();
        goto notify;
    }
    memcpy(Copy, Json.Data, Json.Size);
    while ( g_MdoLive.Count == MDO_LIVE_RECORDS ||
            g_MdoLive.Bytes > MDO_LIVE_BYTES - Json.Size ) MdoLiveEvict();
    Record = &g_MdoLive.Records[(g_MdoLive.First + g_MdoLive.Count) % MDO_LIVE_RECORDS];
    Record->Revision = g_MdoLive.Revision;
    snprintf(Record->Project, sizeof(Record->Project), "%s", Project);
    snprintf(Record->Session, sizeof(Record->Session), "%s", Session);
    Record->Json = Copy; Record->Size = Json.Size;
    g_MdoLive.Bytes += Json.Size; ++g_MdoLive.Count;
notify:
    (void)xrtCondBroadcast(g_MdoLive.Changed);
done:
    xrtMutexUnlock(g_MdoLive.Lock);
}

static bool MdoLiveFrame(MdoLiveClient* Client, uint8 Opcode,
    const void* Data, size_t Size)
{
    xwsframe Frame;
    char Head[XWS_FRAME_HEAD_MAX];
    size_t Bytes;
    xrtWsFrameInit(&Frame); Frame.Opcode = Opcode;
    Frame.Flags = XWS_FRAME_FIN; Frame.PayloadSize = Size;
    Client->Context.SendDeadline = xrtDeadlineAfter(MDO_LIVE_SEND_US);
    return xrtWsFrameWrite(&Frame, NULL, Head, sizeof(Head), &Bytes) &&
        MdoApiDownloadSend(&Client->Context, Head, Bytes) &&
        MdoApiDownloadSend(&Client->Context, Data, Size);
}

static bool MdoLiveText(MdoLiveClient* Client, cstr Text)
{
    return MdoLiveFrame(Client, XWS_OPCODE_TEXT, Text, strlen(Text));
}

static bool MdoLiveValue(MdoLiveClient* Client, xvalue* Value)
{
    size_t Size = 0u;
    str Json = Value != NULL ? xrtJsonStringify(Value, false, &Size) : NULL;
    bool Ok = Json != NULL && Size <= MDO_API_RESPONSE_MAX_BYTES &&
        MdoLiveFrame(Client, XWS_OPCODE_TEXT, Json, Size);
    xrtValueRelease(Value); xrtFree(Json);
    return Ok;
}

static bool MdoLiveClose(MdoLiveClient* Client, uint16 Code)
{
    char Payload[2]; size_t Size;
    Client->CloseSent = true;
    if ( xrtWsCloseWrite(Code, xrtStrView(""), Payload, sizeof(Payload), &Size) )
        (void)MdoLiveFrame(Client, XWS_OPCODE_CLOSE, Payload, Size);
    return false;
}

static bool MdoLiveUInt(const xvalue* Object, cstr Key, uint64* Value)
{
    const xvalue* Item = xrtValueObjectGet(Object, xrtStrView(Key));
    int64 Signed;
    if ( Item == NULL ) return false;
    if ( xrtValueType(Item) == XVALUE_UINT ) return xrtValueGetUInt(Item, Value);
    if ( xrtValueType(Item) != XVALUE_INT || !xrtValueGetInt(Item, &Signed) || Signed < 0 ) return false;
    *Value = (uint64)Signed; return true;
}

static bool MdoLiveId(const xvalue* Object, cstr Key, char* Output, size_t Capacity)
{
    xstrview Text; size_t i;
    const xvalue* Item = xrtValueObjectGet(Object, xrtStrView(Key));
    if ( Item == NULL || !xrtValueGetString(Item, &Text) || Text.Size >= Capacity ) return false;
    for ( i = 0u; i < Text.Size; ++i ) {
        char Byte = Text.Data[i];
        if ( !((Byte >= 'a' && Byte <= 'z') || (Byte >= 'A' && Byte <= 'Z') ||
               (Byte >= '0' && Byte <= '9') || Byte == '-' || Byte == '_' || Byte == '.') ) return false;
    }
    if ( Text.Size != 0u && Text.Data[0] == '.' ) return false;
    memcpy(Output, Text.Data, Text.Size); Output[Text.Size] = '\0';
    return true;
}

static bool MdoLiveCommand(MdoLiveClient* Client)
{
    xjsonreadconfig Config;
    xvalue* Value;
    xstrview Type;
    uint64 Cursor, Selection;
    char Project[MDO_PROJECT_ID_CAPACITY], Session[MDO_SESSION_ID_CAPACITY];
    bool Ok = false;
    xrtJsonReadConfigInit(&Config); Config.MaxInputBytes = MDO_LIVE_INPUT;
    Config.MaxDepth = 2u; Config.MaxValues = 12u; Config.MaxContainerItems = 6u;
    Value = xrtJsonRead(xrtStrViewN(Client->Message, Client->MessageSize), &Config);
    if ( Value == NULL || xrtValueType(Value) != XVALUE_OBJECT ||
         !xrtValueGetString(xrtValueObjectGet(Value, XRT_STR_LITERAL("type")), &Type) ) goto done;
    if ( Type.Size == 4u && memcmp(Type.Data, "pong", 4u) == 0 ) {
        Ok = xrtValueCount(Value) == 1u; goto done;
    }
    if ( Type.Size != 9u || memcmp(Type.Data, "subscribe", 9u) != 0 ||
         xrtValueCount(Value) != 5u ||
         !MdoLiveId(Value, "project_id", Project, sizeof(Project)) ||
         !MdoLiveId(Value, "session_id", Session, sizeof(Session)) ||
         ((Project[0] == '\0') != (Session[0] == '\0')) ||
         !MdoLiveUInt(Value, "after", &Cursor) || Cursor > UINT64_C(9007199254740991) ||
         !MdoLiveUInt(Value, "selection", &Selection) || Selection > UINT64_C(9007199254740991) ) goto done;
    snprintf(Client->Project, sizeof(Client->Project), "%s", Project);
    snprintf(Client->Session, sizeof(Client->Session), "%s", Session);
    Client->Cursor = Cursor; Client->Selection = Selection;
    /* Capture the live watermark BEFORE reading disk. Events committed during
     * replay remain in the ring; cursor deduplication removes the overlap. */
    xrtMutexLock(g_MdoLive.Lock); Client->Revision = g_MdoLive.Revision;
    xrtMutexUnlock(g_MdoLive.Lock);
    Client->Replay = Session[0] != '\0';
    Client->ReplayFirst = true;
    Ok = true;
done:
    xrtValueRelease(Value);
    return Ok || MdoLiveClose(Client, 1008u);
}

static bool MdoLiveReceive(MdoLiveClient* Client)
{
    size_t Available = Client->Request.tls != NULL ?
        xrtTlsStreamAvailable(Client->Request.tls) : xrtNetStreamAvailable(Client->Request.tcp);
    if ( Available != 0u ) {
        xfuture* Future;
        xnetbytes* Bytes;
        xbytesview View;
        size_t Capacity = sizeof(Client->Input) - Client->InputSize;
        bool Ok;
        if ( Capacity == 0u ) return MdoLiveClose(Client, 1009u);
        Future = Client->Request.tls != NULL ? xrtTlsStreamRecvAsync(Client->Request.tls, Capacity) :
            xrtNetStreamRecvAsync(Client->Request.tcp, Capacity);
        Ok = Future != NULL && xrtFutureWaitUntilCancel(Future,
            xrtDeadlineAfter(MDO_LIVE_SEND_US), Client->Context.SendCancel) == XWAIT_OK &&
            xrtFutureState(Future) == XFUTURE_RESOLVED;
        Bytes = Ok ? (xnetbytes*)xrtFutureValue(Future) : NULL;
        if ( Bytes == NULL ) Ok = false;
        if ( Ok ) {
            View = xrtNetBytesView(Bytes);
            Ok = View.Size <= Capacity;
            if ( Ok ) { memcpy(Client->Input + Client->InputSize, View.Data, View.Size); Client->InputSize += View.Size; }
        }
        if ( !Ok && Future != NULL ) (void)xrtFutureCancel(Future);
        xrtFutureDestroy(Future);
        if ( !Ok ) return false;
    }
    while ( Client->InputSize != 0u ) {
        xwsframe Frame; xwsframeconfig Config;
        xwsmessageinfo Info; xwsmessageerrorinfo Error;
        xwsframestatus Status;
        size_t Size, Total;
        char* Payload;
        xrtWsFrameConfigInit(&Config); Config.Mask = XWS_MASK_REQUIRED; Config.MaxPayload = MDO_LIVE_INPUT;
        Status = xrtWsFrameParse((xbytesview){(const uint8*)Client->Input, Client->InputSize}, &Frame, &Config, NULL);
        if ( Status == XWS_FRAME_MORE ) break;
        if ( Status != XWS_FRAME_READY ) return MdoLiveClose(Client, 1002u);
        Size = (size_t)Frame.PayloadSize; Total = Frame.HeadSize + Size;
        if ( Total > Client->InputSize ) break;
        Payload = Client->Input + Frame.HeadSize;
        if ( !xrtWsMask(Payload, Size, Frame.Mask, 0u) ) return MdoLiveClose(Client, 1002u);
        memset(&Error, 0, sizeof(Error));
        if ( !xrtWsMessageFrameBegin(&Client->MessageState, &Frame, &Info, &Error) ||
             !xrtWsMessagePayload(&Client->MessageState, (xbytesview){(const uint8*)Payload, Size}, &Error) ||
             !xrtWsMessageFrameEnd(&Client->MessageState, &Error) )
            return MdoLiveClose(Client, Error.CloseCode != 0u ? Error.CloseCode : 1002u);
        Client->LastRead = xrtClock();
        if ( Frame.Opcode == XWS_OPCODE_CLOSE ) {
            Client->CloseSent = true;
            (void)MdoLiveFrame(Client, XWS_OPCODE_CLOSE, Payload, Size); return false;
        } else if ( Frame.Opcode == XWS_OPCODE_PING ) {
            if ( !MdoLiveFrame(Client, XWS_OPCODE_PONG, Payload, Size) ) return false;
        } else if ( Frame.Opcode == XWS_OPCODE_BINARY ) return MdoLiveClose(Client, 1003u);
        else if ( Frame.Opcode == XWS_OPCODE_TEXT || Frame.Opcode == XWS_OPCODE_CONTINUATION ) {
            if ( Frame.Opcode == XWS_OPCODE_TEXT ) Client->MessageSize = 0u;
            if ( Size > sizeof(Client->Message) - Client->MessageSize ) return MdoLiveClose(Client, 1009u);
            memcpy(Client->Message + Client->MessageSize, Payload, Size); Client->MessageSize += Size;
            if ( (Frame.Flags & XWS_FRAME_FIN) != 0u && !MdoLiveCommand(Client) ) return false;
        }
        Client->InputSize -= Total;
        memmove(Client->Input, Client->Input + Total, Client->InputSize);
    }
    return true;
}

typedef struct MdoLiveBatch {
    MdoLiveClient* Client;
    xvalue* Items;
    uint64 Latest;
} MdoLiveBatch;

static bool MdoLiveVisit(const MdoSessionEventInfo* Event, void* Data)
{
    MdoLiveBatch* Batch = (MdoLiveBatch*)Data;
    xvalue* Item = NULL;
    bool Ok;
    if ( Event->EventId <= Batch->Client->Cursor ) return true;
    Ok = MdoApiSessionEventValue(Event, Batch->Client->Project, Batch->Client->Session, false, &Item) &&
        MdoApiValueAppendTake(Batch->Items, &Item);
    xrtValueRelease(Item);
    if ( Ok ) Batch->Client->Cursor = Event->EventId;
    return Ok;
}

static bool MdoLiveEvents(MdoLiveClient* Client)
{
    MdoLiveRecord Copies[MDO_LIVE_BATCH] = {{0}};
    size_t Count = 0u, i;
    uint64 After = Client->Cursor;
    bool Lost = false, Ok = true, Reply = Client->Replay;
    MdoLiveBatch Batch = {Client, NULL, After};
    xvalue* Value = NULL;
    MdoProjectLease* Lease = NULL;
    xwork_error Error;
    if ( Client->Session[0] == '\0' ) return true;
    xrtMutexLock(g_MdoLive.Lock);
    Ok = Client->Replay || Client->Revision != g_MdoLive.Revision;
    xrtMutexUnlock(g_MdoLive.Lock);
    if ( !Ok ) return true; /* Idle connections allocate and read nothing. */
    Ok = true;
    Batch.Items = xrtValueArray();
    /* Do not hold an idle project lease for the connection's whole lifetime. */
    Lease = MdoProjectLeaseAcquire(Client->Project, MDO_PROJECT_LEASE_SHARED, &Error);
    if ( Lease == NULL ) { xrtValueRelease(Batch.Items); return true; }
    xrtMutexLock(g_MdoLive.Lock);
    if ( !Client->Replay && Client->Revision < g_MdoLive.Revision &&
         (g_MdoLive.Count == 0u || Client->Revision + 1u < g_MdoLive.Records[g_MdoLive.First].Revision) ) {
        Client->Replay = true; Client->ReplayFirst = true; Reply = true;
        Client->Revision = g_MdoLive.Revision;
    }
    if ( !Client->Replay ) {
        for ( i = 0u; i < g_MdoLive.Count; ++i ) {
            MdoLiveRecord* Record = &g_MdoLive.Records[(g_MdoLive.First + i) % MDO_LIVE_RECORDS];
            if ( Record->Revision <= Client->Revision ) continue;
            if ( strcmp(Record->Project, Client->Project) == 0 && strcmp(Record->Session, Client->Session) == 0 ) {
                Copies[Count] = *Record;
                Copies[Count].Json = (char*)xrtMalloc(Record->Size);
                if ( Copies[Count].Json == NULL ) { Ok = false; break; }
                memcpy(Copies[Count].Json, Record->Json, Record->Size); ++Count;
            }
            Client->Revision = Record->Revision;
            if ( Count == MDO_LIVE_BATCH ) break;
        }
    }
    xrtMutexUnlock(g_MdoLive.Lock);
    if ( Batch.Items == NULL ) Ok = false;
    if ( Ok && Client->Replay ) {
        MdoSessionEventSnapshot* Snapshot = MdoSessionEventReplay(Client->Project, Client->Session, After, MDO_LIVE_BATCH, &Error);
        Ok = Snapshot != NULL;
        if ( Ok ) {
            Batch.Latest = MdoSessionEventSnapshotLatestId(Snapshot);
            snprintf(Client->Epoch, sizeof(Client->Epoch), "%s", MdoSessionEventSnapshotEpoch(Snapshot));
            Lost = Client->ReplayFirst && MdoSessionEventSnapshotHistoryLost(Snapshot);
            Client->ReplayFirst = false;
            for ( i = 0u; Ok && i < MdoSessionEventSnapshotCount(Snapshot); ++i ) {
                MdoSessionEventInfo Event = {0}; Event.Size = sizeof(Event);
                Ok = MdoSessionEventSnapshotAt(Snapshot, i, &Event) && MdoLiveVisit(&Event, &Batch);
            }
            Client->Replay = Client->Cursor < Batch.Latest;
        }
        MdoSessionEventSnapshotRelease(Snapshot);
    } else {
        for ( i = 0u; Ok && i < Count; ++i ) Ok = MdoSessionsInternalEventVisit(Client->Project, Client->Session,
            xrtStrViewN(Copies[i].Json, Copies[i].Size), MdoLiveVisit, &Batch);
        Batch.Latest = Client->Cursor;
        /* Replacements (clear/trim/restore) change the file identity. Reads
         * and pushes must share its epoch before the next content is merged. */
        if (Ok && Count) {
            MdoSessionEventSnapshot* Snapshot = MdoSessionEventReplay(Client->Project, Client->Session,
                Client->Cursor, 1u, &Error);
            Ok = Snapshot != NULL;
            if (Ok) snprintf(Client->Epoch, sizeof(Client->Epoch), "%s", MdoSessionEventSnapshotEpoch(Snapshot));
            MdoSessionEventSnapshotRelease(Snapshot);
        }
    }
    for ( i = 0u; i < Count; ++i ) xrtFree(Copies[i].Json);
    MdoProjectLeaseRelease(Lease);
    if ( !Ok ) { xrtValueRelease(Batch.Items); return false; }
    if ( !Reply && xrtValueCount(Batch.Items) == 0u ) { xrtValueRelease(Batch.Items); return true; }
    Value = xrtValueObject();
    Ok = Value != NULL && MdoApiValueSetString(Value, "type", "events") &&
        MdoApiValueSetUInt(Value, "selection", Client->Selection) &&
        MdoApiValueSetString(Value, "project_id", Client->Project) &&
        MdoApiValueSetString(Value, "session_id", Client->Session) &&
        MdoApiValueSetString(Value, "epoch", Client->Epoch) &&
        MdoApiValueSetUInt(Value, "after", After) &&
        MdoApiValueSetUInt(Value, "next_cursor", Client->Cursor) &&
        MdoApiValueSetUInt(Value, "latest_event_id", Batch.Latest) &&
        MdoApiValueSetBool(Value, "history_lost", Lost) &&
        MdoApiValueSetTake(Value, "items", &Batch.Items);
    xrtValueRelease(Batch.Items);
    if ( !Ok ) { xrtValueRelease(Value); return false; }
    return MdoLiveValue(Client, Value);
}

static void MdoLiveDrop(ptr Value, ptr Data)
{
    MdoLiveClient* Client = (MdoLiveClient*)Value;
    (void)Data;
    if ( Client->Request.tls != NULL ) {
        if ( Client->CloseSent ) (void)xrtTlsStreamClose(Client->Request.tls);
        else (void)xrtTlsStreamAbort(Client->Request.tls);
        xrtTlsStreamDestroy(Client->Request.tls);
    } else {
        if ( Client->CloseSent ) (void)xrtNetStreamClose(Client->Request.tcp);
        else (void)xrtNetStreamAbort(Client->Request.tcp);
        xrtNetStreamDestroy(Client->Request.tcp);
    }
    xrtMutexLock(g_MdoLive.Lock);
    g_MdoLive.Clients[Client->Slot] = NULL;
    xrtMutexUnlock(g_MdoLive.Lock);
    xrtFree(Client);
}

static xtaskoutcome MdoLiveRun(xcancel* Cancel, ptr Data, xtaskvalue* Result)
{
    MdoLiveClient* Client = (MdoLiveClient*)Data;
    uint64 Tick = xrtClock();
    xwsmessageconfig Config;
    (void)Result;
    Client->Context.SendCancel = Cancel;
    Client->Context.SendDeadline = xrtDeadlineAfter(MDO_LIVE_SEND_US);
    Client->LastRead = Client->LastPing = Tick;
    xrtWsMessageConfigInitSafe(&Config); Config.MaxSize = MDO_LIVE_INPUT;
    if ( !xrtWsMessageInit(&Client->MessageState, &Config) ||
         !MdoApiDownloadSend(&Client->Context, Client->Head, Client->HeadSize) ||
         !MdoLiveText(Client, "{\"type\":\"ready\",\"version\":1}") ) return XTASK_FAILED;
    for ( ; ; ) {
        uint64 Now = xrtClock(), StateRevision;
        bool Stop;
        Client->Context.SendDeadline = xrtDeadlineAfter(MDO_LIVE_SEND_US);
        if ( !MdoApiDownloadLive(&Client->Context) || !MdoLiveReceive(Client) ) break;
        Now = xrtClock();
        if ( Now - Client->LastRead >= MDO_LIVE_IDLE_US ) { (void)MdoLiveClose(Client, 1001u); break; }
        if ( Now - Client->LastPing >= MDO_LIVE_PING_US ) {
            if ( !MdoLiveText(Client, "{\"type\":\"ping\"}") ) break;
            Client->LastPing = Now;
        }
        xrtMutexLock(g_MdoLive.Lock);
        Stop = g_MdoLive.Stopping; StateRevision = g_MdoLive.StateRevision;
        xrtMutexUnlock(g_MdoLive.Lock);
        if ( Stop || xrtCancelRequested(Cancel) ) break;
        if ( Now >= Tick ) {
            if ( !MdoLiveEvents(Client) ) break;
            if ( Client->StateRevision != StateRevision ) {
                if ( !MdoLiveText(Client, "{\"type\":\"changed\"}") ) break;
                Client->StateRevision = StateRevision;
            }
            Tick = xrtClock() + MDO_LIVE_TICK_US;
        }
        /* Bounded control-input check, event-driven wakeup, 25 ms batching.
         * No file scans or HTTP requests are made while the ring covers us. */
        xrtMutexLock(g_MdoLive.Lock);
        if ( !g_MdoLive.Stopping ) (void)xrtCondWaitUntil(g_MdoLive.Changed, g_MdoLive.Lock, Tick);
        xrtMutexUnlock(g_MdoLive.Lock);
    }
    return XTASK_SUCCESS;
}

bool MdoApiLiveInit(void)
{
    if ( g_MdoLive.Lock != NULL ) return true;
    g_MdoLive.Lock = xrtMutexCreate(); g_MdoLive.Changed = xrtCondCreate();
    if ( g_MdoLive.Lock == NULL || g_MdoLive.Changed == NULL ) {
        xrtMutexDestroy(g_MdoLive.Lock); xrtCondDestroy(g_MdoLive.Changed);
        memset(&g_MdoLive, 0, sizeof(g_MdoLive)); return false;
    }
    MdoSessionsObserve(MdoLivePublish, NULL);
    MdoApprovalObserve(MdoApiLiveChanged, NULL);
    MdoAskObserve(MdoApiLiveChanged, NULL);
    MdoRunObserve(MdoApiLiveChanged, NULL);
    return true;
}

void MdoApiLiveUnit(void)
{
    xtaskpool* Pool;
    size_t i;
    if ( g_MdoLive.Lock == NULL ) return;
    xrtMutexLock(g_MdoLive.Lock); g_MdoLive.Stopping = true; Pool = g_MdoLive.Pool;
    (void)xrtCondBroadcast(g_MdoLive.Changed); xrtMutexUnlock(g_MdoLive.Lock);
    if ( Pool != NULL ) {
        (void)xrtTaskPoolCancel(Pool); (void)xrtTaskPoolWait(Pool); (void)xrtTaskPoolDestroy(Pool);
        g_MdoLive.Pool = NULL;
    }
    for ( i = 0u; i < MDO_LIVE_CLIENTS; ++i ) { xrtFutureDestroy(g_MdoLive.Futures[i]); g_MdoLive.Futures[i] = NULL; }
}

void MdoApiLiveRelease(void)
{
    /* ServiceUnit stops and joins all producers before this final release.
     * During their shutdown, publication sees Stopping and remains a no-op. */
    if ( g_MdoLive.Lock == NULL ) return;
    MdoSessionsObserve(NULL, NULL); MdoApprovalObserve(NULL, NULL);
    MdoAskObserve(NULL, NULL); MdoRunObserve(NULL, NULL);
    while ( g_MdoLive.Count != 0u ) MdoLiveEvict();
    xrtCondDestroy(g_MdoLive.Changed); xrtMutexDestroy(g_MdoLive.Lock);
    memset(&g_MdoLive, 0, sizeof(g_MdoLive));
}

static bool MdoLiveAuthorized(MdoApiContext* Context)
{
    const xhttp1head* Head = Context->Request->head;
    const xhttpfield *Origin = NULL, *Host = NULL, *Protocols = NULL;
    char Expected[384], Token[MDO_API_WRITE_TOKEN_CAPACITY], Protocol[96];
    int Size;
    if ( Context->Target.Query.Size != 0u ||
         xrtHttpFieldGetUnique(Head->Fields, Head->FieldCount, XRT_STR_LITERAL("Origin"), &Origin) != XHTTP_NEXT_ITEM ||
         xrtHttpFieldGetUnique(Head->Fields, Head->FieldCount, XRT_STR_LITERAL("Host"), &Host) != XHTTP_NEXT_ITEM ||
         xrtHttpFieldGetUnique(Head->Fields, Head->FieldCount, XRT_STR_LITERAL("Sec-WebSocket-Protocol"), &Protocols) != XHTTP_NEXT_ITEM ||
         Host->Value.Size > 300u || !MdoApiWriteToken(Token) ) return false;
    Size = snprintf(Expected, sizeof(Expected), "%s://%.*s", Context->Request->tls != NULL ? "https" : "http",
        (int)Host->Value.Size, Host->Value.Data);
    if ( Size <= 0 || (size_t)Size != Origin->Value.Size ||
         memcmp(Expected, Origin->Value.Data, Origin->Value.Size) != 0 ) return false;
    snprintf(Protocol, sizeof(Protocol), "mdo.token.%s", Token);
    return xrtWsProtocolsValid(Protocols->Value) && xrtWsProtocolsHas(Protocols->Value, xrtStrView(Protocol));
}

bool MdoApiLiveRoute(MdoApiContext* Context)
{
    xwsupgradeserverconfig Config;
    xwsupgrade Upgrade;
    xhttpfield Fields[XWS_UPGRADE_RESPONSE_FIELDS_MAX];
    size_t Count = 0u, Slot;
    MdoLiveClient* Client = NULL;
    xfuture* Future = NULL;
    xtaskargs Args = {0};
    if ( !MdoLiveAuthorized(Context) ) return MdoApiReplyError(Context, 403u, "live_forbidden",
        "Use this page's origin and startup token for live updates", NULL);
    xrtWsUpgradeServerConfigInit(&Config); Config.Protocols = XRT_STR_LITERAL("mdo.live.v1");
    if ( !xrtWsUpgradeRequestCheck(Context->Request->head, &Config, &Upgrade) || Upgrade.Protocol.Size == 0u )
        return MdoApiReplyError(Context, 400u, "live_upgrade_invalid", "A WebSocket version 13 upgrade is required", NULL);
    Client = (MdoLiveClient*)xrtCalloc(1u, sizeof(*Client));
    if ( Client == NULL ) goto unavailable;
    if ( !xrtWsUpgradeResponseFields(xrtStrView(Upgrade.Accept), Upgrade.Protocol, xrtStrView(""), Fields, XWS_UPGRADE_RESPONSE_FIELDS_MAX, &Count) ||
         !xrtHttp1ResponseWrite(XHTTP_VERSION_1_1, 101u, XRT_STR_LITERAL("Switching Protocols"), Fields, Count,
            Client->Head, sizeof(Client->Head), &Client->HeadSize) ) goto unavailable;
    if ( Context->Request->tls != NULL ) Client->Request.tls = xrtTlsStreamRef(Context->Request->tls);
    else Client->Request.tcp = xrtNetStreamRef(Context->Request->tcp);
    if ( Client->Request.tcp == NULL && Client->Request.tls == NULL ) goto unavailable;
    Client->Context.Request = &Client->Request;
    xrtMutexLock(g_MdoLive.Lock);
    for ( Slot = 0u; Slot < MDO_LIVE_CLIENTS; ++Slot ) if ( g_MdoLive.Clients[Slot] == NULL ) break;
    if ( Slot == MDO_LIVE_CLIENTS || g_MdoLive.Stopping ) goto unlock;
    if ( g_MdoLive.Pool == NULL ) {
        xtaskpoolconfig PoolConfig = {0}; PoolConfig.Threads = MDO_LIVE_CLIENTS; PoolConfig.QueueLimit = MDO_LIVE_CLIENTS;
        g_MdoLive.Pool = xrtTaskPoolCreate(&PoolConfig);
        if ( g_MdoLive.Pool == NULL ) goto unlock;
    }
    xrtFutureDestroy(g_MdoLive.Futures[Slot]); g_MdoLive.Futures[Slot] = NULL;
    Client->Slot = Slot; Client->StateRevision = g_MdoLive.StateRevision;
    g_MdoLive.Clients[Slot] = Client; Args.Destroy = MdoLiveDrop;
    Future = xrtTaskSubmit(g_MdoLive.Pool, MdoLiveRun, Client, &Args);
    if ( Future != NULL ) g_MdoLive.Futures[Slot] = Future;
    else g_MdoLive.Clients[Slot] = NULL;
unlock:
    xrtMutexUnlock(g_MdoLive.Lock);
    if ( Future != NULL ) { Context->Takeover = true; return true; }
unavailable:
    if ( Client != NULL ) { xrtNetStreamDestroy(Client->Request.tcp); xrtTlsStreamDestroy(Client->Request.tls); xrtFree(Client); }
    return MdoApiReplyError(Context, 503u, "live_unavailable", "The live connection limit has been reached; retry later", NULL);
}
