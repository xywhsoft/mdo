#include <string.h>

#include "internal.h"
#include "../../include/mdo/bootstrap.h"
#include "../../include/mdo/config.h"
#include "../../include/mdo/schedules.h"
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

static cstr MdoApiScheduleFrequencyText(xwork_schedule_frequency Frequency)
{
    switch ( Frequency ) {
    case XWORK_SCHEDULE_ONCE: return "once";
    case XWORK_SCHEDULE_MINUTELY: return "minutely";
    case XWORK_SCHEDULE_HOURLY: return "hourly";
    case XWORK_SCHEDULE_DAILY: return "daily";
    case XWORK_SCHEDULE_WEEKLY: return "weekly";
    default: return "unknown";
    }
}

static cstr MdoApiScheduleTimezoneText(xwork_schedule_timezone Timezone)
{
    switch ( Timezone ) {
    case XWORK_SCHEDULE_TIMEZONE_UTC: return "utc";
    case XWORK_SCHEDULE_TIMEZONE_FIXED_OFFSET: return "fixed_offset";
    case XWORK_SCHEDULE_TIMEZONE_SYSTEM_LOCAL: return "system_local";
    default: return "unknown";
    }
}

static cstr MdoApiScheduleMisfireText(xwork_schedule_misfire_policy Policy)
{
    switch ( Policy ) {
    case XWORK_SCHEDULE_MISFIRE_SKIP: return "skip";
    case XWORK_SCHEDULE_MISFIRE_RUN_ONCE: return "run_once";
    case XWORK_SCHEDULE_MISFIRE_CATCH_UP: return "catch_up";
    default: return "unknown";
    }
}

static cstr MdoApiScheduleOverlapText(xwork_schedule_overlap_policy Policy)
{
    switch ( Policy ) {
    case XWORK_SCHEDULE_OVERLAP_SKIP: return "skip";
    case XWORK_SCHEDULE_OVERLAP_QUEUE_ONE: return "queue_one";
    default: return "unknown";
    }
}

static cstr MdoApiTaskKindText(xwork_task_kind Kind)
{
    switch ( Kind ) {
    case XWORK_TASK_PROCESS: return "process";
    case XWORK_TASK_AGENT: return "agent";
    case XWORK_TASK_SCHEDULED: return "scheduled";
    default: return "unknown";
    }
}

static cstr MdoApiTaskStateText(xwork_task_state State)
{
    switch ( State ) {
    case XWORK_TASK_PENDING: return "pending";
    case XWORK_TASK_RUNNING: return "running";
    case XWORK_TASK_SUCCEEDED: return "succeeded";
    case XWORK_TASK_FAILED: return "failed";
    case XWORK_TASK_CANCELLED: return "cancelled";
    case XWORK_TASK_TIMED_OUT: return "timed_out";
    case XWORK_TASK_LOST: return "lost";
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

bool MdoApiSchedulesRoute(MdoApiContext* Context)
{
    xwork_error Error;
    MdoScheduleCatalog* Catalog;
    MdoScheduleExecutorSnapshot Executor;
    xvalue* Data = xrtValueObject();
    xvalue* Items = xrtValueArray();
    size_t Count;
    size_t Index;
    bool Ok;

    memset(&Error, 0, sizeof(Error));
    memset(&Executor, 0, sizeof(Executor)); Executor.Size = sizeof(Executor);
    Catalog = MdoScheduleCatalogSnapshot(&Error);
    Count = Catalog != NULL ? MdoScheduleCatalogCount(Catalog) : 0u;
    Ok = Catalog != NULL && Data != NULL && Items != NULL &&
        MdoScheduleExecutorGetSnapshot(&Executor);
    if ( Count > MDO_API_LIST_LIMIT ) Count = MDO_API_LIST_LIMIT;
    for ( Index = 0u; Ok && Index < Count; Index++ ) {
        MdoScheduleInfo Info;
        xvalue* Item = xrtValueObject();
        memset(&Info, 0, sizeof(Info)); Info.Size = sizeof(Info);
        Ok = Item != NULL && MdoScheduleCatalogAt(Catalog, Index, &Info) &&
            MdoApiValueSetString(Item, "id", Info.Id) &&
            MdoApiValueSetString(Item, "label", Info.Label) &&
            MdoApiValueSetString(Item, "notify", Info.Notify) &&
            MdoApiValueSetString(Item, "project_id", Info.ProjectId) &&
            MdoApiValueSetString(Item, "agent_id", Info.AgentId) &&
            MdoApiValueSetString(Item, "model_id", Info.ModelId) &&
            MdoApiValueSetString(Item, "protocol",
                Info.Protocol == MDO_SCHEDULE_PROTOCOL_DEFAULT ? "default" :
                MdoModelProtocolName(Info.Protocol)) &&
            MdoApiValueSetString(Item, "reasoning_effort",
                Info.ReasoningEffort) &&
            MdoApiValueSetString(Item, "workspace_root", Info.WorkspaceRoot) &&
            MdoApiValueSetString(Item, "frequency",
                MdoApiScheduleFrequencyText(Info.Frequency)) &&
            MdoApiValueSetString(Item, "timezone",
                MdoApiScheduleTimezoneText(Info.Timezone)) &&
            MdoApiValueSetString(Item, "fold_policy",
                Info.FoldPolicy == XWORK_SCHEDULE_FOLD_LATER ? "later" :
                "earlier") &&
            MdoApiValueSetString(Item, "misfire_policy",
                MdoApiScheduleMisfireText(Info.MisfirePolicy)) &&
            MdoApiValueSetString(Item, "overlap_policy",
                MdoApiScheduleOverlapText(Info.OverlapPolicy)) &&
            MdoApiValueSetUInt(Item, "revision", Info.Revision) &&
            MdoApiValueSetInt(Item, "updated_at", Info.UpdatedAt) &&
            MdoApiValueSetUInt(Item, "runtime_generation",
                Info.RuntimeGeneration) &&
            MdoApiValueSetInt(Item, "next_occurrence_at",
                Info.NextOccurrenceAt) &&
            MdoApiValueSetInt(Item, "last_claimed_at", Info.LastClaimedAt) &&
            MdoApiValueSetUInt(Item, "claim_count", Info.ClaimCount) &&
            MdoApiValueSetUInt(Item, "misfire_count", Info.MisfireCount) &&
            MdoApiValueSetUInt(Item, "active_runs", Info.ActiveRuns) &&
            MdoApiValueSetUInt(Item, "interval", Info.Interval) &&
            MdoApiValueSetInt(Item, "start_at", Info.StartAt) &&
            MdoApiValueSetUInt(Item, "weekday_mask", Info.WeekdayMask) &&
            MdoApiValueSetInt(Item, "utc_offset_seconds",
                Info.UtcOffsetSeconds) &&
            MdoApiValueSetUInt(Item, "misfire_grace_seconds",
                Info.MisfireGraceSeconds) &&
            MdoApiValueSetUInt(Item, "max_catch_up", Info.MaxCatchUp) &&
            MdoApiValueSetUInt(Item, "max_concurrent_runs",
                Info.MaxConcurrentRuns) &&
            MdoApiValueSetUInt(Item, "max_output_tokens",
                Info.MaxOutputTokens) &&
            MdoApiValueSetUInt(Item, "input_bytes", strlen(Info.Input)) &&
            MdoApiValueSetBool(Item, "enabled", Info.Enabled) &&
            MdoApiValueSetBool(Item, "runnable", Info.Runnable) &&
            MdoApiValueAppendTake(Items, &Item);
        xrtValueRelease(Item);
    }
    if ( Ok ) Ok =
        MdoApiValueSetUInt(Data, "generation",
            MdoScheduleCatalogGeneration(Catalog)) &&
        MdoApiValueSetBool(Data, "enabled", MdoScheduleManagerEnabled()) &&
        MdoApiValueSetBool(Data, "automatic", Executor.Automatic) &&
        MdoApiValueSetBool(Data, "persistence_fault",
            Executor.PersistenceFault) &&
        MdoApiValueSetUInt(Data, "active_runs", Executor.ActiveRuns) &&
        MdoApiValueSetUInt(Data, "claims_started", Executor.ClaimsStarted) &&
        MdoApiValueSetUInt(Data, "runs_completed", Executor.RunsCompleted) &&
        MdoApiValueSetUInt(Data, "runs_failed", Executor.RunsFailed) &&
        MdoApiValueSetUInt(Data, "total", MdoScheduleCatalogCount(Catalog)) &&
        MdoApiValueSetBool(Data, "truncated",
            MdoScheduleCatalogCount(Catalog) > MDO_API_LIST_LIMIT) &&
        MdoApiValueSetTake(Data, "items", &Items);
    xrtValueRelease(Items);
    MdoScheduleCatalogRelease(Catalog);
    if ( !Ok ) { xrtValueRelease(Data); Data = NULL; }
    return MdoApiStateReply(Context, Data);
}

bool MdoApiTasksRoute(MdoApiContext* Context)
{
    xwork_runtime* Runtime = MdoBootstrapRuntime();
    xwork_error Error;
    xwork_task_snapshot* Snapshot;
    xvalue* Data = xrtValueObject();
    xvalue* Items = xrtValueArray();
    size_t Total;
    size_t Start;
    size_t Index;
    bool Ok;

    if ( Runtime == NULL ) return MdoApiReplyError(Context, 503u,
        "runtime_unavailable", "The task runtime is unavailable", NULL);
    memset(&Error, 0, sizeof(Error));
    Snapshot = xworkRuntimeTaskSnapshot(Runtime, 0u, &Error);
    Total = Snapshot != NULL ? xworkTaskSnapshotCount(Snapshot) : 0u;
    Start = Total > MDO_API_LIST_LIMIT ? Total - MDO_API_LIST_LIMIT : 0u;
    Ok = Snapshot != NULL && Data != NULL && Items != NULL;
    for ( Index = Start; Ok && Index < Total; Index++ ) {
        xwork_task_info Info;
        xvalue* Item = xrtValueObject();
        xworkTaskInfoInit(&Info);
        Ok = Item != NULL && xworkTaskSnapshotTaskAt(Snapshot, Index, &Info) &&
            MdoApiValueSetUInt(Item, "id", Info.uTaskId) &&
            MdoApiValueSetUInt(Item, "owner_agent_id", Info.uOwnerAgentId) &&
            MdoApiValueSetUInt(Item, "owner_run_id", Info.uOwnerRunId) &&
            MdoApiValueSetUInt(Item, "parent_task_id", Info.uParentTaskId) &&
            MdoApiValueSetString(Item, "kind", MdoApiTaskKindText(Info.eKind)) &&
            MdoApiValueSetString(Item, "state",
                MdoApiTaskStateText(Info.eState)) &&
            MdoApiValueSetUInt(Item, "revision", Info.uRevision) &&
            MdoApiValueSetInt(Item, "created_at", Info.iCreatedAtUs) &&
            MdoApiValueSetInt(Item, "started_at", Info.iStartedAtUs) &&
            MdoApiValueSetInt(Item, "ended_at", Info.iEndedAtUs) &&
            MdoApiValueSetInt(Item, "scheduled_at", Info.iScheduledAtUs) &&
            MdoApiValueSetBool(Item, "exit_status_valid",
                Info.bExitStatusValid) &&
            MdoApiValueSetInt(Item, "exit_code", Info.iExitCode) &&
            MdoApiValueSetInt(Item, "exit_signal", Info.iExitSignal) &&
            MdoApiValueSetInt(Item, "stop_reason", Info.iStopReason) &&
            MdoApiValueSetBool(Item, "notice_taken", Info.bNoticeTaken) &&
            MdoApiValueSetString(Item, "owner_session", Info.sOwnerSession) &&
            MdoApiValueSetString(Item, "label", Info.sLabel) &&
            MdoApiValueSetString(Item, "notify", Info.sNotify) &&
            MdoApiValueSetString(Item, "schedule_id", Info.sScheduleId) &&
            MdoApiValueSetUInt(Item, "schedule_generation",
                Info.uScheduleGeneration) &&
            MdoApiValueAppendTake(Items, &Item);
        xrtValueRelease(Item);
    }
    if ( Ok ) Ok =
        MdoApiValueSetUInt(Data, "total", Total) &&
        MdoApiValueSetBool(Data, "truncated", Start != 0u) &&
        MdoApiValueSetTake(Data, "items", &Items);
    xrtValueRelease(Items);
    xworkTaskSnapshotRelease(Snapshot);
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
