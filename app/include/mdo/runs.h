#ifndef MDO_RUNS_H
#define MDO_RUNS_H

#include <xsbase.h>
#include <xwork.h>

#include "models.h"
#include "sessions.h"

#define MDO_RUN_ID_CAPACITY 49u
#define MDO_RUN_PROMPT_CAPACITY (64u * 1024u + 1u)
#define MDO_RUN_FINAL_TEXT_LIMIT (64u * 1024u)

typedef struct MdoRunSnapshot MdoRunSnapshot;

typedef struct MdoRunManagerOptions {
    uint32 Size;
    bool Automatic;
    uint32 PollMilliseconds;
    size_t MaxActive;
    size_t MaxRetained;
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
    /* When supplied, the manager pins this aggregate for its full lifetime;
     * each open managed session takes an additional temporary reference. */
    void* OwnerUserData;
    xwork_agent_owner_retain_fn OnOwnerRetain;
    xwork_agent_owner_release_fn OnOwnerRelease;
} MdoRunManagerOptions;

typedef struct MdoRunStartOptions {
    uint32 Size;
    const char* ProjectId;
    const char* SessionId;
    const char* Prompt;
    uint32 TimeoutMilliseconds; /* zero means no run deadline. */
} MdoRunStartOptions;

typedef struct MdoRunInfo {
    uint32 Size;
    uint64 AgentRunId;
    uint64 ConfigRevision;
    uint64 ModelGeneration;
    uint64 ModuleGeneration;
    uint64 SkillGeneration;
    uint64 MemoryGeneration;
    uint64 CreatedMicroseconds;
    uint64 StartedMicroseconds;
    uint64 EndedMicroseconds;
    uint64 AgentTurns;
    uint64 ModelCalls;
    uint64 ToolCalls;
    uint64 Compactions;
    size_t FinalTextBytes;
    xwork_run_state State;
    xwork_result Result;
    xwork_error_code ErrorCode;
    MdoModelProtocol Protocol;
    bool Terminal;
    bool CancelRequested;
    bool FinalTextAvailable;
    bool FinalTextTruncated;
    char Id[MDO_RUN_ID_CAPACITY];
    char ProjectId[MDO_PROJECT_ID_CAPACITY];
    char SessionId[MDO_SESSION_ID_CAPACITY];
    char AgentId[MDO_SESSION_IDENTITY_CAPACITY];
    char ModelId[MDO_SESSION_IDENTITY_CAPACITY];
    char ReasoningEffort[MDO_SESSION_REASONING_CAPACITY];
} MdoRunInfo;

typedef struct MdoRunManagerStatus {
    uint32 Size;
    bool Automatic;
    bool Stopping;
    uint32 PollMilliseconds;
    size_t ActiveRuns;
    size_t StartingRuns;
    size_t RetainedRuns;
    size_t MaxActive;
    size_t MaxRetained;
    uint64 RunsStarted;
    uint64 RunsCompleted;
    uint64 RunsFailed;
} MdoRunManagerStatus;

void MdoRunManagerOptionsInit(MdoRunManagerOptions* Options);
void MdoRunStartOptionsInit(MdoRunStartOptions* Options);
bool MdoRunManagerInit(xwork_runtime* Runtime,
    const MdoRunManagerOptions* Options, xwork_error* Error);
void MdoRunManagerUnit(void);
bool MdoRunManagerPump(size_t* Completed, xwork_error* Error);
bool MdoRunManagerGetStatus(MdoRunManagerStatus* Status);

bool MdoRunStart(const MdoRunStartOptions* Options, MdoRunInfo* Info,
    xwork_error* Error);
/* Cancellation is idempotent. A retained terminal run is returned unchanged. */
bool MdoRunCancel(const char* RunId, MdoRunInfo* Info,
    xwork_error* Error);

MdoRunSnapshot* MdoRunSnapshotCreate(xwork_error* Error);
MdoRunSnapshot* MdoRunSnapshotRef(MdoRunSnapshot* Snapshot);
void MdoRunSnapshotRelease(MdoRunSnapshot* Snapshot);
size_t MdoRunSnapshotCount(const MdoRunSnapshot* Snapshot);
bool MdoRunSnapshotAt(const MdoRunSnapshot* Snapshot, size_t Index,
    MdoRunInfo* Info);
bool MdoRunSnapshotFind(const MdoRunSnapshot* Snapshot, const char* RunId,
    MdoRunInfo* Info);
/* Text is borrowed from Snapshot and remains valid until its final release. */
bool MdoRunSnapshotResultAt(const MdoRunSnapshot* Snapshot, size_t Index,
    const char** Text, size_t* Size);
bool MdoRunSnapshotResult(const MdoRunSnapshot* Snapshot, const char* RunId,
    const char** Text, size_t* Size);

#endif
