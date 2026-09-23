#include <stdio.h>
#include <string.h>

#include "../../include/mdo/mcp.h"
#include "../../include/mdo/modules.h"
#include "../../include/mdo/operations.h"

#define MDO_OPERATION_LIMIT 64u
#define MDO_OPERATION_QUEUE_LIMIT 32u

typedef struct MdoOperationEntry {
    bool Occupied;
    MdoOperationInfo Info;
    xfuture* Future;
} MdoOperationEntry;

typedef struct MdoOperationJob {
    char Id[MDO_OPERATION_ID_CAPACITY];
    char Target[128];
} MdoOperationJob;

typedef struct MdoOperationStateData {
    xmutex* Lock;
    xtaskpool* Pool;
    MdoOperationEntry Entries[MDO_OPERATION_LIMIT];
    uint64 NextSequence;
    uint64 FallbackId;
    bool Initialized;
    bool Stopping;
} MdoOperationStateData;

static MdoOperationStateData g_MdoOperations;

static void MdoOperationError(cstr Message)
{
    xerror* Error = xrtErrorCreate(XERR_STATE, "mdo.operation", 1,
        Message != NULL ? Message : "Operation failed");
    if ( Error != NULL ) xrtSetErrorTake(Error);
}

static void MdoOperationJobFree(ptr Value, ptr Context)
{
    (void)Context;
    xrtFree(Value);
}

static bool MdoOperationTerminal(MdoOperationState State)
{
    return State == MDO_OPERATION_SUCCEEDED ||
        State == MDO_OPERATION_FAILED || State == MDO_OPERATION_CANCELLED;
}

static MdoOperationEntry* MdoOperationFindLocked(cstr Id)
{
    size_t Index;
    for ( Index = 0u; Index < MDO_OPERATION_LIMIT; Index++ ) {
        MdoOperationEntry* Entry = &g_MdoOperations.Entries[Index];
        if ( Entry->Occupied && strcmp(Entry->Info.Id, Id) == 0 )
            return Entry;
    }
    return NULL;
}

static bool MdoOperationCopyInfo(MdoOperationInfo* Target,
    const MdoOperationInfo* Source)
{
    uint32 Size;
    if ( Target == NULL || Target->Size < sizeof(*Target) || Source == NULL )
        return false;
    Size = Target->Size;
    *Target = *Source;
    Target->Size = Size;
    return true;
}

static bool MdoOperationId(char Output[MDO_OPERATION_ID_CAPACITY])
{
    static const char Hex[] = "0123456789abcdef";
    unsigned char Random[12];
    unsigned Attempt;
    size_t Index;
    for ( Attempt = 0u; Attempt < 4u; Attempt++ ) {
        if ( !xrtSecureRandom(Random, sizeof(Random)) ) break;
        memcpy(Output, "op-", 3u);
        for ( Index = 0u; Index < sizeof(Random); Index++ ) {
            Output[3u + Index * 2u] = Hex[Random[Index] >> 4u];
            Output[4u + Index * 2u] = Hex[Random[Index] & 15u];
        }
        Output[3u + sizeof(Random) * 2u] = '\0';
        if ( MdoOperationFindLocked(Output) == NULL ) return true;
    }
    do {
        int Count;
        if ( g_MdoOperations.FallbackId == UINT64_MAX ) return false;
        g_MdoOperations.FallbackId++;
        Count = snprintf(Output, MDO_OPERATION_ID_CAPACITY, "op-%016llx",
            (unsigned long long)g_MdoOperations.FallbackId);
        if ( Count <= 0 || (size_t)Count >= MDO_OPERATION_ID_CAPACITY )
            return false;
    } while ( MdoOperationFindLocked(Output) != NULL );
    return true;
}

static MdoOperationEntry* MdoOperationSlotLocked(void)
{
    MdoOperationEntry* Oldest = NULL;
    size_t Index;
    for ( Index = 0u; Index < MDO_OPERATION_LIMIT; Index++ ) {
        MdoOperationEntry* Entry = &g_MdoOperations.Entries[Index];
        if ( !Entry->Occupied ) return Entry;
        if ( MdoOperationTerminal(Entry->Info.State) &&
             (Oldest == NULL ||
              Entry->Info.Sequence < Oldest->Info.Sequence) ) Oldest = Entry;
    }
    if ( Oldest != NULL ) {
        if ( Oldest->Future != NULL ) xrtFutureDestroy(Oldest->Future);
        memset(Oldest, 0, sizeof(*Oldest));
    }
    return Oldest;
}

static void MdoOperationFinish(cstr Id, MdoOperationState State,
    uint64 Generation, size_t Items, size_t Secondary, size_t Tertiary,
    size_t Diagnostics, cstr Message)
{
    MdoOperationEntry* Entry;
    if ( !xrtMutexLock(g_MdoOperations.Lock) ) return;
    Entry = MdoOperationFindLocked(Id);
    if ( Entry != NULL && !MdoOperationTerminal(Entry->Info.State) ) {
        Entry->Info.State = State;
        Entry->Info.EndedAt = xrtNow();
        Entry->Info.Generation = Generation;
        Entry->Info.ItemCount = Items;
        Entry->Info.SecondaryCount = Secondary;
        Entry->Info.TertiaryCount = Tertiary;
        Entry->Info.DiagnosticCount = Diagnostics;
        snprintf(Entry->Info.Message, sizeof(Entry->Info.Message), "%s",
            Message != NULL ? Message : "");
    }
    (void)xrtMutexUnlock(g_MdoOperations.Lock);
}

static void MdoOperationFinishMcp(cstr Id, MdoOperationState State,
    const MdoMcpServerStatus* Status, cstr Message)
{
    MdoOperationEntry* Entry;
    if ( !xrtMutexLock(g_MdoOperations.Lock) ) return;
    Entry = MdoOperationFindLocked(Id);
    if ( Entry != NULL && !MdoOperationTerminal(Entry->Info.State) ) {
        Entry->Info.State = State;
        Entry->Info.EndedAt = xrtNow();
        if ( Status != NULL ) {
            Entry->Info.Generation = Status->CatalogGeneration;
            Entry->Info.AuxiliaryGeneration = Status->SchemaGeneration;
            Entry->Info.CompletedCount = Status->RequestsCompleted;
            Entry->Info.ItemCount = Status->DiscoveredToolCount;
            Entry->Info.RuntimeState = (uint32)Status->State;
            Entry->Info.Enabled = Status->Enabled;
            Entry->Info.Connected = Status->Connected;
            Entry->Info.ToolsDiscovered = Status->ToolsDiscovered;
        }
        snprintf(Entry->Info.Message, sizeof(Entry->Info.Message), "%s",
            Message != NULL ? Message : "");
    }
    (void)xrtMutexUnlock(g_MdoOperations.Lock);
}

static xtaskoutcome MdoOperationRun(xcancel* Cancel, ptr Data,
    xtaskvalue* Result)
{
    MdoOperationJob* Job = (MdoOperationJob*)Data;
    MdoOperationEntry* Entry;
    MdoOperationKind Kind;
    bool Cancelled = false;

    (void)Result;
    if ( Job == NULL || g_MdoOperations.Lock == NULL ||
         !xrtMutexLock(g_MdoOperations.Lock) ) return XTASK_FAILED;
    Entry = MdoOperationFindLocked(Job->Id);
    if ( Entry == NULL || g_MdoOperations.Stopping ||
         Entry->Info.CancelRequested ||
         Entry->Info.State == MDO_OPERATION_CANCELLED ||
         xrtCancelRequested(Cancel) ) {
        if ( Entry != NULL && !MdoOperationTerminal(Entry->Info.State) ) {
            Entry->Info.State = MDO_OPERATION_CANCELLED;
            Entry->Info.EndedAt = xrtNow();
            snprintf(Entry->Info.Message, sizeof(Entry->Info.Message),
                "Operation was cancelled before execution");
        }
        (void)xrtMutexUnlock(g_MdoOperations.Lock);
        return XTASK_CANCELLED;
    }
    Entry->Info.State = MDO_OPERATION_RUNNING;
    Entry->Info.StartedAt = xrtNow();
    Kind = Entry->Info.Kind;
    (void)xrtMutexUnlock(g_MdoOperations.Lock);

    if ( xrtCancelRequested(Cancel) ) Cancelled = true;
    if ( Cancelled ) {
        MdoOperationFinish(Job->Id, MDO_OPERATION_CANCELLED, 0u, 0u, 0u,
            0u, 0u, "Operation was cancelled before execution");
        return XTASK_CANCELLED;
    }
    if ( Kind == MDO_OPERATION_MODULE_RELOAD ) {
        MdoModuleCatalog* Catalog;
        MdoModuleDiagnostics* Diagnostics;
        uint64 Generation;
        size_t Modules;
        size_t Tools;
        size_t Agents;
        size_t DiagnosticCount;
        bool Ok = MdoModuleManagerReload();
        if ( !Ok ) {
            MdoOperationFinish(Job->Id, MDO_OPERATION_FAILED, 0u, 0u, 0u,
                0u, 0u, "Module reload failed; the previous generation remains active");
            MdoOperationError("Module reload operation failed");
            return XTASK_FAILED;
        }
        Catalog = MdoModuleCatalogSnapshot();
        Diagnostics = MdoModuleDiagnosticsSnapshot();
        if ( Catalog == NULL || Diagnostics == NULL ) {
            MdoModuleDiagnosticsRelease(Diagnostics);
            MdoModuleCatalogRelease(Catalog);
            MdoOperationFinish(Job->Id, MDO_OPERATION_FAILED, 0u, 0u, 0u,
                0u, 0u, "Module reload completed but its result is unavailable");
            MdoOperationError("Module reload result unavailable");
            return XTASK_FAILED;
        }
        Generation = MdoModuleManagerGeneration();
        Modules = MdoModuleCatalogModuleCount(Catalog);
        Tools = MdoModuleCatalogToolCount(Catalog);
        Agents = MdoModuleCatalogAgentCount(Catalog);
        DiagnosticCount = MdoModuleDiagnosticsCount(Diagnostics);
        MdoModuleDiagnosticsRelease(Diagnostics);
        MdoModuleCatalogRelease(Catalog);
        MdoOperationFinish(Job->Id, MDO_OPERATION_SUCCEEDED, Generation,
            Modules, Tools, Agents, DiagnosticCount,
            "Module catalog reloaded");
        return XTASK_SUCCESS;
    }
    if ( Kind == MDO_OPERATION_MCP_REFRESH ) {
        MdoMcpCatalog* Catalog = MdoMcpCatalogSnapshot();
        MdoMcpServerInfo Server;
        MdoMcpServerStatus Status;
        xwork_error Error;
        uint64 Timeout;
        bool Ok;
        memset(&Server, 0, sizeof(Server)); Server.Size = sizeof(Server);
        memset(&Status, 0, sizeof(Status)); Status.Size = sizeof(Status);
        memset(&Error, 0, sizeof(Error));
        if ( Catalog == NULL ||
             !MdoMcpCatalogFind(Catalog, Job->Target, &Server) ) {
            MdoMcpCatalogRelease(Catalog);
            MdoOperationFinishMcp(Job->Id, MDO_OPERATION_FAILED, NULL,
                "MCP server does not exist in the active catalog");
            MdoOperationError("MCP refresh target does not exist");
            return XTASK_FAILED;
        }
        Timeout = (uint64)Server.RequestTimeoutMilliseconds * 1000u;
        MdoMcpCatalogRelease(Catalog);
        Ok = MdoMcpManagerRefresh(Job->Target, Cancel,
            xrtDeadlineAfter(Timeout), &Error);
        if ( !Ok ) {
            if ( xrtCancelRequested(Cancel) ) {
                MdoOperationFinishMcp(Job->Id, MDO_OPERATION_CANCELLED,
                    NULL, "MCP refresh was cancelled");
                return XTASK_CANCELLED;
            }
            Ok = MdoMcpManagerGetStatus(Job->Target, &Status);
            MdoOperationFinishMcp(Job->Id, MDO_OPERATION_FAILED,
                Ok ? &Status : NULL,
                "MCP refresh failed; the server remains available for retry");
            MdoOperationError("MCP refresh operation failed");
            return XTASK_FAILED;
        }
        if ( !MdoMcpManagerGetStatus(Job->Target, &Status) ) {
            MdoOperationFinishMcp(Job->Id, MDO_OPERATION_FAILED, NULL,
                "MCP refresh completed but its status is unavailable");
            MdoOperationError("MCP refresh status unavailable");
            return XTASK_FAILED;
        }
        MdoOperationFinishMcp(Job->Id, MDO_OPERATION_SUCCEEDED, &Status,
            "MCP server connected and tool discovery completed");
        return XTASK_SUCCESS;
    }
    MdoOperationFinish(Job->Id, MDO_OPERATION_FAILED, 0u, 0u, 0u, 0u, 0u,
        "Unsupported operation kind");
    MdoOperationError("Unsupported operation kind");
    return XTASK_FAILED;
}

bool MdoOperationManagerInit(void)
{
    xtaskpoolconfig Config;
    if ( g_MdoOperations.Initialized ) return true;
    memset(&g_MdoOperations, 0, sizeof(g_MdoOperations));
    g_MdoOperations.Lock = xrtMutexCreate();
    if ( g_MdoOperations.Lock == NULL ) return false;
    memset(&Config, 0, sizeof(Config));
    Config.Threads = 1u;
    Config.QueueLimit = MDO_OPERATION_QUEUE_LIMIT;
    g_MdoOperations.Pool = xrtTaskPoolCreate(&Config);
    if ( g_MdoOperations.Pool == NULL ) {
        (void)xrtMutexDestroy(g_MdoOperations.Lock);
        memset(&g_MdoOperations, 0, sizeof(g_MdoOperations));
        return false;
    }
    g_MdoOperations.Initialized = true;
    return true;
}

void MdoOperationManagerUnit(void)
{
    xtaskpool* Pool = g_MdoOperations.Pool;
    xmutex* Lock = g_MdoOperations.Lock;
    size_t Index;
    if ( Lock == NULL ) return;
    if ( xrtMutexLock(Lock) ) {
        g_MdoOperations.Stopping = true;
        g_MdoOperations.Initialized = false;
        (void)xrtMutexUnlock(Lock);
    }
    if ( Pool != NULL ) {
        (void)xrtTaskPoolCancel(Pool);
        (void)xrtTaskPoolWait(Pool);
    }
    if ( xrtMutexLock(Lock) ) {
        for ( Index = 0u; Index < MDO_OPERATION_LIMIT; Index++ ) {
            xrtFutureDestroy(g_MdoOperations.Entries[Index].Future);
            g_MdoOperations.Entries[Index].Future = NULL;
        }
        (void)xrtMutexUnlock(Lock);
    }
    if ( Pool != NULL ) (void)xrtTaskPoolDestroy(Pool);
    memset(&g_MdoOperations, 0, sizeof(g_MdoOperations));
    (void)xrtMutexDestroy(Lock);
}

static bool MdoOperationStart(MdoOperationKind Kind, cstr Target,
    MdoOperationInfo* Info)
{
    MdoOperationEntry* Entry;
    MdoOperationJob* Job;
    xtaskargs Arguments;
    xfuture* Future;
    bool Copied;

    if ( Info == NULL || Info->Size < sizeof(*Info) ||
         g_MdoOperations.Lock == NULL ) return false;
    Job = (MdoOperationJob*)xrtCalloc(1u, sizeof(*Job));
    if ( Job == NULL ) return false;
    if ( !xrtMutexLock(g_MdoOperations.Lock) ) {
        xrtFree(Job);
        return false;
    }
    if ( !g_MdoOperations.Initialized || g_MdoOperations.Stopping ) {
        (void)xrtMutexUnlock(g_MdoOperations.Lock);
        xrtFree(Job);
        return false;
    }
    if ( g_MdoOperations.NextSequence == UINT64_MAX ) {
        (void)xrtMutexUnlock(g_MdoOperations.Lock);
        xrtFree(Job);
        MdoOperationError("Operation sequence is exhausted");
        return false;
    }
    Entry = MdoOperationSlotLocked();
    if ( Entry == NULL || !MdoOperationId(Job->Id) ) {
        (void)xrtMutexUnlock(g_MdoOperations.Lock);
        xrtFree(Job);
        MdoOperationError("Operation capacity is exhausted");
        return false;
    }
    memset(Entry, 0, sizeof(*Entry));
    Entry->Occupied = true;
    Entry->Info.Size = sizeof(Entry->Info);
    Entry->Info.Sequence = ++g_MdoOperations.NextSequence;
    Entry->Info.Kind = Kind;
    Entry->Info.State = MDO_OPERATION_PENDING;
    Entry->Info.CreatedAt = xrtNow();
    snprintf(Entry->Info.Id, sizeof(Entry->Info.Id), "%s", Job->Id);
    if ( Target != NULL ) {
        snprintf(Job->Target, sizeof(Job->Target), "%s", Target);
        snprintf(Entry->Info.Target, sizeof(Entry->Info.Target), "%s",
            Target);
    }
    snprintf(Entry->Info.Message, sizeof(Entry->Info.Message), "%s",
        Kind == MDO_OPERATION_MODULE_RELOAD ? "Module reload is queued" :
        "MCP refresh is queued");
    memset(&Arguments, 0, sizeof(Arguments));
    Arguments.Destroy = MdoOperationJobFree;
    Future = xrtTaskSubmit(g_MdoOperations.Pool, MdoOperationRun, Job,
        &Arguments);
    if ( Future == NULL ) {
        memset(Entry, 0, sizeof(*Entry));
        (void)xrtMutexUnlock(g_MdoOperations.Lock);
        xrtFree(Job);
        return false;
    }
    Entry->Future = Future;
    Copied = MdoOperationCopyInfo(Info, &Entry->Info);
    (void)xrtMutexUnlock(g_MdoOperations.Lock);
    return Copied;
}

bool MdoOperationStartModuleReload(MdoOperationInfo* Info)
{
    return MdoOperationStart(MDO_OPERATION_MODULE_RELOAD, NULL, Info);
}

bool MdoOperationStartMcpRefresh(cstr ServerId, MdoOperationInfo* Info)
{
    MdoMcpServerStatus Status;
    if ( ServerId == NULL || ServerId[0] == '\0' ||
         strlen(ServerId) >= sizeof(((MdoOperationInfo*)0)->Target) )
        return false;
    memset(&Status, 0, sizeof(Status)); Status.Size = sizeof(Status);
    if ( !MdoMcpManagerGetStatus(ServerId, &Status) ) return false;
    return MdoOperationStart(MDO_OPERATION_MCP_REFRESH, ServerId, Info);
}

bool MdoOperationGet(const char* Id, MdoOperationInfo* Info)
{
    MdoOperationEntry* Entry;
    bool Result;
    if ( Id == NULL || Info == NULL || Info->Size < sizeof(*Info) ||
         g_MdoOperations.Lock == NULL ||
         !xrtMutexLock(g_MdoOperations.Lock) ) return false;
    Entry = MdoOperationFindLocked(Id);
    Result = Entry != NULL && MdoOperationCopyInfo(Info, &Entry->Info);
    (void)xrtMutexUnlock(g_MdoOperations.Lock);
    return Result;
}

size_t MdoOperationList(MdoOperationInfo* Items, size_t Capacity)
{
    uint64 Before = UINT64_MAX;
    size_t Count = 0u;
    if ( (Items == NULL && Capacity != 0u) || g_MdoOperations.Lock == NULL ||
         !xrtMutexLock(g_MdoOperations.Lock) ) return 0u;
    while ( Count < Capacity ) {
        const MdoOperationEntry* Best = NULL;
        size_t Index;
        for ( Index = 0u; Index < MDO_OPERATION_LIMIT; Index++ ) {
            const MdoOperationEntry* Entry = &g_MdoOperations.Entries[Index];
            if ( Entry->Occupied && Entry->Info.Sequence < Before &&
                 (Best == NULL ||
                  Entry->Info.Sequence > Best->Info.Sequence) ) Best = Entry;
        }
        if ( Best == NULL ) break;
        if ( Items[Count].Size < sizeof(Items[Count]) ||
             !MdoOperationCopyInfo(&Items[Count], &Best->Info) ) break;
        Before = Best->Info.Sequence;
        Count++;
    }
    (void)xrtMutexUnlock(g_MdoOperations.Lock);
    return Count;
}

bool MdoOperationCancel(const char* Id, MdoOperationInfo* Info)
{
    MdoOperationEntry* Entry;
    xfuture* Future = NULL;
    bool Result;
    if ( Id == NULL || Info == NULL || Info->Size < sizeof(*Info) ||
         g_MdoOperations.Lock == NULL ||
         !xrtMutexLock(g_MdoOperations.Lock) ) return false;
    Entry = MdoOperationFindLocked(Id);
    if ( Entry == NULL ) {
        (void)xrtMutexUnlock(g_MdoOperations.Lock);
        return false;
    }
    if ( !MdoOperationTerminal(Entry->Info.State) ) {
        Entry->Info.CancelRequested = true;
        Future = xrtFutureRef(Entry->Future);
    }
    (void)xrtMutexUnlock(g_MdoOperations.Lock);
    if ( Future != NULL ) {
        (void)xrtFutureCancel(Future);
        xrtFutureDestroy(Future);
    }
    if ( !xrtMutexLock(g_MdoOperations.Lock) ) return false;
    Entry = MdoOperationFindLocked(Id);
    if ( Entry != NULL && Entry->Info.State == MDO_OPERATION_PENDING ) {
        Entry->Info.State = MDO_OPERATION_CANCELLED;
        Entry->Info.EndedAt = xrtNow();
        snprintf(Entry->Info.Message, sizeof(Entry->Info.Message),
            "Operation was cancelled before execution");
    }
    Result = Entry != NULL && MdoOperationCopyInfo(Info, &Entry->Info);
    (void)xrtMutexUnlock(g_MdoOperations.Lock);
    return Result;
}
