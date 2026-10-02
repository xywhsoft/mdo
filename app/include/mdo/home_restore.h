#ifndef MDO_HOME_RESTORE_H
#define MDO_HOME_RESTORE_H

#include "home.h"

typedef struct MdoHomeSessionRestore MdoHomeSessionRestore;

/* Storage receipt identity. SessionId is both the fresh target and immutable
 * request ID; never mint another after a lost reply. Project revision/incarnation
 * and source SHA describe the reviewed request, not storage authorization. */
typedef struct MdoHomeSessionRestoreRequest {
    uint32 Size;
    char ProjectId[65], SessionId[33], SourceSessionId[65], SourceSha256[65];
    uint64 ProjectRevision;
    int64 ProjectCreatedAt, RestoredAt;
} MdoHomeSessionRestoreRequest;

typedef enum MdoHomeSessionRestoreOutcome {
    MDO_HOME_SESSION_RESTORE_PENDING,
    MDO_HOME_SESSION_RESTORE_COMMITTED,
    MDO_HOME_SESSION_RESTORE_ABORTED
} MdoHomeSessionRestoreOutcome;

typedef struct MdoHomeSessionRestoreReceipt {
    uint32 Size;
    MdoHomeSessionRestoreRequest Request;
    MdoHomeSessionRestoreOutcome Outcome;
    bool Committed; /* actual position proof survives incomplete receipt/cleanup */
    xfileinfo DirectoryIdentity;
} MdoHomeSessionRestoreReceipt;

/* Same transaction with durable acceptance in its immutable owner record.
 * Existing request IDs, even aborted or subsequently deleted targets, cannot
 * execute again. Acceptance flushes before returning; normal End/startup
 * recovery publishes a permanent terminal receipt before retiring the journal.
 * The caller reserves project/session and drains the handle before Unit. */
MdoHomeSessionRestore* MdoHomeSessionRestoreBeginRequested(
    const MdoHomeSessionRestoreRequest* Request, xroot* Parent);

/* Read-only, including frozen Home. Wrong Size leaves output untouched;
 * other failure clears fields except Size and Found=false. Missing is NOT
 * permission to execute another request. Pending needs the same-ID query or
 * startup recovery; terminal commit is independent of later session deletion.
 * A damaged/conflicting record fails, never becomes a missing result. */
bool MdoHomeSessionRestoreReceiptGet(cstr SessionId,
    MdoHomeSessionRestoreReceipt* Receipt, bool* Found);

/* Storage-only boundary, never HTTP/Agent authorization. The caller must
 * validate the target project/workspace, reserve its fresh session ID, and
 * coordinate project/session managers before publishing. Only 32 lowercase
 * hex session IDs are accepted. Existing target objects are never replaced.
 *
 * Lazily opens the leased Home, reserves one private journal, writes its
 * immutable identity/target ownership record, then returns an independently
 * owned staging parent for MdoSessionBackupStagePrepare. Ordinary unrelated
 * Home operations remain available. Home import/project purge and a second
 * restore cannot start concurrently. No Home lock is held during staging.
 * Parent must point to NULL; a nonempty output is rejected untouched. Close
 * every staging anchor/writer before End; consume Restore before Unit.
 * A failed begin leaves an initially empty Parent NULL. Failed recovery freezes writes. */
MdoHomeSessionRestore* MdoHomeSessionRestoreBegin(cstr ProjectId,
    cstr SessionId, xroot* Parent);

/* Consumes the current transaction exactly once. Publish=false discards its
 * private payload. Publish=true accepts one restore-<32hex> directory whose
 * name and Identity came from a successfully verified Stage. The caller must repeat the
 * Stage check and release all Stage anchors before entering this boundary.
 * This layer checks storage identities/inventory, NOT semantic readiness.
 *
 * The one no-replace directory rename is the commit. Committed remains true
 * if publication succeeded but journal retirement/cleanup failed; synchronize
 * managers and require restart rather than resubmit. Committed=false after
 * an ambiguous error is not retry permission: preserve the journal and query
 * the same target after startup recovery. End(false) cannot undo a commit.
 * Cleanup never follows links or traverses the live target. Startup resolves
 * interrupted transactions before managers initialize. No power-loss metadata
 * durability is promised. Cleanup authorizes only the format's bounded private
 * payload beneath recorded directory identities, not arbitrary Home paths. */
bool MdoHomeSessionRestoreEnd(MdoHomeSessionRestore* Restore,
    cstr DirectoryName, const xfileinfo* Identity, bool Publish, bool* Committed);

#endif
