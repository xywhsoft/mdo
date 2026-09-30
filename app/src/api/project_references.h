#ifndef MDO_API_PROJECT_REFERENCES_H
#define MDO_API_PROJECT_REFERENCES_H

#include "../../include/mdo/home_purge.h"
#include "../../include/mdo/project_lifecycle.h"

typedef struct MdoProjectReferenceGuard MdoProjectReferenceGuard;

/* Execution only: the caller owns a current exclusive project lease. Begin
 * pins that lease and locks selection, then draft, before reading either.
 * Keep the guard on this thread until the storage transaction has committed
 * or rolled back. Free unlocks in reverse order and releases the lease pin.
 * Never acquire this guard while holding a Home or schedule manager lock.
 *
 * Only references whose stored project ID exactly matches ProjectId become
 * targets. Unassociated legacy draft text and other projects are retained.
 * Malformed/changed global records fail closed with no partial guard. These
 * two files must move in the SAME transaction as the project data. */
MdoProjectReferenceGuard* MdoApiProjectReferencesBegin(const char* ProjectId,
    MdoProjectLease* Owner, xwork_error* Error);
size_t MdoApiProjectReferencesCount(const MdoProjectReferenceGuard* Guard);
bool MdoApiProjectReferencesAt(const MdoProjectReferenceGuard* Guard,
    size_t Index, MdoHomePurgeTarget* Target);
void MdoApiProjectReferencesFree(MdoProjectReferenceGuard* Guard);

/* Internal half of the guard. Success keeps the draft mutex on this thread;
 * failure unlocks it. The combined guard always acquires selection first. */
bool MdoApiDraftReferenceLock(const char* ProjectId,
    const MdoProjectLease* Owner, MdoHomePurgeTarget* Target, bool* Present);
void MdoApiDraftReferenceUnlock(void);

static inline bool MdoReferenceInfoUsable(const xfileinfo* Info)
{
    return Info->Type == XFILE_TYPE_FILE && Info->Identity != 0u &&
        (Info->Available & (XFILE_INFO_IDENTITY | XFILE_INFO_SIZE)) ==
            (XFILE_INFO_IDENTITY | XFILE_INFO_SIZE);
}

static inline bool MdoReferenceInfoSame(const xfileinfo* A, const xfileinfo* B)
{
    return MdoReferenceInfoUsable(A) && MdoReferenceInfoUsable(B) &&
        A->Device == B->Device && A->Identity == B->Identity && A->Size == B->Size;
}

#endif
