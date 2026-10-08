#include <stdio.h>
#include <string.h>

#include "internal.h"
#include "../../include/mdo/agents.h"
#include "../../include/mdo/runs.h"
#include "../../include/mdo/sessions.h"

#define MDO_API_RECOVERY_CALL_MAX 32u
#define MDO_API_RECOVERY_CALL_ID_CAPACITY 257u
#define MDO_API_RECOVERY_TOOL_CAPACITY 129u
#define MDO_API_RECOVERY_ARGUMENT_MAX (16u * 1024u)
#define MDO_API_RECOVERY_ARGUMENT_TOTAL_MAX (128u * 1024u)

static bool MdoApiRecoveryPath(const MdoApiContext* Context,
    char Project[MDO_PROJECT_ID_CAPACITY],
    char SessionId[MDO_SESSION_ID_CAPACITY])
{
    size_t Part;
    if ( Context->ParamCount != 2u || Context->Params[0].Size == 0u ||
         Context->Params[0].Size >= MDO_PROJECT_ID_CAPACITY ||
         Context->Params[1].Size == 0u ||
         Context->Params[1].Size >= MDO_SESSION_ID_CAPACITY ) return false;
    for ( Part = 0u; Part < 2u; ++Part ) {
        size_t Index;
        xstrview Value = Context->Params[Part];
        for ( Index = 0u; Index < Value.Size; ++Index ) {
            unsigned char Byte = (unsigned char)Value.Data[Index];
            if ( (Byte >= 'a' && Byte <= 'z') ||
                 (Byte >= 'A' && Byte <= 'Z') ||
                 (Byte >= '0' && Byte <= '9') || Byte == '-' ||
                 Byte == '_' || (Byte == '.' && Index != 0u) ) continue;
            return false;
        }
    }
    memcpy(Project, Context->Params[0].Data, Context->Params[0].Size);
    Project[Context->Params[0].Size] = '\0';
    memcpy(SessionId, Context->Params[1].Data, Context->Params[1].Size);
    SessionId[Context->Params[1].Size] = '\0';
    return true;
}

static bool MdoApiRecoveryFailure(MdoApiContext* Context,
    const xwork_error* Error, const char* ConflictCode)
{
    const xerror* Cause = xrtGetError();
    xerrkind Kind = Cause != NULL ? xrtErrorKind(Cause) : XERR_NONE;
    xrtClearError();
    if ( Kind == XERR_NOT_FOUND )
        return MdoApiReplyError(Context, 404u, "session_not_found",
            "The requested session does not exist", NULL);
    if ( Error != NULL &&
         (Error->eCode == XWORK_ERROR_CONTEXT ||
          Error->eCode == XWORK_ERROR_POLICY ||
          Error->eCode == XWORK_ERROR_RECOVERY_REQUIRED) )
        return MdoApiReplyError(Context, 409u, ConflictCode,
            "The recovery state changed or the session is not idle", NULL);
    if ( Error != NULL && Error->eCode == XWORK_ERROR_INVALID_ARGUMENT )
        return MdoApiReplyError(Context, 422u, "recovery_resume_invalid",
            "The recovery decision document is invalid", NULL);
    if ( Error != NULL && Error->eCode == XWORK_ERROR_LIMIT )
        return MdoApiReplyError(Context, 429u, "run_limit_reached",
            "The interactive run limit was reached", NULL);
    if ( Error != NULL && Error->eCode == XWORK_ERROR_IO )
        return MdoApiReplyError(Context, 500u, "session_read_failed",
            "The session could not be opened", NULL);
    return MdoApiReplyError(Context, 503u, "recovery_service_unavailable",
        "The recovery service is unavailable", NULL);
}

static bool MdoApiRecoveryEffectNames(xwork_tool_effects Effects,
    xvalue** Value)
{
    static const struct {
        xwork_tool_effects Flag;
        const char* Name;
    } Names[] = {
        { XWORK_TOOL_EFFECT_READ, "read" },
        { XWORK_TOOL_EFFECT_WORKSPACE_WRITE, "workspace_write" },
        { XWORK_TOOL_EFFECT_PROCESS, "process" },
        { XWORK_TOOL_EFFECT_NETWORK, "network" },
        { XWORK_TOOL_EFFECT_EXTERNAL_SERVICE, "external_service" },
        { XWORK_TOOL_EFFECT_SECRETS, "secrets" },
        { XWORK_TOOL_EFFECT_SCHEDULE, "schedule" },
        { XWORK_TOOL_EFFECT_AGENT_DELEGATION, "agent_delegation" }
    };
    xvalue* Result = xrtValueArray();
    size_t Index;
    if ( Result == NULL ) return false;
    for ( Index = 0u; Index < sizeof(Names) / sizeof(Names[0]); ++Index ) {
        if ( (Effects & Names[Index].Flag) != 0u &&
             !MdoApiValueAppendString(Result, Names[Index].Name) ) {
            xrtValueRelease(Result);
            return false;
        }
    }
    *Value = Result;
    return true;
}

static bool MdoApiRecoveryItemValue(const xwork_recovery_call_info* Info,
    xvalue** Value)
{
    xvalue* Item = xrtValueObject();
    xvalue* Effects = NULL;
    bool Ok = Item != NULL && MdoApiRecoveryEffectNames(Info->uEffects,
        &Effects) &&
        MdoApiValueSetUInt(Item, "turn", Info->uTurn) &&
        MdoApiValueSetUInt(Item, "catalog_generation",
            Info->uCatalogGeneration) &&
        MdoApiValueSetString(Item, "tool_call_id", Info->sToolCallId) &&
        MdoApiValueSetString(Item, "tool", Info->sToolName) &&
        MdoApiValueSetString(Item, "arguments_json", Info->sArgumentsJson) &&
        MdoApiValueSetUInt(Item, "effect_mask", Info->uEffects) &&
        MdoApiValueSetTake(Item, "effects", &Effects) &&
        MdoApiValueSetBool(Item, "tool_available", Info->bToolAvailable) &&
        MdoApiValueSetBool(Item, "automatic_retry_safe",
            Info->bAutomaticRetrySafe);
    xrtValueRelease(Effects);
    if ( !Ok ) {
        xrtValueRelease(Item);
        return false;
    }
    *Value = Item;
    return true;
}

/* Absence of a terminal event is positive evidence of an interrupted runtime,
 * not of a user stop. A recorded stop or ordinary model error remains manual. */
static bool MdoApiRecoveryMayContinue(const MdoSessionInfo* Session, bool MeteredSearch)
{
    MdoConversationPageInfo Page;
    MdoSessionEventSnapshot* Events;
    xwork_error Error;
    uint64 Run = 0u;
    bool Started = false, Done = false, Failed = false, PersistenceFailed = false;
    size_t Index;
    memset(&Page, 0, sizeof(Page)); xworkErrorInit(&Error);
    Events = MdoSessionConversationPage(Session->ProjectId, Session->Id,
        0u, 0u, NULL, 1u, &Page, &Error);
    if (!Events) { xrtClearError(); return false; }
    for (Index = 0u; Index < MdoSessionEventSnapshotCount(Events); ++Index) {
        MdoSessionEventInfo Event;
        memset(&Event, 0, sizeof(Event)); Event.Size = sizeof(Event);
        if (!MdoSessionEventSnapshotAt(Events, Index, &Event) || Event.AgentDepth != 0u) continue;
        if (Event.Kind == XWORK_EVENT_AGENT_START) {
            Run = Event.RunId; Started = true; Done = Failed = PersistenceFailed = false;
        } else if (Started && Event.RunId == Run) {
            if (Event.Kind == XWORK_EVENT_AGENT_DONE) Done = true;
            if (Event.Kind == XWORK_EVENT_ERROR) {
                Failed = true;
                PersistenceFailed = Event.Text && strstr(Event.Text, "session event persistence failed") != NULL;
            }
        }
    }
    MdoSessionEventSnapshotRelease(Events);
    /* A paid request lost before its receipt is not provably unbilled. Until
     * the service supports idempotency, ask for one Continue click in that
     * ambiguous crash window rather than automatically resubmitting it. */
    return Started && ((!Done && !Failed && !MeteredSearch) || PersistenceFailed);
}

static bool MdoApiRecoveryReply(MdoApiContext* Context,
    const MdoSessionInfo* SessionInfo, xwork_recovery_snapshot* Snapshot,
    bool ResumeRequired, uint64 LastSequence)
{
    xvalue* Data = xrtValueObject();
    xvalue* Items = xrtValueArray();
    size_t Count = xworkRecoverySnapshotCount(Snapshot);
    size_t ArgumentTotal = 0u;
    uint64 CatalogGeneration = 0u;
    char RecoveryToken[MDO_AGENT_RECOVERY_TOKEN_CAPACITY];
    size_t Index;
    bool AllRead = Count != 0u;
    bool MeteredSearch = false;
    bool Ok = Data != NULL && Items != NULL &&
        Count <= MDO_API_RECOVERY_CALL_MAX &&
        MdoAgentRecoverySnapshotToken(Snapshot, ResumeRequired, LastSequence,
            RecoveryToken);

    for ( Index = 0u; Ok && Index < Count; ++Index ) {
        xwork_recovery_call_info Info;
        xvalue* Item = NULL;
        size_t CallIdSize;
        size_t ToolSize;
        size_t ArgumentSize;
        memset(&Info, 0, sizeof(Info));
        Info.uSize = sizeof(Info);
        Info.uAbiVersion = XWORK_ABI_VERSION;
        Ok = xworkRecoverySnapshotAt(Snapshot, Index, &Info) &&
            Info.sToolCallId != NULL && Info.sToolName != NULL &&
            Info.sArgumentsJson != NULL;
        if ( !Ok ) break;
        AllRead = AllRead && Info.bToolAvailable && Info.bAutomaticRetrySafe;
        MeteredSearch = MeteredSearch || strcmp(Info.sToolName, "web_search") == 0;
        CallIdSize = strlen(Info.sToolCallId);
        ToolSize = strlen(Info.sToolName);
        ArgumentSize = strlen(Info.sArgumentsJson);
        Ok = CallIdSize != 0u &&
            CallIdSize < MDO_API_RECOVERY_CALL_ID_CAPACITY &&
            ToolSize != 0u && ToolSize < MDO_API_RECOVERY_TOOL_CAPACITY &&
            ArgumentSize <= MDO_API_RECOVERY_ARGUMENT_MAX &&
            ArgumentTotal <= MDO_API_RECOVERY_ARGUMENT_TOTAL_MAX -
                ArgumentSize &&
            xrtUtf8Valid(xrtStrViewN(Info.sToolCallId, CallIdSize), NULL) &&
            xrtUtf8Valid(xrtStrViewN(Info.sToolName, ToolSize), NULL) &&
            xrtUtf8Valid(xrtStrViewN(Info.sArgumentsJson, ArgumentSize), NULL);
        if ( !Ok ) break;
        ArgumentTotal += ArgumentSize;
        if ( Index == 0u ) CatalogGeneration = Info.uCatalogGeneration;
        else if ( CatalogGeneration != Info.uCatalogGeneration ) Ok = false;
        if ( Ok ) Ok = MdoApiRecoveryItemValue(&Info, &Item) &&
            MdoApiValueAppendTake(Items, &Item);
        xrtValueRelease(Item);
    }
    if ( Ok ) Ok =
        MdoApiValueSetString(Data, "project_id", SessionInfo->ProjectId) &&
        MdoApiValueSetString(Data, "session_id", SessionInfo->Id) &&
        MdoApiValueSetUInt(Data, "revision", SessionInfo->Revision) &&
        MdoApiValueSetUInt(Data, "last_sequence", LastSequence) &&
        MdoApiValueSetUInt(Data, "catalog_generation", CatalogGeneration) &&
        MdoApiValueSetString(Data, "recovery_token", RecoveryToken) &&
        MdoApiValueSetBool(Data, "resume_required", ResumeRequired) &&
        MdoApiValueSetBool(Data, "automatic_resume", ResumeRequired && AllRead &&
            MdoApiRecoveryMayContinue(SessionInfo, MeteredSearch)) &&
        MdoApiValueSetUInt(Data, "total", Count) &&
        MdoApiValueSetTake(Data, "items", &Items);
    xrtValueRelease(Items);
    if ( !Ok ) {
        xrtValueRelease(Data);
        return MdoApiReplyError(Context, 409u,
            "recovery_view_too_large",
            "The pending recovery state cannot be represented safely", NULL);
    }
    {
        char EntityTag[96];
        int Written = snprintf(EntityTag, sizeof(EntityTag),
            "\"mdo-session-%s-%llu\"", SessionInfo->Id,
            (unsigned long long)SessionInfo->Revision);
        if ( Written <= 0 || (size_t)Written >= sizeof(EntityTag) ) {
            xrtValueRelease(Data);
            return MdoApiReplyError(Context, 500u,
                "recovery_result_unavailable",
                "The recovery result could not be created", NULL);
        }
        return MdoApiReplySuccessTakeEntityTag(Context, 200u, Data,
            EntityTag);
    }
}

bool MdoApiSessionRecoveryRoute(MdoApiContext* Context)
{
    char Project[MDO_PROJECT_ID_CAPACITY];
    char SessionId[MDO_SESSION_ID_CAPACITY];
    MdoSession* Session;
    MdoAgentSession* Agent;
    MdoSessionInfo Info;
    xwork_recovery_snapshot* Snapshot;
    xwork_error Error;
    bool Result;
    bool ResumeRequired = false;
    uint64 LastSequence = 0u;

    if ( !MdoApiRecoveryPath(Context, Project, SessionId) )
        return MdoApiReplyError(Context, 400u, "invalid_session_path",
            "The project or session ID is invalid", NULL);
    memset(&Error, 0, sizeof(Error));
    xrtClearError();
    Session = MdoSessionOpen(Project, SessionId, NULL, &Error);
    if ( Session == NULL )
        return MdoApiRecoveryFailure(Context, &Error,
            "recovery_state_conflict");
    memset(&Info, 0, sizeof(Info));
    Info.Size = sizeof(Info);
    Agent = MdoSessionAgentRef(Session);
    if ( Agent == NULL || !MdoSessionGetInfo(Session, &Info) ) {
        MdoAgentSessionRelease(Agent);
        MdoSessionRelease(Session);
        return MdoApiReplyError(Context, 500u,
            "recovery_result_unavailable",
            "The recovery state could not be inspected", NULL);
    }
    Snapshot = MdoAgentSessionRecoverySnapshot(Agent, &Error);
    if ( Snapshot != NULL && !MdoAgentSessionRecoveryRequired(Agent,
            &ResumeRequired, &Error) ) {
        xworkRecoverySnapshotRelease(Snapshot);
        Snapshot = NULL;
    }
    if ( Snapshot != NULL && !MdoAgentSessionLastSequence(Agent,
            &LastSequence, &Error) ) {
        xworkRecoverySnapshotRelease(Snapshot);
        Snapshot = NULL;
    }
    MdoAgentSessionRelease(Agent);
    if ( Snapshot == NULL ) {
        MdoSessionRelease(Session);
        return MdoApiRecoveryFailure(Context, &Error,
            "recovery_state_conflict");
    }
    Result = MdoApiRecoveryReply(Context, &Info, Snapshot, ResumeRequired,
        LastSequence);
    xworkRecoverySnapshotRelease(Snapshot);
    MdoSessionRelease(Session);
    return Result;
}

static bool MdoApiRecoveryDecision(const xvalue* Value,
    xwork_recovery_decision* Decision,
    char CallId[MDO_API_RECOVERY_CALL_ID_CAPACITY])
{
    const xvalue* IdValue;
    const xvalue* ActionValue;
    xstrview Id;
    xstrview Action;
    if ( Value == NULL || xrtValueType(Value) != XVALUE_OBJECT ||
         xrtValueCount(Value) != 2u ) return false;
    IdValue = xrtValueObjectGet(Value, XRT_STR_LITERAL("tool_call_id"));
    ActionValue = xrtValueObjectGet(Value, XRT_STR_LITERAL("action"));
    if ( IdValue == NULL || ActionValue == NULL ||
         xrtValueType(IdValue) != XVALUE_STRING ||
         xrtValueType(ActionValue) != XVALUE_STRING ||
         !xrtValueGetString(IdValue, &Id) ||
         !xrtValueGetString(ActionValue, &Action) || Id.Size == 0u ||
         Id.Size >= MDO_API_RECOVERY_CALL_ID_CAPACITY ||
         memchr(Id.Data, 0, Id.Size) != NULL ||
         !xrtUtf8Valid(Id, NULL) ) return false;
    memcpy(CallId, Id.Data, Id.Size);
    CallId[Id.Size] = '\0';
    xworkRecoveryDecisionInit(Decision);
    Decision->sToolCallId = CallId;
    if ( Action.Size == sizeof("retry") - 1u &&
         memcmp(Action.Data, "retry", Action.Size) == 0 )
        Decision->eAction = XWORK_RECOVERY_RETRY;
    else if ( Action.Size == sizeof("record_uncertain") - 1u &&
              memcmp(Action.Data, "record_uncertain", Action.Size) == 0 )
        Decision->eAction = XWORK_RECOVERY_RECORD_UNCERTAIN;
    else return false;
    return true;
}

static bool MdoApiRecoveryToken(const xvalue* Object,
    char Token[MDO_AGENT_RECOVERY_TOKEN_CAPACITY])
{
    const xvalue* Value = xrtValueObjectGet(Object,
        XRT_STR_LITERAL("recovery_token"));
    xstrview Text;
    size_t Index;
    if ( Value == NULL || xrtValueType(Value) != XVALUE_STRING ||
         !xrtValueGetString(Value, &Text) ||
         Text.Size != MDO_AGENT_RECOVERY_TOKEN_CAPACITY - 1u ) return false;
    for ( Index = 0u; Index < Text.Size; ++Index ) {
        unsigned char Byte = (unsigned char)Text.Data[Index];
        if ( !((Byte >= '0' && Byte <= '9') ||
               (Byte >= 'a' && Byte <= 'f')) ) return false;
    }
    memcpy(Token, Text.Data, Text.Size);
    Token[Text.Size] = '\0';
    return true;
}

static bool MdoApiRecoveryUnsigned(const xvalue* Value, uint64* Output)
{
    int64 Signed;
    if ( Value == NULL || Output == NULL ) return false;
    if ( xrtValueType(Value) == XVALUE_UINT )
        return xrtValueGetUInt(Value, Output);
    if ( xrtValueType(Value) != XVALUE_INT ||
         !xrtValueGetInt(Value, &Signed) || Signed < 0 ) return false;
    *Output = (uint64)Signed;
    return true;
}

bool MdoApiSessionResumeRoute(MdoApiContext* Context)
{
    char Project[MDO_PROJECT_ID_CAPACITY];
    char SessionId[MDO_SESSION_ID_CAPACITY];
    char CallIds[MDO_API_RECOVERY_CALL_MAX]
        [MDO_API_RECOVERY_CALL_ID_CAPACITY];
    xwork_recovery_decision Decisions[MDO_API_RECOVERY_CALL_MAX];
    xwork_resume_options ResumeOptions;
    MdoRunStartOptions Options;
    MdoRunInfo Info;
    MdoApiJsonBody Body;
    MdoApiBodyStatus BodyStatus;
    const xvalue* DecisionValues;
    char RecoveryToken[MDO_AGENT_RECOVERY_TOKEN_CAPACITY];
    size_t Count;
    size_t Index;
    bool Valid;
    xwork_error Error;
    char ClientResumeId[33] = {0};
    const xvalue* ClientIdValue;
    xstrview ClientId;

    if ( !MdoApiRecoveryPath(Context, Project, SessionId) )
        return MdoApiReplyError(Context, 400u, "invalid_session_path",
            "The project or session ID is invalid", NULL);
    BodyStatus = MdoApiJsonBodyRead(Context, &Body);
    if ( BodyStatus != MDO_API_BODY_OK )
        return MdoApiReplyBodyError(Context, BodyStatus);
    DecisionValues = xrtValueType(Body.Value) == XVALUE_OBJECT ?
        xrtValueObjectGet(Body.Value, XRT_STR_LITERAL("decisions")) : NULL;
    ClientIdValue = xrtValueType(Body.Value) == XVALUE_OBJECT ?
        xrtValueObjectGet(Body.Value, XRT_STR_LITERAL("client_resume_id")) : NULL;
    Valid = xrtValueType(Body.Value) == XVALUE_OBJECT &&
        xrtValueCount(Body.Value) == (ClientIdValue != NULL ? 3u : 2u) &&
        MdoApiRecoveryToken(Body.Value, RecoveryToken) &&
        DecisionValues != NULL &&
        xrtValueType(DecisionValues) == XVALUE_ARRAY;
    if ( Valid && ClientIdValue != NULL ) {
        Valid = xrtValueType(ClientIdValue) == XVALUE_STRING &&
            xrtValueGetString(ClientIdValue, &ClientId) && ClientId.Size == 32u;
        for ( Index = 0u; Valid && Index < ClientId.Size; ++Index )
            if ( !((ClientId.Data[Index] >= '0' && ClientId.Data[Index] <= '9') ||
                   (ClientId.Data[Index] >= 'a' && ClientId.Data[Index] <= 'f')) ) Valid = false;
        if ( Valid ) memcpy(ClientResumeId, ClientId.Data, 32u);
    }
    Count = Valid ? xrtValueCount(DecisionValues) : 0u;
    Valid = Valid && Count <= MDO_API_RECOVERY_CALL_MAX;
    for ( Index = 0u; Valid && Index < Count; ++Index ) {
        size_t Previous;
        Valid = MdoApiRecoveryDecision(xrtValueArrayGet(DecisionValues,
            Index), &Decisions[Index], CallIds[Index]);
        for ( Previous = 0u; Valid && Previous < Index; ++Previous ) {
            if ( strcmp(CallIds[Index], CallIds[Previous]) == 0 )
                Valid = false;
        }
    }
    if ( !Valid ) {
        MdoApiJsonBodyUnit(&Body);
        return MdoApiReplyError(Context, 422u,
            "recovery_resume_invalid",
            "Resume requires a recovery token and unique retry or uncertainty decisions",
            NULL);
    }
    xworkResumeOptionsInit(&ResumeOptions);
    ResumeOptions.pDecisions = Decisions;
    ResumeOptions.iDecisionCount = Count;
    MdoRunStartOptionsInit(&Options);
    Options.ProjectId = Project;
    Options.SessionId = SessionId;
    Options.Resume = true;
    Options.RecoveryToken = RecoveryToken;
    Options.ClientResumeId = ClientIdValue != NULL ? ClientResumeId : NULL;
    Options.ResumeOptions = &ResumeOptions;
    memset(&Info, 0, sizeof(Info));
    Info.Size = sizeof(Info);
    memset(&Error, 0, sizeof(Error));
    xrtClearError();
    Valid = MdoRunStart(&Options, &Info, &Error);
    MdoApiJsonBodyUnit(&Body);
    if ( !Valid ) {
        return MdoApiRecoveryFailure(Context, &Error,
            "recovery_state_conflict");
    }
    {
        xvalue* Data = NULL;
        /* Run JSON is shared with the regular run endpoints. Keep the small
         * response explicit here to avoid a public serializer dependency. */
        Data = xrtValueObject();
        if ( Data == NULL ||
             !MdoApiValueSetString(Data, "id", Info.Id) ||
             !MdoApiValueSetString(Data, "project_id", Info.ProjectId) ||
             !MdoApiValueSetString(Data, "session_id", Info.SessionId) ||
             !MdoApiValueSetString(Data, "state", "running") ||
             !MdoApiValueSetString(Data, "result", "pending") ||
             !MdoApiValueSetBool(Data, "terminal", false) ||
             !MdoApiValueSetBool(Data, "cancel_requested", false) ||
             !MdoApiValueSetBool(Data, "resume", true) ||
             !MdoApiValueSetString(Data, "client_resume_id", Info.ClientResumeId) ) {
            xrtValueRelease(Data);
            return MdoApiReplyError(Context, 500u,
                "run_result_unavailable",
                "The recovery run started but its result could not be created",
                NULL);
        }
        return MdoApiReplySuccessTake(Context, 202u, Data, NULL);
    }
}

bool MdoApiSessionAbandonRoute(MdoApiContext* Context)
{
    char Project[MDO_PROJECT_ID_CAPACITY];
    char SessionId[MDO_SESSION_ID_CAPACITY];
    MdoApiJsonBody Body;
    MdoApiBodyStatus BodyStatus;
    const xvalue* RevisionValue;
    const xvalue* SequenceValue;
    MdoSession* Session;
    MdoSessionInfo Info;
    xwork_error Error;
    uint64 Revision;
    uint64 Sequence;
    uint64 FinishedSequence = 0u;
    xvalue* Data;
    bool Ok;
    if ( !MdoApiRecoveryPath(Context, Project, SessionId) )
        return MdoApiReplyError(Context, 400u, "invalid_session_path",
            "The project or session ID is invalid", NULL);
    BodyStatus = MdoApiJsonBodyRead(Context, &Body);
    if ( BodyStatus != MDO_API_BODY_OK )
        return MdoApiReplyBodyError(Context, BodyStatus);
    RevisionValue = xrtValueType(Body.Value) == XVALUE_OBJECT ?
        xrtValueObjectGet(Body.Value, XRT_STR_LITERAL("revision")) : NULL;
    SequenceValue = xrtValueType(Body.Value) == XVALUE_OBJECT ?
        xrtValueObjectGet(Body.Value, XRT_STR_LITERAL("last_sequence")) : NULL;
    Ok = xrtValueType(Body.Value) == XVALUE_OBJECT &&
        xrtValueCount(Body.Value) == 2u && RevisionValue != NULL &&
        SequenceValue != NULL &&
        MdoApiRecoveryUnsigned(RevisionValue, &Revision) &&
        MdoApiRecoveryUnsigned(SequenceValue, &Sequence) &&
        Revision != 0u && Sequence != 0u;
    MdoApiJsonBodyUnit(&Body);
    if ( !Ok ) return MdoApiReplyError(Context, 422u,
        "recovery_abandon_invalid",
        "Abandon requires the exact session revision and ledger sequence",
        NULL);
    memset(&Error, 0, sizeof(Error));
    xrtClearError();
    Session = MdoSessionOpen(Project, SessionId, NULL, &Error);
    if ( Session == NULL ) return MdoApiRecoveryFailure(Context, &Error,
        "recovery_state_conflict");
    Ok = MdoSessionFinishInterrupted(Session, Revision, Sequence,
        &FinishedSequence, &Error);
    memset(&Info, 0, sizeof(Info));
    Info.Size = sizeof(Info);
    if ( Ok ) Ok = MdoSessionGetInfo(Session, &Info);
    MdoSessionRelease(Session);
    if ( !Ok ) return MdoApiRecoveryFailure(Context, &Error,
        "recovery_state_conflict");
    Data = xrtValueObject();
    if ( Data == NULL ||
         !MdoApiValueSetBool(Data, "resume_required", false) ||
         !MdoApiValueSetUInt(Data, "revision", Info.Revision) ||
         !MdoApiValueSetUInt(Data, "last_sequence", FinishedSequence) ) {
        xrtValueRelease(Data);
        return MdoApiReplyError(Context, 500u,
            "recovery_result_unavailable",
            "The interrupted turn was closed but its result is unavailable",
            NULL);
    }
    return MdoApiReplySuccessTakeRevision(Context, 200u, Data, Info.Revision);
}
