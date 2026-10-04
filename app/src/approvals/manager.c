#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../include/mdo/approvals.h"

#define MDO_APPROVAL_POLL_MICROSECONDS UINT64_C(250000)
#define MDO_APPROVAL_MAX_WAIT_MICROSECONDS (UINT64_C(5) * 60u * 1000000u)

typedef struct MdoApprovalEntry {
    bool Used;
    xwork_permission_decision Decision;
    MdoApprovalScope* Scope;
    xcancel* Cancel; /* borrowed while the synchronous callback is active. */
    MdoApprovalInfo Info;
} MdoApprovalEntry;

typedef struct MdoApprovalManager {
    xmutex* Lock;
    void (*Observer)(void*);
    void* ObserverData;
    xcond* Changed;
    bool Initialized;
    bool Stopping;
    size_t ActiveCallbacks;
    MdoApprovalEntry Entries[MDO_APPROVAL_PENDING_MAX];
} MdoApprovalManager;

struct MdoApprovalSnapshot {
    xatomic32 Refs;
    size_t Count;
    MdoApprovalInfo* Items;
};

static MdoApprovalManager g_MdoApprovals;

void MdoApprovalObserve(void (*Changed)(void*), void* Data)
{
    if ( g_MdoApprovals.Lock == NULL ) return;
    xrtMutexLock(g_MdoApprovals.Lock);
    g_MdoApprovals.Observer = Changed; g_MdoApprovals.ObserverData = Data;
    xrtMutexUnlock(g_MdoApprovals.Lock);
}

static void MdoApprovalError(xwork_error* Error, xwork_error_code Code,
    const char* Message)
{
    if ( Error == NULL ) return;
    xworkErrorInit(Error);
    Error->eCode = Code;
    (void)snprintf(Error->sMessage, sizeof(Error->sMessage), "%s", Message);
}

static bool MdoApprovalCopy(char* Target, size_t Capacity,
    const char* Source, bool Required)
{
    const char* End;
    size_t Size;
    if ( Source == NULL ) {
        if ( Required ) return false;
        Target[0] = '\0';
        return true;
    }
    End = (const char*)memchr(Source, 0, Capacity);
    if ( End == NULL ) return false;
    Size = (size_t)(End - Source);
    if ( Required && Size == 0u ) return false;
    if ( Size != 0u ) memcpy(Target, Source, Size);
    Target[Size] = '\0';
    return xrtUtf8Valid(xrtStrViewN(Target, Size), NULL);
}

static bool MdoApprovalCapture(MdoApprovalInfo* Info,
    const xwork_permission_request* Request)
{
    size_t Index;
    if ( Info == NULL || Request == NULL || Request->uRequestId == 0u ||
         Request->eRisk < XWORK_RISK_LOW || Request->eRisk > XWORK_RISK_HIGH ||
         (Request->uEffects & ~XWORK_TOOL_EFFECT_ALL) != 0u ||
         Request->iResourceCount > MDO_APPROVAL_RESOURCE_MAX ||
         (Request->iResourceCount != 0u && Request->pResources == NULL) )
        return false;
    memset(Info, 0, sizeof(*Info));
    Info->Size = sizeof(*Info);
    Info->RequestId = Request->uRequestId;
    Info->AgentId = Request->uAgentId;
    Info->RunId = Request->uRunId;
    Info->CatalogGeneration = Request->uCatalogGeneration;
    Info->AgentTurn = Request->uAgentTurn;
    Info->Effects = Request->uEffects;
    Info->Risk = Request->eRisk;
    Info->CreatedAt = xrtNow();
    Info->CreatedMicroseconds = xrtClock();
    Info->Deadline = Request->uDeadline;
    Info->ExpiresAt = xrtDeadlineAfter(MDO_APPROVAL_MAX_WAIT_MICROSECONDS);
    if ( Request->uDeadline != 0u && Request->uDeadline < Info->ExpiresAt )
        Info->ExpiresAt = Request->uDeadline;
    Info->ResourceCount = Request->iResourceCount;
    if ( !MdoApprovalCopy(Info->ToolName, sizeof(Info->ToolName),
             Request->sToolName, true) ||
         !MdoApprovalCopy(Info->ToolCallId, sizeof(Info->ToolCallId),
             Request->sToolCallId, false) ||
         !MdoApprovalCopy(Info->ArgumentsJson, sizeof(Info->ArgumentsJson),
             Request->sArgumentsJson, false) ||
         !MdoApprovalCopy(Info->WorkspaceRoot, sizeof(Info->WorkspaceRoot),
             Request->sWorkspaceRoot, false) ) return false;
    for ( Index = 0u; Index < Request->iResourceCount; ++Index ) {
        const xwork_permission_resource* Source = &Request->pResources[Index];
        MdoApprovalResourceInfo* Target = &Info->Resources[Index];
        if ( Source->eKind <= XWORK_RESOURCE_NONE ||
             Source->eKind > XWORK_RESOURCE_AGENT ||
             (Source->uAccess & ~(XWORK_RESOURCE_ACCESS_READ |
                 XWORK_RESOURCE_ACCESS_WRITE | XWORK_RESOURCE_ACCESS_EXECUTE |
                 XWORK_RESOURCE_ACCESS_CONTROL | XWORK_RESOURCE_ACCESS_CONNECT |
                 XWORK_RESOURCE_ACCESS_USE)) != 0u ||
             !MdoApprovalCopy(Target->Resource, sizeof(Target->Resource),
                 Source->sResource, true) ) return false;
        Target->Kind = Source->eKind;
        Target->Access = Source->uAccess;
    }
    return true;
}

static size_t MdoApprovalFindLocked(uint64 RequestId)
{
    size_t Index;
    for ( Index = 0u; Index < MDO_APPROVAL_PENDING_MAX; ++Index ) {
        if ( g_MdoApprovals.Entries[Index].Used &&
             g_MdoApprovals.Entries[Index].Info.RequestId == RequestId )
            return Index;
    }
    return SIZE_MAX;
}

static size_t MdoApprovalFreeLocked(void)
{
    size_t Index;
    for ( Index = 0u; Index < MDO_APPROVAL_PENDING_MAX; ++Index ) {
        if ( !g_MdoApprovals.Entries[Index].Used ) return Index;
    }
    return SIZE_MAX;
}

bool MdoApprovalManagerInit(void)
{
    if ( g_MdoApprovals.Initialized ) return true;
    memset(&g_MdoApprovals, 0, sizeof(g_MdoApprovals));
    g_MdoApprovals.Lock = xrtMutexCreate();
    g_MdoApprovals.Changed = xrtCondCreate();
    if ( g_MdoApprovals.Lock == NULL || g_MdoApprovals.Changed == NULL ) {
        if ( g_MdoApprovals.Changed != NULL )
            (void)xrtCondDestroy(g_MdoApprovals.Changed);
        if ( g_MdoApprovals.Lock != NULL )
            (void)xrtMutexDestroy(g_MdoApprovals.Lock);
        memset(&g_MdoApprovals, 0, sizeof(g_MdoApprovals));
        return false;
    }
    g_MdoApprovals.Initialized = true;
    return true;
}

void MdoApprovalManagerUnit(void)
{
    size_t Index;
    if ( !g_MdoApprovals.Initialized || g_MdoApprovals.Lock == NULL ) return;
    if ( !xrtMutexLock(g_MdoApprovals.Lock) ) return;
    g_MdoApprovals.Stopping = true;
    for ( Index = 0u; Index < MDO_APPROVAL_PENDING_MAX; ++Index ) {
        if ( g_MdoApprovals.Entries[Index].Used )
            g_MdoApprovals.Entries[Index].Decision = XWORK_PERMISSION_DENY;
    }
    if ( g_MdoApprovals.Observer != NULL ) g_MdoApprovals.Observer(g_MdoApprovals.ObserverData);
    (void)xrtCondBroadcast(g_MdoApprovals.Changed);
    while ( g_MdoApprovals.ActiveCallbacks != 0u )
        (void)xrtCondWait(g_MdoApprovals.Changed, g_MdoApprovals.Lock);
    (void)xrtMutexUnlock(g_MdoApprovals.Lock);
    (void)xrtCondDestroy(g_MdoApprovals.Changed);
    (void)xrtMutexDestroy(g_MdoApprovals.Lock);
    memset(&g_MdoApprovals, 0, sizeof(g_MdoApprovals));
}

xwork_permission_decision MdoApprovalOnPermission(void* UserData,
    const xwork_permission_request* Request)
{
    MdoApprovalInfo Captured;
    MdoApprovalEntry* Entry;
    xwork_permission_decision Result = XWORK_PERMISSION_DENY;
    size_t Index;
    MdoApprovalScope* Scope = (MdoApprovalScope*)UserData;
    /* Use the runtime's effective full-access profile rather than creating a
     * product prompt before xwork can apply AUTO. The callback remains bound
     * for nested agents; xwork enforces read-only agents before invoking it.
     * External permission callbacks keep their own policy and user data. */
    if ( Request != NULL && ((Scope != NULL && Scope->AutoApprove) ||
         (Request->uEffects &
          ~((xwork_tool_effects)XWORK_TOOL_EFFECT_READ)) == 0u) )
        return XWORK_PERMISSION_DEFAULT;
    if ( !MdoApprovalCapture(&Captured, Request) ||
         !g_MdoApprovals.Initialized || g_MdoApprovals.Lock == NULL ||
         !xrtMutexLock(g_MdoApprovals.Lock) ) return XWORK_PERMISSION_DENY;
    if ( g_MdoApprovals.Stopping ||
         MdoApprovalFindLocked(Request->uRequestId) != SIZE_MAX ) goto unlock;
    if ( Request->pCancel != NULL &&
         xrtCancelRequested(Request->pCancel) ) goto unlock;
    if ( Request->uDeadline != 0u &&
         xrtDeadlineExpired(Request->uDeadline) ) goto unlock;
    if ( Scope != NULL && Scope->AllowRun ) {
        Result = XWORK_PERMISSION_ALLOW;
        goto unlock;
    }
    Index = MdoApprovalFreeLocked();
    if ( Index == SIZE_MAX ) goto unlock;
    Entry = &g_MdoApprovals.Entries[Index];
    memset(Entry, 0, sizeof(*Entry));
    Entry->Used = true;
    Entry->Decision = XWORK_PERMISSION_DEFAULT;
    Entry->Scope = Scope;
    Entry->Cancel = Request->pCancel;
    Entry->Info = Captured;
    ++g_MdoApprovals.ActiveCallbacks;
    if ( g_MdoApprovals.Observer != NULL ) g_MdoApprovals.Observer(g_MdoApprovals.ObserverData);
    (void)xrtCondBroadcast(g_MdoApprovals.Changed);
    while ( Entry->Decision == XWORK_PERMISSION_DEFAULT ) {
        uint64 Now = xrtClock();
        uint64 Wake = Now > UINT64_MAX - MDO_APPROVAL_POLL_MICROSECONDS ?
            UINT64_MAX : Now + MDO_APPROVAL_POLL_MICROSECONDS;
        if ( g_MdoApprovals.Stopping ||
             (Request->pCancel != NULL &&
              xrtCancelRequested(Request->pCancel)) ||
             (Request->uDeadline != 0u &&
              xrtDeadlineExpired(Request->uDeadline)) ||
             xrtDeadlineExpired(Entry->Info.ExpiresAt) ) {
            Entry->Decision = XWORK_PERMISSION_DENY;
            break;
        }
        if ( Request->uDeadline != 0u && Request->uDeadline < Wake )
            Wake = Request->uDeadline;
        if ( Entry->Info.ExpiresAt < Wake ) Wake = Entry->Info.ExpiresAt;
        if ( xrtCondWaitUntil(g_MdoApprovals.Changed,
                g_MdoApprovals.Lock, Wake) == XWAIT_ERROR ) {
            Entry->Decision = XWORK_PERMISSION_DENY;
            break;
        }
    }
    Result = Entry->Decision == XWORK_PERMISSION_ALLOW &&
        !g_MdoApprovals.Stopping &&
        (Request->pCancel == NULL ||
         !xrtCancelRequested(Request->pCancel)) &&
        (Request->uDeadline == 0u ||
         !xrtDeadlineExpired(Request->uDeadline)) &&
        !xrtDeadlineExpired(Entry->Info.ExpiresAt) ?
        XWORK_PERMISSION_ALLOW : XWORK_PERMISSION_DENY;
    memset(Entry, 0, sizeof(*Entry));
    --g_MdoApprovals.ActiveCallbacks;
    if ( g_MdoApprovals.Observer != NULL ) g_MdoApprovals.Observer(g_MdoApprovals.ObserverData);
    (void)xrtCondBroadcast(g_MdoApprovals.Changed);
unlock:
    (void)xrtMutexUnlock(g_MdoApprovals.Lock);
    return Result;
}

MdoApprovalSnapshot* MdoApprovalSnapshotCreate(xwork_error* Error)
{
    MdoApprovalSnapshot* Snapshot = NULL;
    size_t Count = 0u;
    size_t Index;
    xworkErrorInit(Error);
    if ( !g_MdoApprovals.Initialized || g_MdoApprovals.Lock == NULL ||
         !xrtMutexLock(g_MdoApprovals.Lock) ) {
        MdoApprovalError(Error, XWORK_ERROR_CONTEXT,
            "approval manager is unavailable");
        return NULL;
    }
    for ( Index = 0u; Index < MDO_APPROVAL_PENDING_MAX; ++Index ) {
        if ( g_MdoApprovals.Entries[Index].Used &&
             g_MdoApprovals.Entries[Index].Decision ==
                XWORK_PERMISSION_DEFAULT ) ++Count;
    }
    Snapshot = (MdoApprovalSnapshot*)xrtCalloc(1u, sizeof(*Snapshot));
    if ( Snapshot == NULL ) goto memory;
    xrtAtomic32Init(&Snapshot->Refs, 1u);
    if ( Count != 0u ) {
        Snapshot->Items = (MdoApprovalInfo*)xrtCalloc(Count,
            sizeof(*Snapshot->Items));
        if ( Snapshot->Items == NULL ) goto memory;
    }
    for ( Index = 0u; Index < MDO_APPROVAL_PENDING_MAX; ++Index ) {
        if ( g_MdoApprovals.Entries[Index].Used &&
             g_MdoApprovals.Entries[Index].Decision ==
                XWORK_PERMISSION_DEFAULT )
            Snapshot->Items[Snapshot->Count++] =
                g_MdoApprovals.Entries[Index].Info;
    }
    (void)xrtMutexUnlock(g_MdoApprovals.Lock);
    for ( Index = 1u; Index < Snapshot->Count; ++Index ) {
        MdoApprovalInfo Item = Snapshot->Items[Index];
        size_t Position = Index;
        while ( Position != 0u &&
                Snapshot->Items[Position - 1u].CreatedMicroseconds >
                    Item.CreatedMicroseconds ) {
            Snapshot->Items[Position] = Snapshot->Items[Position - 1u];
            --Position;
        }
        Snapshot->Items[Position] = Item;
    }
    return Snapshot;
memory:
    (void)xrtMutexUnlock(g_MdoApprovals.Lock);
    MdoApprovalSnapshotRelease(Snapshot);
    MdoApprovalError(Error, XWORK_ERROR_OUT_OF_MEMORY,
        "cannot allocate approval snapshot");
    return NULL;
}

MdoApprovalSnapshot* MdoApprovalSnapshotRef(MdoApprovalSnapshot* Snapshot)
{
    uint32 Refs;
    if ( Snapshot == NULL ) return NULL;
    Refs = xrtAtomic32Load(&Snapshot->Refs, XMEMORY_ACQUIRE);
    for ( ; ; ) {
        uint32 Expected = Refs;
        if ( Refs == 0u || Refs == UINT32_MAX ) return NULL;
        if ( xrtAtomic32CompareExchange(&Snapshot->Refs, &Expected, Refs + 1u,
                XMEMORY_ACQ_REL, XMEMORY_ACQUIRE) ) return Snapshot;
        Refs = Expected;
    }
}

void MdoApprovalSnapshotRelease(MdoApprovalSnapshot* Snapshot)
{
    uint32 Previous;
    if ( Snapshot == NULL ) return;
    Previous = xrtAtomic32FetchSub(&Snapshot->Refs, 1u, XMEMORY_ACQ_REL);
    if ( Previous > 1u ) return;
    if ( Previous == 0u ) abort();
    xrtFree(Snapshot->Items);
    memset(Snapshot, 0, sizeof(*Snapshot));
    xrtFree(Snapshot);
}

size_t MdoApprovalSnapshotCount(const MdoApprovalSnapshot* Snapshot)
{
    return Snapshot != NULL ? Snapshot->Count : 0u;
}

bool MdoApprovalSnapshotAt(const MdoApprovalSnapshot* Snapshot, size_t Index,
    MdoApprovalInfo* Info)
{
    uint32 Size;
    if ( Snapshot == NULL || Index >= Snapshot->Count || Info == NULL ||
         Info->Size < sizeof(*Info) ) return false;
    Size = Info->Size;
    *Info = Snapshot->Items[Index];
    Info->Size = Size;
    return true;
}

bool MdoApprovalDecide(uint64 RequestId,
    xwork_permission_decision Decision, bool AllowRun, xwork_error* Error)
{
    size_t Index;
    xworkErrorInit(Error);
    if ( RequestId == 0u ||
         (Decision != XWORK_PERMISSION_ALLOW &&
          Decision != XWORK_PERMISSION_DENY) ||
         (AllowRun && Decision != XWORK_PERMISSION_ALLOW) ) {
        MdoApprovalError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "invalid approval decision");
        return false;
    }
    if ( !g_MdoApprovals.Initialized || g_MdoApprovals.Lock == NULL ||
         !xrtMutexLock(g_MdoApprovals.Lock) ) {
        MdoApprovalError(Error, XWORK_ERROR_CONTEXT,
            "approval manager is unavailable");
        return false;
    }
    Index = MdoApprovalFindLocked(RequestId);
    if ( g_MdoApprovals.Stopping || Index == SIZE_MAX ||
         g_MdoApprovals.Entries[Index].Decision != XWORK_PERMISSION_DEFAULT ||
         xrtDeadlineExpired(g_MdoApprovals.Entries[Index].Info.ExpiresAt) ||
         (g_MdoApprovals.Entries[Index].Cancel != NULL &&
          xrtCancelRequested(g_MdoApprovals.Entries[Index].Cancel)) ||
         (AllowRun && g_MdoApprovals.Entries[Index].Scope == NULL) ) {
        (void)xrtMutexUnlock(g_MdoApprovals.Lock);
        MdoApprovalError(Error, XWORK_ERROR_POLICY,
            "approval request is no longer pending");
        return false;
    }
    if ( AllowRun ) {
        MdoApprovalScope* Scope = g_MdoApprovals.Entries[Index].Scope;
        Scope->AllowRun = true;
        for ( Index = 0u; Index < MDO_APPROVAL_PENDING_MAX; ++Index ) {
            MdoApprovalEntry* Entry = &g_MdoApprovals.Entries[Index];
            if ( Entry->Used && Entry->Scope == Scope &&
                 Entry->Decision == XWORK_PERMISSION_DEFAULT )
                Entry->Decision = (Entry->Cancel == NULL ||
                    !xrtCancelRequested(Entry->Cancel)) &&
                    !xrtDeadlineExpired(Entry->Info.ExpiresAt) ?
                    XWORK_PERMISSION_ALLOW : XWORK_PERMISSION_DENY;
        }
    } else {
        g_MdoApprovals.Entries[Index].Decision = Decision;
    }
    if ( g_MdoApprovals.Observer != NULL ) g_MdoApprovals.Observer(g_MdoApprovals.ObserverData);
    (void)xrtCondBroadcast(g_MdoApprovals.Changed);
    (void)xrtMutexUnlock(g_MdoApprovals.Lock);
    return true;
}
