#include <string.h>

#include "internal.h"
#include "../../include/mdo/bootstrap.h"
#include "../../include/mdo/config.h"
#include "../../include/mdo/sessions.h"
#include "../../include/mdo/settings.h"

#define MDO_API_LIST_LIMIT 100u

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

bool MdoApiSettingsRoute(MdoApiContext* Context)
{
    MdoSettingsServiceSnapshot Service;
    xvalue* Data = xrtValueObject();
    xvalue* Patches = xrtValueObject();
    xvalue* AgentValue = xrtValueObject();
    xvalue* WebValue = xrtValueObject();
    xvalue* ServiceValue = xrtValueObject();
    bool Ok;

    memset(&Service, 0, sizeof(Service)); Service.Size = sizeof(Service);
    Ok = Data != NULL && Patches != NULL && AgentValue != NULL &&
        WebValue != NULL && ServiceValue != NULL &&
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
        MdoApiValueSetTake(Data, "user_patches", &Patches);
    if ( Ok ) Ok =
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
        MdoApiValueSetBool(ServiceValue, "runtime_consistent",
            !Service.Degraded) &&
        MdoApiValueSetUInt(ServiceValue, "transactions",
            Service.Transactions) &&
        MdoApiValueSetUInt(ServiceValue, "rollbacks", Service.Rollbacks) &&
        MdoApiValueSetString(ServiceValue, "last_error", Service.LastError) &&
        MdoApiValueSetTake(Data, "transaction_service", &ServiceValue);
    xrtValueRelease(Patches); xrtValueRelease(AgentValue);
    xrtValueRelease(WebValue); xrtValueRelease(ServiceValue);
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
