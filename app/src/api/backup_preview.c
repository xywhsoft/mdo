#include <stdio.h>
#include <string.h>

#include "backup_preview.h"
#include "backup_upload.h"
#include "../../include/mdo/session_backup.h"

#define MDO_BACKUP_PREVIEW_TIMEOUT_US 30000000u
#define MDO_BACKUP_PREVIEW_TTL_US 300000000u
#define MDO_BACKUP_PREVIEW_STEPS 3u

typedef enum MdoBackupPreviewState {
    MDO_BACKUP_PREVIEW_PENDING,
    MDO_BACKUP_PREVIEW_RUNNING,
    MDO_BACKUP_PREVIEW_SUCCEEDED,
    MDO_BACKUP_PREVIEW_FAILED,
    MDO_BACKUP_PREVIEW_CANCELLED
} MdoBackupPreviewState;

/* Only copied facts escape the lock. No file contents or live model session
 * are exposed to HTTP serialization. Stage counts describe completed gates,
 * not an estimated percentage of CPU time. */
typedef struct MdoBackupPreviewInfo {
    char Id[33], UploadId[33], UploadHash[65];
    MdoBackupPreviewState State;
    unsigned Completed;
    xtime CreatedAt, StartedAt, EndedAt;
    xdeadline ExpiresAt;
    bool CancelRequested, Discarded, ResultAvailable;
    xwork_error Error;
    MdoSessionBackupPreview Backup;
    MdoSessionBackupModelHistory History;
    MdoSessionBackupImages Images;
} MdoBackupPreviewInfo;

typedef struct MdoBackupPreviewStore MdoBackupPreviewStore;
struct MdoBackupPreviewDocument {
    MdoBackupPreviewStore* Store;
    MdoSessionBackup* Backup;
    char Hash[65];
    size_t Pins;
    bool Removed;
};

typedef struct MdoBackupPreviewJob {
    MdoBackupPreviewStore* Store;
    MdoBackupPreviewDocument* Document;
    MdoBackupUploadDocument* Upload;
    MdoSessionBackup* Backup;
    MdoSessionBackupLimits Limits;
    MdoBackupPreviewInfo Info;
    bool Succeeded;
} MdoBackupPreviewJob;

struct MdoBackupPreviewStore {
    xmutex* Lock;
    xtaskpool* Pool;
    xcancel* Cancel;
    MdoBackupPreviewJob* Active;
    MdoBackupPreviewDocument* Slot;
    MdoBackupPreviewInfo Info;
    size_t Users; /* manager owner + acquired pins, protected by Lock */
    bool Stopping, Releasing;
};

static MdoBackupPreviewStore* g_MdoBackupPreviews;

static cstr MdoBackupPreviewStateText(MdoBackupPreviewState State)
{
    switch ( State ) {
    case MDO_BACKUP_PREVIEW_RUNNING: return "running";
    case MDO_BACKUP_PREVIEW_SUCCEEDED: return "succeeded";
    case MDO_BACKUP_PREVIEW_FAILED: return "failed";
    case MDO_BACKUP_PREVIEW_CANCELLED: return "cancelled";
    default: return "pending";
    }
}

static bool MdoBackupPreviewTerminal(const MdoBackupPreviewInfo* Info)
{
    return Info->State >= MDO_BACKUP_PREVIEW_SUCCEEDED;
}

/* Lazily reclaim only terminal results. An accepted worker owns its slot
 * through Drop, including a skipped/cancelled task and all cleanup. Expiry is
 * never a reason to submit a second memory-heavy job while the first is alive. */
static MdoBackupPreviewDocument* MdoBackupPreviewDetachLocked(MdoBackupPreviewStore* Store)
{
    MdoBackupPreviewDocument* Document = Store->Slot;
    if ( Document == NULL || !Document->Removed || Document->Pins != 0u ) return NULL;
    Store->Slot = NULL; Store->Releasing = true;
    return Document;
}

static MdoBackupPreviewDocument* MdoBackupPreviewCollectLocked(MdoBackupPreviewStore* Store)
{
    if ( Store->Active == NULL && Store->Info.Id[0] != '\0' &&
         xrtDeadlineExpired(Store->Info.ExpiresAt) ) {
        if ( Store->Slot != NULL ) Store->Slot->Removed = true;
        memset(&Store->Info, 0, sizeof(Store->Info));
    }
    return MdoBackupPreviewDetachLocked(Store);
}

/* Releasing a terminal result also owns admission until its bytes are freed.
 * A concurrent start cannot overlap a new decoded payload with stale cleanup,
 * and freeing a bundle never holds the short status mutex. */
static void MdoBackupPreviewReleaseRetired(MdoBackupPreviewDocument* Document)
{
    MdoBackupPreviewStore* Store;
    if ( Document == NULL ) return;
    /* The caller keeps its pin or the manager owner until after this returns.
     * Cleanup targets the owning generation, never the current global store. */
    Store = Document->Store;
    MdoSessionBackupRelease(Document->Backup); xrtFree(Document);
    xrtMutexLock(Store->Lock); Store->Releasing = false;
    xrtMutexUnlock(Store->Lock);
}

static void MdoBackupPreviewStoreFree(MdoBackupPreviewStore* Store)
{
    xrtMutexDestroy(Store->Lock); xrtFree(Store);
}

static bool MdoBackupPreviewStep(MdoBackupPreviewJob* Job, xcancel* Cancel,
    unsigned Completed)
{
    MdoBackupPreviewStore* Store = Job->Store;
    bool Live;
    xrtMutexLock(Store->Lock);
    Live = !Store->Stopping && !Store->Info.CancelRequested;
    Store->Info.State = MDO_BACKUP_PREVIEW_RUNNING;
    Store->Info.Completed = Completed;
    if ( Store->Info.StartedAt == 0 ) Store->Info.StartedAt = xrtNow();
    Job->Info.StartedAt = Store->Info.StartedAt;
    xrtMutexUnlock(Store->Lock);
    if ( !Live || xrtCancelRequested(Cancel) ) {
        Job->Info.Error.eCode = XWORK_ERROR_CANCELLED;
        snprintf(Job->Info.Error.sMessage, sizeof(Job->Info.Error.sMessage),
            "Backup inspection was cancelled");
        return false;
    }
    if ( xrtDeadlineExpired(Job->Limits.Deadline) ) {
        Job->Info.Error.eCode = XWORK_ERROR_LIMIT;
        snprintf(Job->Info.Error.sMessage, sizeof(Job->Info.Error.sMessage),
            "Backup inspection exceeded its thirty-second budget");
        return false;
    }
    return true;
}

static xtaskoutcome MdoBackupPreviewRun(xcancel* Cancel, ptr Data,
    xtaskvalue* Result)
{
    MdoBackupPreviewJob* Job = (MdoBackupPreviewJob*)Data;
    (void)Result;
    if ( !MdoBackupPreviewStep(Job, Cancel, 0u) ) goto done;
    Job->Backup = MdoSessionBackupDecode(MdoApiBackupUploadData(Job->Upload),
        MdoApiBackupUploadBytes(Job->Upload), &Job->Limits, Cancel, &Job->Info.Error);
    /* Decode owns every retained byte. A removed/expired transport document
     * can now be reclaimed before model replay or pixel decoding begins. */
    MdoApiBackupUploadRelease(Job->Upload); Job->Upload = NULL;
    if ( Job->Backup == NULL ) goto done;
    Job->Info.Backup.Size = sizeof(Job->Info.Backup);
    if ( !MdoSessionBackupPreviewGet(Job->Backup, &Job->Info.Backup) ) {
        Job->Info.Error.eCode = XWORK_ERROR_IO;
        snprintf(Job->Info.Error.sMessage, sizeof(Job->Info.Error.sMessage),
            "Cannot read decoded backup facts");
        goto done;
    }
    if ( !MdoBackupPreviewStep(Job, Cancel, 1u) ) goto done;
    Job->Info.History.Size = sizeof(Job->Info.History);
    if ( !MdoSessionBackupCheckModelHistory(Job->Backup, &Job->Limits, Cancel,
            &Job->Info.History, &Job->Info.Error) ) goto done;
    if ( !MdoBackupPreviewStep(Job, Cancel, 2u) ) goto done;
    Job->Info.Images.Size = sizeof(Job->Info.Images);
    if ( !MdoSessionBackupCheckImages(Job->Backup, &Job->Limits, Cancel,
            &Job->Info.Images, &Job->Info.Error) ) goto done;
    Job->Succeeded = MdoBackupPreviewStep(Job, Cancel, MDO_BACKUP_PREVIEW_STEPS);
done:
    return Job->Succeeded ? XTASK_SUCCESS :
        (Job->Info.Error.eCode == XWORK_ERROR_CANCELLED ? XTASK_CANCELLED : XTASK_FAILED);
}

/* Task cancellation may skip Run entirely. Drop always consumes the pin and
 * unpublished decoded bytes, then publishes a terminal status. Cancellation
 * racing with success wins until that publication; the result is never made
 * available before all gates pass. Unit waits for this resident callback. */
static void MdoBackupPreviewDrop(ptr Data, ptr Context)
{
    MdoBackupPreviewJob* Job = (MdoBackupPreviewJob*)Data;
    MdoBackupPreviewStore* Store = Job->Store;
    bool Cancelled;
    (void)Context;
    MdoApiBackupUploadRelease(Job->Upload); Job->Upload = NULL;
    xrtMutexLock(Store->Lock);
    Cancelled = Store->Stopping || Store->Info.CancelRequested || xrtCancelRequested(Store->Cancel) ||
        Job->Info.Error.eCode == XWORK_ERROR_CANCELLED;
    if ( !Job->Succeeded || Cancelled ) {
        xrtMutexUnlock(Store->Lock);
        MdoSessionBackupRelease(Job->Backup); Job->Backup = NULL;
        xrtMutexLock(Store->Lock);
        /* Cancellation can arrive while unpublished bytes are being freed. */
        Cancelled = Cancelled || Store->Info.CancelRequested || Store->Stopping || xrtCancelRequested(Store->Cancel);
    }
    if ( Cancelled ) {
        Store->Info.CancelRequested = true;
        Store->Info.State = MDO_BACKUP_PREVIEW_CANCELLED;
        xworkErrorInit(&Store->Info.Error);
        Store->Info.Error.eCode = XWORK_ERROR_CANCELLED;
        snprintf(Store->Info.Error.sMessage, sizeof(Store->Info.Error.sMessage),
            "Backup inspection was cancelled");
    } else if ( Job->Succeeded ) {
        Job->Document->Backup = Job->Backup; Job->Backup = NULL;
        Store->Slot = Job->Document; Job->Document = NULL;
        Store->Info.State = MDO_BACKUP_PREVIEW_SUCCEEDED;
        Store->Info.Completed = MDO_BACKUP_PREVIEW_STEPS;
        Store->Info.Backup = Job->Info.Backup;
        Store->Info.History = Job->Info.History;
        Store->Info.Images = Job->Info.Images;
        Store->Info.ResultAvailable = true;
    } else {
        Store->Info.State = MDO_BACKUP_PREVIEW_FAILED;
        Store->Info.Error = Job->Info.Error;
        if ( Store->Info.Error.eCode == XWORK_ERROR_NONE ) {
            Store->Info.Error.eCode = XWORK_ERROR_IO;
            snprintf(Store->Info.Error.sMessage, sizeof(Store->Info.Error.sMessage),
                "Backup worker could not complete inspection");
        }
    }
    Store->Info.EndedAt = xrtNow();
    Store->Info.ExpiresAt = xrtDeadlineAfter(MDO_BACKUP_PREVIEW_TTL_US);
    Store->Active = NULL;
    xrtMutexUnlock(Store->Lock);
    xrtFree(Job->Document); xrtFree(Job);
}

bool MdoApiBackupPreviewsInit(void)
{
    MdoBackupPreviewStore* Store;
    if ( g_MdoBackupPreviews != NULL ) return true;
    Store = (MdoBackupPreviewStore*)xrtCalloc(1u, sizeof(*Store));
    if ( Store == NULL ) return false;
    Store->Lock = xrtMutexCreate();
    if ( Store->Lock == NULL ) { xrtFree(Store); return false; }
    Store->Users = 1u; g_MdoBackupPreviews = Store;
    return true;
}

void MdoApiBackupPreviewsUnit(void)
{
    MdoBackupPreviewStore* Store = g_MdoBackupPreviews;
    MdoBackupPreviewDocument* Retired;
    xtaskpool* Pool;
    xcancel* Cancel;
    bool Last;
    if ( Store == NULL ) return;
    g_MdoBackupPreviews = NULL;
    xrtMutexLock(Store->Lock); Store->Stopping = true;
    Store->Info.CancelRequested = true;
    if ( Store->Slot != NULL ) Store->Slot->Removed = true;
    Pool = Store->Pool; Cancel = xrtCancelRef(Store->Cancel);
    xrtMutexUnlock(Store->Lock);
    if ( Cancel != NULL ) { (void)xrtCancelRequest(Cancel); xrtCancelDestroy(Cancel); }
    if ( Pool != NULL ) {
        (void)xrtTaskPoolCancel(Pool); (void)xrtTaskPoolWait(Pool);
        (void)xrtTaskPoolDestroy(Pool);
    }
    xrtCancelDestroy(Store->Cancel);
    xrtMutexLock(Store->Lock); Retired = MdoBackupPreviewDetachLocked(Store);
    xrtMutexUnlock(Store->Lock);
    MdoBackupPreviewReleaseRetired(Retired);
    xrtMutexLock(Store->Lock); Last = --Store->Users == 0u;
    xrtMutexUnlock(Store->Lock);
    if ( Last ) MdoBackupPreviewStoreFree(Store);
}

MdoBackupPreviewDocument* MdoApiBackupPreviewAcquire(cstr Id,
    MdoBackupPreviewAccess* Access)
{
    MdoBackupPreviewStore* Store = g_MdoBackupPreviews;
    MdoBackupPreviewDocument *Document = NULL, *Retired = NULL;
    MdoBackupPreviewAccess Result = MDO_BACKUP_PREVIEW_ACCESS_UNAVAILABLE;
    if ( Id != NULL && Store != NULL ) {
        xrtMutexLock(Store->Lock); Retired = MdoBackupPreviewCollectLocked(Store);
        if ( Store->Stopping ) Result = MDO_BACKUP_PREVIEW_ACCESS_UNAVAILABLE;
        else if ( Store->Info.Id[0] == '\0' || strcmp(Store->Info.Id, Id) != 0 || Store->Info.Discarded )
            Result = MDO_BACKUP_PREVIEW_ACCESS_MISSING;
        else if ( Store->Slot == NULL || Store->Slot->Removed || !Store->Info.ResultAvailable )
            Result = MDO_BACKUP_PREVIEW_ACCESS_NOT_READY;
        else if ( Store->Slot->Pins != SIZE_MAX && Store->Users != SIZE_MAX ) {
            Document = Store->Slot; ++Document->Pins; ++Store->Users;
            Result = MDO_BACKUP_PREVIEW_ACCESS_OK;
        }
        xrtMutexUnlock(Store->Lock); MdoBackupPreviewReleaseRetired(Retired);
    }
    if ( Access != NULL ) *Access = Result;
    return Document;
}

const MdoSessionBackup* MdoApiBackupPreviewData(const MdoBackupPreviewDocument* Document)
{
    return Document != NULL ? Document->Backup : NULL;
}

bool MdoApiBackupPreviewHash(const MdoBackupPreviewDocument* Document, char Hash[65])
{
    if ( Document == NULL || Hash == NULL ) return false;
    memcpy(Hash, Document->Hash, 65u); return true;
}

void MdoApiBackupPreviewRelease(MdoBackupPreviewDocument* Document)
{
    MdoBackupPreviewStore* Store;
    MdoBackupPreviewDocument* Retired;
    bool Last;
    if ( Document == NULL ) return;
    Store = Document->Store;
    xrtMutexLock(Store->Lock); --Document->Pins;
    Retired = MdoBackupPreviewCollectLocked(Store);
    xrtMutexUnlock(Store->Lock);
    MdoBackupPreviewReleaseRetired(Retired);
    /* This acquisition still owns one Store user through out-of-lock cleanup. */
    xrtMutexLock(Store->Lock); Last = --Store->Users == 0u;
    xrtMutexUnlock(Store->Lock);
    if ( Last ) MdoBackupPreviewStoreFree(Store);
}

static bool MdoBackupPreviewNoBody(const MdoApiContext* Context)
{
    const xhttp1head* Head = Context->Request->head;
    return (Head->Flags & XHTTP1_TRANSFER_ENCODING) == 0u &&
        ((Head->Flags & XHTTP1_CONTENT_LENGTH) == 0u || Head->ContentLength == 0u);
}

static bool MdoBackupPreviewId(const MdoApiContext* Context, char Id[33])
{
    size_t i;
    if ( Context->ParamCount != 1u || Context->Params[0].Size != 32u ) return false;
    for ( i = 0u; i < 32u; ++i ) {
        char c = Context->Params[0].Data[i];
        if ( !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) ) return false;
    }
    memcpy(Id, Context->Params[0].Data, 32u); Id[32] = '\0'; return true;
}

static xvalue* MdoBackupPreviewValue(const MdoBackupPreviewInfo* Info)
{
    xvalue* Value = xrtValueObject();
    bool Terminal = MdoBackupPreviewTerminal(Info);
    uint64 Now = xrtClock();
    cstr Phase = Terminal ? "done" : Info->State == MDO_BACKUP_PREVIEW_PENDING ? "queued" :
        Info->Completed == 0u ? "decode" : Info->Completed == 1u ? "model_ui" : "images";
    bool Ok = Value != NULL && MdoApiValueSetString(Value, "id", Info->Id) &&
        MdoApiValueSetString(Value, "upload_id", Info->UploadId) &&
        MdoApiValueSetString(Value, "sha256", Info->UploadHash) &&
        MdoApiValueSetString(Value, "state", MdoBackupPreviewStateText(Info->State)) &&
        MdoApiValueSetString(Value, "phase", Phase) &&
        MdoApiValueSetUInt(Value, "completed_steps", Info->Completed) &&
        MdoApiValueSetUInt(Value, "total_steps", MDO_BACKUP_PREVIEW_STEPS) &&
        MdoApiValueSetInt(Value, "created_at", Info->CreatedAt) &&
        MdoApiValueSetInt(Value, "started_at", Info->StartedAt) &&
        MdoApiValueSetInt(Value, "ended_at", Info->EndedAt) &&
        MdoApiValueSetUInt(Value, "expires_in_ms", Info->ExpiresAt > Now ? (Info->ExpiresAt - Now) / 1000u : 0u) &&
        MdoApiValueSetBool(Value, "terminal", Terminal) &&
        MdoApiValueSetBool(Value, "cancel_requested", Info->CancelRequested) &&
        MdoApiValueSetBool(Value, "discarded", Info->Discarded) &&
        MdoApiValueSetBool(Value, "result_available", Info->ResultAvailable) &&
        MdoApiValueSetBool(Value, "restore_ready", false) &&
        MdoApiValueSetString(Value, "validation", Info->State == MDO_BACKUP_PREVIEW_SUCCEEDED ?
            "schema-model-ui-images" : "incomplete") &&
        MdoApiValueSetUInt(Value, "error_code", Info->Error.eCode) &&
        MdoApiValueSetString(Value, "message", Info->Error.sMessage);
    if ( Ok && Info->State == MDO_BACKUP_PREVIEW_SUCCEEDED ) {
        const MdoSessionBackupPreview* Backup = &Info->Backup;
        const MdoSessionInfo* Source = &Backup->Info;
        xvalue* Facts = xrtValueObject();
        Ok = Facts != NULL &&
            MdoApiValueSetUInt(Facts, "export_schema", Backup->ExportSchema) &&
            MdoApiValueSetBool(Facts, "legacy_partial", Backup->ExportSchema == 1u) &&
            MdoApiValueSetString(Facts, "session_id", Source->Id) &&
            MdoApiValueSetString(Facts, "project_id", Source->ProjectId) &&
            MdoApiValueSetString(Facts, "title", Source->Title) &&
            MdoApiValueSetString(Facts, "model_id", Source->ModelId) &&
            MdoApiValueSetString(Facts, "agent_id", Source->AgentId) &&
            MdoApiValueSetString(Facts, "workspace_root", Source->WorkspaceRoot) &&
            MdoApiValueSetString(Facts, "protocol", MdoModelProtocolName(Source->Protocol)) &&
            MdoApiValueSetInt(Facts, "captured_at", Backup->CapturedAt) &&
            MdoApiValueSetUInt(Facts, "file_count", Backup->Files) &&
            MdoApiValueSetUInt(Facts, "total_bytes", Backup->Bytes) &&
            MdoApiValueSetUInt(Facts, "ui_first_event_id", Backup->UiFirstEventId) &&
            MdoApiValueSetUInt(Facts, "ui_last_event_id", Backup->UiLastEventId) &&
            MdoApiValueSetUInt(Facts, "ui_records", Backup->UiRecords) &&
            MdoApiValueSetUInt(Facts, "unverified_history_references", Backup->UnverifiedHistoryReferences) &&
            MdoApiValueSetUInt(Facts, "removed_history_references", Backup->RemovedHistoryReferences) &&
            MdoApiValueSetUInt(Facts, "matched_ui_records", Info->History.MatchedUiRecords) &&
            MdoApiValueSetUInt(Facts, "unverified_ui_records", Info->History.UnverifiedUiRecords) &&
            MdoApiValueSetUInt(Facts, "unprojected_model_messages", Info->History.UnprojectedModelMessages) &&
            MdoApiValueSetUInt(Facts, "image_attachments", Info->Images.Attachments) &&
            MdoApiValueSetUInt(Facts, "inline_images", Info->Images.InlineImages) &&
            MdoApiValueSetUInt(Facts, "unverified_images", Info->Images.UnverifiedImages) &&
            MdoApiValueSetUInt(Facts, "rgba_bytes", Info->Images.RgbaBytes) &&
            MdoApiValueSetUInt(Facts, "peak_decoder_memory_bytes", Info->Images.PeakDecoderMemoryBytes);
        if ( Ok ) Ok = xrtValueObjectSetNew(Value, XRT_STR_LITERAL("result"), Facts);
        else xrtValueRelease(Facts);
    }
    if ( !Ok ) { xrtValueRelease(Value); return NULL; }
    return Value;
}

static bool MdoBackupPreviewError(MdoApiContext* Context, uint16 Status, cstr Code)
{
    return MdoApiReplyError(Context, Status, Code,
        Status == 404u ? "The preview or upload is missing, expired or discarded; upload or inspect again" :
        strcmp(Code, "backup_upload_incomplete") == 0 ? "Seal the complete upload before inspecting it" :
        Status == 409u ? "Cancel or discard the current backup inspection before starting another" :
        Status == 503u ? "Backup inspection resources are unavailable; retry later" :
        "Use a valid backup ID and an empty request body", NULL);
}

bool MdoApiBackupPreviewStartRoute(MdoApiContext* Context)
{
    MdoBackupPreviewStore* Store = g_MdoBackupPreviews;
    MdoBackupPreviewJob* Job = NULL;
    MdoBackupUploadDocument* Existing = NULL;
    MdoBackupPreviewDocument* Retired = NULL;
    MdoBackupPreviewInfo Info;
    MdoBackupUploadAccess Access;
    xfuture* Future = NULL;
    xtaskargs Args = {0};
    uint8 Random[16];
    static const char Hex[] = "0123456789abcdef";
    char Id[33];
    uint16 Status = 202u;
    cstr Code = NULL;
    size_t i;
    if ( !MdoBackupPreviewId(Context, Id) || !MdoBackupPreviewNoBody(Context) )
        return MdoBackupPreviewError(Context, 400u, "backup_preview_invalid");
    if ( Store == NULL ) return MdoBackupPreviewError(Context, 503u, "backup_preview_unavailable");
    xrtMutexLock(Store->Lock);
    Retired = MdoBackupPreviewCollectLocked(Store);
    if ( Retired != NULL ) {
        xrtMutexUnlock(Store->Lock); MdoBackupPreviewReleaseRetired(Retired); Retired = NULL;
        xrtMutexLock(Store->Lock);
    }
    if ( Store->Stopping ) { Status = 503u; Code = "backup_preview_unavailable"; goto unlock; }
    if ( Store->Releasing ) { Status = 409u; Code = "backup_preview_busy"; goto unlock; }
    if ( Store->Active != NULL || Store->Slot != NULL ) {
        if ( strcmp(Store->Info.UploadId, Id) == 0 && !Store->Info.CancelRequested && !Store->Info.Discarded ) {
            char Hash[65];
            Existing = MdoApiBackupUploadAcquire(Id, &Access);
            if ( Existing == NULL ) {
                Status = Access == MDO_BACKUP_UPLOAD_ACCESS_MISSING ? 404u :
                    Access == MDO_BACKUP_UPLOAD_ACCESS_INCOMPLETE ? 409u : 503u;
                Code = Access == MDO_BACKUP_UPLOAD_ACCESS_MISSING ? "backup_upload_not_found" :
                    Access == MDO_BACKUP_UPLOAD_ACCESS_INCOMPLETE ? "backup_upload_incomplete" : "backup_preview_unavailable";
            } else if ( MdoApiBackupUploadHash(Existing, Hash) && strcmp(Hash, Store->Info.UploadHash) == 0 ) {
                Info = Store->Info; Status = 200u;
            } else { Status = 409u; Code = "backup_preview_busy"; }
        } else { Status = 409u; Code = "backup_preview_busy"; }
        goto unlock;
    }
    Job = (MdoBackupPreviewJob*)xrtCalloc(1u, sizeof(*Job));
    if ( Job == NULL || !xrtSecureRandom(Random, sizeof(Random)) ) goto unavailable;
    Job->Store = Store;
    Job->Document = (MdoBackupPreviewDocument*)xrtCalloc(1u, sizeof(*Job->Document));
    if ( Job->Document == NULL ) goto unavailable;
    Job->Document->Store = Store;
    Job->Upload = MdoApiBackupUploadAcquire(Id, &Access);
    if ( Job->Upload == NULL ) {
        Status = Access == MDO_BACKUP_UPLOAD_ACCESS_MISSING ? 404u :
            Access == MDO_BACKUP_UPLOAD_ACCESS_INCOMPLETE ? 409u : 503u;
        Code = Access == MDO_BACKUP_UPLOAD_ACCESS_MISSING ? "backup_upload_not_found" :
            Access == MDO_BACKUP_UPLOAD_ACCESS_INCOMPLETE ? "backup_upload_incomplete" : "backup_preview_unavailable";
        goto unlock;
    }
    if ( Store->Pool == NULL ) {
        xtaskpoolconfig Config = {0};
        Config.Threads = 1u; Config.QueueLimit = 1u;
        Store->Pool = xrtTaskPoolCreate(&Config);
        if ( Store->Pool == NULL ) goto unavailable;
    }
    xrtCancelDestroy(Store->Cancel); Store->Cancel = xrtCancelCreate();
    if ( Store->Cancel == NULL ) goto unavailable;
    for ( i = 0u; i < sizeof(Random); ++i ) {
        Job->Info.Id[2u * i] = Hex[Random[i] >> 4u]; Job->Info.Id[2u * i + 1u] = Hex[Random[i] & 15u];
    }
    memcpy(Job->Info.UploadId, Id, sizeof(Id));
    (void)MdoApiBackupUploadHash(Job->Upload, Job->Info.UploadHash);
    memcpy(Job->Document->Hash, Job->Info.UploadHash, sizeof(Job->Document->Hash));
    Job->Info.CreatedAt = xrtNow(); Job->Info.State = MDO_BACKUP_PREVIEW_PENDING;
    MdoSessionBackupLimitsInit(&Job->Limits);
    Job->Limits.Deadline = xrtDeadlineAfter(MDO_BACKUP_PREVIEW_TIMEOUT_US);
    Job->Info.ExpiresAt = Job->Limits.Deadline;
    xworkErrorInit(&Job->Info.Error);
    Args.Cancel = Store->Cancel; Args.Destroy = MdoBackupPreviewDrop;
    /* A pre-cancelled task can run Drop inline inside Submit. Publish its
     * ownership first, then submit OUTSIDE the non-recursive status mutex.
     * Active reserves admission and keeps Pool/Cancel stable. The host drains
     * request callbacks before Unit, so Pool cannot retire during this call. */
    Store->Active = Job; Store->Info = Job->Info; Info = Store->Info;
    xrtMutexUnlock(Store->Lock);
    Future = xrtTaskSubmit(Store->Pool, MdoBackupPreviewRun, Job, &Args);
    if ( Future == NULL ) {
        /* Rejection does not consume Data. Use the same cleanup/publication
         * path, preserving a terminal receipt for concurrent POST retries. */
        Job->Info.Error.eCode = XWORK_ERROR_IO;
        snprintf(Job->Info.Error.sMessage, sizeof(Job->Info.Error.sMessage),
            "Backup worker submission was refused");
        MdoBackupPreviewDrop(Job, NULL);
        Status = 503u; Code = "backup_preview_unavailable";
    }
    /* Successful submission may already have consumed and freed Job. */
    Job = NULL;
    xrtMutexLock(Store->Lock);
    if ( strcmp(Store->Info.Id, Info.Id) == 0 ) Info = Store->Info;
    goto unlock;
unavailable:
    Status = 503u; Code = "backup_preview_unavailable";
unlock:
    xrtMutexUnlock(Store->Lock);
    MdoBackupPreviewReleaseRetired(Retired);
    MdoApiBackupUploadRelease(Existing);
    if ( Job != NULL ) { MdoApiBackupUploadRelease(Job->Upload); xrtFree(Job->Document); xrtFree(Job); }
    xrtFutureDestroy(Future);
    return Code != NULL ? MdoBackupPreviewError(Context, Status, Code) :
        MdoApiReplySuccessTake(Context, Status, MdoBackupPreviewValue(&Info), NULL);
}

/* DELETE cancels an active job cooperatively or discards a retained payload.
 * Keep the small terminal receipt visible until replacement/expiry so a page
 * can observe cancellation and diagnose an error. No new job is accepted
 * until the old task's Drop has finished cleaning up. */
bool MdoApiBackupPreviewRoute(MdoApiContext* Context)
{
    MdoBackupPreviewStore* Store = g_MdoBackupPreviews;
    MdoBackupPreviewInfo Info;
    MdoBackupPreviewDocument *Retired, *Discarded = NULL;
    xcancel* Cancel = NULL;
    char Id[33];
    bool Found;
    if ( !MdoBackupPreviewId(Context, Id) || !MdoBackupPreviewNoBody(Context) )
        return MdoBackupPreviewError(Context, 400u, "backup_preview_invalid");
    if ( Store == NULL ) return MdoBackupPreviewError(Context, 503u, "backup_preview_unavailable");
    xrtMutexLock(Store->Lock); Retired = MdoBackupPreviewCollectLocked(Store);
    Found = strcmp(Store->Info.Id, Id) == 0;
    if ( Found && Context->Request->head->MethodCode == XHTTP_METHOD_DELETE ) {
        Store->Info.Discarded = true; Store->Info.ResultAvailable = false;
        if ( Store->Active != NULL ) {
            Store->Info.CancelRequested = true; Cancel = xrtCancelRef(Store->Cancel);
        } else {
            if ( Store->Slot != NULL ) Store->Slot->Removed = true;
            Discarded = MdoBackupPreviewDetachLocked(Store);
        }
    }
    Info = Store->Info;
    xrtMutexUnlock(Store->Lock);
    if ( Cancel != NULL ) { (void)xrtCancelRequest(Cancel); xrtCancelDestroy(Cancel); }
    MdoBackupPreviewReleaseRetired(Retired); MdoBackupPreviewReleaseRetired(Discarded);
    return !Found ? MdoBackupPreviewError(Context, 404u, "backup_preview_not_found") :
        MdoApiReplySuccessTake(Context, 200u, MdoBackupPreviewValue(&Info), NULL);
}

bool MdoApiBackupPreviewsRoute(MdoApiContext* Context)
{
    MdoBackupPreviewStore* Store = g_MdoBackupPreviews;
    MdoBackupPreviewInfo Info;
    MdoBackupPreviewDocument* Retired;
    xvalue* Value;
    bool Ok;
    if ( !MdoBackupPreviewNoBody(Context) ) return MdoBackupPreviewError(Context, 400u, "backup_preview_invalid");
    if ( Store == NULL ) return MdoBackupPreviewError(Context, 503u, "backup_preview_unavailable");
    xrtMutexLock(Store->Lock); Retired = MdoBackupPreviewCollectLocked(Store); Info = Store->Info;
    xrtMutexUnlock(Store->Lock); MdoBackupPreviewReleaseRetired(Retired);
    Value = xrtValueObject();
    Ok = Value != NULL && MdoApiValueSetUInt(Value, "workers", 1u) &&
        MdoApiValueSetUInt(Value, "timeout_ms", MDO_BACKUP_PREVIEW_TIMEOUT_US / 1000u) &&
        MdoApiValueSetUInt(Value, "result_ttl_ms", MDO_BACKUP_PREVIEW_TTL_US / 1000u) &&
        xrtValueObjectSetNew(Value, XRT_STR_LITERAL("preview"),
            Info.Id[0] != '\0' ? MdoBackupPreviewValue(&Info) : xrtValueNull());
    if ( !Ok ) { xrtValueRelease(Value); Value = NULL; }
    return MdoApiReplySuccessTake(Context, 200u, Value, NULL);
}
