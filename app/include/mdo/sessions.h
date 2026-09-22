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

typedef struct MdoSessionCreateOptions {
    uint32 Size;
    const char* ProjectId;       /* required portable identifier. */
    const char* Title;           /* NULL selects "New session". */
    MdoAgentSessionOptions Agent;
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
    xwork_hook_fn OnHook;
    void* HookUserData;
    xwork_event_fn OnEvent;
    void* EventUserData;
    xwork_model_complete_fn OnModelComplete;
    void* ModelUserData;
    void* OwnerUserData;
    xwork_agent_owner_retain_fn OnOwnerRetain;
    xwork_agent_owner_release_fn OnOwnerRelease;
} MdoSessionRuntimeOptions;

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
    char Id[MDO_SESSION_ID_CAPACITY];
    char ProjectId[MDO_PROJECT_ID_CAPACITY];
    char Title[MDO_SESSION_TITLE_CAPACITY];
    char AgentId[MDO_SESSION_IDENTITY_CAPACITY];
    char ModelId[MDO_SESSION_IDENTITY_CAPACITY];
    char ReasoningEffort[MDO_SESSION_REASONING_CAPACITY];
    char WorkspaceRoot[MDO_SESSION_WORKSPACE_CAPACITY];
} MdoSessionInfo;

typedef struct MdoSessionDiagnostic {
    uint32 Size;
    char Path[MDO_SESSION_PATH_CAPACITY];
    char Message[256];
} MdoSessionDiagnostic;

/* Borrowed string views returned by an owned event snapshot. */
typedef struct MdoSessionEventInfo {
    uint32 Size;
    uint32 SchemaVersion;
    uint64 EventId;
    uint64 SourceEventId;
    int64 OccurredAt;
    xwork_event_kind Kind;
    uint64 AgentTurn;
    uint32 AgentDepth;
    uint64 AgentId;
    uint64 RunId;
    uint64 TaskId;
    uint64 ArtifactId;
    uint64 ParentRunId;
    xwork_tool_effects Effects;
    uint32 TaskState;
    uint64 TaskRevision;
    bool Success;
    bool EffectApplied;
    bool TextTruncated;
    const char* Text;
    const char* ToolName;
    const char* ToolCallId;
    const char* ArtifactPath;
    const char* Model;
} MdoSessionEventInfo;

bool MdoSessionManagerInit(xwork_runtime* Runtime);
void MdoSessionManagerUnit(void);
uint64 MdoSessionManagerGeneration(void);

void MdoSessionCreateOptionsInit(MdoSessionCreateOptions* Options);
void MdoSessionRuntimeOptionsInit(MdoSessionRuntimeOptions* Options);

MdoSession* MdoSessionCreate(const MdoSessionCreateOptions* Options,
    xwork_error* Error);
MdoSession* MdoSessionOpen(const char* ProjectId, const char* SessionId,
    const MdoSessionRuntimeOptions* Options, xwork_error* Error);
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
bool MdoSessionSetPinned(MdoSession* Session, bool Pinned,
    xwork_error* Error);
bool MdoSessionSetArchived(MdoSession* Session, bool Archived,
    xwork_error* Error);
bool MdoSessionMoveToTrash(MdoSession* Session, xwork_error* Error);
bool MdoSessionRestore(MdoSession* Session, xwork_error* Error);

MdoSessionCatalog* MdoSessionCatalogSnapshot(xwork_error* Error);
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

#endif
