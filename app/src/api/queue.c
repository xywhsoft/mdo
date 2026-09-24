#include <stdio.h>
#include <string.h>

#include "internal.h"
#include "../../include/mdo/home.h"
#include "../../include/mdo/sessions.h"

#define MDO_QUEUE_MAX_ITEMS 20u
#define MDO_QUEUE_MAX_TEXT (64u * 1024u - 1u)
#define MDO_QUEUE_MAX_TOTAL_TEXT (192u * 1024u)
#define MDO_QUEUE_FILE_MAX (256u * 1024u)
#define MDO_QUEUE_ID_SIZE 32u

typedef struct MdoQueueItem {
    char Id[MDO_QUEUE_ID_SIZE + 1u];
    char* Text;
    size_t TextSize;
    char Attachments[4][33];
    size_t AttachmentCount;
    bool Sending;
    bool Priority;
} MdoQueueItem;

typedef struct MdoQueue {
    MdoQueueItem Items[MDO_QUEUE_MAX_ITEMS];
    size_t Count;
    size_t TextBytes;
} MdoQueue;

static xmutex* g_MdoQueueLock;

bool MdoApiQueueInit(void)
{
    if ( g_MdoQueueLock != NULL ) return true;
    g_MdoQueueLock = xrtMutexCreate();
    return g_MdoQueueLock != NULL;
}

void MdoApiQueueUnit(void)
{
    if ( g_MdoQueueLock != NULL ) xrtMutexDestroy(g_MdoQueueLock);
    g_MdoQueueLock = NULL;
}

static void MdoQueueRelease(MdoQueue* Queue)
{
    size_t i;
    for ( i = 0u; i < Queue->Count; ++i ) xrtFree(Queue->Items[i].Text);
    memset(Queue, 0, sizeof(*Queue));
}

static bool MdoQueueCaptureId(xstrview View, char* Output,
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

static bool MdoQueueId(xstrview View,
    char Output[MDO_QUEUE_ID_SIZE + 1u])
{
    size_t i;
    if ( View.Size != MDO_QUEUE_ID_SIZE ) return false;
    for ( i = 0u; i < View.Size; ++i ) {
        unsigned char Byte = (unsigned char)View.Data[i];
        if ( !((Byte >= '0' && Byte <= '9') ||
               (Byte >= 'a' && Byte <= 'f')) ) return false;
    }
    memcpy(Output, View.Data, View.Size);
    Output[View.Size] = '\0';
    return true;
}

static bool MdoQueueString(const xvalue* Object, cstr Key,
    xstrview* Text)
{
    const xvalue* Value = xrtValueObjectGet(Object, xrtStrView(Key));
    return xrtValueType(Value) == XVALUE_STRING &&
        xrtValueGetString(Value, Text);
}

static bool MdoQueueText(xstrview Text, bool AllowEmpty)
{
    return (AllowEmpty || Text.Size > 0u) &&
        Text.Size <= MDO_QUEUE_MAX_TEXT &&
        memchr(Text.Data, 0, Text.Size) == NULL &&
        xrtUtf8Valid(Text, NULL);
}

static bool MdoQueueBool(const xvalue* Object, cstr Key, bool* Result)
{
    const xvalue* Value = xrtValueObjectGet(Object, xrtStrView(Key));
    return xrtValueType(Value) == XVALUE_BOOL &&
        xrtValueGetBool(Value, Result);
}

static bool MdoQueueState(xstrview State, bool* Sending)
{
    if ( State.Size == 7u && memcmp(State.Data, "pending", 7u) == 0 ) {
        *Sending = false;
        return true;
    }
    if ( State.Size == 7u && memcmp(State.Data, "sending", 7u) == 0 ) {
        *Sending = true;
        return true;
    }
    return false;
}

static size_t MdoQueueFind(const MdoQueue* Queue, const char* Id)
{
    size_t i;
    for ( i = 0u; i < Queue->Count; ++i )
        if ( strcmp(Queue->Items[i].Id, Id) == 0 ) return i;
    return SIZE_MAX;
}

static bool MdoQueueInsert(MdoQueue* Queue, const char* Id,
    xstrview Text, const char Attachments[4][33],
    size_t AttachmentCount, bool First, bool Priority)
{
    MdoQueueItem* Item;
    char* Copy;
    size_t Position = First && Queue->Count != 0u &&
        Queue->Items[0].Sending ? 1u : (First ? 0u : Queue->Count);
    if ( Queue->Count >= MDO_QUEUE_MAX_ITEMS ||
         Text.Size > MDO_QUEUE_MAX_TOTAL_TEXT - Queue->TextBytes )
        return false;
    Copy = (char*)xrtMalloc(Text.Size + 1u);
    if ( Copy == NULL ) return false;
    if ( Text.Size != 0u ) memcpy(Copy, Text.Data, Text.Size);
    Copy[Text.Size] = '\0';
    if ( Position < Queue->Count )
        memmove(&Queue->Items[Position + 1u], &Queue->Items[Position],
            (Queue->Count - Position) * sizeof(Queue->Items[0]));
    Item = &Queue->Items[Position];
    memset(Item, 0, sizeof(*Item));
    memcpy(Item->Id, Id, MDO_QUEUE_ID_SIZE + 1u);
    Item->Text = Copy;
    Item->TextSize = Text.Size;
    memcpy(Item->Attachments, Attachments, sizeof(Item->Attachments));
    Item->AttachmentCount = AttachmentCount;
    Item->Priority = Priority;
    Queue->TextBytes += Text.Size;
    Queue->Count++;
    return true;
}

static bool MdoQueueRead(const char* Path, MdoQueue* Queue)
{
    bool Exists = false;
    xfileinfo Info;
    xfile File = NULL;
    char* Bytes = NULL;
    xvalue* Root = NULL;
    const xvalue* Items;
    xjsonreadconfig Config;
    uint64 Schema;
    size_t i;
    bool Ok = false;
    memset(Queue, 0, sizeof(*Queue));
    if ( !MdoHomeExternalStat(Path, &Exists, &Info) ) return false;
    if ( !Exists ) return true;
    if ( Info.Type != XFILE_TYPE_FILE ||
         (Info.Available & XFILE_INFO_SIZE) == 0u ||
         Info.Size > MDO_QUEUE_FILE_MAX || Info.Size > SIZE_MAX - 1u )
        return false;
    File = MdoHomeOpenRead(Path);
    Bytes = (char*)xrtMalloc((size_t)Info.Size + 1u);
    if ( File == NULL || Bytes == NULL ||
         (Info.Size != 0u && !xrtReadFull(File, Bytes,
            (size_t)Info.Size, NULL)) ) goto done;
    Bytes[Info.Size] = '\0';
    xrtJsonReadConfigInit(&Config);
    Config.MaxInputBytes = MDO_QUEUE_FILE_MAX;
    Config.MaxDepth = 5u;
    Config.MaxValues = 256u;
    Config.MaxContainerItems = MDO_QUEUE_MAX_ITEMS;
    Root = xrtJsonRead(xrtStrViewN(Bytes, (size_t)Info.Size), &Config);
    Items = Root != NULL ? xrtValueObjectGet(Root,
        XRT_STR_LITERAL("items")) : NULL;
    if ( xrtValueType(Root) != XVALUE_OBJECT ||
         xrtValueCount(Root) != 2u ||
         xrtValueType(Items) != XVALUE_ARRAY ||
         xrtValueCount(Items) > MDO_QUEUE_MAX_ITEMS ) goto done;
    {
        const xvalue* Version = xrtValueObjectGet(Root,
            XRT_STR_LITERAL("schema_version"));
        int64 Signed;
        if ( xrtValueType(Version) == XVALUE_UINT ) {
            if ( !xrtValueGetUInt(Version, &Schema) ) goto done;
        } else if ( xrtValueType(Version) == XVALUE_INT ) {
            if ( !xrtValueGetInt(Version, &Signed) || Signed < 0 ) goto done;
            Schema = (uint64)Signed;
        } else goto done;
        if ( Schema != 1u && Schema != 2u && Schema != 3u ) goto done;
    }
    for ( i = 0u; i < xrtValueCount(Items); ++i ) {
        const xvalue* Entry = xrtValueArrayGet(Items, i);
        xstrview Id;
        xstrview Text;
        xstrview State;
        char IdText[MDO_QUEUE_ID_SIZE + 1u];
        char Attachments[4][33] = {{ 0 }};
        size_t AttachmentCount = 0u;
        bool Sending;
        bool Priority = false;
        if ( xrtValueType(Entry) != XVALUE_OBJECT ||
             xrtValueCount(Entry) != (Schema == 1u ? 3u :
                (Schema == 2u ? 4u : 5u)) ||
             !MdoQueueString(Entry, "id", &Id) ||
             !MdoQueueString(Entry, "text", &Text) ||
             !MdoQueueString(Entry, "state", &State) ||
             !MdoQueueId(Id, IdText) ||
             (Schema >= 2u &&
              !MdoAttachmentIdsRead(xrtValueObjectGet(Entry,
                XRT_STR_LITERAL("attachments")), Attachments,
                &AttachmentCount)) ||
             (Schema == 3u &&
              !MdoQueueBool(Entry, "priority", &Priority)) ||
             !MdoQueueText(Text, AttachmentCount != 0u) ||
             !MdoQueueState(State, &Sending) ||
             MdoQueueFind(Queue, IdText) != SIZE_MAX ||
             !MdoQueueInsert(Queue, IdText, Text, Attachments,
                AttachmentCount, false, Priority) ) goto done;
        Queue->Items[Queue->Count - 1u].Sending = Sending;
    }
    Ok = true;
done:
    xrtValueRelease(Root);
    xrtFree(Bytes);
    if ( File != NULL && !xrtClose(File) ) Ok = false;
    if ( !Ok ) MdoQueueRelease(Queue);
    return Ok;
}

bool MdoApiQueueAttachmentReferenced(const char* ProjectId,
    const char* SessionId, const char* Id, bool* Referenced)
{
    char Path[MDO_SESSION_PATH_CAPACITY];
    MdoQueue Queue;
    size_t i, j;
    bool Ok;
    int Written;
    if ( ProjectId == NULL || SessionId == NULL || Id == NULL ||
         Referenced == NULL ) return false;
    *Referenced = false;
    Written = snprintf(Path, sizeof(Path), "sessions/%s/%s/queue.json",
        ProjectId, SessionId);
    if ( Written <= 0 || (size_t)Written >= sizeof(Path) ) return false;
    xrtMutexLock(g_MdoQueueLock);
    Ok = MdoQueueRead(Path, &Queue);
    if ( Ok ) {
        for ( i = 0u; i < Queue.Count && !*Referenced; ++i )
            for ( j = 0u; j < Queue.Items[i].AttachmentCount; ++j )
                if ( strcmp(Queue.Items[i].Attachments[j], Id) == 0 ) {
                    *Referenced = true;
                    break;
                }
        MdoQueueRelease(&Queue);
    }
    xrtMutexUnlock(g_MdoQueueLock);
    return Ok;
}

static xvalue* MdoQueueValue(const MdoQueue* Queue)
{
    xvalue* Root = xrtValueObject();
    xvalue* Items = xrtValueArray();
    size_t i;
    bool Ok = Root != NULL && Items != NULL;
    for ( i = 0u; Ok && i < Queue->Count; ++i ) {
        const MdoQueueItem* Source = &Queue->Items[i];
        xvalue* Item = xrtValueObject();
        Ok = Item != NULL &&
            MdoApiValueSetString(Item, "id", Source->Id) &&
            MdoApiValueSetStringView(Item, "text",
                xrtStrViewN(Source->Text, Source->TextSize)) &&
            MdoApiValueSetString(Item, "state",
                Source->Sending ? "sending" : "pending") &&
            MdoAttachmentIdsWriteValue(Item, Source->Attachments,
                Source->AttachmentCount) &&
            MdoApiValueSetBool(Item, "priority", Source->Priority) &&
            MdoApiValueAppendTake(Items, &Item);
        xrtValueRelease(Item);
    }
    if ( Ok ) Ok = MdoApiValueSetTake(Root, "items", &Items);
    xrtValueRelease(Items);
    if ( !Ok ) { xrtValueRelease(Root); return NULL; }
    return Root;
}

static bool MdoQueueWrite(const char* Path, const MdoQueue* Queue)
{
    xvalue* Data = MdoQueueValue(Queue);
    char* Json;
    size_t Size = 0u;
    bool Ok;
    if ( Data == NULL || !MdoApiValueSetUInt(Data, "schema_version", 3u) ) {
        xrtValueRelease(Data);
        return false;
    }
    Json = xrtJsonStringify(Data, false, &Size);
    Ok = Json != NULL && Size <= MDO_QUEUE_FILE_MAX &&
        MdoHomeAtomicWrite(Path, Json, Size, false);
    xrtFree(Json);
    xrtValueRelease(Data);
    return Ok;
}

static bool MdoQueuePath(MdoApiContext* Context,
    char Path[MDO_SESSION_PATH_CAPACITY], MdoSessionStatus* Status,
    char ProjectId[MDO_PROJECT_ID_CAPACITY],
    char SessionId[MDO_SESSION_ID_CAPACITY])
{
    MdoSession* Session;
    MdoSessionInfo Info;
    xwork_error Error;
    int Written;
    if ( (Context->ParamCount != 2u && Context->ParamCount != 3u) ||
         !MdoQueueCaptureId(Context->Params[0], ProjectId,
            MDO_PROJECT_ID_CAPACITY) ||
         !MdoQueueCaptureId(Context->Params[1], SessionId,
            MDO_SESSION_ID_CAPACITY) ) return false;
    Written = snprintf(Path, MDO_SESSION_PATH_CAPACITY,
        "sessions/%s/%s/queue.json", ProjectId, SessionId);
    if ( Written <= 0 || (size_t)Written >= MDO_SESSION_PATH_CAPACITY )
        return false;
    memset(&Error, 0, sizeof(Error));
    Session = MdoSessionLoad(ProjectId, SessionId, &Error);
    if ( Session == NULL ) return false;
    memset(&Info, 0, sizeof(Info)); Info.Size = sizeof(Info);
    if ( !MdoSessionGetInfo(Session, &Info) ) {
        MdoSessionRelease(Session);
        return false;
    }
    MdoSessionRelease(Session);
    *Status = Info.Status;
    return true;
}

static bool MdoQueueReply(MdoApiContext* Context, uint16 Status,
    MdoQueue* Queue)
{
    xvalue* Data = MdoQueueValue(Queue);
    MdoQueueRelease(Queue);
    if ( Data == NULL ) return MdoApiReplyError(Context, 503u,
        "queue_unavailable", "The queue response could not be created", NULL);
    return MdoApiReplySuccessTake(Context, Status, Data, NULL);
}

bool MdoApiQueueRoute(MdoApiContext* Context)
{
    char Path[MDO_SESSION_PATH_CAPACITY];
    char ProjectId[MDO_PROJECT_ID_CAPACITY];
    char SessionId[MDO_SESSION_ID_CAPACITY];
    char Id[MDO_QUEUE_ID_SIZE + 1u];
    MdoSessionStatus SessionStatus;
    MdoQueue Queue;
    MdoApiJsonBody Body;
    MdoApiBodyStatus BodyStatus;
    xstrview IdView = { 0 };
    xstrview Text = { 0 };
    char Attachments[4][33] = {{ 0 }};
    size_t AttachmentCount = 0u;
    bool First = false;
    bool Priority = false;
    bool Add = Context->Request->head->MethodCode == XHTTP_METHOD_POST;
    bool Ok;
    bool AttachmentLocked = false;
    bool Duplicate = false;
    bool Full = false;
    size_t Index = SIZE_MAX;

    if ( !MdoQueuePath(Context, Path, &SessionStatus,
            ProjectId, SessionId) )
        return MdoApiReplyError(Context, 404u, "session_not_found",
            "The requested session does not exist", NULL);
    if ( Add ) {
        if ( SessionStatus != MDO_SESSION_ACTIVE )
            return MdoApiReplyError(Context, 409u, "session_state_conflict",
                "The session must be active before queueing a prompt", NULL);
        BodyStatus = MdoApiJsonBodyRead(Context, &Body);
        if ( BodyStatus != MDO_API_BODY_OK )
            return MdoApiReplyBodyError(Context, BodyStatus);
        AttachmentLocked = MdoApiAttachmentLock();
        if ( !AttachmentLocked ) {
            MdoApiJsonBodyUnit(&Body);
            return MdoApiReplyError(Context, 503u,
                "attachment_unavailable", "Image storage is unavailable",
                NULL);
        }
        {
            const xvalue* References = xrtValueObjectGet(Body.Value,
                XRT_STR_LITERAL("attachments"));
            const xvalue* PriorityValue = xrtValueObjectGet(Body.Value,
                XRT_STR_LITERAL("priority"));
            Ok = xrtValueType(Body.Value) == XVALUE_OBJECT &&
                xrtValueCount(Body.Value) == (References == NULL ? 3u : 4u) +
                    (PriorityValue == NULL ? 0u : 1u) &&
                MdoQueueString(Body.Value, "id", &IdView) &&
                MdoQueueString(Body.Value, "text", &Text) &&
                MdoQueueBool(Body.Value, "first", &First) &&
                (PriorityValue == NULL ||
                 MdoQueueBool(Body.Value, "priority", &Priority)) &&
                MdoQueueId(IdView, Id) &&
                (References == NULL ||
                 MdoAttachmentIdsRead(References, Attachments,
                    &AttachmentCount)) &&
                MdoQueueText(Text, AttachmentCount != 0u) &&
                MdoAttachmentIdsExist(ProjectId, SessionId, Attachments,
                    AttachmentCount);
        }
        if ( !Ok ) {
            MdoApiAttachmentUnlock();
            MdoApiJsonBodyUnit(&Body);
            return MdoApiReplyError(Context, 422u, "queue_item_invalid",
                "Expected a bounded prompt, item ID and first flag", NULL);
        }
    }
    xrtMutexLock(g_MdoQueueLock);
    Ok = MdoQueueRead(Path, &Queue);
    if ( Ok && Add ) {
        Index = MdoQueueFind(&Queue, Id);
        if ( Index != SIZE_MAX ) {
            Duplicate = Queue.Items[Index].TextSize != Text.Size ||
                (Text.Size != 0u &&
                 memcmp(Queue.Items[Index].Text, Text.Data, Text.Size) != 0) ||
                Queue.Items[Index].AttachmentCount != AttachmentCount ||
                Queue.Items[Index].Priority != Priority ||
                memcmp(Queue.Items[Index].Attachments, Attachments,
                    sizeof(Attachments)) != 0;
        } else {
            Full = Queue.Count >= MDO_QUEUE_MAX_ITEMS ||
                Text.Size > MDO_QUEUE_MAX_TOTAL_TEXT - Queue.TextBytes;
            if ( !Full ) Ok = MdoQueueInsert(&Queue, Id, Text,
                Attachments, AttachmentCount, First, Priority) &&
                MdoQueueWrite(Path, &Queue);
        }
    }
    xrtMutexUnlock(g_MdoQueueLock);
    if ( AttachmentLocked ) MdoApiAttachmentUnlock();
    if ( Add ) MdoApiJsonBodyUnit(&Body);
    if ( !Ok ) { MdoQueueRelease(&Queue); return MdoApiReplyError(Context,
        503u, "queue_unavailable", "The queue could not be read or saved", NULL); }
    if ( Duplicate || Full ) {
        MdoQueueRelease(&Queue);
        return MdoApiReplyError(Context, Duplicate ? 409u : 422u,
            Duplicate ? "queue_id_conflict" : "queue_full",
            Duplicate ? "The queue item ID already has different text" :
                "The queue has reached its item or byte limit", NULL);
    }
    return MdoQueueReply(Context, Add && Index == SIZE_MAX ? 201u : 200u,
        &Queue);
}

bool MdoApiQueueItemRoute(MdoApiContext* Context)
{
    char Path[MDO_SESSION_PATH_CAPACITY];
    char ProjectId[MDO_PROJECT_ID_CAPACITY];
    char SessionId[MDO_SESSION_ID_CAPACITY];
    char Id[MDO_QUEUE_ID_SIZE + 1u];
    MdoSessionStatus SessionStatus;
    MdoQueue Queue;
    MdoApiJsonBody Body;
    MdoApiBodyStatus BodyStatus;
    xstrview State;
    bool Sending = false;
    bool Change = Context->Request->head->MethodCode == XHTTP_METHOD_PUT;
    bool Ok;
    bool Conflict = false;
    size_t Index;

    if ( !MdoQueuePath(Context, Path, &SessionStatus,
            ProjectId, SessionId) ||
         Context->ParamCount != 3u ||
         !MdoQueueId(Context->Params[2], Id) )
        return MdoApiReplyError(Context, 404u, "queue_item_not_found",
            "The queue item does not exist", NULL);
    if ( Change ) {
        if ( SessionStatus != MDO_SESSION_ACTIVE )
            return MdoApiReplyError(Context, 409u, "session_state_conflict",
                "The session must be active to update its queue", NULL);
        BodyStatus = MdoApiJsonBodyRead(Context, &Body);
        if ( BodyStatus != MDO_API_BODY_OK )
            return MdoApiReplyBodyError(Context, BodyStatus);
        Ok = xrtValueType(Body.Value) == XVALUE_OBJECT &&
            xrtValueCount(Body.Value) == 1u &&
            MdoQueueString(Body.Value, "state", &State) &&
            MdoQueueState(State, &Sending);
        MdoApiJsonBodyUnit(&Body);
        if ( !Ok ) return MdoApiReplyError(Context, 422u,
            "queue_state_invalid", "Expected pending or sending state", NULL);
    } else {
        const xhttp1head* Head = Context->Request->head;
        if ( ((Head->Flags & (uint32)XHTTP1_CONTENT_LENGTH) != 0u &&
              Head->ContentLength != 0u) ||
             (Head->Flags & (uint32)XHTTP1_TRANSFER_ENCODING) != 0u )
            return MdoApiReplyError(Context, 400u, "body_not_allowed",
                "This operation does not accept a request body", NULL);
    }
    xrtMutexLock(g_MdoQueueLock);
    Ok = MdoQueueRead(Path, &Queue);
    if ( Ok ) {
        Index = MdoQueueFind(&Queue, Id);
        if ( Change ) {
            Conflict = Index == SIZE_MAX ||
                Queue.Items[Index].Sending == Sending;
            if ( !Conflict ) {
                Queue.Items[Index].Sending = Sending;
                Ok = MdoQueueWrite(Path, &Queue);
            }
        } else if ( Index != SIZE_MAX ) {
            Queue.TextBytes -= Queue.Items[Index].TextSize;
            xrtFree(Queue.Items[Index].Text);
            if ( Index + 1u < Queue.Count )
                memmove(&Queue.Items[Index], &Queue.Items[Index + 1u],
                    (Queue.Count - Index - 1u) * sizeof(Queue.Items[0]));
            Queue.Count--;
            memset(&Queue.Items[Queue.Count], 0, sizeof(Queue.Items[0]));
            Ok = MdoQueueWrite(Path, &Queue);
        }
    }
    xrtMutexUnlock(g_MdoQueueLock);
    if ( !Ok ) { MdoQueueRelease(&Queue); return MdoApiReplyError(Context,
        503u, "queue_unavailable", "The queue could not be read or saved", NULL); }
    if ( Conflict ) { MdoQueueRelease(&Queue); return MdoApiReplyError(Context,
        409u, "queue_state_conflict", "The queue item changed state", NULL); }
    return MdoQueueReply(Context, 200u, &Queue);
}
