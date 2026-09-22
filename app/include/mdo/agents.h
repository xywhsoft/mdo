#ifndef MDO_AGENTS_H
#define MDO_AGENTS_H

#include <xsbase.h>
#include <xwork.h>

#include "models.h"

typedef struct MdoAgentSession MdoAgentSession;
typedef struct MdoAgentRun MdoAgentRun;

typedef struct MdoAgentSessionOptions {
    uint32 Size;
    const char* AgentId;          /* NULL selects mdo.default. */
    const char* ModelId;          /* NULL inherits the Agent/global default. */
    MdoModelProtocol Protocol;    /* zero selects the model default. */
    const char* ReasoningEffort;  /* NULL inherits Agent/global/model policy. */
    uint32 MaxOutputTokens;       /* zero inherits the selected profile. */
    const char* WorkspaceRoot;    /* NULL selects the current directory. */
    const char* SessionPath;      /* optional xllm-session snapshot path. */
    const char* JournalPath;      /* optional write-ahead journal path. */
    const char* ArtifactDirectory;
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
MdoAgentSession* MdoAgentSessionCreateWithRuntime(xwork_runtime* Runtime,
    const MdoAgentSessionOptions* Options, xwork_error* Error);
MdoAgentSession* MdoAgentSessionRef(MdoAgentSession* Session);
void MdoAgentSessionRelease(MdoAgentSession* Session);
bool MdoAgentSessionGetInfo(const MdoAgentSession* Session,
    MdoAgentSessionInfo* Info);

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
