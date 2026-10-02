#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backup_restore.h"
#include "backup_preview.h"
#include "../../include/mdo/session_restore.h"
#include "../../include/mdo/home_restore.h"

#define MDO_BACKUP_RESTORE_TTL_US 300000000u

typedef enum MdoBackupRestorePhase {
    MDO_BACKUP_RESTORE_REVIEW,
    MDO_BACKUP_RESTORE_ADMITTING,
    MDO_BACKUP_RESTORE_QUEUED,
    MDO_BACKUP_RESTORE_RUNNING,
    MDO_BACKUP_RESTORE_DONE
} MdoBackupRestorePhase;

typedef struct MdoBackupRestoreInfo {
    MdoSessionRestoreRequest Request;
    MdoSessionBackupPreview Source;
    char PreviewId[33], Hash[65];
    MdoBackupRestorePhase Phase;
    xtime CreatedAt, StartedAt, EndedAt;
    xdeadline ExpiresAt;
    bool Accepted, CancelRequested;
    MdoSessionRestoreResult Result;
    xwork_error Error;
} MdoBackupRestoreInfo;

typedef struct MdoBackupRestoreStore MdoBackupRestoreStore;
typedef struct MdoBackupRestoreJob {
    MdoBackupRestoreStore* Store;
    MdoBackupPreviewDocument* Document;
    MdoSessionRestoreOperation* Operation;
    xcancel* Cancel;
    MdoBackupRestoreInfo Info; /* protected by Store.Lock, except worker locals */
} MdoBackupRestoreJob;

struct MdoBackupRestoreStore {
    xmutex* Lock;
    xtaskpool* Pool;
    MdoBackupRestoreJob* Slot;
    MdoBackupRestoreJob* Active; /* owns admission through acceptance/Drop */
    bool Stopping, Releasing;
};

static MdoBackupRestoreStore* g_MdoBackupRestores;

static bool MdoBackupRestoreNoBody(const MdoApiContext* Context)
{
    const xhttp1head* Head = Context->Request->head;
    return (Head->Flags & XHTTP1_TRANSFER_ENCODING) == 0u &&
        ((Head->Flags & XHTTP1_CONTENT_LENGTH) == 0u || Head->ContentLength == 0u);
}

static bool MdoBackupRestoreId(const MdoApiContext* Context, char Id[33])
{
    size_t i;
    if ( Context->ParamCount != 1u || Context->Params[0].Size != 32u ) return false;
    for ( i = 0u; i < 32u; ++i ) {
        char Ch = Context->Params[0].Data[i];
        if ( !((Ch >= '0' && Ch <= '9') || (Ch >= 'a' && Ch <= 'f')) ) return false;
    }
    memcpy(Id, Context->Params[0].Data, 32u); Id[32] = '\0'; return true;
}

static bool MdoBackupRestoreRandomId(char Id[33])
{
    static const char Hex[] = "0123456789abcdef";
    uint8 Random[16];
    size_t i;
    if ( !xrtSecureRandom(Random, sizeof(Random)) ) return false;
    for ( i = 0u; i < sizeof(Random); ++i ) {
        Id[i * 2u] = Hex[Random[i] >> 4u]; Id[i * 2u + 1u] = Hex[Random[i] & 15u];
    }
    Id[32] = '\0'; return true;
}

static void MdoBackupRestoreJobFree(MdoBackupRestoreJob* Job)
{
    if ( Job == NULL ) return;
    /* A lifecycle programming violation must not release borrowed bytes or
     * unload TCC with an accepted operation still live. Normal IO failures
     * consume the operation and retain their evidence in Home instead. */
    if ( Job->Operation != NULL ) abort();
    MdoApiBackupPreviewRelease(Job->Document);
    xrtCancelDestroy(Job->Cancel); xrtFree(Job);
}

static MdoBackupRestoreJob* MdoBackupRestoreDetachLocked(MdoBackupRestoreStore* Store)
{
    MdoBackupRestoreJob* Job = Store->Slot;
    if ( Job == NULL || Store->Active != NULL ) return NULL;
    Store->Slot = NULL; Store->Releasing = true;
    return Job;
}

static void MdoBackupRestoreRetire(MdoBackupRestoreStore* Store, MdoBackupRestoreJob* Job)
{
    if ( Job == NULL ) return;
    MdoBackupRestoreJobFree(Job); /* large preview cleanup outside status lock */
    xrtMutexLock(Store->Lock); Store->Releasing = false; xrtMutexUnlock(Store->Lock);
}

static MdoBackupRestoreJob* MdoBackupRestoreCollectLocked(MdoBackupRestoreStore* Store)
{
    return Store->Slot != NULL && xrtDeadlineExpired(Store->Slot->Info.ExpiresAt) ?
        MdoBackupRestoreDetachLocked(Store) : NULL;
}

bool MdoApiBackupRestoresInit(void)
{
    MdoBackupRestoreStore* Store;
    if ( g_MdoBackupRestores != NULL ) return true;
    Store = xrtCalloc(1u, sizeof(*Store));
    if ( Store == NULL ) return false;
    Store->Lock = xrtMutexCreate();
    if ( Store->Lock == NULL ) { xrtFree(Store); return false; }
    g_MdoBackupRestores = Store;
    return true;
}

void MdoApiBackupRestoresUnit(void)
{
    MdoBackupRestoreStore* Store = g_MdoBackupRestores;
    xcancel* Cancel;
    if ( Store == NULL ) return;
    g_MdoBackupRestores = NULL;
    xrtMutexLock(Store->Lock); Store->Stopping = true;
    Cancel = Store->Active != NULL ? xrtCancelRef(Store->Active->Cancel) : NULL;
    xrtMutexUnlock(Store->Lock);
    if ( Cancel != NULL ) { (void)xrtCancelRequest(Cancel); xrtCancelDestroy(Cancel); }
    if ( Store->Pool != NULL ) {
        (void)xrtTaskPoolCancel(Store->Pool); (void)xrtTaskPoolWait(Store->Pool);
        (void)xrtTaskPoolDestroy(Store->Pool);
    }
    MdoBackupRestoreJobFree(Store->Slot);
    xrtMutexDestroy(Store->Lock); xrtFree(Store);
}

static cstr MdoBackupRestorePhaseText(MdoBackupRestorePhase Phase)
{
    switch ( Phase ) {
    case MDO_BACKUP_RESTORE_ADMITTING: return "admitting";
    case MDO_BACKUP_RESTORE_QUEUED: return "queued";
    case MDO_BACKUP_RESTORE_RUNNING: return "running";
    case MDO_BACKUP_RESTORE_DONE: return "done";
    default: return "review";
    }
}

/* Persistent facts are authoritative. Memory contributes only a reviewed
 * workspace/source summary and resident worker diagnostics, never rollback or
 * permission to retry. No source bytes/current project are read here. */
static xvalue* MdoBackupRestoreValue(const MdoHomeSessionRestoreReceipt* Receipt,
    const MdoBackupRestoreInfo* Info)
{
    xvalue* Value = xrtValueObject();
    const MdoHomeSessionRestoreRequest* Recorded = Receipt != NULL ? &Receipt->Request : NULL;
    const MdoSessionRestoreRequest* Review = Info != NULL ? &Info->Request : NULL;
    MdoHomeSnapshot Home = {0};
    bool Accepted = Receipt != NULL;
    cstr State = Receipt != NULL ? (Receipt->Outcome == MDO_HOME_SESSION_RESTORE_COMMITTED ? "committed" :
        Receipt->Outcome == MDO_HOME_SESSION_RESTORE_ABORTED ? "aborted" : "pending") :
        (Info->Phase == MDO_BACKUP_RESTORE_DONE ? "not_accepted" :
            Info->Phase == MDO_BACKUP_RESTORE_ADMITTING ? "admitting" : "review");
    bool Ok;
    Home.Size = sizeof(Home);
    (void)MdoHomeGetSnapshot(&Home);
    Ok = Value != NULL &&
        MdoApiValueSetString(Value, "id", Recorded != NULL ? Recorded->SessionId : Review->SessionId) &&
        MdoApiValueSetString(Value, "session_id", Recorded != NULL ? Recorded->SessionId : Review->SessionId) &&
        MdoApiValueSetString(Value, "project_id", Recorded != NULL ? Recorded->ProjectId : Review->Binding.ProjectId) &&
        MdoApiValueSetString(Value, "source_session_id", Recorded != NULL ? Recorded->SourceSessionId : Info->Source.Info.Id) &&
        MdoApiValueSetString(Value, "source_sha256", Recorded != NULL ? Recorded->SourceSha256 : Info->Hash) &&
        MdoApiValueSetUInt(Value, "project_revision", Recorded != NULL ? Recorded->ProjectRevision : Review->Binding.Revision) &&
        MdoApiValueSetInt(Value, "project_created_at", Recorded != NULL ? Recorded->ProjectCreatedAt : Review->Binding.CreatedAt) &&
        MdoApiValueSetInt(Value, "restored_at", Recorded != NULL ? Recorded->RestoredAt : Review->RestoredAt) &&
        MdoApiValueSetString(Value, "state", State) &&
        MdoApiValueSetBool(Value, "accepted", Accepted) &&
        MdoApiValueSetBool(Value, "committed", Receipt != NULL && Receipt->Committed) &&
        MdoApiValueSetBool(Value, "restart_required", Home.RestartRequired ||
            (Info != NULL && Info->Result.RestartRequired)) &&
        MdoApiValueSetBool(Value, "terminal", Receipt != NULL ? Receipt->Outcome != MDO_HOME_SESSION_RESTORE_PENDING :
            Info->Phase == MDO_BACKUP_RESTORE_DONE) &&
        MdoApiValueSetBool(Value, "cleanup_pending", Receipt != NULL &&
            Receipt->Outcome == MDO_HOME_SESSION_RESTORE_PENDING && Receipt->Committed);
    if ( Ok && Info != NULL ) {
        Ok = MdoApiValueSetString(Value, "phase", MdoBackupRestorePhaseText(Info->Phase)) &&
            MdoApiValueSetString(Value, "preview_id", Info->PreviewId) &&
            MdoApiValueSetString(Value, "workspace_root", Info->Request.Binding.WorkspaceRoot) &&
            MdoApiValueSetString(Value, "source_title", Info->Source.Info.Title) &&
            MdoApiValueSetString(Value, "source_project_id", Info->Source.Info.ProjectId) &&
            MdoApiValueSetString(Value, "source_workspace_root", Info->Source.Info.WorkspaceRoot) &&
            MdoApiValueSetString(Value, "source_model_id", Info->Source.Info.ModelId) &&
            MdoApiValueSetString(Value, "source_agent_id", Info->Source.Info.AgentId) &&
            MdoApiValueSetString(Value, "source_protocol", MdoModelProtocolName(Info->Source.Info.Protocol)) &&
            MdoApiValueSetInt(Value, "source_captured_at", Info->Source.CapturedAt) &&
            MdoApiValueSetUInt(Value, "unverified_history_references", Info->Source.UnverifiedHistoryReferences) &&
            MdoApiValueSetUInt(Value, "file_count", Info->Source.Files) &&
            MdoApiValueSetUInt(Value, "total_bytes", Info->Source.Bytes) &&
            MdoApiValueSetInt(Value, "created_at", Info->CreatedAt) &&
            MdoApiValueSetInt(Value, "started_at", Info->StartedAt) &&
            MdoApiValueSetInt(Value, "ended_at", Info->EndedAt) &&
            MdoApiValueSetBool(Value, "worker_finished", Info->Phase == MDO_BACKUP_RESTORE_DONE) &&
            MdoApiValueSetBool(Value, "cancel_requested", Info->CancelRequested) &&
            MdoApiValueSetUInt(Value, "catalog_generation", Info->Result.CatalogGeneration) &&
            MdoApiValueSetUInt(Value, "error_code", Info->Error.eCode) &&
            MdoApiValueSetString(Value, "message", Info->Error.sMessage);
    }
    if ( !Ok ) { xrtValueRelease(Value); return NULL; }
    return Value;
}

static bool MdoBackupRestoreReply(MdoApiContext* Context, cstr Id, uint16 Status)
{
    MdoBackupRestoreStore* Store = g_MdoBackupRestores;
    MdoBackupRestoreInfo Info;
    MdoHomeSessionRestoreReceipt Receipt = {0};
    MdoBackupRestoreJob* Retired;
    bool Found, Resident = false;
    xvalue* Value;
    Receipt.Size = sizeof(Receipt);
    if ( !MdoHomeSessionRestoreReceiptGet(Id, &Receipt, &Found) )
        return MdoApiReplyError(Context, 503u, "restore_result_unavailable",
            "Cannot verify this restore result; keep the same request ID", NULL);
    if ( Store != NULL ) {
        xrtMutexLock(Store->Lock); Retired = MdoBackupRestoreCollectLocked(Store);
        if ( Store->Slot != NULL && strcmp(Store->Slot->Info.Request.SessionId, Id) == 0 ) {
            Info = Store->Slot->Info; Resident = true;
        }
        xrtMutexUnlock(Store->Lock); MdoBackupRestoreRetire(Store, Retired);
    }
    /* Acceptance/settlement can finish between the first Home read and the
     * small memory copy. Never label a resident accepted/finished task as an
     * unaccepted review because the earlier read raced its owner flush. */
    if ( !Found && Resident && Info.Phase != MDO_BACKUP_RESTORE_REVIEW ) {
        if ( !MdoHomeSessionRestoreReceiptGet(Id, &Receipt, &Found) || (!Found && Info.Accepted) )
            return MdoApiReplyError(Context, 503u, "restore_result_unavailable",
                "Cannot verify accepted restore evidence; keep the same request ID", NULL);
    }
    if ( !Found && !Resident ) return MdoApiReplyError(Context, 404u, "restore_request_not_found",
        "No accepted request or current review is known; this does not authorize a new restore", NULL);
    Value = MdoBackupRestoreValue(Found ? &Receipt : NULL, Resident ? &Info : NULL);
    if ( Value == NULL ) return MdoApiReplyError(Context, 500u, "restore_result_unavailable", "Cannot serialize restore facts", NULL);
    return MdoApiReplySuccessTake(Context, Status, Value, NULL);
}

static xtaskoutcome MdoBackupRestoreRun(xcancel* Cancel, ptr Data, xtaskvalue* Result)
{
    MdoBackupRestoreJob* Job = Data;
    MdoBackupRestoreStore* Store = Job->Store;
    MdoSessionRestoreResult Facts;
    xwork_error Error;
    bool Ok;
    (void)Result;
    xrtMutexLock(Store->Lock); Job->Info.Phase = MDO_BACKUP_RESTORE_RUNNING;
    Job->Info.StartedAt = xrtNow(); xrtMutexUnlock(Store->Lock);
    MdoSessionRestoreResultInit(&Facts);
    Ok = MdoSessionRestoreExecute(&Job->Operation, Cancel, &Facts, &Error);
    xrtMutexLock(Store->Lock); Job->Info.Result = Facts; Job->Info.Error = Error;
    xrtMutexUnlock(Store->Lock);
    return Ok ? XTASK_SUCCESS : (Error.eCode == XWORK_ERROR_CANCELLED ? XTASK_CANCELLED : XTASK_FAILED);
}

static void MdoBackupRestoreDrop(ptr Data, ptr Context)
{
    MdoBackupRestoreJob* Job = Data;
    MdoBackupRestoreStore* Store = Job->Store;
    MdoSessionRestoreResult Facts;
    xwork_error Error;
    bool Skipped = Job->Operation != NULL;
    (void)Context;
    if ( Skipped ) {
        MdoSessionRestoreResultInit(&Facts);
        (void)MdoSessionRestoreDiscard(&Job->Operation, &Facts, &Error);
        if ( Job->Operation != NULL ) abort(); /* managers must still be live */
        xrtMutexLock(Store->Lock); Job->Info.Result = Facts;
        if ( Error.eCode != XWORK_ERROR_NONE ) Job->Info.Error = Error;
        else if ( Job->Info.Error.eCode == XWORK_ERROR_NONE ) {
            Job->Info.Error.eCode = XWORK_ERROR_CANCELLED;
            snprintf(Job->Info.Error.sMessage, sizeof(Job->Info.Error.sMessage), "Restore task was cancelled before execution");
        }
        xrtMutexUnlock(Store->Lock);
    }
    MdoApiBackupPreviewRelease(Job->Document); Job->Document = NULL;
    xrtMutexLock(Store->Lock);
    /* Cancellation after the directory commit cannot change its outcome. */
    Job->Info.CancelRequested = Job->Info.CancelRequested || xrtCancelRequested(Job->Cancel);
    Job->Info.Phase = MDO_BACKUP_RESTORE_DONE; Job->Info.EndedAt = xrtNow();
    Job->Info.ExpiresAt = xrtDeadlineAfter(MDO_BACKUP_RESTORE_TTL_US);
    Store->Active = NULL;
    xrtMutexUnlock(Store->Lock);
}

bool MdoApiBackupRestoreReviewRoute(MdoApiContext* Context)
{
    MdoBackupRestoreStore* Store = g_MdoBackupRestores;
    MdoBackupRestoreJob *Job = NULL, *Retired;
    MdoBackupPreviewAccess Access;
    MdoApiJsonBody Body;
    MdoApiBodyStatus BodyStatus;
    MdoProjectLease* Owner = NULL;
    xstrview Project;
    xwork_error Error;
    char Preview[33], Id[33], ProjectId[MDO_PROJECT_ID_CAPACITY];
    uint16 Status = 201u;
    cstr Code = "restore_review_unavailable", Message = "Cannot prepare restore review";
    bool Valid;
    if ( !MdoBackupRestoreId(Context, Preview) ) return MdoApiReplyError(Context, 400u,
        "restore_review_invalid", "Use a valid preview ID", NULL);
    BodyStatus = MdoApiJsonBodyRead(Context, &Body);
    if ( BodyStatus != MDO_API_BODY_OK ) return MdoApiReplyBodyError(Context, BodyStatus);
    Job = xrtCalloc(1u, sizeof(*Job));
    if ( Job == NULL ) { MdoApiJsonBodyUnit(&Body); goto failed; }
    MdoSessionRestoreRequestInit(&Job->Info.Request);
    Valid = xrtValueType(Body.Value) == XVALUE_OBJECT && xrtValueCount(Body.Value) == 1u &&
        xrtValueGetString(xrtValueObjectGet(Body.Value, XRT_STR_LITERAL("project_id")), &Project) &&
        Project.Size != 0u && Project.Size < sizeof(Job->Info.Request.Binding.ProjectId) &&
        memchr(Project.Data, '\0', Project.Size) == NULL;
    if ( Valid ) {
        memcpy(Job->Info.Request.Binding.ProjectId, Project.Data, Project.Size);
        Job->Info.Request.Binding.ProjectId[Project.Size] = '\0';
    }
    MdoApiJsonBodyUnit(&Body);
    if ( !Valid ) { Status = 400u; Code = "restore_review_invalid"; Message = "Explicitly select one target project"; goto failed; }
    if ( Store == NULL ) { Status = 503u; goto failed; }
    Job->Store = Store;
    memcpy(ProjectId, Job->Info.Request.Binding.ProjectId, sizeof(ProjectId));
    Owner = MdoProjectLeaseAcquire(ProjectId, MDO_PROJECT_LEASE_SHARED, &Error);
    if ( Owner == NULL || !MdoProjectBindingGet(ProjectId, &Job->Info.Request.Binding, &Error) ) {
        Status = Error.eCode == XWORK_ERROR_INVALID_ARGUMENT ? 400u : 409u;
        Code = "restore_target_unavailable"; Message = "Select an available project with a physical workspace"; goto failed;
    }
    MdoProjectLeaseRelease(Owner); Owner = NULL;
    Job->Document = MdoApiBackupPreviewAcquire(Preview, &Access);
    if ( Job->Document == NULL ) {
        Status = Access == MDO_BACKUP_PREVIEW_ACCESS_MISSING ? 404u :
            Access == MDO_BACKUP_PREVIEW_ACCESS_NOT_READY ? 409u : 503u;
        Code = "restore_preview_unavailable"; Message = "Inspect a complete backup before preparing its restore"; goto failed;
    }
    Job->Info.Source.Size = sizeof(Job->Info.Source);
    if ( !MdoSessionBackupPreviewGet(MdoApiBackupPreviewData(Job->Document), &Job->Info.Source) ||
         Job->Info.Source.ExportSchema != MDO_SESSION_BACKUP_SCHEMA ) {
        Status = 422u; Code = "restore_partial_backup"; Message = "Legacy partial exports cannot restore a complete session"; goto failed;
    }
    if ( !MdoApiBackupPreviewHash(Job->Document, Job->Info.Hash) ||
         !MdoBackupRestoreRandomId(Job->Info.Request.SessionId) ) { Status = 503u; goto failed; }
    memcpy(Job->Info.PreviewId, Preview, sizeof(Preview));
    Job->Info.CreatedAt = Job->Info.Request.RestoredAt = xrtNow();
    Job->Info.ExpiresAt = xrtDeadlineAfter(MDO_BACKUP_RESTORE_TTL_US);
    MdoSessionRestoreResultInit(&Job->Info.Result);
    xrtMutexLock(Store->Lock); Retired = MdoBackupRestoreCollectLocked(Store);
    if ( Retired != NULL ) {
        xrtMutexUnlock(Store->Lock); MdoBackupRestoreRetire(Store, Retired); Retired = NULL;
        xrtMutexLock(Store->Lock);
    }
    if ( Store->Stopping || Store->Releasing || Store->Slot != NULL ) {
        if ( Store->Slot != NULL && Store->Slot->Info.Phase == MDO_BACKUP_RESTORE_REVIEW &&
             strcmp(Store->Slot->Info.PreviewId, Preview) == 0 &&
             strcmp(Store->Slot->Info.Request.Binding.ProjectId, Job->Info.Request.Binding.ProjectId) == 0 ) {
            memcpy(Id, Store->Slot->Info.Request.SessionId, sizeof(Id)); Status = 200u;
        } else { Status = 409u; Code = "restore_busy"; Message = "Finish or discard the current restore review first"; }
        xrtMutexUnlock(Store->Lock); MdoBackupRestoreRetire(Store, Retired);
        if ( Status == 200u ) { MdoBackupRestoreJobFree(Job); return MdoBackupRestoreReply(Context, Id, Status); }
        goto failed;
    }
    memcpy(Id, Job->Info.Request.SessionId, sizeof(Id)); Store->Slot = Job; Job = NULL;
    xrtMutexUnlock(Store->Lock); MdoBackupRestoreRetire(Store, Retired);
    return MdoBackupRestoreReply(Context, Id, Status);
failed:
    MdoProjectLeaseRelease(Owner); MdoBackupRestoreJobFree(Job);
    return MdoApiReplyError(Context, Status >= 400u ? Status : 503u, Code, Message, NULL);
}

bool MdoApiBackupRestoreApplyRoute(MdoApiContext* Context)
{
    MdoBackupRestoreStore* Store = g_MdoBackupRestores;
    MdoBackupRestoreJob* Job;
    MdoHomeSessionRestoreReceipt Receipt = {0};
    MdoSessionRestoreOperation* Operation;
    xwork_error Error;
    xtaskargs Args = {0};
    xfuture* Future;
    char Id[33];
    bool Found;
    if ( !MdoBackupRestoreId(Context, Id) || !MdoBackupRestoreNoBody(Context) )
        return MdoApiReplyError(Context, 400u, "restore_request_invalid", "Use a reviewed request ID and an empty body", NULL);
    Receipt.Size = sizeof(Receipt);
    if ( !MdoHomeSessionRestoreReceiptGet(Id, &Receipt, &Found) )
        return MdoApiReplyError(Context, 503u, "restore_result_unavailable", "Keep this request ID and query its result", NULL);
    if ( Found ) return MdoBackupRestoreReply(Context, Id, 200u);
    if ( Store == NULL ) return MdoApiReplyError(Context, 503u, "restore_unavailable", "Restore admission is unavailable", NULL);
    xrtMutexLock(Store->Lock); Job = Store->Slot;
    if ( Store->Stopping || Job == NULL || strcmp(Job->Info.Request.SessionId, Id) != 0 ||
         (Store->Active == NULL && xrtDeadlineExpired(Job->Info.ExpiresAt)) ) {
        xrtMutexUnlock(Store->Lock);
        return MdoApiReplyError(Context, 404u, "restore_review_not_found", "The review is no longer available; no restore was started by this call", NULL);
    }
    if ( Store->Active != NULL || Job->Info.Phase != MDO_BACKUP_RESTORE_REVIEW ) {
        xrtMutexUnlock(Store->Lock);
        return MdoApiReplyError(Context, 409u, "restore_admission_pending", "Query this same request; it cannot be submitted twice", NULL);
    }
    if ( Store->Pool == NULL ) {
        xtaskpoolconfig Config = {0}; Config.Threads = 1u; Config.QueueLimit = 1u;
        Store->Pool = xrtTaskPoolCreate(&Config);
    }
    Job->Cancel = xrtCancelCreate();
    if ( Store->Pool == NULL || Job->Cancel == NULL ) {
        xrtCancelDestroy(Job->Cancel); Job->Cancel = NULL; xrtMutexUnlock(Store->Lock);
        return MdoApiReplyError(Context, 503u, "restore_unavailable", "Cannot allocate a restore worker; the review remains available", NULL);
    }
    Store->Active = Job; Job->Info.Phase = MDO_BACKUP_RESTORE_ADMITTING;
    xrtMutexUnlock(Store->Lock);
    /* Active owns Job through admission/Drop. Cancellation can request its
     * token, but cannot retire the preview pin while Home acceptance runs. */
    Operation = MdoSessionRestoreAccept(MdoApiBackupPreviewData(Job->Document), &Job->Info.Request,
        Job->Info.Hash, NULL, Job->Cancel, &Error);
    xrtMutexLock(Store->Lock); Job->Operation = Operation; Job->Info.Error = Error;
    Job->Info.Accepted = Operation != NULL;
    if ( Operation != NULL ) Job->Info.Phase = MDO_BACKUP_RESTORE_QUEUED;
    xrtMutexUnlock(Store->Lock);
    if ( Operation == NULL ) {
        MdoBackupRestoreDrop(Job, NULL);
        /* Failed acceptance may already have flushed owner/aborted evidence.
         * The response still identifies the same request for recovery. */
        return MdoBackupRestoreReply(Context, Id, 200u);
    }
    Args.Cancel = Job->Cancel; Args.Destroy = MdoBackupRestoreDrop;
    Future = xrtTaskSubmit(Store->Pool, MdoBackupRestoreRun, Job, &Args);
    if ( Future == NULL ) {
        xrtMutexLock(Store->Lock); Job->Info.Error.eCode = XWORK_ERROR_IO;
        snprintf(Job->Info.Error.sMessage, sizeof(Job->Info.Error.sMessage), "Restore worker rejected the accepted task");
        xrtMutexUnlock(Store->Lock); MdoBackupRestoreDrop(Job, NULL);
        return MdoBackupRestoreReply(Context, Id, 200u);
    }
    /* Drop may have run inline and concurrent DELETE may have retired Job.
     * Only the future and copied request ID remain ours after submission. */
    xrtFutureDestroy(Future);
    return MdoBackupRestoreReply(Context, Id, 202u);
}

bool MdoApiBackupRestoreRoute(MdoApiContext* Context)
{
    MdoBackupRestoreStore* Store = g_MdoBackupRestores;
    MdoBackupRestoreJob* Retired = NULL;
    xcancel* Cancel = NULL;
    xvalue* Value;
    char Id[33];
    if ( !MdoBackupRestoreId(Context, Id) || !MdoBackupRestoreNoBody(Context) )
        return MdoApiReplyError(Context, 400u, "restore_request_invalid", "Use a valid request ID and an empty body", NULL);
    if ( Context->Request->head->MethodCode != XHTTP_METHOD_DELETE )
        return MdoBackupRestoreReply(Context, Id, 200u);
    if ( Store != NULL ) {
        xrtMutexLock(Store->Lock);
        if ( Store->Slot != NULL && strcmp(Store->Slot->Info.Request.SessionId, Id) == 0 ) {
            Store->Slot->Info.CancelRequested = true;
            if ( Store->Active != NULL ) Cancel = xrtCancelRef(Store->Slot->Cancel);
            else Retired = MdoBackupRestoreDetachLocked(Store);
        }
        xrtMutexUnlock(Store->Lock);
        if ( Cancel != NULL ) { (void)xrtCancelRequest(Cancel); xrtCancelDestroy(Cancel); }
        MdoBackupRestoreRetire(Store, Retired);
    }
    /* DELETE is idempotent. Losing a review never allows a late apply; a
     * terminal accepted result remains in Home even after resident cleanup. */
    Value = xrtValueObject();
    if ( Value == NULL || !MdoApiValueSetString(Value, "id", Id) ||
         !MdoApiValueSetBool(Value, "cancel_requested", true) ) {
        xrtValueRelease(Value);
        return MdoApiReplyError(Context, 500u, "restore_result_unavailable", "Cannot serialize cancellation acknowledgement", NULL);
    }
    return MdoApiReplySuccessTake(Context, 200u, Value, NULL);
}
