#include <stdlib.h>
#include <string.h>

#include "../../include/mdo/agents.h"
#include "../../include/mdo/asks.h"
#include "../../include/mdo/schedules.h"

#define MDO_SCHEDULE_EXECUTOR_ACTIVE_MAX MDO_SCHEDULE_OUTSTANDING_MAX
#define MDO_SCHEDULE_EXECUTOR_CLAIM_MAX 16u
#define MDO_SCHEDULE_EXECUTOR_POLL_DEFAULT 250u
#define MDO_SCHEDULE_EXECUTOR_POLL_MIN 50u
#define MDO_SCHEDULE_EXECUTOR_POLL_MAX 5000u
#define MDO_SCHEDULE_ASK_SCOPE_CAPACITY 48u

typedef struct MdoScheduleExecution {
    uint64 TaskId;
    uint64 AgentRunId;
    MdoAgentRun* Run;
    bool CancelRequested;
    char ProjectId[MDO_SCHEDULE_PROJECT_CAPACITY];
    char AskScopeId[MDO_SCHEDULE_ASK_SCOPE_CAPACITY];
} MdoScheduleExecution;

typedef struct MdoScheduleExecutorState {
    xmutex* Lock;
    xthread* Thread;
    xwork_runtime* Runtime;
    MdoScheduleExecution* Active;
    size_t ActiveCount;
    size_t ActiveCapacity;
    MdoScheduleExecutorOptions Options;
    uint64 ClaimsStarted;
    uint64 RunsCompleted;
    uint64 RunsFailed;
    bool Stopping;
    bool PersistenceFault;
    bool Initialized;
    char LastError[256];
} MdoScheduleExecutorState;

static MdoScheduleExecutorState g_MdoScheduleExecutor;

static void MdoScheduleExecutorError(xwork_error* Error,
    xwork_error_code Code, const char* Message)
{
    if ( Error == NULL ) return;
    xworkErrorInit(Error);
    Error->eCode = Code;
    snprintf(Error->sMessage, sizeof(Error->sMessage), "%s",
        Message != NULL ? Message : "schedule executor operation failed");
}

static void MdoScheduleExecutorRemember(const xwork_error* Error,
    const char* Fallback)
{
    const char* Message = Error != NULL && Error->sMessage[0] != '\0' ?
        Error->sMessage : Fallback;
    snprintf(g_MdoScheduleExecutor.LastError,
        sizeof(g_MdoScheduleExecutor.LastError), "%s",
        Message != NULL ? Message : "schedule execution failed");
    if ( Error != NULL && Error->eCode == XWORK_ERROR_IO )
        g_MdoScheduleExecutor.PersistenceFault = true;
}

static bool MdoScheduleExecutorGrow(void)
{
    size_t Next;
    MdoScheduleExecution* Value;
    if ( g_MdoScheduleExecutor.ActiveCount <
         g_MdoScheduleExecutor.ActiveCapacity ) return true;
    if ( g_MdoScheduleExecutor.ActiveCapacity >=
         MDO_SCHEDULE_EXECUTOR_ACTIVE_MAX ) return false;
    Next = g_MdoScheduleExecutor.ActiveCapacity != 0u ?
        g_MdoScheduleExecutor.ActiveCapacity * 2u : 8u;
    if ( Next > MDO_SCHEDULE_EXECUTOR_ACTIVE_MAX )
        Next = MDO_SCHEDULE_EXECUTOR_ACTIVE_MAX;
    Value = (MdoScheduleExecution*)xrtRealloc(g_MdoScheduleExecutor.Active,
        Next * sizeof(*Value));
    if ( Value == NULL ) return false;
    g_MdoScheduleExecutor.Active = Value;
    g_MdoScheduleExecutor.ActiveCapacity = Next;
    return true;
}

static char* MdoScheduleExecutorResultText(const char* Text)
{
    size_t Size = Text != NULL ? strlen(Text) : 0u;
    char* Result;
    if ( Size >= MDO_SCHEDULE_RESULT_CAPACITY ) {
        Size = MDO_SCHEDULE_RESULT_CAPACITY - 1u;
        while ( Size != 0u &&
                (((unsigned char)Text[Size] & 0xc0u) == 0x80u) ) --Size;
    }
    Result = (char*)xrtMalloc(Size + 1u);
    if ( Result == NULL ) return NULL;
    if ( Size != 0u ) memcpy(Result, Text, Size);
    Result[Size] = '\0';
    return Result;
}

static bool MdoScheduleExecutorFinishAt(size_t Index, size_t* Completed,
    xwork_error* Error)
{
    MdoScheduleExecution Execution = g_MdoScheduleExecutor.Active[Index];
    MdoAgentRunInfo Info;
    xwork_run_result Result;
    xwork_error WaitError;
    xwork_result Code;
    char* Text;
    bool Stored;
    memset(&Info, 0, sizeof(Info));
    Info.Size = sizeof(Info);
    if ( !MdoAgentRunGetInfo(Execution.Run, &Info) ) {
        MdoScheduleExecutorError(Error, XWORK_ERROR_CONTEXT,
            "cannot inspect a scheduled Agent run");
        return false;
    }
    if ( Info.Run.eState == XWORK_RUN_CREATED ||
         Info.Run.eState == XWORK_RUN_RUNNING ) return true;
    memset(&Result, 0, sizeof(Result));
    xworkErrorInit(&WaitError);
    Code = MdoAgentRunWait(Execution.Run, XRT_DEADLINE_NEVER, &Result,
        &WaitError);
    Text = MdoScheduleExecutorResultText(Result.sFinalText != NULL ?
        Result.sFinalText : (WaitError.sMessage[0] != '\0' ?
        WaitError.sMessage : "scheduled Agent run completed without text"));
    if ( Text == NULL ) {
        xworkRunResultUnit(&Result);
        MdoScheduleExecutorError(Error, XWORK_ERROR_OUT_OF_MEMORY,
            "cannot allocate a scheduled Agent result");
        return false;
    }
    Stored = MdoScheduleFinishTaskWithRun(Execution.TaskId,
        Execution.AgentRunId, Code, Text, Error);
    xrtFree(Text);
    xworkRunResultUnit(&Result);
    MdoAgentRunDestroy(Execution.Run);
    g_MdoScheduleExecutor.Active[Index] =
        g_MdoScheduleExecutor.Active[g_MdoScheduleExecutor.ActiveCount - 1u];
    --g_MdoScheduleExecutor.ActiveCount;
    if ( g_MdoScheduleExecutor.RunsCompleted != UINT64_MAX )
        ++g_MdoScheduleExecutor.RunsCompleted;
    if ( Code != XWORK_RESULT_OK &&
         g_MdoScheduleExecutor.RunsFailed != UINT64_MAX )
        ++g_MdoScheduleExecutor.RunsFailed;
    if ( Completed != NULL ) ++*Completed;
    if ( !Stored ) MdoScheduleExecutorRemember(Error,
        "cannot persist a scheduled Agent result");
    return Stored;
}

static bool MdoScheduleExecutorHarvest(size_t* Completed, xwork_error* Error)
{
    size_t i = 0u;
    while ( i < g_MdoScheduleExecutor.ActiveCount ) {
        size_t Before = g_MdoScheduleExecutor.ActiveCount;
        if ( !MdoScheduleExecutorFinishAt(i, Completed, Error) ) return false;
        if ( g_MdoScheduleExecutor.ActiveCount == Before ) ++i;
    }
    return true;
}

/* Generic task_cancel callers can bypass the product API. Observe their
 * requests before harvesting or starting more work; the snapshot owns copied
 * task data and keeps the runtime's locks out of Agent cancellation. */
static bool MdoScheduleExecutorForwardTaskCancels(xwork_error* Error)
{
    xwork_task_snapshot* Tasks;
    size_t i;
    if ( g_MdoScheduleExecutor.ActiveCount == 0u ) return true;
    Tasks = xworkRuntimeTaskSnapshot(g_MdoScheduleExecutor.Runtime, 0u, Error);
    if ( Tasks == NULL ) return false;
    for ( i = 0u; i < g_MdoScheduleExecutor.ActiveCount; ++i ) {
        MdoScheduleExecution* Execution = &g_MdoScheduleExecutor.Active[i];
        xwork_task_info Info;
        if ( Execution->CancelRequested ) continue;
        xworkTaskInfoInit(&Info);
        if ( !xworkTaskSnapshotFind(Tasks, Execution->TaskId, &Info) ||
             Info.eState == XWORK_TASK_CANCELLED ) {
            if ( !MdoAgentRunCancel(Execution->Run) ) {
                xworkTaskSnapshotRelease(Tasks);
                MdoScheduleExecutorError(Error, XWORK_ERROR_CONTEXT,
                    "cannot cancel a scheduled Agent run");
                return false;
            }
            Execution->CancelRequested = true;
        }
    }
    xworkTaskSnapshotRelease(Tasks);
    return true;
}

static bool MdoScheduleExecutorFailClaim(uint64 TaskId, const char* Message,
    xwork_error* Error)
{
    xwork_error FinishError;
    const char* Text = Message != NULL && Message[0] != '\0' ? Message :
        "scheduled Agent run could not start";
    if ( MdoScheduleFinishTaskWithRun(TaskId, 0u, XWORK_RESULT_ERROR,
            Text, &FinishError) ) return true;
    if ( Error != NULL ) *Error = FinishError;
    MdoScheduleExecutorRemember(&FinishError,
        "cannot persist a failed schedule claim");
    return false;
}

static bool MdoScheduleExecutorStart(const MdoScheduleClaim* Claim,
    uint64* AgentRunId, xwork_error* Error)
{
    MdoAgentSessionOptions SessionOptions;
    MdoAgentRunOptions RunOptions;
    MdoAgentSession* Session = NULL;
    MdoAgentRun* Run = NULL;
    MdoAgentRunInfo Info;
    MdoScheduleExecution* Active;
    char Failure[256];
    char AskScopeId[MDO_SCHEDULE_ASK_SCOPE_CAPACITY];
    MdoAgentSessionOptionsInit(&SessionOptions);
    SessionOptions.AgentId = Claim->AgentId;
    SessionOptions.ModelId = Claim->ModelId[0] != '\0' ? Claim->ModelId : NULL;
    SessionOptions.Protocol = Claim->Protocol;
    SessionOptions.ReasoningEffort = Claim->ReasoningEffort[0] != '\0' ?
        Claim->ReasoningEffort : NULL;
    SessionOptions.MaxOutputTokens = Claim->MaxOutputTokens;
    SessionOptions.WorkspaceRoot = Claim->WorkspaceRoot[0] != '\0' ?
        Claim->WorkspaceRoot : NULL;
    SessionOptions.ProjectId = Claim->ProjectId;
    SessionOptions.ProductSessionId = Claim->ScheduleId;
    /* The plan ID remains the memory/audit identity; questions belong to this
     * particular occurrence, so overlapping executions never share answers. */
    snprintf(AskScopeId, sizeof(AskScopeId), "schedule-task-%llu",
        (unsigned long long)Claim->TaskId);
    SessionOptions.AskScopeId = AskScopeId;
    SessionOptions.OnApproval = g_MdoScheduleExecutor.Options.OnApproval;
    SessionOptions.ApprovalUserData =
        g_MdoScheduleExecutor.Options.ApprovalUserData;
    SessionOptions.OnPermission = g_MdoScheduleExecutor.Options.OnPermission;
    SessionOptions.PermissionUserData =
        g_MdoScheduleExecutor.Options.PermissionUserData;
    SessionOptions.UseRunPermissionScope =
        g_MdoScheduleExecutor.Options.UseRunPermissionScope;
    SessionOptions.OnHook = g_MdoScheduleExecutor.Options.OnHook;
    SessionOptions.HookUserData = g_MdoScheduleExecutor.Options.HookUserData;
    SessionOptions.OnEvent = g_MdoScheduleExecutor.Options.OnEvent;
    SessionOptions.EventUserData = g_MdoScheduleExecutor.Options.EventUserData;
    SessionOptions.OnModelComplete =
        g_MdoScheduleExecutor.Options.OnModelComplete;
    SessionOptions.ModelUserData = g_MdoScheduleExecutor.Options.ModelUserData;
    SessionOptions.OwnerUserData = g_MdoScheduleExecutor.Options.OwnerUserData;
    SessionOptions.OnOwnerRetain =
        g_MdoScheduleExecutor.Options.OnOwnerRetain;
    SessionOptions.OnOwnerRelease =
        g_MdoScheduleExecutor.Options.OnOwnerRelease;
    Session = MdoAgentSessionCreateWithRuntime(g_MdoScheduleExecutor.Runtime,
        &SessionOptions, Error);
    if ( Session == NULL ) goto fail;
    MdoAgentRunOptionsInit(&RunOptions);
    RunOptions.Prompt = Claim->Input;
    Run = MdoAgentRunCreate(Session, &RunOptions, Error);
    if ( Run == NULL || !MdoAgentRunStart(Run, Error) ) goto fail;
    memset(&Info, 0, sizeof(Info));
    Info.Size = sizeof(Info);
    if ( !MdoAgentRunGetInfo(Run, &Info) ) {
        MdoScheduleExecutorError(Error, XWORK_ERROR_CONTEXT,
            "cannot inspect a newly started scheduled Agent run");
        goto fail;
    }
    Active = &g_MdoScheduleExecutor.Active[g_MdoScheduleExecutor.ActiveCount++];
    memset(Active, 0, sizeof(*Active));
    Active->TaskId = Claim->TaskId;
    Active->AgentRunId = Info.Run.uRunId;
    Active->Run = Run;
    snprintf(Active->ProjectId, sizeof(Active->ProjectId), "%s", Claim->ProjectId);
    snprintf(Active->AskScopeId, sizeof(Active->AskScopeId), "%s", AskScopeId);
    Run = NULL;
    MdoAgentSessionRelease(Session);
    if ( g_MdoScheduleExecutor.ClaimsStarted != UINT64_MAX )
        ++g_MdoScheduleExecutor.ClaimsStarted;
    if ( AgentRunId != NULL ) *AgentRunId = Info.Run.uRunId;
    return true;
fail:
    snprintf(Failure, sizeof(Failure), "%s",
        Error != NULL && Error->sMessage[0] != '\0' ? Error->sMessage :
        "scheduled Agent run could not start");
    MdoAgentRunDestroy(Run);
    MdoAgentSessionRelease(Session);
    if ( g_MdoScheduleExecutor.RunsFailed != UINT64_MAX )
        ++g_MdoScheduleExecutor.RunsFailed;
    MdoScheduleExecutorRemember(Error, Failure);
    (void)MdoScheduleExecutorFailClaim(Claim->TaskId, Failure, Error);
    return false;
}

void MdoScheduleExecutorOptionsInit(MdoScheduleExecutorOptions* Options)
{
    if ( Options == NULL ) return;
    memset(Options, 0, sizeof(*Options));
    Options->Size = sizeof(*Options);
    Options->Automatic = true;
    Options->PollMilliseconds = MDO_SCHEDULE_EXECUTOR_POLL_DEFAULT;
    Options->MaxClaimsPerPump = 4u;
}

bool MdoScheduleExecutorPump(int64 Now, size_t* Started, size_t* Completed,
    xwork_error* Error)
{
    size_t StartedValue = 0u;
    size_t CompletedValue = 0u;
    size_t i;
    bool Ok = false;
    xworkErrorInit(Error);
    if ( !g_MdoScheduleExecutor.Initialized || Now < 0 ) {
        MdoScheduleExecutorError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "invalid schedule executor pump request");
        return false;
    }
    xrtMutexLock(g_MdoScheduleExecutor.Lock);
    if ( g_MdoScheduleExecutor.Stopping ) {
        MdoScheduleExecutorError(Error, XWORK_ERROR_CANCELLED,
            "schedule executor is stopping");
        goto done;
    }
    if ( !MdoScheduleExecutorForwardTaskCancels(Error) ||
         !MdoScheduleExecutorHarvest(&CompletedValue, Error) ) goto done;
    for ( i = 0u; i < g_MdoScheduleExecutor.Options.MaxClaimsPerPump; ++i ) {
        MdoScheduleClaim Claim;
        if ( !MdoScheduleExecutorGrow() ) {
            MdoScheduleExecutorError(Error, XWORK_ERROR_LIMIT,
                "schedule executor active run limit was exceeded");
            goto done;
        }
        MdoScheduleClaimInit(&Claim);
        if ( !MdoScheduleClaimDue(Now, &Claim, Error) ) goto done;
        if ( !Claim.Claimed ) break;
        if ( !MdoScheduleExecutorStart(&Claim, NULL, Error) ) goto done;
        ++StartedValue;
    }
    Ok = true;
done:
    if ( !Ok ) MdoScheduleExecutorRemember(Error,
        "schedule executor pump failed");
    xrtMutexUnlock(g_MdoScheduleExecutor.Lock);
    if ( Started != NULL ) *Started = StartedValue;
    if ( Completed != NULL ) *Completed = CompletedValue;
    return Ok;
}

bool MdoScheduleExecutorRunNow(const char* ScheduleId,
    uint64 ExpectedRevision, int64 Now, uint64* TaskId,
    uint64* AgentRunId, xwork_error* Error)
{
    MdoScheduleClaim Claim;
    bool Ok = false;
    xworkErrorInit(Error);
    if ( TaskId != NULL ) *TaskId = 0u;
    if ( AgentRunId != NULL ) *AgentRunId = 0u;
    if ( !g_MdoScheduleExecutor.Initialized || ScheduleId == NULL ||
         ExpectedRevision == 0u || Now <= 0 ) {
        MdoScheduleExecutorError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "invalid explicit schedule run request");
        return false;
    }
    xrtMutexLock(g_MdoScheduleExecutor.Lock);
    if ( g_MdoScheduleExecutor.Stopping ) {
        MdoScheduleExecutorError(Error, XWORK_ERROR_CANCELLED,
            "schedule executor is stopping");
        goto done;
    }
    if ( !MdoScheduleExecutorGrow() ) {
        MdoScheduleExecutorError(Error, XWORK_ERROR_LIMIT,
            "schedule executor active run limit was exceeded");
        goto done;
    }
    MdoScheduleClaimInit(&Claim);
    if ( !MdoScheduleTrigger(ScheduleId, ExpectedRevision, Now,
            &Claim, Error) ) goto done;
    if ( !MdoScheduleExecutorStart(&Claim, AgentRunId, Error) ) goto done;
    if ( TaskId != NULL ) *TaskId = Claim.TaskId;
    Ok = true;
done:
    xrtMutexUnlock(g_MdoScheduleExecutor.Lock);
    return Ok;
}

bool MdoScheduleExecutorCancelTask(uint64 TaskId, bool* Handled,
    xwork_error* Error)
{
    size_t i;
    bool Ok = true;
    xworkErrorInit(Error);
    if ( Handled != NULL ) *Handled = false;
    if ( TaskId == 0u || Handled == NULL ) {
        MdoScheduleExecutorError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "a task id and cancellation ownership result are required");
        return false;
    }
    if ( !g_MdoScheduleExecutor.Initialized ) return true;
    xrtMutexLock(g_MdoScheduleExecutor.Lock);
    for ( i = 0u; i < g_MdoScheduleExecutor.ActiveCount; ++i ) {
        MdoScheduleExecution* Execution = &g_MdoScheduleExecutor.Active[i];
        size_t Before;
        if ( Execution->TaskId != TaskId ) continue;
        *Handled = true;
        Before = g_MdoScheduleExecutor.ActiveCount;
        /* FinishAt waits only after observing a terminal Run. The executor
         * lock keeps the Run alive and serializes cancellation with harvest. */
        Ok = MdoScheduleExecutorFinishAt(i, NULL, Error);
        if ( !Ok || g_MdoScheduleExecutor.ActiveCount != Before ) break;
        if ( !Execution->CancelRequested ) {
            Ok = MdoAgentRunCancel(Execution->Run);
            if ( Ok ) Execution->CancelRequested = true;
            else MdoScheduleExecutorError(Error, XWORK_ERROR_CONTEXT,
                "cannot cancel a scheduled Agent run");
        }
        break;
    }
    xrtMutexUnlock(g_MdoScheduleExecutor.Lock);
    return Ok;
}

bool MdoScheduleExecutorTaskCancellationRequested(uint64 TaskId)
{
    size_t i;
    bool Requested = false;
    if ( !g_MdoScheduleExecutor.Initialized || TaskId == 0u ) return false;
    xrtMutexLock(g_MdoScheduleExecutor.Lock);
    for ( i = 0u; i < g_MdoScheduleExecutor.ActiveCount; ++i ) {
        if ( g_MdoScheduleExecutor.Active[i].TaskId == TaskId ) {
            Requested = g_MdoScheduleExecutor.Active[i].CancelRequested;
            break;
        }
    }
    xrtMutexUnlock(g_MdoScheduleExecutor.Lock);
    return Requested;
}

bool MdoScheduleExecutorTaskAsks(uint64 TaskId, MdoAskInfo* Items,
    size_t Capacity, size_t* Count, xwork_error* Error)
{
    size_t i;
    bool Ok = true;
    xworkErrorInit(Error);
    if ( Count != NULL ) *Count = 0u;
    if ( TaskId == 0u || Count == NULL ||
         (Capacity != 0u && Items == NULL) ) {
        MdoScheduleExecutorError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "invalid task question query");
        return false;
    }
    if ( !g_MdoScheduleExecutor.Initialized ) return true;
    xrtMutexLock(g_MdoScheduleExecutor.Lock);
    for ( i = 0u; i < g_MdoScheduleExecutor.ActiveCount; ++i ) {
        MdoScheduleExecution* Execution = &g_MdoScheduleExecutor.Active[i];
        if ( Execution->TaskId != TaskId || Execution->CancelRequested ) continue;
        Ok = MdoAskList(Execution->ProjectId, Execution->AskScopeId,
            Items, Capacity, Count);
        if ( !Ok ) MdoScheduleExecutorError(Error, XWORK_ERROR_CONTEXT,
            "scheduled task questions are unavailable");
        break;
    }
    xrtMutexUnlock(g_MdoScheduleExecutor.Lock);
    return Ok;
}

bool MdoScheduleExecutorAnswerTaskAsk(uint64 TaskId, uint64 AskId,
    const char* Answer, xwork_error* Error)
{
    size_t i;
    bool Ok = false;
    xworkErrorInit(Error);
    if ( !g_MdoScheduleExecutor.Initialized || TaskId == 0u || AskId == 0u ) {
        MdoScheduleExecutorError(Error, XWORK_ERROR_POLICY,
            "the scheduled task question is no longer pending");
        return false;
    }
    xrtMutexLock(g_MdoScheduleExecutor.Lock);
    for ( i = 0u; i < g_MdoScheduleExecutor.ActiveCount; ++i ) {
        MdoScheduleExecution* Execution = &g_MdoScheduleExecutor.Active[i];
        if ( Execution->TaskId != TaskId || Execution->CancelRequested ) continue;
        /* Answer checks the tool's cancellation token and deadline atomically
         * with its one-shot submission. Harvest/stop cannot destroy the scope
         * while this short operation holds the executor lifecycle lock. */
        Ok = MdoAskAnswer(Execution->ProjectId, Execution->AskScopeId,
            AskId, Answer, Error);
        break;
    }
    if ( !Ok && (Error == NULL || Error->eCode == XWORK_ERROR_NONE) )
        MdoScheduleExecutorError(Error, XWORK_ERROR_POLICY,
            "the scheduled task question is no longer pending");
    xrtMutexUnlock(g_MdoScheduleExecutor.Lock);
    return Ok;
}

static int32 MdoScheduleExecutorThread(ptr Data)
{
    (void)Data;
    for ( ; ; ) {
        bool Stop;
        uint32 Poll;
        xwork_error Error;
        xrtMutexLock(g_MdoScheduleExecutor.Lock);
        Stop = g_MdoScheduleExecutor.Stopping;
        Poll = g_MdoScheduleExecutor.Options.PollMilliseconds;
        xrtMutexUnlock(g_MdoScheduleExecutor.Lock);
        if ( Stop ) break;
        (void)MdoScheduleExecutorPump(xrtNow(), NULL, NULL, &Error);
        xrtSleep(Poll);
    }
    return 0;
}

bool MdoScheduleExecutorInit(xwork_runtime* Runtime,
    const MdoScheduleExecutorOptions* Options, xwork_error* Error)
{
    MdoScheduleExecutorOptions Defaults;
    xworkErrorInit(Error);
    if ( g_MdoScheduleExecutor.Initialized ) return true;
    if ( Options == NULL ) {
        MdoScheduleExecutorOptionsInit(&Defaults);
        Options = &Defaults;
    }
    if ( Runtime == NULL || MdoScheduleManagerGeneration() == 0u ||
         Options->Size < sizeof(*Options) ||
         Options->PollMilliseconds < MDO_SCHEDULE_EXECUTOR_POLL_MIN ||
         Options->PollMilliseconds > MDO_SCHEDULE_EXECUTOR_POLL_MAX ||
         Options->MaxClaimsPerPump == 0u ||
         Options->MaxClaimsPerPump > MDO_SCHEDULE_EXECUTOR_CLAIM_MAX ||
         ((Options->OnOwnerRetain != NULL) !=
          (Options->OnOwnerRelease != NULL)) ) {
        MdoScheduleExecutorError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "invalid schedule executor configuration");
        return false;
    }
    memset(&g_MdoScheduleExecutor, 0, sizeof(g_MdoScheduleExecutor));
    g_MdoScheduleExecutor.Lock = xrtMutexCreate();
    g_MdoScheduleExecutor.Runtime = xworkRuntimeRef(Runtime);
    g_MdoScheduleExecutor.Options = *Options;
    if ( g_MdoScheduleExecutor.Lock == NULL ||
         g_MdoScheduleExecutor.Runtime == NULL ) {
        MdoScheduleExecutorUnit();
        MdoScheduleExecutorError(Error, XWORK_ERROR_OUT_OF_MEMORY,
            "cannot allocate schedule executor state");
        return false;
    }
    g_MdoScheduleExecutor.Initialized = true;
    if ( Options->Automatic ) {
        g_MdoScheduleExecutor.Thread = xrtThreadCreate(
            MdoScheduleExecutorThread, NULL, 0u);
        if ( g_MdoScheduleExecutor.Thread == NULL ) {
            MdoScheduleExecutorUnit();
            MdoScheduleExecutorError(Error, XWORK_ERROR_CONTEXT,
                "cannot start the schedule executor timer");
            return false;
        }
    }
    return true;
}

void MdoScheduleExecutorUnit(void)
{
    size_t i;
    xthread* Thread;
    MdoScheduleExecution* Active;
    size_t ActiveCount;
    if ( g_MdoScheduleExecutor.Lock != NULL ) {
        xrtMutexLock(g_MdoScheduleExecutor.Lock);
        g_MdoScheduleExecutor.Stopping = true;
        Thread = g_MdoScheduleExecutor.Thread;
        g_MdoScheduleExecutor.Thread = NULL;
        xrtMutexUnlock(g_MdoScheduleExecutor.Lock);
    } else {
        Thread = g_MdoScheduleExecutor.Thread;
    }
    if ( Thread != NULL ) {
        (void)xrtThreadWait(Thread);
        xrtThreadDestroy(Thread);
    }
    if ( g_MdoScheduleExecutor.Lock != NULL ) {
        xrtMutexLock(g_MdoScheduleExecutor.Lock);
        Active = g_MdoScheduleExecutor.Active;
        ActiveCount = g_MdoScheduleExecutor.ActiveCount;
        g_MdoScheduleExecutor.Active = NULL;
        g_MdoScheduleExecutor.ActiveCount = 0u;
        g_MdoScheduleExecutor.ActiveCapacity = 0u;
        xrtMutexUnlock(g_MdoScheduleExecutor.Lock);
    } else {
        Active = g_MdoScheduleExecutor.Active;
        ActiveCount = g_MdoScheduleExecutor.ActiveCount;
    }
    for ( i = 0u; i < ActiveCount; ++i ) {
        xwork_error Error;
        (void)MdoAgentRunCancel(Active[i].Run);
        MdoAgentRunDestroy(Active[i].Run);
        (void)MdoScheduleFinishTaskWithRun(
            Active[i].TaskId, Active[i].AgentRunId,
            XWORK_RESULT_CANCELLED, "mdo stopped before the scheduled run completed",
            &Error);
    }
    xrtFree(Active);
    if ( g_MdoScheduleExecutor.Lock != NULL ) {
        xrtMutexDestroy(g_MdoScheduleExecutor.Lock);
    }
    if ( g_MdoScheduleExecutor.Runtime != NULL )
        xworkRuntimeRelease(g_MdoScheduleExecutor.Runtime);
    memset(&g_MdoScheduleExecutor, 0, sizeof(g_MdoScheduleExecutor));
}

bool MdoScheduleExecutorGetSnapshot(MdoScheduleExecutorSnapshot* Snapshot)
{
    uint32 Size;
    if ( !g_MdoScheduleExecutor.Initialized || Snapshot == NULL ||
         Snapshot->Size < sizeof(*Snapshot) ) return false;
    Size = Snapshot->Size;
    xrtMutexLock(g_MdoScheduleExecutor.Lock);
    memset(Snapshot, 0, sizeof(*Snapshot));
    Snapshot->Size = Size;
    Snapshot->Automatic = g_MdoScheduleExecutor.Options.Automatic;
    Snapshot->PersistenceFault = g_MdoScheduleExecutor.PersistenceFault;
    Snapshot->PollMilliseconds =
        g_MdoScheduleExecutor.Options.PollMilliseconds;
    Snapshot->ActiveRuns = g_MdoScheduleExecutor.ActiveCount;
    Snapshot->ClaimsStarted = g_MdoScheduleExecutor.ClaimsStarted;
    Snapshot->RunsCompleted = g_MdoScheduleExecutor.RunsCompleted;
    Snapshot->RunsFailed = g_MdoScheduleExecutor.RunsFailed;
    snprintf(Snapshot->LastError, sizeof(Snapshot->LastError), "%s",
        g_MdoScheduleExecutor.LastError);
    xrtMutexUnlock(g_MdoScheduleExecutor.Lock);
    return true;
}
