#ifndef MDO_HOME_RESTORE_H
#define MDO_HOME_RESTORE_H

#include "home.h"

typedef struct MdoHomeSessionRestore MdoHomeSessionRestore;

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
