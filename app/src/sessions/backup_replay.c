#include <xllm-session.h>

#include "backup_internal.h"

/* Decode preserves schema-valid historical data even when its model semantics
 * cannot replay. Keep this explicit operation separate so inspection does not
 * destroy that evidence. The library owns the entire unpublished replay. */
xllm_session* MdoSessionBackupReplayModel(const MdoSessionBackup* Backup,
    const MdoSessionBackupLimits* Limits, const xcancel* Cancel, xwork_error* Error)
{
    MdoSessionBackupLimits Budget;
    const MdoBackupOwnedFile *Snapshot, *Journal;
    xllm_session_restore_options Options;
    xllm_error Cause;
    xllm_session* Session;
    size_t i;
    if ( Error != NULL ) xworkErrorInit(Error);
    if ( Backup == NULL || !Backup->Decoded ) {
        (void)MdoBackupError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "model replay requires a decoded session backup", NULL);
        return NULL;
    }
    if ( !MdoBackupLimits(Limits, &Budget, 30000000u, Error) ||
         !MdoBackupCheck(&Budget, Cancel, Error) ) return NULL;
    if ( Backup->Count > Budget.Files || Backup->Bytes > Budget.TotalBytes ) goto limit;
    for ( i = 0u; i < Backup->Count; ++i ) if ( Backup->Files[i].Bytes > Budget.FileBytes ) goto limit;
    Snapshot = MdoBackupFind(Backup, "snapshot.json");
    Journal = MdoBackupFind(Backup, "journal.jsonl");
    if ( Snapshot == NULL || Snapshot->Bytes == 0u ) {
        (void)MdoBackupError(Error, XWORK_ERROR_IO, "model snapshot is missing", NULL);
        return NULL;
    }
    xllmSessionRestoreOptionsInit(&Options);
    Options.iMaxSnapshotBytes = Budget.FileBytes;
    Options.iMaxJournalBytes = Budget.FileBytes;
    Options.iMaxValues = MDO_BACKUP_JSON_VALUES;
    Options.uMaxDepth = 32u;
    Options.uDeadline = Budget.Deadline; Options.pCancel = Cancel;
    Session = xllmSessionRestore(Snapshot->Data, Snapshot->Bytes,
        Journal != NULL ? Journal->Data : NULL, Journal != NULL ? Journal->Bytes : 0u,
        &Options, &Cause);
    if ( Session == NULL ) {
        xwork_error_code Code = XWORK_ERROR_IO;
        if ( Cause.eCode == XLLM_ERROR_CANCELLED ) Code = XWORK_ERROR_CANCELLED;
        else if ( Cause.eCode == XLLM_ERROR_TIMEOUT || Cause.eCode == XLLM_ERROR_LIMIT ) Code = XWORK_ERROR_LIMIT;
        else if ( Cause.eCode == XLLM_ERROR_OUT_OF_MEMORY ) Code = XWORK_ERROR_OUT_OF_MEMORY;
        (void)MdoBackupError(Error, Code, "cannot replay model context", Cause.sMessage);
    }
    return Session;
limit:
    (void)MdoBackupError(Error, XWORK_ERROR_LIMIT, "decoded backup exceeds model replay budget", NULL);
    return NULL;
}
