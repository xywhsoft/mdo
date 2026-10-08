#include <stdio.h>
#include <string.h>

#include "internal.h"
#include "../../include/mdo/attachments.h"
#include "../../include/mdo/sessions.h"

#define MDO_API_EVENT_DEFAULT_LIMIT 32u
#define MDO_API_EVENT_MAX_LIMIT 32u
#define MDO_API_EVENT_TEXT_BYTES 4096u
#define MDO_API_EVENT_SINGLE_TEXT_BYTES 65536u

static cstr MdoApiEventKindText(xwork_event_kind Kind)
{
    switch ( (uint32)Kind ) {
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
    case MDO_SESSION_EVENT_HISTORY_TRUNCATED: return "history_truncated";
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
        if ( Digit > Maximum || Value > (Maximum - Digit) / 10u ) return false;
        Value = Value * 10u + Digit;
    }
    *pValue = Value;
    return true;
}

static bool MdoApiEventQuery(xstrview Query, uint64* pAfter, size_t* pLimit,
    bool* pFullText, char Epoch[65])
{
    size_t Position = 0u;
    bool HasAfter = false;
    bool HasLimit = false;
    bool HasFullText = false;

    *pAfter = 0u;
    *pLimit = MDO_API_EVENT_DEFAULT_LIMIT;
    if ( pFullText != NULL ) *pFullText = false;
    Epoch[0] = '\0';
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
        } else if ( Name.Size == 9u &&
                    memcmp(Name.Data, "full_text", 9u) == 0 ) {
            if ( pFullText == NULL || HasFullText || Value.Size != 1u ||
                 Value.Data[0] != '1' ) return false;
            *pFullText = true;
            HasFullText = true;
        } else if (xrtStrEqual(Name, XRT_STR_LITERAL("epoch"))) {
            if (Epoch[0] || Value.Size != 64u) return false;
            for (size_t i = 0u; i < Value.Size; ++i)
                if (!((Value.Data[i] >= '0' && Value.Data[i] <= '9') ||
                      (Value.Data[i] >= 'a' && Value.Data[i] <= 'f'))) return false;
            memcpy(Epoch, Value.Data, 64u); Epoch[64] = '\0';
        } else return false;
        Position = End + (End < Query.Size ? 1u : 0u);
        if ( Position == Query.Size && End < Query.Size ) return false;
    }
    return !HasFullText || (HasAfter && HasLimit && *pLimit == 1u);
}

static xstrview MdoApiEventText(cstr Text, size_t Size, size_t Limit,
    bool* pTruncated)
{
    size_t Retained = Size;

    if ( Text == NULL ) return (xstrview){ NULL, 0u };
    if ( Retained > Limit ) {
        Retained = Limit;
        while ( Retained != 0u &&
               (((const unsigned char*)Text)[Retained] & 0xc0u) == 0x80u )
            Retained--;
        *pTruncated = true;
    }
    return xrtStrViewN(Text, Retained);
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

bool MdoApiSessionEventValue(const MdoSessionEventInfo* Event,
    const char* ProjectId, const char* SessionId, bool FullText,
    xvalue** pValue)
{
    xvalue* Item = xrtValueObject();
    char Attachments[4][33] = {{ 0 }};
    size_t AttachmentCount = 0u;
    size_t TextSize = Event->Text != NULL ? strlen(Event->Text) : 0u;
    bool Truncated = Event->TextTruncated;
    xstrview Text = MdoApiEventText(Event->Text, TextSize,
        FullText ? MDO_API_EVENT_SINGLE_TEXT_BYTES : MDO_API_EVENT_TEXT_BYTES,
        &Truncated);
    bool Ok = Item != NULL &&
        MdoApiValueSetUInt(Item, "schema_version", Event->SchemaVersion) &&
        MdoApiValueSetUInt(Item, "event_id", Event->EventId) &&
        MdoApiValueSetUInt(Item, "aggregate_end_id", Event->AggregateEndId) &&
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
        MdoApiValueSetString(Item, "model_id", Event->ModelId) &&
        MdoApiValueSetUInt(Item, "context_window_tokens",
            Event->ContextWindowTokens) &&
        MdoApiValueSetUInt(Item, "task_state", Event->TaskState) &&
        MdoApiValueSetUInt(Item, "task_revision", Event->TaskRevision);
    if (Ok) Ok = MdoApiValueSetUInt(Item, "input_tokens", Event->InputTokens) &&
        MdoApiValueSetUInt(Item, "output_tokens", Event->OutputTokens) &&
        MdoApiValueSetUInt(Item, "total_tokens", Event->TotalTokens);
    if (Ok && Event->Kind == XWORK_EVENT_ERROR)
        Ok = MdoApiValueSetString(Item, "model_error_kind", Event->ModelErrorKind) &&
            MdoApiValueSetUInt(Item, "model_http_status", Event->ModelHttpStatus) &&
            MdoApiValueSetUInt(Item, "model_attempts", Event->ModelAttempts);
    if ( Ok && Event->Kind == XWORK_EVENT_AGENT_START &&
         Event->AgentDepth == 0u )
        Ok = MdoSessionAttachmentEventRead(ProjectId, SessionId,
            Event->EventId, Event->RunId, Attachments, &AttachmentCount) &&
            MdoAttachmentIdsWriteValue(Item, Attachments, AttachmentCount);
    if ( !Ok ) { xrtValueRelease(Item); return false; }
    *pValue = Item;
    return true;
}

bool MdoApiSessionEventsRoute(MdoApiContext* Context)
{
    char Epoch[65];
    char ProjectId[MDO_PROJECT_ID_CAPACITY];
    char SessionId[MDO_SESSION_ID_CAPACITY];
    xwork_error Error;
    MdoSession* Session;
    MdoSessionEventSnapshot* Snapshot;
    xvalue* Data = xrtValueObject();
    xvalue* Items = xrtValueArray();
    uint64 After;
    size_t Limit;
    bool FullText;
    size_t Index;
    bool Ok;

    if ( !MdoApiCaptureId(Context, 0u, ProjectId, sizeof(ProjectId)) ||
         !MdoApiCaptureId(Context, 1u, SessionId, sizeof(SessionId)) )
        return MdoApiReplyError(Context, 400u, "invalid_path",
            "Project and session identifiers are invalid", NULL);
    if ( !MdoApiEventQuery(Context->Target.Query, &After, &Limit,
            &FullText, Epoch) )
        return MdoApiReplyError(Context, 400u, "invalid_query",
            "Only bounded event queries are accepted",
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
    if (Epoch[0] && strcmp(Epoch, MdoSessionEventSnapshotEpoch(Snapshot))) {
        xrtValueRelease(Data); xrtValueRelease(Items); MdoSessionEventSnapshotRelease(Snapshot);
        return MdoApiReplyError(Context, 409u, "conversation_changed", "Conversation history changed", NULL);
    }
    Ok = Data != NULL && Items != NULL;
    for ( Index = 0u; Ok && Index < MdoSessionEventSnapshotCount(Snapshot);
          Index++ ) {
        MdoSessionEventInfo Event;
        xvalue* Item = NULL;
        memset(&Event, 0, sizeof(Event)); Event.Size = sizeof(Event);
        Ok = MdoSessionEventSnapshotAt(Snapshot, Index, &Event) &&
            MdoApiSessionEventValue(&Event, ProjectId, SessionId,
                FullText, &Item) &&
            MdoApiValueAppendTake(Items, &Item);
        xrtValueRelease(Item);
    }
    if ( Ok ) Ok =
        MdoApiValueSetString(Data, "stream", "session") &&
        MdoApiValueSetString(Data, "project_id", ProjectId) &&
        MdoApiValueSetString(Data, "session_id", SessionId) &&
        MdoApiValueSetString(Data, "epoch", MdoSessionEventSnapshotEpoch(Snapshot)) &&
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

bool MdoApiSessionTurnsRoute(MdoApiContext* Context)
{
    char Project[MDO_PROJECT_ID_CAPACITY], Session[MDO_SESSION_ID_CAPACITY];
    uint64 Before = 0u, Limit = 4u;
    bool HasBefore = false, HasLimit = false;
    size_t Position = 0u;
    xstrview Query = Context->Target.Query;
    xwork_error Error;
    MdoSession* Loaded;
    xvalue* Data;
    if ( !MdoApiCaptureId(Context, 0u, Project, sizeof(Project)) ||
         !MdoApiCaptureId(Context, 1u, Session, sizeof(Session)) )
        return MdoApiReplyError(Context, 400u, "invalid_path", "Invalid session identity", NULL);
    while ( Position < Query.Size ) {
        size_t End = Position, Equal = SIZE_MAX;
        xstrview Name, Value;
        uint64 Number;
        while ( End < Query.Size && Query.Data[End] != '&' ) {
            if ( Query.Data[End] == '=' && Equal == SIZE_MAX ) Equal = End;
            ++End;
        }
        if ( Equal == SIZE_MAX || Equal <= Position || Equal + 1u >= End ) goto invalid;
        Name = xrtStrViewN(Query.Data + Position, Equal - Position);
        Value = xrtStrViewN(Query.Data + Equal + 1u, End - Equal - 1u);
        if ( Name.Size == 6u && memcmp(Name.Data, "before", 6u) == 0 ) {
            if ( HasBefore || !MdoApiUnsigned(Value, UINT64_MAX, &Number) ) goto invalid;
            HasBefore = true; Before = Number;
        } else if ( Name.Size == 5u && memcmp(Name.Data, "limit", 5u) == 0 ) {
            if ( HasLimit || !MdoApiUnsigned(Value, 64u, &Number) || Number == 0u ) goto invalid;
            HasLimit = true; Limit = Number;
        } else goto invalid;
        Position = End + (End < Query.Size ? 1u : 0u);
        if ( Position == Query.Size && End < Query.Size ) goto invalid;
    }
    xworkErrorInit(&Error);
    Loaded = MdoSessionLoad(Project, Session, &Error);
    if ( Loaded == NULL ) return MdoApiReplyError(Context, 404u, "session_not_found",
        "The requested session does not exist", NULL);
    MdoSessionRelease(Loaded);
    Data = MdoSessionConversationTurns(Project, Session, Before, (size_t)Limit, &Error);
    if ( Data == NULL ) return MdoApiReplyError(Context, 500u, "conversation_unavailable",
        "Conversation history cannot be read", NULL);
    return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
invalid:
    return MdoApiReplyError(Context, 400u, "invalid_query", "Only before and limit are accepted", NULL);
}

static bool MdoApiConversationHash(xstrview Bytes, char Hex[65])
{
    xsha256 Hash; uint8 Digest[32]; xrtSha256Init(&Hash);
    if (!xrtSha256Update(&Hash, Bytes.Data, Bytes.Size) || !xrtSha256Final(&Hash, Digest)) return false;
    for (size_t i = 0u; i < 32u; ++i) snprintf(Hex+i*2u, 3u, "%02x", Digest[i]);
    return true;
}

bool MdoApiSessionConversationRoute(MdoApiContext* Context)
{
    char Project[MDO_PROJECT_ID_CAPACITY], Session[MDO_SESSION_ID_CAPACITY], Epoch[65] = "", Hash[65];
    uint64 Before = 0u, After = 0u, Limit = 4u; unsigned Seen = 0u;
    xstrview Query = Context->Target.Query;
    xwork_error Error; MdoConversationPageInfo Page;
    MdoSession* Loaded = NULL; MdoSessionEventSnapshot* Snapshot = NULL;
    xvalue *Items = NULL, *Data = NULL; char* Json = NULL; size_t JsonSize = 0u;
    if (Context->ParamCount != 2u ||
        !MdoApiCaptureId(Context, 0u, Project, sizeof(Project)) ||
        !MdoApiCaptureId(Context, 1u, Session, sizeof(Session))) goto invalid;
    for (size_t Position = 0u; Position < Query.Size;) {
        size_t End = Position, Equal;
        while (End < Query.Size && Query.Data[End] != '&') ++End;
        Equal = Position;
        while (Equal < End && Query.Data[Equal] != '=') ++Equal;
        if (Equal == Position || Equal == End || Equal+1u == End) goto invalid;
        xstrview Name = xrtStrViewN(Query.Data+Position, Equal-Position);
        xstrview Value = xrtStrViewN(Query.Data+Equal+1u, End-Equal-1u);
        unsigned Bit = xrtStrEqual(Name, XRT_STR_LITERAL("before")) ? 1u :
            xrtStrEqual(Name, XRT_STR_LITERAL("after")) ? 2u :
            xrtStrEqual(Name, XRT_STR_LITERAL("limit")) ? 4u :
            xrtStrEqual(Name, XRT_STR_LITERAL("epoch")) ? 8u : 0u;
        if (!Bit || (Seen & Bit)) goto invalid;
        Seen |= Bit;
        if (Bit == 8u) {
            if (Value.Size != 64u) goto invalid;
            for (size_t i = 0u; i < Value.Size; ++i)
                if (!((Value.Data[i] >= '0' && Value.Data[i] <= '9') ||
                      (Value.Data[i] >= 'a' && Value.Data[i] <= 'f'))) goto invalid;
            memcpy(Epoch, Value.Data, Value.Size);
        } else {
            uint64 Number;
            if (!MdoApiUnsigned(Value, Bit == 4u ? 4u : UINT64_MAX, &Number) ||
                (Bit == 4u && !Number)) goto invalid;
            if (Bit == 1u) Before = Number; else if (Bit == 2u) After = Number; else Limit = Number;
        }
        Position = End + (End < Query.Size ? 1u : 0u);
        if (Position == Query.Size && End < Query.Size) goto invalid;
    }
    if (Before && After) goto invalid;
    Loaded = MdoSessionLoad(Project, Session, &Error);
    if (!Loaded) return MdoApiReplyError(Context, 404u, "session_not_found", "The requested session does not exist", NULL);
    Snapshot = MdoSessionConversationPage(Project, Session, Before, After, Epoch, (size_t)Limit, &Page, &Error);
    MdoSessionRelease(Loaded);
    if (!Snapshot) return MdoApiReplyError(Context, Error.eCode == XWORK_ERROR_CONTEXT ? 409u : 503u,
        "conversation_changed", "Conversation changed while reading; retry the snapshot", NULL);
    Items = xrtValueArray(); Data = xrtValueObject();
    if (!Items || !Data) goto fail;
    for (size_t i = 0u; i < MdoSessionEventSnapshotCount(Snapshot); ++i) {
        MdoSessionEventInfo Event; xvalue* Item = NULL; char Node[256];
        Event.Size = sizeof(Event);
        if (!MdoSessionEventSnapshotAt(Snapshot, i, &Event) ||
            !MdoApiSessionEventValue(&Event, Project, Session,
                Event.Kind == XWORK_EVENT_MODEL_TEXT_DELTA || Event.Kind == XWORK_EVENT_MODEL_REASONING_DELTA, &Item)) goto fail;
        if (!MdoApiValueSetString(Item, "projection_epoch", Page.Epoch)) { xrtValueRelease(Item); goto fail; }
        snprintf(Node, sizeof(Node), "%s:%s:%llu", Session, Page.Epoch, (unsigned long long)Event.EventId);
        size_t Size = 0u; char* Encoded = xrtJsonStringify(Item, false, &Size);
        bool Ok = Encoded && MdoApiConversationHash(xrtStrViewN(Encoded, Size), Hash) &&
            MdoApiValueSetString(Item, "content_hash", Hash) && MdoApiValueSetString(Item, "node_id", Node);
        xrtFree(Encoded);
        if (Ok) Ok = MdoApiValueAppendTake(Items, &Item);
        xrtValueRelease(Item);
        if (!Ok) goto fail;
    }
    Json = xrtJsonStringify(Items, false, &JsonSize);
    if (!Json || !MdoApiConversationHash(xrtStrViewN(Json, JsonSize), Hash) ||
        !MdoApiValueSetString(Data, "project_id", Project) ||
        !MdoApiValueSetString(Data, "session_id", Session) ||
        !MdoApiValueSetString(Data, "epoch", Page.Epoch) ||
        !MdoApiValueSetString(Data, "items_hash", Hash) ||
        !MdoApiValueSetStringView(Data, "items_json", xrtStrViewN(Json, JsonSize)) ||
        !MdoApiValueSetUInt(Data, "latest_event_id", MdoSessionEventSnapshotLatestId(Snapshot)) ||
        !MdoApiValueSetUInt(Data, "next_cursor", MdoSessionEventSnapshotNextCursor(Snapshot)) ||
        !MdoApiValueSetUInt(Data, "next_before", Page.NextBefore) ||
        !MdoApiValueSetBool(Data, "delta", Page.Delta) ||
        !MdoApiValueSetBool(Data, "has_more", Page.HasMore) ||
        !MdoApiValueSetBool(Data, "history_lost", MdoSessionEventSnapshotHistoryLost(Snapshot))) goto fail;
    xrtFree(Json); xrtValueRelease(Items); MdoSessionEventSnapshotRelease(Snapshot);
    return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
invalid:
    return MdoApiReplyError(Context, 400u, "invalid_query", "Only bounded before, after, epoch and limit are accepted", NULL);
fail:
    xrtFree(Json); xrtValueRelease(Items); xrtValueRelease(Data); MdoSessionEventSnapshotRelease(Snapshot);
    return MdoApiReplyError(Context, 500u, "conversation_unavailable", "Conversation snapshot could not be serialized", NULL);
}
