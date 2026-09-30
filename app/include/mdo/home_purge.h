#ifndef MDO_HOME_PURGE_H
#define MDO_HOME_PURGE_H

#include "home.h"

#define MDO_HOME_PURGE_PATH_CAPACITY 256u
#define MDO_HOME_PURGE_TARGET_LIMIT 1024u

typedef struct MdoHomePurgeTarget {
    char Path[MDO_HOME_PURGE_PATH_CAPACITY];
    xfileinfo Info;
} MdoHomePurgeTarget;

/* Storage-only transaction, not an API authorization boundary. The caller
 * must hold its project's exclusive lifecycle lease, validate the revision,
 * settle global references, and synchronize managers after a commit.
 * Targets come from a fresh exclusive inventory, never an HTTP request.
 *
 * Moves only the project's fixed roots and valid schedule namespaces into a
 * private journal. Immutable flushed markers and recorded file identities
 * support process-interruption recovery before managers initialize. A failed
 * precommit move rolls back without replacement; ambiguity freezes Home
 * writes. Committed is true once deletion is committed, even if later cleanup
 * fails. Such failure requires restart; a commit must never be retried as if
 * nothing happened. No power-loss metadata durability is promised. */
bool MdoHomePurgeFiles(cstr ProjectId, const MdoHomePurgeTarget* Targets,
    size_t Count, bool* Committed);

#endif
