#include <stdlib.h>
#include <string.h>

#include "../../include/mdo/runs.h"
#include "../../include/mdo/attachments.h"

#define MDO_RUN_POLL_DEFAULT 50u
#define MDO_RUN_POLL_MIN 25u
#define MDO_RUN_POLL_MAX 5000u
#define MDO_RUN_ACTIVE_DEFAULT 8u
#define MDO_RUN_ACTIVE_MAX 64u
#define MDO_RUN_RETAINED_DEFAULT 64u
#define MDO_RUN_RETAINED_MAX 256u
#define MDO_RUN_TIMEOUT_MAX (24u * 60u * 60u * 1000u)

typedef struct MdoRunEntry {
    MdoRunInfo Info;
    MdoSession* Session;
    MdoAgentRun* Run;
    xcancel* Cancel;
    char* FinalText;
    bool Starting;
} MdoRunEntry;

typedef struct MdoRunCleanup {
    MdoSession* Session;
    MdoAgentRun* Run;
    xcancel* Cancel;
} MdoRunCleanup;

typedef struct MdoRunManagerState {
    xmutex* Lock;
    xcond* Changed;
    xthread* Thread;
    xwork_runtime* Runtime;
    MdoRunEntry* Entries;
    size_t Count;
    size_t Capacity;
    size_t ActiveCount;
    size_t StartingCount;
    MdoRunManagerOptions Options;
    uint64 RunsStarted;
    uint64 RunsCompleted;
    uint64 RunsFailed;
    bool OwnerRetained;
    bool Stopping;
    bool Initialized;
} MdoRunManagerState;

typedef struct MdoRunSnapshotItem {
    MdoRunInfo Info;
    char* FinalText;
} MdoRunSnapshotItem;

struct MdoRunSnapshot {
    xatomic32 Refs;
    MdoRunSnapshotItem* Items;
    size_t Count;
};

static MdoRunManagerState g_MdoRuns;

static void MdoRunsError(xwork_error* Error, xwork_error_code Code,
    const char* Message)
{
    if ( Error == NULL ) return;
    xworkErrorInit(Error);
    Error->eCode = Code;
    snprintf(Error->sMessage, sizeof(Error->sMessage), "%s",
        Message != NULL && Message[0] != '\0' ? Message :
        "interactive run operation failed");
}

static bool MdoRunsTerminal(xwork_run_state State)
{
    return State == XWORK_RUN_SUCCEEDED || State == XWORK_RUN_FAILED ||
        State == XWORK_RUN_CANCELLED || State == XWORK_RUN_TIMED_OUT;
}

static bool MdoRunsIdValid(const char* Text, size_t Capacity)
{
    size_t i;
    size_t Size;
    if ( Text == NULL || Text[0] == '\0' ) return false;
    Size = strlen(Text);
    if ( Size >= Capacity || (Size == 1u && Text[0] == '.') ||
         (Size == 2u && Text[0] == '.' && Text[1] == '.') ) return false;
    for ( i = 0u; i < Size; ++i ) {
        unsigned char Byte = (unsigned char)Text[i];
        if ( (Byte >= 'a' && Byte <= 'z') ||
             (Byte >= 'A' && Byte <= 'Z') ||
             (Byte >= '0' && Byte <= '9') || Byte == '-' || Byte == '_' ||
             (Byte == '.' && i != 0u) ) continue;
        return false;
    }
    return true;
}

static bool MdoRunsPromptValid(const char* Prompt, bool AllowEmpty)
{
    size_t Size;
    if ( Prompt == NULL || (!AllowEmpty && Prompt[0] == '\0') ) return false;
    Size = strlen(Prompt);
    return Size < MDO_RUN_PROMPT_CAPACITY &&
        xrtUtf8Valid(xrtStrViewN(Prompt, Size), NULL);
}

static bool MdoRunsRecoveryTokenValid(MdoAgentSession* Agent,
    const char* ExpectedToken, xwork_error* Error)
{
    xwork_recovery_snapshot* Snapshot;
    char CurrentToken[MDO_AGENT_RECOVERY_TOKEN_CAPACITY];
    bool ResumeRequired = false;

    Snapshot = MdoAgentSessionRecoverySnapshot(Agent, Error);
    if ( Snapshot == NULL ) return false;
    if ( !MdoAgentSessionRecoveryRequired(Agent, &ResumeRequired, Error) ) {
        xworkRecoverySnapshotRelease(Snapshot);
        return false;
    }
    if ( !ResumeRequired || !MdoAgentRecoverySnapshotToken(Snapshot,
            ResumeRequired, CurrentToken) ) {
        xworkRecoverySnapshotRelease(Snapshot);
        MdoRunsError(Error, ResumeRequired ? XWORK_ERROR_CONTEXT :
            XWORK_ERROR_POLICY, ResumeRequired ?
            "cannot fingerprint the current recovery state" :
            "durable session has no interrupted run to resume");
        return false;
    }
    xworkRecoverySnapshotRelease(Snapshot);
    if ( ExpectedToken == NULL || strcmp(CurrentToken, ExpectedToken) != 0 ) {
        MdoRunsError(Error, XWORK_ERROR_POLICY,
            "recovery state changed after it was inspected");
        return false;
    }
    return true;
}

static size_t MdoRunsFindLocked(const char* RunId)
{
    size_t i;
    for ( i = 0u; i < g_MdoRuns.Count; ++i ) {
        if ( strcmp(g_MdoRuns.Entries[i].Info.Id, RunId) == 0 ) return i;
    }
    return SIZE_MAX;
}

static bool MdoRunsGrowLocked(void)
{
    size_t Next;
    MdoRunEntry* Entries;
    if ( g_MdoRuns.Count < g_MdoRuns.Capacity ) return true;
    if ( g_MdoRuns.Capacity >= g_MdoRuns.Options.MaxRetained ) return false;
    Next = g_MdoRuns.Capacity != 0u ? g_MdoRuns.Capacity * 2u : 8u;
    if ( Next > g_MdoRuns.Options.MaxRetained )
        Next = g_MdoRuns.Options.MaxRetained;
    Entries = (MdoRunEntry*)xrtRealloc(g_MdoRuns.Entries,
        Next * sizeof(*Entries));
    if ( Entries == NULL ) return false;
    g_MdoRuns.Entries = Entries;
    g_MdoRuns.Capacity = Next;
    return true;
}

static void MdoRunsRemoveLocked(size_t Index)
{
    MdoRunEntry* Entry = &g_MdoRuns.Entries[Index];
    xrtFree(Entry->FinalText);
    if ( Index + 1u < g_MdoRuns.Count ) {
        memmove(&g_MdoRuns.Entries[Index], &g_MdoRuns.Entries[Index + 1u],
            (g_MdoRuns.Count - Index - 1u) * sizeof(*g_MdoRuns.Entries));
    }
    --g_MdoRuns.Count;
    memset(&g_MdoRuns.Entries[g_MdoRuns.Count], 0,
        sizeof(*g_MdoRuns.Entries));
}

static bool MdoRunsMakeRoomLocked(void)
{
    size_t i;
    if ( g_MdoRuns.Count < g_MdoRuns.Options.MaxRetained ) return true;
    for ( i = 0u; i < g_MdoRuns.Count; ++i ) {
        MdoRunEntry* Entry = &g_MdoRuns.Entries[i];
        if ( !Entry->Starting && Entry->Info.Terminal && Entry->Run == NULL &&
             Entry->Session == NULL && Entry->Cancel == NULL ) {
            MdoRunsRemoveLocked(i);
            return true;
        }
    }
    return false;
}

static bool MdoRunsAssignIdLocked(char Output[MDO_RUN_ID_CAPACITY])
{
    unsigned Attempt;
    for ( Attempt = 0u; Attempt < 8u; ++Attempt ) {
        char* Xid = xrtXidMakeString();
        int Written;
        if ( Xid == NULL ) return false;
        Written = snprintf(Output, MDO_RUN_ID_CAPACITY, "run-%s", Xid);
        xrtFree(Xid);
        if ( Written > 0 && (size_t)Written < MDO_RUN_ID_CAPACITY &&
             MdoRunsFindLocked(Output) == SIZE_MAX ) return true;
    }
    return false;
}

static char* MdoRunsCopyFinal(const char* Text, size_t* Size,
    bool* Truncated)
{
    size_t SourceSize;
    size_t CopySize;
    char* Copy;
    *Size = 0u;
    *Truncated = false;
    if ( Text == NULL ) return NULL;
    SourceSize = strlen(Text);
    CopySize = SourceSize;
    if ( CopySize > MDO_RUN_FINAL_TEXT_LIMIT ) {
        CopySize = MDO_RUN_FINAL_TEXT_LIMIT;
        while ( CopySize != 0u &&
                (((unsigned char)Text[CopySize] & 0xc0u) == 0x80u) )
            --CopySize;
        *Truncated = true;
    }
    Copy = (char*)xrtMalloc(CopySize + 1u);
    if ( Copy == NULL ) return NULL;
    if ( CopySize != 0u ) memcpy(Copy, Text, CopySize);
    Copy[CopySize] = '\0';
    *Size = CopySize;
    return Copy;
}

static void MdoRunsCleanup(MdoRunCleanup* Items, size_t Count,
    bool RequestCancel)
{
    size_t i;
    for ( i = 0u; i < Count; ++i ) {
        if ( RequestCancel && Items[i].Cancel != NULL )
            (void)xrtCancelRequest(Items[i].Cancel);
        MdoAgentRunDestroy(Items[i].Run);
        MdoSessionRelease(Items[i].Session);
        xrtCancelDestroy(Items[i].Cancel);
    }
}

void MdoRunManagerOptionsInit(MdoRunManagerOptions* Options)
{
    if ( Options == NULL ) return;
    memset(Options, 0, sizeof(*Options));
    Options->Size = sizeof(*Options);
    Options->Automatic = true;
    Options->PollMilliseconds = MDO_RUN_POLL_DEFAULT;
    Options->MaxActive = MDO_RUN_ACTIVE_DEFAULT;
    Options->MaxRetained = MDO_RUN_RETAINED_DEFAULT;
}

void MdoRunStartOptionsInit(MdoRunStartOptions* Options)
{
    if ( Options == NULL ) return;
    memset(Options, 0, sizeof(*Options));
    Options->Size = sizeof(*Options);
}

bool MdoRunManagerPump(size_t* Completed, xwork_error* Error)
{
    MdoRunCleanup Cleanup[MDO_RUN_ACTIVE_MAX];
    size_t CleanupCount = 0u;
    size_t CompletedValue = 0u;
    size_t i;
    bool Ok = true;
    xworkErrorInit(Error);
    memset(Cleanup, 0, sizeof(Cleanup));
    if ( !g_MdoRuns.Initialized || g_MdoRuns.Lock == NULL ) {
        MdoRunsError(Error, XWORK_ERROR_CONTEXT,
            "interactive run manager is not initialized");
        return false;
    }
    if ( !xrtMutexLock(g_MdoRuns.Lock) ) {
        MdoRunsError(Error, XWORK_ERROR_CONTEXT,
            "cannot lock the interactive run manager");
        return false;
    }
    if ( g_MdoRuns.Stopping ) {
        MdoRunsError(Error, XWORK_ERROR_CANCELLED,
            "interactive run manager is stopping");
        Ok = false;
        goto unlock;
    }
    for ( i = 0u; i < g_MdoRuns.Count; ++i ) {
        MdoRunEntry* Entry = &g_MdoRuns.Entries[i];
        MdoAgentRunInfo AgentInfo;
        xwork_run_result Result;
        xwork_error WaitError;
        xwork_result WaitResult;
        if ( Entry->Starting || Entry->Run == NULL || Entry->Info.Terminal )
            continue;
        memset(&AgentInfo, 0, sizeof(AgentInfo));
        AgentInfo.Size = sizeof(AgentInfo);
        if ( !MdoAgentRunGetInfo(Entry->Run, &AgentInfo) ) {
            MdoRunsError(Error, XWORK_ERROR_CONTEXT,
                "cannot inspect an interactive Agent run");
            Ok = false;
            break;
        }
        Entry->Info.State = AgentInfo.Run.eState;
        Entry->Info.Result = AgentInfo.Run.eResult;
        Entry->Info.CreatedMicroseconds = AgentInfo.Run.uCreatedUs;
        Entry->Info.StartedMicroseconds = AgentInfo.Run.uStartedUs;
        Entry->Info.EndedMicroseconds = AgentInfo.Run.uEndedUs;
        if ( !MdoRunsTerminal(AgentInfo.Run.eState) ) continue;
        Entry->Info.EndedAt = xrtNow();
        memset(&Result, 0, sizeof(Result));
        xworkErrorInit(&WaitError);
        WaitResult = MdoAgentRunWait(Entry->Run, XRT_DEADLINE_NEVER,
            &Result, &WaitError);
        Entry->Info.Terminal = true;
        Entry->Info.ErrorCode = WaitError.eCode;
        Entry->Info.Result = AgentInfo.Run.eResult;
        Entry->Info.AgentTurns = Result.uAgentTurns;
        Entry->Info.ModelCalls = Result.uModelCalls;
        Entry->Info.ToolCalls = Result.uToolCalls;
        Entry->Info.Compactions = Result.uCompactions;
        if ( Result.sFinalText != NULL ) {
            Entry->FinalText = MdoRunsCopyFinal(Result.sFinalText,
                &Entry->Info.FinalTextBytes,
                &Entry->Info.FinalTextTruncated);
            Entry->Info.FinalTextAvailable = Entry->FinalText != NULL;
            if ( Entry->FinalText == NULL &&
                 Entry->Info.ErrorCode == XWORK_ERROR_NONE )
                Entry->Info.ErrorCode = XWORK_ERROR_OUT_OF_MEMORY;
        }
        xworkRunResultUnit(&Result);
        Cleanup[CleanupCount].Run = Entry->Run;
        Cleanup[CleanupCount].Session = Entry->Session;
        Cleanup[CleanupCount].Cancel = Entry->Cancel;
        ++CleanupCount;
        Entry->Run = NULL;
        Entry->Session = NULL;
        Entry->Cancel = NULL;
        if ( g_MdoRuns.ActiveCount != 0u ) --g_MdoRuns.ActiveCount;
        if ( g_MdoRuns.RunsCompleted != UINT64_MAX )
            ++g_MdoRuns.RunsCompleted;
        if ( WaitResult != XWORK_RESULT_OK &&
             g_MdoRuns.RunsFailed != UINT64_MAX ) ++g_MdoRuns.RunsFailed;
        ++CompletedValue;
    }
unlock:
    (void)xrtMutexUnlock(g_MdoRuns.Lock);
    MdoRunsCleanup(Cleanup, CleanupCount, false);
    if ( Completed != NULL ) *Completed = CompletedValue;
    return Ok;
}

static int32 MdoRunsThread(ptr Data)
{
    (void)Data;
    for ( ; ; ) {
        bool Stop;
        uint32 Poll;
        xwork_error Error;
        if ( !xrtMutexLock(g_MdoRuns.Lock) ) break;
        Stop = g_MdoRuns.Stopping;
        Poll = g_MdoRuns.Options.PollMilliseconds;
        if ( !Stop )
            (void)xrtCondWaitFor(g_MdoRuns.Changed, g_MdoRuns.Lock,
                (uint64)Poll * UINT64_C(1000));
        Stop = g_MdoRuns.Stopping;
        (void)xrtMutexUnlock(g_MdoRuns.Lock);
        if ( Stop ) break;
        (void)MdoRunManagerPump(NULL, &Error);
    }
    return 0;
}

bool MdoRunManagerInit(xwork_runtime* Runtime,
    const MdoRunManagerOptions* Options, xwork_error* Error)
{
    MdoRunManagerOptions Defaults;
    bool OwnerRetained = false;
    xworkErrorInit(Error);
    if ( g_MdoRuns.Initialized ) return true;
    if ( Options == NULL ) {
        MdoRunManagerOptionsInit(&Defaults);
        Options = &Defaults;
    }
    if ( Runtime == NULL || MdoSessionManagerGeneration() == 0u ||
         Options->Size < sizeof(*Options) ||
         Options->PollMilliseconds < MDO_RUN_POLL_MIN ||
         Options->PollMilliseconds > MDO_RUN_POLL_MAX ||
         Options->MaxActive == 0u ||
         Options->MaxActive > MDO_RUN_ACTIVE_MAX ||
         Options->MaxRetained < Options->MaxActive ||
         Options->MaxRetained > MDO_RUN_RETAINED_MAX ||
         ((Options->OnOwnerRetain != NULL) !=
          (Options->OnOwnerRelease != NULL)) ) {
        MdoRunsError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "invalid interactive run manager configuration");
        return false;
    }
    if ( Options->OnOwnerRetain != NULL ) {
        OwnerRetained = Options->OnOwnerRetain(Options->OwnerUserData);
        if ( !OwnerRetained ) {
            MdoRunsError(Error, XWORK_ERROR_CONTEXT,
                "interactive run callback owner is closing");
            return false;
        }
    }
    memset(&g_MdoRuns, 0, sizeof(g_MdoRuns));
    g_MdoRuns.Lock = xrtMutexCreate();
    g_MdoRuns.Changed = xrtCondCreate();
    g_MdoRuns.Runtime = xworkRuntimeRef(Runtime);
    g_MdoRuns.Options = *Options;
    g_MdoRuns.OwnerRetained = OwnerRetained;
    if ( g_MdoRuns.Lock == NULL || g_MdoRuns.Changed == NULL ||
         g_MdoRuns.Runtime == NULL ) {
        MdoRunManagerUnit();
        MdoRunsError(Error, XWORK_ERROR_OUT_OF_MEMORY,
            "cannot allocate interactive run manager state");
        return false;
    }
    g_MdoRuns.Initialized = true;
    if ( Options->Automatic ) {
        g_MdoRuns.Thread = xrtThreadCreate(MdoRunsThread, NULL, 0u);
        if ( g_MdoRuns.Thread == NULL ) {
            MdoRunManagerUnit();
            MdoRunsError(Error, XWORK_ERROR_CONTEXT,
                "cannot start the interactive run poller");
            return false;
        }
    }
    return true;
}

void MdoRunManagerUnit(void)
{
    xthread* Thread = NULL;
    MdoRunEntry* Entries = NULL;
    size_t Count = 0u;
    size_t i;
    MdoRunCleanup Cleanup[MDO_RUN_RETAINED_MAX];
    MdoRunManagerOptions Options;
    bool OwnerRetained;
    memset(Cleanup, 0, sizeof(Cleanup));
    memset(&Options, 0, sizeof(Options));
    if ( g_MdoRuns.Lock != NULL && xrtMutexLock(g_MdoRuns.Lock) ) {
        g_MdoRuns.Stopping = true;
        Thread = g_MdoRuns.Thread;
        g_MdoRuns.Thread = NULL;
        if ( g_MdoRuns.Changed != NULL )
            (void)xrtCondBroadcast(g_MdoRuns.Changed);
        (void)xrtMutexUnlock(g_MdoRuns.Lock);
    } else {
        Thread = g_MdoRuns.Thread;
        g_MdoRuns.Thread = NULL;
    }
    if ( Thread != NULL ) {
        (void)xrtThreadWait(Thread);
        xrtThreadDestroy(Thread);
    }
    if ( g_MdoRuns.Lock != NULL && xrtMutexLock(g_MdoRuns.Lock) ) {
        while ( g_MdoRuns.StartingCount != 0u &&
                g_MdoRuns.Changed != NULL )
            (void)xrtCondWait(g_MdoRuns.Changed, g_MdoRuns.Lock);
        Entries = g_MdoRuns.Entries;
        Count = g_MdoRuns.Count;
        g_MdoRuns.Entries = NULL;
        g_MdoRuns.Count = 0u;
        g_MdoRuns.Capacity = 0u;
        g_MdoRuns.ActiveCount = 0u;
        (void)xrtMutexUnlock(g_MdoRuns.Lock);
    } else {
        Entries = g_MdoRuns.Entries;
        Count = g_MdoRuns.Count;
    }
    for ( i = 0u; i < Count; ++i ) {
        Cleanup[i].Run = Entries[i].Run;
        Cleanup[i].Session = Entries[i].Session;
        Cleanup[i].Cancel = Entries[i].Cancel;
        xrtFree(Entries[i].FinalText);
    }
    MdoRunsCleanup(Cleanup, Count, true);
    xrtFree(Entries);
    Options = g_MdoRuns.Options;
    OwnerRetained = g_MdoRuns.OwnerRetained;
    if ( g_MdoRuns.Runtime != NULL )
        xworkRuntimeRelease(g_MdoRuns.Runtime);
    if ( g_MdoRuns.Changed != NULL )
        (void)xrtCondDestroy(g_MdoRuns.Changed);
    if ( g_MdoRuns.Lock != NULL )
        (void)xrtMutexDestroy(g_MdoRuns.Lock);
    memset(&g_MdoRuns, 0, sizeof(g_MdoRuns));
    if ( OwnerRetained && Options.OnOwnerRelease != NULL )
        Options.OnOwnerRelease(Options.OwnerUserData);
}

bool MdoRunStartWithOutcome(const MdoRunStartOptions* Options,
    MdoRunInfo* Info, xwork_error* Error, bool* MayHaveExecuted)
{
    MdoSessionRuntimeOptions RuntimeOptions;
    MdoAgentRunOptions RunOptions;
    MdoAgentRunInfo AgentInfo;
    MdoSession* Session = NULL;
    MdoAgentSession* Agent = NULL;
    MdoAgentRun* Run = NULL;
    xcancel* Cancel = NULL;
    xcancel* StartCancel = NULL;
    MdoRunEntry* Entry;
    MdoRunInfo Reserved;
    uint64 Deadline;
    size_t Index;
    bool Ready = false;
    bool Published = false;
    bool Stopping = false;
    uint64 ImageRunId = 0u;
    if ( MayHaveExecuted != NULL ) *MayHaveExecuted = false;
    xworkErrorInit(Error);
    if ( Options == NULL || Options->Size < sizeof(*Options) ||
         !MdoRunsIdValid(Options->ProjectId, MDO_PROJECT_ID_CAPACITY) ||
         !MdoRunsIdValid(Options->SessionId, MDO_SESSION_ID_CAPACITY) ||
         ((!Options->Resume && (!MdoRunsPromptValid(Options->Prompt,
                Options->UserMessage != NULL) ||
             (Options->AttachmentCount != 0u &&
              (Options->UserMessage == NULL ||
               Options->AttachmentIds == NULL ||
               Options->AttachmentCount > 4u)) ||
             Options->ResumeOptions != NULL ||
             Options->RecoveryToken != NULL)) ||
          (Options->Resume && (Options->Prompt != NULL ||
             Options->UserMessage != NULL ||
             Options->AttachmentCount != 0u ||
             Options->RecoveryToken == NULL ||
             strlen(Options->RecoveryToken) !=
                MDO_AGENT_RECOVERY_TOKEN_CAPACITY - 1u))) ||
         Options->TimeoutMilliseconds > MDO_RUN_TIMEOUT_MAX ||
         (Info != NULL && Info->Size < sizeof(*Info)) ) {
        MdoRunsError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "invalid interactive run start request");
        return false;
    }
    if ( !g_MdoRuns.Initialized || g_MdoRuns.Lock == NULL ) {
        MdoRunsError(Error, XWORK_ERROR_CONTEXT,
            "interactive run manager is not initialized");
        return false;
    }
    Cancel = xrtCancelCreate();
    if ( Cancel == NULL ) {
        MdoRunsError(Error, XWORK_ERROR_OUT_OF_MEMORY,
            "cannot allocate interactive run cancellation state");
        return false;
    }
    memset(&Reserved, 0, sizeof(Reserved));
    Reserved.Size = sizeof(Reserved);
    Reserved.State = XWORK_RUN_CREATED;
    Reserved.Result = XWORK_RESULT_ERROR;
    Reserved.CreatedAt = xrtNow();
    Reserved.CreatedMicroseconds = xrtClock();
    Reserved.Resume = Options->Resume;
    snprintf(Reserved.ProjectId, sizeof(Reserved.ProjectId), "%s",
        Options->ProjectId);
    snprintf(Reserved.SessionId, sizeof(Reserved.SessionId), "%s",
        Options->SessionId);
    if ( !xrtMutexLock(g_MdoRuns.Lock) ) {
        MdoRunsError(Error, XWORK_ERROR_CONTEXT,
            "cannot lock the interactive run manager");
        goto done;
    }
    if ( g_MdoRuns.Stopping ) {
        MdoRunsError(Error, XWORK_ERROR_CANCELLED,
            "interactive run manager is stopping");
        goto unlock_reserve;
    }
    if ( g_MdoRuns.ActiveCount + g_MdoRuns.StartingCount >=
            g_MdoRuns.Options.MaxActive ) {
        MdoRunsError(Error, XWORK_ERROR_LIMIT,
            "interactive run active limit was reached");
        goto unlock_reserve;
    }
    if ( !MdoRunsMakeRoomLocked() ) {
        MdoRunsError(Error, XWORK_ERROR_LIMIT,
            "interactive run retention limit was reached");
        goto unlock_reserve;
    }
    if ( !MdoRunsGrowLocked() || !MdoRunsAssignIdLocked(Reserved.Id) ) {
        MdoRunsError(Error, XWORK_ERROR_OUT_OF_MEMORY,
            "cannot reserve interactive run state");
        goto unlock_reserve;
    }
    Entry = &g_MdoRuns.Entries[g_MdoRuns.Count++];
    memset(Entry, 0, sizeof(*Entry));
    Entry->Info = Reserved;
    Entry->Cancel = Cancel;
    Entry->Starting = true;
    StartCancel = xrtCancelRef(Cancel);
    if ( StartCancel == NULL ) {
        Entry->Cancel = NULL;
        MdoRunsRemoveLocked(g_MdoRuns.Count - 1u);
        MdoRunsError(Error, XWORK_ERROR_OUT_OF_MEMORY,
            "cannot retain interactive run cancellation state");
        goto unlock_reserve;
    }
    Cancel = NULL;
    ++g_MdoRuns.StartingCount;
    Reserved = Entry->Info;
    (void)xrtMutexUnlock(g_MdoRuns.Lock);

    Deadline = Options->TimeoutMilliseconds != 0u ?
        xrtDeadlineAfter((uint64)Options->TimeoutMilliseconds *
            UINT64_C(1000)) : XRT_DEADLINE_NEVER;
    MdoSessionRuntimeOptionsInit(&RuntimeOptions);
    RuntimeOptions.Cancel = StartCancel;
    RuntimeOptions.Deadline = Deadline;
    RuntimeOptions.OnApproval = g_MdoRuns.Options.OnApproval;
    RuntimeOptions.ApprovalUserData = g_MdoRuns.Options.ApprovalUserData;
    RuntimeOptions.OnPermission = g_MdoRuns.Options.OnPermission;
    RuntimeOptions.PermissionUserData = g_MdoRuns.Options.PermissionUserData;
    RuntimeOptions.UseRunPermissionScope =
        g_MdoRuns.Options.UseRunPermissionScope;
    RuntimeOptions.OnHook = g_MdoRuns.Options.OnHook;
    RuntimeOptions.HookUserData = g_MdoRuns.Options.HookUserData;
    RuntimeOptions.OnEvent = g_MdoRuns.Options.OnEvent;
    RuntimeOptions.EventUserData = g_MdoRuns.Options.EventUserData;
    RuntimeOptions.OnModelComplete = g_MdoRuns.Options.OnModelComplete;
    RuntimeOptions.ModelUserData = g_MdoRuns.Options.ModelUserData;
    RuntimeOptions.OwnerUserData = g_MdoRuns.Options.OwnerUserData;
    RuntimeOptions.OnOwnerRetain = g_MdoRuns.Options.OnOwnerRetain;
    RuntimeOptions.OnOwnerRelease = g_MdoRuns.Options.OnOwnerRelease;
    Session = MdoSessionOpen(Options->ProjectId, Options->SessionId,
        &RuntimeOptions, Error);
    if ( Session == NULL ) goto publish;
    Agent = MdoSessionAgentRef(Session);
    if ( Agent == NULL ) {
        MdoRunsError(Error, XWORK_ERROR_CONTEXT,
            "managed session has no active Agent runtime");
        goto publish;
    }
    if ( Options->Resume && !MdoRunsRecoveryTokenValid(Agent,
            Options->RecoveryToken, Error) ) goto publish;
    MdoAgentRunOptionsInit(&RunOptions);
    RunOptions.Prompt = Options->Prompt;
    RunOptions.UserMessage = Options->UserMessage;
    RunOptions.Resume = Options->Resume;
    RunOptions.Cancel = StartCancel;
    RunOptions.Deadline = Deadline;
    RunOptions.ResumeOptions = Options->ResumeOptions;
    Run = MdoAgentRunCreate(Agent, &RunOptions, Error);
    if ( Run == NULL ) goto publish;
    memset(&AgentInfo, 0, sizeof(AgentInfo));
    AgentInfo.Size = sizeof(AgentInfo);
    if ( !MdoAgentRunGetInfo(Run, &AgentInfo) ) {
        MdoRunsError(Error, XWORK_ERROR_CONTEXT,
            "cannot inspect the created interactive Agent run");
        goto publish;
    }
    ImageRunId = AgentInfo.Run.uRunId;
    if ( !MdoSessionAttachmentPendingSet(Session, ImageRunId,
            Options->AttachmentIds, Options->AttachmentCount) ) {
        MdoRunsError(Error, XWORK_ERROR_CONTEXT,
            "cannot register image references before starting the run");
        goto publish;
    }
    /* From this call onward a worker may have crossed the execution
     * boundary, including when Start itself reports failure. */
    if ( MayHaveExecuted != NULL ) *MayHaveExecuted = true;
    if ( !MdoAgentRunStart(Run, Error) ) {
        MdoSessionAttachmentPendingClear(Session, ImageRunId);
        goto publish;
    }
    memset(&AgentInfo, 0, sizeof(AgentInfo));
    AgentInfo.Size = sizeof(AgentInfo);
    if ( !MdoAgentRunGetInfo(Run, &AgentInfo) ) {
        MdoRunsError(Error, XWORK_ERROR_CONTEXT,
            "cannot inspect the started interactive Agent run");
        goto publish;
    }
    Ready = true;

publish:
    MdoAgentSessionRelease(Agent);
    Agent = NULL;
    if ( !xrtMutexLock(g_MdoRuns.Lock) ) {
        MdoRunsError(Error, XWORK_ERROR_CONTEXT,
            "cannot publish interactive run state");
        goto done;
    }
    Index = MdoRunsFindLocked(Reserved.Id);
    if ( Index == SIZE_MAX ) {
        MdoRunsError(Error, XWORK_ERROR_CONTEXT,
            "interactive run reservation disappeared");
        (void)xrtMutexUnlock(g_MdoRuns.Lock);
        goto done;
    }
    Entry = &g_MdoRuns.Entries[Index];
    Stopping = g_MdoRuns.Stopping;
    if ( Ready && Run != NULL && !Stopping ) {
        bool CancelRequested = Entry->Info.CancelRequested;
        Entry->Info.AgentRunId = AgentInfo.Run.uRunId;
        Entry->Info.ConfigRevision = AgentInfo.ConfigRevision;
        Entry->Info.ModelGeneration = AgentInfo.ModelGeneration;
        Entry->Info.ModuleGeneration = AgentInfo.ModuleGeneration;
        Entry->Info.SkillGeneration = AgentInfo.SkillGeneration;
        Entry->Info.MemoryGeneration = AgentInfo.MemoryGeneration;
        Entry->Info.CreatedMicroseconds = AgentInfo.Run.uCreatedUs;
        Entry->Info.StartedMicroseconds = AgentInfo.Run.uStartedUs;
        Entry->Info.EndedMicroseconds = AgentInfo.Run.uEndedUs;
        Entry->Info.StartedAt = xrtNow();
        Entry->Info.State = AgentInfo.Run.eState;
        Entry->Info.Result = AgentInfo.Run.eResult;
        Entry->Info.Protocol = AgentInfo.Protocol;
        Entry->Info.Resume = AgentInfo.Run.bResume;
        Entry->Info.CancelRequested = CancelRequested;
        snprintf(Entry->Info.AgentId, sizeof(Entry->Info.AgentId), "%s",
            AgentInfo.AgentId != NULL ? AgentInfo.AgentId : "");
        snprintf(Entry->Info.ModelId, sizeof(Entry->Info.ModelId), "%s",
            AgentInfo.ModelId != NULL ? AgentInfo.ModelId : "");
        snprintf(Entry->Info.ReasoningEffort,
            sizeof(Entry->Info.ReasoningEffort), "%s",
            AgentInfo.ReasoningEffort != NULL ? AgentInfo.ReasoningEffort : "");
        Entry->Session = Session;
        Entry->Run = Run;
        Entry->Starting = false;
        Session = NULL;
        Run = NULL;
        --g_MdoRuns.StartingCount;
        ++g_MdoRuns.ActiveCount;
        if ( g_MdoRuns.RunsStarted != UINT64_MAX ) ++g_MdoRuns.RunsStarted;
        Reserved = Entry->Info;
        Published = true;
    } else {
        Cancel = Entry->Cancel;
        Entry->Cancel = NULL;
        MdoRunsRemoveLocked(Index);
        --g_MdoRuns.StartingCount;
        if ( Stopping )
            MdoRunsError(Error, XWORK_ERROR_CANCELLED,
                "interactive run manager stopped while starting the run");
    }
    (void)xrtCondBroadcast(g_MdoRuns.Changed);
    (void)xrtMutexUnlock(g_MdoRuns.Lock);
    if ( Published && Info != NULL ) {
        uint32 Size = Info->Size;
        *Info = Reserved;
        Info->Size = Size;
    }
done:
    if ( !Published ) {
        if ( Cancel == NULL && g_MdoRuns.Lock != NULL &&
             xrtMutexLock(g_MdoRuns.Lock) ) {
            Index = MdoRunsFindLocked(Reserved.Id);
            if ( Index != SIZE_MAX && g_MdoRuns.Entries[Index].Starting ) {
                Cancel = g_MdoRuns.Entries[Index].Cancel;
                g_MdoRuns.Entries[Index].Cancel = NULL;
                MdoRunsRemoveLocked(Index);
                if ( g_MdoRuns.StartingCount != 0u )
                    --g_MdoRuns.StartingCount;
                (void)xrtCondBroadcast(g_MdoRuns.Changed);
            }
            (void)xrtMutexUnlock(g_MdoRuns.Lock);
        }
        if ( Cancel != NULL ) (void)xrtCancelRequest(Cancel);
        MdoAgentRunDestroy(Run);
        MdoSessionRelease(Session);
        xrtCancelDestroy(Cancel);
    }
    xrtCancelDestroy(StartCancel);
    return Published;

unlock_reserve:
    (void)xrtMutexUnlock(g_MdoRuns.Lock);
    goto done;
}

bool MdoRunStart(const MdoRunStartOptions* Options, MdoRunInfo* Info,
    xwork_error* Error)
{
    return MdoRunStartWithOutcome(Options, Info, Error, NULL);
}

bool MdoRunCancel(const char* RunId, MdoRunInfo* Info,
    xwork_error* Error)
{
    size_t Index;
    xcancel* Cancel = NULL;
    MdoRunInfo Value;
    xworkErrorInit(Error);
    if ( !MdoRunsIdValid(RunId, MDO_RUN_ID_CAPACITY) ||
         (Info != NULL && Info->Size < sizeof(*Info)) ) {
        MdoRunsError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "invalid interactive run cancellation request");
        return false;
    }
    if ( !g_MdoRuns.Initialized || g_MdoRuns.Lock == NULL ||
         !xrtMutexLock(g_MdoRuns.Lock) ) {
        MdoRunsError(Error, XWORK_ERROR_CONTEXT,
            "interactive run manager is unavailable");
        return false;
    }
    Index = MdoRunsFindLocked(RunId);
    if ( Index == SIZE_MAX ) {
        (void)xrtMutexUnlock(g_MdoRuns.Lock);
        MdoRunsError(Error, XWORK_ERROR_CONTEXT,
            "interactive run was not found");
        return false;
    }
    if ( !g_MdoRuns.Entries[Index].Info.Terminal ) {
        g_MdoRuns.Entries[Index].Info.CancelRequested = true;
        Cancel = xrtCancelRef(g_MdoRuns.Entries[Index].Cancel);
    }
    Value = g_MdoRuns.Entries[Index].Info;
    (void)xrtMutexUnlock(g_MdoRuns.Lock);
    if ( Cancel != NULL ) {
        (void)xrtCancelRequest(Cancel);
        xrtCancelDestroy(Cancel);
    }
    if ( Info != NULL ) {
        uint32 Size = Info->Size;
        *Info = Value;
        Info->Size = Size;
    }
    return true;
}

bool MdoRunManagerGetStatus(MdoRunManagerStatus* Status)
{
    uint32 Size;
    if ( !g_MdoRuns.Initialized || Status == NULL ||
         Status->Size < sizeof(*Status) || g_MdoRuns.Lock == NULL ||
         !xrtMutexLock(g_MdoRuns.Lock) ) return false;
    Size = Status->Size;
    memset(Status, 0, sizeof(*Status));
    Status->Size = Size;
    Status->Automatic = g_MdoRuns.Options.Automatic;
    Status->Stopping = g_MdoRuns.Stopping;
    Status->PollMilliseconds = g_MdoRuns.Options.PollMilliseconds;
    Status->ActiveRuns = g_MdoRuns.ActiveCount;
    Status->StartingRuns = g_MdoRuns.StartingCount;
    Status->RetainedRuns = g_MdoRuns.Count;
    Status->MaxActive = g_MdoRuns.Options.MaxActive;
    Status->MaxRetained = g_MdoRuns.Options.MaxRetained;
    Status->RunsStarted = g_MdoRuns.RunsStarted;
    Status->RunsCompleted = g_MdoRuns.RunsCompleted;
    Status->RunsFailed = g_MdoRuns.RunsFailed;
    (void)xrtMutexUnlock(g_MdoRuns.Lock);
    return true;
}

MdoRunSnapshot* MdoRunSnapshotCreate(xwork_error* Error)
{
    MdoRunSnapshot* Snapshot = NULL;
    size_t i;
    xworkErrorInit(Error);
    if ( !g_MdoRuns.Initialized || g_MdoRuns.Lock == NULL ||
         !xrtMutexLock(g_MdoRuns.Lock) ) {
        MdoRunsError(Error, XWORK_ERROR_CONTEXT,
            "interactive run manager is unavailable");
        return NULL;
    }
    Snapshot = (MdoRunSnapshot*)xrtCalloc(1u, sizeof(*Snapshot));
    if ( Snapshot == NULL ) goto memory;
    xrtAtomic32Init(&Snapshot->Refs, 1u);
    if ( g_MdoRuns.Count != 0u ) {
        Snapshot->Items = (MdoRunSnapshotItem*)xrtCalloc(g_MdoRuns.Count,
            sizeof(*Snapshot->Items));
        if ( Snapshot->Items == NULL ) goto memory;
    }
    Snapshot->Count = g_MdoRuns.Count;
    for ( i = 0u; i < Snapshot->Count; ++i ) {
        Snapshot->Items[i].Info = g_MdoRuns.Entries[i].Info;
        if ( g_MdoRuns.Entries[i].FinalText != NULL ) {
            Snapshot->Items[i].FinalText =
                xrtStrDup(g_MdoRuns.Entries[i].FinalText);
            if ( Snapshot->Items[i].FinalText == NULL ) goto memory;
        }
    }
    (void)xrtMutexUnlock(g_MdoRuns.Lock);
    return Snapshot;
memory:
    (void)xrtMutexUnlock(g_MdoRuns.Lock);
    MdoRunSnapshotRelease(Snapshot);
    MdoRunsError(Error, XWORK_ERROR_OUT_OF_MEMORY,
        "cannot allocate interactive run snapshot");
    return NULL;
}

MdoRunSnapshot* MdoRunSnapshotRef(MdoRunSnapshot* Snapshot)
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

void MdoRunSnapshotRelease(MdoRunSnapshot* Snapshot)
{
    uint32 Previous;
    size_t i;
    if ( Snapshot == NULL ) return;
    Previous = xrtAtomic32FetchSub(&Snapshot->Refs, 1u, XMEMORY_ACQ_REL);
    if ( Previous > 1u ) return;
    if ( Previous == 0u ) abort();
    for ( i = 0u; i < Snapshot->Count; ++i )
        xrtFree(Snapshot->Items[i].FinalText);
    xrtFree(Snapshot->Items);
    memset(Snapshot, 0, sizeof(*Snapshot));
    xrtFree(Snapshot);
}

size_t MdoRunSnapshotCount(const MdoRunSnapshot* Snapshot)
{
    return Snapshot != NULL ? Snapshot->Count : 0u;
}

bool MdoRunSnapshotAt(const MdoRunSnapshot* Snapshot, size_t Index,
    MdoRunInfo* Info)
{
    uint32 Size;
    if ( Snapshot == NULL || Index >= Snapshot->Count || Info == NULL ||
         Info->Size < sizeof(*Info) ) return false;
    Size = Info->Size;
    *Info = Snapshot->Items[Index].Info;
    Info->Size = Size;
    return true;
}

bool MdoRunSnapshotFind(const MdoRunSnapshot* Snapshot, const char* RunId,
    MdoRunInfo* Info)
{
    size_t i;
    if ( Snapshot == NULL || RunId == NULL ) return false;
    for ( i = 0u; i < Snapshot->Count; ++i ) {
        if ( strcmp(Snapshot->Items[i].Info.Id, RunId) == 0 )
            return MdoRunSnapshotAt(Snapshot, i, Info);
    }
    return false;
}

bool MdoRunSnapshotResultAt(const MdoRunSnapshot* Snapshot, size_t Index,
    const char** Text, size_t* Size)
{
    if ( Snapshot == NULL || Index >= Snapshot->Count || Text == NULL ||
         Size == NULL ) return false;
    *Text = Snapshot->Items[Index].FinalText;
    *Size = Snapshot->Items[Index].Info.FinalTextBytes;
    return true;
}

bool MdoRunSnapshotResult(const MdoRunSnapshot* Snapshot, const char* RunId,
    const char** Text, size_t* Size)
{
    size_t i;
    if ( Snapshot == NULL || RunId == NULL ) return false;
    for ( i = 0u; i < Snapshot->Count; ++i ) {
        if ( strcmp(Snapshot->Items[i].Info.Id, RunId) == 0 )
            return MdoRunSnapshotResultAt(Snapshot, i, Text, Size);
    }
    return false;
}
