#include <stdio.h>
#include <string.h>

#include "internal.h"
#include "../../include/mdo/bootstrap.h"

#define MDO_API_TASK_LIST_LIMIT 100u
#define MDO_API_TASK_OUTPUT_DEFAULT_BYTES (16u * 1024u)
#define MDO_API_TASK_OUTPUT_MAX_BYTES (64u * 1024u)
#define MDO_API_TASK_EVENT_DEFAULT_LIMIT 32u
#define MDO_API_TASK_EVENT_MAX_LIMIT 64u

typedef struct MdoApiTaskOutputQuery {
    uint64 StdoutOffset;
    uint64 StderrOffset;
    uint64 ResultOffset;
    size_t Limit;
} MdoApiTaskOutputQuery;

typedef enum MdoApiTaskLookup {
    MDO_API_TASK_LOOKUP_FOUND = 0,
    MDO_API_TASK_LOOKUP_MISSING,
    MDO_API_TASK_LOOKUP_FAILED
} MdoApiTaskLookup;

static cstr MdoApiTaskKindText(xwork_task_kind Kind)
{
    switch ( Kind ) {
    case XWORK_TASK_PROCESS: return "process";
    case XWORK_TASK_AGENT: return "agent";
    case XWORK_TASK_SCHEDULED: return "scheduled";
    default: return "unknown";
    }
}

static cstr MdoApiTaskStateText(xwork_task_state State)
{
    switch ( State ) {
    case XWORK_TASK_PENDING: return "pending";
    case XWORK_TASK_RUNNING: return "running";
    case XWORK_TASK_SUCCEEDED: return "succeeded";
    case XWORK_TASK_FAILED: return "failed";
    case XWORK_TASK_CANCELLED: return "cancelled";
    case XWORK_TASK_TIMED_OUT: return "timed_out";
    case XWORK_TASK_LOST: return "lost";
    default: return "unknown";
    }
}

static bool MdoApiTaskTerminal(xwork_task_state State)
{
    return State == XWORK_TASK_SUCCEEDED || State == XWORK_TASK_FAILED ||
        State == XWORK_TASK_CANCELLED || State == XWORK_TASK_TIMED_OUT ||
        State == XWORK_TASK_LOST;
}

static cstr MdoApiTaskEventKindText(xwork_task_event_kind Kind)
{
    switch ( Kind ) {
    case XWORK_TASK_EVENT_CREATED: return "created";
    case XWORK_TASK_EVENT_STATE_CHANGED: return "state_changed";
    case XWORK_TASK_EVENT_CANCEL_REQUESTED: return "cancel_requested";
    case XWORK_TASK_EVENT_RESTORED: return "restored";
    case XWORK_TASK_EVENT_NOTICE_TAKEN: return "notice_taken";
    default: return "unknown";
    }
}

static bool MdoApiTaskUnsigned(xstrview Text, uint64 Maximum, uint64* Value)
{
    uint64 Number = 0u;
    size_t Index;
    if ( Text.Size == 0u || Value == NULL ) return false;
    for ( Index = 0u; Index < Text.Size; Index++ ) {
        uint64 Digit;
        if ( Text.Data[Index] < '0' || Text.Data[Index] > '9' ) return false;
        Digit = (uint64)(Text.Data[Index] - '0');
        if ( Number > (Maximum - Digit) / 10u ) return false;
        Number = Number * 10u + Digit;
    }
    *Value = Number;
    return true;
}

static bool MdoApiTaskPath(const MdoApiContext* Context, uint64* TaskId)
{
    return Context->ParamCount == 1u &&
        MdoApiTaskUnsigned(Context->Params[0], UINT64_MAX, TaskId) &&
        *TaskId != 0u;
}

static bool MdoApiTaskNoBody(const MdoApiContext* Context)
{
    const xhttp1head* Head = Context->Request->head;
    return !(((Head->Flags & (uint32)XHTTP1_CONTENT_LENGTH) != 0u &&
              Head->ContentLength != 0u) ||
             (Head->Flags & (uint32)XHTTP1_TRANSFER_ENCODING) != 0u);
}

static bool MdoApiTaskInfoValue(const xwork_task_info* Info, xvalue** Value)
{
    xvalue* Item = xrtValueObject();
    bool Ok = Item != NULL &&
        MdoApiValueSetUInt(Item, "id", Info->uTaskId) &&
        MdoApiValueSetUInt(Item, "owner_agent_id", Info->uOwnerAgentId) &&
        MdoApiValueSetUInt(Item, "owner_run_id", Info->uOwnerRunId) &&
        MdoApiValueSetUInt(Item, "parent_task_id", Info->uParentTaskId) &&
        MdoApiValueSetString(Item, "kind", MdoApiTaskKindText(Info->eKind)) &&
        MdoApiValueSetString(Item, "state", MdoApiTaskStateText(Info->eState)) &&
        MdoApiValueSetBool(Item, "terminal", MdoApiTaskTerminal(Info->eState)) &&
        MdoApiValueSetUInt(Item, "revision", Info->uRevision) &&
        MdoApiValueSetInt(Item, "created_at", Info->iCreatedAtUs) &&
        MdoApiValueSetInt(Item, "started_at", Info->iStartedAtUs) &&
        MdoApiValueSetInt(Item, "ended_at", Info->iEndedAtUs) &&
        MdoApiValueSetInt(Item, "scheduled_at", Info->iScheduledAtUs) &&
        MdoApiValueSetBool(Item, "exit_status_valid", Info->bExitStatusValid) &&
        MdoApiValueSetInt(Item, "exit_code", Info->iExitCode) &&
        MdoApiValueSetInt(Item, "exit_signal", Info->iExitSignal) &&
        MdoApiValueSetInt(Item, "stop_reason", Info->iStopReason) &&
        MdoApiValueSetBool(Item, "notice_taken", Info->bNoticeTaken) &&
        MdoApiValueSetString(Item, "owner_session", Info->sOwnerSession) &&
        MdoApiValueSetString(Item, "label", Info->sLabel) &&
        MdoApiValueSetString(Item, "notify", Info->sNotify) &&
        MdoApiValueSetString(Item, "schedule_id", Info->sScheduleId) &&
        MdoApiValueSetUInt(Item, "schedule_generation",
            Info->uScheduleGeneration);
    if ( !Ok ) { xrtValueRelease(Item); return false; }
    *Value = Item;
    return true;
}

static MdoApiTaskLookup MdoApiTaskLookupValue(xwork_runtime* Runtime,
    uint64 TaskId, xvalue** Value, uint64* Revision)
{
    xwork_error Error;
    xwork_task_snapshot* Snapshot;
    xwork_task_info Info;
    MdoApiTaskLookup Lookup;
    *Value = NULL;
    *Revision = 0u;
    memset(&Error, 0, sizeof(Error));
    Snapshot = xworkRuntimeTaskSnapshot(Runtime, 0u, &Error);
    if ( Snapshot == NULL ) return MDO_API_TASK_LOOKUP_FAILED;
    xworkTaskInfoInit(&Info);
    if ( !xworkTaskSnapshotFind(Snapshot, TaskId, &Info) ) {
        Lookup = MDO_API_TASK_LOOKUP_MISSING;
    } else if ( !MdoApiTaskInfoValue(&Info, Value) ) {
        Lookup = MDO_API_TASK_LOOKUP_FAILED;
    } else {
        *Revision = Info.uRevision;
        Lookup = MDO_API_TASK_LOOKUP_FOUND;
    }
    xworkTaskSnapshotRelease(Snapshot);
    return Lookup;
}

static bool MdoApiTaskReply(MdoApiContext* Context, xwork_runtime* Runtime,
    uint64 TaskId)
{
    xvalue* Data = NULL;
    char EntityTag[80];
    MdoApiTaskLookup Lookup;
    uint64 Revision;
    Lookup = MdoApiTaskLookupValue(Runtime, TaskId, &Data, &Revision);
    if ( Lookup == MDO_API_TASK_LOOKUP_MISSING )
        return MdoApiReplyError(Context, 404u, "task_not_found",
            "The requested task does not exist", NULL);
    if ( Lookup == MDO_API_TASK_LOOKUP_FAILED )
        return MdoApiReplyError(Context, 500u, "task_unavailable",
            "The task detail could not be read", NULL);
    (void)snprintf(EntityTag, sizeof(EntityTag), "\"mdo-task-%llu-%llu\"",
        (unsigned long long)TaskId, (unsigned long long)Revision);
    return MdoApiReplySuccessTakeEntityTag(Context, 200u, Data, EntityTag);
}

bool MdoApiTasksRoute(MdoApiContext* Context)
{
    xwork_runtime* Runtime = MdoBootstrapRuntime();
    xwork_error Error;
    xwork_task_snapshot* Snapshot;
    xvalue* Data = xrtValueObject();
    xvalue* Items = xrtValueArray();
    size_t Total;
    size_t Start;
    size_t Index;
    bool Ok;
    if ( Runtime == NULL ) return MdoApiReplyError(Context, 503u,
        "runtime_unavailable", "The task runtime is unavailable", NULL);
    memset(&Error, 0, sizeof(Error));
    Snapshot = xworkRuntimeTaskSnapshot(Runtime, 0u, &Error);
    Total = Snapshot != NULL ? xworkTaskSnapshotCount(Snapshot) : 0u;
    Start = Total > MDO_API_TASK_LIST_LIMIT ?
        Total - MDO_API_TASK_LIST_LIMIT : 0u;
    Ok = Snapshot != NULL && Data != NULL && Items != NULL;
    for ( Index = Start; Ok && Index < Total; Index++ ) {
        xwork_task_info Info;
        xvalue* Item = NULL;
        xworkTaskInfoInit(&Info);
        Ok = xworkTaskSnapshotTaskAt(Snapshot, Index, &Info) &&
            MdoApiTaskInfoValue(&Info, &Item) &&
            MdoApiValueAppendTake(Items, &Item);
        xrtValueRelease(Item);
    }
    if ( Ok ) Ok =
        MdoApiValueSetUInt(Data, "total", Total) &&
        MdoApiValueSetUInt(Data, "limit", MDO_API_TASK_LIST_LIMIT) &&
        MdoApiValueSetBool(Data, "truncated", Start != 0u) &&
        MdoApiValueSetTake(Data, "items", &Items);
    xrtValueRelease(Items);
    xworkTaskSnapshotRelease(Snapshot);
    if ( !Ok ) { xrtValueRelease(Data); Data = NULL; }
    if ( Data == NULL ) return MdoApiReplyError(Context, 500u,
        "tasks_unavailable", "The task list could not be created", NULL);
    return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
}

bool MdoApiTaskRoute(MdoApiContext* Context)
{
    xwork_runtime* Runtime = MdoBootstrapRuntime();
    xwork_error Error;
    uint64 TaskId;
    if ( !MdoApiTaskPath(Context, &TaskId) )
        return MdoApiReplyError(Context, 400u, "invalid_path",
            "The task identifier must be a nonzero decimal integer", NULL);
    if ( Runtime == NULL ) return MdoApiReplyError(Context, 503u,
        "runtime_unavailable", "The task runtime is unavailable", NULL);
    if ( Context->Request->head->MethodCode != XHTTP_METHOD_DELETE )
        return MdoApiTaskReply(Context, Runtime, TaskId);
    if ( !MdoApiTaskNoBody(Context) )
        return MdoApiReplyError(Context, 400u, "body_not_allowed",
            "This operation does not accept a request body", NULL);
    memset(&Error, 0, sizeof(Error));
    if ( !xworkRuntimeCancelTask(Runtime, TaskId, &Error) ) {
        if ( Error.eCode == XWORK_ERROR_INVALID_ARGUMENT )
            return MdoApiReplyError(Context, 404u, "task_not_found",
                "The requested task does not exist", NULL);
        return MdoApiReplyError(Context, 409u, "task_cancel_failed",
            "The task cancellation request could not be applied", NULL);
    }
    return MdoApiTaskReply(Context, Runtime, TaskId);
}

static bool MdoApiTaskQueryPart(xstrview Query, size_t* Position,
    xstrview* Name, xstrview* Value)
{
    size_t End = *Position;
    size_t Equal = SIZE_MAX;
    while ( End < Query.Size && Query.Data[End] != '&' ) {
        if ( Query.Data[End] == '=' && Equal == SIZE_MAX ) Equal = End;
        End++;
    }
    if ( End == *Position || Equal == SIZE_MAX || Equal == *Position ||
         Equal + 1u == End ) return false;
    *Name = xrtStrViewN(Query.Data + *Position, Equal - *Position);
    *Value = xrtStrViewN(Query.Data + Equal + 1u, End - Equal - 1u);
    *Position = End + (End < Query.Size ? 1u : 0u);
    if ( *Position == Query.Size && End < Query.Size ) return false;
    return true;
}

static bool MdoApiTaskOutputQueryParse(xstrview Query,
    MdoApiTaskOutputQuery* Output)
{
    size_t Position = 0u;
    unsigned Seen = 0u;
    memset(Output, 0, sizeof(*Output));
    Output->Limit = MDO_API_TASK_OUTPUT_DEFAULT_BYTES;
    while ( Position < Query.Size ) {
        xstrview Name;
        xstrview Value;
        uint64 Number;
        unsigned Bit;
        uint64 Maximum = UINT64_MAX;
        if ( !MdoApiTaskQueryPart(Query, &Position, &Name, &Value) )
            return false;
        if ( Name.Size == 6u && memcmp(Name.Data, "stdout", 6u) == 0 )
            Bit = 1u;
        else if ( Name.Size == 6u && memcmp(Name.Data, "stderr", 6u) == 0 )
            Bit = 2u;
        else if ( Name.Size == 6u && memcmp(Name.Data, "result", 6u) == 0 )
            Bit = 4u;
        else if ( Name.Size == 5u && memcmp(Name.Data, "limit", 5u) == 0 ) {
            Bit = 8u;
            Maximum = MDO_API_TASK_OUTPUT_MAX_BYTES;
        } else return false;
        if ( (Seen & Bit) != 0u ||
             !MdoApiTaskUnsigned(Value, Maximum, &Number) ||
             (Bit == 8u && Number == 0u) ) return false;
        Seen |= Bit;
        if ( Bit == 1u ) Output->StdoutOffset = Number;
        else if ( Bit == 2u ) Output->StderrOffset = Number;
        else if ( Bit == 4u ) Output->ResultOffset = Number;
        else Output->Limit = (size_t)Number;
    }
    return true;
}

static bool MdoApiTaskOutputStreamValue(const void* Bytes, size_t Size,
    uint64 Start, uint64 Next, bool Dropped, xvalue** Value)
{
    str Encoded = xrtBase64EncodeNew(Bytes, Size, NULL);
    xvalue* Item = xrtValueObject();
    bool Ok = Encoded != NULL && Item != NULL &&
        MdoApiValueSetUInt(Item, "start", Start) &&
        MdoApiValueSetUInt(Item, "next", Next) &&
        MdoApiValueSetUInt(Item, "bytes", Size) &&
        MdoApiValueSetBool(Item, "dropped", Dropped) &&
        MdoApiValueSetString(Item, "data", Encoded);
    xrtFree(Encoded);
    if ( !Ok ) { xrtValueRelease(Item); return false; }
    *Value = Item;
    return true;
}

bool MdoApiTaskOutputRoute(MdoApiContext* Context)
{
    xwork_runtime* Runtime = MdoBootstrapRuntime();
    xwork_error Error;
    xwork_task_output Output;
    MdoApiTaskOutputQuery Query;
    xvalue* Data = xrtValueObject();
    xvalue* Stdout = NULL;
    xvalue* Stderr = NULL;
    xvalue* Result = NULL;
    uint64 TaskId;
    bool Ok;
    if ( !MdoApiTaskPath(Context, &TaskId) )
        return MdoApiReplyError(Context, 400u, "invalid_path",
            "The task identifier must be a nonzero decimal integer", NULL);
    if ( !MdoApiTaskOutputQueryParse(Context->Target.Query, &Query) )
        return MdoApiReplyError(Context, 400u, "invalid_query",
            "Only unique numeric stdout, stderr, result, and bounded limit parameters are accepted",
            NULL);
    if ( Runtime == NULL ) return MdoApiReplyError(Context, 503u,
        "runtime_unavailable", "The task runtime is unavailable", NULL);
    xworkTaskOutputInit(&Output);
    memset(&Error, 0, sizeof(Error));
    if ( !xworkRuntimeReadTaskOutput(Runtime, TaskId, Query.StdoutOffset,
            Query.StderrOffset, Query.ResultOffset, Query.Limit, &Output,
            &Error) ) {
        xrtValueRelease(Data);
        xworkTaskOutputUnit(&Output);
        if ( Error.eCode == XWORK_ERROR_INVALID_ARGUMENT )
            return MdoApiReplyError(Context, 404u, "task_not_found",
                "The requested task does not exist", NULL);
        return MdoApiReplyError(Context, 500u, "task_output_unavailable",
            "The task output could not be read", NULL);
    }
    Ok = Data != NULL &&
        MdoApiTaskOutputStreamValue(Output.pStdout, Output.iStdoutSize,
            Output.uStdoutStart, Output.uStdoutNext, Output.bStdoutDropped,
            &Stdout) &&
        MdoApiTaskOutputStreamValue(Output.pStderr, Output.iStderrSize,
            Output.uStderrStart, Output.uStderrNext, Output.bStderrDropped,
            &Stderr) &&
        MdoApiTaskOutputStreamValue(Output.sResult, Output.iResultSize,
            Output.uResultStart, Output.uResultNext, Output.bResultDropped,
            &Result) &&
        MdoApiValueSetUInt(Data, "task_id", TaskId) &&
        MdoApiValueSetString(Data, "encoding", "base64") &&
        MdoApiValueSetUInt(Data, "limit", Query.Limit) &&
        MdoApiValueSetBool(Data, "complete", Output.bComplete) &&
        MdoApiValueSetTake(Data, "stdout", &Stdout) &&
        MdoApiValueSetTake(Data, "stderr", &Stderr) &&
        MdoApiValueSetTake(Data, "result", &Result);
    xrtValueRelease(Stdout); xrtValueRelease(Stderr); xrtValueRelease(Result);
    xworkTaskOutputUnit(&Output);
    if ( !Ok ) { xrtValueRelease(Data); Data = NULL; }
    if ( Data == NULL ) return MdoApiReplyError(Context, 500u,
        "task_output_unavailable", "The task output could not be encoded", NULL);
    return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
}

static bool MdoApiTaskEventsQuery(xstrview Query, uint64* After, size_t* Limit)
{
    size_t Position = 0u;
    unsigned Seen = 0u;
    *After = 0u;
    *Limit = MDO_API_TASK_EVENT_DEFAULT_LIMIT;
    while ( Position < Query.Size ) {
        xstrview Name;
        xstrview Value;
        uint64 Number;
        unsigned Bit;
        uint64 Maximum;
        if ( !MdoApiTaskQueryPart(Query, &Position, &Name, &Value) )
            return false;
        if ( Name.Size == 5u && memcmp(Name.Data, "after", 5u) == 0 ) {
            Bit = 1u; Maximum = UINT64_MAX;
        } else if ( Name.Size == 5u &&
                    memcmp(Name.Data, "limit", 5u) == 0 ) {
            Bit = 2u; Maximum = MDO_API_TASK_EVENT_MAX_LIMIT;
        } else return false;
        if ( (Seen & Bit) != 0u ||
             !MdoApiTaskUnsigned(Value, Maximum, &Number) ||
             (Bit == 2u && Number == 0u) ) return false;
        Seen |= Bit;
        if ( Bit == 1u ) *After = Number;
        else *Limit = (size_t)Number;
    }
    return true;
}

bool MdoApiTaskEventsRoute(MdoApiContext* Context)
{
    xwork_runtime* Runtime = MdoBootstrapRuntime();
    xwork_error Error;
    xwork_task_event Events[MDO_API_TASK_EVENT_MAX_LIMIT];
    xvalue* Data = xrtValueObject();
    xvalue* Items = xrtValueArray();
    uint64 TaskId;
    uint64 After;
    uint64 Next;
    size_t Limit;
    size_t Count;
    size_t Index;
    bool HistoryLost;
    bool Ok;
    if ( !MdoApiTaskPath(Context, &TaskId) )
        return MdoApiReplyError(Context, 400u, "invalid_path",
            "The task identifier must be a nonzero decimal integer", NULL);
    if ( !MdoApiTaskEventsQuery(Context->Target.Query, &After, &Limit) )
        return MdoApiReplyError(Context, 400u, "invalid_query",
            "Only unique numeric after and bounded limit parameters are accepted",
            NULL);
    if ( Runtime == NULL ) return MdoApiReplyError(Context, 503u,
        "runtime_unavailable", "The task runtime is unavailable", NULL);
    memset(&Error, 0, sizeof(Error));
    if ( !xworkRuntimeReadTaskEvents(Runtime, TaskId, After, Events, Limit,
            &Count, &Next, &HistoryLost, &Error) ) {
        xrtValueRelease(Data); xrtValueRelease(Items);
        if ( Error.eCode == XWORK_ERROR_INVALID_ARGUMENT )
            return MdoApiReplyError(Context, 404u, "task_not_found",
                "The requested task does not exist", NULL);
        return MdoApiReplyError(Context, 500u, "task_events_unavailable",
            "The task event stream could not be read", NULL);
    }
    Ok = Data != NULL && Items != NULL;
    for ( Index = 0u; Ok && Index < Count; Index++ ) {
        const xwork_task_event* Event = &Events[Index];
        xvalue* Item = xrtValueObject();
        Ok = Item != NULL &&
            MdoApiValueSetUInt(Item, "task_id", Event->uTaskId) &&
            MdoApiValueSetUInt(Item, "revision", Event->uRevision) &&
            MdoApiValueSetString(Item, "kind",
                MdoApiTaskEventKindText(Event->eKind)) &&
            MdoApiValueSetUInt(Item, "kind_code", Event->eKind) &&
            MdoApiValueSetString(Item, "state",
                MdoApiTaskStateText(Event->eState)) &&
            MdoApiValueSetBool(Item, "terminal",
                MdoApiTaskTerminal(Event->eState)) &&
            MdoApiValueSetInt(Item, "time", Event->iTimestampUs) &&
            MdoApiValueAppendTake(Items, &Item);
        xrtValueRelease(Item);
    }
    if ( Ok ) Ok =
        MdoApiValueSetUInt(Data, "task_id", TaskId) &&
        MdoApiValueSetUInt(Data, "after", After) &&
        MdoApiValueSetUInt(Data, "next_revision", Next) &&
        MdoApiValueSetBool(Data, "history_lost", HistoryLost) &&
        MdoApiValueSetTake(Data, "items", &Items);
    xrtValueRelease(Items);
    if ( !Ok ) { xrtValueRelease(Data); Data = NULL; }
    if ( Data == NULL ) return MdoApiReplyError(Context, 500u,
        "task_events_unavailable", "The task event stream could not be encoded",
        NULL);
    return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
}
