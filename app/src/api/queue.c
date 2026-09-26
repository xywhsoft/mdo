#include <stdio.h>
#include <string.h>

#include "internal.h"
#include "../../include/mdo/home.h"
#include "../../include/mdo/runs.h"
#include "../../include/mdo/sessions.h"

#define MDO_QUEUE_MAX_ITEMS 20u
#define MDO_QUEUE_MAX_TEXT (64u * 1024u - 1u)
#define MDO_QUEUE_MAX_TOTAL_TEXT (192u * 1024u)
#define MDO_QUEUE_FILE_MAX (256u * 1024u)
#define MDO_QUEUE_ID_SIZE 32u
#define MDO_QUEUE_RECEIPT_FILE_MAX 512u

typedef enum MdoQueueState {
    MDO_QUEUE_STAGED,
    MDO_QUEUE_PENDING,
    MDO_QUEUE_SENDING
} MdoQueueState;

typedef struct MdoQueueItem {
    char Id[MDO_QUEUE_ID_SIZE + 1u];
    char* Text;
    size_t TextSize;
    char Attachments[4][33];
    size_t AttachmentCount;
    MdoQueueState State;
    bool Priority;
    bool StartClaimed;
    char RunId[MDO_RUN_ID_CAPACITY];
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

static bool MdoQueueRunId(xstrview View, char Output[MDO_RUN_ID_CAPACITY])
{
    return View.Size > 4u && View.Size < MDO_RUN_ID_CAPACITY &&
        memcmp(View.Data, "run-", 4u) == 0 &&
        MdoQueueCaptureId(View, Output, MDO_RUN_ID_CAPACITY);
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

static bool MdoQueueReceiptPath(char Path[MDO_SESSION_PATH_CAPACITY],
    const char* ProjectId, const char* SessionId, const char* Id)
{
    int Written = snprintf(Path, MDO_SESSION_PATH_CAPACITY,
        "sessions/%s/%s/queue-receipts/%s.json", ProjectId, SessionId, Id);
    return Written > 0 && (size_t)Written < MDO_SESSION_PATH_CAPACITY;
}

/* A receipt survives queue removal. A malformed receipt fails closed: the
 * same queue ID must never become available merely because storage is bad. */
static bool MdoQueueReceiptRead(const char* ProjectId,
    const char* SessionId, const char* Id, bool* Exists,
    char RunId[MDO_RUN_ID_CAPACITY])
{
    char Path[MDO_SESSION_PATH_CAPACITY];
    xfileinfo Info;
    xfile File = NULL;
    char Bytes[MDO_QUEUE_RECEIPT_FILE_MAX + 1u];
    xjsonreadconfig Config;
    xvalue* Root = NULL;
    const xvalue* Version;
    uint64 Schema = 0u;
    int64 Signed;
    xstrview StoredId, StoredRun, StoredState;
    bool Ok = false;
    *Exists = false;
    memset(RunId, 0, MDO_RUN_ID_CAPACITY);
    if ( !MdoQueueReceiptPath(Path, ProjectId, SessionId, Id) ||
         !MdoHomeExternalStat(Path, Exists, &Info) ) return false;
    if ( !*Exists ) return true;
    if ( Info.Type != XFILE_TYPE_FILE ||
         (Info.Available & XFILE_INFO_SIZE) == 0u ||
         Info.Size == 0u || Info.Size > MDO_QUEUE_RECEIPT_FILE_MAX )
        return false;
    File = MdoHomeOpenRead(Path);
    if ( File == NULL || !xrtReadFull(File, Bytes, (size_t)Info.Size,
            NULL) ) goto done;
    Bytes[Info.Size] = '\0';
    xrtJsonReadConfigInit(&Config);
    Config.MaxInputBytes = MDO_QUEUE_RECEIPT_FILE_MAX;
    Config.MaxDepth = 2u;
    Config.MaxValues = 8u;
    Root = xrtJsonRead(xrtStrViewN(Bytes, (size_t)Info.Size), &Config);
    Version = xrtValueObjectGet(Root, XRT_STR_LITERAL("schema_version"));
    if ( xrtValueType(Version) == XVALUE_UINT ) {
        if ( !xrtValueGetUInt(Version, &Schema) ) goto done;
    } else if ( xrtValueType(Version) == XVALUE_INT ) {
        if ( !xrtValueGetInt(Version, &Signed) || Signed < 0 ) goto done;
        Schema = (uint64)Signed;
    } else goto done;
    if ( xrtValueType(Root) != XVALUE_OBJECT ||
         xrtValueCount(Root) != 3u || (Schema != 1u && Schema != 2u) ||
         !MdoQueueString(Root, "id", &StoredId) ||
         StoredId.Size != MDO_QUEUE_ID_SIZE ||
         memcmp(StoredId.Data, Id, MDO_QUEUE_ID_SIZE) != 0 ) goto done;
    if ( Schema == 1u ) {
        if ( !MdoQueueString(Root, "run_id", &StoredRun) ||
             !MdoQueueRunId(StoredRun, RunId) ) goto done;
    } else {
        if ( !MdoQueueString(Root, "state", &StoredState) ||
             StoredState.Size != 8u ||
             memcmp(StoredState.Data, "starting", 8u) != 0 ) goto done;
    }
    Ok = true;
done:
    xrtValueRelease(Root);
    if ( File != NULL && !xrtClose(File) ) Ok = false;
    if ( !Ok ) RunId[0] = '\0';
    return Ok;
}

static bool MdoQueueReceiptWrite(const char* ProjectId,
    const char* SessionId, const char* Id, const char* RunId)
{
    char Path[MDO_SESSION_PATH_CAPACITY];
    char Bytes[MDO_QUEUE_RECEIPT_FILE_MAX];
    char ExistingRun[MDO_RUN_ID_CAPACITY];
    bool Exists;
    int Written;
    if ( !MdoQueueReceiptPath(Path, ProjectId, SessionId, Id) ||
         !MdoQueueReceiptRead(ProjectId, SessionId, Id, &Exists,
            ExistingRun) ) return false;
    if ( Exists && ExistingRun[0] != '\0' )
        return strcmp(ExistingRun, RunId) == 0;
    Written = snprintf(Bytes, sizeof(Bytes),
        "{\"schema_version\":1,\"id\":\"%s\",\"run_id\":\"%s\"}",
        Id, RunId);
    return Written > 0 && (size_t)Written < sizeof(Bytes) &&
        MdoHomeAtomicWrite(Path, Bytes, (size_t)Written, false);
}

/* Persist admission before starting the runtime. An interrupted start keeps
 * this marker, so another page cannot silently replay the same submission. */
static bool MdoQueueReceiptClaim(const char* ProjectId,
    const char* SessionId, const char* Id)
{
    char Path[MDO_SESSION_PATH_CAPACITY];
    char Bytes[MDO_QUEUE_RECEIPT_FILE_MAX];
    int Written;
    if ( !MdoQueueReceiptPath(Path, ProjectId, SessionId, Id) )
        return false;
    Written = snprintf(Bytes, sizeof(Bytes),
        "{\"schema_version\":2,\"id\":\"%s\",\"state\":\"starting\"}", Id);
    return Written > 0 && (size_t)Written < sizeof(Bytes) &&
        MdoHomeAtomicWrite(Path, Bytes, (size_t)Written, false);
}

static bool MdoQueueParseState(xstrview State, bool AllowStaged,
    MdoQueueState* Result)
{
    if ( AllowStaged && State.Size == 6u &&
         memcmp(State.Data, "staged", 6u) == 0 ) {
        *Result = MDO_QUEUE_STAGED;
        return true;
    }
    if ( State.Size == 7u && memcmp(State.Data, "pending", 7u) == 0 ) {
        *Result = MDO_QUEUE_PENDING;
        return true;
    }
    if ( State.Size == 7u && memcmp(State.Data, "sending", 7u) == 0 ) {
        *Result = MDO_QUEUE_SENDING;
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
    size_t AttachmentCount, bool First, bool Priority,
    MdoQueueState State)
{
    MdoQueueItem* Item;
    char* Copy;
    size_t Position = First && Queue->Count != 0u &&
        Queue->Items[0].State == MDO_QUEUE_SENDING ? 1u :
        (First ? 0u : Queue->Count);
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
    Item->State = State;
    Queue->TextBytes += Text.Size;
    Queue->Count++;
    return true;
}

static bool MdoQueueRead(const char* Path, const char* ProjectId,
    const char* SessionId, MdoQueue* Queue)
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
        if ( Schema < 1u || Schema > 5u ) goto done;
    }
    for ( i = 0u; i < xrtValueCount(Items); ++i ) {
        const xvalue* Entry = xrtValueArrayGet(Items, i);
        xstrview Id;
        xstrview Text;
        xstrview StateView;
        xstrview RunIdView = { 0 };
        char RunId[MDO_RUN_ID_CAPACITY] = { 0 };
        char ReceiptRunId[MDO_RUN_ID_CAPACITY];
        bool ReceiptExists = false;
        const xvalue* RunIdValue = xrtValueObjectGet(Entry,
            XRT_STR_LITERAL("run_id"));
        char IdText[MDO_QUEUE_ID_SIZE + 1u];
        char Attachments[4][33] = {{ 0 }};
        size_t AttachmentCount = 0u;
        MdoQueueState State;
        bool Priority = false;
        if ( xrtValueType(Entry) != XVALUE_OBJECT ||
             xrtValueCount(Entry) != (Schema == 1u ? 3u :
                (Schema == 2u ? 4u : 5u +
                    (Schema == 5u && RunIdValue != NULL ? 1u : 0u))) ||
             !MdoQueueString(Entry, "id", &Id) ||
             !MdoQueueString(Entry, "text", &Text) ||
             !MdoQueueString(Entry, "state", &StateView) ||
             !MdoQueueId(Id, IdText) ||
             (Schema >= 2u &&
              !MdoAttachmentIdsRead(xrtValueObjectGet(Entry,
                XRT_STR_LITERAL("attachments")), Attachments,
                &AttachmentCount)) ||
             (Schema >= 3u &&
              !MdoQueueBool(Entry, "priority", &Priority)) ||
             !MdoQueueText(Text, AttachmentCount != 0u) ||
             !MdoQueueParseState(StateView, Schema >= 4u, &State) ||
             (RunIdValue != NULL &&
              (Schema != 5u || State != MDO_QUEUE_SENDING ||
               xrtValueType(RunIdValue) != XVALUE_STRING ||
               !xrtValueGetString(RunIdValue, &RunIdView) ||
               !MdoQueueRunId(RunIdView, RunId))) ||
             MdoQueueFind(Queue, IdText) != SIZE_MAX ||
             !MdoQueueInsert(Queue, IdText, Text, Attachments,
                AttachmentCount, false, Priority, State) ) goto done;
        if ( !MdoQueueReceiptRead(ProjectId, SessionId, IdText,
                &ReceiptExists, ReceiptRunId) ) goto done;
        if ( ReceiptExists && State != MDO_QUEUE_SENDING ) goto done;
        if ( RunIdValue != NULL && ReceiptExists &&
             strcmp(RunId, ReceiptRunId) != 0 ) goto done;
        if ( RunIdValue == NULL && ReceiptRunId[0] != '\0' )
            memcpy(RunId, ReceiptRunId, sizeof(RunId));
        Queue->Items[Queue->Count - 1u].StartClaimed =
            ReceiptExists && ReceiptRunId[0] == '\0';
        if ( RunIdValue != NULL || ReceiptRunId[0] != '\0' )
            memcpy(Queue->Items[Queue->Count - 1u].RunId, RunId,
                sizeof(RunId));
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
    Ok = MdoQueueRead(Path, ProjectId, SessionId, &Queue);
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

static xvalue* MdoQueueValue(const MdoQueue* Queue, bool IncludeClaim)
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
                Source->State == MDO_QUEUE_STAGED ? "staged" :
                (Source->State == MDO_QUEUE_SENDING ? "sending" :
                "pending")) &&
            MdoAttachmentIdsWriteValue(Item, Source->Attachments,
                Source->AttachmentCount) &&
            MdoApiValueSetBool(Item, "priority", Source->Priority) &&
            (!IncludeClaim || !Source->StartClaimed ||
             MdoApiValueSetBool(Item, "start_claimed", true)) &&
            (Source->RunId[0] == '\0' ||
             MdoApiValueSetString(Item, "run_id", Source->RunId)) &&
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
    xvalue* Data = MdoQueueValue(Queue, false);
    char* Json;
    size_t Size = 0u;
    bool Ok;
    if ( Data == NULL || !MdoApiValueSetUInt(Data, "schema_version", 5u) ) {
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
    xvalue* Data = MdoQueueValue(Queue, true);
    MdoQueueRelease(Queue);
    if ( Data == NULL ) return MdoApiReplyError(Context, 503u,
        "queue_unavailable", "The queue response could not be created", NULL);
    return MdoApiReplySuccessTake(Context, Status, Data, NULL);
}

static bool MdoQueueRunPath(char Path[MDO_SESSION_PATH_CAPACITY],
    const char* ProjectId, const char* SessionId)
{
    int Written;
    if ( ProjectId == NULL || SessionId == NULL ) return false;
    Written = snprintf(Path, MDO_SESSION_PATH_CAPACITY,
        "sessions/%s/%s/queue.json", ProjectId, SessionId);
    return Written > 0 && (size_t)Written < MDO_SESSION_PATH_CAPACITY;
}

static bool MdoQueueRunMatches(const MdoQueueItem* Item, xstrview Prompt,
    const char Attachments[4][33], size_t AttachmentCount)
{
    return Item->State == MDO_QUEUE_SENDING &&
        Item->TextSize == Prompt.Size &&
        (Prompt.Size == 0u ||
         memcmp(Item->Text, Prompt.Data, Prompt.Size) == 0) &&
        Item->AttachmentCount == AttachmentCount &&
        memcmp(Item->Attachments, Attachments,
            sizeof(Item->Attachments)) == 0;
}

MdoApiQueueRunStatus MdoApiQueueRunPrepare(const char* ProjectId,
    const char* SessionId, const char* Id, xstrview Prompt,
    const char Attachments[4][33], size_t AttachmentCount)
{
    char Path[MDO_SESSION_PATH_CAPACITY];
    MdoQueue Queue;
    MdoApiQueueRunStatus Result = MDO_API_QUEUE_RUN_UNAVAILABLE;
    size_t Index;
    if ( !MdoQueueRunPath(Path, ProjectId, SessionId) || Id == NULL ||
         Attachments == NULL ) return Result;
    xrtMutexLock(g_MdoQueueLock);
    if ( MdoQueueRead(Path, ProjectId, SessionId, &Queue) ) {
        Index = MdoQueueFind(&Queue, Id);
        Result = MDO_API_QUEUE_RUN_CONFLICT;
        if ( Index != SIZE_MAX ) {
            const MdoQueueItem* Item = &Queue.Items[Index];
            if ( MdoQueueRunMatches(Item, Prompt, Attachments,
                    AttachmentCount) ) {
                bool Exists;
                char RunId[MDO_RUN_ID_CAPACITY];
                if ( !MdoQueueReceiptRead(ProjectId, SessionId, Id,
                        &Exists, RunId) )
                    Result = MDO_API_QUEUE_RUN_UNAVAILABLE;
                else Result = Item->RunId[0] != '\0' ||
                    (Exists && RunId[0] != '\0') ?
                    MDO_API_QUEUE_RUN_ACCEPTED :
                    (Exists ? MDO_API_QUEUE_RUN_STARTING :
                    MDO_API_QUEUE_RUN_READY);
            }
        } else {
            bool Exists;
            char RunId[MDO_RUN_ID_CAPACITY];
            if ( MdoQueueReceiptRead(ProjectId, SessionId, Id,
                    &Exists, RunId) ) {
                if ( Exists ) Result = RunId[0] != '\0' ?
                    MDO_API_QUEUE_RUN_ACCEPTED : MDO_API_QUEUE_RUN_STARTING;
            } else Result = MDO_API_QUEUE_RUN_UNAVAILABLE;
        }
        MdoQueueRelease(&Queue);
    }
    xrtMutexUnlock(g_MdoQueueLock);
    return Result;
}

MdoApiQueueRunStatus MdoApiQueueRunClaim(const char* ProjectId,
    const char* SessionId, const char* Id, xstrview Prompt,
    const char Attachments[4][33], size_t AttachmentCount)
{
    char Path[MDO_SESSION_PATH_CAPACITY];
    MdoQueue Queue;
    MdoApiQueueRunStatus Result = MDO_API_QUEUE_RUN_UNAVAILABLE;
    size_t Index;
    bool Exists;
    char RunId[MDO_RUN_ID_CAPACITY];
    if ( !MdoQueueRunPath(Path, ProjectId, SessionId) || Id == NULL ||
         Attachments == NULL ) return Result;
    xrtMutexLock(g_MdoQueueLock);
    if ( MdoQueueRead(Path, ProjectId, SessionId, &Queue) ) {
        Index = MdoQueueFind(&Queue, Id);
        Result = MDO_API_QUEUE_RUN_CONFLICT;
        if ( Index != SIZE_MAX &&
             MdoQueueRunMatches(&Queue.Items[Index], Prompt, Attachments,
                AttachmentCount) ) {
            if ( MdoQueueReceiptRead(ProjectId, SessionId, Id,
                    &Exists, RunId) ) {
                Result = Queue.Items[Index].RunId[0] != '\0' ||
                    (Exists && RunId[0] != '\0') ?
                    MDO_API_QUEUE_RUN_ACCEPTED :
                    (Exists ? MDO_API_QUEUE_RUN_STARTING :
                    (MdoQueueReceiptClaim(ProjectId, SessionId, Id) ?
                        MDO_API_QUEUE_RUN_READY :
                        MDO_API_QUEUE_RUN_UNAVAILABLE));
            } else Result = MDO_API_QUEUE_RUN_UNAVAILABLE;
        }
        MdoQueueRelease(&Queue);
    }
    xrtMutexUnlock(g_MdoQueueLock);
    return Result;
}

bool MdoApiQueueRunReleaseClaim(const char* ProjectId,
    const char* SessionId, const char* Id)
{
    char Path[MDO_SESSION_PATH_CAPACITY];
    char RunId[MDO_RUN_ID_CAPACITY];
    bool Exists;
    bool Ok = false;
    if ( Id == NULL || !MdoQueueReceiptPath(Path, ProjectId,
            SessionId, Id) ) return false;
    xrtMutexLock(g_MdoQueueLock);
    if ( MdoQueueReceiptRead(ProjectId, SessionId, Id,
            &Exists, RunId) && Exists && RunId[0] == '\0' )
        Ok = MdoHomeRemove(Path, false);
    xrtMutexUnlock(g_MdoQueueLock);
    return Ok;
}

bool MdoApiQueueRunBind(const char* ProjectId, const char* SessionId,
    const char* Id, xstrview Prompt, const char Attachments[4][33],
    size_t AttachmentCount, const char* RunId)
{
    char Path[MDO_SESSION_PATH_CAPACITY];
    MdoQueue Queue;
    size_t Index;
    bool Ok = false;
    char ValidRunId[MDO_RUN_ID_CAPACITY];
    if ( !MdoQueueRunPath(Path, ProjectId, SessionId) || Id == NULL ||
         Attachments == NULL || RunId == NULL ||
         !MdoQueueRunId(xrtStrView(RunId), ValidRunId) ) return false;
    xrtMutexLock(g_MdoQueueLock);
    if ( MdoQueueRead(Path, ProjectId, SessionId, &Queue) ) {
        Index = MdoQueueFind(&Queue, Id);
        if ( Index != SIZE_MAX &&
             MdoQueueRunMatches(&Queue.Items[Index], Prompt, Attachments,
                AttachmentCount) &&
             (Queue.Items[Index].RunId[0] == '\0' ||
              strcmp(Queue.Items[Index].RunId, RunId) == 0) &&
             MdoQueueReceiptWrite(ProjectId, SessionId, Id, RunId) ) {
            memcpy(Queue.Items[Index].RunId, RunId, strlen(RunId) + 1u);
            Ok = MdoQueueWrite(Path, &Queue);
        } else if ( Index == SIZE_MAX ) {
            /* A second page may remove the item while its run starts. The
             * durable claim still belongs to this start and must acquire
             * the resulting run ID. */
            bool Exists;
            char ReceiptRunId[MDO_RUN_ID_CAPACITY];
            Ok = MdoQueueReceiptRead(ProjectId, SessionId, Id,
                &Exists, ReceiptRunId) && Exists &&
                MdoQueueReceiptWrite(ProjectId, SessionId, Id, RunId);
        }
        MdoQueueRelease(&Queue);
    }
    xrtMutexUnlock(g_MdoQueueLock);
    return Ok;
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
    bool Stage = false;
    bool Add = Context->Request->head->MethodCode == XHTTP_METHOD_POST;
    bool Ok;
    bool AttachmentLocked = false;
    bool Duplicate = false;
    bool Full = false;
    bool Consumed = false;
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
            const xvalue* StageValue = xrtValueObjectGet(Body.Value,
                XRT_STR_LITERAL("stage"));
            Ok = xrtValueType(Body.Value) == XVALUE_OBJECT &&
                xrtValueCount(Body.Value) == (References == NULL ? 3u : 4u) +
                    (PriorityValue == NULL ? 0u : 1u) +
                    (StageValue == NULL ? 0u : 1u) &&
                MdoQueueString(Body.Value, "id", &IdView) &&
                MdoQueueString(Body.Value, "text", &Text) &&
                MdoQueueBool(Body.Value, "first", &First) &&
                (PriorityValue == NULL ||
                 MdoQueueBool(Body.Value, "priority", &Priority)) &&
                (StageValue == NULL ||
                 MdoQueueBool(Body.Value, "stage", &Stage)) &&
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
    Ok = MdoQueueRead(Path, ProjectId, SessionId, &Queue);
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
            char RunId[MDO_RUN_ID_CAPACITY];
            Ok = MdoQueueReceiptRead(ProjectId, SessionId, Id,
                &Consumed, RunId);
            if ( Ok && !Consumed ) {
                Full = Queue.Count >= MDO_QUEUE_MAX_ITEMS ||
                    Text.Size > MDO_QUEUE_MAX_TOTAL_TEXT - Queue.TextBytes;
                if ( !Full ) Ok = MdoQueueInsert(&Queue, Id, Text,
                    Attachments, AttachmentCount, First, Priority,
                    Stage ? MDO_QUEUE_STAGED : MDO_QUEUE_PENDING) &&
                    MdoQueueWrite(Path, &Queue);
            }
        }
    }
    xrtMutexUnlock(g_MdoQueueLock);
    if ( AttachmentLocked ) MdoApiAttachmentUnlock();
    if ( Add ) MdoApiJsonBodyUnit(&Body);
    if ( !Ok ) { MdoQueueRelease(&Queue); return MdoApiReplyError(Context,
        503u, "queue_unavailable", "The queue could not be read or saved", NULL); }
    if ( Duplicate || Full || Consumed ) {
        MdoQueueRelease(&Queue);
        return MdoApiReplyError(Context, Full ? 422u : 409u,
            Consumed ? "queue_item_consumed" :
                (Duplicate ? "queue_id_conflict" : "queue_full"),
            Consumed ? "This queue item already started a run" :
                (Duplicate ? "The queue item ID already has different text" :
                    "The queue has reached its item or byte limit"), NULL);
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
    xstrview StateView;
    MdoQueueState NextState = MDO_QUEUE_PENDING;
    bool Change = Context->Request->head->MethodCode == XHTTP_METHOD_PUT;
    bool Read = Context->Request->head->MethodCode == XHTTP_METHOD_GET ||
        Context->Request->head->MethodCode == XHTTP_METHOD_HEAD;
    bool Ok;
    bool Conflict = false;
    size_t Index;

    if ( !MdoQueuePath(Context, Path, &SessionStatus,
            ProjectId, SessionId) ||
         Context->ParamCount != 3u ||
         !MdoQueueId(Context->Params[2], Id) )
        return MdoApiReplyError(Context, 404u, "queue_item_not_found",
            "The queue item does not exist", NULL);
    if ( Read ) {
        bool Exists;
        char RunId[MDO_RUN_ID_CAPACITY];
        xvalue* Data;
        xrtMutexLock(g_MdoQueueLock);
        Ok = MdoQueueReceiptRead(ProjectId, SessionId, Id,
            &Exists, RunId);
        xrtMutexUnlock(g_MdoQueueLock);
        if ( !Ok ) return MdoApiReplyError(Context, 503u,
            "queue_unavailable", "The queue receipt could not be read", NULL);
        if ( !Exists ) return MdoApiReplyError(Context, 404u,
            "queue_receipt_not_found", "The queue receipt does not exist",
            NULL);
        Data = xrtValueObject();
        if ( Data == NULL ||
             !MdoApiValueSetString(Data, "id", Id) ||
             !MdoApiValueSetString(Data, "state",
                RunId[0] != '\0' ? "accepted" : "starting") ||
             (RunId[0] != '\0' &&
              !MdoApiValueSetString(Data, "run_id", RunId)) ) {
            xrtValueRelease(Data);
            return MdoApiReplyError(Context, 503u,
                "queue_unavailable", "The queue receipt could not be read",
                NULL);
        }
        return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
    }
    if ( Change ) {
        if ( SessionStatus != MDO_SESSION_ACTIVE )
            return MdoApiReplyError(Context, 409u, "session_state_conflict",
                "The session must be active to update its queue", NULL);
        BodyStatus = MdoApiJsonBodyRead(Context, &Body);
        if ( BodyStatus != MDO_API_BODY_OK )
            return MdoApiReplyBodyError(Context, BodyStatus);
        Ok = xrtValueType(Body.Value) == XVALUE_OBJECT &&
            xrtValueCount(Body.Value) == 1u &&
            MdoQueueString(Body.Value, "state", &StateView) &&
            MdoQueueParseState(StateView, false, &NextState);
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
    Ok = MdoQueueRead(Path, ProjectId, SessionId, &Queue);
    if ( Ok ) {
        Index = MdoQueueFind(&Queue, Id);
        if ( Change ) {
            Conflict = Index == SIZE_MAX ||
                !((Queue.Items[Index].State == MDO_QUEUE_STAGED &&
                   NextState == MDO_QUEUE_PENDING) ||
                  (Queue.Items[Index].State == MDO_QUEUE_PENDING &&
                   NextState == MDO_QUEUE_SENDING) ||
                  (Queue.Items[Index].State == MDO_QUEUE_SENDING &&
                   Queue.Items[Index].RunId[0] == '\0' &&
                   NextState == MDO_QUEUE_PENDING));
            if ( !Conflict && Queue.Items[Index].State == MDO_QUEUE_SENDING ) {
                bool ReceiptExists;
                char ReceiptRunId[MDO_RUN_ID_CAPACITY];
                Ok = MdoQueueReceiptRead(ProjectId, SessionId, Id,
                    &ReceiptExists, ReceiptRunId);
                Conflict = Ok && ReceiptExists;
            }
            if ( !Conflict ) {
                Queue.Items[Index].State = NextState;
                Ok = MdoQueueWrite(Path, &Queue);
            }
        } else if ( Index != SIZE_MAX ) {
            if ( Queue.Items[Index].RunId[0] != '\0' )
                Ok = MdoQueueReceiptWrite(ProjectId, SessionId, Id,
                    Queue.Items[Index].RunId);
            if ( Ok ) {
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
    }
    xrtMutexUnlock(g_MdoQueueLock);
    if ( !Ok ) { MdoQueueRelease(&Queue); return MdoApiReplyError(Context,
        503u, "queue_unavailable", "The queue could not be read or saved", NULL); }
    if ( Conflict ) { MdoQueueRelease(&Queue); return MdoApiReplyError(Context,
        409u, "queue_state_conflict", "The queue item changed state", NULL); }
    return MdoQueueReply(Context, 200u, &Queue);
}
