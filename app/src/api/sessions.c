#include <stdio.h>
#include <string.h>

#include "internal.h"
#include "../../include/mdo/sessions.h"

static bool MdoApiSessionString(const xvalue* Object, cstr Name,
    char* Output, size_t Capacity, bool Required, size_t* Present)
{
    const xvalue* Value = xrtValueObjectGet(Object, xrtStrView(Name));
    xstrview Text;
    if ( Value == NULL ) return !Required;
    (*Present)++;
    if ( xrtValueType(Value) != XVALUE_STRING ||
         !xrtValueGetString(Value, &Text) || Text.Size == 0u ||
         Text.Size >= Capacity || memchr(Text.Data, 0, Text.Size) != NULL )
        return false;
    memcpy(Output, Text.Data, Text.Size);
    Output[Text.Size] = '\0';
    return true;
}

static bool MdoApiSessionUnsigned(const xvalue* Object, cstr Name,
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
    if ( Number == 0u || Number > UINT32_MAX ) return false;
    *Output = (uint32)Number;
    return true;
}

static bool MdoApiSessionProtocol(cstr Text, MdoModelProtocol* Protocol)
{
    if ( Text[0] == '\0' || strcmp(Text, "default") == 0 ) {
        *Protocol = 0;
        return true;
    }
    if ( strcmp(Text, "openai-chat-completions") == 0 )
        *Protocol = MDO_MODEL_PROTOCOL_OPENAI_CHAT_COMPLETIONS;
    else if ( strcmp(Text, "openai-responses") == 0 )
        *Protocol = MDO_MODEL_PROTOCOL_OPENAI_RESPONSES;
    else if ( strcmp(Text, "anthropic-messages") == 0 )
        *Protocol = MDO_MODEL_PROTOCOL_ANTHROPIC_MESSAGES;
    else return false;
    return true;
}

static bool MdoApiSessionInfoValue(const MdoSessionInfo* Info,
    xvalue** Value)
{
    xvalue* Data = xrtValueObject();
    bool Ok = Data != NULL &&
        MdoApiValueSetString(Data, "id", Info->Id) &&
        MdoApiValueSetString(Data, "project_id", Info->ProjectId) &&
        MdoApiValueSetString(Data, "parent_session_id",
            Info->ParentSessionId) &&
        MdoApiValueSetString(Data, "title", Info->Title) &&
        MdoApiValueSetString(Data, "agent_id", Info->AgentId) &&
        MdoApiValueSetString(Data, "model_id", Info->ModelId) &&
        MdoApiValueSetString(Data, "protocol",
            MdoModelProtocolName(Info->Protocol)) &&
        MdoApiValueSetString(Data, "reasoning_effort",
            Info->ReasoningEffort) &&
        MdoApiValueSetString(Data, "workspace_root", Info->WorkspaceRoot) &&
        MdoApiValueSetString(Data, "status",
            Info->Status == MDO_SESSION_ACTIVE ? "active" :
            (Info->Status == MDO_SESSION_ARCHIVED ? "archived" : "trash")) &&
        MdoApiValueSetUInt(Data, "revision", Info->Revision) &&
        MdoApiValueSetInt(Data, "created_at", Info->CreatedAt) &&
        MdoApiValueSetInt(Data, "updated_at", Info->UpdatedAt) &&
        MdoApiValueSetBool(Data, "pinned", Info->Pinned) &&
        MdoApiValueSetBool(Data, "runtime_open", Info->RuntimeOpen) &&
        MdoApiValueSetUInt(Data, "config_revision", Info->ConfigRevision) &&
        MdoApiValueSetUInt(Data, "model_generation", Info->ModelGeneration) &&
        MdoApiValueSetUInt(Data, "module_generation",
            Info->ModuleGeneration) &&
        MdoApiValueSetUInt(Data, "skill_generation", Info->SkillGeneration) &&
        MdoApiValueSetUInt(Data, "max_output_tokens",
            Info->MaxOutputTokens) &&
        MdoApiValueSetUInt(Data, "forked_through_sequence",
            Info->ForkedThroughSequence);
    if ( !Ok ) {
        xrtValueRelease(Data);
        return false;
    }
    *Value = Data;
    return true;
}

static bool MdoApiSessionReply(MdoApiContext* Context, uint16 Status,
    const MdoSessionInfo* Info)
{
    char EntityTag[96];
    xvalue* Data = NULL;
    int Count = snprintf(EntityTag, sizeof(EntityTag),
        "\"mdo-session-%s-%llu\"", Info->Id,
        (unsigned long long)Info->Revision);
    if ( Count <= 0 || (size_t)Count >= sizeof(EntityTag) ||
         !MdoApiSessionInfoValue(Info, &Data) ) {
        xrtValueRelease(Data);
        return MdoApiReplyError(Context, 500u, "session_result_unavailable",
            "The session result could not be created", NULL);
    }
    return MdoApiReplySuccessTakeEntityTag(Context, Status, Data, EntityTag);
}

static bool MdoApiSessionCreateFailure(MdoApiContext* Context,
    const xwork_error* Error)
{
    if ( Error != NULL && Error->eCode == XWORK_ERROR_IO )
        return MdoApiReplyError(Context, 500u, "session_persistence_failed",
            "The session could not be persisted", NULL);
    if ( Error != NULL &&
         (Error->eCode == XWORK_ERROR_OUT_OF_MEMORY ||
          Error->eCode == XWORK_ERROR_CONTEXT ||
          (Error->eCode == XWORK_ERROR_MODEL &&
           (Error->tModelError.eCode == XLLM_ERROR_AUTH ||
            Error->tModelError.eCode == XLLM_ERROR_OUT_OF_MEMORY))) ) {
        return MdoApiReplyError(Context, 503u, "session_service_unavailable",
            "The session service is not ready for this request", NULL);
    }
    if ( Error != NULL &&
         (Error->eCode == XWORK_ERROR_INVALID_ARGUMENT ||
          Error->eCode == XWORK_ERROR_MODEL ||
          Error->eCode == XWORK_ERROR_POLICY ||
          Error->eCode == XWORK_ERROR_LIMIT) ) {
        return MdoApiReplyError(Context, 422u, "session_profile_invalid",
            "The requested session profile is invalid", NULL);
    }
    return MdoApiReplyError(Context, 500u, "session_create_failed",
        "The session could not be created", NULL);
}

bool MdoApiSessionCreateRoute(MdoApiContext* Context)
{
    MdoApiJsonBody Body;
    MdoApiBodyStatus BodyStatus;
    MdoSessionCreateOptions Options;
    MdoSessionInfo Info;
    MdoSession* Session;
    xwork_error Error;
    char Project[MDO_PROJECT_ID_CAPACITY] = { 0 };
    char Title[MDO_SESSION_TITLE_CAPACITY] = { 0 };
    char Agent[MDO_SESSION_IDENTITY_CAPACITY] = { 0 };
    char Model[MDO_SESSION_IDENTITY_CAPACITY] = { 0 };
    char Protocol[40] = { 0 };
    char Reasoning[MDO_SESSION_REASONING_CAPACITY] = { 0 };
    char Workspace[MDO_SESSION_WORKSPACE_CAPACITY] = { 0 };
    size_t Present = 0u;
    bool Valid;

    BodyStatus = MdoApiJsonBodyRead(Context, &Body);
    if ( BodyStatus != MDO_API_BODY_OK )
        return MdoApiReplyBodyError(Context, BodyStatus);
    MdoSessionCreateOptionsInit(&Options);
    Valid = xrtValueType(Body.Value) == XVALUE_OBJECT &&
        MdoApiSessionString(Body.Value, "project_id", Project,
            sizeof(Project), true, &Present) &&
        MdoApiSessionString(Body.Value, "title", Title,
            sizeof(Title), false, &Present) &&
        MdoApiSessionString(Body.Value, "agent_id", Agent,
            sizeof(Agent), false, &Present) &&
        MdoApiSessionString(Body.Value, "model_id", Model,
            sizeof(Model), false, &Present) &&
        MdoApiSessionString(Body.Value, "protocol", Protocol,
            sizeof(Protocol), false, &Present) &&
        MdoApiSessionString(Body.Value, "reasoning_effort", Reasoning,
            sizeof(Reasoning), false, &Present) &&
        MdoApiSessionString(Body.Value, "workspace_root", Workspace,
            sizeof(Workspace), false, &Present) &&
        MdoApiSessionUnsigned(Body.Value, "max_output_tokens",
            &Options.Agent.MaxOutputTokens, &Present) &&
        Present == xrtValueCount(Body.Value) &&
        MdoApiSessionProtocol(Protocol, &Options.Agent.Protocol);
    if ( !Valid ) {
        MdoApiJsonBodyUnit(&Body);
        return MdoApiReplyError(Context, 422u, "session_create_invalid",
            "The session create document is invalid", NULL);
    }
    Options.ProjectId = Project;
    Options.Title = Title[0] != '\0' ? Title : NULL;
    Options.Agent.AgentId = Agent[0] != '\0' ? Agent : NULL;
    Options.Agent.ModelId = Model[0] != '\0' ? Model : NULL;
    Options.Agent.ReasoningEffort = Reasoning[0] != '\0' ? Reasoning : NULL;
    Options.Agent.WorkspaceRoot = Workspace[0] != '\0' ? Workspace : NULL;
    memset(&Error, 0, sizeof(Error));
    Session = MdoSessionCreate(&Options, &Error);
    MdoApiJsonBodyUnit(&Body);
    if ( Session == NULL )
        return MdoApiSessionCreateFailure(Context, &Error);
    memset(&Info, 0, sizeof(Info)); Info.Size = sizeof(Info);
    if ( !MdoSessionGetInfo(Session, &Info) ) {
        MdoSessionRelease(Session);
        return MdoApiReplyError(Context, 500u, "session_result_unavailable",
            "The session was created but its metadata is unavailable", NULL);
    }
    MdoSessionRelease(Session);
    memset(&Error, 0, sizeof(Error));
    Session = MdoSessionLoad(Info.ProjectId, Info.Id, &Error);
    if ( Session == NULL )
        return MdoApiReplyError(Context, 500u, "session_result_unavailable",
            "The session was created but could not be reloaded", NULL);
    memset(&Info, 0, sizeof(Info)); Info.Size = sizeof(Info);
    if ( !MdoSessionGetInfo(Session, &Info) ) {
        MdoSessionRelease(Session);
        return MdoApiReplyError(Context, 500u, "session_result_unavailable",
            "The session was created but its metadata is unavailable", NULL);
    }
    MdoSessionRelease(Session);
    return MdoApiSessionReply(Context, 201u, &Info);
}

bool MdoApiSessionRoute(MdoApiContext* Context)
{
    char Project[MDO_PROJECT_ID_CAPACITY];
    char SessionId[MDO_SESSION_ID_CAPACITY];
    MdoSession* Session;
    MdoSessionInfo Info;
    xwork_error Error;

    if ( Context->ParamCount != 2u || Context->Params[0].Size == 0u ||
         Context->Params[0].Size >= sizeof(Project) ||
         Context->Params[1].Size == 0u ||
         Context->Params[1].Size >= sizeof(SessionId) ) {
        return MdoApiReplyError(Context, 400u, "invalid_session_path",
            "The project or session ID is invalid", NULL);
    }
    memcpy(Project, Context->Params[0].Data, Context->Params[0].Size);
    Project[Context->Params[0].Size] = '\0';
    memcpy(SessionId, Context->Params[1].Data, Context->Params[1].Size);
    SessionId[Context->Params[1].Size] = '\0';
    memset(&Error, 0, sizeof(Error));
    xrtClearError();
    Session = MdoSessionLoad(Project, SessionId, &Error);
    if ( Session == NULL ) {
        const xerror* Cause = xrtGetError();
        xerrkind Kind = Cause != NULL ? xrtErrorKind(Cause) : XERR_NONE;
        xrtClearError();
        if ( Error.eCode == XWORK_ERROR_INVALID_ARGUMENT )
            return MdoApiReplyError(Context, 400u, "invalid_session_path",
                "The project or session ID is invalid", NULL);
        if ( Kind == XERR_NOT_FOUND )
            return MdoApiReplyError(Context, 404u, "session_not_found",
                "The requested session does not exist", NULL);
        return MdoApiReplyError(Context, 500u, "session_read_failed",
            "The session metadata could not be read", NULL);
    }
    memset(&Info, 0, sizeof(Info)); Info.Size = sizeof(Info);
    if ( !MdoSessionGetInfo(Session, &Info) ) {
        MdoSessionRelease(Session);
        return MdoApiReplyError(Context, 500u, "session_result_unavailable",
            "The session metadata is unavailable", NULL);
    }
    MdoSessionRelease(Session);
    return MdoApiSessionReply(Context, 200u, &Info);
}
