#ifndef MDO_AGENTS_H
#define MDO_AGENTS_H

#include <xsbase.h>
#include <xwork.h>

#include "models.h"

typedef struct MdoAgentSession MdoAgentSession;
typedef struct MdoAgentRun MdoAgentRun;

#define MDO_AGENT_RECOVERY_TOKEN_CAPACITY 65u

typedef struct MdoAgentSessionOptions {
    uint32 Size;
    const char* AgentId;          /* NULL selects mdo.default. */
    const char* ModelId;          /* NULL inherits the Agent/global default. */
    MdoModelProtocol Protocol;    /* zero selects the model default. */
    const char* ReasoningEffort;  /* NULL inherits Agent/global/model policy. */
    const char* PermissionProfile; /* NULL inherits Agent/global policy. */
    uint32 MaxOutputTokens;       /* zero inherits the selected profile. */
    const char* WorkspaceRoot;    /* NULL selects the current directory. */
    const char* ProjectId;        /* optional project lifecycle/memory identity. */
    const char* ProductSessionId; /* optional memory audit correlation. */
    const char* AskScopeId;       /* optional question scope; defaults to ProductSessionId. */
    const char* SessionPath;      /* optional xllm-session snapshot path. */
    const char* JournalPath;      /* optional write-ahead journal path. */
    bool Recover;                 /* recover snapshot + journal as one unit. */
    const char* ArtifactDirectory;
    xcancel* Cancel;
    uint64 Deadline;
    xwork_approval_fn OnApproval;
    void* ApprovalUserData;
    xwork_permission_fn OnPermission;
    void* PermissionUserData;
    bool UseRunPermissionScope; /* pass an owner-scoped grant state instead. */
    xwork_hook_fn OnHook;
    void* HookUserData;
    xwork_event_fn OnEvent;
    void* EventUserData;
    /* Optional injectable boundary for offline hosts/tests. When supplied,
     * endpoint and credential resolution are skipped after profile validation. */
    xwork_model_complete_fn OnModelComplete;
    void* ModelUserData;
    /* Optional aggregate pin for all caller-owned callback state above. */
    void* OwnerUserData;
    xwork_agent_owner_retain_fn OnOwnerRetain;
    xwork_agent_owner_release_fn OnOwnerRelease;
} MdoAgentSessionOptions;

typedef struct MdoAgentSessionInfo {
    uint32 Size;
    uint64 ConfigRevision;
    uint64 ModelGeneration;
    uint64 ModuleGeneration;
    uint64 SkillGeneration;
    uint64 MemoryGeneration;
    uint64 ToolCatalogGeneration;
    const char* AgentId;
    const char* ModuleId;
    const char* ModelId;
    const char* ProviderId;
    const char* WireModel;
    const char* ReasoningEffort;
    const char* PermissionProfile;
    MdoModelProtocol Protocol;
    uint64 ContextWindowTokens;
    uint64 MaxInputTokens;
    uint32 MaxOutputTokens;
    size_t ToolCount;
    size_t SkillCount;
    size_t SubagentCount;
} MdoAgentSessionInfo;

/* All string pointers returned through MdoAgentSessionInfo and
 * MdoAgentRunInfo are borrowed from the retained session. */

typedef struct MdoAgentRunOptions {
    uint32 Size;
    const char* Prompt;           /* required unless Resume is true. */
    const xllm_message* UserMessage; /* borrowed through run creation */
    bool Resume;
    xcancel* Cancel;
    uint64 Deadline;
    xwork_event_fn OnEvent;
    void* EventUserData;
    const xwork_resume_options* ResumeOptions;
} MdoAgentRunOptions;

typedef struct MdoAgentRunInfo {
    uint32 Size;
    uint64 ConfigRevision;
    uint64 ModelGeneration;
    uint64 ModuleGeneration;
    uint64 SkillGeneration;
    uint64 MemoryGeneration;
    const char* AgentId;
    const char* ModelId;
    const char* WireModel;
    const char* ReasoningEffort;
    MdoModelProtocol Protocol;
    xwork_run_info Run;
} MdoAgentRunInfo;

void MdoAgentSessionOptionsInit(MdoAgentSessionOptions* Options);
MdoAgentSession* MdoAgentSessionCreate(
    const MdoAgentSessionOptions* Options, xwork_error* Error);
/* A nonempty ProjectId pins the project until the final callback owner release,
 * including retained runtime Agent references, regardless of memory settings. */
MdoAgentSession* MdoAgentSessionCreateWithRuntime(xwork_runtime* Runtime,
    const MdoAgentSessionOptions* Options, xwork_error* Error);
MdoAgentSession* MdoAgentSessionRef(MdoAgentSession* Session);
void MdoAgentSessionRelease(MdoAgentSession* Session);
bool MdoAgentSessionGetInfo(const MdoAgentSession* Session,
    MdoAgentSessionInfo* Info);
/* Capture unresolved durable tool calls while the Agent is idle. The caller
 * owns the returned snapshot and releases it with
 * xworkRecoverySnapshotRelease. */
xwork_recovery_snapshot* MdoAgentSessionRecoverySnapshot(
    MdoAgentSession* Session, xwork_error* Error);
bool MdoAgentSessionRecoveryRequired(MdoAgentSession* Session,
    bool* Required, xwork_error* Error);
/* Stable SHA-256 token over pending call identity, arguments, and current
 * tool descriptors. Catalog generations are intentionally excluded because
 * an equivalent catalog receives a new runtime-local generation on reopen. */
bool MdoAgentRecoverySnapshotToken(const xwork_recovery_snapshot* Snapshot,
    bool ResumeRequired,
    char Token[MDO_AGENT_RECOVERY_TOKEN_CAPACITY]);

/* Ledger maintenance claims the same exclusive run window as an Agent run.
 * It therefore fails while another run is active. Successful mutations are
 * checkpointed to the managed snapshot before returning. */
bool MdoAgentSessionCheckpoint(MdoAgentSession* Session, xwork_error* Error);
/* Checkpoint and perform a bounded, read-only capture before releasing the
 * exclusive run window. Read is called synchronously only after a successful
 * checkpoint; NULL Read performs just the checkpoint. A reader must not call
 * run or ledger maintenance APIs for this Agent. Failure releases the window
 * and preserves the reader's error. The caller must own Session on entry. */
typedef bool (*MdoAgentCheckpointReadFn)(void* UserData, xwork_error* Error);
bool MdoAgentSessionWithCheckpoint(MdoAgentSession* Session,
    MdoAgentCheckpointReadFn Read, void* UserData, xwork_error* Error);
/* Capture additionally refuses pending/running background tasks and retained
 * descendants. This covers their native artifact writes and final callbacks;
 * a root run claim alone does not freeze an asynchronous child Agent. */
bool MdoAgentSessionWithQuiescentCheckpoint(MdoAgentSession* Session,
    MdoAgentCheckpointReadFn Read, void* UserData, xwork_error* Error);
bool MdoAgentSessionLastSequence(MdoAgentSession* Session,
    uint64* LastSequence, xwork_error* Error);
/* Close a cancelled model turn without repeating it. Requires an exact ledger
 * tail and no unresolved tool calls; the synthetic assistant marker is
 * journaled and checkpointed under the exclusive Agent run claim. */
bool MdoAgentSessionFinishInterrupted(MdoAgentSession* Session,
    uint64 ExpectedLastSequence, uint64* FinishedSequence,
    xwork_error* Error);
bool MdoAgentSessionClear(MdoAgentSession* Session, xwork_error* Error);
bool MdoAgentSessionTruncateAfter(MdoAgentSession* Session,
    uint64 ThroughSequence, xwork_error* Error);
/* UINT64_MAX selects the current retained tail. The target is a new snapshot;
 * the fork has no journal attachment or runtime callbacks. */
bool MdoAgentSessionSaveFork(MdoAgentSession* Session,
    uint64 ThroughSequence, const char* SnapshotPath, uint64* SavedThrough,
    xwork_error* Error);

void MdoAgentRunOptionsInit(MdoAgentRunOptions* Options);
MdoAgentRun* MdoAgentRunCreate(MdoAgentSession* Session,
    const MdoAgentRunOptions* Options, xwork_error* Error);
bool MdoAgentRunStart(MdoAgentRun* Run, xwork_error* Error);
xwork_result MdoAgentRunWait(MdoAgentRun* Run, uint64 Deadline,
    xwork_run_result* Result, xwork_error* Error);
bool MdoAgentRunCancel(MdoAgentRun* Run);
bool MdoAgentRunGetInfo(const MdoAgentRun* Run, MdoAgentRunInfo* Info);
void MdoAgentRunDestroy(MdoAgentRun* Run);

#endif
