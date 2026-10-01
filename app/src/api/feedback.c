#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"
#include "../../include/mdo/project_lifecycle.h"
#include "../../include/mdo/home.h"
#include "../../include/mdo/sessions.h"

/* Feedback is UI state, separate from the agent's checkpoint and event log. */
#define MDO_FEEDBACK_MAX_ITEMS 512u
#define MDO_FEEDBACK_MAX_BYTES (32u * 1024u)
#define MDO_FEEDBACK_REPLAY_PAGE 1000u
#define MDO_FEEDBACK_LIST_LIMIT 50u
#define MDO_FEEDBACK_SCAN_LIMIT 100u

typedef struct MdoFeedbackItem {
    uint64 EventId;
    bool Good;
} MdoFeedbackItem;

static xmutex* g_MdoFeedbackLock;

bool MdoApiFeedbackInit(void)
{
    if ( g_MdoFeedbackLock != NULL ) return true;
    g_MdoFeedbackLock = xrtMutexCreate();
    return g_MdoFeedbackLock != NULL;
}

void MdoApiFeedbackUnit(void)
{
    if ( g_MdoFeedbackLock != NULL ) xrtMutexDestroy(g_MdoFeedbackLock);
    g_MdoFeedbackLock = NULL;
}

bool MdoApiFeedbackCaptureTryLock(void)
{
    return g_MdoFeedbackLock != NULL && xrtMutexTryLock(g_MdoFeedbackLock);
}

void MdoApiFeedbackCaptureUnlock(void)
{
    xrtMutexUnlock(g_MdoFeedbackLock);
}

static bool MdoFeedbackCaptureId(xstrview View, char* Output,
    size_t Capacity)
{
    size_t i;
    if ( View.Size == 0u || View.Size >= Capacity || View.Data[0] == '.' )
        return false;
    for ( i = 0u; i < View.Size; ++i ) {
        unsigned char Byte = (unsigned char)View.Data[i];
        if ( (Byte >= 'a' && Byte <= 'z') ||
             (Byte >= 'A' && Byte <= 'Z') ||
             (Byte >= '0' && Byte <= '9') || Byte == '-' || Byte == '_' ||
             Byte == '.' ) continue;
        return false;
    }
    memcpy(Output, View.Data, View.Size);
    Output[View.Size] = '\0';
    return true;
}

static bool MdoFeedbackPath(char Path[MDO_SESSION_PATH_CAPACITY],
    const char* ProjectId, const char* SessionId)
{
    int Size = snprintf(Path, MDO_SESSION_PATH_CAPACITY,
        "sessions/%s/%s/feedback.json", ProjectId, SessionId);
    return Size > 0 && (size_t)Size < MDO_SESSION_PATH_CAPACITY;
}

static bool MdoFeedbackUInt(const xvalue* Object, cstr Name,
    uint64* Output)
{
    const xvalue* Value = xrtValueObjectGet(Object, xrtStrView(Name));
    int64 Signed;
    if ( xrtValueType(Value) == XVALUE_UINT )
        return xrtValueGetUInt(Value, Output);
    if ( xrtValueType(Value) != XVALUE_INT ||
         !xrtValueGetInt(Value, &Signed) || Signed < 0 ) return false;
    *Output = (uint64)Signed;
    return true;
}

static bool MdoFeedbackValue(const xvalue* Object, bool* Good)
{
    const xvalue* Value = xrtValueObjectGet(Object, XRT_STR_LITERAL("value"));
    xstrview Text;
    if ( xrtValueType(Value) != XVALUE_STRING ||
         !xrtValueGetString(Value, &Text) ) return false;
    if ( Text.Size == 4u && memcmp(Text.Data, "good", 4u) == 0 ) {
        *Good = true;
        return true;
    }
    if ( Text.Size == 3u && memcmp(Text.Data, "bad", 3u) == 0 ) {
        *Good = false;
        return true;
    }
    return false;
}

/* Called under the feedback lock; malformed sidecars are never overwritten. */
static bool MdoFeedbackRead(const char* Path, MdoFeedbackItem* Items,
    size_t* Count)
{
    bool Exists = false;
    xfileinfo Info;
    xfile File = NULL;
    char* Bytes = NULL;
    xvalue* Root = NULL;
    const xvalue* Array;
    xjsonreadconfig Config;
    uint64 Schema;
    size_t i;
    bool Ok = false;
    *Count = 0u;
    if ( !MdoHomeExternalStat(Path, &Exists, &Info) ) return false;
    if ( !Exists ) return true;
    if ( Info.Type != XFILE_TYPE_FILE ||
         (Info.Available & XFILE_INFO_SIZE) == 0u ||
         Info.Size > MDO_FEEDBACK_MAX_BYTES || Info.Size > SIZE_MAX - 1u )
        return false;
    File = MdoHomeOpenRead(Path);
    Bytes = (char*)xrtMalloc((size_t)Info.Size + 1u);
    if ( File == NULL || Bytes == NULL ||
         (Info.Size != 0u && !xrtReadFull(File, Bytes,
            (size_t)Info.Size, NULL)) ) goto done;
    Bytes[Info.Size] = '\0';
    xrtJsonReadConfigInit(&Config);
    Config.MaxInputBytes = MDO_FEEDBACK_MAX_BYTES;
    Config.MaxDepth = 4u;
    Config.MaxValues = MDO_FEEDBACK_MAX_ITEMS * 3u + 2u;
    Config.MaxContainerItems = MDO_FEEDBACK_MAX_ITEMS;
    Root = xrtJsonRead(xrtStrViewN(Bytes, (size_t)Info.Size), &Config);
    Array = Root != NULL ? xrtValueObjectGet(Root,
        XRT_STR_LITERAL("items")) : NULL;
    if ( xrtValueType(Root) != XVALUE_OBJECT ||
         xrtValueCount(Root) != 2u ||
         !MdoFeedbackUInt(Root, "schema_version", &Schema) || Schema != 1u ||
         xrtValueType(Array) != XVALUE_ARRAY ||
         xrtValueCount(Array) > MDO_FEEDBACK_MAX_ITEMS ) goto done;
    for ( i = 0u; i < xrtValueCount(Array); ++i ) {
        const xvalue* Item = xrtValueArrayGet(Array, i);
        uint64 EventId;
        size_t j;
        bool Good;
        if ( xrtValueType(Item) != XVALUE_OBJECT ||
             xrtValueCount(Item) != 2u ||
             !MdoFeedbackUInt(Item, "event_id", &EventId) ||
             EventId == 0u || !MdoFeedbackValue(Item, &Good) ) goto done;
        for ( j = 0u; j < i; ++j )
            if ( Items[j].EventId == EventId ) goto done;
        Items[i].EventId = EventId;
        Items[i].Good = Good;
    }
    *Count = xrtValueCount(Array);
    Ok = true;
done:
    xrtValueRelease(Root);
    xrtFree(Bytes);
    if ( File != NULL && !xrtClose(File) ) Ok = false;
    return Ok;
}

static bool MdoFeedbackWrite(const char* Path,
    const MdoFeedbackItem* Items, size_t Count)
{
    char Bytes[MDO_FEEDBACK_MAX_BYTES];
    size_t Used = 0u;
    size_t i;
    int Written = snprintf(Bytes, sizeof(Bytes),
        "{\"schema_version\":1,\"items\":[");
    if ( Written < 0 || (size_t)Written >= sizeof(Bytes) ) return false;
    Used = (size_t)Written;
    for ( i = 0u; i < Count; ++i ) {
        Written = snprintf(Bytes + Used, sizeof(Bytes) - Used,
            "%s{\"event_id\":%llu,\"value\":\"%s\"}",
            i != 0u ? "," : "", (unsigned long long)Items[i].EventId,
            Items[i].Good ? "good" : "bad");
        if ( Written < 0 || (size_t)Written >= sizeof(Bytes) - Used )
            return false;
        Used += (size_t)Written;
    }
    if ( Used + 2u > sizeof(Bytes) ) return false;
    Bytes[Used++] = ']';
    Bytes[Used++] = '}';
    return MdoHomeAtomicWrite(Path, Bytes, Used, false);
}

static bool MdoFeedbackIsCompletedModel(const char* ProjectId,
    const char* SessionId, uint64 EventId)
{
    MdoSessionEventSnapshot* Snapshot;
    MdoSessionEventInfo Event;
    xwork_error Error;
    bool Found = false;
    if ( EventId == 0u ) return false;
    memset(&Error, 0, sizeof(Error));
    Snapshot = MdoSessionEventReplay(ProjectId, SessionId, EventId - 1u,
        1u, &Error);
    if ( Snapshot == NULL ) return false;
    memset(&Event, 0, sizeof(Event));
    Event.Size = sizeof(Event);
    if ( MdoSessionEventSnapshotAt(Snapshot, 0u, &Event) )
        Found = Event.EventId == EventId &&
            Event.Kind == XWORK_EVENT_MODEL_DONE && Event.Success;
    MdoSessionEventSnapshotRelease(Snapshot);
    return Found;
}

/* History markers describe the exact range removed from the UI journal.
 * Keep votes for older events that merely fell out of the bounded journal. */
static bool MdoFeedbackPruneRemoved(const char* ProjectId,
    const char* SessionId, MdoFeedbackItem* Items, size_t* Count,
    bool* Changed)
{
    MdoSessionEventSnapshot* Snapshot;
    MdoSessionEventInfo Event;
    xwork_error Error;
    uint64 Cursor = UINT64_MAX;
    size_t i;
    *Changed = false;
    if ( *Count == 0u ) return true;
    for ( i = 0u; i < *Count; ++i )
        if ( Items[i].EventId < Cursor ) Cursor = Items[i].EventId;
    --Cursor;
    for ( ; ; ) {
        uint64 Next;
        uint64 Latest;
        size_t PageCount;
        memset(&Error, 0, sizeof(Error));
        Snapshot = MdoSessionEventReplay(ProjectId, SessionId, Cursor,
            MDO_FEEDBACK_REPLAY_PAGE, &Error);
        if ( Snapshot == NULL ) return false;
        PageCount = MdoSessionEventSnapshotCount(Snapshot);
        for ( i = 0u; i < PageCount; ++i ) {
            size_t ItemIndex = 0u;
            memset(&Event, 0, sizeof(Event));
            Event.Size = sizeof(Event);
            if ( !MdoSessionEventSnapshotAt(Snapshot, i, &Event) ) {
                MdoSessionEventSnapshotRelease(Snapshot);
                return false;
            }
            if ( Event.Kind != MDO_SESSION_EVENT_HISTORY_TRUNCATED ||
                 Event.SourceEventId == 0u ) continue;
            while ( ItemIndex < *Count ) {
                if ( Items[ItemIndex].EventId >= Event.SourceEventId &&
                     Items[ItemIndex].EventId < Event.EventId ) {
                    if ( ItemIndex + 1u < *Count )
                        memmove(&Items[ItemIndex], &Items[ItemIndex + 1u],
                            (*Count - ItemIndex - 1u) * sizeof(Items[0]));
                    --*Count;
                    *Changed = true;
                } else ++ItemIndex;
            }
        }
        Next = MdoSessionEventSnapshotNextCursor(Snapshot);
        Latest = MdoSessionEventSnapshotLatestId(Snapshot);
        MdoSessionEventSnapshotRelease(Snapshot);
        if ( PageCount < MDO_FEEDBACK_REPLAY_PAGE || Next >= Latest )
            return true;
        if ( Next <= Cursor ) return false;
        Cursor = Next;
    }
}

/* Called after history mutation; GET/PUT also repair an interrupted cleanup. */
bool MdoApiFeedbackReconcile(const char* ProjectId, const char* SessionId)
{
    char Path[MDO_SESSION_PATH_CAPACITY];
    MdoFeedbackItem Items[MDO_FEEDBACK_MAX_ITEMS];
    size_t Count = 0u;
    bool Changed = false;
    bool Ok;
    MdoProjectLease* Lease;
    if ( !MdoFeedbackPath(Path, ProjectId, SessionId) ) return false;
    /* The global feedback listing also repairs sidecars, without a scoped
     * request or an open session. Protect that independent write boundary. */
    Lease = MdoProjectLeaseAcquire(ProjectId, MDO_PROJECT_LEASE_SHARED, NULL);
    if ( Lease == NULL ) return false;
    xrtMutexLock(g_MdoFeedbackLock);
    Ok = MdoFeedbackRead(Path, Items, &Count);
    if ( Ok ) Ok = MdoFeedbackPruneRemoved(ProjectId, SessionId,
        Items, &Count, &Changed);
    if ( Ok && Changed ) Ok = MdoFeedbackWrite(Path, Items, Count);
    xrtMutexUnlock(g_MdoFeedbackLock);
    MdoProjectLeaseRelease(Lease);
    return Ok;
}

static xvalue* MdoFeedbackResponse(const MdoFeedbackItem* Items,
    size_t Count)
{
    xvalue* Data = xrtValueObject();
    xvalue* Array = xrtValueArray();
    size_t i;
    bool Ok = Data != NULL && Array != NULL;
    for ( i = 0u; Ok && i < Count; ++i ) {
        xvalue* Item = xrtValueObject();
        Ok = Item != NULL &&
            MdoApiValueSetUInt(Item, "event_id", Items[i].EventId) &&
            MdoApiValueSetString(Item, "value",
                Items[i].Good ? "good" : "bad") &&
            MdoApiValueAppendTake(Array, &Item);
        xrtValueRelease(Item);
    }
    if ( Ok ) Ok = MdoApiValueSetTake(Data, "items", &Array);
    xrtValueRelease(Array);
    if ( !Ok ) { xrtValueRelease(Data); return NULL; }
    return Data;
}

static bool MdoFeedbackDecimal(xstrview Text, uint64* Number)
{
    size_t Index;
    uint64 Value = 0u;
    if ( Text.Size == 0u ) return false;
    for ( Index = 0u; Index < Text.Size; ++Index ) {
        uint64 Digit;
        if ( Text.Data[Index] < '0' || Text.Data[Index] > '9' ) return false;
        Digit = (uint64)(Text.Data[Index] - '0');
        if ( Value > (UINT64_MAX - Digit) / 10u ) return false;
        Value = Value * 10u + Digit;
    }
    *Number = Value;
    return true;
}

static bool MdoFeedbackListCursor(xstrview Query, uint64* Generation,
    size_t* SessionIndex, uint64* AfterEventId)
{
    xstrview Parts[3];
    size_t Start = 7u;
    size_t Index;
    uint64 Session;
    if ( Query.Size == 0u ) {
        *Generation = 0u;
        *SessionIndex = 0u;
        *AfterEventId = 0u;
        return true;
    }
    if ( Query.Size < 12u || memcmp(Query.Data, "cursor=", 7u) != 0 )
        return false;
    for ( Index = 0u; Index < 3u; ++Index ) {
        size_t End = Start;
        while ( End < Query.Size && Query.Data[End] != '.' ) ++End;
        if ( (Index < 2u && End == Query.Size) ||
             (Index == 2u && End != Query.Size) ) return false;
        Parts[Index] = xrtStrViewN(Query.Data + Start, End - Start);
        Start = End + 1u;
    }
    if ( !MdoFeedbackDecimal(Parts[0], Generation) ||
         !MdoFeedbackDecimal(Parts[1], &Session) ||
         !MdoFeedbackDecimal(Parts[2], AfterEventId) ||
         Session > SIZE_MAX ) return false;
    *SessionIndex = (size_t)Session;
    return true;
}

static int MdoFeedbackCompareEventId(const void* Left, const void* Right)
{
    const MdoFeedbackItem* A = (const MdoFeedbackItem*)Left;
    const MdoFeedbackItem* B = (const MdoFeedbackItem*)Right;
    return A->EventId < B->EventId ? -1 : A->EventId > B->EventId ? 1 : 0;
}

static int64 MdoFeedbackOccurredAt(const char* ProjectId,
    const char* SessionId, uint64 EventId)
{
    MdoSessionEventSnapshot* Snapshot;
    MdoSessionEventInfo Event;
    xwork_error Error;
    int64 Time = 0;
    if ( EventId == 0u ) return 0;
    memset(&Error, 0, sizeof(Error));
    Snapshot = MdoSessionEventReplay(ProjectId, SessionId, EventId - 1u,
        1u, &Error);
    if ( Snapshot == NULL ) return 0;
    memset(&Event, 0, sizeof(Event));
    Event.Size = sizeof(Event);
    if ( MdoSessionEventSnapshotAt(Snapshot, 0u, &Event) &&
         Event.EventId == EventId ) Time = Event.OccurredAt;
    MdoSessionEventSnapshotRelease(Snapshot);
    return Time;
}

static xvalue* MdoFeedbackListItem(const MdoSessionInfo* Session,
    const MdoFeedbackItem* Feedback)
{
    xvalue* Item = xrtValueObject();
    bool Ok = Item != NULL &&
        MdoApiValueSetString(Item, "project_id", Session->ProjectId) &&
        MdoApiValueSetString(Item, "session_id", Session->Id) &&
        MdoApiValueSetString(Item, "title", Session->Title) &&
        MdoApiValueSetUInt(Item, "event_id", Feedback->EventId) &&
        MdoApiValueSetString(Item, "value", Feedback->Good ? "good" : "bad") &&
        MdoApiValueSetInt(Item, "occurred_at",
            MdoFeedbackOccurredAt(Session->ProjectId, Session->Id,
                Feedback->EventId));
    if ( !Ok ) { xrtValueRelease(Item); return NULL; }
    return Item;
}

bool MdoApiFeedbackListRoute(MdoApiContext* Context)
{
    MdoSessionCatalog* Catalog;
    MdoSessionInfo Session;
    MdoFeedbackItem Feedback[MDO_FEEDBACK_MAX_ITEMS];
    xwork_error Error;
    xvalue* Data = NULL;
    xvalue* Array = NULL;
    uint64 ExpectedGeneration;
    uint64 AfterEventId;
    uint64 Generation;
    size_t SessionIndex;
    size_t TotalSessions;
    size_t Scanned = 0u;
    size_t Emitted = 0u;
    char Cursor[96] = "";
    bool Ok = true;

    if ( !MdoFeedbackListCursor(Context->Target.Query,
            &ExpectedGeneration, &SessionIndex, &AfterEventId) )
        return MdoApiReplyError(Context, 400u, "invalid_query",
            "Expected cursor=generation.session_index.after_event_id", NULL);
    memset(&Error, 0, sizeof(Error));
    Catalog = MdoSessionCatalogSnapshot(&Error);
    if ( Catalog == NULL ) return MdoApiReplyError(Context, 503u,
        "feedback_unavailable", "Session catalog could not be read", NULL);
    Generation = MdoSessionCatalogGeneration(Catalog);
    TotalSessions = MdoSessionCatalogCount(Catalog);
    if ( SessionIndex > TotalSessions ||
         (Context->Target.Query.Size != 0u &&
          ExpectedGeneration != Generation) ) {
        MdoSessionCatalogRelease(Catalog);
        return MdoApiReplyError(Context, 409u, "feedback_cursor_stale",
            "Session catalog changed; restart feedback listing", NULL);
    }
    Array = xrtValueArray();
    Ok = Array != NULL;
    while ( Ok && SessionIndex < TotalSessions &&
            Scanned < MDO_FEEDBACK_SCAN_LIMIT &&
            Emitted < MDO_FEEDBACK_LIST_LIMIT ) {
        char Path[MDO_SESSION_PATH_CAPACITY];
        size_t Count = 0u;
        size_t Index;
        memset(&Session, 0, sizeof(Session));
        Session.Size = sizeof(Session);
        if ( !MdoSessionCatalogAt(Catalog, SessionIndex, &Session) ||
             !MdoFeedbackPath(Path, Session.ProjectId, Session.Id) ||
             !MdoApiFeedbackReconcile(Session.ProjectId, Session.Id) ) {
            Ok = false;
            break;
        }
        xrtMutexLock(g_MdoFeedbackLock);
        Ok = MdoFeedbackRead(Path, Feedback, &Count);
        xrtMutexUnlock(g_MdoFeedbackLock);
        if ( !Ok ) break;
        qsort(Feedback, Count, sizeof(Feedback[0]), MdoFeedbackCompareEventId);
        for ( Index = 0u; Ok && Index < Count; ++Index ) {
            xvalue* Item;
            if ( Feedback[Index].EventId <= AfterEventId ) continue;
            Item = MdoFeedbackListItem(&Session, &Feedback[Index]);
            Ok = Item != NULL && MdoApiValueAppendTake(Array, &Item);
            xrtValueRelease(Item);
            if ( !Ok ) break;
            ++Emitted;
            if ( Emitted == MDO_FEEDBACK_LIST_LIMIT ) {
                AfterEventId = Feedback[Index].EventId;
                break;
            }
        }
        ++Scanned;
        if ( Emitted == MDO_FEEDBACK_LIST_LIMIT && Index + 1u < Count )
            break;
        ++SessionIndex;
        AfterEventId = 0u;
    }
    if ( Ok && SessionIndex < TotalSessions ) {
        int Written = snprintf(Cursor, sizeof(Cursor), "%llu.%llu.%llu",
            (unsigned long long)Generation,
            (unsigned long long)SessionIndex,
            (unsigned long long)AfterEventId);
        Ok = Written > 0 && (size_t)Written < sizeof(Cursor);
    }
    Data = Ok ? xrtValueObject() : NULL;
    Ok = Ok && Data != NULL &&
        MdoApiValueSetUInt(Data, "generation", Generation) &&
        MdoApiValueSetUInt(Data, "scanned_sessions", Scanned) &&
        MdoApiValueSetUInt(Data, "catalog_diagnostics",
            MdoSessionCatalogDiagnosticCount(Catalog)) &&
        MdoApiValueSetString(Data, "next_cursor", Cursor) &&
        MdoApiValueSetTake(Data, "items", &Array);
    xrtValueRelease(Array);
    MdoSessionCatalogRelease(Catalog);
    if ( !Ok ) {
        xrtValueRelease(Data);
        return MdoApiReplyError(Context, 503u, "feedback_unavailable",
            "Feedback listing could not be read", NULL);
    }
    return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
}

bool MdoApiFeedbackRoute(MdoApiContext* Context)
{
    char ProjectId[MDO_PROJECT_ID_CAPACITY];
    char SessionId[MDO_SESSION_ID_CAPACITY];
    char Path[MDO_SESSION_PATH_CAPACITY];
    MdoFeedbackItem Items[MDO_FEEDBACK_MAX_ITEMS];
    size_t Count = 0u;
    MdoSession* Session;
    xwork_error Error;
    MdoApiJsonBody Body;
    MdoApiBodyStatus BodyStatus;
    const xvalue* Value;
    xstrview Text;
    uint64 EventId = 0u;
    bool Good = false;
    bool Remove = false;
    bool Changed = false;
    size_t i;
    bool Ok;
    xvalue* Data;

    if ( Context->ParamCount != 2u ||
         !MdoFeedbackCaptureId(Context->Params[0], ProjectId,
            sizeof(ProjectId)) ||
         !MdoFeedbackCaptureId(Context->Params[1], SessionId,
            sizeof(SessionId)) ||
         !MdoFeedbackPath(Path, ProjectId, SessionId) )
        return MdoApiReplyError(Context, 400u, "invalid_path",
            "Project and session identifiers are invalid", NULL);
    memset(&Error, 0, sizeof(Error));
    Session = MdoSessionLoad(ProjectId, SessionId, &Error);
    if ( Session == NULL ) return MdoApiReplyError(Context, 404u,
        "session_not_found", "The requested session does not exist", NULL);
    MdoSessionRelease(Session);
    if ( Context->Request->head->MethodCode == XHTTP_METHOD_PUT ) {
        BodyStatus = MdoApiJsonBodyRead(Context, &Body);
        if ( BodyStatus != MDO_API_BODY_OK )
            return MdoApiReplyBodyError(Context, BodyStatus);
        Value = Body.Value != NULL ? xrtValueObjectGet(Body.Value,
            XRT_STR_LITERAL("value")) : NULL;
        Ok = xrtValueType(Body.Value) == XVALUE_OBJECT &&
            xrtValueCount(Body.Value) == 2u &&
            MdoFeedbackUInt(Body.Value, "event_id", &EventId) &&
            EventId > 0u && xrtValueType(Value) == XVALUE_STRING &&
            xrtValueGetString(Value, &Text);
        if ( Ok ) {
            if ( Text.Size == 4u && memcmp(Text.Data, "good", 4u) == 0 )
                Good = true;
            else if ( Text.Size == 3u &&
                     memcmp(Text.Data, "bad", 3u) == 0 ) Good = false;
            else if ( Text.Size == 4u &&
                     memcmp(Text.Data, "none", 4u) == 0 ) Remove = true;
            else Ok = false;
        }
        MdoApiJsonBodyUnit(&Body);
        if ( !Ok ) return MdoApiReplyError(Context, 422u,
            "feedback_invalid", "Expected a completed model event and good, bad or none", NULL);
        if ( !MdoFeedbackIsCompletedModel(ProjectId, SessionId, EventId) )
            return MdoApiReplyError(Context, 422u, "feedback_event_invalid",
                "The event is not a completed model reply in this session", NULL);
    }
    xrtMutexLock(g_MdoFeedbackLock);
    Ok = MdoFeedbackRead(Path, Items, &Count);
    if ( Ok ) Ok = MdoFeedbackPruneRemoved(ProjectId, SessionId,
        Items, &Count, &Changed);
    if ( Ok && Context->Request->head->MethodCode == XHTTP_METHOD_PUT ) {
        for ( i = 0u; i < Count && Items[i].EventId != EventId; ++i ) {}
        if ( i < Count && Remove ) {
            memmove(&Items[i], &Items[i + 1u],
                (Count - i - 1u) * sizeof(Items[0]));
            --Count;
        } else if ( !Remove ) {
            if ( i == Count && Count == MDO_FEEDBACK_MAX_ITEMS ) Ok = false;
            else {
                Items[i].EventId = EventId;
                Items[i].Good = Good;
                if ( i == Count ) ++Count;
            }
        }
    }
    if ( Ok && (Changed ||
         Context->Request->head->MethodCode == XHTTP_METHOD_PUT) )
        Ok = MdoFeedbackWrite(Path, Items, Count);
    Data = Ok ? MdoFeedbackResponse(Items, Count) : NULL;
    xrtMutexUnlock(g_MdoFeedbackLock);
    if ( Data == NULL ) return MdoApiReplyError(Context, 503u,
        "feedback_unavailable", "Session feedback could not be read or saved", NULL);
    return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
}
