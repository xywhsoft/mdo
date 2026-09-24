#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"
#include "../../include/mdo/home.h"

#define MDO_SESSION_EVENT_SCHEMA 3u
#define MDO_SESSION_EVENT_FILE_LIMIT (16u * 1024u * 1024u)
#define MDO_SESSION_EVENT_RETAIN_BYTES (8u * 1024u * 1024u)
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
    char ProjectId[MDO_PROJECT_ID_CAPACITY];
    char SessionId[MDO_SESSION_ID_CAPACITY];
    char Path[MDO_SESSION_PATH_CAPACITY];
    uint64 NextEventId;
    xwork_event_fn UserEvent;
    void* UserEventData;
    void* UserOwnerData;
    xwork_agent_owner_release_fn UserOwnerRelease;
    bool UserOwnerRetained;
    bool Registered;
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
         Info.Size > MDO_SESSION_EVENT_FILE_LIMIT ||
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
    uint64 EventId, const xwork_event* Event, size_t* Size)
{
    xvalue* Object = xrtValueObject();
    xstrview Text;
    xstrview ToolName;
    xstrview ToolCallId;
    xstrview ArtifactPath;
    xstrview Model;
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
         !MdoEventsObjectString(Object, "model", Model.Data, Model.Size) )
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

static bool MdoEventsCompactAppend(const char* Path, const char* Json,
    size_t JsonSize)
{
    char* Existing = NULL;
    char* Combined = NULL;
    size_t ExistingSize = 0u;
    size_t Start;
    size_t Keep;
    bool Ok = false;
    if ( !MdoEventsRead(Path, &Existing, &ExistingSize) ) return false;
    Start = ExistingSize > MDO_SESSION_EVENT_RETAIN_BYTES ?
        ExistingSize - MDO_SESSION_EVENT_RETAIN_BYTES : 0u;
    while ( Start < ExistingSize && Existing[Start] != '\n' ) ++Start;
    if ( Start < ExistingSize ) ++Start;
    Keep = ExistingSize - Start;
    if ( Keep > SIZE_MAX - JsonSize - 1u ) goto done;
    Combined = (char*)xrtMalloc(Keep + JsonSize + 1u);
    if ( Combined == NULL ) goto done;
    memcpy(Combined, Existing + Start, Keep);
    memcpy(Combined + Keep, Json, JsonSize);
    Combined[Keep + JsonSize] = '\n';
    Ok = MdoHomeAtomicWrite(Path, Combined, Keep + JsonSize + 1u, false);
done:
    xrtFree(Combined);
    xrtFree(Existing);
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
    if ( Bridge->NextEventId == 0u || Bridge->NextEventId == UINT64_MAX )
        return false;
    Json = MdoEventsRecord(Bridge, Bridge->NextEventId, Event, &Size);
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
    if ( Exists && (Info.Size > MDO_SESSION_EVENT_FILE_LIMIT - Size - 1u) )
        Ok = MdoEventsCompactAppend(Bridge->Path, Json, Size);
    else Ok = MdoEventsAppendBytes(Bridge->Path, Json, Size);
    xrtFree(Json);
    if ( Ok ) ++Bridge->NextEventId;
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
          xrtValueCount(Root) != 29u) ||
         !MdoEventsValueUInt(Root, "schema_version", &Schema) ||
         !((Schema == 1u && xrtValueCount(Root) == 25u) ||
           (Schema == 2u && xrtValueCount(Root) == 28u) ||
           (Schema == MDO_SESSION_EVENT_SCHEMA &&
            xrtValueCount(Root) == 29u)) ||
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
         Kind > XWORK_EVENT_RECOVERY_RESOLVED ||
         !MdoEventsValueUInt(Root, "agent_turn", &Result->Info.AgentTurn) ||
         (Schema >= 3u && !MdoEventsValueUInt(Root,
            "user_message_sequence", &Result->Info.UserMessageSequence)) ||
         !MdoEventsValueUInt(Root, "agent_depth", &AgentDepth) ||
         AgentDepth > UINT32_MAX ||
         !MdoEventsValueUInt(Root, "agent_id", &Result->Info.AgentId) ||
         !MdoEventsValueUInt(Root, "run_id", &Result->Info.RunId) ||
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
         !MdoEventsValueString(Root, "model", &Model) ) goto done;
    Result->Text = MdoEventsCopy(Text, MDO_SESSION_EVENT_TEXT_LIMIT);
    Result->ToolName = MdoEventsCopy(ToolName, MDO_SESSION_EVENT_METADATA_LIMIT);
    Result->ToolCallId = MdoEventsCopy(ToolCallId,
        MDO_SESSION_EVENT_METADATA_LIMIT);
    Result->ArtifactPath = MdoEventsCopy(ArtifactPath,
        MDO_SESSION_EVENT_METADATA_LIMIT);
    Result->Model = MdoEventsCopy(Model, MDO_SESSION_EVENT_METADATA_LIMIT);
    if ( Result->Text == NULL || Result->ToolName == NULL ||
         Result->ToolCallId == NULL || Result->ArtifactPath == NULL ||
         Result->Model == NULL ) goto done;
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
    Ok = true;
done:
    xrtValueRelease(Root);
    if ( !Ok ) MdoEventsOwnedUnit(Result);
    return Ok;
}

static bool MdoEventsScanLatest(const char* ProjectId, const char* SessionId,
    const char* Data, size_t Size, uint64* Latest, bool* Incomplete)
{
    size_t Start = 0u;
    uint64 Last = 0u;
    *Incomplete = false;
    while ( Start < Size ) {
        const char* End = (const char*)memchr(Data + Start, '\n', Size - Start);
        MdoSessionEventOwned Event;
        size_t Length;
        if ( End == NULL ) {
            *Incomplete = true;
            break;
        }
        Length = (size_t)(End - (Data + Start));
        if ( Length == 0u || Length > MDO_SESSION_EVENT_RECORD_LIMIT ||
             !MdoEventsParse(ProjectId, SessionId,
                xrtStrViewN(Data + Start, Length), &Event) ) {
            xrtClearError();
            Start += Length + 1u;
            continue;
        }
        if ( Event.Info.EventId <= Last ) {
            MdoEventsOwnedUnit(&Event);
            Start += Length + 1u;
            continue;
        }
        Last = Event.Info.EventId;
        MdoEventsOwnedUnit(&Event);
        Start += Length + 1u;
    }
    *Latest = Last;
    return true;
}

MdoSessionEventBridge* MdoSessionEventBridgeCreate(
    const char* ProjectId, const char* SessionId,
    xwork_event_fn UserEvent, void* UserEventData,
    void* UserOwnerData, xwork_agent_owner_retain_fn UserOwnerRetain,
    xwork_agent_owner_release_fn UserOwnerRelease, xwork_error* Error)
{
    MdoSessionEventBridge* Bridge;
    bool Exists = false;
    xfileinfo Info;
    char* Data = NULL;
    size_t Size = 0u;
    uint64 Latest = 0u;
    bool Incomplete = false;
    char RuntimeLockPath[MDO_SESSION_PATH_CAPACITY];
    xworkErrorInit(Error);
    if ( !MdoEventsIdValid(ProjectId, MDO_PROJECT_ID_CAPACITY) ||
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
             !MdoEventsRead(Bridge->Path, &Data, &Size) ||
             !MdoEventsScanLatest(ProjectId, SessionId, Data, Size,
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
    xrtFree(Data);
    return Bridge;
memory:
    MdoEventsError(Error, XWORK_ERROR_OUT_OF_MEMORY,
        "cannot allocate the session event bridge");
    goto fail;
io:
    MdoEventsXrtError(Error, "cannot inspect the session event journal");
fail:
    xrtFree(Data);
    MdoSessionEventBridgeRelease(Bridge);
    return NULL;
}

/* Forks are prepared before the child Agent is published. Rebuild the UI
 * journal under the child's identity, keeping only completed source runs
 * before the requested user-message boundary. The source journal is bounded
 * and the result is published in one atomic write. */
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
        Json = MdoEventsRecord(Bridge, Bridge->NextEventId, &Event, &JsonSize);
        MdoEventsOwnedUnit(&Entry);
        if ( Json == NULL ) {
            MdoEventsError(Error, XWORK_ERROR_OUT_OF_MEMORY,
                "cannot serialize the fork event journal");
            goto done;
        }
        if ( OutputSize >= MDO_SESSION_EVENT_FILE_LIMIT * 2u ||
             JsonSize > MDO_SESSION_EVENT_FILE_LIMIT * 2u -
                OutputSize - 1u ) {
            xrtFree(Json);
            MdoEventsError(Error, XWORK_ERROR_LIMIT,
                "fork event journal exceeds its bounded copy limit");
            goto done;
        }
        Needed = OutputSize + JsonSize + 1u;
        if ( Needed > Capacity ) {
            size_t Next = Capacity != 0u ? Capacity : 4096u;
            char* NewOutput;
            while ( Next < Needed ) Next *= 2u;
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
    if ( OutputSize > MDO_SESSION_EVENT_FILE_LIMIT ) {
        size_t Offset = OutputSize - MDO_SESSION_EVENT_RETAIN_BYTES;
        while ( Offset < OutputSize && Output[Offset] != '\n' ) ++Offset;
        if ( Offset < OutputSize ) ++Offset;
        memmove(Output, Output + Offset, OutputSize - Offset);
        OutputSize -= Offset;
    }
    if ( OutputSize != 0u &&
         !MdoHomeAtomicWrite(Bridge->Path, Output, OutputSize, false) )
        goto io;
    Ok = true;
    goto done;
io:
    MdoEventsXrtError(Error, "cannot clone the session event journal");
done:
    xrtFree(Output);
    xrtFree(Source);
    return Ok;
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
    memset(Bridge, 0, sizeof(*Bridge));
    xrtFree(Bridge);
}

void MdoSessionEventBridgeSetRegistered(MdoSessionEventBridge* Bridge)
{
    if ( Bridge != NULL ) Bridge->Registered = true;
}

bool MdoSessionEventBridgeOnEvent(void* Value, const xwork_event* Event)
{
    MdoSessionEventBridge* Bridge = (MdoSessionEventBridge*)Value;
    bool Ok;
    if ( Bridge == NULL || Event == NULL ) return false;
    xrtMutexLock(Bridge->Lock);
    Ok = MdoEventsAppend(Bridge, Event);
    /* A UI projection failure must not cancel a successful Agent tool call.
     * The journal remains authoritative for the completed tool event. */
    if ( Ok && !MdoSessionTodoProject(Bridge->ProjectId,
            Bridge->SessionId, Bridge->NextEventId - 1u, Event) )
        xrtClearError();
    xrtMutexUnlock(Bridge->Lock);
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
    char Path[MDO_SESSION_PATH_CAPACITY];
    char* Data = NULL;
    size_t Size = 0u;
    size_t Start = 0u;
    uint64 First = 0u;
    uint64 Previous = 0u;
    bool Exists = false;
    xfileinfo Info;
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
    if ( !MdoHomeExternalStat(Path, &Exists, &Info) ) goto io;
    if ( !Exists ) return Snapshot;
    if ( Info.Type != XFILE_TYPE_FILE ||
         (Info.Available & XFILE_INFO_SIZE) == 0u ||
         !MdoEventsRead(Path, &Data, &Size) ) goto io;
    while ( Start < Size ) {
        const char* End = (const char*)memchr(Data + Start, '\n', Size - Start);
        MdoSessionEventOwned Event;
        size_t Length;
        if ( End == NULL ) {
            Snapshot->HistoryLost = true;
            break;
        }
        Length = (size_t)(End - (Data + Start));
        if ( Length == 0u || Length > MDO_SESSION_EVENT_RECORD_LIMIT ||
             !MdoEventsParse(ProjectId, SessionId,
                xrtStrViewN(Data + Start, Length), &Event) ) {
            Snapshot->HistoryLost = true;
            xrtClearError();
            Start += Length + 1u;
            continue;
        }
        if ( Event.Info.EventId <= Previous ) {
            Snapshot->HistoryLost = true;
            MdoEventsOwnedUnit(&Event);
            Start += Length + 1u;
            continue;
        }
        if ( First == 0u ) First = Event.Info.EventId;
        Previous = Event.Info.EventId;
        Snapshot->LatestId = Previous;
        if ( Event.Info.EventId > AfterEventId && Snapshot->Count < Limit ) {
            if ( !MdoEventsSnapshotGrow(Snapshot) ) {
                MdoEventsOwnedUnit(&Event);
                goto memory;
            }
            Snapshot->Events[Snapshot->Count++] = Event;
            Snapshot->NextCursor = Event.Info.EventId;
        } else MdoEventsOwnedUnit(&Event);
        Start += Length + 1u;
    }
    if ( First > 1u && AfterEventId < First - 1u )
        Snapshot->HistoryLost = true;
    xrtFree(Data);
    return Snapshot;
memory:
    xrtFree(Data);
    MdoSessionEventSnapshotRelease(Snapshot);
    MdoEventsError(Error, XWORK_ERROR_OUT_OF_MEMORY,
        "cannot allocate the session event replay");
    return NULL;
io:
    xrtFree(Data);
    MdoSessionEventSnapshotRelease(Snapshot);
    MdoEventsXrtError(Error, "cannot read the session event journal");
    return NULL;
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
         Info->Size < sizeof(*Info) ) return false;
    Size = Info->Size;
    *Info = Snapshot->Events[Index].Info;
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
