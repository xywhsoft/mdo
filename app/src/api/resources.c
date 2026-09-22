#include <string.h>

#include "internal.h"
#include "../../include/mdo/bootstrap.h"
#include "../../include/mdo/version.h"

static bool MdoApiSetString(xvalue* Object, cstr Key, cstr Value)
{
    return xrtValueObjectSetNew(Object, xrtStrView(Key),
        xrtValueString(xrtStrView(Value != NULL ? Value : "")));
}

static bool MdoApiSetUInt(xvalue* Object, cstr Key, uint64 Value)
{
    return xrtValueObjectSetNew(Object, xrtStrView(Key), xrtValueUInt(Value));
}

static bool MdoApiSetBool(xvalue* Object, cstr Key, bool Value)
{
    return xrtValueObjectSetNew(Object, xrtStrView(Key), xrtValueBool(Value));
}

static bool MdoApiSetObjectTake(xvalue* Object, cstr Key, xvalue** pChild)
{
    xvalue* Child;

    if ( pChild == NULL || *pChild == NULL ) return false;
    Child = *pChild;
    *pChild = NULL;
    return xrtValueObjectSetNew(Object, xrtStrView(Key), Child);
}

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
        MdoApiSetString(Data, "version", MDO_VERSION_TEXT) &&
        MdoApiSetBool(Data, "ready", Snapshot->Ready) &&
        MdoApiSetString(Data, "stage", MdoApiBootstrapStageText(Snapshot->Stage)) &&
        MdoApiSetUInt(Data, "stage_code", (uint64)Snapshot->Stage) &&
        MdoApiSetString(Data, "message", Snapshot->Message) &&
        MdoApiSetUInt(Data, "defaults_bytes", Snapshot->DefaultsBytes);
    if ( Ok ) Ok =
        MdoApiSetString(Home, "mode",
            MdoApiPersistenceText(Snapshot->Home.Persistence)) &&
        MdoApiSetBool(Home, "external_overlay", Snapshot->Home.ExternalOverlay) &&
        MdoApiSetString(Home, "path", Snapshot->Home.Path) &&
        MdoApiSetString(Home, "message", Snapshot->Home.Message) &&
        MdoApiSetObjectTake(Data, "home", &Home);
    if ( Ok ) Ok =
        MdoApiSetBool(Patches, "settings",
            Snapshot->Config.UserPatch[MDO_CONFIG_SETTINGS]) &&
        MdoApiSetBool(Patches, "models",
            Snapshot->Config.UserPatch[MDO_CONFIG_MODELS]) &&
        MdoApiSetBool(Patches, "permissions",
            Snapshot->Config.UserPatch[MDO_CONFIG_PERMISSIONS]) &&
        MdoApiSetUInt(Config, "schema_version", Snapshot->Config.SchemaVersion) &&
        MdoApiSetUInt(Config, "revision", Snapshot->Config.Revision) &&
        MdoApiSetBool(Config, "runtime_override", Snapshot->Config.RuntimeOverride) &&
        MdoApiSetUInt(Config, "effective_bytes", Snapshot->Config.EffectiveBytes) &&
        MdoApiSetObjectTake(Config, "user_patches", &Patches);
    if ( Ok ) Ok = MdoApiSetObjectTake(Data, "config", &Config);
    if ( Ok ) Ok =
        MdoApiSetUInt(Models, "generation", Snapshot->ModelGeneration) &&
        MdoApiSetUInt(Models, "providers", Snapshot->ModelProviderCount) &&
        MdoApiSetUInt(Models, "models", Snapshot->ModelCount) &&
        MdoApiSetUInt(Skills, "generation", Snapshot->SkillGeneration) &&
        MdoApiSetUInt(Skills, "items", Snapshot->SkillCount) &&
        MdoApiSetUInt(Skills, "diagnostics", Snapshot->SkillDiagnosticCount) &&
        MdoApiSetUInt(Memory, "generation", Snapshot->MemoryGeneration) &&
        MdoApiSetUInt(Memory, "global_items", Snapshot->GlobalMemoryCount) &&
        MdoApiSetBool(Web, "enabled", Snapshot->WebEnabled) &&
        MdoApiSetUInt(Web, "documents", Snapshot->WebDocumentCount) &&
        MdoApiSetUInt(Web, "max_documents", Snapshot->WebMaxDocuments) &&
        MdoApiSetUInt(Web, "requests_completed", Snapshot->WebRequestsCompleted) &&
        MdoApiSetUInt(Web, "requests_failed", Snapshot->WebRequestsFailed) &&
        MdoApiSetUInt(Mcp, "generation", Snapshot->McpGeneration) &&
        MdoApiSetUInt(Mcp, "servers", Snapshot->McpServerCount) &&
        MdoApiSetUInt(Mcp, "diagnostics", Snapshot->McpDiagnosticCount) &&
        MdoApiSetUInt(Modules, "generation", Snapshot->ModuleGeneration) &&
        MdoApiSetUInt(Modules, "modules", Snapshot->ModuleCount) &&
        MdoApiSetUInt(Modules, "tools", Snapshot->ModuleToolCount) &&
        MdoApiSetUInt(Modules, "agents", Snapshot->ModuleAgentCount) &&
        MdoApiSetUInt(Modules, "diagnostics", Snapshot->ModuleDiagnosticCount) &&
        MdoApiSetUInt(Schedules, "generation", Snapshot->ScheduleGeneration) &&
        MdoApiSetUInt(Schedules, "items", Snapshot->ScheduleCount) &&
        MdoApiSetUInt(Schedules, "diagnostics",
            Snapshot->ScheduleDiagnosticCount) &&
        MdoApiSetBool(Schedules, "enabled", Snapshot->SchedulesEnabled) &&
        MdoApiSetBool(Schedules, "automatic",
            Snapshot->ScheduleExecutorAutomatic) &&
        MdoApiSetUInt(Schedules, "active_runs", Snapshot->ScheduleActiveRuns) &&
        MdoApiSetUInt(Schedules, "runs_completed",
            Snapshot->ScheduleRunsCompleted) &&
        MdoApiSetUInt(Schedules, "runs_failed", Snapshot->ScheduleRunsFailed) &&
        MdoApiSetUInt(Sessions, "generation", Snapshot->SessionGeneration) &&
        MdoApiSetUInt(Sessions, "items", Snapshot->SessionCount) &&
        MdoApiSetUInt(Sessions, "diagnostics",
            Snapshot->SessionDiagnosticCount);
    if ( Ok ) Ok =
        MdoApiSetObjectTake(Resources, "models", &Models) &&
        MdoApiSetObjectTake(Resources, "skills", &Skills) &&
        MdoApiSetObjectTake(Resources, "memory", &Memory) &&
        MdoApiSetObjectTake(Resources, "web", &Web) &&
        MdoApiSetObjectTake(Resources, "mcp", &Mcp) &&
        MdoApiSetObjectTake(Resources, "modules", &Modules) &&
        MdoApiSetObjectTake(Resources, "schedules", &Schedules) &&
        MdoApiSetObjectTake(Resources, "sessions", &Sessions);
    if ( Ok ) Ok = MdoApiSetObjectTake(Data, "resources", &Resources);

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
