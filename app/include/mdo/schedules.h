#ifndef MDO_SCHEDULES_H
#define MDO_SCHEDULES_H

#include <xsbase.h>
#include <xwork.h>

#include "models.h"

#define MDO_SCHEDULE_ID_CAPACITY 65u
#define MDO_SCHEDULE_LABEL_CAPACITY 257u
#define MDO_SCHEDULE_NOTIFY_CAPACITY 257u
#define MDO_SCHEDULE_PROJECT_CAPACITY 65u
#define MDO_SCHEDULE_IDENTITY_CAPACITY 129u
#define MDO_SCHEDULE_REASONING_CAPACITY 33u
#define MDO_SCHEDULE_WORKSPACE_CAPACITY 2049u
#define MDO_SCHEDULE_INPUT_CAPACITY (64u * 1024u + 1u)
#define MDO_SCHEDULE_RESULT_CAPACITY (64u * 1024u + 1u)
#define MDO_SCHEDULE_PATH_CAPACITY 256u
#define MDO_SCHEDULE_PROTOCOL_DEFAULT ((MdoModelProtocol)0)

typedef struct MdoScheduleCatalog MdoScheduleCatalog;

typedef struct MdoScheduleInfo {
    uint32 Size;
    uint64 Revision;
    int64 UpdatedAt;
    uint64 RuntimeGeneration;
    int64 NextOccurrenceAt;
    int64 LastClaimedAt;
    uint64 ClaimCount;
    uint64 MisfireCount;
    size_t ActiveRuns;
    xwork_schedule_frequency Frequency;
    uint32 Interval;
    int64 StartAt;
    uint8 WeekdayMask;
    xwork_schedule_timezone Timezone;
    int32 UtcOffsetSeconds;
    xwork_schedule_fold_policy FoldPolicy;
    xwork_schedule_misfire_policy MisfirePolicy;
    uint32 MisfireGraceSeconds;
    uint32 MaxCatchUp;
    xwork_schedule_overlap_policy OverlapPolicy;
    uint32 MaxConcurrentRuns;
    MdoModelProtocol Protocol;
    uint32 MaxOutputTokens;
    bool Enabled;       /* durable definition setting. */
    bool Runnable;      /* false when scheduling is globally disabled. */
    char Id[MDO_SCHEDULE_ID_CAPACITY];
    char Label[MDO_SCHEDULE_LABEL_CAPACITY];
    char Notify[MDO_SCHEDULE_NOTIFY_CAPACITY];
    char ProjectId[MDO_SCHEDULE_PROJECT_CAPACITY];
    char AgentId[MDO_SCHEDULE_IDENTITY_CAPACITY];
    char ModelId[MDO_SCHEDULE_IDENTITY_CAPACITY];
    char ReasoningEffort[MDO_SCHEDULE_REASONING_CAPACITY];
    char WorkspaceRoot[MDO_SCHEDULE_WORKSPACE_CAPACITY];
    char Input[MDO_SCHEDULE_INPUT_CAPACITY];
} MdoScheduleInfo;

typedef struct MdoScheduleCreateOptions {
    uint32 Size;
    const char* Id;                 /* NULL creates an XID. */
    const char* Label;
    const char* Notify;
    const char* ProjectId;
    const char* AgentId;
    const char* ModelId;            /* NULL inherits the Agent/default. */
    MdoModelProtocol Protocol;       /* zero inherits selection policy. */
    const char* ReasoningEffort;     /* NULL inherits Agent/default. */
    uint32 MaxOutputTokens;          /* zero inherits Agent/model limits. */
    const char* WorkspaceRoot;       /* NULL selects current directory. */
    const char* Input;
    xwork_schedule_frequency Frequency;
    uint32 Interval;
    int64 StartAt;
    uint8 WeekdayMask;
    xwork_schedule_timezone Timezone;
    int32 UtcOffsetSeconds;
    xwork_schedule_fold_policy FoldPolicy;
    xwork_schedule_misfire_policy MisfirePolicy;
    uint32 MisfireGraceSeconds;
    uint32 MaxCatchUp;
    xwork_schedule_overlap_policy OverlapPolicy;
    uint32 MaxConcurrentRuns;
    bool Enabled;
} MdoScheduleCreateOptions;

typedef struct MdoScheduleClaim {
    uint32 Size;
    bool Claimed;
    int64 NextWakeAt;
    uint64 TaskId;
    uint64 RuntimeGeneration;
    uint64 DefinitionRevision;
    int64 OccurrenceAt;
    char ScheduleId[MDO_SCHEDULE_ID_CAPACITY];
    char ProjectId[MDO_SCHEDULE_PROJECT_CAPACITY];
    char AgentId[MDO_SCHEDULE_IDENTITY_CAPACITY];
    char ModelId[MDO_SCHEDULE_IDENTITY_CAPACITY];
    MdoModelProtocol Protocol;
    char ReasoningEffort[MDO_SCHEDULE_REASONING_CAPACITY];
    uint32 MaxOutputTokens;
    char WorkspaceRoot[MDO_SCHEDULE_WORKSPACE_CAPACITY];
    char Input[MDO_SCHEDULE_INPUT_CAPACITY];
} MdoScheduleClaim;

typedef struct MdoScheduleDiagnostic {
    uint32 Size;
    char Path[MDO_SCHEDULE_PATH_CAPACITY];
    char Message[256];
} MdoScheduleDiagnostic;

typedef struct MdoScheduleExecutorOptions {
    uint32 Size;
    bool Automatic;
    uint32 PollMilliseconds;
    size_t MaxClaimsPerPump;
    xwork_approval_fn OnApproval;
    void* ApprovalUserData;
    xwork_permission_fn OnPermission;
    void* PermissionUserData;
    xwork_hook_fn OnHook;
    void* HookUserData;
    xwork_event_fn OnEvent;
    void* EventUserData;
    xwork_model_complete_fn OnModelComplete;
    void* ModelUserData;
    void* OwnerUserData;
    xwork_agent_owner_retain_fn OnOwnerRetain;
    xwork_agent_owner_release_fn OnOwnerRelease;
} MdoScheduleExecutorOptions;

typedef struct MdoScheduleExecutorSnapshot {
    uint32 Size;
    bool Automatic;
    bool PersistenceFault;
    uint32 PollMilliseconds;
    size_t ActiveRuns;
    uint64 ClaimsStarted;
    uint64 RunsCompleted;
    uint64 RunsFailed;
    char LastError[256];
} MdoScheduleExecutorSnapshot;

bool MdoScheduleManagerInit(xwork_runtime* Runtime);
void MdoScheduleManagerUnit(void);
uint64 MdoScheduleManagerGeneration(void);
bool MdoScheduleManagerEnabled(void);

void MdoScheduleCreateOptionsInit(MdoScheduleCreateOptions* Options);
void MdoScheduleClaimInit(MdoScheduleClaim* Claim);

bool MdoScheduleCreate(const MdoScheduleCreateOptions* Options,
    MdoScheduleInfo* Info, xwork_error* Error);
bool MdoScheduleSetEnabled(const char* ScheduleId, uint64 ExpectedRevision,
    bool Enabled, MdoScheduleInfo* Info, xwork_error* Error);
bool MdoScheduleClaimDue(int64 Now, MdoScheduleClaim* Claim,
    xwork_error* Error);
bool MdoScheduleFinishTask(uint64 TaskId, xwork_result Result,
    const char* ResultText, xwork_error* Error);
bool MdoScheduleFinishTaskWithRun(uint64 TaskId, uint64 AgentRunId,
    xwork_result Result, const char* ResultText, xwork_error* Error);

/* The product executor owns ordinary Agent runs for claimed occurrences.
 * Automatic mode uses one lightweight host timer thread; manual mode lets
 * tests and embedders supply an explicit Unix-microsecond clock. */
void MdoScheduleExecutorOptionsInit(MdoScheduleExecutorOptions* Options);
bool MdoScheduleExecutorInit(xwork_runtime* Runtime,
    const MdoScheduleExecutorOptions* Options, xwork_error* Error);
void MdoScheduleExecutorUnit(void);
bool MdoScheduleExecutorPump(int64 Now, size_t* Started, size_t* Completed,
    xwork_error* Error);
bool MdoScheduleExecutorGetSnapshot(MdoScheduleExecutorSnapshot* Snapshot);

MdoScheduleCatalog* MdoScheduleCatalogSnapshot(xwork_error* Error);
MdoScheduleCatalog* MdoScheduleCatalogRef(MdoScheduleCatalog* Catalog);
void MdoScheduleCatalogRelease(MdoScheduleCatalog* Catalog);
uint64 MdoScheduleCatalogGeneration(const MdoScheduleCatalog* Catalog);
size_t MdoScheduleCatalogCount(const MdoScheduleCatalog* Catalog);
bool MdoScheduleCatalogAt(const MdoScheduleCatalog* Catalog, size_t Index,
    MdoScheduleInfo* Info);
bool MdoScheduleCatalogFind(const MdoScheduleCatalog* Catalog,
    const char* ScheduleId, MdoScheduleInfo* Info);
size_t MdoScheduleCatalogDiagnosticCount(const MdoScheduleCatalog* Catalog);
bool MdoScheduleCatalogDiagnosticAt(const MdoScheduleCatalog* Catalog,
    size_t Index, MdoScheduleDiagnostic* Diagnostic);

#endif
