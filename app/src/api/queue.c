#include <stdio.h>
#include <string.h>

#include "internal.h"
#include "profile.h"
#include "../sessions/sidecars/queue.h"
#include "../../include/mdo/home.h"
#include "../../include/mdo/runs.h"
#include "../../include/mdo/sessions.h"
#include "../../include/mdo/project_lifecycle.h"

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

bool MdoApiQueueCaptureTryLock(void)
{
    return g_MdoQueueLock != NULL && xrtMutexTryLock(g_MdoQueueLock);
}

void MdoApiQueueCaptureUnlock(void)
{
    xrtMutexUnlock(g_MdoQueueLock);
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
static bool MdoQueueReceiptReadEx(const char* ProjectId,
    const char* SessionId, const char* Id, bool* Exists,
    char RunId[MDO_RUN_ID_CAPACITY],
    char PreparedRunId[MDO_RUN_ID_CAPACITY], uint64* AgentRunId)
{
    char Path[MDO_SESSION_PATH_CAPACITY];
    xfileinfo Info;
    xfile File = NULL;
    char Bytes[MDO_QUEUE_RECEIPT_FILE_MAX + 1u];
    MdoQueueReceipt Receipt;
    bool Ok = false;
    *Exists = false; RunId[0] = '\0';
    if ( PreparedRunId != NULL ) PreparedRunId[0] = '\0';
    if ( AgentRunId != NULL ) *AgentRunId = 0u;
    if ( !MdoQueueReceiptPath(Path, ProjectId, SessionId, Id) ||
         !MdoHomeExternalStat(Path, Exists, &Info) ) return false;
    if ( !*Exists ) return true;
    if ( Info.Type != XFILE_TYPE_FILE || (Info.Available & XFILE_INFO_SIZE) == 0u ||
         Info.Size == 0u || Info.Size > MDO_QUEUE_RECEIPT_FILE_MAX ) return false;
    File = MdoHomeOpenRead(Path);
    if ( File != NULL && xrtReadFull(File, Bytes, (size_t)Info.Size, NULL) )
        Ok = MdoQueueReceiptParse(xrtStrViewN(Bytes, (size_t)Info.Size), Id, &Receipt);
    if ( File != NULL && !xrtClose(File) ) Ok = false;
    if ( !Ok ) return false;
    if ( Receipt.Schema == 1u ) memcpy(RunId, Receipt.RunId, MDO_RUN_ID_CAPACITY);
    if ( Receipt.Schema == 3u ) {
        if ( PreparedRunId != NULL ) memcpy(PreparedRunId, Receipt.RunId, MDO_RUN_ID_CAPACITY);
        if ( AgentRunId != NULL ) *AgentRunId = Receipt.AgentRunId;
    }
    return true;
}

static bool MdoQueueReceiptRead(const char* ProjectId,
    const char* SessionId, const char* Id, bool* Exists,
    char RunId[MDO_RUN_ID_CAPACITY])
{
    return MdoQueueReceiptReadEx(ProjectId, SessionId, Id, Exists,
        RunId, NULL, NULL);
}

static bool MdoQueueReceiptWrite(const char* ProjectId,
    const char* SessionId, const char* Id, const char* RunId)
{
    char Path[MDO_SESSION_PATH_CAPACITY];
    char Bytes[MDO_QUEUE_RECEIPT_FILE_MAX];
    char ExistingRun[MDO_RUN_ID_CAPACITY];
    char PreparedRun[MDO_RUN_ID_CAPACITY];
    bool Exists;
    int Written;
    if ( !MdoQueueReceiptPath(Path, ProjectId, SessionId, Id) ||
         !MdoQueueReceiptReadEx(ProjectId, SessionId, Id, &Exists,
            ExistingRun, PreparedRun, NULL) ) return false;
    if ( Exists && ExistingRun[0] != '\0' )
        return strcmp(ExistingRun, RunId) == 0;
    if ( Exists && PreparedRun[0] != '\0' &&
         strcmp(PreparedRun, RunId) != 0 ) return false;
    Written = snprintf(Bytes, sizeof(Bytes),
        "{\"schema_version\":1,\"id\":\"%s\",\"run_id\":\"%s\"}",
        Id, RunId);
    return Written > 0 && (size_t)Written < sizeof(Bytes) &&
        MdoHomeAtomicWrite(Path, Bytes, (size_t)Written, false);
}

/* Only a matching, durable main-Agent start event can promote a prepared
 * receipt. Missing or trimmed evidence remains an explicit review state. */
static bool MdoQueueReceiptResolve(const char* ProjectId,
    const char* SessionId, const char* Id, bool* Exists,
    char RunId[MDO_RUN_ID_CAPACITY])
{
    char PreparedRunId[MDO_RUN_ID_CAPACITY];
    uint64 AgentRunId;
    bool Seen = false;
    if ( !MdoQueueReceiptReadEx(ProjectId, SessionId, Id, Exists,
            RunId, PreparedRunId, &AgentRunId) ) return false;
    if ( !*Exists || PreparedRunId[0] == '\0' ) return true;
    if ( !MdoSessionEventQueueStartSeen(ProjectId, SessionId, Id,
            AgentRunId, &Seen) ) return false;
    if ( !Seen ) return true;
    if ( !MdoQueueReceiptWrite(ProjectId, SessionId, Id,
            PreparedRunId) ) return false;
    memcpy(RunId, PreparedRunId, MDO_RUN_ID_CAPACITY);
    return true;
}

bool MdoApiQueueRunRecordPrepared(const char* ProjectId,
    const char* SessionId, const char* Id, const char* RunId,
    uint64 AgentRunId)
{
    char Path[MDO_SESSION_PATH_CAPACITY];
    char Bytes[MDO_QUEUE_RECEIPT_FILE_MAX];
    char ExistingRun[MDO_RUN_ID_CAPACITY];
    char PreparedRun[MDO_RUN_ID_CAPACITY];
    char CheckedRun[MDO_RUN_ID_CAPACITY];
    uint64 ExistingAgent = 0u;
    bool Exists;
    bool Ok = false;
    int Written;
    if ( ProjectId == NULL || SessionId == NULL || Id == NULL ||
         RunId == NULL || AgentRunId == 0u ||
         !MdoQueueRunId(xrtStrView(RunId), CheckedRun) ||
         !MdoQueueReceiptPath(Path, ProjectId, SessionId, Id) )
        return false;
    MdoProjectLease* Lease = MdoProjectLeaseAcquire(ProjectId,
        MDO_PROJECT_LEASE_SHARED, NULL);
    if ( Lease == NULL ) return false;
    xrtMutexLock(g_MdoQueueLock);
    if ( MdoQueueReceiptReadEx(ProjectId, SessionId, Id, &Exists,
            ExistingRun, PreparedRun, &ExistingAgent) && Exists &&
         ExistingRun[0] == '\0' ) {
        if ( PreparedRun[0] != '\0' )
            Ok = strcmp(PreparedRun, RunId) == 0 &&
                ExistingAgent == AgentRunId;
        else {
            Written = snprintf(Bytes, sizeof(Bytes),
                "{\"schema_version\":3,\"id\":\"%s\","
                "\"state\":\"starting\",\"run_id\":\"%s\","
                "\"agent_run_id\":%llu}", Id, RunId,
                (unsigned long long)AgentRunId);
            Ok = Written > 0 && (size_t)Written < sizeof(Bytes) &&
                MdoHomeAtomicWrite(Path, Bytes, (size_t)Written, false);
        }
    }
    xrtMutexUnlock(g_MdoQueueLock);
    MdoProjectLeaseRelease(Lease);
    return Ok;
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

static bool MdoQueueRead(const char* Path, const char* ProjectId,
    const char* SessionId, MdoQueue* Queue)
{
    bool Exists = false, Ok = false;
    xfileinfo Info;
    xfile File = NULL;
    char* Bytes = NULL;
    memset(Queue, 0, sizeof(*Queue));
    if ( !MdoHomeExternalStat(Path, &Exists, &Info) ) return false;
    if ( !Exists ) return true;
    if ( Info.Type != XFILE_TYPE_FILE || (Info.Available & XFILE_INFO_SIZE) == 0u ||
         Info.Size > MDO_QUEUE_FILE_MAX || Info.Size > SIZE_MAX - 1u ) return false;
    File = MdoHomeOpenRead(Path); Bytes = (char*)xrtMalloc((size_t)Info.Size + 1u);
    if ( File != NULL && Bytes != NULL && (Info.Size == 0u ||
         xrtReadFull(File, Bytes, (size_t)Info.Size, NULL)) ) {
        Bytes[Info.Size] = '\0';
        Ok = MdoQueueParse(xrtStrViewN(Bytes, (size_t)Info.Size), Queue);
        if ( Ok ) {
            size_t i;
            for ( i = 0u; i < Queue->Count; ++i ) {
                bool ReceiptExists;
                char ReceiptRunId[MDO_RUN_ID_CAPACITY];
                MdoQueueItem* Item = &Queue->Items[i];
                if ( !MdoQueueReceiptResolve(ProjectId, SessionId, Item->Id, &ReceiptExists, ReceiptRunId) ||
                     !MdoQueueReceiptApply(Item, ReceiptExists, ReceiptRunId) ) { Ok = false; break; }
            }
        }
    }
    xrtFree(Bytes);
    if ( File != NULL && !xrtClose(File) ) Ok = false;
    if ( !Ok ) { MdoQueueRelease(Queue); }
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
    MdoProjectLease* Lease = MdoProjectLeaseAcquire(ProjectId,
        MDO_PROJECT_LEASE_SHARED, NULL);
    if ( Lease == NULL ) return false;
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
    MdoProjectLeaseRelease(Lease);
    return Ok;
}

static xvalue* MdoQueueValue(const MdoQueue* Queue, bool IncludeClaim)
{
    xvalue* Root = xrtValueObject();
    xvalue* Items = xrtValueArray();
    xvalue* Discard = xrtValueArray();
    size_t i;
    bool Ok = Root != NULL && Items != NULL && Discard != NULL;
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
            MdoApiProfileSet(Item, "profile", &Source->Profile) &&
            (!IncludeClaim || !Source->StartClaimed ||
             MdoApiValueSetBool(Item, "start_claimed", true)) &&
            (Source->RunId[0] == '\0' ||
             MdoApiValueSetString(Item, "run_id", Source->RunId)) &&
            MdoApiValueAppendTake(Items, &Item);
        xrtValueRelease(Item);
    }
    if ( Ok ) Ok = MdoApiValueSetTake(Root, "items", &Items);
    for ( i = 0u; Ok && i < Queue->DiscardCount; ++i )
        Ok = MdoApiValueAppendString(Discard, Queue->DiscardImages[i]);
    if ( Ok ) Ok = MdoApiValueSetTake(Root, "discard_images", &Discard);
    xrtValueRelease(Items);
    xrtValueRelease(Discard);
    if ( !Ok ) { xrtValueRelease(Root); return NULL; }
    return Root;
}

static bool MdoQueueWrite(const char* Path, const MdoQueue* Queue)
{
    xvalue* Data = MdoQueueValue(Queue, false);
    char* Json;
    size_t Size = 0u;
    bool Ok;
    if ( Data == NULL || !MdoApiValueSetUInt(Data, "schema_version", 7u) ) {
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

/* Attachment DELETE already serialized its reference check. A failed marker
 * write leaves the deleted image missing, so retrying DELETE can acknowledge
 * the durable marker without touching another attachment. */
bool MdoApiQueueDiscardAcknowledged(const char* ProjectId,
    const char* SessionId, const char* Id)
{
    char Path[MDO_SESSION_PATH_CAPACITY];
    char Checked[33];
    MdoQueue Queue;
    size_t Index;
    bool Ok;
    int Written;
    if ( ProjectId == NULL || SessionId == NULL || Id == NULL ||
         !MdoQueueId(xrtStrView(Id), Checked) ) return false;
    Written = snprintf(Path, sizeof(Path), "sessions/%s/%s/queue.json",
        ProjectId, SessionId);
    if ( Written <= 0 || (size_t)Written >= sizeof(Path) ) return false;
    MdoProjectLease* Lease = MdoProjectLeaseAcquire(ProjectId,
        MDO_PROJECT_LEASE_SHARED, NULL);
    if ( Lease == NULL ) return false;
    xrtMutexLock(g_MdoQueueLock);
    Ok = MdoQueueRead(Path, ProjectId, SessionId, &Queue);
    if ( Ok ) {
        Index = MdoQueueDiscardFind(&Queue, Checked);
        if ( Index != SIZE_MAX ) {
            --Queue.DiscardCount;
            if ( Index < Queue.DiscardCount )
                memmove(&Queue.DiscardImages[Index],
                    &Queue.DiscardImages[Index + 1u],
                    (Queue.DiscardCount - Index) *
                    sizeof(Queue.DiscardImages[0]));
            memset(&Queue.DiscardImages[Queue.DiscardCount], 0,
                sizeof(Queue.DiscardImages[0]));
            Ok = MdoQueueWrite(Path, &Queue);
        }
        MdoQueueRelease(&Queue);
    }
    xrtMutexUnlock(g_MdoQueueLock);
    MdoProjectLeaseRelease(Lease);
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
    const char Attachments[4][33], size_t AttachmentCount,
    MdoApiProfile* Profile)
{
    char Path[MDO_SESSION_PATH_CAPACITY];
    MdoQueue Queue;
    MdoApiQueueRunStatus Result = MDO_API_QUEUE_RUN_UNAVAILABLE;
    size_t Index;
    if ( Profile != NULL ) memset(Profile, 0, sizeof(*Profile));
    if ( !MdoQueueRunPath(Path, ProjectId, SessionId) || Id == NULL ||
         Attachments == NULL || Profile == NULL ) return Result;
    MdoProjectLease* Lease = MdoProjectLeaseAcquire(ProjectId,
        MDO_PROJECT_LEASE_SHARED, NULL);
    if ( Lease == NULL ) return MDO_API_QUEUE_RUN_UNAVAILABLE;
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
                else {
                    Result = Item->RunId[0] != '\0' ||
                        (Exists && RunId[0] != '\0') ?
                        MDO_API_QUEUE_RUN_ACCEPTED :
                        (Exists ? MDO_API_QUEUE_RUN_STARTING :
                        MDO_API_QUEUE_RUN_READY);
                    if ( Result == MDO_API_QUEUE_RUN_READY )
                        *Profile = Item->Profile;
                }
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
    MdoProjectLeaseRelease(Lease);
    return Result;
}

MdoApiQueueRunStatus MdoApiQueueRunClaim(const char* ProjectId,
    const char* SessionId, const char* Id, xstrview Prompt,
    const char Attachments[4][33], size_t AttachmentCount,
    const MdoApiProfile* ExpectedProfile)
{
    char Path[MDO_SESSION_PATH_CAPACITY];
    MdoQueue Queue;
    MdoApiQueueRunStatus Result = MDO_API_QUEUE_RUN_UNAVAILABLE;
    size_t Index;
    bool Exists;
    char RunId[MDO_RUN_ID_CAPACITY];
    if ( !MdoQueueRunPath(Path, ProjectId, SessionId) || Id == NULL ||
         Attachments == NULL || ExpectedProfile == NULL ) return Result;
    MdoProjectLease* Lease = MdoProjectLeaseAcquire(ProjectId,
        MDO_PROJECT_LEASE_SHARED, NULL);
    if ( Lease == NULL ) return MDO_API_QUEUE_RUN_UNAVAILABLE;
    xrtMutexLock(g_MdoQueueLock);
    if ( MdoQueueRead(Path, ProjectId, SessionId, &Queue) ) {
        Index = MdoQueueFind(&Queue, Id);
        Result = MDO_API_QUEUE_RUN_CONFLICT;
        if ( Index != SIZE_MAX &&
             MdoQueueRunMatches(&Queue.Items[Index], Prompt, Attachments,
                AttachmentCount) &&
             MdoApiProfileEqual(&Queue.Items[Index].Profile,
                ExpectedProfile) ) {
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
    MdoProjectLeaseRelease(Lease);
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
    MdoProjectLease* Lease = MdoProjectLeaseAcquire(ProjectId,
        MDO_PROJECT_LEASE_SHARED, NULL);
    if ( Lease == NULL ) return false;
    xrtMutexLock(g_MdoQueueLock);
    if ( MdoQueueReceiptRead(ProjectId, SessionId, Id,
            &Exists, RunId) && Exists && RunId[0] == '\0' )
        Ok = MdoHomeRemove(Path, false);
    xrtMutexUnlock(g_MdoQueueLock);
    MdoProjectLeaseRelease(Lease);
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
    MdoProjectLease* Lease = MdoProjectLeaseAcquire(ProjectId,
        MDO_PROJECT_LEASE_SHARED, NULL);
    if ( Lease == NULL ) return false;
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
    MdoProjectLeaseRelease(Lease);
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
    MdoApiProfile Profile = { 0 };
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
            const xvalue* ProfileValue = xrtValueObjectGet(Body.Value,
                XRT_STR_LITERAL("profile"));
            Ok = xrtValueType(Body.Value) == XVALUE_OBJECT &&
                xrtValueCount(Body.Value) == (References == NULL ? 3u : 4u) +
                    (PriorityValue == NULL ? 0u : 1u) +
                    (StageValue == NULL ? 0u : 1u) +
                    (ProfileValue == NULL ? 0u : 1u) &&
                MdoQueueString(Body.Value, "id", &IdView) &&
                MdoQueueString(Body.Value, "text", &Text) &&
                MdoQueueBool(Body.Value, "first", &First) &&
                (PriorityValue == NULL ||
                 MdoQueueBool(Body.Value, "priority", &Priority)) &&
                (StageValue == NULL ||
                 MdoQueueBool(Body.Value, "stage", &Stage)) &&
                (ProfileValue == NULL ||
                 (MdoApiProfileRead(ProfileValue, &Profile) &&
                  Profile.Present)) &&
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
                    sizeof(Attachments)) != 0 ||
                !MdoApiProfileEqual(&Queue.Items[Index].Profile,
                    &Profile);
        } else {
            char RunId[MDO_RUN_ID_CAPACITY];
            Ok = MdoQueueReceiptRead(ProjectId, SessionId, Id,
                &Consumed, RunId);
            if ( Ok && !Consumed ) {
                Full = Queue.Count >= MDO_QUEUE_MAX_ITEMS ||
                    Text.Size > MDO_QUEUE_MAX_TOTAL_TEXT - Queue.TextBytes;
                if ( !Full ) Ok = MdoQueueInsert(&Queue, Id, Text,
                    Attachments, AttachmentCount, First, Priority,
                    Stage ? MDO_QUEUE_STAGED : MDO_QUEUE_PENDING,
                    &Profile) &&
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

/* Record cleanup before a draft drops its last image reference. A crash
 * after that draft write leaves this ID in queue.json for the next page to
 * retry. Attachment DELETE still checks every reference before removing it. */
bool MdoApiQueueDiscardRoute(MdoApiContext* Context)
{
    char Path[MDO_SESSION_PATH_CAPACITY];
    char ProjectId[MDO_PROJECT_ID_CAPACITY];
    char SessionId[MDO_SESSION_ID_CAPACITY];
    char Id[33];
    MdoSessionStatus SessionStatus;
    MdoQueue Queue;
    const xhttp1head* Head = Context->Request->head;
    bool Ok;
    bool Full = false;
    if ( Context->ParamCount != 3u ||
         !MdoQueuePath(Context, Path, &SessionStatus,
            ProjectId, SessionId) ||
         !MdoQueueId(Context->Params[2], Id) )
        return MdoApiReplyError(Context, 404u, "attachment_not_found",
            "The requested session or image does not exist", NULL);
    if ( ((Head->Flags & (uint32)XHTTP1_CONTENT_LENGTH) != 0u &&
          Head->ContentLength != 0u) ||
         (Head->Flags & (uint32)XHTTP1_TRANSFER_ENCODING) != 0u )
        return MdoApiReplyError(Context, 400u, "body_not_allowed",
            "This operation does not accept a request body", NULL);
    xrtMutexLock(g_MdoQueueLock);
    Ok = MdoQueueRead(Path, ProjectId, SessionId, &Queue);
    if ( Ok && MdoQueueDiscardFind(&Queue, Id) == SIZE_MAX ) {
        Full = !MdoQueueDiscardAdd(&Queue, Id);
        if ( !Full ) Ok = MdoQueueWrite(Path, &Queue);
    }
    xrtMutexUnlock(g_MdoQueueLock);
    if ( !Ok ) { MdoQueueRelease(&Queue); return MdoApiReplyError(Context,
        503u, "queue_unavailable", "Image cleanup could not be recorded",
        NULL); }
    if ( Full ) { MdoQueueRelease(&Queue); return MdoApiReplyError(Context,
        507u, "queue_cleanup_full",
        "Too many images are awaiting cleanup", NULL); }
    return MdoQueueReply(Context, 200u, &Queue);
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
    bool CleanupFull = false;
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
        Ok = MdoQueueReceiptResolve(ProjectId, SessionId, Id,
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
            if ( Ok && Queue.Items[Index].RunId[0] == '\0' &&
                 !Queue.Items[Index].StartClaimed ) {
                size_t ImageIndex;
                for ( ImageIndex = 0u;
                      ImageIndex < Queue.Items[Index].AttachmentCount;
                      ++ImageIndex )
                    if ( !MdoQueueDiscardAdd(&Queue,
                            Queue.Items[Index].Attachments[ImageIndex]) ) {
                        CleanupFull = true;
                        break;
                    }
            }
            if ( Ok && !CleanupFull ) {
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
    if ( CleanupFull ) { MdoQueueRelease(&Queue); return MdoApiReplyError(
        Context, 507u, "queue_cleanup_full",
        "Too many cancelled images are awaiting cleanup", NULL); }
    if ( Conflict ) { MdoQueueRelease(&Queue); return MdoApiReplyError(Context,
        409u, "queue_state_conflict", "The queue item changed state", NULL); }
    return MdoQueueReply(Context, 200u, &Queue);
}
