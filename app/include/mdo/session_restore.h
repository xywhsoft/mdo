#ifndef MDO_SESSION_RESTORE_H
#define MDO_SESSION_RESTORE_H

#include "session_backup.h"
#include "project_binding.h"

typedef struct MdoSessionRestoreRequest {
    uint32 Size;
    MdoProjectBinding Binding; /* explicit reviewed project/workspace snapshot */
    char SessionId[MDO_SESSION_ID_CAPACITY]; /* fresh 32 lowercase hex, reused for result recovery */
    int64 RestoredAt; /* positive UTC microseconds */
} MdoSessionRestoreRequest;

typedef struct MdoSessionRestoreResult {
    uint32 Size;
    bool Committed;
    bool RestartRequired;
    uint64 CatalogGeneration;
    MdoSessionBackupRestoreInfo Restore;
    MdoSessionBackupStageInfo Stage;
} MdoSessionRestoreResult;

void MdoSessionRestoreRequestInit(MdoSessionRestoreRequest* Request);
void MdoSessionRestoreResultInit(MdoSessionRestoreResult* Result);

/* Synchronous production coordinator for a bounded worker, never a network
 * callback. Borrows immutable decoded v2 bytes and cancellation only until
 * return. Copies request/facts. One thirty-second cooperative budget covers
 * preparation, disk Stage/model/UI/pixels, repeat verification and the final
 * project binding callback; native calls cannot be preempted. The host stops
 * admission and drains callers before manager/Home Unit.
 *
 * Reserves identity/data, prepares new identity/provenance and reviewed input,
 * uses the durable Home journal for Stage ownership, closes all anchors, then
 * repeats project/workspace/version checks and atomically publishes without
 * replacement plus one catalog advance. No Agent/driver/live queue is opened.
 * Unknown source profiles remain descriptive until the live runtime checks
 * them. Nothing is automatically run by restoring draft/queue content.
 *
 * Result.Size mismatch leaves it untouched. Ordinary failure clears facts;
 * Committed survives any later cleanup/close error and includes verified
 * facts/catalog generation. bool false is never permission to mint a new ID:
 * preserve/query this same target after ambiguous failure/startup recovery.
 * Cleanup failures freeze Home; an already committed session is not deleted.
 * This is not upload ownership, a durable HTTP request receipt, user approval,
 * an asynchronous worker API, or a claim of power-loss directory durability. */
bool MdoSessionRestorePublish(const MdoSessionBackup* Backup,
    const MdoSessionRestoreRequest* Request, const MdoSessionBackupLimits* Limits,
    const xcancel* Cancel, MdoSessionRestoreResult* Result, xwork_error* Error);

#endif
