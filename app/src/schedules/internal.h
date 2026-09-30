#ifndef MDO_SCHEDULES_INTERNAL_H
#define MDO_SCHEDULES_INTERNAL_H

#include "../../include/mdo/schedules.h"
#include "../../include/mdo/project_lifecycle.h"

/* Schema bridge used by the offline legacy migration staging pipeline. */
char* MdoSchedulesInternalJson(const MdoScheduleInfo* Info, size_t* Size);
bool MdoSchedulesInternalParse(const char* ExpectedId, xstrview Json,
    MdoScheduleInfo* Info);
bool MdoSchedulesInternalValidate(const MdoScheduleInfo* Info,
    xwork_error* Error);

typedef struct MdoSchedulePurgeGuard MdoSchedulePurgeGuard;
/* Application transaction only. Acquire after project exclusion and global
 * reference guards, before Home operations. ExpectedGeneration is from the
 * fresh inventory. Success pins the owner and holds the manager mutex on this
 * thread until Free; no schedule snapshot/ordinary mutation may be called
 * while holding it. Unit requires all guards to have been freed. */
MdoSchedulePurgeGuard* MdoSchedulesPurgeBegin(const char* ProjectId,
    MdoProjectLease* Owner, uint64 ExpectedGeneration, xwork_error* Error);
size_t MdoSchedulesPurgeCount(const MdoSchedulePurgeGuard* Guard);
/* Call iff storage reported Committed, even when storage returned an error.
 * No files or shared audit are written. Forget all owned cache entries and
 * unregister their native schedules. Failure freezes Home and pins exclusion
 * until manager Unit; calling again returns the same outcome. */
bool MdoSchedulesPurgeCommit(MdoSchedulePurgeGuard* Guard, xwork_error* Error);
/* An unresolved precommit rollback also isolates execution until restart.
 * Keep the original cache/disk metadata; disable native owned schedules. */
void MdoSchedulesPurgeQuarantine(MdoSchedulePurgeGuard* Guard);
void MdoSchedulesPurgeFree(MdoSchedulePurgeGuard* Guard);

#endif
