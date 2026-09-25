#include <string.h>

#include "internal.h"
#include "../../include/mdo/bootstrap.h"
#include "../../include/mdo/config.h"
#include "../../include/mdo/sessions.h"
#include "../../include/mdo/settings.h"

#define MDO_API_LIST_LIMIT 100u
#define MDO_API_ARTIFACT_DEFAULT_BYTES (64u * 1024u)
#define MDO_API_ARTIFACT_MAX_BYTES (64u * 1024u)

static bool MdoApiStateReply(MdoApiContext* Context, xvalue* Data)
{
    if ( Data == NULL ) {
        return MdoApiReplyError(Context, 500u, "state_unavailable",
            "The requested state could not be created", NULL);
    }
    return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
}

static cstr MdoApiSessionStatusText(MdoSessionStatus Status)
{
    switch ( Status ) {
    case MDO_SESSION_ACTIVE: return "active";
    case MDO_SESSION_ARCHIVED: return "archived";
    case MDO_SESSION_TRASH: return "trash";
    default: return "unknown";
    }
}

static bool MdoApiSettingsStringField(xvalue* Target, cstr TargetName,
    const xvalue* Source, cstr SourceName)
{
    const xvalue* Value = Source != NULL ? xrtValueObjectGet(Source,
        xrtStrView(SourceName)) : NULL;
    xstrview Text;
    return Value != NULL && xrtValueGetString(Value, &Text) &&
        MdoApiValueSetStringView(Target, TargetName, Text);
}

static bool MdoApiSettingsBoolField(xvalue* Target, cstr TargetName,
    const xvalue* Source, cstr SourceName)
{
    const xvalue* Value = Source != NULL ? xrtValueObjectGet(Source,
        xrtStrView(SourceName)) : NULL;
    bool Boolean;
    return Value != NULL && xrtValueGetBool(Value, &Boolean) &&
        MdoApiValueSetBool(Target, TargetName, Boolean);
}

bool MdoApiSettingsRoute(MdoApiContext* Context)
{
    MdoSettingsServiceSnapshot Service;
    str EffectiveJson;
    size_t EffectiveSize = 0u;
    xvalue* Effective;
    const xvalue* EffectiveSettings;
    const xvalue* EffectiveAppearance;
    const xvalue* EffectiveComposer;
    const xvalue* EffectiveNotifications;
    const xvalue* EffectiveAgent;
    const xvalue* EffectiveWorkspace;
    xvalue* Data = xrtValueObject();
    xvalue* Patches = xrtValueObject();
    xvalue* AppearanceValue = xrtValueObject();
    xvalue* ComposerValue = xrtValueObject();
    xvalue* NotificationsValue = xrtValueObject();
    xvalue* AgentValue = xrtValueObject();
    xvalue* WebValue = xrtValueObject();
    xvalue* WorkspaceValue = xrtValueObject();
    xvalue* ServiceValue = xrtValueObject();
    bool Ok;

    memset(&Service, 0, sizeof(Service)); Service.Size = sizeof(Service);
    EffectiveJson = MdoConfigEffectiveJson(&EffectiveSize);
    Effective = EffectiveJson != NULL ?
        xrtJsonParse(xrtStrViewN(EffectiveJson, EffectiveSize)) : NULL;
    xrtFree(EffectiveJson);
    EffectiveSettings = Effective != NULL ? xrtValueObjectGet(Effective,
        XRT_STR_LITERAL("settings")) : NULL;
    EffectiveAppearance = EffectiveSettings != NULL ?
        xrtValueObjectGet(EffectiveSettings,
            XRT_STR_LITERAL("appearance")) : NULL;
    EffectiveComposer = EffectiveSettings != NULL ?
        xrtValueObjectGet(EffectiveSettings,
            XRT_STR_LITERAL("composer")) : NULL;
    EffectiveNotifications = EffectiveSettings != NULL ?
        xrtValueObjectGet(EffectiveSettings,
            XRT_STR_LITERAL("notifications")) : NULL;
    EffectiveAgent = EffectiveSettings != NULL ?
        xrtValueObjectGet(EffectiveSettings, XRT_STR_LITERAL("agent")) : NULL;
    EffectiveWorkspace = EffectiveSettings != NULL ?
        xrtValueObjectGet(EffectiveSettings,
            XRT_STR_LITERAL("workspace")) : NULL;
    Ok = Data != NULL && Patches != NULL && AppearanceValue != NULL &&
        ComposerValue != NULL && NotificationsValue != NULL &&
        AgentValue != NULL && WebValue != NULL && WorkspaceValue != NULL &&
        ServiceValue != NULL && EffectiveSettings != NULL &&
        MdoSettingsServiceGetSnapshot(&Service);
    if ( Ok ) Ok =
        MdoApiValueSetBool(Patches, "settings",
            Service.Config.UserPatch[MDO_CONFIG_SETTINGS]) &&
        MdoApiValueSetBool(Patches, "models",
            Service.Config.UserPatch[MDO_CONFIG_MODELS]) &&
        MdoApiValueSetBool(Patches, "permissions",
            Service.Config.UserPatch[MDO_CONFIG_PERMISSIONS]) &&
        MdoApiValueSetUInt(Data, "schema_version",
            Service.Config.SchemaVersion) &&
        MdoApiValueSetUInt(Data, "revision", Service.Config.Revision) &&
        MdoApiValueSetBool(Data, "runtime_override",
            Service.Config.RuntimeOverride) &&
        MdoApiValueSetUInt(Data, "effective_bytes",
            Service.Config.EffectiveBytes) &&
        MdoApiValueSetTake(Data, "user_patches", &Patches) &&
        MdoApiSettingsStringField(Data, "locale", EffectiveSettings,
            "locale") &&
        MdoApiSettingsStringField(AppearanceValue, "theme",
            EffectiveAppearance, "theme") &&
        MdoApiSettingsStringField(AppearanceValue, "font_size",
            EffectiveAppearance, "font_size") &&
        MdoApiSettingsStringField(AppearanceValue, "density",
            EffectiveAppearance, "density") &&
        MdoApiValueSetTake(Data, "appearance", &AppearanceValue);
    if ( Ok ) Ok =
        MdoApiSettingsStringField(ComposerValue, "submit_mode",
            EffectiveComposer, "submit_mode") &&
        MdoApiValueSetTake(Data, "composer", &ComposerValue);
    if ( Ok ) Ok =
        (EffectiveNotifications != NULL ?
            MdoApiSettingsBoolField(NotificationsValue, "sound",
                EffectiveNotifications, "sound") :
            MdoApiValueSetBool(NotificationsValue, "sound", false)) &&
        MdoApiValueSetTake(Data, "notifications", &NotificationsValue);
    if ( Ok ) Ok =
        MdoApiSettingsStringField(AgentValue, "interaction_mode",
            EffectiveAgent, "interaction_mode") &&
        MdoApiSettingsStringField(AgentValue, "user_instructions",
            EffectiveAgent, "user_instructions") &&
        MdoApiSettingsBoolField(AgentValue, "web_search", EffectiveAgent,
            "web_search") &&
        MdoApiValueSetBool(AgentValue, "memory", Service.Agent.MemoryEnabled) &&
        MdoApiValueSetBool(AgentValue, "schedules",
            Service.Agent.SchedulesEnabled) &&
        MdoApiValueSetUInt(AgentValue, "max_parallel_tools",
            Service.Agent.MaxParallelTools) &&
        MdoApiValueSetUInt(AgentValue, "max_parallel_subagents",
            Service.Agent.MaxParallelSubagents) &&
        MdoApiValueSetString(AgentValue, "reasoning_effort",
            Service.Agent.ReasoningEffort) &&
        MdoApiValueSetString(AgentValue, "permission_profile",
            Service.Agent.PermissionProfile) &&
        MdoApiValueSetTake(Data, "agent", &AgentValue);
    if ( Ok ) Ok =
        MdoApiValueSetBool(WebValue, "enabled", Service.Web.Enabled) &&
        MdoApiValueSetBool(WebValue, "allow_http", Service.Web.AllowHttp) &&
        MdoApiValueSetBool(WebValue, "allow_private_networks",
            Service.Web.AllowPrivateNetworks) &&
        MdoApiValueSetUInt(WebValue, "timeout_ms",
            Service.Web.TimeoutMilliseconds) &&
        MdoApiValueSetUInt(WebValue, "idle_timeout_ms",
            Service.Web.IdleTimeoutMilliseconds) &&
        MdoApiValueSetUInt(WebValue, "max_response_bytes",
            Service.Web.MaxResponseBytes) &&
        MdoApiValueSetUInt(WebValue, "max_text_bytes",
            Service.Web.MaxTextBytes) &&
        MdoApiValueSetUInt(WebValue, "max_documents",
            Service.Web.MaxDocuments) &&
        MdoApiValueSetUInt(WebValue, "max_results", Service.Web.MaxResults) &&
        MdoApiValueSetString(WebValue, "provider", Service.Web.Provider) &&
        MdoApiValueSetString(WebValue, "endpoint", Service.Web.Endpoint) &&
        MdoApiValueSetBool(WebValue, "credential_configured",
            Service.Web.SecretRef[0] != '\0') &&
        MdoApiValueSetTake(Data, "web", &WebValue);
    if ( Ok ) Ok =
        MdoApiSettingsStringField(WorkspaceValue, "open_mode",
            EffectiveWorkspace, "open_mode") &&
        MdoApiSettingsBoolField(WorkspaceValue, "confirm_external_write",
            EffectiveWorkspace, "confirm_external_write") &&
        MdoApiValueSetTake(Data, "workspace", &WorkspaceValue);
    if ( Ok ) Ok =
        MdoApiValueSetBool(ServiceValue, "runtime_consistent",
            !Service.Degraded) &&
        MdoApiValueSetUInt(ServiceValue, "transactions",
            Service.Transactions) &&
        MdoApiValueSetUInt(ServiceValue, "rollbacks", Service.Rollbacks) &&
        MdoApiValueSetString(ServiceValue, "last_error", Service.LastError) &&
        MdoApiValueSetTake(Data, "transaction_service", &ServiceValue);
    xrtValueRelease(Patches); xrtValueRelease(AppearanceValue);
    xrtValueRelease(ComposerValue); xrtValueRelease(NotificationsValue);
    xrtValueRelease(AgentValue); xrtValueRelease(WebValue);
    xrtValueRelease(WorkspaceValue); xrtValueRelease(ServiceValue);
    xrtValueRelease(Effective);
    if ( !Ok ) { xrtValueRelease(Data); Data = NULL; }
    if ( Data == NULL ) return MdoApiStateReply(Context, NULL);
    return MdoApiReplySuccessTakeRevision(Context, 200u, Data,
        Service.Config.Revision);
}

bool MdoApiSessionsRoute(MdoApiContext* Context)
{
    MdoSessionQuery Query;
    xwork_error Error;
    MdoSessionCatalog* Catalog;
    xvalue* Data = xrtValueObject();
    xvalue* Items = xrtValueArray();
    size_t Index;
    bool Ok;

    if ( Context->Request->head->MethodCode == XHTTP_METHOD_POST )
        return MdoApiSessionCreateRoute(Context);
    MdoSessionQueryInit(&Query);
    Query.Limit = MDO_API_LIST_LIMIT;
    memset(&Error, 0, sizeof(Error));
    Catalog = MdoSessionCatalogSearch(&Query, &Error);
    Ok = Catalog != NULL && Data != NULL && Items != NULL;
    for ( Index = 0u; Ok && Index < MdoSessionCatalogCount(Catalog); Index++ ) {
        MdoSessionInfo Info;
        xvalue* Item = xrtValueObject();
        memset(&Info, 0, sizeof(Info)); Info.Size = sizeof(Info);
        Ok = Item != NULL && MdoSessionCatalogAt(Catalog, Index, &Info) &&
            MdoApiValueSetString(Item, "id", Info.Id) &&
            MdoApiValueSetString(Item, "project_id", Info.ProjectId) &&
            MdoApiValueSetString(Item, "parent_session_id",
                Info.ParentSessionId) &&
            MdoApiValueSetString(Item, "title", Info.Title) &&
            MdoApiValueSetString(Item, "agent_id", Info.AgentId) &&
            MdoApiValueSetString(Item, "model_id", Info.ModelId) &&
            MdoApiValueSetString(Item, "protocol",
                MdoModelProtocolName(Info.Protocol)) &&
            MdoApiValueSetString(Item, "reasoning_effort",
                Info.ReasoningEffort) &&
            MdoApiValueSetString(Item, "workspace_root", Info.WorkspaceRoot) &&
            MdoApiValueSetString(Item, "status",
                MdoApiSessionStatusText(Info.Status)) &&
            MdoApiValueSetUInt(Item, "revision", Info.Revision) &&
            MdoApiValueSetInt(Item, "created_at", Info.CreatedAt) &&
            MdoApiValueSetInt(Item, "updated_at", Info.UpdatedAt) &&
            MdoApiValueSetBool(Item, "pinned", Info.Pinned) &&
            MdoApiValueSetBool(Item, "runtime_open", Info.RuntimeOpen) &&
            MdoApiValueSetUInt(Item, "max_output_tokens",
                Info.MaxOutputTokens) &&
            MdoApiValueSetUInt(Item, "forked_through_sequence",
                Info.ForkedThroughSequence) &&
            MdoApiValueAppendTake(Items, &Item);
        xrtValueRelease(Item);
    }
    if ( Ok ) Ok =
        MdoApiValueSetUInt(Data, "generation",
            MdoSessionCatalogGeneration(Catalog)) &&
        MdoApiValueSetUInt(Data, "limit", MDO_API_LIST_LIMIT) &&
        MdoApiValueSetBool(Data, "possibly_truncated",
            MdoSessionCatalogCount(Catalog) == MDO_API_LIST_LIMIT) &&
        MdoApiValueSetTake(Data, "items", &Items);
    xrtValueRelease(Items);
    MdoSessionCatalogRelease(Catalog);
    if ( !Ok ) { xrtValueRelease(Data); Data = NULL; }
    return MdoApiStateReply(Context, Data);
}

bool MdoApiArtifactsRoute(MdoApiContext* Context)
{
    xwork_runtime* Runtime = MdoBootstrapRuntime();
    xvalue* Data = xrtValueObject();
    xvalue* Items = xrtValueArray();
    size_t Total;
    size_t Start;
    size_t Index;
    bool Ok;

    if ( Runtime == NULL ) return MdoApiReplyError(Context, 503u,
        "runtime_unavailable", "The artifact runtime is unavailable", NULL);
    Total = xworkRuntimeArtifactCount(Runtime);
    Start = Total > MDO_API_LIST_LIMIT ? Total - MDO_API_LIST_LIMIT : 0u;
    Ok = Data != NULL && Items != NULL;
    for ( Index = Start; Ok && Index < Total; Index++ ) {
        xwork_artifact_info Info;
        xvalue* Item = xrtValueObject();
        xworkArtifactInfoInit(&Info);
        Ok = Item != NULL && xworkRuntimeArtifactAt(Runtime, Index, &Info) &&
            MdoApiValueSetUInt(Item, "id", Info.uArtifactId) &&
            MdoApiValueSetString(Item, "kind", "tool_output") &&
            MdoApiValueSetInt(Item, "created_at", Info.iCreatedAtUs) &&
            MdoApiValueSetUInt(Item, "agent_id", Info.uAgentId) &&
            MdoApiValueSetUInt(Item, "run_id", Info.uRunId) &&
            MdoApiValueSetUInt(Item, "task_id", Info.uTaskId) &&
            MdoApiValueSetString(Item, "source", Info.sSource) &&
            MdoApiValueSetString(Item, "path", Info.sPath) &&
            MdoApiValueSetString(Item, "media_type", Info.sMediaType) &&
            MdoApiValueSetUInt(Item, "size_bytes", Info.uSizeBytes) &&
            MdoApiValueSetString(Item, "sha256", Info.sSha256) &&
            MdoApiValueAppendTake(Items, &Item);
        xrtValueRelease(Item);
    }
    if ( Ok ) Ok =
        MdoApiValueSetUInt(Data, "total", Total) &&
        MdoApiValueSetBool(Data, "truncated", Start != 0u) &&
        MdoApiValueSetTake(Data, "items", &Items);
    xrtValueRelease(Items);
    if ( !Ok ) { xrtValueRelease(Data); Data = NULL; }
    return MdoApiStateReply(Context, Data);
}

static bool MdoApiArtifactUnsigned(xstrview Text, uint64 Maximum,
    uint64* Value)
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

static bool MdoApiArtifactQueryPart(xstrview Query, size_t* Position,
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

static bool MdoApiArtifactQuery(xstrview Query, uint64* Offset,
    size_t* Limit)
{
    size_t Position = 0u;
    unsigned Seen = 0u;
    *Offset = 0u;
    *Limit = MDO_API_ARTIFACT_DEFAULT_BYTES;
    while ( Position < Query.Size ) {
        xstrview Name;
        xstrview Value;
        uint64 Number;
        unsigned Bit;
        uint64 Maximum;
        if ( !MdoApiArtifactQueryPart(Query, &Position, &Name, &Value) )
            return false;
        if ( Name.Size == 6u && memcmp(Name.Data, "offset", 6u) == 0 ) {
            Bit = 1u; Maximum = UINT64_MAX;
        } else if ( Name.Size == 5u &&
                    memcmp(Name.Data, "limit", 5u) == 0 ) {
            Bit = 2u; Maximum = MDO_API_ARTIFACT_MAX_BYTES;
        } else return false;
        if ( (Seen & Bit) != 0u ||
             !MdoApiArtifactUnsigned(Value, Maximum, &Number) ||
             (Bit == 2u && Number == 0u) ) return false;
        Seen |= Bit;
        if ( Bit == 1u ) *Offset = Number;
        else *Limit = (size_t)Number;
    }
    return true;
}

bool MdoApiArtifactRoute(MdoApiContext* Context)
{
    xwork_runtime* Runtime = MdoBootstrapRuntime();
    xwork_artifact_info Info;
    xwork_artifact_chunk Chunk;
    xwork_error Error;
    xvalue* Data = NULL;
    str Encoded = NULL;
    uint64 ArtifactId;
    uint64 Offset;
    size_t Limit;
    bool Ok;
    if ( Context->ParamCount != 1u ||
         !MdoApiArtifactUnsigned(Context->Params[0], UINT64_MAX,
            &ArtifactId) || ArtifactId == 0u )
        return MdoApiReplyError(Context, 400u, "invalid_path",
            "The artifact identifier must be a nonzero decimal integer", NULL);
    if ( !MdoApiArtifactQuery(Context->Target.Query, &Offset, &Limit) )
        return MdoApiReplyError(Context, 400u, "invalid_query",
            "Only unique numeric offset and bounded limit parameters are accepted",
            NULL);
    if ( Runtime == NULL ) return MdoApiReplyError(Context, 503u,
        "runtime_unavailable", "The artifact runtime is unavailable", NULL);
    xworkArtifactInfoInit(&Info);
    if ( !xworkRuntimeArtifactGetInfo(Runtime, ArtifactId, &Info) )
        return MdoApiReplyError(Context, 404u, "artifact_not_found",
            "The requested artifact does not exist", NULL);
    if ( Offset > Info.uSizeBytes )
        return MdoApiReplyError(Context, 416u,
            "artifact_offset_out_of_range",
            "The artifact offset exceeds its size", NULL);
    xworkArtifactChunkInit(&Chunk);
    memset(&Error, 0, sizeof(Error));
    if ( !xworkRuntimeReadArtifact(Runtime, ArtifactId, Offset, Limit,
            &Chunk, &Error) ) {
        xworkArtifactChunkUnit(&Chunk);
        return MdoApiReplyError(Context, 409u, "artifact_read_failed",
            "The artifact is unavailable or changed", NULL);
    }
    Encoded = xrtBase64EncodeNew(Chunk.pData, Chunk.iSize, NULL);
    Data = xrtValueObject();
    Ok = Encoded != NULL && Data != NULL &&
        MdoApiValueSetUInt(Data, "artifact_id", ArtifactId) &&
        MdoApiValueSetString(Data, "encoding", "base64") &&
        MdoApiValueSetUInt(Data, "offset", Chunk.uOffset) &&
        MdoApiValueSetUInt(Data, "next",
            Chunk.uOffset + (uint64)Chunk.iSize) &&
        MdoApiValueSetUInt(Data, "total_size", Chunk.uTotalSize) &&
        MdoApiValueSetUInt(Data, "bytes", Chunk.iSize) &&
        MdoApiValueSetBool(Data, "eof", Chunk.bEof) &&
        MdoApiValueSetString(Data, "sha256", Info.sSha256) &&
        MdoApiValueSetString(Data, "media_type", Info.sMediaType) &&
        MdoApiValueSetString(Data, "data", Encoded);
    xrtFree(Encoded);
    xworkArtifactChunkUnit(&Chunk);
    if ( !Ok ) { xrtValueRelease(Data); Data = NULL; }
    if ( Data == NULL ) return MdoApiReplyError(Context, 500u,
        "artifact_read_failed", "The artifact chunk could not be encoded",
        NULL);
    return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
}

bool MdoApiStorageRoute(MdoApiContext* Context)
{
    MdoBootstrapSnapshot Snapshot;
    xvalue* Data = xrtValueObject();
    bool Ok;

    memset(&Snapshot, 0, sizeof(Snapshot)); Snapshot.Size = sizeof(Snapshot);
    Ok = Data != NULL && MdoBootstrapGetSnapshot(&Snapshot) &&
        MdoApiValueSetString(Data, "home_path", Snapshot.Home.Path) &&
        MdoApiValueSetString(Data, "persistence",
            Snapshot.Home.Persistence == MDO_PERSISTENCE_EXTERNAL ?
                "external" : (Snapshot.Home.Persistence ==
                MDO_PERSISTENCE_EPHEMERAL ? "ephemeral" : "lazy")) &&
        MdoApiValueSetBool(Data, "external_overlay",
            Snapshot.Home.ExternalOverlay) &&
        MdoApiValueSetUInt(Data, "config_bytes",
            Snapshot.Config.EffectiveBytes) &&
        MdoApiValueSetUInt(Data, "session_count", Snapshot.SessionCount) &&
        MdoApiValueSetUInt(Data, "schedule_count", Snapshot.ScheduleCount) &&
        MdoApiValueSetUInt(Data, "global_memory_count",
            Snapshot.GlobalMemoryCount) &&
        MdoApiValueSetUInt(Data, "artifact_count",
            MdoBootstrapRuntime() != NULL ?
                xworkRuntimeArtifactCount(MdoBootstrapRuntime()) : 0u);
    if ( !Ok ) { xrtValueRelease(Data); Data = NULL; }
    return MdoApiStateReply(Context, Data);
}
