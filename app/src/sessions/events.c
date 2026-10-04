#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#include "internal.h"
#include "../../include/mdo/attachments.h"
#include "../../include/mdo/home.h"

#define MDO_SESSION_EVENT_SCHEMA 5u
#define MDO_SESSION_EVENT_RECORD_LIMIT (96u * 1024u)
#define MDO_SESSION_EVENT_TEXT_LIMIT (64u * 1024u)
#define MDO_SESSION_EVENT_METADATA_LIMIT 4096u
#define MDO_SESSION_EVENT_REPLAY_DEFAULT 100u
#define MDO_SESSION_EVENT_REPLAY_MAX 1000u

typedef struct MdoSessionEventOwned {
    MdoSessionEventInfo Info;
    char* Text;
    char* ToolName;
    char* ToolCallId;
    char* ArtifactPath;
    char* Model;
    char* ModelId;
} MdoSessionEventOwned;

struct MdoSessionEventSnapshot {
    xatomic32 Refs;
    MdoSessionEventOwned* Events;
    size_t Count;
    size_t Capacity;
    uint64 NextCursor;
    uint64 LatestId;
    bool HistoryLost;
};

struct MdoSessionEventBridge {
    xatomic32 Refs;
    xmutex* Lock;
    xfile RuntimeLock;
    MdoProjectLease* ProjectLease;
    char ProjectId[MDO_PROJECT_ID_CAPACITY];
    char SessionId[MDO_SESSION_ID_CAPACITY];
    char ModelId[MDO_SESSION_IDENTITY_CAPACITY];
    uint64 ContextWindowTokens;
    char Path[MDO_SESSION_PATH_CAPACITY];
    uint64 NextEventId;
    xwork_event_fn UserEvent;
    void* UserEventData;
    void* UserOwnerData;
    xwork_agent_owner_release_fn UserOwnerRelease;
    bool UserOwnerRetained;
    bool Registered;
    uint64 PendingRunId;
    char PendingIds[4][33];
    size_t PendingCount;
    bool PendingEmptyPrompt;
    char PendingQueueItemId[33];
};

struct MdoSessionEventTrimPlan {
    MdoSessionEventBridge* Bridge;
    char* Data;
    size_t Keep;
    uint64 NextEventId;
    uint64 RemovedFromEventId;
    bool Clear;
    bool Changed;
};

static void MdoEventsError(xwork_error* Error, xwork_error_code Code,
    const char* Message)
{
    if ( Error == NULL ) return;
    xworkErrorInit(Error);
    Error->eCode = Code;
    snprintf(Error->sMessage, sizeof(Error->sMessage), "%s",
        Message != NULL ? Message : "session event operation failed");
}

static void MdoEventsXrtError(xwork_error* Error, const char* Fallback)
{
    const xerror* Cause = xrtGetError();
    MdoEventsError(Error, XWORK_ERROR_IO,
        Cause != NULL && xrtErrorMessage(Cause) != NULL ?
        xrtErrorMessage(Cause) : Fallback);
}

static bool MdoEventsIdValid(const char* Text, size_t Capacity)
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

static bool MdoEventsPath(char Output[MDO_SESSION_PATH_CAPACITY],
    const char* ProjectId, const char* SessionId)
{
    int Written = snprintf(Output, MDO_SESSION_PATH_CAPACITY,
        "sessions/%s/%s/ui-events.jsonl", ProjectId, SessionId);
    return Written > 0 && (size_t)Written < MDO_SESSION_PATH_CAPACITY;
}

static bool MdoEventsRuntimeLockPath(char Output[MDO_SESSION_PATH_CAPACITY],
    const char* ProjectId, const char* SessionId)
{
    int Written = snprintf(Output, MDO_SESSION_PATH_CAPACITY,
        "sessions/%s/%s/.runtime.lock", ProjectId, SessionId);
    return Written > 0 && (size_t)Written < MDO_SESSION_PATH_CAPACITY;
}

static bool MdoEventsRead(const char* Path, char** Data, size_t* Size)
{
    xfile File = NULL;
    xfileinfo Info;
    char* Bytes = NULL;
    bool Ok = false;
    *Data = NULL;
    *Size = 0u;
    File = MdoHomeOpenRead(Path);
    if ( File == NULL || !xrtFileStat(File, &Info) ||
         (Info.Available & XFILE_INFO_SIZE) == 0u ||
         Info.Type != XFILE_TYPE_FILE ||
         Info.Size > SIZE_MAX - 1u ) goto done;
    Bytes = (char*)xrtMalloc((size_t)Info.Size + 1u);
    if ( Bytes == NULL ||
         (Info.Size != 0u && !xrtReadFull(File, Bytes,
            (size_t)Info.Size, NULL)) ) goto done;
    Bytes[Info.Size] = '\0';
    *Data = Bytes;
    *Size = (size_t)Info.Size;
    Bytes = NULL;
    Ok = true;
done:
    if ( File != NULL && !xrtClose(File) ) Ok = false;
    xrtFree(Bytes);
    if ( !Ok ) {
        xrtFree(*Data);
        *Data = NULL;
        *Size = 0u;
    }
    return Ok;
}

static bool MdoEventsObjectTake(xvalue* Object, const char* Key,
    xvalue* Value)
{
    bool Ok = Object != NULL && Value != NULL &&
        xrtValueObjectSetTake(Object, xrtStrView(Key), &Value);
    xrtValueRelease(Value);
    return Ok;
}

static bool MdoEventsObjectString(xvalue* Object, const char* Key,
    const char* Text, size_t Size)
{
    return MdoEventsObjectTake(Object, Key,
        xrtValueString(xrtStrViewN(Text != NULL ? Text : "", Size)));
}

static bool MdoEventsBoundedView(const char* Text, size_t Claimed,
    size_t Limit, xstrview* View, bool* Truncated)
{
    size_t Size;
    if ( Text == NULL ) {
        View->Data = "";
        View->Size = 0u;
        return true;
    }
    if ( Claimed != SIZE_MAX ) Size = Claimed;
    else {
        for ( Size = 0u; Size < Limit && Text[Size] != '\0'; ++Size ) {}
        if ( Size == Limit && Text[Size] != '\0' ) *Truncated = true;
    }
    if ( Size > Limit ) {
        Size = Limit;
        *Truncated = true;
        while ( Size != 0u &&
                (((unsigned char)Text[Size] & 0xc0u) == 0x80u) ) --Size;
    }
    View->Data = Text;
    View->Size = Size;
    return xrtUtf8Valid(*View, NULL);
}

static char* MdoEventsRecord(const MdoSessionEventBridge* Bridge,
    uint64 EventId, const xwork_event* Event, const char* ModelId,
    uint64 ContextWindowTokens, size_t* Size)
{
    xvalue* Object = xrtValueObject();
    xstrview Text;
    xstrview ToolName;
    xstrview ToolCallId;
    xstrview ArtifactPath;
    xstrview Model;
    const char* QueueItemId = Event->eKind == XWORK_EVENT_AGENT_START &&
        Event->uAgentDepth == 0u &&
        Event->uRunId == Bridge->PendingRunId ?
            Bridge->PendingQueueItemId : "";
    bool Truncated = Event->bTextTruncated;
    char* Json = NULL;
    if ( Object == NULL ||
         !MdoEventsBoundedView(Event->sText, Event->iTextLength,
            MDO_SESSION_EVENT_TEXT_LIMIT, &Text, &Truncated) ||
         !MdoEventsBoundedView(Event->sToolName, SIZE_MAX,
            MDO_SESSION_EVENT_METADATA_LIMIT, &ToolName, &Truncated) ||
         !MdoEventsBoundedView(Event->sToolCallId, SIZE_MAX,
            MDO_SESSION_EVENT_METADATA_LIMIT, &ToolCallId, &Truncated) ||
         !MdoEventsBoundedView(Event->sArtifactPath, SIZE_MAX,
            MDO_SESSION_EVENT_METADATA_LIMIT, &ArtifactPath, &Truncated) ||
         !MdoEventsBoundedView(Event->sModel, SIZE_MAX,
            MDO_SESSION_EVENT_METADATA_LIMIT, &Model, &Truncated) ||
         !MdoEventsObjectTake(Object, "schema_version",
            xrtValueUInt(MDO_SESSION_EVENT_SCHEMA)) ||
         !MdoEventsObjectTake(Object, "event_id", xrtValueUInt(EventId)) ||
         !MdoEventsObjectTake(Object, "source_event_id",
            xrtValueUInt(Event->uEventId)) ||
         !MdoEventsObjectTake(Object, "occurred_at_us",
            xrtValueInt(Event->iOccurredAtUs)) ||
         !MdoEventsObjectString(Object, "project_id", Bridge->ProjectId,
            strlen(Bridge->ProjectId)) ||
         !MdoEventsObjectString(Object, "session_id", Bridge->SessionId,
            strlen(Bridge->SessionId)) ||
         !MdoEventsObjectTake(Object, "kind", xrtValueUInt(Event->eKind)) ||
         !MdoEventsObjectTake(Object, "agent_turn",
            xrtValueUInt(Event->uAgentTurn)) ||
         !MdoEventsObjectTake(Object, "user_message_sequence",
            xrtValueUInt(Event->uUserMessageSequence)) ||
         !MdoEventsObjectTake(Object, "agent_depth",
            xrtValueUInt(Event->uAgentDepth)) ||
         !MdoEventsObjectTake(Object, "agent_id",
            xrtValueUInt(Event->uAgentId)) ||
         !MdoEventsObjectTake(Object, "run_id", xrtValueUInt(Event->uRunId)) ||
         !MdoEventsObjectString(Object, "queue_item_id",
            QueueItemId, strlen(QueueItemId)) ||
         !MdoEventsObjectTake(Object, "task_id",
            xrtValueUInt(Event->uTaskId)) ||
         !MdoEventsObjectTake(Object, "artifact_id",
            xrtValueUInt(Event->uArtifactId)) ||
         !MdoEventsObjectTake(Object, "parent_run_id",
            xrtValueUInt(Event->uParentRunId)) ||
         !MdoEventsObjectTake(Object, "effects",
            xrtValueUInt(Event->uEffects)) ||
         !MdoEventsObjectTake(Object, "task_state",
            xrtValueUInt(Event->eTaskState)) ||
         !MdoEventsObjectTake(Object, "task_revision",
            xrtValueUInt(Event->uTaskRevision)) ||
         !MdoEventsObjectTake(Object, "input_tokens",
            xrtValueUInt(Event->tUsage.uInputTokens)) ||
         !MdoEventsObjectTake(Object, "output_tokens",
            xrtValueUInt(Event->tUsage.uOutputTokens)) ||
         !MdoEventsObjectTake(Object, "total_tokens",
            xrtValueUInt(Event->tUsage.uTotalTokens)) ||
         !MdoEventsObjectTake(Object, "success",
            xrtValueBool(Event->bSuccess)) ||
         !MdoEventsObjectTake(Object, "effect_applied",
            xrtValueBool(Event->bEffectApplied)) ||
         !MdoEventsObjectTake(Object, "text_truncated",
            xrtValueBool(Truncated)) ||
         !MdoEventsObjectString(Object, "text", Text.Data, Text.Size) ||
         !MdoEventsObjectString(Object, "tool_name", ToolName.Data,
            ToolName.Size) ||
         !MdoEventsObjectString(Object, "tool_call_id", ToolCallId.Data,
            ToolCallId.Size) ||
         !MdoEventsObjectString(Object, "artifact_path", ArtifactPath.Data,
            ArtifactPath.Size) ||
         !MdoEventsObjectString(Object, "model", Model.Data, Model.Size) ||
         !MdoEventsObjectString(Object, "model_id", ModelId,
            strlen(ModelId)) ||
         !MdoEventsObjectTake(Object, "context_window_tokens",
            xrtValueUInt(ContextWindowTokens)) )
        goto done;
    Json = xrtJsonStringify(Object, false, Size);
    if ( Json != NULL && *Size > MDO_SESSION_EVENT_RECORD_LIMIT ) {
        xrtFree(Json);
        Json = NULL;
    }
done:
    xrtValueRelease(Object);
    return Json;
}

static bool MdoEventsAppendBytes(const char* Path, const char* Json,
    size_t Size)
{
    xfile File = MdoHomeOpenWrite(Path,
        XFILE_CREATE | XFILE_APPEND | XFILE_SYNC);
    bool Ok;
    if ( File == NULL ) return false;
    Ok = xrtWriteFull(File, Json, Size, NULL) &&
        xrtWriteFull(File, "\n", 1u, NULL) && xrtFlush(File);
    if ( !xrtClose(File) ) Ok = false;
    return Ok;
}

static bool MdoEventsAppend(MdoSessionEventBridge* Bridge,
    const xwork_event* Event)
{
    xfileinfo Info;
    bool Exists = false;
    char* Json;
    size_t Size = 0u;
    bool Ok;
    bool MainModelCall = Event->eKind == XWORK_EVENT_MODEL_DONE &&
        Event->uAgentDepth == 0u;
    if ( Bridge->NextEventId == 0u || Bridge->NextEventId == UINT64_MAX )
        return false;
    if ( MainModelCall && Bridge->ModelId[0] == '\0' ) return false;
    Json = MdoEventsRecord(Bridge, Bridge->NextEventId, Event,
        MainModelCall ? Bridge->ModelId : "",
        MainModelCall ? Bridge->ContextWindowTokens : 0u, &Size);
    if ( Json == NULL ||
         !MdoHomeExternalStat(Bridge->Path, &Exists, &Info) ) {
        xrtFree(Json);
        return false;
    }
    if ( Exists && (Info.Type != XFILE_TYPE_FILE ||
         (Info.Available & XFILE_INFO_SIZE) == 0u) ) {
        xrtFree(Json);
        return false;
    }
    /* Display history is independent of model context compaction. Only an
     * explicit user clear/edit may remove records; append never evicts them. */
    Ok = MdoEventsAppendBytes(Bridge->Path, Json, Size);
    if ( Ok ) {
        MdoSessionsInternalPublish(Bridge->ProjectId, Bridge->SessionId,
            xrtStrViewN(Json, Size), Event->eKind != XWORK_EVENT_MODEL_TEXT_DELTA &&
                Event->eKind != XWORK_EVENT_MODEL_REASONING_DELTA);
        ++Bridge->NextEventId;
    }
    xrtFree(Json);
    return Ok;
}

static bool MdoEventsValueUInt(const xvalue* Object, const char* Key,
    uint64* Result)
{
    const xvalue* Value = xrtValueObjectGet(Object, xrtStrView(Key));
    int64 Signed;
    if ( Value == NULL ) return false;
    if ( xrtValueType(Value) == XVALUE_UINT )
        return xrtValueGetUInt(Value, Result);
    if ( xrtValueType(Value) != XVALUE_INT ||
         !xrtValueGetInt(Value, &Signed) || Signed < 0 ) return false;
    *Result = (uint64)Signed;
    return true;
}

static bool MdoEventsValueInt(const xvalue* Object, const char* Key,
    int64* Result)
{
    const xvalue* Value = xrtValueObjectGet(Object, xrtStrView(Key));
    uint64 Unsigned;
    if ( Value == NULL ) return false;
    if ( xrtValueType(Value) == XVALUE_INT )
        return xrtValueGetInt(Value, Result);
    if ( xrtValueType(Value) != XVALUE_UINT ||
         !xrtValueGetUInt(Value, &Unsigned) || Unsigned > INT64_MAX )
        return false;
    *Result = (int64)Unsigned;
    return true;
}

static bool MdoEventsValueBool(const xvalue* Object, const char* Key,
    bool* Result)
{
    const xvalue* Value = xrtValueObjectGet(Object, xrtStrView(Key));
    return Value != NULL && xrtValueType(Value) == XVALUE_BOOL &&
        xrtValueGetBool(Value, Result);
}

static bool MdoEventsValueString(const xvalue* Object, const char* Key,
    xstrview* Result)
{
    const xvalue* Value = xrtValueObjectGet(Object, xrtStrView(Key));
    return Value != NULL && xrtValueType(Value) == XVALUE_STRING &&
        xrtValueGetString(Value, Result);
}

static char* MdoEventsCopy(xstrview Text, size_t Limit)
{
    char* Copy;
    if ( Text.Size > Limit || !xrtUtf8Valid(Text, NULL) ) return NULL;
    Copy = (char*)xrtMalloc(Text.Size + 1u);
    if ( Copy == NULL ) return NULL;
    memcpy(Copy, Text.Data, Text.Size);
    Copy[Text.Size] = '\0';
    return Copy;
}

static void MdoEventsOwnedUnit(MdoSessionEventOwned* Event)
{
    if ( Event == NULL ) return;
    xrtFree(Event->Text);
    xrtFree(Event->ToolName);
    xrtFree(Event->ToolCallId);
    xrtFree(Event->ArtifactPath);
    xrtFree(Event->Model);
    xrtFree(Event->ModelId);
    memset(Event, 0, sizeof(*Event));
}

static bool MdoEventsParse(const char* ProjectId, const char* SessionId,
    xstrview Json, MdoSessionEventOwned* Result)
{
    xjsonreadconfig Config;
    xvalue* Root;
    xstrview Project;
    xstrview Session;
    xstrview Text;
    xstrview ToolName;
    xstrview ToolCallId;
    xstrview ArtifactPath;
    xstrview Model;
    xstrview ModelId = xrtStrView("");
    xstrview QueueItemId = xrtStrView("");
    uint64 Schema;
    uint64 Kind;
    uint64 AgentDepth;
    uint64 Effects;
    uint64 TaskState;
    bool Ok = false;
    memset(Result, 0, sizeof(*Result));
    Result->Info.Size = sizeof(Result->Info);
    xrtJsonReadConfigInit(&Config);
    Config.MaxInputBytes = MDO_SESSION_EVENT_RECORD_LIMIT;
    Config.MaxDepth = 6u;
    Config.MaxValues = 40u;
    Config.MaxContainerItems = 32u;
    Root = xrtJsonRead(Json, &Config);
    if ( Root == NULL || xrtValueType(Root) != XVALUE_OBJECT ||
         (xrtValueCount(Root) != 25u && xrtValueCount(Root) != 28u &&
          xrtValueCount(Root) != 29u && xrtValueCount(Root) != 31u &&
          xrtValueCount(Root) != 32u) ||
         !MdoEventsValueUInt(Root, "schema_version", &Schema) ||
         !((Schema == 1u && xrtValueCount(Root) == 25u) ||
           (Schema == 2u && xrtValueCount(Root) == 28u) ||
           (Schema == 3u && xrtValueCount(Root) == 29u) ||
           (Schema == 4u && xrtValueCount(Root) == 31u) ||
           (Schema == MDO_SESSION_EVENT_SCHEMA &&
            xrtValueCount(Root) == 32u)) ||
         !MdoEventsValueUInt(Root, "event_id", &Result->Info.EventId) ||
         Result->Info.EventId == 0u ||
         !MdoEventsValueUInt(Root, "source_event_id",
            &Result->Info.SourceEventId) ||
         !MdoEventsValueInt(Root, "occurred_at_us", &Result->Info.OccurredAt) ||
         !MdoEventsValueString(Root, "project_id", &Project) ||
         !MdoEventsValueString(Root, "session_id", &Session) ||
         Project.Size != strlen(ProjectId) ||
         memcmp(Project.Data, ProjectId, Project.Size) != 0 ||
         Session.Size != strlen(SessionId) ||
         memcmp(Session.Data, SessionId, Session.Size) != 0 ||
         !MdoEventsValueUInt(Root, "kind", &Kind) ||
         Kind > (uint64)MDO_SESSION_EVENT_HISTORY_TRUNCATED ||
         !MdoEventsValueUInt(Root, "agent_turn", &Result->Info.AgentTurn) ||
         (Schema >= 3u && !MdoEventsValueUInt(Root,
            "user_message_sequence", &Result->Info.UserMessageSequence)) ||
         !MdoEventsValueUInt(Root, "agent_depth", &AgentDepth) ||
         AgentDepth > UINT32_MAX ||
         !MdoEventsValueUInt(Root, "agent_id", &Result->Info.AgentId) ||
         !MdoEventsValueUInt(Root, "run_id", &Result->Info.RunId) ||
         (Schema >= 5u &&
          !MdoEventsValueString(Root, "queue_item_id", &QueueItemId)) ||
         (QueueItemId.Size != 0u &&
          (QueueItemId.Size != 32u ||
           Kind != (uint64)XWORK_EVENT_AGENT_START || AgentDepth != 0u)) ||
         !MdoEventsValueUInt(Root, "task_id", &Result->Info.TaskId) ||
         !MdoEventsValueUInt(Root, "artifact_id", &Result->Info.ArtifactId) ||
         !MdoEventsValueUInt(Root, "parent_run_id", &Result->Info.ParentRunId) ||
         !MdoEventsValueUInt(Root, "effects", &Effects) ||
         Effects > UINT32_MAX ||
         !MdoEventsValueUInt(Root, "task_state", &TaskState) ||
         TaskState > UINT32_MAX ||
         !MdoEventsValueUInt(Root, "task_revision",
            &Result->Info.TaskRevision) ||
         (Schema >= 2u &&
          (!MdoEventsValueUInt(Root, "input_tokens",
             &Result->Info.InputTokens) ||
           !MdoEventsValueUInt(Root, "output_tokens",
             &Result->Info.OutputTokens) ||
           !MdoEventsValueUInt(Root, "total_tokens",
             &Result->Info.TotalTokens))) ||
         !MdoEventsValueBool(Root, "success", &Result->Info.Success) ||
         !MdoEventsValueBool(Root, "effect_applied",
            &Result->Info.EffectApplied) ||
         !MdoEventsValueBool(Root, "text_truncated",
            &Result->Info.TextTruncated) ||
         !MdoEventsValueString(Root, "text", &Text) ||
         !MdoEventsValueString(Root, "tool_name", &ToolName) ||
         !MdoEventsValueString(Root, "tool_call_id", &ToolCallId) ||
         !MdoEventsValueString(Root, "artifact_path", &ArtifactPath) ||
         !MdoEventsValueString(Root, "model", &Model) ||
         (Schema >= 4u &&
          (!MdoEventsValueString(Root, "model_id", &ModelId) ||
           !MdoEventsValueUInt(Root, "context_window_tokens",
              &Result->Info.ContextWindowTokens))) ) goto done;
    if ( QueueItemId.Size != 0u ) {
        size_t Index;
        for ( Index = 0u; Index < QueueItemId.Size; ++Index ) {
            unsigned char Byte = (unsigned char)QueueItemId.Data[Index];
            if ( !((Byte >= '0' && Byte <= '9') ||
                   (Byte >= 'a' && Byte <= 'f')) ) goto done;
        }
        memcpy(Result->Info.QueueItemId, QueueItemId.Data, 32u);
    }
    Result->Text = MdoEventsCopy(Text, MDO_SESSION_EVENT_TEXT_LIMIT);
    Result->ToolName = MdoEventsCopy(ToolName, MDO_SESSION_EVENT_METADATA_LIMIT);
    Result->ToolCallId = MdoEventsCopy(ToolCallId,
        MDO_SESSION_EVENT_METADATA_LIMIT);
    Result->ArtifactPath = MdoEventsCopy(ArtifactPath,
        MDO_SESSION_EVENT_METADATA_LIMIT);
    Result->Model = MdoEventsCopy(Model, MDO_SESSION_EVENT_METADATA_LIMIT);
    Result->ModelId = MdoEventsCopy(ModelId,
        MDO_SESSION_IDENTITY_CAPACITY - 1u);
    if ( Result->Text == NULL || Result->ToolName == NULL ||
         Result->ToolCallId == NULL || Result->ArtifactPath == NULL ||
         Result->Model == NULL || Result->ModelId == NULL ) goto done;
    Result->Info.SchemaVersion = (uint32)Schema;
    Result->Info.Kind = (xwork_event_kind)Kind;
    Result->Info.AgentDepth = (uint32)AgentDepth;
    Result->Info.Effects = (xwork_tool_effects)Effects;
    Result->Info.TaskState = (uint32)TaskState;
    Result->Info.Text = Result->Text;
    Result->Info.ToolName = Result->ToolName;
    Result->Info.ToolCallId = Result->ToolCallId;
    Result->Info.ArtifactPath = Result->ArtifactPath;
    Result->Info.Model = Result->Model;
    Result->Info.ModelId = Result->ModelId;
    Ok = true;
done:
    xrtValueRelease(Root);
    if ( !Ok ) MdoEventsOwnedUnit(Result);
    return Ok;
}

bool MdoSessionsInternalEventVisit(const char* ProjectId, const char* SessionId,
    xstrview Json, MdoSessionEventVisitor Visitor, void* Data)
{
    MdoSessionEventOwned Event;
    bool Ok;
    if ( ProjectId == NULL || SessionId == NULL ) return false;
    Ok = MdoEventsParse(ProjectId, SessionId, Json, &Event);
    if ( Ok && Visitor != NULL ) Ok = Visitor(&Event.Info, Data);
    MdoEventsOwnedUnit(&Event);
    return Ok;
}

#include "event_reader.inc.c"
#include "conversation_turns.inc.c"

static bool MdoEventsScanLatestPath(const char* Project, const char* Session,
    const char* Path, uint64* Latest, bool* Incomplete)
{
    MdoEventReader* Reader = MdoEventReaderOpen(Path);
    uint64 LastNewline = 0u;
    bool Lost = false;
    bool Ok;
    if ( Reader == NULL ) return false;
    *Latest = MdoEventReaderLatest(Reader, Project, Session, &Lost);
    *Incomplete = Reader->Size != 0u &&
        (!MdoEventReaderNewline(Reader, Reader->Size, &LastNewline) ||
         LastNewline + 1u != Reader->Size);
    Ok = !Reader->Failed;
    MdoEventReaderClose(Reader);
    return Ok;
}

MdoSessionEventBridge* MdoSessionEventBridgeCreate(
    const char* ProjectId, const char* SessionId,
    MdoProjectLease* ProjectLease,
    xwork_event_fn UserEvent, void* UserEventData,
    void* UserOwnerData, xwork_agent_owner_retain_fn UserOwnerRetain,
    xwork_agent_owner_release_fn UserOwnerRelease, xwork_error* Error)
{
    MdoSessionEventBridge* Bridge;
    bool Exists = false;
    xfileinfo Info;
    uint64 Latest = 0u;
    bool Incomplete = false;
    char RuntimeLockPath[MDO_SESSION_PATH_CAPACITY];
    xworkErrorInit(Error);
    if ( ProjectLease == NULL ||
         !MdoEventsIdValid(ProjectId, MDO_PROJECT_ID_CAPACITY) ||
         !MdoEventsIdValid(SessionId, MDO_SESSION_ID_CAPACITY) ||
         ((UserOwnerRetain != NULL) != (UserOwnerRelease != NULL)) ) {
        MdoEventsError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "invalid session event bridge request");
        return NULL;
    }
    Bridge = (MdoSessionEventBridge*)xrtCalloc(1u, sizeof(*Bridge));
    if ( Bridge == NULL ) {
        MdoEventsError(Error, XWORK_ERROR_OUT_OF_MEMORY,
            "cannot allocate the session event bridge");
        return NULL;
    }
    xrtAtomic32Init(&Bridge->Refs, 1u);
    Bridge->ProjectLease = MdoProjectLeaseRef(ProjectLease);
    if ( Bridge->ProjectLease == NULL ) {
        MdoEventsError(Error, XWORK_ERROR_LIMIT,
            "cannot retain the session project lease");
        goto fail;
    }
    Bridge->Lock = xrtMutexCreate();
    snprintf(Bridge->ProjectId, sizeof(Bridge->ProjectId), "%s", ProjectId);
    snprintf(Bridge->SessionId, sizeof(Bridge->SessionId), "%s", SessionId);
    Bridge->UserEvent = UserEvent;
    Bridge->UserEventData = UserEventData;
    Bridge->UserOwnerData = UserOwnerData;
    Bridge->UserOwnerRelease = UserOwnerRelease;
    if ( Bridge->Lock == NULL ||
         !MdoEventsPath(Bridge->Path, ProjectId, SessionId) ||
         !MdoEventsRuntimeLockPath(RuntimeLockPath, ProjectId, SessionId) )
        goto memory;
    if ( UserOwnerRetain != NULL ) {
        if ( !UserOwnerRetain(UserOwnerData) ) {
            MdoEventsError(Error, XWORK_ERROR_CONTEXT,
                "session callback owner is no longer retainable");
            goto fail;
        }
        Bridge->UserOwnerRetained = true;
    }
    Bridge->RuntimeLock = MdoHomeOpenWrite(RuntimeLockPath,
        XFILE_READ | XFILE_CREATE | XFILE_SYNC);
    if ( Bridge->RuntimeLock == NULL ||
         !xrtFileLock(Bridge->RuntimeLock, XFILE_LOCK_EXCLUSIVE, false) ) {
        MdoEventsError(Error, XWORK_ERROR_CONTEXT,
            "session runtime is locked by another process");
        goto fail;
    }
    if ( !MdoHomeExternalStat(Bridge->Path, &Exists, &Info) ) goto io;
    if ( Exists ) {
        if ( Info.Type != XFILE_TYPE_FILE ||
             (Info.Available & XFILE_INFO_SIZE) == 0u ||
             !MdoEventsScanLatestPath(ProjectId, SessionId, Bridge->Path,
                &Latest, &Incomplete) ) goto io;
        if ( Incomplete && !MdoEventsAppendBytes(Bridge->Path, "", 0u) )
            goto io;
    }
    if ( Latest == UINT64_MAX ) {
        MdoEventsError(Error, XWORK_ERROR_LIMIT,
            "session event identity space is exhausted");
        goto fail;
    }
    Bridge->NextEventId = Latest + 1u;
    return Bridge;
memory:
    MdoEventsError(Error, XWORK_ERROR_OUT_OF_MEMORY,
        "cannot allocate the session event bridge");
    goto fail;
io:
    MdoEventsXrtError(Error, "cannot inspect the session event journal");
fail:
    MdoSessionEventBridgeRelease(Bridge);
    return NULL;
}

/* Forks are prepared before the child Agent is published. Rebuild the UI
 * journal under the child's identity, keeping only completed source runs
 * before the requested user-message boundary. The result is published in one
 * atomic write, without discarding an older prefix to meet a byte quota. */
bool MdoSessionEventBridgeClonePrefix(MdoSessionEventBridge* Bridge,
    const char* SourceProjectId, const char* SourceSessionId,
    uint64 ThroughSequence, xwork_error* Error)
{
    char SourcePath[MDO_SESSION_PATH_CAPACITY];
    char* Source = NULL;
    char* Output = NULL;
    size_t SourceSize = 0u;
    size_t OutputSize = 0u;
    size_t Capacity = 0u;
    size_t Start = 0u;
    uint64 LastSourceEventId = 0u;
    xfileinfo Info;
    bool Exists = false;
    bool Ok = false;
    xworkErrorInit(Error);
    if ( Bridge == NULL || Bridge->NextEventId != 1u ||
         !MdoEventsIdValid(SourceProjectId, MDO_PROJECT_ID_CAPACITY) ||
         !MdoEventsIdValid(SourceSessionId, MDO_SESSION_ID_CAPACITY) ||
         !MdoEventsPath(SourcePath, SourceProjectId, SourceSessionId) ) {
        MdoEventsError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "invalid event journal fork request");
        return false;
    }
    if ( ThroughSequence == 0u ) return true;
    if ( !MdoHomeExternalStat(SourcePath, &Exists, &Info) ) goto io;
    if ( !Exists ) return true;
    if ( Info.Type != XFILE_TYPE_FILE ||
         (Info.Available & XFILE_INFO_SIZE) == 0u ||
         !MdoEventsRead(SourcePath, &Source, &SourceSize) ) goto io;
    while ( Start < SourceSize ) {
        const char* End = (const char*)memchr(Source + Start, '\n',
            SourceSize - Start);
        MdoSessionEventOwned Entry;
        xwork_event Event;
        char* Json = NULL;
        size_t JsonSize = 0u;
        size_t Length;
        size_t Needed;
        if ( End == NULL ) break;
        Length = (size_t)(End - (Source + Start));
        if ( Length == 0u || Length > MDO_SESSION_EVENT_RECORD_LIMIT ||
             !MdoEventsParse(SourceProjectId, SourceSessionId,
                xrtStrViewN(Source + Start, Length), &Entry) ) {
            xrtClearError();
            Start += Length + 1u;
            continue;
        }
        Start += Length + 1u;
        if ( Entry.Info.EventId <= LastSourceEventId ) {
            MdoEventsOwnedUnit(&Entry);
            continue;
        }
        LastSourceEventId = Entry.Info.EventId;
        if ( Entry.Info.Kind == XWORK_EVENT_AGENT_START &&
             Entry.Info.AgentDepth == 0u &&
             Entry.Info.UserMessageSequence > ThroughSequence ) {
            MdoEventsOwnedUnit(&Entry);
            break;
        }
        if ( OutputSize == 0u && Entry.Info.EventId > 1u )
            Bridge->NextEventId = Entry.Info.EventId;
        if ( Bridge->NextEventId == UINT64_MAX ) {
            MdoEventsOwnedUnit(&Entry);
            MdoEventsError(Error, XWORK_ERROR_LIMIT,
                "fork event identity space is exhausted");
            goto done;
        }
        if ( Entry.Info.Kind == XWORK_EVENT_AGENT_START &&
             Entry.Info.AgentDepth == 0u &&
             !MdoSessionAttachmentEventClone(SourceProjectId,
                SourceSessionId, Entry.Info.EventId, Bridge->ProjectId,
                Bridge->SessionId, Bridge->NextEventId,
                Entry.Info.RunId) ) {
            MdoEventsOwnedUnit(&Entry);
            MdoEventsXrtError(Error,
                "cannot copy retained images into the fork session");
            goto done;
        }
        memset(&Event, 0, sizeof(Event));
        Event.eKind = Entry.Info.Kind;
        Event.uAgentTurn = Entry.Info.AgentTurn;
        Event.uUserMessageSequence = Entry.Info.UserMessageSequence;
        Event.uAgentDepth = Entry.Info.AgentDepth;
        Event.uEventId = Entry.Info.SourceEventId;
        Event.iOccurredAtUs = Entry.Info.OccurredAt;
        Event.uAgentId = Entry.Info.AgentId;
        Event.uRunId = Entry.Info.RunId;
        Event.uTaskId = Entry.Info.TaskId;
        Event.uArtifactId = Entry.Info.ArtifactId;
        Event.uParentRunId = Entry.Info.ParentRunId;
        Event.uEffects = Entry.Info.Effects;
        Event.eTaskState = Entry.Info.TaskState;
        Event.uTaskRevision = Entry.Info.TaskRevision;
        Event.tUsage.uInputTokens = Entry.Info.InputTokens;
        Event.tUsage.uOutputTokens = Entry.Info.OutputTokens;
        Event.tUsage.uTotalTokens = Entry.Info.TotalTokens;
        Event.bSuccess = Entry.Info.Success;
        Event.bEffectApplied = Entry.Info.EffectApplied;
        Event.bTextTruncated = Entry.Info.TextTruncated;
        Event.sText = Entry.Info.Text;
        Event.iTextLength = strlen(Entry.Info.Text);
        Event.sToolName = Entry.Info.ToolName;
        Event.sToolCallId = Entry.Info.ToolCallId;
        Event.sArtifactPath = Entry.Info.ArtifactPath;
        Event.sModel = Entry.Info.Model;
        Json = MdoEventsRecord(Bridge, Bridge->NextEventId, &Event,
            Entry.Info.ModelId, Entry.Info.ContextWindowTokens, &JsonSize);
        MdoEventsOwnedUnit(&Entry);
        if ( Json == NULL ) {
            MdoEventsError(Error, XWORK_ERROR_OUT_OF_MEMORY,
                "cannot serialize the fork event journal");
            goto done;
        }
        if ( OutputSize > SIZE_MAX - JsonSize - 1u ) {
            xrtFree(Json);
            MdoEventsError(Error, XWORK_ERROR_LIMIT,
                "fork event journal exceeds its bounded copy limit");
            goto done;
        }
        Needed = OutputSize + JsonSize + 1u;
        if ( Needed > Capacity ) {
            size_t Next = Capacity != 0u ? Capacity : 4096u;
            char* NewOutput;
            while ( Next < Needed ) {
                if ( Next > SIZE_MAX / 2u ) { Next = Needed; break; }
                Next *= 2u;
            }
            NewOutput = (char*)xrtRealloc(Output, Next);
            if ( NewOutput == NULL ) {
                xrtFree(Json);
                MdoEventsError(Error, XWORK_ERROR_OUT_OF_MEMORY,
                    "cannot allocate the fork event journal");
                goto done;
            }
            Output = NewOutput;
            Capacity = Next;
        }
        memcpy(Output + OutputSize, Json, JsonSize);
        OutputSize += JsonSize;
        Output[OutputSize++] = '\n';
        ++Bridge->NextEventId;
        xrtFree(Json);
    }
    if ( OutputSize != 0u &&
         !MdoHomeAtomicWrite(Bridge->Path, Output, OutputSize, false) )
        goto io;
    if ( OutputSize != 0u ) {
        MdoSessionEventTrimPlan Projection;
        memset(&Projection, 0, sizeof(Projection));
        Projection.Bridge = Bridge;
        Projection.Data = Output;
        Projection.Keep = OutputSize;
        Projection.NextEventId = Bridge->NextEventId;
        if ( !MdoSessionEventTrimReconcileTodo(&Projection, Error) )
            goto done;
    }
    Ok = true;
    goto done;
io:
    MdoEventsXrtError(Error, "cannot clone the session event journal");
done:
    xrtFree(Output);
    xrtFree(Source);
    return Ok;
}

static bool MdoEventsMessageMatches(MdoSessionEventBridge* Bridge,
    const char* Data, size_t Size, uint64 SourceEventId, uint64 Sequence)
{
    size_t Start = 0u;
    bool Found = false;
    while ( Start < Size ) {
        const char* End = (const char*)memchr(Data + Start, '\n', Size - Start);
        MdoSessionEventOwned Entry;
        size_t Length;
        if ( End == NULL ) break;
        Length = (size_t)(End - (Data + Start));
        if ( Length != 0u && Length <= MDO_SESSION_EVENT_RECORD_LIMIT &&
             MdoEventsParse(Bridge->ProjectId, Bridge->SessionId,
                xrtStrViewN(Data + Start, Length), &Entry) ) {
            if ( Entry.Info.EventId == SourceEventId ) {
                bool Matches = !Found &&
                    Entry.Info.Kind == XWORK_EVENT_AGENT_START &&
                    Entry.Info.AgentDepth == 0u &&
                    Entry.Info.UserMessageSequence == Sequence;
                MdoEventsOwnedUnit(&Entry);
                if ( !Matches ) return false;
                Found = true;
            } else MdoEventsOwnedUnit(&Entry);
        } else xrtClearError();
        Start += Length + 1u;
    }
    return Found;
}

MdoSessionEventTrimPlan* MdoSessionEventTrimPrepare(
    MdoSessionEventBridge* Bridge, uint64 ThroughSequence, bool Clear,
    uint64 SourceEventId, bool* MessageChanged, xwork_error* Error)
{
    MdoSessionEventTrimPlan* Plan = NULL;
    char* Data = NULL;
    size_t Size = 0u;
    size_t Start = 0u;
    size_t Keep = 0u;
    uint64 LastEventId = 0u;
    bool SawUnsequencedStart = false;
    bool HaveRunBoundary = false;
    bool Exists = false;
    xfileinfo Info;
    xworkErrorInit(Error);
    if ( MessageChanged != NULL ) *MessageChanged = false;
    if ( Bridge == NULL || (SourceEventId != 0u &&
            (Clear || ThroughSequence == UINT64_MAX)) ) {
        MdoEventsError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "an active event bridge is required to trim history");
        return NULL;
    }
    Plan = (MdoSessionEventTrimPlan*)xrtCalloc(1u, sizeof(*Plan));
    if ( Plan == NULL || !MdoSessionEventBridgeRef(Bridge) ) goto memory;
    Plan->Bridge = Bridge;
    Plan->Clear = Clear || ThroughSequence == 0u;
    if ( Plan->Clear ) Plan->RemovedFromEventId = 1u;
    xrtMutexLock(Bridge->Lock);
    Plan->NextEventId = Bridge->NextEventId;
    if ( !MdoHomeExternalStat(Bridge->Path, &Exists, &Info) ||
         (Exists && (Info.Type != XFILE_TYPE_FILE ||
          (Info.Available & XFILE_INFO_SIZE) == 0u ||
          !MdoEventsRead(Bridge->Path, &Data, &Size))) ) goto io;
    /* Validate against the unmodified journal, including a first-message edit
     * whose trim plan is Clear. Manager serialization spans this check and the
     * mutation, so another history edit cannot replace the source in between. */
    if ( SourceEventId != 0u && !MdoEventsMessageMatches(Bridge,
            Data, Size, SourceEventId, ThroughSequence + 1u) ) {
        if ( MessageChanged != NULL ) *MessageChanged = true;
        MdoEventsError(Error, XWORK_ERROR_CONTEXT,
            "the original message changed; reload before editing or retrying");
        goto fail;
    }
    while ( !Plan->Clear && Start < Size ) {
        const char* End = (const char*)memchr(Data + Start, '\n',
            Size - Start);
        MdoSessionEventOwned Entry;
        size_t Length;
        if ( End == NULL ) break;
        Length = (size_t)(End - (Data + Start));
        if ( Length == 0u || Length > MDO_SESSION_EVENT_RECORD_LIMIT ||
             !MdoEventsParse(Bridge->ProjectId, Bridge->SessionId,
                xrtStrViewN(Data + Start, Length), &Entry) ) {
            xrtClearError();
            HaveRunBoundary = false;
            Start += Length + 1u;
            continue;
        }
        if ( Entry.Info.EventId <= LastEventId ) {
            MdoEventsOwnedUnit(&Entry);
            HaveRunBoundary = false;
            Start += Length + 1u;
            continue;
        }
        LastEventId = Entry.Info.EventId;
        if ( Entry.Info.Kind == XWORK_EVENT_AGENT_START &&
             Entry.Info.AgentDepth == 0u ) {
            HaveRunBoundary = true;
            if ( Entry.Info.UserMessageSequence == 0u )
                SawUnsequencedStart = true;
            if ( Entry.Info.UserMessageSequence > ThroughSequence ) {
                Plan->RemovedFromEventId = Entry.Info.EventId;
                MdoEventsOwnedUnit(&Entry);
                break;
            }
        }
        if ( Entry.Info.Kind == MDO_SESSION_EVENT_HISTORY_TRUNCATED )
            HaveRunBoundary = true;
        /* A bounded journal may start halfway through a removed run. Events
         * before the first top-level start have no reliable ledger boundary. */
        if ( !HaveRunBoundary ) {
            MdoEventsOwnedUnit(&Entry);
            Start += Length + 1u;
            continue;
        }
        MdoEventsOwnedUnit(&Entry);
        memmove(Data + Keep, Data + Start, Length + 1u);
        Keep += Length + 1u;
        Start += Length + 1u;
    }
    /* Old journals cannot identify an exact message boundary. Hiding their
     * projection is safer than showing messages removed from the ledger. */
    if ( Plan->Clear || SawUnsequencedStart ) {
        Keep = 0u;
        Plan->RemovedFromEventId = 1u;
    }
    Plan->Data = Data;
    Plan->Keep = Keep;
    Plan->Changed = Plan->Clear || Keep != Size;
    if ( Plan->Changed && Plan->RemovedFromEventId == 0u )
        Plan->RemovedFromEventId = 1u;
    xrtMutexUnlock(Bridge->Lock);
    return Plan;
io:
    MdoEventsXrtError(Error, "cannot read the session event journal");
    goto fail;
memory:
    MdoEventsError(Error, XWORK_ERROR_OUT_OF_MEMORY,
        "cannot allocate the event trim plan");
    MdoSessionEventTrimPlanRelease(Plan);
    return NULL;
fail:
    xrtMutexUnlock(Bridge->Lock);
    xrtFree(Data);
    MdoSessionEventTrimPlanRelease(Plan);
    return NULL;
}

bool MdoSessionEventTrimApply(MdoSessionEventTrimPlan* Plan,
    xwork_error* Error)
{
    MdoSessionEventBridge* Bridge;
    xwork_event Marker;
    char* Json = NULL;
    char* Output = NULL;
    size_t JsonSize = 0u;
    size_t Keep;
    size_t Total;
    bool Ok = false;
    xworkErrorInit(Error);
    if ( Plan == NULL || Plan->Bridge == NULL ) {
        MdoEventsError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "an event trim plan is required");
        return false;
    }
    Bridge = Plan->Bridge;
    if ( !Plan->Changed ) return true;
    xrtMutexLock(Bridge->Lock);
    if ( Bridge->NextEventId != Plan->NextEventId ||
         Bridge->NextEventId == UINT64_MAX ) {
        MdoEventsError(Error, XWORK_ERROR_CONTEXT,
            "the event journal changed during history maintenance");
        goto done;
    }
    memset(&Marker, 0, sizeof(Marker));
    Marker.eKind = MDO_SESSION_EVENT_HISTORY_TRUNCATED;
    /* Synthetic markers use source_event_id as the first discarded UI ID.
     * This lets sidecar repair recognize a prior partial mutation on retry. */
    Marker.uEventId = Plan->RemovedFromEventId;
    Marker.iOccurredAtUs = xrtNow();
    Marker.bSuccess = true;
    Marker.sText = Plan->Clear ? "会话历史已清空" : "会话历史已截断";
    Marker.iTextLength = strlen(Marker.sText);
    Json = MdoEventsRecord(Bridge, Bridge->NextEventId,
        &Marker, "", 0u, &JsonSize);
    if ( Json == NULL || Plan->Keep > SIZE_MAX - JsonSize - 1u ) {
        MdoEventsError(Error, XWORK_ERROR_OUT_OF_MEMORY,
            "cannot serialize the history boundary");
        goto done;
    }
    Keep = Plan->Keep;
    Total = Keep + JsonSize + 1u;
    Output = (char*)xrtMalloc(Total);
    if ( Output == NULL ) {
        MdoEventsError(Error, XWORK_ERROR_OUT_OF_MEMORY,
            "cannot allocate the trimmed event journal");
        goto done;
    }
    if ( Keep != 0u ) memcpy(Output, Plan->Data, Keep);
    memcpy(Output + Keep, Json, JsonSize);
    Output[Keep + JsonSize] = '\n';
    if ( !MdoHomeAtomicWrite(Bridge->Path, Output, Total, false) ) {
        MdoEventsXrtError(Error, "cannot publish the trimmed event journal");
        goto done;
    }
    Plan->Keep = Keep;
    ++Bridge->NextEventId;
    Ok = true;
done:
    xrtFree(Output);
    xrtFree(Json);
    xrtMutexUnlock(Bridge->Lock);
    return Ok;
}

/* The todo sidecar is a projection of a successful main-Agent tool event.
 * Rebuild it from the retained journal when its source event was removed.
 * A bounded journal may have evicted an older valid source; preserve that
 * sidecar unless a history marker explicitly invalidates its event ID. */
bool MdoSessionEventTrimReconcileTodo(MdoSessionEventTrimPlan* Plan,
    xwork_error* Error)
{
    xvalue* Stored = NULL;
    uint64 StoredId = 0u;
    uint64 FirstId = 0u;
    uint64 LatestTodoId = 0u;
    char* LatestTodoText = NULL;
    size_t LatestTodoSize = 0u;
    size_t Start = 0u;
    bool StoredFound = false;
    bool Stale;
    bool Ok = false;
    xworkErrorInit(Error);
    if ( Plan == NULL || Plan->Bridge == NULL ) {
        MdoEventsError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "an event trim plan is required to reconcile the plan");
        return false;
    }
    if ( !MdoSessionTodoLoad(Plan->Bridge->ProjectId,
            Plan->Bridge->SessionId, &Stored) ||
         !MdoEventsValueUInt(Stored, "event_id", &StoredId) ) {
        MdoEventsError(Error, XWORK_ERROR_IO,
            "cannot read the session plan projection");
        goto done;
    }
    Stale = Plan->Clear ||
        (Plan->RemovedFromEventId != 0u &&
         StoredId >= Plan->RemovedFromEventId &&
         StoredId < Plan->NextEventId);
    while ( Start < Plan->Keep ) {
        const char* End = (const char*)memchr(Plan->Data + Start, '\n',
            Plan->Keep - Start);
        MdoSessionEventOwned Entry;
        size_t Length;
        if ( End == NULL ) goto malformed;
        Length = (size_t)(End - (Plan->Data + Start));
        if ( Length == 0u || Length > MDO_SESSION_EVENT_RECORD_LIMIT ||
             !MdoEventsParse(Plan->Bridge->ProjectId,
                Plan->Bridge->SessionId,
                xrtStrViewN(Plan->Data + Start, Length), &Entry) )
            goto malformed;
        if ( FirstId == 0u ) FirstId = Entry.Info.EventId;
        if ( StoredId != 0u && Entry.Info.EventId == StoredId )
            StoredFound = true;
        if ( StoredId != 0u &&
             Entry.Info.Kind == MDO_SESSION_EVENT_HISTORY_TRUNCATED &&
             StoredId < Entry.Info.EventId &&
             StoredId >= (Entry.Info.SourceEventId != 0u ?
                Entry.Info.SourceEventId : 1u) ) Stale = true;
        if ( Entry.Info.Kind == XWORK_EVENT_TOOL_DONE &&
             Entry.Info.Success && Entry.Info.AgentDepth == 0u &&
             strcmp(Entry.ToolName, "mdo.todo") == 0 &&
             !Entry.Info.TextTruncated ) {
            size_t TextSize = strlen(Entry.Text);
            char* Copy = (char*)xrtMalloc(TextSize + 1u);
            if ( Copy == NULL ) {
                MdoEventsOwnedUnit(&Entry);
                MdoEventsError(Error, XWORK_ERROR_OUT_OF_MEMORY,
                    "cannot retain the previous session plan");
                goto done;
            }
            memcpy(Copy, Entry.Text, TextSize + 1u);
            xrtFree(LatestTodoText);
            LatestTodoText = Copy;
            LatestTodoSize = TextSize;
            LatestTodoId = Entry.Info.EventId;
        }
        MdoEventsOwnedUnit(&Entry);
        Start += Length + 1u;
    }
    if ( StoredId != 0u && !StoredFound &&
         (FirstId == 0u || StoredId >= FirstId) ) Stale = true;
    if ( Stale || LatestTodoId > StoredId ) {
        if ( LatestTodoId != 0u ) {
            xwork_event Event;
            memset(&Event, 0, sizeof(Event));
            Event.eKind = XWORK_EVENT_TOOL_DONE;
            Event.bSuccess = true;
            Event.sToolName = "mdo.todo";
            Event.sText = LatestTodoText;
            Event.iTextLength = LatestTodoSize;
            Ok = MdoSessionTodoProject(Plan->Bridge->ProjectId,
                Plan->Bridge->SessionId, LatestTodoId, &Event);
        } else Ok = StoredId == 0u ||
            MdoSessionTodoReset(Plan->Bridge->ProjectId,
                Plan->Bridge->SessionId);
        if ( !Ok ) MdoEventsError(Error, XWORK_ERROR_IO,
            "cannot publish the reconciled session plan");
    } else Ok = true;
    goto done;
malformed:
    MdoEventsError(Error, XWORK_ERROR_IO,
        "the retained event journal is malformed");
done:
    xrtFree(LatestTodoText);
    xrtValueRelease(Stored);
    return Ok;
}

void MdoSessionEventTrimPlanRelease(MdoSessionEventTrimPlan* Plan)
{
    if ( Plan == NULL ) return;
    xrtFree(Plan->Data);
    MdoSessionEventBridgeRelease(Plan->Bridge);
    xrtFree(Plan);
}

bool MdoSessionEventBridgeRef(void* Value)
{
    MdoSessionEventBridge* Bridge = (MdoSessionEventBridge*)Value;
    uint32 Refs;
    if ( Bridge == NULL ) return false;
    Refs = xrtAtomic32Load(&Bridge->Refs, XMEMORY_ACQUIRE);
    for ( ; ; ) {
        uint32 Expected = Refs;
        if ( Refs == 0u || Refs == UINT32_MAX ) return false;
        if ( xrtAtomic32CompareExchange(&Bridge->Refs, &Expected, Refs + 1u,
                XMEMORY_ACQ_REL, XMEMORY_ACQUIRE) ) return true;
        Refs = Expected;
    }
}

void MdoSessionEventBridgeRelease(void* Value)
{
    MdoSessionEventBridge* Bridge = (MdoSessionEventBridge*)Value;
    uint32 Previous;
    if ( Bridge == NULL ) return;
    Previous = xrtAtomic32FetchSub(&Bridge->Refs, 1u, XMEMORY_ACQ_REL);
    if ( Previous > 1u ) return;
    if ( Previous == 0u ) abort();
    if ( Bridge->Registered )
        MdoSessionsInternalActiveRelease(Bridge->ProjectId, Bridge->SessionId);
    if ( Bridge->RuntimeLock != NULL ) {
        (void)xrtFileUnlock(Bridge->RuntimeLock);
        (void)xrtClose(Bridge->RuntimeLock);
    }
    if ( Bridge->UserOwnerRetained && Bridge->UserOwnerRelease != NULL )
        Bridge->UserOwnerRelease(Bridge->UserOwnerData);
    if ( Bridge->Lock != NULL ) xrtMutexDestroy(Bridge->Lock);
    MdoProjectLeaseRelease(Bridge->ProjectLease);
    memset(Bridge, 0, sizeof(*Bridge));
    xrtFree(Bridge);
}

void MdoSessionEventBridgeSetRegistered(MdoSessionEventBridge* Bridge)
{
    if ( Bridge != NULL ) Bridge->Registered = true;
}

bool MdoSessionEventBridgeSetProfile(MdoSessionEventBridge* Bridge,
    const char* ModelId, uint64 ContextWindowTokens)
{
    size_t Size;
    if ( Bridge == NULL || ModelId == NULL || ContextWindowTokens == 0u )
        return false;
    Size = strlen(ModelId);
    if ( Size == 0u || Size >= sizeof(Bridge->ModelId) ||
         !xrtUtf8Valid(xrtStrViewN(ModelId, Size), NULL) ) return false;
    xrtMutexLock(Bridge->Lock);
    memcpy(Bridge->ModelId, ModelId, Size + 1u);
    Bridge->ContextWindowTokens = ContextWindowTokens;
    xrtMutexUnlock(Bridge->Lock);
    return true;
}

bool MdoSessionEventBridgePendingSet(MdoSessionEventBridge* Bridge,
    uint64 RunId, const char Ids[4][33], size_t Count,
    bool EmptyPrompt, const char* QueueItemId)
{
    bool Ok;
    size_t Index;
    if ( Bridge == NULL || RunId == 0u || Count > 4u ||
         (EmptyPrompt && Count == 0u) ||
         (Count != 0u && Ids == NULL) ) return false;
    if ( QueueItemId != NULL ) {
        if ( strlen(QueueItemId) != 32u ) return false;
        for ( Index = 0u; Index < 32u; ++Index ) {
            unsigned char Byte = (unsigned char)QueueItemId[Index];
            if ( !((Byte >= '0' && Byte <= '9') ||
                   (Byte >= 'a' && Byte <= 'f')) ) return false;
        }
    }
    xrtMutexLock(Bridge->Lock);
    Ok = true;
    Bridge->PendingRunId = RunId;
    Bridge->PendingCount = Count;
    Bridge->PendingEmptyPrompt = EmptyPrompt;
    memset(Bridge->PendingIds, 0, sizeof(Bridge->PendingIds));
    memset(Bridge->PendingQueueItemId, 0,
        sizeof(Bridge->PendingQueueItemId));
    if ( QueueItemId != NULL )
        memcpy(Bridge->PendingQueueItemId, QueueItemId, 32u);
    if ( Count != 0u )
        memcpy(Bridge->PendingIds, Ids, Count * sizeof(Ids[0]));
    xrtMutexUnlock(Bridge->Lock);
    return Ok;
}

void MdoSessionEventBridgePendingClear(MdoSessionEventBridge* Bridge,
    uint64 RunId)
{
    if ( Bridge == NULL || RunId == 0u ) return;
    xrtMutexLock(Bridge->Lock);
    if ( Bridge->PendingRunId == RunId ) {
        Bridge->PendingRunId = 0u;
        Bridge->PendingCount = 0u;
        Bridge->PendingEmptyPrompt = false;
        memset(Bridge->PendingIds, 0, sizeof(Bridge->PendingIds));
        memset(Bridge->PendingQueueItemId, 0,
            sizeof(Bridge->PendingQueueItemId));
    }
    xrtMutexUnlock(Bridge->Lock);
}

bool MdoSessionEventBridgeOnEvent(void* Value, const xwork_event* Event)
{
    MdoSessionEventBridge* Bridge = (MdoSessionEventBridge*)Value;
    MdoSessionDataLease* DataLease;
    xwork_event UserEvent;
    bool Ok;
    if ( Bridge == NULL || Event == NULL ) return false;
    DataLease = MdoSessionDataAcquire(Bridge->ProjectId, Bridge->SessionId,
        MDO_SESSION_DATA_WRITE, NULL);
    if ( DataLease == NULL ) return false;
    UserEvent = *Event;
    xrtMutexLock(Bridge->Lock);
    Ok = true;
    if ( Event->eKind == XWORK_EVENT_AGENT_START &&
         Event->uAgentDepth == 0u ) {
        const size_t Count = Bridge->PendingRunId == Event->uRunId ?
            Bridge->PendingCount : 0u;
        /* xwork's diagnostic start text may contain an image placeholder.
         * Persist the actual empty submission for display, edit, retry and
         * export. Run-bound intent distinguishes it from literal user text;
         * the original runtime event and model message remain untouched. */
        if ( Count != 0u && Bridge->PendingEmptyPrompt ) {
            UserEvent.sText = "";
            UserEvent.iTextLength = 0u;
            UserEvent.bTextTruncated = false;
        }
        Ok = MdoSessionAttachmentEventWrite(Bridge->ProjectId,
            Bridge->SessionId, Bridge->NextEventId, Event->uRunId,
            Bridge->PendingIds, Count);
    }
    if ( Ok ) Ok = MdoEventsAppend(Bridge, &UserEvent);
    if ( Ok && Event->eKind == XWORK_EVENT_AGENT_START &&
         Event->uAgentDepth == 0u &&
         Bridge->PendingRunId == Event->uRunId ) {
        Bridge->PendingRunId = 0u;
        Bridge->PendingCount = 0u;
        Bridge->PendingEmptyPrompt = false;
        memset(Bridge->PendingIds, 0, sizeof(Bridge->PendingIds));
        memset(Bridge->PendingQueueItemId, 0,
            sizeof(Bridge->PendingQueueItemId));
    }
    /* A UI projection failure must not cancel a successful Agent tool call.
     * The journal remains authoritative for the completed tool event. */
    if ( Ok && !MdoSessionTodoProject(Bridge->ProjectId,
            Bridge->SessionId, Bridge->NextEventId - 1u, Event) )
        xrtClearError();
    xrtMutexUnlock(Bridge->Lock);
    MdoSessionDataRelease(DataLease);
    if ( !Ok ) return false;
    return Bridge->UserEvent == NULL ||
        Bridge->UserEvent(Bridge->UserEventData, Event);
}

static bool MdoEventsSnapshotGrow(MdoSessionEventSnapshot* Snapshot)
{
    size_t Capacity;
    void* Events;
    if ( Snapshot->Count < Snapshot->Capacity ) return true;
    Capacity = Snapshot->Capacity != 0u ? Snapshot->Capacity * 2u : 16u;
    Events = xrtRealloc(Snapshot->Events,
        Capacity * sizeof(*Snapshot->Events));
    if ( Events == NULL ) return false;
    Snapshot->Events = (MdoSessionEventOwned*)Events;
    Snapshot->Capacity = Capacity;
    return true;
}

MdoSessionEventSnapshot* MdoSessionEventReplay(const char* ProjectId,
    const char* SessionId, uint64 AfterEventId, size_t Limit,
    xwork_error* Error)
{
    MdoSessionEventSnapshot* Snapshot = NULL;
    MdoEventReader* Reader = NULL;
    char Path[MDO_SESSION_PATH_CAPACITY];
    xstrview Record;
    uint64 Previous = AfterEventId;
    xworkErrorInit(Error);
    if ( !MdoEventsIdValid(ProjectId, MDO_PROJECT_ID_CAPACITY) ||
         !MdoEventsIdValid(SessionId, MDO_SESSION_ID_CAPACITY) ||
         Limit > MDO_SESSION_EVENT_REPLAY_MAX ||
         !MdoEventsPath(Path, ProjectId, SessionId) ) {
        MdoEventsError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "invalid session event replay request");
        return NULL;
    }
    if ( Limit == 0u ) Limit = MDO_SESSION_EVENT_REPLAY_DEFAULT;
    Snapshot = (MdoSessionEventSnapshot*)xrtCalloc(1u, sizeof(*Snapshot));
    if ( Snapshot == NULL ) goto memory;
    xrtAtomic32Init(&Snapshot->Refs, 1u);
    Snapshot->NextCursor = AfterEventId;
    Reader = MdoEventReaderOpen(Path);
    if ( Reader == NULL ) goto io;
    Snapshot->LatestId = MdoEventReaderLatest(Reader, ProjectId, SessionId,
        &Snapshot->HistoryLost);
    /* Retain compatibility with journals already evicted by older releases. */
    Reader->Position = 0u;
    if ( MdoEventReaderNext(Reader, &Record) ) {
        MdoSessionEventOwned First;
        if ( MdoEventsParse(ProjectId, SessionId, Record, &First) ) {
            if ( First.Info.EventId > 1u && AfterEventId < First.Info.EventId - 1u )
                Snapshot->HistoryLost = true;
            MdoEventsOwnedUnit(&First);
        } else { Snapshot->HistoryLost = true; xrtClearError(); }
    }
    MdoEventReaderAfter(Reader, ProjectId, SessionId, AfterEventId);
    while ( Snapshot->Count < Limit && MdoEventReaderNext(Reader, &Record) ) {
        MdoSessionEventOwned Event;
        if ( !MdoEventsParse(ProjectId, SessionId, Record, &Event) ) {
            Snapshot->HistoryLost = true;
            xrtClearError();
            continue;
        }
        if ( Event.Info.EventId > Previous ) {
            if ( !MdoEventsSnapshotGrow(Snapshot) ) {
                MdoEventsOwnedUnit(&Event);
                goto memory;
            }
            Previous = Event.Info.EventId;
            Snapshot->Events[Snapshot->Count++] = Event;
            Snapshot->NextCursor = Previous;
        } else MdoEventsOwnedUnit(&Event);
    }
    if ( Reader->Failed ) goto io;
    MdoEventReaderClose(Reader);
    return Snapshot;
memory:
    MdoEventReaderClose(Reader);
    MdoSessionEventSnapshotRelease(Snapshot);
    MdoEventsError(Error, XWORK_ERROR_OUT_OF_MEMORY,
        "cannot allocate the session event replay");
    return NULL;
io:
    MdoEventReaderClose(Reader);
    MdoSessionEventSnapshotRelease(Snapshot);
    MdoEventsXrtError(Error, "cannot read the session event journal");
    return NULL;
}

bool MdoSessionEventBridgeCaptureTryLock(MdoSessionEventBridge* Bridge)
{
    return Bridge != NULL && xrtMutexTryLock(Bridge->Lock);
}

void MdoSessionEventBridgeCaptureUnlock(MdoSessionEventBridge* Bridge)
{
    if ( Bridge != NULL ) xrtMutexUnlock(Bridge->Lock);
}

bool MdoSessionEventQueueStartSeen(const char* ProjectId,
    const char* SessionId, const char* QueueItemId, uint64 AgentRunId,
    bool* Seen)
{
    char Path[MDO_SESSION_PATH_CAPACITY];
    xfileinfo Info;
    char* Data = NULL;
    size_t Size = 0u;
    size_t Start = 0u;
    bool Exists = false;
    size_t i;
    if ( Seen == NULL || QueueItemId == NULL || AgentRunId == 0u ||
         !MdoEventsIdValid(ProjectId, MDO_PROJECT_ID_CAPACITY) ||
         !MdoEventsIdValid(SessionId, MDO_SESSION_ID_CAPACITY) ||
         strlen(QueueItemId) != 32u ||
         !MdoEventsPath(Path, ProjectId, SessionId) ) return false;
    *Seen = false;
    for ( i = 0u; i < 32u; ++i ) {
        unsigned char Byte = (unsigned char)QueueItemId[i];
        if ( !((Byte >= '0' && Byte <= '9') ||
               (Byte >= 'a' && Byte <= 'f')) ) return false;
    }
    if ( !MdoHomeExternalStat(Path, &Exists, &Info) ) return false;
    if ( !Exists ) return true;
    if ( Info.Type != XFILE_TYPE_FILE ||
         (Info.Available & XFILE_INFO_SIZE) == 0u ||
         !MdoEventsRead(Path, &Data, &Size) ) return false;
    while ( Start < Size ) {
        const char* End = (const char*)memchr(Data + Start, '\n',
            Size - Start);
        size_t Length;
        bool Possible = false;
        if ( End == NULL ) break; /* An unterminated tail proves nothing. */
        Length = (size_t)(End - (Data + Start));
        if ( Length <= MDO_SESSION_EVENT_RECORD_LIMIT ) {
            for ( i = Start; i + 32u <= Start + Length; ++i ) {
                if ( Data[i] == QueueItemId[0] &&
                     memcmp(Data + i, QueueItemId, 32u) == 0 ) {
                    Possible = true;
                    break;
                }
            }
        }
        if ( Possible ) {
            MdoSessionEventOwned Event;
            if ( MdoEventsParse(ProjectId, SessionId,
                    xrtStrViewN(Data + Start, Length), &Event) ) {
                *Seen = Event.Info.Kind == XWORK_EVENT_AGENT_START &&
                    Event.Info.AgentDepth == 0u &&
                    Event.Info.RunId == AgentRunId &&
                    strcmp(Event.Info.QueueItemId, QueueItemId) == 0;
                MdoEventsOwnedUnit(&Event);
                if ( *Seen ) break;
            } else xrtClearError();
        }
        Start += Length + 1u;
    }
    xrtFree(Data);
    return true;
}

MdoSessionEventSnapshot* MdoSessionEventSnapshotRef(
    MdoSessionEventSnapshot* Snapshot)
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

void MdoSessionEventSnapshotRelease(MdoSessionEventSnapshot* Snapshot)
{
    uint32 Previous;
    size_t i;
    if ( Snapshot == NULL ) return;
    Previous = xrtAtomic32FetchSub(&Snapshot->Refs, 1u, XMEMORY_ACQ_REL);
    if ( Previous > 1u ) return;
    if ( Previous == 0u ) abort();
    for ( i = 0u; i < Snapshot->Count; ++i )
        MdoEventsOwnedUnit(&Snapshot->Events[i]);
    xrtFree(Snapshot->Events);
    xrtFree(Snapshot);
}

size_t MdoSessionEventSnapshotCount(const MdoSessionEventSnapshot* Snapshot)
{
    return Snapshot != NULL ? Snapshot->Count : 0u;
}

bool MdoSessionEventSnapshotAt(const MdoSessionEventSnapshot* Snapshot,
    size_t Index, MdoSessionEventInfo* Info)
{
    uint32 Size;
    if ( Snapshot == NULL || Index >= Snapshot->Count || Info == NULL ||
         Info->Size < offsetof(MdoSessionEventInfo, ModelId) ) return false;
    Size = Info->Size;
    memcpy(Info, &Snapshot->Events[Index].Info,
        Size < sizeof(*Info) ? Size : sizeof(*Info));
    Info->Size = Size;
    return true;
}

uint64 MdoSessionEventSnapshotNextCursor(
    const MdoSessionEventSnapshot* Snapshot)
{
    return Snapshot != NULL ? Snapshot->NextCursor : 0u;
}

uint64 MdoSessionEventSnapshotLatestId(
    const MdoSessionEventSnapshot* Snapshot)
{
    return Snapshot != NULL ? Snapshot->LatestId : 0u;
}

bool MdoSessionEventSnapshotHistoryLost(
    const MdoSessionEventSnapshot* Snapshot)
{
    return Snapshot != NULL && Snapshot->HistoryLost;
}
