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

bool MdoSessionRestorePublish(const MdoSessionBackup* Backup,
    const MdoSessionRestoreRequest* Request, const MdoSessionBackupLimits* Limits,
    const xcancel* Cancel, MdoSessionRestoreResult* Result, xwork_error* Error)
{
    MdoSessionRestoreRequest Owned;
    MdoSessionBackupLimits Budget;
    MdoProjectBinding Current = {0};
    MdoHomeSnapshot Home = {0};
    MdoProjectLease* Owner = NULL;
    MdoSessionRestoreReservation* Reservation = NULL;
    MdoSessionBackup* Prepared = NULL;
    MdoSessionBackupStage* Stage = NULL;
    MdoSessionBackupRestoreTarget Target = {0};
    MdoSessionBackupRestoreInfo Facts = {0};
    MdoRestorePublication Publication = {0};
    xwork_error Local, Cleanup;
    xroot Parent = NULL;
    bool Ok = false, Released;
    if ( Error == NULL ) Error = &Local;
    xworkErrorInit(Error);
    if ( Result == NULL || Result->Size != sizeof(*Result) )
        return MdoBackupError(Error, XWORK_ERROR_INVALID_ARGUMENT, "invalid restore result output", NULL);
    MdoSessionRestoreResultInit(Result);
    if ( Backup == NULL || Request == NULL || Request->Size != sizeof(*Request) ||
         Request->RestoredAt <= 0 || Request->Binding.Size != sizeof(Request->Binding) ||
         memchr(Request->SessionId, '\0', sizeof(Request->SessionId)) != Request->SessionId + 32u ||
         !MdoBackupDigits(Request->SessionId, 32u, true) ||
         !MdoProjectBindingMatches(&Request->Binding, &Request->Binding) )
        return MdoBackupError(Error, XWORK_ERROR_INVALID_ARGUMENT, "invalid reviewed session restore target", NULL);
    Owned = *Request;
    if ( !MdoBackupLimits(Limits, &Budget, 30000000u, Error) || !MdoBackupCheck(&Budget, Cancel, Error) ) goto done;
    Owner = MdoProjectLeaseAcquire(Owned.Binding.ProjectId, MDO_PROJECT_LEASE_SHARED, Error);
    if ( Owner == NULL ) goto done;
    Current.Size = sizeof(Current);
    if ( !MdoProjectBindingGet(Owned.Binding.ProjectId, &Current, Error) ) goto done;
    if ( !MdoProjectBindingMatches(&Owned.Binding, &Current) ) {
        (void)MdoBackupError(Error, XWORK_ERROR_CONTEXT, "target project or workspace changed after review", NULL); goto done;
    }
    Reservation = MdoSessionsRestoreReserve(Owned.Binding.ProjectId, Owned.SessionId, Owner, Error);
    if ( Reservation == NULL ) goto done;
    Target.Size = sizeof(Target); Target.ProjectId = Owned.Binding.ProjectId; Target.SessionId = Owned.SessionId;
    Target.WorkspaceRoot = Owned.Binding.WorkspaceRoot; Target.RestoredAt = Owned.RestoredAt;
    Facts.Size = sizeof(Facts);
    Prepared = MdoSessionBackupPrepareRestore(Backup, &Target, &Budget, Cancel, &Facts, Error);
    if ( Prepared == NULL || !MdoBackupCheck(&Budget, Cancel, Error) ||
         !MdoSessionsRestoreStorageBegin(Reservation, &Parent, Error) ) goto done;
    Ok = MdoSessionBackupStagePrepare(Prepared, Parent, &Budget, Cancel, &Stage, Error);
    MdoSessionBackupRelease(Prepared); Prepared = NULL;
    if ( !xrtRootClose(Parent) ) Ok = MdoBackupError(Error, XWORK_ERROR_IO, "cannot close restore staging parent", NULL);
    Parent = NULL;
    if ( !Ok || !MdoSessionBackupStageCheck(Stage, &Budget, Cancel, Error) ) { Ok = false; goto done; }
    Publication.Stage.Size = sizeof(Publication.Stage);
    if ( !MdoSessionBackupStageInfoGet(Stage, &Publication.Stage) || !Publication.Stage.Verified ) {
        Ok = MdoBackupError(Error, XWORK_ERROR_CONTEXT, "restore Stage verification was revoked", NULL); goto done;
    }
    if ( !MdoSessionBackupStageRelease(&Stage, Error) ) { Ok = false; goto done; }
    Publication.Reservation = Reservation; Publication.Limits = &Budget; Publication.Cancel = Cancel;
    Ok = MdoProjectWithBinding(&Owned.Binding, Owner, MdoRestorePublicationCommit, &Publication, Error);
    if ( Publication.Committed && !Ok )
        (void)MdoHomeRequireRestart("committed session restore requires restart after publication cleanup/close failure");
done:
    MdoSessionBackupRelease(Prepared);
    if ( Parent != NULL && !xrtRootClose(Parent) ) {
        if ( Error->eCode == XWORK_ERROR_NONE )
            (void)MdoBackupError(Error, XWORK_ERROR_IO, "cannot close failed restore staging parent", NULL);
        Ok = false;
    }
    if ( Stage != NULL && !MdoSessionBackupStageRelease(&Stage, &Cleanup) ) {
        if ( Error->eCode == XWORK_ERROR_NONE ) *Error = Cleanup;
        Ok = false;
    }
    Released = MdoSessionsRestoreRelease(&Reservation, &Cleanup);
    if ( !Released ) { if ( Error->eCode == XWORK_ERROR_NONE ) *Error = Cleanup; Ok = false; }
    MdoProjectLeaseRelease(Owner);
    Result->Committed = Publication.Committed;
    Home.Size = sizeof(Home);
    if ( MdoHomeGetSnapshot(&Home) ) Result->RestartRequired = Home.RestartRequired;
    if ( Publication.Committed ) {
        Result->CatalogGeneration = Publication.Generation;
        Result->Restore = Facts; Result->Stage = Publication.Stage;
    }
    return Ok;
}
