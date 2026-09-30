#ifndef MDO_HOME_PURGE_H
#define MDO_HOME_PURGE_H

#include "home.h"

#define MDO_HOME_PURGE_PATH_CAPACITY 256u
#define MDO_HOME_PURGE_TARGET_LIMIT 1024u
#define MDO_HOME_PURGE_REQUEST_CAPACITY 33u

typedef struct MdoHomePurgeTarget {
    char Path[MDO_HOME_PURGE_PATH_CAPACITY];
    xfileinfo Info;
} MdoHomePurgeTarget;

/* Request IDs are exactly 32 lowercase hexadecimal bytes. Immutable request
 * metadata describes the exclusively scanned attempt, not a client path list.
 * Zero statistics are reserved for a cancellation with no target scan/move. */
typedef struct MdoHomePurgeRequest {
    char Id[MDO_HOME_PURGE_REQUEST_CAPACITY];
    char ProjectId[65];
    uint64 Revision;
    uint64 CreatedAt; /* project incarnation, Unix microseconds */
    size_t Targets, Files, Directories, Schedules;
    uint64 Bytes;
    bool Selection, GlobalDraft;
} MdoHomePurgeRequest;

typedef enum MdoHomePurgeOutcome {
    MDO_HOME_PURGE_PENDING = 0,
    MDO_HOME_PURGE_COMMITTED,
    MDO_HOME_PURGE_ABORTED
} MdoHomePurgeOutcome;

typedef struct MdoHomePurgeReceipt {
    MdoHomePurgeRequest Request;
    MdoHomePurgeOutcome Outcome;
    bool Committed; /* proven commit marker even if terminal publication is pending */
} MdoHomePurgeReceipt;

bool MdoHomePurgeRequestIdValid(cstr RequestId);
/* Read-only, including during restart-required isolation. Found=false means
 * no accepted request or terminal receipt is known; it does not grant retry
 * authorization. Pending requests require recovery, not a new execution.
 * Malformed/conflicting records fail, never appear as a missing result. */
bool MdoHomePurgeReceiptGet(cstr RequestId, MdoHomePurgeReceipt* Receipt, bool* Found);

/* Durably reserve the reviewed ID as ABORTED before any execution accepts it.
 * No project roots are read or moved, and a missing Home is never created.
 * A matching terminal receipt is returned unchanged (including COMMITTED:
 * cancellation cannot undo a completed purge). Replayed distinguishes that
 * case from a newly reserved ID. Pending, conflicting or damaged evidence
 * fails; query the SAME ID and recover before discarding the client intent.
 * Home's lock serializes acceptance with FilesRequested, including callers
 * that already scanned targets. No project lifecycle lease is needed here. */
bool MdoHomePurgeRequestCancel(cstr RequestId, cstr ProjectId, uint64 Revision,
    int64 CreatedAt, MdoHomePurgeReceipt* Receipt, bool* Replayed);

/* Storage-only transaction, not an API authorization boundary. The caller
 * must hold its project's exclusive lifecycle lease, validate the revision,
 * settle global references, and synchronize managers after a commit.
 * Targets come from a fresh exclusive inventory, never an HTTP request.
 *
 * Global data/draft.json and data/workspace-state.json are allowed only when
 * the caller holds their reference guard and has validated current ownership;
 * unassociated text and another project's records must never be included.
 * Moves the project's fixed roots, owned references and schedule namespaces into a
 * private journal. Immutable flushed markers and recorded file identities
 * support process-interruption recovery before managers initialize. A failed
 * precommit move rolls back without replacement; ambiguity freezes Home
 * writes. Committed is true once deletion is committed, even if later cleanup
 * fails. Such failure requires restart; a commit must never be retried as if
 * nothing happened. No power-loss metadata durability is promised. */
bool MdoHomePurgeFiles(cstr ProjectId, const MdoHomePurgeTarget* Targets,
    size_t Count, bool* Committed);

/* Same storage boundary, with durable acceptance and immutable terminal
 * receipt. Existing IDs are rejected before any move; the application looks
 * up and replays matching results before entering this function. Neither
 * receipt retention nor process-interruption recovery expires IDs. */
bool MdoHomePurgeFilesRequested(const MdoHomePurgeRequest* Request,
    const MdoHomePurgeTarget* Targets, size_t Count, bool* Committed);

#endif
