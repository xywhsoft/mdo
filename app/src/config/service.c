#include <stdio.h>
#include <string.h>

#include "../../include/mdo/models.h"
#include "../../include/mdo/schedules.h"
#include "../../include/mdo/settings.h"
#include "../../include/mdo/web.h"

typedef struct MdoSettingsRuntime {
    bool (*ReloadModels)(void* Context);
    bool (*ReloadWeb)(void* Context);
    bool (*ReloadSchedules)(void* Context);
    void* Context;
} MdoSettingsRuntime;

typedef struct MdoSettingsState {
    xmutex* Lock;
    MdoSettingsRuntime Runtime;
    uint64 Transactions;
    uint64 Rollbacks;
    bool Initialized;
    bool Degraded;
    char LastError[256];
} MdoSettingsState;

static MdoSettingsState g_MdoSettings;

static bool MdoSettingsReloadModels(void* Context)
{
    (void)Context;
    return MdoModelManagerReload();
}

static bool MdoSettingsReloadWeb(void* Context)
{
    (void)Context;
    return MdoWebManagerReload();
}

static bool MdoSettingsReloadSchedules(void* Context)
{
    xwork_error Error;
    (void)Context;
    xworkErrorInit(&Error);
    return MdoScheduleManagerReloadSettings(&Error);
}

static void MdoSettingsResultInit(MdoSettingsResult* Result,
    MdoConfigDomain Domain)
{
    uint32 Size = Result != NULL ? Result->Size : 0u;
    if ( Result == NULL || Size < sizeof(*Result) ) return;
    memset(Result, 0, sizeof(*Result));
    Result->Size = Size;
    Result->Domain = Domain;
}

static bool MdoSettingsFail(MdoSettingsResult* Result,
    MdoSettingsStatus Status, cstr Message)
{
    if ( Result != NULL && Result->Size >= sizeof(*Result) ) {
        Result->Status = Status;
        snprintf(Result->Message, sizeof(Result->Message), "%s", Message);
    }
    return false;
}

enum {
    MDO_SETTINGS_RUNTIME_MODELS = 1u << 0,
    MDO_SETTINGS_RUNTIME_WEB = 1u << 1,
    MDO_SETTINGS_RUNTIME_SCHEDULES = 1u << 2
};

static bool MdoSettingsReloadDomain(MdoConfigDomain Domain,
    const MdoSettingsRuntime* Runtime, uint32* Attempted, uint32* Published)
{
    *Attempted = 0u;
    *Published = 0u;
    if ( Domain == MDO_CONFIG_MODELS ) {
        *Attempted |= MDO_SETTINGS_RUNTIME_MODELS;
        if ( !Runtime->ReloadModels(Runtime->Context) ) return false;
        *Published |= MDO_SETTINGS_RUNTIME_MODELS;
        return true;
    }
    if ( Domain == MDO_CONFIG_SETTINGS ) {
        *Attempted |= MDO_SETTINGS_RUNTIME_WEB;
        if ( !Runtime->ReloadWeb(Runtime->Context) ) return false;
        *Published |= MDO_SETTINGS_RUNTIME_WEB;
        *Attempted |= MDO_SETTINGS_RUNTIME_SCHEDULES;
        if ( !Runtime->ReloadSchedules(Runtime->Context) ) return false;
        *Published |= MDO_SETTINGS_RUNTIME_SCHEDULES;
        return true;
    }
    return Domain == MDO_CONFIG_PERMISSIONS;
}

static bool MdoSettingsRollbackRuntime(MdoConfigDomain Domain,
    const MdoSettingsRuntime* Runtime, uint32 Attempted, uint32 Published)
{
    bool Ok = true;
    if ( Domain == MDO_CONFIG_SETTINGS ) {
        if ( (Published & MDO_SETTINGS_RUNTIME_WEB) != 0u &&
             !Runtime->ReloadWeb(Runtime->Context) ) Ok = false;
        if ( (Attempted & MDO_SETTINGS_RUNTIME_SCHEDULES) != 0u &&
             !Runtime->ReloadSchedules(Runtime->Context) ) Ok = false;
    }
    return Ok;
}

static void MdoSettingsResultSnapshot(MdoSettingsResult* Result)
{
    MdoConfigSnapshot Config;
    MdoWebSnapshot Web;

    if ( Result == NULL || Result->Size < sizeof(*Result) ) return;
    memset(&Config, 0, sizeof(Config));
    Config.Size = sizeof(Config);
    if ( MdoConfigGetSnapshot(&Config) ) Result->Revision = Config.Revision;
    Result->ModelGeneration = MdoModelManagerGeneration();
    memset(&Web, 0, sizeof(Web));
    Web.Size = sizeof(Web);
    if ( MdoWebManagerGetSnapshot(&Web) )
        Result->WebGeneration = Web.Generation;
    Result->ScheduleGeneration = MdoScheduleManagerGeneration();
}

static bool MdoSettingsServiceInitWithRuntime(
    const MdoSettingsRuntime* Runtime)
{
    if ( Runtime == NULL || Runtime->ReloadModels == NULL ||
         Runtime->ReloadWeb == NULL || Runtime->ReloadSchedules == NULL )
        return false;
    if ( g_MdoSettings.Initialized ) return true;
    memset(&g_MdoSettings, 0, sizeof(g_MdoSettings));
    g_MdoSettings.Lock = xrtMutexCreate();
    if ( g_MdoSettings.Lock == NULL ) return false;
    g_MdoSettings.Runtime = *Runtime;
    g_MdoSettings.Initialized = true;
    return true;
}

bool MdoSettingsServiceInit(void)
{
    MdoSettingsRuntime Runtime;
    memset(&Runtime, 0, sizeof(Runtime));
    Runtime.ReloadModels = MdoSettingsReloadModels;
    Runtime.ReloadWeb = MdoSettingsReloadWeb;
    Runtime.ReloadSchedules = MdoSettingsReloadSchedules;
    return MdoSettingsServiceInitWithRuntime(&Runtime);
}

void MdoSettingsServiceUnit(void)
{
    xmutex* Lock = g_MdoSettings.Lock;
    memset(&g_MdoSettings, 0, sizeof(g_MdoSettings));
    if ( Lock != NULL ) (void)xrtMutexDestroy(Lock);
}

bool MdoSettingsServiceGetSnapshot(MdoSettingsServiceSnapshot* Snapshot)
{
    uint32 Size;
    if ( Snapshot == NULL || Snapshot->Size < sizeof(*Snapshot) ||
         g_MdoSettings.Lock == NULL || !xrtMutexLock(g_MdoSettings.Lock) )
        return false;
    Size = Snapshot->Size;
    memset(Snapshot, 0, sizeof(*Snapshot));
    Snapshot->Size = Size;
    Snapshot->Config.Size = sizeof(Snapshot->Config);
    Snapshot->Agent.Size = sizeof(Snapshot->Agent);
    Snapshot->Web.Size = sizeof(Snapshot->Web);
    if ( !MdoConfigGetSnapshot(&Snapshot->Config) ||
         !MdoConfigGetAgentSettings(&Snapshot->Agent) ||
         !MdoConfigGetWebSettings(&Snapshot->Web) ) {
        (void)xrtMutexUnlock(g_MdoSettings.Lock);
        return false;
    }
    Snapshot->Initialized = g_MdoSettings.Initialized;
    Snapshot->Degraded = g_MdoSettings.Degraded;
    Snapshot->Transactions = g_MdoSettings.Transactions;
    Snapshot->Rollbacks = g_MdoSettings.Rollbacks;
    snprintf(Snapshot->LastError, sizeof(Snapshot->LastError), "%s",
        g_MdoSettings.LastError);
    (void)xrtMutexUnlock(g_MdoSettings.Lock);
    return true;
}

static bool MdoSettingsMutate(MdoConfigDomain Domain, xstrview Document,
    bool Restore, uint64 ExpectedRevision, MdoSettingsResult* Result)
{
    MdoConfigSnapshot Before;
    MdoConfigPreview Preview;
    char* Previous = NULL;
    size_t PreviousSize = 0u;
    bool Applied = false;
    bool RuntimeOk;
    bool RollbackConfig;
    bool RollbackRuntime;
    uint32 RuntimeAttempted = 0u;
    uint32 RuntimePublished = 0u;
    bool Ok = false;

    MdoSettingsResultInit(Result, Domain);
    if ( Result == NULL || Result->Size < sizeof(*Result) ||
         Domain < MDO_CONFIG_SETTINGS || Domain >= MDO_CONFIG_DOMAIN_COUNT ||
         (!Restore && (Document.Data == NULL || Document.Size == 0u)) )
        return MdoSettingsFail(Result, MDO_SETTINGS_STATUS_ARGUMENT,
            "Invalid settings transaction request");
    if ( g_MdoSettings.Lock == NULL || !xrtMutexLock(g_MdoSettings.Lock) )
        return MdoSettingsFail(Result, MDO_SETTINGS_STATUS_UNAVAILABLE,
            "Settings transaction service is unavailable");
    if ( !g_MdoSettings.Initialized || g_MdoSettings.Degraded ) {
        (void)xrtMutexUnlock(g_MdoSettings.Lock);
        return MdoSettingsFail(Result, MDO_SETTINGS_STATUS_UNAVAILABLE,
            "Settings writes are disabled until the runtime is restarted");
    }
    memset(&Before, 0, sizeof(Before));
    Before.Size = sizeof(Before);
    if ( !MdoConfigGetSnapshot(&Before) ) goto persistence_failed;
    Result->PreviousRevision = Before.Revision;
    if ( ExpectedRevision != UINT64_MAX && ExpectedRevision != Before.Revision ) {
        MdoSettingsResultSnapshot(Result);
        (void)xrtMutexUnlock(g_MdoSettings.Lock);
        return MdoSettingsFail(Result, MDO_SETTINGS_STATUS_CONFLICT,
            "Configuration revision does not match the current state");
    }
    memset(&Preview, 0, sizeof(Preview));
    Preview.Size = sizeof(Preview);
    if ( Restore ? !MdoConfigPreviewRestore(Domain, &Preview) :
         !MdoConfigPreviewImport(Domain, Document, &Preview) ) {
        (void)xrtMutexUnlock(g_MdoSettings.Lock);
        return MdoSettingsFail(Result, MDO_SETTINGS_STATUS_VALIDATION,
            Preview.Message[0] != '\0' ? Preview.Message :
                "Configuration validation failed");
    }
    Result->Changed = Preview.Changes;
    Result->Restored = Restore;
    if ( !Preview.Changes ) {
        Result->Status = MDO_SETTINGS_STATUS_OK;
        snprintf(Result->Message, sizeof(Result->Message),
            "No configuration change was required");
        MdoSettingsResultSnapshot(Result);
        Ok = true;
        goto done;
    }
    Previous = MdoConfigExport(Domain, false, &PreviousSize);
    if ( Previous == NULL ) goto persistence_failed;
    Applied = Restore ? MdoConfigRestore(Domain) :
        MdoConfigImport(Domain, Document);
    if ( !Applied ) goto persistence_failed;
    RuntimeOk = MdoSettingsReloadDomain(Domain, &g_MdoSettings.Runtime,
        &RuntimeAttempted, &RuntimePublished);
    if ( RuntimeOk ) {
        g_MdoSettings.Transactions++;
        Result->Status = MDO_SETTINGS_STATUS_OK;
        snprintf(Result->Message, sizeof(Result->Message),
            "Configuration and runtime were updated");
        MdoSettingsResultSnapshot(Result);
        Ok = true;
        goto done;
    }
    RollbackConfig = MdoConfigImport(Domain,
        xrtStrViewN(Previous, PreviousSize));
    RollbackRuntime = RollbackConfig && MdoSettingsRollbackRuntime(Domain,
        &g_MdoSettings.Runtime, RuntimeAttempted, RuntimePublished);
    g_MdoSettings.Rollbacks++;
    if ( RollbackConfig && RollbackRuntime ) {
        snprintf(g_MdoSettings.LastError, sizeof(g_MdoSettings.LastError),
            "Runtime rejected a configuration change; the previous state was restored");
        MdoSettingsResultSnapshot(Result);
        (void)MdoSettingsFail(Result,
            MDO_SETTINGS_STATUS_RUNTIME_REJECTED,
            "Runtime rejected a configuration change; the previous state was restored");
        (void)xrtMutexUnlock(g_MdoSettings.Lock);
        xrtFree(Previous);
        return false;
    }
    g_MdoSettings.Degraded = true;
    snprintf(g_MdoSettings.LastError, sizeof(g_MdoSettings.LastError),
        "Configuration or runtime rollback failed; settings writes require a restart");
    MdoSettingsResultSnapshot(Result);
    (void)MdoSettingsFail(Result, MDO_SETTINGS_STATUS_ROLLBACK_FAILED,
        "Configuration or runtime rollback failed; settings writes require a restart");
    (void)xrtMutexUnlock(g_MdoSettings.Lock);
    xrtFree(Previous);
    return false;

persistence_failed:
    MdoSettingsResultSnapshot(Result);
    (void)xrtMutexUnlock(g_MdoSettings.Lock);
    xrtFree(Previous);
    return MdoSettingsFail(Result, MDO_SETTINGS_STATUS_PERSISTENCE,
        "Configuration storage could not be updated");
done:
    (void)xrtMutexUnlock(g_MdoSettings.Lock);
    xrtFree(Previous);
    return Ok;
}

bool MdoSettingsApply(MdoConfigDomain Domain, xstrview Document,
    uint64 ExpectedRevision, MdoSettingsResult* Result)
{
    return MdoSettingsMutate(Domain, Document, false, ExpectedRevision, Result);
}

bool MdoSettingsRestore(MdoConfigDomain Domain, uint64 ExpectedRevision,
    MdoSettingsResult* Result)
{
    return MdoSettingsMutate(Domain, (xstrview){ NULL, 0u }, true,
        ExpectedRevision, Result);
}
