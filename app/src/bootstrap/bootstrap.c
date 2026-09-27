#include <stdio.h>
#include <string.h>

#include "../../include/mdo/approvals.h"
#include "../../include/mdo/asks.h"
#include "../../include/mdo/bootstrap.h"
#include "../../include/mdo/mcp.h"
#include "../../include/mdo/memory.h"
#include "../../include/mdo/models.h"
#include "../../include/mdo/modules.h"
#include "../../include/mdo/operations.h"
#include "../../include/mdo/runs.h"
#include "../../include/mdo/schedules.h"
#include "../../include/mdo/sessions.h"
#include "../../include/mdo/settings.h"
#include "../../include/mdo/skills.h"
#include "../../include/mdo/version.h"
#include "../../include/mdo/web.h"

typedef struct MdoBootstrapState {
    MdoBootstrapStage Stage;
    xwork_runtime* Runtime;
    size_t DefaultsBytes;
    bool ConfigReady;
    char Message[256];
} MdoBootstrapState;

static MdoBootstrapState g_MdoBootstrap;

static void MdoBootstrapFail(cstr Fallback)
{
    const xerror* pError = xrtGetError();
    cstr sMessage = pError != NULL ? xrtErrorMessage(pError) : NULL;

    snprintf(g_MdoBootstrap.Message, sizeof(g_MdoBootstrap.Message), "%s",
        (sMessage != NULL && sMessage[0] != '\0') ? sMessage : Fallback);
    g_MdoBootstrap.Stage = MDO_BOOTSTRAP_FAILED;
    printf("[mdo] bootstrap failed: %s\n", g_MdoBootstrap.Message);
}

bool MdoBootstrapInit(XS_HostInfo* pHost)
{
    xwork_runtime_config RuntimeConfig;
    xwork_error WorkError;
    MdoHomeSnapshot Home;
    MdoRunManagerOptions RunOptions;
    MdoScheduleExecutorOptions ExecutorOptions;

    (void)pHost;
    if ( g_MdoBootstrap.Stage != MDO_BOOTSTRAP_EMPTY )
        return g_MdoBootstrap.Stage == MDO_BOOTSTRAP_EXECUTOR_READY;
    if ( !MdoHomeInit() ) {
        MdoBootstrapFail("Home initialization failed");
        return false;
    }
    g_MdoBootstrap.Stage = MDO_BOOTSTRAP_HOME_READY;

    if ( !MdoConfigInit() ) {
        MdoBootstrapFail("configuration initialization failed");
        return false;
    }
    g_MdoBootstrap.ConfigReady = true;
    {
        MdoConfigSnapshot Config;
        memset(&Config, 0, sizeof(Config));
        Config.Size = sizeof(Config);
        if ( !MdoConfigGetSnapshot(&Config) ) {
            MdoBootstrapFail("configuration snapshot failed");
            return false;
        }
        g_MdoBootstrap.DefaultsBytes = Config.EffectiveBytes;
    }
    g_MdoBootstrap.Stage = MDO_BOOTSTRAP_CONFIG_READY;

    if ( !MdoSettingsServiceInit() ) {
        MdoBootstrapFail("settings transaction service initialization failed");
        return false;
    }

    if ( !MdoModelManagerInit() ) {
        MdoBootstrapFail("model manager initialization failed");
        return false;
    }
    g_MdoBootstrap.Stage = MDO_BOOTSTRAP_MODELS_READY;

    xworkRuntimeConfigInit(&RuntimeConfig);
    memset(&WorkError, 0, sizeof(WorkError));
    g_MdoBootstrap.Runtime = xworkRuntimeCreate(&RuntimeConfig, &WorkError);
    if ( g_MdoBootstrap.Runtime == NULL ) {
        snprintf(g_MdoBootstrap.Message, sizeof(g_MdoBootstrap.Message), "%.255s",
            WorkError.sMessage[0] != '\0' ? WorkError.sMessage :
            "xwork runtime initialization failed");
        g_MdoBootstrap.Stage = MDO_BOOTSTRAP_FAILED;
        printf("[mdo] bootstrap failed: %s\n", g_MdoBootstrap.Message);
        return false;
    }
    g_MdoBootstrap.Stage = MDO_BOOTSTRAP_RUNTIME_READY;
    if ( !MdoSkillManagerInit() ) {
        MdoBootstrapFail("Skill manager initialization failed");
        return false;
    }
    g_MdoBootstrap.Stage = MDO_BOOTSTRAP_SKILLS_READY;
    if ( !MdoMemoryManagerInit(g_MdoBootstrap.Runtime) ) {
        MdoBootstrapFail("memory manager initialization failed");
        return false;
    }
    g_MdoBootstrap.Stage = MDO_BOOTSTRAP_MEMORY_READY;
    if ( !MdoWebManagerInit(g_MdoBootstrap.Runtime) ) {
        MdoBootstrapFail("Web tool manager initialization failed");
        return false;
    }
    g_MdoBootstrap.Stage = MDO_BOOTSTRAP_WEB_READY;
    if ( !MdoMcpManagerInit(g_MdoBootstrap.Runtime) ) {
        MdoBootstrapFail("MCP manager initialization failed");
        return false;
    }
    g_MdoBootstrap.Stage = MDO_BOOTSTRAP_MCP_READY;
    if ( !MdoModuleManagerInit(g_MdoBootstrap.Runtime) ) {
        MdoBootstrapFail("module manager initialization failed");
        return false;
    }
    g_MdoBootstrap.Stage = MDO_BOOTSTRAP_MODULES_READY;
    if ( !MdoOperationManagerInit() ) {
        MdoBootstrapFail("operation manager initialization failed");
        return false;
    }
    if ( !MdoScheduleManagerInit(g_MdoBootstrap.Runtime) ) {
        MdoBootstrapFail("schedule manager initialization failed");
        return false;
    }
    g_MdoBootstrap.Stage = MDO_BOOTSTRAP_SCHEDULES_READY;
    if ( !MdoSessionManagerInit(g_MdoBootstrap.Runtime) ) {
        MdoBootstrapFail("session manager initialization failed");
        return false;
    }
    g_MdoBootstrap.Stage = MDO_BOOTSTRAP_SESSIONS_READY;
    if ( !MdoApprovalManagerInit() ) {
        MdoBootstrapFail("approval manager initialization failed");
        return false;
    }
    if ( !MdoAskManagerInit() ) {
        MdoBootstrapFail("ask manager initialization failed");
        return false;
    }
    MdoRunManagerOptionsInit(&RunOptions);
    RunOptions.OnPermission = MdoApprovalOnPermission;
    RunOptions.UseRunPermissionScope = true;
    if ( !MdoRunManagerInit(g_MdoBootstrap.Runtime, &RunOptions, &WorkError) ) {
        snprintf(g_MdoBootstrap.Message, sizeof(g_MdoBootstrap.Message), "%.255s",
            WorkError.sMessage[0] != '\0' ? WorkError.sMessage :
            "interactive run manager initialization failed");
        g_MdoBootstrap.Stage = MDO_BOOTSTRAP_FAILED;
        printf("[mdo] bootstrap failed: %s\n", g_MdoBootstrap.Message);
        return false;
    }
    MdoScheduleExecutorOptionsInit(&ExecutorOptions);
    ExecutorOptions.OnPermission = MdoApprovalOnPermission;
    ExecutorOptions.UseRunPermissionScope = true;
    if ( !MdoScheduleExecutorInit(g_MdoBootstrap.Runtime, &ExecutorOptions,
            &WorkError) ) {
        snprintf(g_MdoBootstrap.Message, sizeof(g_MdoBootstrap.Message), "%.255s",
            WorkError.sMessage[0] != '\0' ? WorkError.sMessage :
            "schedule executor initialization failed");
        g_MdoBootstrap.Stage = MDO_BOOTSTRAP_FAILED;
        printf("[mdo] bootstrap failed: %s\n", g_MdoBootstrap.Message);
        return false;
    }
    g_MdoBootstrap.Stage = MDO_BOOTSTRAP_EXECUTOR_READY;

    memset(&Home, 0, sizeof(Home));
    Home.Size = sizeof(Home);
    if ( MdoHomeGetSnapshot(&Home) ) {
        const char* sMode = Home.Persistence == MDO_PERSISTENCE_EXTERNAL ?
            "external" : (Home.Persistence == MDO_PERSISTENCE_EPHEMERAL ?
            "ephemeral" : "lazy");
        MdoModuleCatalog* pModules = MdoModuleCatalogSnapshot();
        size_t iModules = MdoModuleCatalogModuleCount(pModules);
        size_t iTools = MdoModuleCatalogToolCount(pModules);
        MdoSkillCatalog* pSkills = MdoSkillCatalogSnapshot();
        size_t iSkills = MdoSkillCatalogCount(pSkills);
        MdoMcpCatalog* pMcp = MdoMcpCatalogSnapshot();
        size_t iMcp = MdoMcpCatalogCount(pMcp);
        MdoModelCatalog* pModels = MdoModelCatalogSnapshot();
        size_t iProviders = MdoModelCatalogProviderCount(pModels);
        size_t iModels = MdoModelCatalogModelCount(pModels);
        MdoWebSnapshot Web;
        memset(&Web, 0, sizeof(Web));
        Web.Size = sizeof(Web);
        (void)MdoWebManagerGetSnapshot(&Web);
        MdoMemorySnapshot* pMemory = MdoMemorySnapshotCreate(
            MDO_MEMORY_GLOBAL, NULL, NULL);
        size_t iMemory = MdoMemorySnapshotCount(pMemory);
        xwork_error ScheduleError;
        MdoScheduleCatalog* pSchedules =
            MdoScheduleCatalogSnapshot(&ScheduleError);
        size_t iSchedules = MdoScheduleCatalogCount(pSchedules);
        printf("[mdo] bootstrap ready: version=%s home=%s mode=%s defaults=%zu providers=%zu models=%zu skills=%zu memory=%zu web=%d mcp=%zu modules=%zu tools=%zu schedules=%zu\n",
            MDO_VERSION_TEXT, Home.Path, sMode, g_MdoBootstrap.DefaultsBytes,
            iProviders, iModels, iSkills, iMemory, Web.Enabled ? 1 : 0,
            iMcp, iModules, iTools, iSchedules);
        MdoScheduleCatalogRelease(pSchedules);
        MdoMemorySnapshotRelease(pMemory);
        MdoModelCatalogRelease(pModels);
        MdoMcpCatalogRelease(pMcp);
        MdoSkillCatalogRelease(pSkills);
        MdoModuleCatalogRelease(pModules);
    }
    return true;
}

void MdoBootstrapUnit(void)
{
    MdoScheduleExecutorUnit();
    MdoRunManagerUnit();
    MdoAskManagerUnit();
    MdoApprovalManagerUnit();
    MdoSessionManagerUnit();
    MdoScheduleManagerUnit();
    MdoOperationManagerUnit();
    MdoModuleManagerUnit();
    MdoMcpManagerUnit();
    MdoWebManagerUnit();
    MdoMemoryManagerUnit();
    MdoSkillManagerUnit();
    if ( g_MdoBootstrap.Runtime != NULL )
        xworkRuntimeRelease(g_MdoBootstrap.Runtime);
    g_MdoBootstrap.Runtime = NULL;
    MdoModelManagerUnit();
    MdoSettingsServiceUnit();
    MdoConfigUnit();
    MdoHomeUnit();
    memset(&g_MdoBootstrap, 0, sizeof(g_MdoBootstrap));
}

bool MdoBootstrapGetSnapshot(MdoBootstrapSnapshot* pSnapshot)
{
    if ( pSnapshot == NULL || pSnapshot->Size < sizeof(*pSnapshot) )
        return false;
    pSnapshot->Stage = g_MdoBootstrap.Stage;
    pSnapshot->Ready = g_MdoBootstrap.Stage == MDO_BOOTSTRAP_EXECUTOR_READY;
    pSnapshot->DefaultsBytes = g_MdoBootstrap.DefaultsBytes;
    pSnapshot->Message = g_MdoBootstrap.Message;
    pSnapshot->ModelGeneration = 0u;
    pSnapshot->ModelProviderCount = 0u;
    pSnapshot->ModelCount = 0u;
    pSnapshot->SkillGeneration = 0u;
    pSnapshot->SkillCount = 0u;
    pSnapshot->SkillDiagnosticCount = 0u;
    pSnapshot->MemoryGeneration = 0u;
    pSnapshot->GlobalMemoryCount = 0u;
    pSnapshot->WebEnabled = false;
    pSnapshot->WebDocumentCount = 0u;
    pSnapshot->WebMaxDocuments = 0u;
    pSnapshot->WebRequestsCompleted = 0u;
    pSnapshot->WebRequestsFailed = 0u;
    pSnapshot->McpGeneration = 0u;
    pSnapshot->McpServerCount = 0u;
    pSnapshot->McpDiagnosticCount = 0u;
    pSnapshot->ModuleGeneration = 0u;
    pSnapshot->ModuleCount = 0u;
    pSnapshot->ModuleToolCount = 0u;
    pSnapshot->ModuleAgentCount = 0u;
    pSnapshot->ModuleDiagnosticCount = 0u;
    pSnapshot->ScheduleGeneration = 0u;
    pSnapshot->ScheduleCount = 0u;
    pSnapshot->ScheduleDiagnosticCount = 0u;
    pSnapshot->SchedulesEnabled = false;
    pSnapshot->ScheduleExecutorAutomatic = false;
    pSnapshot->ScheduleActiveRuns = 0u;
    pSnapshot->ScheduleRunsCompleted = 0u;
    pSnapshot->ScheduleRunsFailed = 0u;
    pSnapshot->SessionGeneration = 0u;
    pSnapshot->SessionCount = 0u;
    pSnapshot->SessionDiagnosticCount = 0u;
    {
        MdoModelCatalog* pCatalog = MdoModelCatalogSnapshot();
        if ( pCatalog != NULL ) {
            pSnapshot->ModelGeneration = MdoModelManagerGeneration();
            pSnapshot->ModelProviderCount =
                MdoModelCatalogProviderCount(pCatalog);
            pSnapshot->ModelCount = MdoModelCatalogModelCount(pCatalog);
        }
        MdoModelCatalogRelease(pCatalog);
    }
    {
        MdoMemorySnapshot* pMemory = MdoMemorySnapshotCreate(
            MDO_MEMORY_GLOBAL, NULL, NULL);
        if ( pMemory != NULL ) {
            pSnapshot->MemoryGeneration = MdoMemoryManagerGeneration();
            pSnapshot->GlobalMemoryCount = MdoMemorySnapshotCount(pMemory);
        }
        MdoMemorySnapshotRelease(pMemory);
    }
    {
        MdoWebSnapshot Web;
        memset(&Web, 0, sizeof(Web));
        Web.Size = sizeof(Web);
        if ( MdoWebManagerGetSnapshot(&Web) ) {
            pSnapshot->WebEnabled = Web.Enabled;
            pSnapshot->WebDocumentCount = Web.DocumentCount;
            pSnapshot->WebMaxDocuments = Web.MaxDocuments;
            pSnapshot->WebRequestsCompleted = Web.RequestsCompleted;
            pSnapshot->WebRequestsFailed = Web.RequestsFailed;
        }
    }
    {
        MdoSkillCatalog* pCatalog = MdoSkillCatalogSnapshot();
        MdoSkillDiagnostics* pDiagnostics = MdoSkillDiagnosticsSnapshot();
        if ( pCatalog != NULL ) {
            pSnapshot->SkillGeneration = MdoSkillManagerGeneration();
            pSnapshot->SkillCount = MdoSkillCatalogCount(pCatalog);
        }
        pSnapshot->SkillDiagnosticCount =
            MdoSkillDiagnosticsCount(pDiagnostics);
        MdoSkillCatalogRelease(pCatalog);
        MdoSkillDiagnosticsRelease(pDiagnostics);
    }
    {
        MdoMcpCatalog* pCatalog = MdoMcpCatalogSnapshot();
        MdoMcpDiagnostics* pDiagnostics = MdoMcpDiagnosticsSnapshot();
        if ( pCatalog != NULL ) {
            pSnapshot->McpGeneration = MdoMcpManagerGeneration();
            pSnapshot->McpServerCount = MdoMcpCatalogCount(pCatalog);
        }
        pSnapshot->McpDiagnosticCount =
            MdoMcpDiagnosticsCount(pDiagnostics);
        MdoMcpCatalogRelease(pCatalog);
        MdoMcpDiagnosticsRelease(pDiagnostics);
    }
    {
        MdoModuleCatalog* pCatalog = MdoModuleCatalogSnapshot();
        MdoModuleDiagnostics* pDiagnostics = MdoModuleDiagnosticsSnapshot();
        if ( pCatalog != NULL ) {
            pSnapshot->ModuleGeneration = MdoModuleManagerGeneration();
            pSnapshot->ModuleCount = MdoModuleCatalogModuleCount(pCatalog);
            pSnapshot->ModuleToolCount = MdoModuleCatalogToolCount(pCatalog);
            pSnapshot->ModuleAgentCount = MdoModuleCatalogAgentCount(pCatalog);
        }
        pSnapshot->ModuleDiagnosticCount =
            MdoModuleDiagnosticsCount(pDiagnostics);
        MdoModuleCatalogRelease(pCatalog);
        MdoModuleDiagnosticsRelease(pDiagnostics);
    }
    {
        xwork_error Error;
        MdoScheduleCatalog* pCatalog = MdoScheduleCatalogSnapshot(&Error);
        if ( pCatalog != NULL ) {
            pSnapshot->ScheduleGeneration =
                MdoScheduleCatalogGeneration(pCatalog);
            pSnapshot->ScheduleCount = MdoScheduleCatalogCount(pCatalog);
            pSnapshot->ScheduleDiagnosticCount =
                MdoScheduleCatalogDiagnosticCount(pCatalog);
            pSnapshot->SchedulesEnabled = MdoScheduleManagerEnabled();
        }
        MdoScheduleCatalogRelease(pCatalog);
    }
    {
        MdoScheduleExecutorSnapshot Executor;
        memset(&Executor, 0, sizeof(Executor));
        Executor.Size = sizeof(Executor);
        if ( MdoScheduleExecutorGetSnapshot(&Executor) ) {
            pSnapshot->ScheduleExecutorAutomatic = Executor.Automatic;
            pSnapshot->ScheduleActiveRuns = Executor.ActiveRuns;
            pSnapshot->ScheduleRunsCompleted = Executor.RunsCompleted;
            pSnapshot->ScheduleRunsFailed = Executor.RunsFailed;
        }
    }
    {
        xwork_error Error;
        MdoSessionCatalog* pCatalog = MdoSessionCatalogSnapshot(&Error);
        if ( pCatalog != NULL ) {
            pSnapshot->SessionGeneration =
                MdoSessionCatalogGeneration(pCatalog);
            pSnapshot->SessionCount = MdoSessionCatalogCount(pCatalog);
            pSnapshot->SessionDiagnosticCount =
                MdoSessionCatalogDiagnosticCount(pCatalog);
        }
        MdoSessionCatalogRelease(pCatalog);
    }
    memset(&pSnapshot->Config, 0, sizeof(pSnapshot->Config));
    pSnapshot->Config.Size = sizeof(pSnapshot->Config);
    if ( g_MdoBootstrap.ConfigReady &&
         !MdoConfigGetSnapshot(&pSnapshot->Config) ) return false;
    memset(&pSnapshot->Home, 0, sizeof(pSnapshot->Home));
    pSnapshot->Home.Size = sizeof(pSnapshot->Home);
    if ( MdoHomeGetSnapshot(&pSnapshot->Home) ) return true;
    if ( g_MdoBootstrap.Stage != MDO_BOOTSTRAP_FAILED ) return false;
    xrtClearError();
    pSnapshot->Home.Persistence = MDO_PERSISTENCE_EPHEMERAL;
    pSnapshot->Home.Path = "";
    snprintf(pSnapshot->Home.Message, sizeof(pSnapshot->Home.Message), "%s",
        g_MdoBootstrap.Message);
    return true;
}

xwork_runtime* MdoBootstrapRuntime(void)
{
    return g_MdoBootstrap.Runtime;
}
