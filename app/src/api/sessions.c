#include <stdio.h>
#include <string.h>

#include "internal.h"
#include "../../include/mdo/home.h"
#include "../../include/mdo/projects.h"
#include "../../include/mdo/sessions.h"

static xmutex* g_MdoApiSessionCreateLock;

bool MdoApiSessionsInit(void)
{
    if ( g_MdoApiSessionCreateLock != NULL ) return true;
    g_MdoApiSessionCreateLock = xrtMutexCreate();
    return g_MdoApiSessionCreateLock != NULL;
}

void MdoApiSessionsUnit(void)
{
    if ( g_MdoApiSessionCreateLock != NULL )
        xrtMutexDestroy(g_MdoApiSessionCreateLock);
    g_MdoApiSessionCreateLock = NULL;
}

static bool MdoApiClientSessionIdValid(const char* Id)
{
    size_t i;
    if ( strlen(Id) != 32u ) return false;
    for ( i = 0u; i < 32u; ++i )
        if ( !((Id[i] >= '0' && Id[i] <= '9') ||
               (Id[i] >= 'a' && Id[i] <= 'f')) ) return false;
    return true;
}

static bool MdoApiSessionReplayMatches(const MdoSessionInfo* Info,
    const MdoSessionCreateOptions* Options)
{
    char* Workspace = xrtPathAbs(Options->Agent.WorkspaceRoot != NULL &&
        Options->Agent.WorkspaceRoot[0] != '\0' ?
        Options->Agent.WorkspaceRoot : ".");
    bool Matches = Workspace != NULL &&
        strcmp(Info->ProjectId, Options->ProjectId) == 0 &&
        strcmp(Info->Id, Options->RequestedId) == 0 &&
        strcmp(Info->Title, Options->Title != NULL ? Options->Title :
            "New session") == 0 &&
        strcmp(Info->WorkspaceRoot, Workspace) == 0 &&
        (Options->Agent.AgentId == NULL ||
         strcmp(Info->AgentId, Options->Agent.AgentId) == 0) &&
        (Options->Agent.ModelId == NULL ||
         strcmp(Info->ModelId, Options->Agent.ModelId) == 0) &&
        (Options->Agent.ReasoningEffort == NULL ||
         strcmp(Info->ReasoningEffort,
            Options->Agent.ReasoningEffort) == 0) &&
        (Options->Agent.PermissionProfile == NULL ||
         strcmp(Info->PermissionProfile,
            Options->Agent.PermissionProfile) == 0) &&
        (Options->Agent.Protocol == 0 ||
         Info->Protocol == Options->Agent.Protocol) &&
        (Options->Agent.MaxOutputTokens == 0u ||
         Info->MaxOutputTokens == Options->Agent.MaxOutputTokens);
    xrtFree(Workspace);
    return Matches;
}

typedef enum MdoApiSessionPreconditionStatus {
    MDO_API_SESSION_PRECONDITION_OK = 0,
    MDO_API_SESSION_PRECONDITION_MISSING,
    MDO_API_SESSION_PRECONDITION_INVALID
} MdoApiSessionPreconditionStatus;

typedef enum MdoApiSessionPatchKind {
    MDO_API_SESSION_PATCH_NONE = 0,
    MDO_API_SESSION_PATCH_TITLE,
    MDO_API_SESSION_PATCH_PINNED,
    MDO_API_SESSION_PATCH_ARCHIVED
} MdoApiSessionPatchKind;

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

static bool MdoApiSessionUInt64(const xvalue* Object, cstr Name,
    uint64* Output, size_t* Present)
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
    *Output = Number;
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

static bool MdoApiSessionPath(const MdoApiContext* Context,
    char Project[MDO_PROJECT_ID_CAPACITY],
    char SessionId[MDO_SESSION_ID_CAPACITY])
{
    if ( Context->ParamCount != 2u || Context->Params[0].Size == 0u ||
         Context->Params[0].Size >= MDO_PROJECT_ID_CAPACITY ||
         Context->Params[1].Size == 0u ||
         Context->Params[1].Size >= MDO_SESSION_ID_CAPACITY ) return false;
    memcpy(Project, Context->Params[0].Data, Context->Params[0].Size);
    Project[Context->Params[0].Size] = '\0';
    memcpy(SessionId, Context->Params[1].Data, Context->Params[1].Size);
    SessionId[Context->Params[1].Size] = '\0';
    return true;
}

static bool MdoApiSessionNoBody(const MdoApiContext* Context)
{
    const xhttp1head* Head = Context->Request->head;
    return !(((Head->Flags & (uint32)XHTTP1_CONTENT_LENGTH) != 0u &&
              Head->ContentLength != 0u) ||
             (Head->Flags & (uint32)XHTTP1_TRANSFER_ENCODING) != 0u);
}

static MdoApiSessionPreconditionStatus MdoApiSessionExpectedRevision(
    const MdoApiContext* Context, const char* SessionId, uint64* Revision,
    bool* MatchesSession)
{
    static const char Prefix[] = "\"mdo-session-";
    const xhttpfield* Field = NULL;
    xhttpnext Next;
    xstrview Value;
    uint64 Number = 0u;
    size_t PrefixSize = sizeof(Prefix) - 1u;
    size_t Dash;
    size_t Index;
    size_t SessionSize = strlen(SessionId);

    *MatchesSession = false;
    Next = xrtHttpFieldGetUnique(Context->Request->head->Fields,
        Context->Request->head->FieldCount, XRT_STR_LITERAL("If-Match"),
        &Field);
    if ( Next == XHTTP_NEXT_END )
        return MDO_API_SESSION_PRECONDITION_MISSING;
    if ( Next != XHTTP_NEXT_ITEM || Field == NULL )
        return MDO_API_SESSION_PRECONDITION_INVALID;
    Value = xrtStrTrim(Field->Value);
    if ( Value.Size < PrefixSize + 4u ||
         memcmp(Value.Data, Prefix, PrefixSize) != 0 ||
         Value.Data[Value.Size - 1u] != '"' )
        return MDO_API_SESSION_PRECONDITION_INVALID;
    Dash = Value.Size - 2u;
    while ( Dash > PrefixSize && Value.Data[Dash] != '-' ) Dash--;
    if ( Dash == PrefixSize || Value.Data[Dash] != '-' )
        return MDO_API_SESSION_PRECONDITION_INVALID;
    for ( Index = PrefixSize; Index < Dash; Index++ ) {
        unsigned char Byte = (unsigned char)Value.Data[Index];
        size_t IdIndex = Index - PrefixSize;
        if ( (Byte >= 'a' && Byte <= 'z') ||
             (Byte >= 'A' && Byte <= 'Z') ||
             (Byte >= '0' && Byte <= '9') || Byte == '-' || Byte == '_' ||
             (Byte == '.' && IdIndex != 0u) ) continue;
        return MDO_API_SESSION_PRECONDITION_INVALID;
    }
    if ( Dash + 1u >= Value.Size - 1u )
        return MDO_API_SESSION_PRECONDITION_INVALID;
    for ( Index = Dash + 1u; Index + 1u < Value.Size; Index++ ) {
        uint64 Digit;
        if ( Value.Data[Index] < '0' || Value.Data[Index] > '9' )
            return MDO_API_SESSION_PRECONDITION_INVALID;
        Digit = (uint64)(Value.Data[Index] - '0');
        if ( Number > (UINT64_MAX - Digit) / 10u )
            return MDO_API_SESSION_PRECONDITION_INVALID;
        Number = Number * 10u + Digit;
    }
    if ( Number == 0u ) return MDO_API_SESSION_PRECONDITION_INVALID;
    *MatchesSession = Dash - PrefixSize == SessionSize &&
        memcmp(Value.Data + PrefixSize, SessionId, SessionSize) == 0;
    *Revision = Number;
    return MDO_API_SESSION_PRECONDITION_OK;
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
        MdoApiValueSetString(Data, "permission_profile",
            Info->PermissionProfile) &&
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

static bool MdoApiSessionActiveFailure(MdoApiContext* Context,
    const xwork_error* Error)
{
    const xerror* Cause = xrtGetError();
    xerrkind Kind = Cause != NULL ? xrtErrorKind(Cause) : XERR_NONE;
    xrtClearError();
    if ( Error != NULL && Error->eCode == XWORK_ERROR_INVALID_ARGUMENT )
        return MdoApiReplyError(Context, 400u, "invalid_session_path",
            "The project or session ID is invalid", NULL);
    if ( Kind == XERR_NOT_FOUND )
        return MdoApiReplyError(Context, 404u, "session_not_found",
            "The requested session does not exist", NULL);
    if ( Error != NULL &&
         (Error->eCode == XWORK_ERROR_CONTEXT ||
          Error->eCode == XWORK_ERROR_POLICY ||
          Error->eCode == XWORK_ERROR_LIMIT) )
        return MdoApiReplyError(Context, 409u, "session_state_conflict",
            "The session must be active and idle for this operation", NULL);
    if ( Error != NULL && Error->eCode == XWORK_ERROR_MODEL &&
         Error->tModelError.eCode != XLLM_ERROR_AUTH &&
         Error->tModelError.eCode != XLLM_ERROR_OUT_OF_MEMORY )
        return MdoApiReplyError(Context, 422u, "session_profile_invalid",
            "The session model profile is invalid", NULL);
    if ( Error != NULL &&
         (Error->eCode == XWORK_ERROR_OUT_OF_MEMORY ||
          Error->eCode == XWORK_ERROR_MODEL) )
        return MdoApiReplyError(Context, 503u,
            "session_service_unavailable",
            "The session service is not ready for this operation", NULL);
    return MdoApiReplyError(Context, 500u, "session_operation_failed",
        "The session operation could not be completed", NULL);
}

static MdoSession* MdoApiSessionOpenActive(MdoApiContext* Context,
    bool RequireRevision, MdoSessionInfo* Info, uint64* ExpectedRevision,
    bool* ReplyResult)
{
    char Project[MDO_PROJECT_ID_CAPACITY];
    char SessionId[MDO_SESSION_ID_CAPACITY];
    MdoApiSessionPreconditionStatus Precondition;
    MdoSession* Session;
    xwork_error Error;
    bool MatchesSession = false;

    *ReplyResult = false;
    *ExpectedRevision = 0u;
    if ( !MdoApiSessionPath(Context, Project, SessionId) ) {
        *ReplyResult = MdoApiReplyError(Context, 400u,
            "invalid_session_path", "The project or session ID is invalid",
            NULL);
        return NULL;
    }
    if ( RequireRevision ) {
        Precondition = MdoApiSessionExpectedRevision(Context, SessionId,
            ExpectedRevision, &MatchesSession);
        if ( Precondition == MDO_API_SESSION_PRECONDITION_MISSING ) {
            *ReplyResult = MdoApiReplyError(Context, 428u,
                "precondition_required",
                "If-Match must contain the current session ETag", NULL);
            return NULL;
        }
        if ( Precondition != MDO_API_SESSION_PRECONDITION_OK ) {
            *ReplyResult = MdoApiReplyError(Context, 400u,
                "invalid_precondition",
                "If-Match must use the form \"mdo-session-ID-N\"", NULL);
            return NULL;
        }
    }
    memset(&Error, 0, sizeof(Error));
    xrtClearError();
    Session = MdoSessionOpen(Project, SessionId, NULL, &Error);
    if ( Session == NULL ) {
        *ReplyResult = MdoApiSessionActiveFailure(Context, &Error);
        return NULL;
    }
    memset(Info, 0, sizeof(*Info)); Info->Size = sizeof(*Info);
    if ( !MdoSessionGetInfo(Session, Info) ) {
        MdoSessionRelease(Session);
        *ReplyResult = MdoApiReplyError(Context, 500u,
            "session_result_unavailable",
            "The session metadata is unavailable", NULL);
        return NULL;
    }
    if ( RequireRevision &&
         (!MatchesSession || Info->Revision != *ExpectedRevision) ) {
        MdoSessionRelease(Session);
        *ReplyResult = MdoApiReplyError(Context, 412u,
            "revision_conflict",
            "The session changed; reload it before updating", NULL);
        return NULL;
    }
    return Session;
}

static bool MdoApiSessionReplyData(MdoApiContext* Context, xvalue* Data,
    const MdoSessionInfo* Info)
{
    char EntityTag[96];
    int Count = snprintf(EntityTag, sizeof(EntityTag),
        "\"mdo-session-%s-%llu\"", Info->Id,
        (unsigned long long)Info->Revision);
    if ( Count <= 0 || (size_t)Count >= sizeof(EntityTag) ) {
        xrtValueRelease(Data);
        return MdoApiReplyError(Context, 500u,
            "session_result_unavailable",
            "The session result could not be created", NULL);
    }
    return MdoApiReplySuccessTakeEntityTag(Context, 200u, Data, EntityTag);
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

static bool MdoApiSessionLoadFailure(MdoApiContext* Context,
    const xwork_error* Error)
{
    const xerror* Cause = xrtGetError();
    xerrkind Kind = Cause != NULL ? xrtErrorKind(Cause) : XERR_NONE;
    xrtClearError();
    if ( Error != NULL && Error->eCode == XWORK_ERROR_INVALID_ARGUMENT )
        return MdoApiReplyError(Context, 400u, "invalid_session_path",
            "The project or session ID is invalid", NULL);
    if ( Kind == XERR_NOT_FOUND )
        return MdoApiReplyError(Context, 404u, "session_not_found",
            "The requested session does not exist", NULL);
    return MdoApiReplyError(Context, 500u, "session_read_failed",
        "The session metadata could not be read", NULL);
}

static bool MdoApiSessionMutationFailure(MdoApiContext* Context,
    const char* Project, const char* SessionId, uint64 ExpectedRevision,
    const xwork_error* Error)
{
    MdoSession* Fresh;
    MdoSessionInfo Info;
    xwork_error LoadError;
    bool Stale = false;

    memset(&LoadError, 0, sizeof(LoadError));
    xrtClearError();
    Fresh = MdoSessionLoad(Project, SessionId, &LoadError);
    if ( Fresh != NULL ) {
        memset(&Info, 0, sizeof(Info)); Info.Size = sizeof(Info);
        Stale = MdoSessionGetInfo(Fresh, &Info) &&
            Info.Revision != ExpectedRevision;
        MdoSessionRelease(Fresh);
    }
    xrtClearError();
    if ( Stale )
        return MdoApiReplyError(Context, 412u, "revision_conflict",
            "The session changed; reload it before updating", NULL);
    if ( Error != NULL && Error->eCode == XWORK_ERROR_INVALID_ARGUMENT )
        return MdoApiReplyError(Context, 422u, "session_update_invalid",
            "The session update is invalid", NULL);
    if ( Error != NULL &&
         (Error->eCode == XWORK_ERROR_CONTEXT ||
          Error->eCode == XWORK_ERROR_POLICY ||
          Error->eCode == XWORK_ERROR_LIMIT) )
        return MdoApiReplyError(Context, 409u, "session_state_conflict",
            "The session state does not allow this update", NULL);
    if ( Error != NULL && Error->eCode == XWORK_ERROR_OUT_OF_MEMORY )
        return MdoApiReplyError(Context, 503u, "session_service_unavailable",
            "The session service could not complete this update", NULL);
    if ( Error != NULL && Error->eCode == XWORK_ERROR_IO )
        return MdoApiReplyError(Context, 500u,
            "session_persistence_failed",
            "The session update could not be persisted", NULL);
    return MdoApiReplyError(Context, 500u, "session_update_failed",
        "The session could not be updated", NULL);
}

bool MdoApiSessionProfileRoute(MdoApiContext* Context)
{
    char Project[MDO_PROJECT_ID_CAPACITY];
    char SessionId[MDO_SESSION_ID_CAPACITY];
    char Model[MDO_SESSION_IDENTITY_CAPACITY] = { 0 };
    char Reasoning[MDO_SESSION_REASONING_CAPACITY] = { 0 };
    char Permission[MDO_SESSION_REASONING_CAPACITY] = { 0 };
    MdoApiSessionPreconditionStatus Precondition;
    MdoApiJsonBody Body;
    MdoApiBodyStatus BodyStatus;
    MdoSession* Session;
    MdoSessionInfo Info;
    xwork_error Error;
    uint64 ExpectedRevision = 0u;
    size_t Present = 0u;
    bool MatchesSession = false;
    bool Valid;

    if ( !MdoApiSessionPath(Context, Project, SessionId) )
        return MdoApiReplyError(Context, 400u, "invalid_session_path",
            "The project or session ID is invalid", NULL);
    Precondition = MdoApiSessionExpectedRevision(Context, SessionId,
        &ExpectedRevision, &MatchesSession);
    if ( Precondition == MDO_API_SESSION_PRECONDITION_MISSING )
        return MdoApiReplyError(Context, 428u, "precondition_required",
            "If-Match must contain the current session ETag", NULL);
    if ( Precondition != MDO_API_SESSION_PRECONDITION_OK )
        return MdoApiReplyError(Context, 400u, "invalid_precondition",
            "If-Match must use the form \"mdo-session-ID-N\"", NULL);
    BodyStatus = MdoApiJsonBodyRead(Context, &Body);
    if ( BodyStatus != MDO_API_BODY_OK )
        return MdoApiReplyBodyError(Context, BodyStatus);
    Valid = xrtValueType(Body.Value) == XVALUE_OBJECT &&
        MdoApiSessionString(Body.Value, "model_id", Model,
            sizeof(Model), true, &Present) &&
        MdoApiSessionString(Body.Value, "reasoning_effort", Reasoning,
            sizeof(Reasoning), true, &Present) &&
        MdoApiSessionString(Body.Value, "permission_profile", Permission,
            sizeof(Permission), true, &Present) &&
        Present == xrtValueCount(Body.Value);
    MdoApiJsonBodyUnit(&Body);
    if ( !Valid )
        return MdoApiReplyError(Context, 422u, "session_profile_invalid",
            "The session profile document is invalid", NULL);
    memset(&Error, 0, sizeof(Error));
    xrtClearError();
    Session = MdoSessionLoad(Project, SessionId, &Error);
    if ( Session == NULL ) return MdoApiSessionLoadFailure(Context, &Error);
    memset(&Info, 0, sizeof(Info)); Info.Size = sizeof(Info);
    if ( !MdoSessionGetInfo(Session, &Info) ) {
        MdoSessionRelease(Session);
        return MdoApiReplyError(Context, 500u, "session_result_unavailable",
            "The session metadata is unavailable", NULL);
    }
    if ( !MatchesSession || Info.Revision != ExpectedRevision ) {
        MdoSessionRelease(Session);
        return MdoApiReplyError(Context, 412u, "revision_conflict",
            "The session changed; reload it before updating", NULL);
    }
    if ( strcmp(Info.ModelId, Model) == 0 &&
         strcmp(Info.ReasoningEffort, Reasoning) == 0 &&
         strcmp(Info.PermissionProfile, Permission) == 0 ) {
        MdoSessionRelease(Session);
        return MdoApiSessionReply(Context, 200u, &Info);
    }
    if ( !MdoSessionSetProfile(Session, Model, Reasoning, Permission,
            NULL, &Error) ) {
        MdoSessionRelease(Session);
        if ( Error.eCode == XWORK_ERROR_MODEL ||
             Error.eCode == XWORK_ERROR_INVALID_ARGUMENT )
            return MdoApiReplyError(Context, 422u,
                "session_profile_invalid",
                "The requested session profile is invalid", NULL);
        return MdoApiSessionMutationFailure(Context, Project, SessionId,
            ExpectedRevision, &Error);
    }
    memset(&Info, 0, sizeof(Info)); Info.Size = sizeof(Info);
    Valid = MdoSessionGetInfo(Session, &Info);
    MdoSessionRelease(Session);
    if ( !Valid )
        return MdoApiReplyError(Context, 500u, "session_result_unavailable",
            "The session was updated but its metadata is unavailable", NULL);
    return MdoApiSessionReply(Context, 200u, &Info);
}

bool MdoApiSessionCreateRoute(MdoApiContext* Context)
{
    MdoApiJsonBody Body;
    MdoApiBodyStatus BodyStatus;
    MdoSessionCreateOptions Options;
    MdoSessionInfo Info;
    MdoProjectInfo ProjectInfo;
    MdoSession* Session;
    xwork_error Error;
    char Project[MDO_PROJECT_ID_CAPACITY] = { 0 };
    char Title[MDO_SESSION_TITLE_CAPACITY] = { 0 };
    char Agent[MDO_SESSION_IDENTITY_CAPACITY] = { 0 };
    char Model[MDO_SESSION_IDENTITY_CAPACITY] = { 0 };
    char Protocol[40] = { 0 };
    char Reasoning[MDO_SESSION_REASONING_CAPACITY] = { 0 };
    char Permission[MDO_SESSION_REASONING_CAPACITY] = { 0 };
    char Workspace[MDO_SESSION_WORKSPACE_CAPACITY] = { 0 };
    char ClientSessionId[MDO_SESSION_ID_CAPACITY] = { 0 };
    char SessionDirectory[MDO_SESSION_PATH_CAPACITY];
    str ProjectWorkspace = NULL;
    size_t Present = 0u;
    bool Valid;
    bool ProjectFound = false;
    bool DirectoryExists = false;
    bool Replayed = false;

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
        MdoApiSessionString(Body.Value, "permission_profile", Permission,
            sizeof(Permission), false, &Present) &&
        MdoApiSessionString(Body.Value, "workspace_root", Workspace,
            sizeof(Workspace), false, &Present) &&
        MdoApiSessionString(Body.Value, "client_session_id",
            ClientSessionId, sizeof(ClientSessionId), false, &Present) &&
        MdoApiSessionUnsigned(Body.Value, "max_output_tokens",
            &Options.Agent.MaxOutputTokens, &Present) &&
        Present == xrtValueCount(Body.Value) &&
        MdoApiSessionProtocol(Protocol, &Options.Agent.Protocol) &&
        (ClientSessionId[0] == '\0' ||
         MdoApiClientSessionIdValid(ClientSessionId));
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
    Options.Agent.PermissionProfile = Permission[0] != '\0' ?
        Permission : NULL;
    Options.Agent.WorkspaceRoot = Workspace[0] != '\0' ? Workspace : NULL;
    Options.RequestedId = ClientSessionId[0] != '\0' ?
        ClientSessionId : NULL;
    memset(&Error, 0, sizeof(Error));
    memset(&ProjectInfo, 0, sizeof(ProjectInfo));
    ProjectInfo.Size = sizeof(ProjectInfo);
    if ( !MdoProjectGet(Project, &ProjectInfo, &ProjectFound, &Error) ) {
        MdoApiJsonBodyUnit(&Body);
        if ( Error.eCode == XWORK_ERROR_INVALID_ARGUMENT )
            return MdoApiReplyError(Context, 422u,
                "session_create_invalid",
                "The session create document is invalid", NULL);
        return MdoApiReplyError(Context, 503u, "project_unavailable",
            "The project definition is invalid or unavailable", NULL);
    }
    if ( ProjectFound ) {
        if ( Options.Agent.WorkspaceRoot == NULL ) {
            if ( xrtPathIsAbs(ProjectInfo.WorkspaceRoot) )
                Options.Agent.WorkspaceRoot = ProjectInfo.WorkspaceRoot;
            else {
                ProjectWorkspace = xrtPathJoin(xsAppPath(),
                    ProjectInfo.WorkspaceRoot);
                if ( ProjectWorkspace == NULL ) {
                    MdoApiJsonBodyUnit(&Body);
                    return MdoApiReplyError(Context, 500u,
                        "project_unavailable",
                        "The project workspace could not be resolved", NULL);
                }
                Options.Agent.WorkspaceRoot = ProjectWorkspace;
            }
        }
        if ( Options.Agent.ModelId == NULL && ProjectInfo.DefaultModelId[0] != '\0' )
            Options.Agent.ModelId = ProjectInfo.DefaultModelId;
    }
    if ( Options.RequestedId != NULL ) {
        int Written = snprintf(SessionDirectory,
            sizeof(SessionDirectory), "sessions/%s/%s", Project,
            ClientSessionId);
        if ( Written <= 0 || (size_t)Written >= sizeof(SessionDirectory) ||
             g_MdoApiSessionCreateLock == NULL ||
             !xrtMutexLock(g_MdoApiSessionCreateLock) ) {
            xrtFree(ProjectWorkspace);
            MdoApiJsonBodyUnit(&Body);
            return MdoApiReplyError(Context, 503u,
                "session_service_unavailable",
                "The session create lock is unavailable", NULL);
        }
        if ( !MdoHomeExternalStat(SessionDirectory,
                &DirectoryExists, NULL) ) {
            xrtMutexUnlock(g_MdoApiSessionCreateLock);
            xrtFree(ProjectWorkspace);
            MdoApiJsonBodyUnit(&Body);
            return MdoApiReplyError(Context, 503u,
                "session_service_unavailable",
                "The requested session could not be inspected", NULL);
        }
        if ( DirectoryExists ) {
            Session = MdoSessionLoad(Project, ClientSessionId, &Error);
            if ( Session == NULL ) {
                xrtMutexUnlock(g_MdoApiSessionCreateLock);
                xrtFree(ProjectWorkspace);
                MdoApiJsonBodyUnit(&Body);
                return MdoApiReplyError(Context, 409u,
                    "session_create_incomplete",
                    "The requested session directory needs inspection", NULL);
            }
            memset(&Info, 0, sizeof(Info)); Info.Size = sizeof(Info);
            Replayed = MdoSessionGetInfo(Session, &Info) &&
                MdoApiSessionReplayMatches(&Info, &Options);
            if ( !Replayed ) {
                MdoSessionRelease(Session);
                xrtMutexUnlock(g_MdoApiSessionCreateLock);
                xrtFree(ProjectWorkspace);
                MdoApiJsonBodyUnit(&Body);
                return MdoApiReplyError(Context, 409u,
                    "session_create_conflict",
                    "The requested session ID belongs to another profile",
                    NULL);
            }
        } else Session = MdoSessionCreate(&Options, &Error);
        xrtMutexUnlock(g_MdoApiSessionCreateLock);
    } else Session = MdoSessionCreate(&Options, &Error);
    xrtFree(ProjectWorkspace);
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
    return MdoApiSessionReply(Context, Replayed ? 200u : 201u, &Info);
}

static bool MdoApiSessionPatch(MdoApiContext* Context, MdoSession* Session,
    xwork_error* Error, bool* Attempted)
{
    MdoApiJsonBody Body;
    MdoApiBodyStatus BodyStatus;
    const xvalue* TitleValue;
    const xvalue* PinnedValue;
    const xvalue* ArchivedValue;
    MdoApiSessionPatchKind Kind = MDO_API_SESSION_PATCH_NONE;
    char Title[MDO_SESSION_TITLE_CAPACITY];
    bool BooleanValue = false;
    xstrview Text;
    bool Valid = false;
    bool Ok;

    *Attempted = false;
    BodyStatus = MdoApiJsonBodyRead(Context, &Body);
    if ( BodyStatus != MDO_API_BODY_OK )
        return MdoApiReplyBodyError(Context, BodyStatus);
    TitleValue = xrtValueType(Body.Value) == XVALUE_OBJECT ?
        xrtValueObjectGet(Body.Value, XRT_STR_LITERAL("title")) : NULL;
    PinnedValue = xrtValueType(Body.Value) == XVALUE_OBJECT ?
        xrtValueObjectGet(Body.Value, XRT_STR_LITERAL("pinned")) : NULL;
    ArchivedValue = xrtValueType(Body.Value) == XVALUE_OBJECT ?
        xrtValueObjectGet(Body.Value, XRT_STR_LITERAL("archived")) : NULL;
    if ( xrtValueType(Body.Value) == XVALUE_OBJECT &&
         xrtValueCount(Body.Value) == 1u && TitleValue != NULL &&
         xrtValueType(TitleValue) == XVALUE_STRING &&
         xrtValueGetString(TitleValue, &Text) &&
         Text.Size < sizeof(Title) &&
         memchr(Text.Data, 0, Text.Size) == NULL &&
         xrtUtf8Valid(Text, NULL) ) {
        if ( Text.Size != 0u ) memcpy(Title, Text.Data, Text.Size);
        Title[Text.Size] = '\0';
        Kind = MDO_API_SESSION_PATCH_TITLE;
        Valid = true;
    } else if ( xrtValueType(Body.Value) == XVALUE_OBJECT &&
                xrtValueCount(Body.Value) == 1u && PinnedValue != NULL &&
                xrtValueType(PinnedValue) == XVALUE_BOOL &&
                xrtValueGetBool(PinnedValue, &BooleanValue) ) {
        Kind = MDO_API_SESSION_PATCH_PINNED;
        Valid = true;
    } else if ( xrtValueType(Body.Value) == XVALUE_OBJECT &&
                xrtValueCount(Body.Value) == 1u && ArchivedValue != NULL &&
                xrtValueType(ArchivedValue) == XVALUE_BOOL &&
                xrtValueGetBool(ArchivedValue, &BooleanValue) ) {
        Kind = MDO_API_SESSION_PATCH_ARCHIVED;
        Valid = true;
    }
    MdoApiJsonBodyUnit(&Body);
    if ( !Valid )
        return MdoApiReplyError(Context, 422u, "session_patch_invalid",
            "PATCH must contain exactly one valid title, pinned, or archived field",
            NULL);
    *Attempted = true;
    if ( Kind == MDO_API_SESSION_PATCH_TITLE )
        Ok = MdoSessionRename(Session, Title, Error);
    else if ( Kind == MDO_API_SESSION_PATCH_PINNED )
        Ok = MdoSessionSetPinned(Session, BooleanValue, Error);
    else Ok = MdoSessionSetArchived(Session, BooleanValue, Error);
    return Ok;
}

static bool MdoApiSessionMutate(MdoApiContext* Context, bool Restore)
{
    char Project[MDO_PROJECT_ID_CAPACITY];
    char SessionId[MDO_SESSION_ID_CAPACITY];
    MdoApiSessionPreconditionStatus Precondition;
    MdoSession* Session;
    MdoSessionInfo Info;
    xwork_error Error;
    uint64 ExpectedRevision = 0u;
    bool MatchesSession = false;
    bool Attempted = true;
    bool Ok;

    if ( !MdoApiSessionPath(Context, Project, SessionId) )
        return MdoApiReplyError(Context, 400u, "invalid_session_path",
            "The project or session ID is invalid", NULL);
    Precondition = MdoApiSessionExpectedRevision(Context, SessionId,
        &ExpectedRevision, &MatchesSession);
    if ( Precondition == MDO_API_SESSION_PRECONDITION_MISSING )
        return MdoApiReplyError(Context, 428u, "precondition_required",
            "If-Match must contain the current session ETag", NULL);
    if ( Precondition != MDO_API_SESSION_PRECONDITION_OK )
        return MdoApiReplyError(Context, 400u, "invalid_precondition",
            "If-Match must use the form \"mdo-session-ID-N\"", NULL);
    if ( (Restore || Context->Request->head->MethodCode ==
            XHTTP_METHOD_DELETE) && !MdoApiSessionNoBody(Context) ) {
        return MdoApiReplyError(Context, 400u, "body_not_allowed",
            "This session operation does not accept a body", NULL);
    }
    memset(&Error, 0, sizeof(Error));
    xrtClearError();
    Session = MdoSessionLoad(Project, SessionId, &Error);
    if ( Session == NULL ) return MdoApiSessionLoadFailure(Context, &Error);
    memset(&Info, 0, sizeof(Info)); Info.Size = sizeof(Info);
    if ( !MdoSessionGetInfo(Session, &Info) ) {
        MdoSessionRelease(Session);
        return MdoApiReplyError(Context, 500u, "session_result_unavailable",
            "The session metadata is unavailable", NULL);
    }
    if ( !MatchesSession || Info.Revision != ExpectedRevision ) {
        MdoSessionRelease(Session);
        return MdoApiReplyError(Context, 412u, "revision_conflict",
            "The session changed; reload it before updating", NULL);
    }
    memset(&Error, 0, sizeof(Error));
    if ( Restore ) Ok = MdoSessionRestore(Session, &Error);
    else if ( Context->Request->head->MethodCode == XHTTP_METHOD_DELETE )
        Ok = MdoSessionMoveToTrash(Session, &Error);
    else Ok = MdoApiSessionPatch(Context, Session, &Error, &Attempted);
    if ( !Attempted ) {
        MdoSessionRelease(Session);
        return Ok;
    }
    if ( !Ok ) {
        MdoSessionRelease(Session);
        return MdoApiSessionMutationFailure(Context, Project, SessionId,
            ExpectedRevision, &Error);
    }
    memset(&Info, 0, sizeof(Info)); Info.Size = sizeof(Info);
    if ( !MdoSessionGetInfo(Session, &Info) ) {
        MdoSessionRelease(Session);
        return MdoApiReplyError(Context, 500u, "session_result_unavailable",
            "The session was updated but its metadata is unavailable", NULL);
    }
    MdoSessionRelease(Session);
    return MdoApiSessionReply(Context, 200u, &Info);
}

bool MdoApiSessionHistoryRoute(MdoApiContext* Context)
{
    MdoSessionInfo Info;
    MdoSession* Session;
    xwork_error Error;
    xvalue* Data;
    uint64 ExpectedRevision;
    uint64 LastSequence = 0u;
    bool ReplyResult;

    Session = MdoApiSessionOpenActive(Context, false, &Info,
        &ExpectedRevision, &ReplyResult);
    if ( Session == NULL ) return ReplyResult;
    memset(&Error, 0, sizeof(Error));
    if ( !MdoSessionLastSequence(Session, &LastSequence, &Error) ) {
        MdoSessionRelease(Session);
        return MdoApiSessionActiveFailure(Context, &Error);
    }
    Data = xrtValueObject();
    if ( Data == NULL ||
         !MdoApiValueSetString(Data, "project_id", Info.ProjectId) ||
         !MdoApiValueSetString(Data, "session_id", Info.Id) ||
         !MdoApiValueSetUInt(Data, "revision", Info.Revision) ||
         !MdoApiValueSetUInt(Data, "last_sequence", LastSequence) ) {
        xrtValueRelease(Data);
        MdoSessionRelease(Session);
        return MdoApiReplyError(Context, 500u,
            "session_history_unavailable",
            "The session history boundary could not be read", NULL);
    }
    MdoSessionRelease(Session);
    return MdoApiSessionReplyData(Context, Data, &Info);
}

bool MdoApiSessionExportRoute(MdoApiContext* Context)
{
    MdoSessionInfo Info;
    MdoSession* Session;
    xwork_error Error;
    str Document;
    char EntityTag[96];
    char Disposition[128];
    size_t DocumentSize = 0u;
    int EntityTagSize;
    int DispositionSize;
    uint64 ExpectedRevision;
    bool ReplyResult;
    bool Result;

    Session = MdoApiSessionOpenActive(Context, false, &Info,
        &ExpectedRevision, &ReplyResult);
    if ( Session == NULL ) return ReplyResult;
    memset(&Error, 0, sizeof(Error));
    Document = MdoSessionExportJson(Session, &DocumentSize, &Error);
    MdoSessionRelease(Session);
    if ( Document == NULL ) return MdoApiSessionActiveFailure(Context, &Error);
    EntityTagSize = snprintf(EntityTag, sizeof(EntityTag),
        "\"mdo-session-%s-%llu\"", Info.Id,
        (unsigned long long)Info.Revision);
    DispositionSize = snprintf(Disposition, sizeof(Disposition),
        "attachment; filename=\"mdo-session-%s.json\"", Info.Id);
    if ( EntityTagSize <= 0 || (size_t)EntityTagSize >= sizeof(EntityTag) ||
         DispositionSize <= 0 ||
         (size_t)DispositionSize >= sizeof(Disposition) ||
         DocumentSize > MDO_API_DOWNLOAD_MAX_BYTES ) {
        xrtFree(Document);
        return MdoApiReplyError(Context, 500u,
            "session_export_unavailable",
            "The session export exceeds the bounded download limit", NULL);
    }
    Result = MdoApiReplyDownload(Context, Document, DocumentSize,
        Disposition, EntityTag);
    xrtFree(Document);
    return Result;
}

static bool MdoApiSessionLedgerMutation(MdoApiContext* Context,
    bool Clear)
{
    MdoApiJsonBody Body;
    MdoApiBodyStatus BodyStatus;
    MdoSessionInfo Info;
    MdoSession* Session;
    xwork_error Error;
    uint64 ExpectedRevision;
    uint64 ThroughSequence = 0u;
    size_t Present = 0u;
    bool ReplyResult;
    bool Ok;

    Session = MdoApiSessionOpenActive(Context, true, &Info,
        &ExpectedRevision, &ReplyResult);
    if ( Session == NULL ) return ReplyResult;
    if ( Clear ) {
        if ( !MdoApiSessionNoBody(Context) ) {
            MdoSessionRelease(Session);
            return MdoApiReplyError(Context, 400u, "body_not_allowed",
                "This session operation does not accept a body", NULL);
        }
    } else {
        BodyStatus = MdoApiJsonBodyRead(Context, &Body);
        if ( BodyStatus != MDO_API_BODY_OK ) {
            MdoSessionRelease(Session);
            return MdoApiReplyBodyError(Context, BodyStatus);
        }
        Ok = xrtValueType(Body.Value) == XVALUE_OBJECT &&
            MdoApiSessionUInt64(Body.Value, "through_sequence",
                &ThroughSequence, &Present) && Present == 1u &&
            Present == xrtValueCount(Body.Value);
        MdoApiJsonBodyUnit(&Body);
        if ( !Ok ) {
            MdoSessionRelease(Session);
            return MdoApiReplyError(Context, 422u,
                "session_truncate_invalid",
                "Truncate requires exactly one non-negative through_sequence",
                NULL);
        }
    }
    memset(&Error, 0, sizeof(Error));
    Ok = Clear ? MdoSessionClear(Session, &Error) :
        MdoSessionTruncateAfter(Session, ThroughSequence, &Error);
    if ( !Ok ) {
        MdoSessionRelease(Session);
        return MdoApiSessionMutationFailure(Context, Info.ProjectId,
            Info.Id, ExpectedRevision, &Error);
    }
    memset(&Info, 0, sizeof(Info)); Info.Size = sizeof(Info);
    if ( !MdoSessionGetInfo(Session, &Info) ) {
        MdoSessionRelease(Session);
        return MdoApiReplyError(Context, 500u,
            "session_result_unavailable",
            "The session was updated but its metadata is unavailable", NULL);
    }
    if ( !MdoApiFeedbackReconcile(Info.ProjectId, Info.Id) ) {
        MdoSessionRelease(Session);
        return MdoApiReplyError(Context, 503u,
            "session_feedback_unavailable",
            "The session was updated but its feedback could not be reconciled",
            NULL);
    }
    MdoSessionRelease(Session);
    return MdoApiSessionReply(Context, 200u, &Info);
}

bool MdoApiSessionClearRoute(MdoApiContext* Context)
{
    return MdoApiSessionLedgerMutation(Context, true);
}

bool MdoApiSessionTruncateRoute(MdoApiContext* Context)
{
    return MdoApiSessionLedgerMutation(Context, false);
}

bool MdoApiSessionForkRoute(MdoApiContext* Context)
{
    MdoApiJsonBody Body;
    MdoApiBodyStatus BodyStatus;
    MdoSessionForkOptions Options;
    MdoSessionInfo SourceInfo;
    MdoSessionInfo ForkInfo;
    MdoSession* Source;
    MdoSession* Fork;
    xwork_error Error;
    char Title[MDO_SESSION_TITLE_CAPACITY] = { 0 };
    uint64 ExpectedRevision;
    size_t Present = 0u;
    bool ReplyResult;
    bool Valid;

    Source = MdoApiSessionOpenActive(Context, true, &SourceInfo,
        &ExpectedRevision, &ReplyResult);
    if ( Source == NULL ) return ReplyResult;
    BodyStatus = MdoApiJsonBodyRead(Context, &Body);
    if ( BodyStatus != MDO_API_BODY_OK ) {
        MdoSessionRelease(Source);
        return MdoApiReplyBodyError(Context, BodyStatus);
    }
    MdoSessionForkOptionsInit(&Options);
    Valid = xrtValueType(Body.Value) == XVALUE_OBJECT &&
        MdoApiSessionString(Body.Value, "title", Title,
            sizeof(Title), false, &Present) &&
        MdoApiSessionUInt64(Body.Value, "through_sequence",
            &Options.ThroughSequence, &Present) &&
        Present == xrtValueCount(Body.Value);
    MdoApiJsonBodyUnit(&Body);
    if ( !Valid ) {
        MdoSessionRelease(Source);
        return MdoApiReplyError(Context, 422u, "session_fork_invalid",
            "Fork accepts only a valid title and through_sequence", NULL);
    }
    Options.Title = Title[0] != '\0' ? Title : NULL;
    memset(&Error, 0, sizeof(Error));
    Fork = MdoSessionFork(Source, &Options, &Error);
    MdoSessionRelease(Source);
    if ( Fork == NULL )
        return MdoApiSessionMutationFailure(Context, SourceInfo.ProjectId,
            SourceInfo.Id, ExpectedRevision, &Error);
    memset(&ForkInfo, 0, sizeof(ForkInfo)); ForkInfo.Size = sizeof(ForkInfo);
    if ( !MdoSessionGetInfo(Fork, &ForkInfo) ) {
        MdoSessionRelease(Fork);
        return MdoApiReplyError(Context, 500u,
            "session_result_unavailable",
            "The fork was created but its metadata is unavailable", NULL);
    }
    MdoSessionRelease(Fork);
    return MdoApiSessionReply(Context, 201u, &ForkInfo);
}

bool MdoApiSessionRestoreRoute(MdoApiContext* Context)
{
    return MdoApiSessionMutate(Context, true);
}

bool MdoApiSessionRoute(MdoApiContext* Context)
{
    char Project[MDO_PROJECT_ID_CAPACITY];
    char SessionId[MDO_SESSION_ID_CAPACITY];
    MdoSession* Session;
    MdoSessionInfo Info;
    xwork_error Error;

    if ( Context->Request->head->MethodCode == XHTTP_METHOD_PATCH ||
         Context->Request->head->MethodCode == XHTTP_METHOD_DELETE )
        return MdoApiSessionMutate(Context, false);
    if ( !MdoApiSessionPath(Context, Project, SessionId) ) {
        return MdoApiReplyError(Context, 400u, "invalid_session_path",
            "The project or session ID is invalid", NULL);
    }
    memset(&Error, 0, sizeof(Error));
    xrtClearError();
    Session = MdoSessionLoad(Project, SessionId, &Error);
    if ( Session == NULL ) return MdoApiSessionLoadFailure(Context, &Error);
    memset(&Info, 0, sizeof(Info)); Info.Size = sizeof(Info);
    if ( !MdoSessionGetInfo(Session, &Info) ) {
        MdoSessionRelease(Session);
        return MdoApiReplyError(Context, 500u, "session_result_unavailable",
            "The session metadata is unavailable", NULL);
    }
    MdoSessionRelease(Session);
    return MdoApiSessionReply(Context, 200u, &Info);
}
