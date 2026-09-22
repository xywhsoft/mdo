#include <stdio.h>
#include <string.h>

#include "../../include/mdo/bootstrap.h"
#include "../../include/mdo/modules.h"
#include "../../include/mdo/skills.h"
#include "../../include/mdo/version.h"

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

    (void)pHost;
    if ( g_MdoBootstrap.Stage != MDO_BOOTSTRAP_EMPTY )
        return g_MdoBootstrap.Stage == MDO_BOOTSTRAP_MODULES_READY;
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
    if ( !MdoModuleManagerInit(g_MdoBootstrap.Runtime) ) {
        MdoBootstrapFail("module manager initialization failed");
        return false;
    }
    g_MdoBootstrap.Stage = MDO_BOOTSTRAP_MODULES_READY;

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
        printf("[mdo] bootstrap ready: version=%s home=%s mode=%s defaults=%zu skills=%zu modules=%zu tools=%zu\n",
            MDO_VERSION_TEXT, Home.Path, sMode, g_MdoBootstrap.DefaultsBytes,
            iSkills, iModules, iTools);
        MdoSkillCatalogRelease(pSkills);
        MdoModuleCatalogRelease(pModules);
    }
    return true;
}

void MdoBootstrapUnit(void)
{
    MdoModuleManagerUnit();
    MdoSkillManagerUnit();
    if ( g_MdoBootstrap.Runtime != NULL )
        xworkRuntimeRelease(g_MdoBootstrap.Runtime);
    g_MdoBootstrap.Runtime = NULL;
    MdoConfigUnit();
    MdoHomeUnit();
    memset(&g_MdoBootstrap, 0, sizeof(g_MdoBootstrap));
}

bool MdoBootstrapGetSnapshot(MdoBootstrapSnapshot* pSnapshot)
{
    if ( pSnapshot == NULL || pSnapshot->Size < sizeof(*pSnapshot) )
        return false;
    pSnapshot->Stage = g_MdoBootstrap.Stage;
    pSnapshot->Ready = g_MdoBootstrap.Stage == MDO_BOOTSTRAP_MODULES_READY;
    pSnapshot->DefaultsBytes = g_MdoBootstrap.DefaultsBytes;
    pSnapshot->Message = g_MdoBootstrap.Message;
    pSnapshot->SkillGeneration = 0u;
    pSnapshot->SkillCount = 0u;
    pSnapshot->SkillDiagnosticCount = 0u;
    pSnapshot->ModuleGeneration = 0u;
    pSnapshot->ModuleCount = 0u;
    pSnapshot->ModuleToolCount = 0u;
    pSnapshot->ModuleAgentCount = 0u;
    pSnapshot->ModuleDiagnosticCount = 0u;
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
    memset(&pSnapshot->Config, 0, sizeof(pSnapshot->Config));
    pSnapshot->Config.Size = sizeof(pSnapshot->Config);
    if ( g_MdoBootstrap.ConfigReady &&
         !MdoConfigGetSnapshot(&pSnapshot->Config) ) return false;
    memset(&pSnapshot->Home, 0, sizeof(pSnapshot->Home));
    pSnapshot->Home.Size = sizeof(pSnapshot->Home);
    return MdoHomeGetSnapshot(&pSnapshot->Home);
}

xwork_runtime* MdoBootstrapRuntime(void)
{
    return g_MdoBootstrap.Runtime;
}
