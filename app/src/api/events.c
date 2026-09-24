#include <string.h>

#include "internal.h"
#include "../../include/mdo/attachments.h"
#include "../../include/mdo/bootstrap.h"
#include "../../include/mdo/sessions.h"

#define MDO_API_EVENT_DEFAULT_LIMIT 32u
#define MDO_API_EVENT_MAX_LIMIT 32u
#define MDO_API_EVENT_TEXT_BYTES 4096u

static cstr MdoApiEventKindText(xwork_event_kind Kind)
{
    switch ( Kind ) {
    case XWORK_EVENT_AGENT_START: return "agent_start";
    case XWORK_EVENT_MODEL_START: return "model_start";
    case XWORK_EVENT_MODEL_TEXT_DELTA: return "model_text_delta";
    case XWORK_EVENT_MODEL_REASONING_DELTA: return "model_reasoning_delta";
    case XWORK_EVENT_MODEL_DONE: return "model_done";
    case XWORK_EVENT_TOOL_START: return "tool_start";
    case XWORK_EVENT_TOOL_DONE: return "tool_done";
    case XWORK_EVENT_COMPACTION_START: return "compaction_start";
    case XWORK_EVENT_COMPACTION_REJECTED: return "compaction_rejected";
    case XWORK_EVENT_COMPACTION_DONE: return "compaction_done";
    case XWORK_EVENT_AGENT_DONE: return "agent_done";
    case XWORK_EVENT_ERROR: return "error";
    case XWORK_EVENT_ARTIFACT_CREATED: return "artifact_created";
    case XWORK_EVENT_TASK_UPDATED: return "task_updated";
    case XWORK_EVENT_RECOVERY_REQUIRED: return "recovery_required";
    case XWORK_EVENT_RECOVERY_RESOLVED: return "recovery_resolved";
    default: return "unknown";
    }
}

static bool MdoApiEventTerminal(xwork_event_kind Kind)
{
    return Kind == XWORK_EVENT_AGENT_DONE || Kind == XWORK_EVENT_ERROR;
}

static bool MdoApiUnsigned(xstrview Text, uint64 Maximum, uint64* pValue)
{
    uint64 Value = 0u;
    size_t Index;

    if ( Text.Size == 0u || pValue == NULL ) return false;
    for ( Index = 0u; Index < Text.Size; Index++ ) {
        uint64 Digit;
        if ( Text.Data[Index] < '0' || Text.Data[Index] > '9' ) return false;
        Digit = (uint64)(Text.Data[Index] - '0');
        if ( Value > (Maximum - Digit) / 10u ) return false;
        Value = Value * 10u + Digit;
    }
    *pValue = Value;
    return true;
}

static bool MdoApiEventQuery(xstrview Query, uint64* pAfter, size_t* pLimit)
{
    size_t Position = 0u;
    bool HasAfter = false;
    bool HasLimit = false;

    *pAfter = 0u;
    *pLimit = MDO_API_EVENT_DEFAULT_LIMIT;
    while ( Position < Query.Size ) {
        size_t End = Position;
        size_t Equal = SIZE_MAX;
        xstrview Name;
        xstrview Value;
        uint64 Number;
        while ( End < Query.Size && Query.Data[End] != '&' ) {
            if ( Query.Data[End] == '=' && Equal == SIZE_MAX ) Equal = End;
            End++;
        }
        if ( End == Position || Equal == SIZE_MAX || Equal == Position ||
             Equal + 1u == End ) return false;
        Name = xrtStrViewN(Query.Data + Position, Equal - Position);
        Value = xrtStrViewN(Query.Data + Equal + 1u, End - Equal - 1u);
        if ( Name.Size == 5u && memcmp(Name.Data, "after", 5u) == 0 ) {
            if ( HasAfter || !MdoApiUnsigned(Value, UINT64_MAX, &Number) )
                return false;
            *pAfter = Number;
            HasAfter = true;
        } else if ( Name.Size == 5u &&
                    memcmp(Name.Data, "limit", 5u) == 0 ) {
            if ( HasLimit || !MdoApiUnsigned(Value, MDO_API_EVENT_MAX_LIMIT,
                    &Number) || Number == 0u ) return false;
            *pLimit = (size_t)Number;
            HasLimit = true;
        } else return false;
        Position = End + (End < Query.Size ? 1u : 0u);
        if ( Position == Query.Size && End < Query.Size ) return false;
    }
    return true;
}

static xstrview MdoApiEventText(cstr Text, size_t Size, bool* pTruncated)
{
    size_t Retained = Size;

    if ( Text == NULL ) return (xstrview){ NULL, 0u };
    if ( Retained > MDO_API_EVENT_TEXT_BYTES ) {
        Retained = MDO_API_EVENT_TEXT_BYTES;
        while ( Retained != 0u &&
               (((const unsigned char*)Text)[Retained] & 0xc0u) == 0x80u )
            Retained--;
        *pTruncated = true;
    }
    return xrtStrViewN(Text, Retained);
}

static bool MdoApiRuntimeEventValue(const xwork_event* Event, xvalue** pValue)
{
    xvalue* Item = xrtValueObject();
    bool Truncated = Event->bTextTruncated;
    xstrview Text = MdoApiEventText(Event->sText, Event->iTextLength,
        &Truncated);
    bool Ok = Item != NULL &&
        MdoApiValueSetUInt(Item, "schema_version", Event->uSchemaVersion) &&
        MdoApiValueSetUInt(Item, "event_id", Event->uEventId) &&
        MdoApiValueSetInt(Item, "time", Event->iOccurredAtUs) &&
        MdoApiValueSetString(Item, "kind", MdoApiEventKindText(Event->eKind)) &&
        MdoApiValueSetUInt(Item, "kind_code", Event->eKind) &&
        MdoApiValueSetBool(Item, "terminal", MdoApiEventTerminal(Event->eKind)) &&
        MdoApiValueSetBool(Item, "success", Event->bSuccess) &&
        MdoApiValueSetUInt(Item, "agent_id", Event->uAgentId) &&
        MdoApiValueSetUInt(Item, "run_id", Event->uRunId) &&
        MdoApiValueSetUInt(Item, "parent_run_id", Event->uParentRunId) &&
        MdoApiValueSetUInt(Item, "task_id", Event->uTaskId) &&
        MdoApiValueSetUInt(Item, "artifact_id", Event->uArtifactId) &&
        MdoApiValueSetUInt(Item, "agent_turn", Event->uAgentTurn) &&
        MdoApiValueSetUInt(Item, "user_message_sequence",
            Event->uUserMessageSequence) &&
        MdoApiValueSetUInt(Item, "agent_depth", Event->uAgentDepth) &&
        MdoApiValueSetUInt(Item, "effects", Event->uEffects) &&
        MdoApiValueSetBool(Item, "effect_applied", Event->bEffectApplied) &&
        MdoApiValueSetBool(Item, "text_truncated", Truncated) &&
        MdoApiValueSetUInt(Item, "original_text_bytes",
            Event->iOriginalTextLength != 0u ? Event->iOriginalTextLength :
            Event->iTextLength) &&
        MdoApiValueSetStringView(Item, "text", Text) &&
        MdoApiValueSetString(Item, "tool_name", Event->sToolName) &&
        MdoApiValueSetString(Item, "tool_call_id", Event->sToolCallId) &&
        MdoApiValueSetString(Item, "artifact_path", Event->sArtifactPath) &&
        MdoApiValueSetString(Item, "model", Event->sModel) &&
        MdoApiValueSetString(Item, "finish_reason", Event->sFinishReason) &&
        MdoApiValueSetUInt(Item, "task_state", Event->eTaskState) &&
        MdoApiValueSetUInt(Item, "task_revision", Event->uTaskRevision);
    if ( !Ok ) { xrtValueRelease(Item); return false; }
    *pValue = Item;
    return true;
}

bool MdoApiEventsRoute(MdoApiContext* Context)
{
    xwork_runtime* Runtime = MdoBootstrapRuntime();
    xwork_error Error;
    xwork_event_snapshot* Snapshot;
    xvalue* Data = xrtValueObject();
    xvalue* Items = xrtValueArray();
    uint64 After;
    size_t Limit;
    size_t Index;
    bool Ok;

    if ( !MdoApiEventQuery(Context->Target.Query, &After, &Limit) )
        return MdoApiReplyError(Context, 400u, "invalid_query",
            "Only bounded numeric after and limit parameters are accepted",
            NULL);
    if ( Runtime == NULL ) return MdoApiReplyError(Context, 503u,
        "runtime_unavailable", "The event runtime is unavailable", NULL);
    memset(&Error, 0, sizeof(Error));
    Snapshot = xworkRuntimeEventSnapshot(Runtime, After, Limit, &Error);
    Ok = Snapshot != NULL && Data != NULL && Items != NULL;
    for ( Index = 0u; Ok && Index < xworkEventSnapshotCount(Snapshot);
          Index++ ) {
        xwork_event Event;
        xvalue* Item = NULL;
        memset(&Event, 0, sizeof(Event));
        Ok = xworkEventSnapshotAt(Snapshot, Index, &Event) &&
            MdoApiRuntimeEventValue(&Event, &Item) &&
            MdoApiValueAppendTake(Items, &Item);
        xrtValueRelease(Item);
    }
    if ( Ok ) Ok =
        MdoApiValueSetString(Data, "stream", "runtime") &&
        MdoApiValueSetUInt(Data, "after", After) &&
        MdoApiValueSetUInt(Data, "next_cursor",
            xworkEventSnapshotNextCursor(Snapshot)) &&
        MdoApiValueSetUInt(Data, "latest_event_id",
            xworkEventSnapshotLatestId(Snapshot)) &&
        MdoApiValueSetUInt(Data, "dropped_count",
            xworkEventSnapshotDroppedCount(Snapshot)) &&
        MdoApiValueSetBool(Data, "history_lost",
            xworkEventSnapshotHistoryLost(Snapshot)) &&
        MdoApiValueSetTake(Data, "items", &Items);
    xrtValueRelease(Items);
    xworkEventSnapshotRelease(Snapshot);
    if ( !Ok ) { xrtValueRelease(Data); Data = NULL; }
    if ( Data == NULL ) return MdoApiReplyError(Context, 500u,
        "events_unavailable", "Runtime events could not be replayed", NULL);
    return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
}

static bool MdoApiCaptureId(MdoApiContext* Context, size_t Index,
    char* Output, size_t Capacity)
{
    xstrview Value;
    size_t Position;

    if ( Context == NULL || Index >= Context->ParamCount || Capacity == 0u )
        return false;
    Value = Context->Params[Index];
    if ( Value.Size == 0u || Value.Size >= Capacity ) return false;
    for ( Position = 0u; Position < Value.Size; Position++ ) {
        unsigned char Byte = (unsigned char)Value.Data[Position];
        if ( !((Byte >= 'a' && Byte <= 'z') ||
               (Byte >= 'A' && Byte <= 'Z') ||
               (Byte >= '0' && Byte <= '9') || Byte == '-' || Byte == '_' ||
               Byte == '.') ) return false;
    }
    memcpy(Output, Value.Data, Value.Size);
    Output[Value.Size] = '\0';
    return true;
}

static bool MdoApiSessionEventValue(const MdoSessionEventInfo* Event,
    const char* ProjectId, const char* SessionId, xvalue** pValue)
{
    xvalue* Item = xrtValueObject();
    char Attachments[4][33] = {{ 0 }};
    size_t AttachmentCount = 0u;
    size_t TextSize = Event->Text != NULL ? strlen(Event->Text) : 0u;
    bool Truncated = Event->TextTruncated;
    xstrview Text = MdoApiEventText(Event->Text, TextSize, &Truncated);
    bool Ok = Item != NULL &&
        MdoApiValueSetUInt(Item, "schema_version", Event->SchemaVersion) &&
        MdoApiValueSetUInt(Item, "event_id", Event->EventId) &&
        MdoApiValueSetUInt(Item, "source_event_id", Event->SourceEventId) &&
        MdoApiValueSetInt(Item, "time", Event->OccurredAt) &&
        MdoApiValueSetString(Item, "kind", MdoApiEventKindText(Event->Kind)) &&
        MdoApiValueSetUInt(Item, "kind_code", Event->Kind) &&
        MdoApiValueSetBool(Item, "terminal", MdoApiEventTerminal(Event->Kind)) &&
        MdoApiValueSetBool(Item, "success", Event->Success) &&
        MdoApiValueSetUInt(Item, "agent_id", Event->AgentId) &&
        MdoApiValueSetUInt(Item, "run_id", Event->RunId) &&
        MdoApiValueSetUInt(Item, "parent_run_id", Event->ParentRunId) &&
        MdoApiValueSetUInt(Item, "task_id", Event->TaskId) &&
        MdoApiValueSetUInt(Item, "artifact_id", Event->ArtifactId) &&
        MdoApiValueSetUInt(Item, "agent_turn", Event->AgentTurn) &&
        MdoApiValueSetUInt(Item, "user_message_sequence",
            Event->UserMessageSequence) &&
        MdoApiValueSetUInt(Item, "agent_depth", Event->AgentDepth) &&
        MdoApiValueSetUInt(Item, "effects", Event->Effects) &&
        MdoApiValueSetBool(Item, "effect_applied", Event->EffectApplied) &&
        MdoApiValueSetBool(Item, "text_truncated", Truncated) &&
        MdoApiValueSetUInt(Item, "original_text_bytes", TextSize) &&
        MdoApiValueSetStringView(Item, "text", Text) &&
        MdoApiValueSetString(Item, "tool_name", Event->ToolName) &&
        MdoApiValueSetString(Item, "tool_call_id", Event->ToolCallId) &&
        MdoApiValueSetString(Item, "artifact_path", Event->ArtifactPath) &&
        MdoApiValueSetString(Item, "model", Event->Model) &&
        MdoApiValueSetUInt(Item, "task_state", Event->TaskState) &&
        MdoApiValueSetUInt(Item, "task_revision", Event->TaskRevision);
    if (Ok) Ok = MdoApiValueSetUInt(Item, "input_tokens", Event->InputTokens) &&
        MdoApiValueSetUInt(Item, "output_tokens", Event->OutputTokens) &&
        MdoApiValueSetUInt(Item, "total_tokens", Event->TotalTokens);
    if ( Ok && Event->Kind == XWORK_EVENT_AGENT_START &&
         Event->AgentDepth == 0u )
        Ok = MdoSessionAttachmentRunRead(ProjectId, SessionId,
            Event->RunId, Attachments, &AttachmentCount) &&
            MdoAttachmentIdsWriteValue(Item, Attachments, AttachmentCount);
    if ( !Ok ) { xrtValueRelease(Item); return false; }
    *pValue = Item;
    return true;
}

bool MdoApiSessionEventsRoute(MdoApiContext* Context)
{
    char ProjectId[MDO_PROJECT_ID_CAPACITY];
    char SessionId[MDO_SESSION_ID_CAPACITY];
    xwork_error Error;
    MdoSession* Session;
    MdoSessionEventSnapshot* Snapshot;
    xvalue* Data = xrtValueObject();
    xvalue* Items = xrtValueArray();
    uint64 After;
    size_t Limit;
    size_t Index;
    bool Ok;

    if ( !MdoApiCaptureId(Context, 0u, ProjectId, sizeof(ProjectId)) ||
         !MdoApiCaptureId(Context, 1u, SessionId, sizeof(SessionId)) )
        return MdoApiReplyError(Context, 400u, "invalid_path",
            "Project and session identifiers are invalid", NULL);
    if ( !MdoApiEventQuery(Context->Target.Query, &After, &Limit) )
        return MdoApiReplyError(Context, 400u, "invalid_query",
            "Only bounded numeric after and limit parameters are accepted",
            NULL);
    memset(&Error, 0, sizeof(Error));
    Session = MdoSessionLoad(ProjectId, SessionId, &Error);
    if ( Session == NULL ) return MdoApiReplyError(Context, 404u,
        "session_not_found", "The requested session does not exist", NULL);
    MdoSessionRelease(Session);
    memset(&Error, 0, sizeof(Error));
    Snapshot = MdoSessionEventReplay(ProjectId, SessionId, After, Limit, &Error);
    if ( Snapshot == NULL ) {
        xrtValueRelease(Data); xrtValueRelease(Items);
        return MdoApiReplyError(Context, 404u, "session_events_unavailable",
            "The session event stream does not exist or cannot be read", NULL);
    }
    Ok = Data != NULL && Items != NULL;
    for ( Index = 0u; Ok && Index < MdoSessionEventSnapshotCount(Snapshot);
          Index++ ) {
        MdoSessionEventInfo Event;
        xvalue* Item = NULL;
        memset(&Event, 0, sizeof(Event)); Event.Size = sizeof(Event);
        Ok = MdoSessionEventSnapshotAt(Snapshot, Index, &Event) &&
            MdoApiSessionEventValue(&Event, ProjectId, SessionId, &Item) &&
            MdoApiValueAppendTake(Items, &Item);
        xrtValueRelease(Item);
    }
    if ( Ok ) Ok =
        MdoApiValueSetString(Data, "stream", "session") &&
        MdoApiValueSetString(Data, "project_id", ProjectId) &&
        MdoApiValueSetString(Data, "session_id", SessionId) &&
        MdoApiValueSetUInt(Data, "after", After) &&
        MdoApiValueSetUInt(Data, "next_cursor",
            MdoSessionEventSnapshotNextCursor(Snapshot)) &&
        MdoApiValueSetUInt(Data, "latest_event_id",
            MdoSessionEventSnapshotLatestId(Snapshot)) &&
        MdoApiValueSetBool(Data, "history_lost",
            MdoSessionEventSnapshotHistoryLost(Snapshot)) &&
        MdoApiValueSetTake(Data, "items", &Items);
    xrtValueRelease(Items);
    MdoSessionEventSnapshotRelease(Snapshot);
    if ( !Ok ) { xrtValueRelease(Data); Data = NULL; }
    if ( Data == NULL ) return MdoApiReplyError(Context, 500u,
        "events_unavailable", "Session events could not be replayed", NULL);
    return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
}
