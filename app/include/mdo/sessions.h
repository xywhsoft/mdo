#ifndef MDO_SESSIONS_H
#define MDO_SESSIONS_H

#include <xsbase.h>
#include <xwork.h>

#include "agents.h"

#define MDO_SESSION_ID_CAPACITY 33u
#define MDO_PROJECT_ID_CAPACITY 65u
#define MDO_SESSION_TITLE_CAPACITY 257u
#define MDO_SESSION_IDENTITY_CAPACITY 129u
#define MDO_SESSION_REASONING_CAPACITY 33u
#define MDO_SESSION_WORKSPACE_CAPACITY 2049u
#define MDO_SESSION_PATH_CAPACITY 256u

typedef struct MdoSession MdoSession;
typedef struct MdoSessionCatalog MdoSessionCatalog;
typedef struct MdoSessionEventSnapshot MdoSessionEventSnapshot;

typedef enum MdoSessionStatus {
    MDO_SESSION_ACTIVE = 1,
    MDO_SESSION_ARCHIVED,
    MDO_SESSION_TRASH
} MdoSessionStatus;

#define MDO_SESSION_STATUS_ACTIVE_FLAG (1u << 0)
#define MDO_SESSION_STATUS_ARCHIVED_FLAG (1u << 1)
#define MDO_SESSION_STATUS_TRASH_FLAG (1u << 2)
#define MDO_SESSION_STATUS_ALL_FLAGS \
    (MDO_SESSION_STATUS_ACTIVE_FLAG | MDO_SESSION_STATUS_ARCHIVED_FLAG | \
     MDO_SESSION_STATUS_TRASH_FLAG)

typedef struct MdoSessionCreateOptions {
    uint32 Size;
    const char* ProjectId;       /* required portable identifier. */
    const char* Title;           /* NULL selects "New session". */
    MdoAgentSessionOptions Agent;
    const char* RequestedId;     /* NULL generates an ID; otherwise reserved. */
} MdoSessionCreateOptions;

/* Open-time callbacks are intentionally separate from durable identity.
 * Agent/model/workspace selection is restored from meta.json. */
typedef struct MdoSessionRuntimeOptions {
    uint32 Size;
    xcancel* Cancel;
    uint64 Deadline;
    xwork_approval_fn OnApproval;
    void* ApprovalUserData;
    xwork_permission_fn OnPermission;
    void* PermissionUserData;
    bool UseRunPermissionScope;
    xwork_hook_fn OnHook;
    void* HookUserData;
    xwork_event_fn OnEvent;
    void* EventUserData;
    xwork_model_complete_fn OnModelComplete;
    void* ModelUserData;
    void* OwnerUserData;
    xwork_agent_owner_retain_fn OnOwnerRetain;
    xwork_agent_owner_release_fn OnOwnerRelease;
    /* Complete open-time profile override. Applied while this session owns
     * the exclusive runtime reservation, before its Agent can run. */
    const char* ProfileModelId;
    const char* ProfileReasoningEffort;
    const char* ProfilePermissionProfile;
} MdoSessionRuntimeOptions;

typedef struct MdoSessionForkOptions {
    uint32 Size;
    const char* Title;           /* NULL inherits the source title. */
    uint64 ThroughSequence;      /* UINT64_MAX selects the retained tail. */
    MdoSessionRuntimeOptions Runtime;
} MdoSessionForkOptions;

typedef struct MdoSessionInfo {
    uint32 Size;
    uint64 Revision;
    int64 CreatedAt;
    int64 UpdatedAt;
    bool Pinned;
    bool RuntimeOpen;            /* transient; never persisted to meta.json. */
    MdoSessionStatus Status;
    MdoSessionStatus PreviousStatus;
    MdoModelProtocol Protocol;
    uint32 MaxOutputTokens;
    uint64 ConfigRevision;
    uint64 ModelGeneration;
    uint64 ModuleGeneration;
    uint64 SkillGeneration;
    uint64 ForkedThroughSequence;
    char Id[MDO_SESSION_ID_CAPACITY];
    char ProjectId[MDO_PROJECT_ID_CAPACITY];
    char ParentSessionId[MDO_SESSION_ID_CAPACITY];
    char Title[MDO_SESSION_TITLE_CAPACITY];
    char AgentId[MDO_SESSION_IDENTITY_CAPACITY];
    char ModelId[MDO_SESSION_IDENTITY_CAPACITY];
    char ReasoningEffort[MDO_SESSION_REASONING_CAPACITY];
    char PermissionProfile[MDO_SESSION_REASONING_CAPACITY];
    char WorkspaceRoot[MDO_SESSION_WORKSPACE_CAPACITY];
} MdoSessionInfo;

typedef struct MdoSessionDiagnostic {
    uint32 Size;
    char Path[MDO_SESSION_PATH_CAPACITY];
    char Message[256];
} MdoSessionDiagnostic;

typedef struct MdoSessionQuery {
    uint32 Size;
    const char* ProjectId;       /* NULL searches every project. */
    const char* Text;            /* NULL/empty matches every session. */
    uint32 StatusFlags;          /* zero selects all states. */
    bool PinnedOnly;
    size_t Limit;                /* zero selects the bounded default. */
} MdoSessionQuery;

/* UI journal event outside xwork's live callback vocabulary. */
#define MDO_SESSION_EVENT_HISTORY_TRUNCATED \
    ((xwork_event_kind)(XWORK_EVENT_RECOVERY_RESOLVED + 1))

/* Borrowed string views returned by an owned event snapshot. */
typedef struct MdoSessionEventInfo {
    uint32 Size;
    uint32 SchemaVersion;
    uint64 EventId;
    uint64 SourceEventId;
    int64 OccurredAt;
    xwork_event_kind Kind;
    uint64 AgentTurn;
    uint64 UserMessageSequence;
    uint32 AgentDepth;
    uint64 AgentId;
    uint64 RunId;
    char QueueItemId[33]; /* Present only on a queue-bound main Agent start. */
    uint64 TaskId;
    uint64 ArtifactId;
    uint64 ParentRunId;
    xwork_tool_effects Effects;
    uint32 TaskState;
    uint64 TaskRevision;
    uint64 InputTokens;
    uint64 OutputTokens;
    uint64 TotalTokens;
    bool Success;
    bool EffectApplied;
    bool TextTruncated;
    const char* Text;
    const char* ToolName;
    const char* ToolCallId;
    const char* ArtifactPath;
    const char* Model;
    const char* ModelId;
    uint64 ContextWindowTokens;
} MdoSessionEventInfo;

/* Product bootstrap (or an embedder) initializes MdoProjectLifecycle before
 * this manager. Session handles and callback owners pin a shared project lease. */
bool MdoSessionManagerInit(xwork_runtime* Runtime);
void MdoSessionManagerUnit(void);
uint64 MdoSessionManagerGeneration(void);

void MdoSessionCreateOptionsInit(MdoSessionCreateOptions* Options);
void MdoSessionRuntimeOptionsInit(MdoSessionRuntimeOptions* Options);
void MdoSessionForkOptionsInit(MdoSessionForkOptions* Options);
void MdoSessionQueryInit(MdoSessionQuery* Query);

MdoSession* MdoSessionCreate(const MdoSessionCreateOptions* Options,
    xwork_error* Error);
MdoSession* MdoSessionOpen(const char* ProjectId, const char* SessionId,
    const MdoSessionRuntimeOptions* Options, xwork_error* Error);
MdoSession* MdoSessionFork(MdoSession* Source,
    const MdoSessionForkOptions* Options, xwork_error* Error);
/* Loads metadata for archived/trash administration without creating an
 * Agent. MdoSessionAgentRef returns NULL for this handle. */
MdoSession* MdoSessionLoad(const char* ProjectId, const char* SessionId,
    xwork_error* Error);
MdoSession* MdoSessionRef(MdoSession* Session);
void MdoSessionRelease(MdoSession* Session);
bool MdoSessionGetInfo(MdoSession* Session, MdoSessionInfo* Info);
MdoAgentSession* MdoSessionAgentRef(MdoSession* Session);

bool MdoSessionRename(MdoSession* Session, const char* Title,
    xwork_error* Error);
/* Revalidate a recovered idle session before atomically changing its next-run
 * model, reasoning and permission profile. NULL fields retain their value. */
bool MdoSessionSetProfile(MdoSession* Session, const char* ModelId,
    const char* ReasoningEffort, const char* PermissionProfile,
    const MdoSessionRuntimeOptions* Runtime, xwork_error* Error);
bool MdoSessionSetPinned(MdoSession* Session, bool Pinned,
    xwork_error* Error);
bool MdoSessionSetArchived(MdoSession* Session, bool Archived,
    xwork_error* Error);
bool MdoSessionMoveToTrash(MdoSession* Session, xwork_error* Error);
bool MdoSessionRestore(MdoSession* Session, xwork_error* Error);
bool MdoSessionLastSequence(MdoSession* Session, uint64* LastSequence,
    xwork_error* Error);
/* The caller must present the recovery view's revision and ledger sequence.
 * Fails if a run is active, the state changed, or tool decisions are pending. */
bool MdoSessionFinishInterrupted(MdoSession* Session,
    uint64 ExpectedRevision, uint64 ExpectedLastSequence,
    uint64* FinishedSequence, xwork_error* Error);
bool MdoSessionClear(MdoSession* Session, xwork_error* Error);
bool MdoSessionTruncateAfter(MdoSession* Session, uint64 ThroughSequence,
    xwork_error* Error);
typedef enum MdoSessionMessageMutationResult {
    MDO_SESSION_MESSAGE_OK = 0,
    MDO_SESSION_MESSAGE_CHANGED,
    MDO_SESSION_MESSAGE_FAILED
} MdoSessionMessageMutationResult;
/* Edit/retry must identify the original top-level agent_start event, not just
 * a ledger sequence that can be reused after truncation. Refuses without
 * changing files when that event no longer owns ThroughSequence + 1. */
MdoSessionMessageMutationResult MdoSessionTruncateMessage(MdoSession* Session,
    uint64 ThroughSequence, uint64 SourceEventId, xwork_error* Error);
/* Returns owned UTF-8 JSON containing meta and a checkpointed xllm snapshot.
 * Release with xrtFree. Artifacts and UI events remain separate exports. */
str MdoSessionExportJson(MdoSession* Session, size_t* Size,
    xwork_error* Error);

/* Nonblocking session-owned capture: current metadata, quiescent checkpoint,
 * UI events, todo and direct attachment reference/fork writers are fixed for
 * the callback. API callers must additionally freeze attachment/draft/queue/
 * feedback storage with the API capture guard. Read performs bounded external
 * Home reads/copies only; no network, manager APIs or callback reentry. */
typedef bool (*MdoSessionCaptureReadFn)(const MdoSessionInfo* Info,
    void* UserData, xwork_error* Error);
bool MdoSessionWithCapture(MdoSession* Session, MdoSessionCaptureReadFn Read,
    void* UserData, xwork_error* Error);

MdoSessionCatalog* MdoSessionCatalogSnapshot(xwork_error* Error);
MdoSessionCatalog* MdoSessionCatalogSearch(const MdoSessionQuery* Query,
    xwork_error* Error);
MdoSessionCatalog* MdoSessionCatalogRef(MdoSessionCatalog* Catalog);
void MdoSessionCatalogRelease(MdoSessionCatalog* Catalog);
uint64 MdoSessionCatalogGeneration(const MdoSessionCatalog* Catalog);
size_t MdoSessionCatalogCount(const MdoSessionCatalog* Catalog);
bool MdoSessionCatalogAt(const MdoSessionCatalog* Catalog, size_t Index,
    MdoSessionInfo* Info);
size_t MdoSessionCatalogDiagnosticCount(const MdoSessionCatalog* Catalog);
bool MdoSessionCatalogDiagnosticAt(const MdoSessionCatalog* Catalog,
    size_t Index, MdoSessionDiagnostic* Diagnostic);

/* Replays up to Limit events strictly after AfterEventId. Limit zero selects
 * the default; values above the public maximum are rejected. */
MdoSessionEventSnapshot* MdoSessionEventReplay(const char* ProjectId,
    const char* SessionId, uint64 AfterEventId, size_t Limit,
    xwork_error* Error);
MdoSessionEventSnapshot* MdoSessionEventSnapshotRef(
    MdoSessionEventSnapshot* Snapshot);
void MdoSessionEventSnapshotRelease(MdoSessionEventSnapshot* Snapshot);
size_t MdoSessionEventSnapshotCount(const MdoSessionEventSnapshot* Snapshot);
bool MdoSessionEventSnapshotAt(const MdoSessionEventSnapshot* Snapshot,
    size_t Index, MdoSessionEventInfo* Info);
uint64 MdoSessionEventSnapshotNextCursor(
    const MdoSessionEventSnapshot* Snapshot);
uint64 MdoSessionEventSnapshotLatestId(
    const MdoSessionEventSnapshot* Snapshot);
bool MdoSessionEventSnapshotHistoryLost(
    const MdoSessionEventSnapshot* Snapshot);
/* Positive evidence only: a missing or trimmed event leaves the start
 * uncertain. Does not create or change session state. */
bool MdoSessionEventQueueStartSeen(const char* ProjectId,
    const char* SessionId, const char* QueueItemId, uint64 AgentRunId,
    bool* Seen);

/* The built-in mdo.todo tool projects its latest successful main-Agent
 * snapshot into a bounded Home sidecar. A missing sidecar loads as empty and
 * never creates Home. The returned value is caller-owned. Project/Reset take
 * a shared lifecycle lease before any sidecar write, including direct calls. */
bool MdoSessionTodoProject(const char* ProjectId, const char* SessionId,
    uint64 EventId, const xwork_event* Event);
bool MdoSessionTodoLoad(const char* ProjectId, const char* SessionId,
    xvalue** Output);
/* Replace a stale plan with an explicit empty projection. */
bool MdoSessionTodoReset(const char* ProjectId, const char* SessionId);

#endif
