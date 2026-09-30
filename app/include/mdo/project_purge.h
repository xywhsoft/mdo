#ifndef MDO_PROJECT_PURGE_H
#define MDO_PROJECT_PURGE_H

#include "projects.h"
#include "project_lifecycle.h"
#include "home_purge.h"

#define MDO_PROJECT_PURGE_PATH_CAPACITY MDO_HOME_PURGE_PATH_CAPACITY
#define MDO_PROJECT_PURGE_TARGET_LIMIT MDO_HOME_PURGE_TARGET_LIMIT
#define MDO_PROJECT_PURGE_NODE_LIMIT 8192u
#define MDO_PROJECT_PURGE_DEPTH_LIMIT 16u

typedef struct MdoProjectPurgeInventory MdoProjectPurgeInventory;
typedef MdoHomePurgeTarget MdoProjectPurgeTarget;

typedef struct MdoProjectPurgeInventoryInfo {
    MdoProjectInfo Project;
    size_t Targets, Files, Directories;
    uint64 Bytes;
    size_t ScheduleBackups, ScheduleHistories;
    bool ProjectDraft, ProjectDraftBackup;
} MdoProjectPurgeInventoryInfo;

/* Read-only and bounded; never creates Home, locks on disk, or repairs data.
 * NULL Owner acquires a shared project lease for this scan. An owned exclusive
 * lease from the current registry allows a transaction to rescan without
 * releasing exclusion. All filesystem targets are generated internally.
 *
 * This enumerates project-owned data and validates schedule/catalog ownership;
 * it is not deletion authorization. Shared audit/report records and global
 * selection/draft references require the transaction's separate policy. A
 * shared preview can race ordinary writers; execution must rescan exclusively.
 * No partial inventory is returned after an error or a scan limit. */
MdoProjectPurgeInventory* MdoProjectPurgeInventoryCreate(const char* ProjectId,
    const MdoProjectLease* Owner, xwork_error* Error);
void MdoProjectPurgeInventoryFree(MdoProjectPurgeInventory* Inventory);
bool MdoProjectPurgeInventoryGetInfo(const MdoProjectPurgeInventory* Inventory,
    MdoProjectPurgeInventoryInfo* Info);
bool MdoProjectPurgeInventoryAt(const MdoProjectPurgeInventory* Inventory,
    size_t Index, MdoProjectPurgeTarget* Target);

#endif
