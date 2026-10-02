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

typedef struct MdoSessionRestoreOperation MdoSessionRestoreOperation;

/* Accept a reviewed v2 request before scheduling its expensive work. Copies
 * request/budgets, pins the project and reserves the target, then flushes Home's
 * immutable requested-owner record before returning. SourceSha256 is the
 * transport digest associated with Backup by its trusted owner (e.g. a preview
 * pin), not a caller-supplied replacement for decoding/semantic validation.
 * This bounded storage admission does not replay a model or decode images.
 *
 * Backup is borrowed through consumption of the operation. The scheduler must
 * retain its immutable owner, and must Execute or Discard even if Run is skipped.
 * The thirty-second budget starts at acceptance and includes queue waiting.
 * A failed acceptance can still leave recorded aborted/pending evidence: query
 * the SAME SessionId before any further action; never silently mint a new ID.
 * Acceptance is not user confirmation, upload ownership or an HTTP response. */
MdoSessionRestoreOperation* MdoSessionRestoreAccept(const MdoSessionBackup* Backup,
    const MdoSessionRestoreRequest* Request, cstr SourceSha256,
    const MdoSessionBackupLimits* Limits, const xcancel* Cancel, xwork_error* Error);

/* One exclusive owner calls these; no concurrent Execute/Discard on a handle.
 * Execute belongs on a bounded worker and repeats the reviewed binding before
 * preparation and in the final publication callback. Discard settles an
 * accepted-but-skipped task without staging or publishing it. Both consume
 * *Operation, including normal cancellation/IO failures, and report commit
 * independently of success. Discard(NULL handle) succeeds with empty facts.
 * Invalid arguments/Result.Size leave the operation untouched. A lifecycle
 * violation (manager retired while storage is live) retains the handle for
 * diagnosis; it cannot Execute again. Drain operations before manager/Home/TCC
 * Unit. Discard of an executed handle only retries cleanup, never publication. */
bool MdoSessionRestoreExecute(MdoSessionRestoreOperation** Operation,
    const xcancel* Cancel, MdoSessionRestoreResult* Result, xwork_error* Error);
bool MdoSessionRestoreDiscard(MdoSessionRestoreOperation** Operation,
    MdoSessionRestoreResult* Result, xwork_error* Error);

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
