#include <string.h>

#include "internal.h"
#include "../../include/mdo/bootstrap.h"
#include "../../include/mdo/version.h"

static cstr MdoApiBootstrapStageText(MdoBootstrapStage Stage)
{
    switch ( Stage ) {
    case MDO_BOOTSTRAP_EMPTY: return "empty";
    case MDO_BOOTSTRAP_HOME_READY: return "home_ready";
    case MDO_BOOTSTRAP_CONFIG_READY: return "config_ready";
    case MDO_BOOTSTRAP_MODELS_READY: return "models_ready";
    case MDO_BOOTSTRAP_RUNTIME_READY: return "runtime_ready";
    case MDO_BOOTSTRAP_SKILLS_READY: return "skills_ready";
    case MDO_BOOTSTRAP_MEMORY_READY: return "memory_ready";
    case MDO_BOOTSTRAP_WEB_READY: return "web_ready";
    case MDO_BOOTSTRAP_MCP_READY: return "mcp_ready";
    case MDO_BOOTSTRAP_MODULES_READY: return "modules_ready";
    case MDO_BOOTSTRAP_SCHEDULES_READY: return "schedules_ready";
    case MDO_BOOTSTRAP_SESSIONS_READY: return "sessions_ready";
    case MDO_BOOTSTRAP_EXECUTOR_READY: return "ready";
    case MDO_BOOTSTRAP_FAILED: return "failed";
    default: return "unknown";
    }
}

static cstr MdoApiPersistenceText(MdoPersistenceMode Mode)
{
    switch ( Mode ) {
    case MDO_PERSISTENCE_LAZY: return "lazy";
    case MDO_PERSISTENCE_EXTERNAL: return "external";
    case MDO_PERSISTENCE_EPHEMERAL: return "ephemeral";
    default: return "unknown";
    }
}

static xvalue* MdoApiBootstrapData(const MdoBootstrapSnapshot* Snapshot)
{
    xvalue* Data = xrtValueObject();
    xvalue* Home = xrtValueObject();
    xvalue* Config = xrtValueObject();
    xvalue* Patches = xrtValueObject();
    xvalue* Resources = xrtValueObject();
    xvalue* Models = xrtValueObject();
    xvalue* Skills = xrtValueObject();
    xvalue* Memory = xrtValueObject();
    xvalue* Web = xrtValueObject();
    xvalue* Mcp = xrtValueObject();
    xvalue* Modules = xrtValueObject();
    xvalue* Schedules = xrtValueObject();
    xvalue* Sessions = xrtValueObject();
    bool Ok = Data != NULL && Home != NULL && Config != NULL &&
        Patches != NULL && Resources != NULL && Models != NULL &&
        Skills != NULL && Memory != NULL && Web != NULL && Mcp != NULL &&
        Modules != NULL && Schedules != NULL && Sessions != NULL;

    if ( Ok ) Ok =
        MdoApiValueSetString(Data, "version", MDO_VERSION_TEXT) &&
        MdoApiValueSetBool(Data, "ready", Snapshot->Ready) &&
        MdoApiValueSetString(Data, "stage", MdoApiBootstrapStageText(Snapshot->Stage)) &&
        MdoApiValueSetUInt(Data, "stage_code", (uint64)Snapshot->Stage) &&
        MdoApiValueSetString(Data, "message", Snapshot->Message) &&
        MdoApiValueSetUInt(Data, "defaults_bytes", Snapshot->DefaultsBytes);
    if ( Ok ) Ok =
        MdoApiValueSetString(Home, "mode",
            MdoApiPersistenceText(Snapshot->Home.Persistence)) &&
        MdoApiValueSetBool(Home, "external_overlay", Snapshot->Home.ExternalOverlay) &&
        MdoApiValueSetBool(Home, "restart_required", Snapshot->Home.RestartRequired) &&
        MdoApiValueSetBool(Home, "import_in_progress", Snapshot->Home.ImportInProgress) &&
        MdoApiValueSetString(Home, "path", Snapshot->Home.Path) &&
        MdoApiValueSetString(Home, "message", Snapshot->Home.Message) &&
        MdoApiValueSetTake(Data, "home", &Home);
    if ( Ok ) Ok =
        MdoApiValueSetBool(Patches, "settings",
            Snapshot->Config.UserPatch[MDO_CONFIG_SETTINGS]) &&
        MdoApiValueSetBool(Patches, "models",
            Snapshot->Config.UserPatch[MDO_CONFIG_MODELS]) &&
        MdoApiValueSetBool(Patches, "permissions",
            Snapshot->Config.UserPatch[MDO_CONFIG_PERMISSIONS]) &&
        MdoApiValueSetUInt(Config, "schema_version", Snapshot->Config.SchemaVersion) &&
        MdoApiValueSetUInt(Config, "revision", Snapshot->Config.Revision) &&
        MdoApiValueSetBool(Config, "runtime_override", Snapshot->Config.RuntimeOverride) &&
        MdoApiValueSetUInt(Config, "effective_bytes", Snapshot->Config.EffectiveBytes) &&
        MdoApiValueSetTake(Config, "user_patches", &Patches);
    if ( Ok ) Ok = MdoApiValueSetTake(Data, "config", &Config);
    if ( Ok ) Ok =
        MdoApiValueSetUInt(Models, "generation", Snapshot->ModelGeneration) &&
        MdoApiValueSetUInt(Models, "providers", Snapshot->ModelProviderCount) &&
        MdoApiValueSetUInt(Models, "models", Snapshot->ModelCount) &&
        MdoApiValueSetUInt(Skills, "generation", Snapshot->SkillGeneration) &&
        MdoApiValueSetUInt(Skills, "items", Snapshot->SkillCount) &&
        MdoApiValueSetUInt(Skills, "diagnostics", Snapshot->SkillDiagnosticCount) &&
        MdoApiValueSetUInt(Memory, "generation", Snapshot->MemoryGeneration) &&
        MdoApiValueSetUInt(Memory, "global_items", Snapshot->GlobalMemoryCount) &&
        MdoApiValueSetBool(Web, "enabled", Snapshot->WebEnabled) &&
        MdoApiValueSetUInt(Web, "documents", Snapshot->WebDocumentCount) &&
        MdoApiValueSetUInt(Web, "max_documents", Snapshot->WebMaxDocuments) &&
        MdoApiValueSetUInt(Web, "requests_completed", Snapshot->WebRequestsCompleted) &&
        MdoApiValueSetUInt(Web, "requests_failed", Snapshot->WebRequestsFailed) &&
        MdoApiValueSetUInt(Mcp, "generation", Snapshot->McpGeneration) &&
        MdoApiValueSetUInt(Mcp, "servers", Snapshot->McpServerCount) &&
        MdoApiValueSetUInt(Mcp, "diagnostics", Snapshot->McpDiagnosticCount) &&
        MdoApiValueSetUInt(Modules, "generation", Snapshot->ModuleGeneration) &&
        MdoApiValueSetUInt(Modules, "modules", Snapshot->ModuleCount) &&
        MdoApiValueSetUInt(Modules, "tools", Snapshot->ModuleToolCount) &&
        MdoApiValueSetUInt(Modules, "agents", Snapshot->ModuleAgentCount) &&
        MdoApiValueSetUInt(Modules, "diagnostics", Snapshot->ModuleDiagnosticCount) &&
        MdoApiValueSetUInt(Schedules, "generation", Snapshot->ScheduleGeneration) &&
        MdoApiValueSetUInt(Schedules, "items", Snapshot->ScheduleCount) &&
        MdoApiValueSetUInt(Schedules, "diagnostics",
            Snapshot->ScheduleDiagnosticCount) &&
        MdoApiValueSetBool(Schedules, "enabled", Snapshot->SchedulesEnabled) &&
        MdoApiValueSetBool(Schedules, "automatic",
            Snapshot->ScheduleExecutorAutomatic) &&
        MdoApiValueSetUInt(Schedules, "active_runs", Snapshot->ScheduleActiveRuns) &&
        MdoApiValueSetUInt(Schedules, "runs_completed",
            Snapshot->ScheduleRunsCompleted) &&
        MdoApiValueSetUInt(Schedules, "runs_failed", Snapshot->ScheduleRunsFailed) &&
        MdoApiValueSetUInt(Sessions, "generation", Snapshot->SessionGeneration) &&
        MdoApiValueSetUInt(Sessions, "items", Snapshot->SessionCount) &&
        MdoApiValueSetUInt(Sessions, "diagnostics",
            Snapshot->SessionDiagnosticCount);
    if ( Ok ) Ok =
        MdoApiValueSetTake(Resources, "models", &Models) &&
        MdoApiValueSetTake(Resources, "skills", &Skills) &&
        MdoApiValueSetTake(Resources, "memory", &Memory) &&
        MdoApiValueSetTake(Resources, "web", &Web) &&
        MdoApiValueSetTake(Resources, "mcp", &Mcp) &&
        MdoApiValueSetTake(Resources, "modules", &Modules) &&
        MdoApiValueSetTake(Resources, "schedules", &Schedules) &&
        MdoApiValueSetTake(Resources, "sessions", &Sessions);
    if ( Ok ) Ok = MdoApiValueSetTake(Data, "resources", &Resources);

    xrtValueRelease(Home);
    xrtValueRelease(Config);
    xrtValueRelease(Patches);
    xrtValueRelease(Resources);
    xrtValueRelease(Models);
    xrtValueRelease(Skills);
    xrtValueRelease(Memory);
    xrtValueRelease(Web);
    xrtValueRelease(Mcp);
    xrtValueRelease(Modules);
    xrtValueRelease(Schedules);
    xrtValueRelease(Sessions);
    if ( !Ok ) {
        xrtValueRelease(Data);
        return NULL;
    }
    return Data;
}

bool MdoApiBootstrapRoute(MdoApiContext* pContext)
{
    MdoBootstrapSnapshot Snapshot;
    xvalue* Data;

    memset(&Snapshot, 0, sizeof(Snapshot));
    Snapshot.Size = sizeof(Snapshot);
    if ( !MdoBootstrapGetSnapshot(&Snapshot) ) {
        return MdoApiReplyError(pContext, 503u, "bootstrap_unavailable",
            "Bootstrap state is unavailable", NULL);
    }
    Data = MdoApiBootstrapData(&Snapshot);
    if ( Data == NULL ) {
        return MdoApiReplyError(pContext, 500u, "internal_error",
            "Bootstrap response could not be created", NULL);
    }
    return MdoApiReplySuccessTake(pContext, 200u, Data, NULL);
}
