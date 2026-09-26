#include <string.h>

#include "internal.h"
#include "../../include/mdo/runs.h"

static cstr MdoApiRunStateName(xwork_run_state State)
{
    switch ( State ) {
    case XWORK_RUN_CREATED: return "created";
    case XWORK_RUN_RUNNING: return "running";
    case XWORK_RUN_SUCCEEDED: return "succeeded";
    case XWORK_RUN_FAILED: return "failed";
    case XWORK_RUN_CANCELLED: return "cancelled";
    case XWORK_RUN_TIMED_OUT: return "timed_out";
    default: return "unknown";
    }
}

static cstr MdoApiRunResultName(xwork_result Result)
{
    switch ( Result ) {
    case XWORK_RESULT_OK: return "ok";
    case XWORK_RESULT_ERROR: return "error";
    case XWORK_RESULT_CANCELLED: return "cancelled";
    case XWORK_RESULT_LIMIT: return "limit";
    case XWORK_RESULT_TIMEOUT: return "timeout";
    default: return "unknown";
    }
}

static bool MdoApiRunValue(const MdoRunInfo* Info, const char* FinalText,
    size_t FinalTextSize, bool IncludeResult, xvalue** Value)
{
    xvalue* Item = xrtValueObject();
    bool Ok = Item != NULL &&
        MdoApiValueSetString(Item, "id", Info->Id) &&
        MdoApiValueSetString(Item, "project_id", Info->ProjectId) &&
        MdoApiValueSetString(Item, "session_id", Info->SessionId) &&
        MdoApiValueSetUInt(Item, "agent_run_id", Info->AgentRunId) &&
        MdoApiValueSetString(Item, "agent_id", Info->AgentId) &&
        MdoApiValueSetString(Item, "model_id", Info->ModelId) &&
        MdoApiValueSetString(Item, "protocol",
            Info->Protocol != 0 ? MdoModelProtocolName(Info->Protocol) : "") &&
        MdoApiValueSetString(Item, "reasoning_effort",
            Info->ReasoningEffort) &&
        MdoApiValueSetString(Item, "state", MdoApiRunStateName(Info->State)) &&
        MdoApiValueSetString(Item, "result",
            Info->Terminal ? MdoApiRunResultName(Info->Result) : "pending") &&
        MdoApiValueSetString(Item, "error_code",
            xworkErrorCodeName(Info->ErrorCode)) &&
        MdoApiValueSetBool(Item, "terminal", Info->Terminal) &&
        MdoApiValueSetBool(Item, "cancel_requested", Info->CancelRequested) &&
        MdoApiValueSetBool(Item, "resume", Info->Resume) &&
        MdoApiValueSetInt(Item, "created_at", Info->CreatedAt) &&
        MdoApiValueSetInt(Item, "started_at", Info->StartedAt) &&
        MdoApiValueSetInt(Item, "ended_at", Info->EndedAt) &&
        MdoApiValueSetUInt(Item, "config_revision", Info->ConfigRevision) &&
        MdoApiValueSetUInt(Item, "model_generation", Info->ModelGeneration) &&
        MdoApiValueSetUInt(Item, "module_generation", Info->ModuleGeneration) &&
        MdoApiValueSetUInt(Item, "skill_generation", Info->SkillGeneration) &&
        MdoApiValueSetUInt(Item, "memory_generation", Info->MemoryGeneration) &&
        MdoApiValueSetUInt(Item, "agent_turns", Info->AgentTurns) &&
        MdoApiValueSetUInt(Item, "model_calls", Info->ModelCalls) &&
        MdoApiValueSetUInt(Item, "tool_calls", Info->ToolCalls) &&
        MdoApiValueSetUInt(Item, "compactions", Info->Compactions) &&
        MdoApiValueSetBool(Item, "final_text_available",
            Info->FinalTextAvailable) &&
        MdoApiValueSetBool(Item, "final_text_truncated",
            Info->FinalTextTruncated) &&
        MdoApiValueSetUInt(Item, "final_text_bytes", Info->FinalTextBytes);
    if ( Ok && IncludeResult ) {
        xvalue* Text = FinalText != NULL ?
            xrtValueString(xrtStrViewN(FinalText, FinalTextSize)) :
            xrtValueNull();
        Ok = Text != NULL && xrtValueObjectSetNew(Item,
            XRT_STR_LITERAL("final_text"), Text);
    }
    if ( !Ok ) {
        xrtValueRelease(Item);
        return false;
    }
    *Value = Item;
    return true;
}

static bool MdoApiRunPath(const MdoApiContext* Context,
    char RunId[MDO_RUN_ID_CAPACITY])
{
    size_t i;
    if ( Context->ParamCount != 1u || Context->Params[0].Size == 0u ||
         Context->Params[0].Size >= MDO_RUN_ID_CAPACITY ) return false;
    for ( i = 0u; i < Context->Params[0].Size; ++i ) {
        unsigned char Byte = (unsigned char)Context->Params[0].Data[i];
        if ( (Byte >= 'a' && Byte <= 'z') ||
             (Byte >= 'A' && Byte <= 'Z') ||
             (Byte >= '0' && Byte <= '9') || Byte == '-' || Byte == '_' ||
             (Byte == '.' && i != 0u) ) continue;
        return false;
    }
    memcpy(RunId, Context->Params[0].Data, Context->Params[0].Size);
    RunId[Context->Params[0].Size] = '\0';
    return true;
}

static bool MdoApiRunSessionPath(const MdoApiContext* Context,
    char Project[MDO_PROJECT_ID_CAPACITY],
    char SessionId[MDO_SESSION_ID_CAPACITY])
{
    size_t i;
    if ( Context->ParamCount != 2u || Context->Params[0].Size == 0u ||
         Context->Params[0].Size >= MDO_PROJECT_ID_CAPACITY ||
         Context->Params[1].Size == 0u ||
         Context->Params[1].Size >= MDO_SESSION_ID_CAPACITY ) return false;
    for ( i = 0u; i < Context->Params[0].Size; ++i ) {
        unsigned char Byte = (unsigned char)Context->Params[0].Data[i];
        if ( (Byte >= 'a' && Byte <= 'z') ||
             (Byte >= 'A' && Byte <= 'Z') ||
             (Byte >= '0' && Byte <= '9') || Byte == '-' || Byte == '_' ||
             (Byte == '.' && i != 0u) ) continue;
        return false;
    }
    for ( i = 0u; i < Context->Params[1].Size; ++i ) {
        unsigned char Byte = (unsigned char)Context->Params[1].Data[i];
        if ( (Byte >= 'a' && Byte <= 'z') ||
             (Byte >= 'A' && Byte <= 'Z') ||
             (Byte >= '0' && Byte <= '9') || Byte == '-' || Byte == '_' ||
             (Byte == '.' && i != 0u) ) continue;
        return false;
    }
    memcpy(Project, Context->Params[0].Data, Context->Params[0].Size);
    Project[Context->Params[0].Size] = '\0';
    memcpy(SessionId, Context->Params[1].Data, Context->Params[1].Size);
    SessionId[Context->Params[1].Size] = '\0';
    return true;
}

static bool MdoApiRunNoBody(const MdoApiContext* Context)
{
    const xhttp1head* Head = Context->Request->head;
    return !(((Head->Flags & (uint32)XHTTP1_CONTENT_LENGTH) != 0u &&
              Head->ContentLength != 0u) ||
             (Head->Flags & (uint32)XHTTP1_TRANSFER_ENCODING) != 0u);
}

static bool MdoApiRunReadUInt32(const xvalue* Object, cstr Name,
    uint32* Output, size_t* Present)
{
    const xvalue* Value = xrtValueObjectGet(Object, xrtStrView(Name));
    uint64 Number;
    int64 Signed;
    if ( Value == NULL ) return true;
    (*Present)++;
    if ( xrtValueType(Value) == XVALUE_UINT ) {
        if ( !xrtValueGetUInt(Value, &Number) ) return false;
    } else if ( xrtValueType(Value) == XVALUE_INT ) {
        if ( !xrtValueGetInt(Value, &Signed) || Signed < 0 ) return false;
        Number = (uint64)Signed;
    } else return false;
    if ( Number > UINT32_MAX ) return false;
    *Output = (uint32)Number;
    return true;
}

static bool MdoApiRunAttachmentIds(const xvalue* Object,
    char Ids[4][33], size_t* Count, size_t* Present)
{
    const xvalue* Array = xrtValueObjectGet(Object,
        XRT_STR_LITERAL("attachments"));
    if ( Array == NULL ) { *Count = 0u; return true; }
    (*Present)++;
    return MdoAttachmentIdsRead(Array, Ids, Count) && *Count != 0u;
}

static bool MdoApiRunQueueId(const xvalue* Object, char Id[33],
    size_t* Present)
{
    const xvalue* Value = xrtValueObjectGet(Object,
        XRT_STR_LITERAL("queue_item_id"));
    xstrview Text;
    size_t i;
    if ( Value == NULL ) return true;
    (*Present)++;
    if ( xrtValueType(Value) != XVALUE_STRING ||
         !xrtValueGetString(Value, &Text) || Text.Size != 32u ) return false;
    for ( i = 0u; i < Text.Size; ++i ) {
        unsigned char Byte = (unsigned char)Text.Data[i];
        if ( !((Byte >= '0' && Byte <= '9') ||
               (Byte >= 'a' && Byte <= 'f')) ) return false;
    }
    memcpy(Id, Text.Data, Text.Size);
    Id[Text.Size] = '\0';
    return true;
}

static bool MdoApiRunBuildMessage(const char* Project, const char* Session,
    const char* Prompt, char Ids[4][33], size_t Count,
    xllm_message* Message)
{
    xllm_part Part;
    size_t Total = 0u;
    size_t i;
    bool Ok;
    xllmMessageInit(Message, XLLM_ROLE_USER);
    xllmPartInit(&Part, XLLM_PART_TEXT);
    Ok = xllmPartSetText(&Part, Prompt) &&
        xllmMessageAddPart(Message, &Part);
    xllmPartUnit(&Part);
    for ( i = 0u; Ok && i < Count; ++i ) {
        char* Bytes = NULL;
        size_t Size = 0u;
        cstr Mime = NULL;
        Ok = MdoAttachmentReadForRun(Project, Session, Ids[i],
            &Bytes, &Size, &Mime) &&
            Size <= 16u * 1024u * 1024u - Total;
        if ( Ok ) {
            xllmPartInit(&Part, XLLM_PART_IMAGE);
            Ok = xllmPartSetImageData(&Part, Bytes, Size, Mime) &&
                xllmMessageAddPart(Message, &Part);
            xllmPartUnit(&Part);
            Total += Size;
        }
        xrtFree(Bytes);
    }
    if ( !Ok ) xllmMessageUnit(Message);
    return Ok;
}

static bool MdoApiRunStartFailure(MdoApiContext* Context,
    const xwork_error* Error)
{
    const xerror* Cause = xrtGetError();
    xerrkind Kind = Cause != NULL ? xrtErrorKind(Cause) : XERR_NONE;
    xrtClearError();
    if ( Kind == XERR_NOT_FOUND )
        return MdoApiReplyError(Context, 404u, "session_not_found",
            "The requested session does not exist", NULL);
    if ( Error != NULL && Error->eCode == XWORK_ERROR_LIMIT )
        return MdoApiReplyError(Context, 429u, "run_limit_reached",
            "The interactive run limit was reached", NULL);
    if ( Error != NULL &&
         (Error->eCode == XWORK_ERROR_CONTEXT ||
          Error->eCode == XWORK_ERROR_POLICY) )
        return MdoApiReplyError(Context, 409u, "session_busy",
            "The session is unavailable for a new run", NULL);
    if ( Error != NULL && Error->eCode == XWORK_ERROR_INVALID_ARGUMENT )
        return MdoApiReplyError(Context, 422u, "run_start_invalid",
            "The interactive run request is invalid", NULL);
    if ( Error != NULL && Error->eCode == XWORK_ERROR_IO )
        return MdoApiReplyError(Context, 500u, "session_read_failed",
            "The session could not be opened", NULL);
    if ( Error != NULL && Error->eCode == XWORK_ERROR_MODEL &&
         (Error->tModelError.eCode == XLLM_ERROR_AUTH ||
          Error->tModelError.eCode == XLLM_ERROR_OUT_OF_MEMORY) )
        return MdoApiReplyError(Context, 503u, "run_service_unavailable",
            "The session model service is not configured", NULL);
    if ( Error != NULL && Error->eCode == XWORK_ERROR_MODEL )
        return MdoApiReplyError(Context, 422u, "session_profile_invalid",
            "The session model profile is invalid", NULL);
    return MdoApiReplyError(Context, 503u, "run_service_unavailable",
        "The interactive run service is unavailable", NULL);
}

bool MdoApiRunsRoute(MdoApiContext* Context)
{
    MdoRunManagerStatus Status;
    MdoRunSnapshot* Snapshot;
    xwork_error Error;
    xvalue* Data = xrtValueObject();
    xvalue* Items = xrtValueArray();
    size_t Index;
    bool Ok;
    memset(&Error, 0, sizeof(Error));
    if ( !MdoRunManagerPump(NULL, &Error) ) {
        xrtValueRelease(Data);
        xrtValueRelease(Items);
        return MdoApiReplyError(Context, 503u, "run_service_unavailable",
            "The interactive run service is unavailable", NULL);
    }
    Snapshot = MdoRunSnapshotCreate(&Error);
    memset(&Status, 0, sizeof(Status)); Status.Size = sizeof(Status);
    Ok = Snapshot != NULL && MdoRunManagerGetStatus(&Status) &&
        Data != NULL && Items != NULL;
    Index = MdoRunSnapshotCount(Snapshot);
    while ( Ok && Index != 0u ) {
        MdoRunInfo Info;
        xvalue* Item = NULL;
        --Index;
        memset(&Info, 0, sizeof(Info)); Info.Size = sizeof(Info);
        Ok = MdoRunSnapshotAt(Snapshot, Index, &Info) &&
            MdoApiRunValue(&Info, NULL, 0u, false, &Item) &&
            MdoApiValueAppendTake(Items, &Item);
        xrtValueRelease(Item);
    }
    if ( Ok ) Ok =
        MdoApiValueSetBool(Data, "automatic", Status.Automatic) &&
        MdoApiValueSetUInt(Data, "active_runs", Status.ActiveRuns) &&
        MdoApiValueSetUInt(Data, "starting_runs", Status.StartingRuns) &&
        MdoApiValueSetUInt(Data, "retained_runs", Status.RetainedRuns) &&
        MdoApiValueSetUInt(Data, "max_active", Status.MaxActive) &&
        MdoApiValueSetUInt(Data, "max_retained", Status.MaxRetained) &&
        MdoApiValueSetUInt(Data, "runs_started", Status.RunsStarted) &&
        MdoApiValueSetUInt(Data, "runs_completed", Status.RunsCompleted) &&
        MdoApiValueSetUInt(Data, "runs_failed", Status.RunsFailed) &&
        MdoApiValueSetTake(Data, "items", &Items);
    xrtValueRelease(Items);
    MdoRunSnapshotRelease(Snapshot);
    if ( !Ok ) { xrtValueRelease(Data); Data = NULL; }
    if ( Data == NULL ) return MdoApiReplyError(Context, 500u,
        "runs_unavailable", "Interactive runs could not be created", NULL);
    return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
}

static bool MdoApiRunQueueFailure(MdoApiContext* Context,
    MdoApiQueueRunStatus Status)
{
    if ( Status == MDO_API_QUEUE_RUN_UNAVAILABLE )
        return MdoApiReplyError(Context, 503u, "queue_unavailable",
            "The queue could not be checked", NULL);
    if ( Status == MDO_API_QUEUE_RUN_ACCEPTED )
        return MdoApiReplyError(Context, 409u, "queue_run_started",
            "This queue item already started a run", NULL);
    if ( Status == MDO_API_QUEUE_RUN_STARTING )
        return MdoApiReplyError(Context, 409u, "queue_run_starting",
            "This queue item has a start in progress or needs review", NULL);
    return MdoApiReplyError(Context, 409u, "queue_run_conflict",
        "The sending queue item does not match this run", NULL);
}

bool MdoApiRunStartRoute(MdoApiContext* Context)
{
    MdoApiJsonBody Body;
    MdoApiBodyStatus BodyStatus;
    MdoRunStartOptions Options;
    MdoRunInfo Info;
    MdoSession* Session;
    MdoSessionInfo SessionInfo;
    xwork_error Error;
    const xvalue* PromptValue;
    xstrview Prompt;
    MdoModelCatalog* Catalog;
    MdoModelInfo Model;
    xllm_message UserMessage;
    char AttachmentIds[4][33] = {{ 0 }};
    size_t AttachmentCount = 0u;
    char QueueItemId[33] = { 0 };
    char Project[MDO_PROJECT_ID_CAPACITY];
    char SessionId[MDO_SESSION_ID_CAPACITY];
    char* PromptText = NULL;
    size_t Present = 0u;
    bool Valid;
    bool AttachmentLocked = false;
    bool QueueBound = true;
    if ( !MdoApiRunSessionPath(Context, Project, SessionId) )
        return MdoApiReplyError(Context, 400u, "invalid_run_path",
            "The project or session ID is invalid", NULL);
    BodyStatus = MdoApiJsonBodyRead(Context, &Body);
    if ( BodyStatus != MDO_API_BODY_OK )
        return MdoApiReplyBodyError(Context, BodyStatus);
    MdoRunStartOptionsInit(&Options);
    PromptValue = xrtValueType(Body.Value) == XVALUE_OBJECT ?
        xrtValueObjectGet(Body.Value, XRT_STR_LITERAL("prompt")) : NULL;
    Valid = xrtValueType(Body.Value) == XVALUE_OBJECT &&
        PromptValue != NULL && xrtValueType(PromptValue) == XVALUE_STRING &&
        xrtValueGetString(PromptValue, &Prompt) &&
        Prompt.Size < MDO_RUN_PROMPT_CAPACITY &&
        memchr(Prompt.Data, 0, Prompt.Size) == NULL &&
        xrtUtf8Valid(Prompt, NULL);
    if ( PromptValue != NULL ) Present++;
    Valid = Valid && MdoApiRunAttachmentIds(Body.Value, AttachmentIds,
        &AttachmentCount, &Present) &&
        MdoApiRunQueueId(Body.Value, QueueItemId, &Present) &&
        (Prompt.Size != 0u || AttachmentCount != 0u) &&
        MdoApiRunReadUInt32(Body.Value, "timeout_ms",
        &Options.TimeoutMilliseconds, &Present) &&
        Present == xrtValueCount(Body.Value);
    if ( !Valid ) {
        MdoApiJsonBodyUnit(&Body);
        return MdoApiReplyError(Context, 422u, "run_start_invalid",
            "The interactive run document is invalid", NULL);
    }
    PromptText = (char*)xrtMalloc(Prompt.Size + 1u);
    if ( PromptText == NULL ) {
        MdoApiJsonBodyUnit(&Body);
        return MdoApiReplyError(Context, 503u, "run_service_unavailable",
            "The interactive run service is unavailable", NULL);
    }
    memcpy(PromptText, Prompt.Data, Prompt.Size);
    PromptText[Prompt.Size] = '\0';
    memset(&Error, 0, sizeof(Error));
    xrtClearError();
    Session = MdoSessionLoad(Project, SessionId, &Error);
    if ( Session == NULL ) {
        xrtFree(PromptText);
        MdoApiJsonBodyUnit(&Body);
        return MdoApiRunStartFailure(Context, &Error);
    }
    memset(&SessionInfo, 0, sizeof(SessionInfo));
    SessionInfo.Size = sizeof(SessionInfo);
    Valid = MdoSessionGetInfo(Session, &SessionInfo);
    MdoSessionRelease(Session);
    if ( !Valid ) {
        xrtFree(PromptText);
        MdoApiJsonBodyUnit(&Body);
        return MdoApiReplyError(Context, 500u, "session_read_failed",
            "The session metadata could not be read", NULL);
    }
    if ( QueueItemId[0] != '\0' ) {
        MdoApiQueueRunStatus QueueStatus = MdoApiQueueRunPrepare(Project,
            SessionId, QueueItemId, Prompt, AttachmentIds,
            AttachmentCount);
        if ( QueueStatus != MDO_API_QUEUE_RUN_READY ) {
            xrtFree(PromptText);
            MdoApiJsonBodyUnit(&Body);
            return MdoApiRunQueueFailure(Context, QueueStatus);
        }
    }
    if ( SessionInfo.Status != MDO_SESSION_ACTIVE || SessionInfo.RuntimeOpen ) {
        xrtFree(PromptText);
        MdoApiJsonBodyUnit(&Body);
        return MdoApiReplyError(Context, 409u,
            SessionInfo.RuntimeOpen ? "session_busy" :
                "session_state_conflict",
            SessionInfo.RuntimeOpen ?
                "The session already has an active runtime" :
                "The session must be active before starting a run", NULL);
    }
    if ( AttachmentCount != 0u ) {
        Catalog = MdoModelCatalogSnapshot();
        memset(&Model, 0, sizeof(Model)); Model.Size = sizeof(Model);
        Valid = Catalog != NULL &&
            MdoModelCatalogModelFind(Catalog, SessionInfo.ModelId, &Model) &&
            (Model.Capabilities & XLLM_CAP_IMAGE_IN) != 0u &&
            (Model.Attachments & MDO_MODEL_ATTACHMENT_IMAGE) != 0u;
        MdoModelCatalogRelease(Catalog);
        if ( !Valid ) {
            xrtFree(PromptText);
            MdoApiJsonBodyUnit(&Body);
            return MdoApiReplyError(Context, 422u,
                "image_model_unsupported",
                "The selected model does not support image input", NULL);
        }
        AttachmentLocked = MdoApiAttachmentLock();
        if ( !AttachmentLocked ) {
            xrtFree(PromptText);
            MdoApiJsonBodyUnit(&Body);
            return MdoApiReplyError(Context, 503u,
                "attachment_unavailable", "Image storage is unavailable",
                NULL);
        }
        if ( !MdoApiRunBuildMessage(Project, SessionId, PromptText,
                AttachmentIds, AttachmentCount, &UserMessage) ) {
            MdoApiAttachmentUnlock();
            xrtFree(PromptText);
            MdoApiJsonBodyUnit(&Body);
            return MdoApiReplyError(Context, 422u,
                "attachment_invalid",
                "An image reference is missing, corrupt, or exceeds the limit",
                NULL);
        }
        Options.UserMessage = &UserMessage;
        Options.AttachmentIds = AttachmentIds;
        Options.AttachmentCount = AttachmentCount;
    }
    Options.ProjectId = Project;
    Options.SessionId = SessionId;
    Options.Prompt = PromptText;
    if ( QueueItemId[0] != '\0' ) {
        MdoApiQueueRunStatus QueueStatus = MdoApiQueueRunClaim(Project,
            SessionId, QueueItemId, Prompt, AttachmentIds,
            AttachmentCount);
        if ( QueueStatus != MDO_API_QUEUE_RUN_READY ) {
            if ( AttachmentLocked ) MdoApiAttachmentUnlock();
            if ( AttachmentCount != 0u ) xllmMessageUnit(&UserMessage);
            xrtFree(PromptText);
            MdoApiJsonBodyUnit(&Body);
            return MdoApiRunQueueFailure(Context, QueueStatus);
        }
    }
    memset(&Info, 0, sizeof(Info)); Info.Size = sizeof(Info);
    memset(&Error, 0, sizeof(Error));
    xrtClearError();
    Valid = MdoRunStart(&Options, &Info, &Error);
    if ( Valid && QueueItemId[0] != '\0' )
        QueueBound = MdoApiQueueRunBind(Project, SessionId,
            QueueItemId, Prompt, AttachmentIds, AttachmentCount, Info.Id);
    if ( AttachmentLocked ) MdoApiAttachmentUnlock();
    if ( AttachmentCount != 0u ) xllmMessageUnit(&UserMessage);
    xrtFree(PromptText);
    MdoApiJsonBodyUnit(&Body);
    if ( !Valid ) return MdoApiRunStartFailure(Context, &Error);
    if ( !QueueBound ) return MdoApiReplyError(Context, 503u,
        "run_receipt_unavailable",
        "The run started but its queue receipt could not be saved", NULL);
    {
        xvalue* Data = NULL;
        if ( !MdoApiRunValue(&Info, NULL, 0u, false, &Data) )
            return MdoApiReplyError(Context, 500u, "run_result_unavailable",
                "The run started but its result could not be created", NULL);
        return MdoApiReplySuccessTake(Context, 202u, Data, NULL);
    }
}

bool MdoApiRunRoute(MdoApiContext* Context)
{
    char RunId[MDO_RUN_ID_CAPACITY];
    MdoRunSnapshot* Snapshot;
    MdoRunInfo Info;
    const char* FinalText = NULL;
    size_t FinalTextSize = 0u;
    xwork_error Error;
    xvalue* Data = NULL;
    bool Found;
    if ( !MdoApiRunPath(Context, RunId) )
        return MdoApiReplyError(Context, 400u, "invalid_run_path",
            "The interactive run ID is invalid", NULL);
    if ( Context->Request->head->MethodCode == XHTTP_METHOD_DELETE &&
         !MdoApiRunNoBody(Context) )
        return MdoApiReplyError(Context, 400u, "body_not_allowed",
            "This operation does not accept a request body", NULL);
    memset(&Error, 0, sizeof(Error));
    if ( !MdoRunManagerPump(NULL, &Error) )
        return MdoApiReplyError(Context, 503u, "run_service_unavailable",
            "The interactive run service is unavailable", NULL);
    Snapshot = MdoRunSnapshotCreate(&Error);
    memset(&Info, 0, sizeof(Info)); Info.Size = sizeof(Info);
    Found = Snapshot != NULL && MdoRunSnapshotFind(Snapshot, RunId, &Info);
    if ( !Found ) {
        bool Unavailable = Snapshot == NULL;
        MdoRunSnapshotRelease(Snapshot);
        return MdoApiReplyError(Context, Unavailable ? 503u : 404u,
            Unavailable ? "run_service_unavailable" : "run_not_found",
            Unavailable ? "The interactive run service is unavailable" :
            "The requested interactive run does not exist", NULL);
    }
    if ( Context->Request->head->MethodCode == XHTTP_METHOD_DELETE ) {
        MdoRunSnapshotRelease(Snapshot);
        memset(&Info, 0, sizeof(Info)); Info.Size = sizeof(Info);
        if ( !MdoRunCancel(RunId, &Info, &Error) )
            return MdoApiReplyError(Context, 404u, "run_not_found",
                "The requested interactive run does not exist", NULL);
        if ( !MdoApiRunValue(&Info, NULL, 0u, false, &Data) )
            return MdoApiReplyError(Context, 500u, "run_result_unavailable",
                "The cancellation result could not be created", NULL);
        return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
    }
    if ( !MdoRunSnapshotResult(Snapshot, RunId, &FinalText, &FinalTextSize) ||
         !MdoApiRunValue(&Info, FinalText, FinalTextSize, true, &Data) ) {
        MdoRunSnapshotRelease(Snapshot);
        xrtValueRelease(Data);
        return MdoApiReplyError(Context, 500u, "run_result_unavailable",
            "The interactive run result could not be created", NULL);
    }
    MdoRunSnapshotRelease(Snapshot);
    return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
}
