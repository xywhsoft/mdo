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
    uint64 ScheduleGeneration;
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

typedef enum MdoProjectPurgeStatus {
    MDO_PROJECT_PURGE_OK = 0,
    MDO_PROJECT_PURGE_INVALID,
    MDO_PROJECT_PURGE_NOT_FOUND,
    MDO_PROJECT_PURGE_REVISION_CONFLICT,
    MDO_PROJECT_PURGE_BUSY,
    MDO_PROJECT_PURGE_UNAVAILABLE,
    MDO_PROJECT_PURGE_ABORTED,
    MDO_PROJECT_PURGE_RESTART_REQUIRED,
    MDO_PROJECT_PURGE_REQUEST_CONFLICT
} MdoProjectPurgeStatus;

typedef struct MdoProjectPurgeResult {
    bool Committed, RestartRequired;
    bool SelectionRemoved, GlobalDraftRemoved;
    size_t Targets, Files, Directories, Schedules;
    uint64 Bytes;
    bool Replayed;
    char RequestId[MDO_HOME_PURGE_REQUEST_CAPACITY];
} MdoProjectPurgeResult;

/* Synchronous application coordinator. Acquires exclusion, validates the
 * current revision and complete inventories, locks conditional references
 * and schedule cache, then moves all data in one Home transaction. Result is
 * always initialized and Committed is authoritative even on an error.
 *
 * This has no durable client request/result receipt yet. Do not expose it as
 * an HTTP delete endpoint or automatically retry after a lost response. */
MdoProjectPurgeStatus MdoProjectPurgeExecute(const char* ProjectId,
    uint64 ExpectedRevision, MdoProjectPurgeResult* Result, xwork_error* Error);

/* Durable client execution. Request ID is bound to project ID, revision and
 * creation time (a recreated definition can start again at revision 1).
 * Matching terminal receipts replay without touching the current project.
 * Accepted pending requests require recovery; conflicts never move files. */
MdoProjectPurgeStatus MdoProjectPurgeExecuteRequested(const char* RequestId,
    const char* ProjectId, uint64 ExpectedRevision, int64 ExpectedCreatedAt,
    MdoProjectPurgeResult* Result, xwork_error* Error);

#endif
