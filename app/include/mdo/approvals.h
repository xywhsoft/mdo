#ifndef MDO_APPROVALS_H
#define MDO_APPROVALS_H

#include <xsbase.h>
#include <xwork.h>

#define MDO_APPROVAL_PENDING_MAX 32u
#define MDO_APPROVAL_API_LIMIT 4u
#define MDO_APPROVAL_TOOL_CAPACITY 129u
#define MDO_APPROVAL_CALL_ID_CAPACITY 257u
#define MDO_APPROVAL_ARGUMENTS_CAPACITY (16u * 1024u + 1u)
#define MDO_APPROVAL_WORKSPACE_CAPACITY 4097u
#define MDO_APPROVAL_RESOURCE_MAX 8u
#define MDO_APPROVAL_RESOURCE_CAPACITY 4097u

typedef struct MdoApprovalSnapshot MdoApprovalSnapshot;

/* Owned by one Agent runtime (one interactive turn or scheduled execution).
 * AutoApprove is set from the effective profile before publishing the runtime
 * and remains immutable. Only the approval manager may access AllowRun, under
 * its lock. */
typedef struct MdoApprovalScope {
    bool AllowRun;
    bool AutoApprove;
} MdoApprovalScope;

typedef struct MdoApprovalResourceInfo {
    xwork_resource_kind Kind;
    xwork_resource_access Access;
    char Resource[MDO_APPROVAL_RESOURCE_CAPACITY];
} MdoApprovalResourceInfo;

typedef struct MdoApprovalInfo {
    uint32 Size;
    uint64 RequestId;
    uint64 AgentId;
    uint64 RunId;
    uint64 CatalogGeneration;
    uint64 AgentTurn;
    uint64 Effects;
    xwork_risk_level Risk;
    int64 CreatedAt;
    uint64 CreatedMicroseconds;
    uint64 Deadline; /* xwork request deadline; zero means none. */
    uint64 ExpiresAt; /* earliest request or product wait deadline. */
    size_t ResourceCount;
    char ToolName[MDO_APPROVAL_TOOL_CAPACITY];
    char ToolCallId[MDO_APPROVAL_CALL_ID_CAPACITY];
    char ArgumentsJson[MDO_APPROVAL_ARGUMENTS_CAPACITY];
    char WorkspaceRoot[MDO_APPROVAL_WORKSPACE_CAPACITY];
    MdoApprovalResourceInfo Resources[MDO_APPROVAL_RESOURCE_MAX];
} MdoApprovalInfo;

bool MdoApprovalManagerInit(void);
void MdoApprovalManagerUnit(void);

/* Synchronous xwork callback. UserData is NULL for one-shot decisions or a
 * runtime-owned MdoApprovalScope for grants covering that runtime. Requests
 * that cannot be represented exactly are denied before tool execution;
 * cancellation, timeout, and shutdown also deny. */
xwork_permission_decision MdoApprovalOnPermission(
    void* UserData, const xwork_permission_request* Request);

MdoApprovalSnapshot* MdoApprovalSnapshotCreate(xwork_error* Error);
MdoApprovalSnapshot* MdoApprovalSnapshotRef(MdoApprovalSnapshot* Snapshot);
void MdoApprovalSnapshotRelease(MdoApprovalSnapshot* Snapshot);
size_t MdoApprovalSnapshotCount(const MdoApprovalSnapshot* Snapshot);
bool MdoApprovalSnapshotAt(const MdoApprovalSnapshot* Snapshot, size_t Index,
    MdoApprovalInfo* Info);

/* A pending request accepts one decision. Stale or unknown IDs fail closed. */
bool MdoApprovalDecide(uint64 RequestId,
    xwork_permission_decision Decision, bool AllowRun, xwork_error* Error);

/* Observer runs under the manager lock; enqueue only, never reenter. */
void MdoApprovalObserve(void (*Changed)(void*), void* Data);

#endif
