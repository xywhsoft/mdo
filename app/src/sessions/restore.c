#include <string.h>
#include "../../include/mdo/session_restore.h"
#include "../../include/mdo/home.h"
#include "internal.h"
#include "backup_internal.h"

typedef struct MdoRestorePublication {
    MdoSessionRestoreReservation* Reservation;
    MdoSessionBackupStageInfo Stage;
    const MdoSessionBackupLimits* Limits;
    const xcancel* Cancel;
    bool Committed;
    uint64 Generation;
} MdoRestorePublication;

struct MdoSessionRestoreOperation {
    const MdoSessionBackup* Backup; /* scheduler retains its immutable owner */
    MdoSessionRestoreRequest Request;
    MdoSessionBackupLimits Budget;
    MdoProjectLease* Owner;
    MdoSessionRestoreReservation* Reservation;
    xroot Parent;
    MdoSessionRestoreResult Result;
    bool Started;
};

void MdoSessionRestoreRequestInit(MdoSessionRestoreRequest* Request)
{
    if ( Request == NULL ) return;
    memset(Request, 0, sizeof(*Request)); Request->Size = sizeof(*Request);
    Request->Binding.Size = sizeof(Request->Binding);
}

void MdoSessionRestoreResultInit(MdoSessionRestoreResult* Result)
{
    if ( Result == NULL ) return;
    memset(Result, 0, sizeof(*Result)); Result->Size = sizeof(*Result);
}

static bool MdoRestorePublicationCommit(const MdoProjectBinding* Current, void* Data, xwork_error* Error)
{
    MdoRestorePublication* Publication = Data;
    (void)Current;
    if ( !MdoBackupCheck(Publication->Limits, Publication->Cancel, Error) ) return false;
    return MdoSessionsRestorePublish(Publication->Reservation, Publication->Stage.DirectoryName,
        &Publication->Stage.DirectoryIdentity, &Publication->Committed, &Publication->Generation, Error);
}

/* Preserve independently proven commit facts, even when consuming storage or
 * closing anchors fails. A retired manager cannot consume live storage: retain
 * the operation rather than free the only diagnostic ownership handle. */
static bool MdoRestoreConsume(MdoSessionRestoreOperation** Value,
    MdoSessionRestoreResult* Result, xwork_error* Error)
{
    MdoSessionRestoreOperation* Operation = *Value;
    MdoHomeSnapshot Home = {0};
    xwork_error Cleanup;
    bool Ok = true;
    if ( Operation->Parent != NULL ) {
        if ( !xrtRootClose(Operation->Parent) ) {
            if ( Error->eCode == XWORK_ERROR_NONE )
                (void)MdoBackupError(Error, XWORK_ERROR_IO, "cannot close restore staging parent", NULL);
            (void)MdoHomeRequireRestart("session restore staging parent close failed");
            Ok = false;
        }
        Operation->Parent = NULL;
    }
    if ( !MdoSessionsRestoreRelease(&Operation->Reservation, &Cleanup) ) {
        if ( Error->eCode == XWORK_ERROR_NONE ) *Error = Cleanup;
        Ok = false;
    }
    Home.Size = sizeof(Home);
    if ( MdoHomeGetSnapshot(&Home) ) Operation->Result.RestartRequired = Home.RestartRequired;
    *Result = Operation->Result;
    if ( Operation->Reservation == NULL ) {
        MdoProjectLeaseRelease(Operation->Owner);
        *Value = NULL; xrtFree(Operation);
    }
    return Ok;
}

static MdoSessionRestoreOperation* MdoRestoreCreate(const MdoSessionBackup* Backup,
    const MdoSessionRestoreRequest* Request, const MdoSessionBackupLimits* Limits,
    const xcancel* Cancel, xwork_error* Error)
{
    MdoSessionRestoreOperation* Operation;
    MdoProjectBinding Current = {0};
    MdoSessionRestoreResult Ignored;
    if ( Backup == NULL || Request == NULL || Request->Size != sizeof(*Request) ||
         Request->RestoredAt <= 0 || Request->Binding.Size != sizeof(Request->Binding) ||
         memchr(Request->SessionId, '\0', sizeof(Request->SessionId)) != Request->SessionId + 32u ||
         !MdoBackupDigits(Request->SessionId, 32u, true) ||
         !MdoProjectBindingMatches(&Request->Binding, &Request->Binding) ) {
        (void)MdoBackupError(Error, XWORK_ERROR_INVALID_ARGUMENT, "invalid reviewed session restore target", NULL);
        return NULL;
    }
    Operation = xrtCalloc(1u, sizeof(*Operation));
    if ( Operation == NULL ) {
        (void)MdoBackupError(Error, XWORK_ERROR_OUT_OF_MEMORY, "cannot own session restore operation", NULL);
        return NULL;
    }
    Operation->Backup = Backup; Operation->Request = *Request;
    MdoSessionRestoreResultInit(&Operation->Result);
    if ( !MdoBackupLimits(Limits, &Operation->Budget, 30000000u, Error) ||
         !MdoBackupCheck(&Operation->Budget, Cancel, Error) ) goto failed;
    Operation->Owner = MdoProjectLeaseAcquire(Request->Binding.ProjectId, MDO_PROJECT_LEASE_SHARED, Error);
    if ( Operation->Owner == NULL ) goto failed;
    Current.Size = sizeof(Current);
    if ( !MdoProjectBindingGet(Request->Binding.ProjectId, &Current, Error) ) goto failed;
    if ( !MdoProjectBindingMatches(&Request->Binding, &Current) ) {
        (void)MdoBackupError(Error, XWORK_ERROR_CONTEXT, "target project or workspace changed after review", NULL);
        goto failed;
    }
    Operation->Reservation = MdoSessionsRestoreReserve(Request->Binding.ProjectId,
        Request->SessionId, Operation->Owner, Error);
    if ( Operation->Reservation == NULL ) goto failed;
    return Operation;
failed:
    (void)MdoRestoreConsume(&Operation, &Ignored, Error);
    return NULL;
}

MdoSessionRestoreOperation* MdoSessionRestoreAccept(const MdoSessionBackup* Backup,
    const MdoSessionRestoreRequest* Request, cstr SourceSha256,
    const MdoSessionBackupLimits* Limits, const xcancel* Cancel, xwork_error* Error)
{
    MdoSessionRestoreOperation* Operation;
    MdoHomeSessionRestoreRequest Storage = {0};
    MdoSessionRestoreResult Ignored;
    xwork_error Local;
    if ( Error == NULL ) Error = &Local;
    xworkErrorInit(Error);
    /* No data copy, schema traversal or semantic replay on admission. The
     * trusted decoded owner is immutable for this operation's whole lifetime. */
    if ( Backup == NULL || !Backup->Decoded || Backup->Schema != MDO_SESSION_BACKUP_SCHEMA ||
         SourceSha256 == NULL || strlen(SourceSha256) != 64u || !MdoBackupDigits(SourceSha256, 64u, true) ) {
        (void)MdoBackupError(Error, XWORK_ERROR_INVALID_ARGUMENT, "restore requires a decoded v2 backup and its transport digest", NULL);
        return NULL;
    }
    Operation = MdoRestoreCreate(Backup, Request, Limits, Cancel, Error);
    if ( Operation == NULL ) return NULL;
    if ( Backup->Count > Operation->Budget.Files || Backup->Bytes > Operation->Budget.TotalBytes ) {
        (void)MdoBackupError(Error, XWORK_ERROR_LIMIT, "accepted backup exceeds restore budgets", NULL);
        goto failed;
    }
    Storage.Size = sizeof(Storage);
    snprintf(Storage.ProjectId, sizeof(Storage.ProjectId), "%s", Request->Binding.ProjectId);
    snprintf(Storage.SessionId, sizeof(Storage.SessionId), "%s", Request->SessionId);
    snprintf(Storage.SourceSessionId, sizeof(Storage.SourceSessionId), "%s", Backup->Info.Id);
    memcpy(Storage.SourceSha256, SourceSha256, sizeof(Storage.SourceSha256));
    Storage.ProjectRevision = Request->Binding.Revision; Storage.ProjectCreatedAt = Request->Binding.CreatedAt;
    Storage.RestoredAt = Request->RestoredAt;
    if ( !MdoBackupCheck(&Operation->Budget, Cancel, Error) ||
         !MdoSessionsRestoreStorageBeginRequested(Operation->Reservation, &Storage, &Operation->Parent, Error) ) goto failed;
    return Operation;
failed:
    (void)MdoRestoreConsume(&Operation, &Ignored, Error);
    return NULL;
}

bool MdoSessionRestoreDiscard(MdoSessionRestoreOperation** Operation,
    MdoSessionRestoreResult* Result, xwork_error* Error)
{
    xwork_error Local;
    if ( Error == NULL ) Error = &Local;
    xworkErrorInit(Error);
    if ( Operation == NULL || Result == NULL || Result->Size != sizeof(*Result) )
        return MdoBackupError(Error, XWORK_ERROR_INVALID_ARGUMENT, "invalid restore discard output", NULL);
    MdoSessionRestoreResultInit(Result);
    if ( *Operation == NULL ) return true;
    (*Operation)->Started = true;
    return MdoRestoreConsume(Operation, Result, Error);
}

bool MdoSessionRestoreExecute(MdoSessionRestoreOperation** Value,
    const xcancel* Cancel, MdoSessionRestoreResult* Result, xwork_error* Error)
{
    MdoSessionRestoreOperation* Operation;
    MdoSessionRestoreRequest Owned;
    MdoSessionBackupLimits Budget;
    MdoProjectBinding Current = {0};
    MdoSessionRestoreReservation* Reservation;
    MdoSessionBackup* Prepared = NULL;
    MdoSessionBackupStage* Stage = NULL;
    MdoSessionBackupRestoreTarget Target = {0};
    MdoSessionBackupRestoreInfo Facts = {0};
    MdoRestorePublication Publication = {0};
    xwork_error Local, Cleanup;
    bool Ok = false;
    if ( Error == NULL ) Error = &Local;
    xworkErrorInit(Error);
    if ( Value == NULL || *Value == NULL || Result == NULL || Result->Size != sizeof(*Result) )
        return MdoBackupError(Error, XWORK_ERROR_INVALID_ARGUMENT, "invalid restore result output", NULL);
    MdoSessionRestoreResultInit(Result);
    Operation = *Value;
    if ( Operation->Started )
        return MdoBackupError(Error, XWORK_ERROR_CONTEXT, "restore operation was already executed or discarded", NULL);
    Operation->Started = true;
    Owned = Operation->Request; Budget = Operation->Budget; Reservation = Operation->Reservation;
    if ( !MdoBackupCheck(&Budget, Cancel, Error) ) goto done;
    Current.Size = sizeof(Current);
    if ( !MdoProjectBindingGet(Owned.Binding.ProjectId, &Current, Error) ) goto done;
    if ( !MdoProjectBindingMatches(&Owned.Binding, &Current) ) {
        (void)MdoBackupError(Error, XWORK_ERROR_CONTEXT, "target project or workspace changed after review", NULL); goto done;
    }
    Target.Size = sizeof(Target); Target.ProjectId = Owned.Binding.ProjectId; Target.SessionId = Owned.SessionId;
    Target.WorkspaceRoot = Owned.Binding.WorkspaceRoot; Target.RestoredAt = Owned.RestoredAt;
    Facts.Size = sizeof(Facts);
    Prepared = MdoSessionBackupPrepareRestore(Operation->Backup, &Target, &Budget, Cancel, &Facts, Error);
    if ( Prepared == NULL || !MdoBackupCheck(&Budget, Cancel, Error) ||
         (Operation->Parent == NULL && !MdoSessionsRestoreStorageBegin(Reservation, &Operation->Parent, Error)) ) goto done;
    Ok = MdoSessionBackupStagePrepare(Prepared, Operation->Parent, &Budget, Cancel, &Stage, Error);
    MdoSessionBackupRelease(Prepared); Prepared = NULL;
    if ( !xrtRootClose(Operation->Parent) ) {
        Ok = MdoBackupError(Error, XWORK_ERROR_IO, "cannot close restore staging parent", NULL);
        (void)MdoHomeRequireRestart("session restore staging parent close failed");
    }
    Operation->Parent = NULL;
    if ( !Ok || !MdoSessionBackupStageCheck(Stage, &Budget, Cancel, Error) ) { Ok = false; goto done; }
    Publication.Stage.Size = sizeof(Publication.Stage);
    if ( !MdoSessionBackupStageInfoGet(Stage, &Publication.Stage) || !Publication.Stage.Verified ) {
        Ok = MdoBackupError(Error, XWORK_ERROR_CONTEXT, "restore Stage verification was revoked", NULL); goto done;
    }
    if ( !MdoSessionBackupStageRelease(&Stage, Error) ) { Ok = false; goto done; }
    Publication.Reservation = Reservation; Publication.Limits = &Budget; Publication.Cancel = Cancel;
    Ok = MdoProjectWithBinding(&Owned.Binding, Operation->Owner, MdoRestorePublicationCommit, &Publication, Error);
    if ( Publication.Committed && !Ok )
        (void)MdoHomeRequireRestart("committed session restore requires restart after publication cleanup/close failure");
done:
    MdoSessionBackupRelease(Prepared);
    if ( Stage != NULL && !MdoSessionBackupStageRelease(&Stage, &Cleanup) ) {
        if ( Error->eCode == XWORK_ERROR_NONE ) *Error = Cleanup;
        Ok = false;
    }
    Operation->Result.Committed = Publication.Committed;
    if ( Publication.Committed ) {
        Operation->Result.CatalogGeneration = Publication.Generation;
        Operation->Result.Restore = Facts; Operation->Result.Stage = Publication.Stage;
    }
    if ( !MdoRestoreConsume(Value, Result, Error) ) Ok = false;
    return Ok;
}

bool MdoSessionRestorePublish(const MdoSessionBackup* Backup,
    const MdoSessionRestoreRequest* Request, const MdoSessionBackupLimits* Limits,
    const xcancel* Cancel, MdoSessionRestoreResult* Result, xwork_error* Error)
{
    MdoSessionRestoreOperation* Operation;
    xwork_error Local;
    if ( Error == NULL ) Error = &Local;
    xworkErrorInit(Error);
    if ( Result == NULL || Result->Size != sizeof(*Result) )
        return MdoBackupError(Error, XWORK_ERROR_INVALID_ARGUMENT, "invalid restore result output", NULL);
    MdoSessionRestoreResultInit(Result);
    Operation = MdoRestoreCreate(Backup, Request, Limits, Cancel, Error);
    if ( Operation == NULL ) return false;
    return MdoSessionRestoreExecute(&Operation, Cancel, Result, Error);
}
