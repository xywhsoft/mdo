/* Private controls for the copied HTTP application only. Never compiled into
 * mdo.exe. Each purge runs the production coordinator on its request thread. */
#include "../schedules/internal.h"
#include "../../include/mdo/memory.h"

static MdoSession* g_MdoPurgeHeldSession;
static MdoSessionCatalog* g_MdoPurgeOldSessions;
static MdoScheduleCatalog* g_MdoPurgeOldSchedules;
static MdoMemorySnapshot* g_MdoPurgeOldMemory;
static unsigned g_MdoPurgeFault, g_MdoPurgeUnregisters, g_MdoPurgeRepeatViolations, g_MdoPurgeCleanups;
static uint64 g_MdoPurgeNativeTask;

bool MdoPurgeFixtureCommitAllowed(void) { return g_MdoPurgeFault != 1u && g_MdoPurgeFault != 2u; }
bool MdoPurgeFixtureRollbackAllowed(void) { return g_MdoPurgeFault != 2u; }
bool MdoPurgeFixtureCleanupAllowed(void)
{
    ++g_MdoPurgeCleanups;
    return g_MdoPurgeFault != 5u && (g_MdoPurgeFault != 6u || g_MdoPurgeCleanups != 1u);
}

bool MdoPurgeFixtureUnregister(xwork_runtime* Runtime, const char* Id, xwork_error* Error)
{
    ++g_MdoPurgeUnregisters;
    if ( g_MdoPurgeFault == 3u && g_MdoPurgeUnregisters == 2u ) {
        xworkErrorInit(Error);
        if ( Error != NULL ) {
            Error->eCode = XWORK_ERROR_IO;
            snprintf(Error->sMessage, sizeof(Error->sMessage), "bounded unregister failure");
        }
        return false;
    }
    return xworkRuntimeUnregisterSchedule(Runtime, Id, Error);
}

bool MdoPurgeFixtureCacheCommit(MdoSchedulePurgeGuard* Guard, xwork_error* Error)
{
    xwork_error First, Second;
    bool A = MdoSchedulesPurgeCommit(Guard, &First);
    bool B = MdoSchedulesPurgeCommit(Guard, &Second);
    if ( A != B || First.eCode != Second.eCode || strcmp(First.sMessage, Second.sMessage) != 0 )
        ++g_MdoPurgeRepeatViolations;
    if ( Error != NULL ) *Error = First;
    return A;
}

void MdoPurgeFixtureAfterStorage(bool Committed)
{
    if ( Committed && g_MdoPurgeFault == 4u ) {
        printf("project_purge_after_commit=1\n"); fflush(stdout);
        xrtSleep(8000u); /* The owner process is interrupted at this checkpoint. */
    }
}

static void MdoPurgeFixtureUnit(void)
{
    MdoSessionRelease(g_MdoPurgeHeldSession); g_MdoPurgeHeldSession = NULL;
    MdoSessionCatalogRelease(g_MdoPurgeOldSessions); g_MdoPurgeOldSessions = NULL;
    MdoScheduleCatalogRelease(g_MdoPurgeOldSchedules); g_MdoPurgeOldSchedules = NULL;
    MdoMemorySnapshotRelease(g_MdoPurgeOldMemory); g_MdoPurgeOldMemory = NULL;
}

static bool MdoPurgeFixtureState(xvalue* Data)
{
    xwork_runtime* Runtime = MdoBootstrapRuntime();
    MdoHomeSnapshot Home;
    MdoProjectLease* Owner;
    MdoScheduleCatalog* Catalog;
    xwork_error Error;
    size_t i, Owned = 0u, Enabled = 0u, Cached = 0u;
    bool Ok;
    memset(&Home, 0, sizeof(Home)); Home.Size = sizeof(Home);
    if ( !MdoHomeGetSnapshot(&Home) ) return false;
    for ( i = 0u; i < xworkRuntimeScheduleCount(Runtime); ++i ) {
        xwork_schedule_info Info;
        xworkScheduleInfoInit(&Info);
        if ( !xworkRuntimeScheduleAt(Runtime, i, &Info) ) return false;
        if ( strncmp(Info.tConfig.sScheduleId, "purge-own-", 10u) == 0 ) {
            ++Owned;
            if ( Info.tConfig.bEnabled ) ++Enabled;
        }
    }
    Catalog = MdoScheduleCatalogSnapshot(&Error);
    if ( Catalog == NULL ) return false;
    for ( i = 0u; i < MdoScheduleCatalogCount(Catalog); ++i ) {
        MdoScheduleInfo Info;
        memset(&Info, 0, sizeof(Info)); Info.Size = sizeof(Info);
        if ( !MdoScheduleCatalogAt(Catalog, i, &Info) ) { MdoScheduleCatalogRelease(Catalog); return false; }
        if ( strcmp(Info.ProjectId, "purge-probe") == 0 ) ++Cached;
    }
    MdoScheduleCatalogRelease(Catalog);
    Owner = MdoProjectLeaseAcquire("purge-probe", MDO_PROJECT_LEASE_EXCLUSIVE, &Error);
    Ok = MdoApiValueSetBool(Data, "exclusive_available", Owner != NULL) &&
        MdoApiValueSetBool(Data, "restart_required", Home.RestartRequired) &&
        MdoApiValueSetUInt(Data, "native_owned", Owned) &&
        MdoApiValueSetUInt(Data, "native_enabled", Enabled) &&
        MdoApiValueSetUInt(Data, "native_total", xworkRuntimeScheduleCount(Runtime)) &&
        MdoApiValueSetUInt(Data, "cached_owned", Cached) &&
        MdoApiValueSetUInt(Data, "session_generation", MdoSessionManagerGeneration()) &&
        MdoApiValueSetUInt(Data, "memory_generation", MdoMemoryManagerGeneration()) &&
        MdoApiValueSetUInt(Data, "schedule_generation", MdoScheduleManagerGeneration()) &&
        MdoApiValueSetUInt(Data, "unregister_calls", g_MdoPurgeUnregisters) &&
        MdoApiValueSetUInt(Data, "repeat_violations", g_MdoPurgeRepeatViolations) &&
        MdoApiValueSetUInt(Data, "old_sessions", MdoSessionCatalogCount(g_MdoPurgeOldSessions)) &&
        MdoApiValueSetUInt(Data, "old_schedules", MdoScheduleCatalogCount(g_MdoPurgeOldSchedules)) &&
        MdoApiValueSetUInt(Data, "old_memory", MdoMemorySnapshotCount(g_MdoPurgeOldMemory));
    MdoProjectLeaseRelease(Owner);
    return Ok;
}

static bool MdoPurgeFixtureControl(XS_HttpReq* Request)
{
    static const char Prefix[] = "/__fixture/project-purge/";
    MdoApiContext Context;
    xstrview Target;
    xvalue* Data;
    xwork_error Error;
    bool Ok = true;
    char Mode[64];
    size_t Length;
    if ( Request == NULL || Request->head == NULL ) return false;
    Target = Request->head->Target;
    if ( Target.Size < sizeof(Prefix) - 1u || memcmp(Target.Data, Prefix, sizeof(Prefix) - 1u) != 0 )
        return false;
    Length = Target.Size - (sizeof(Prefix) - 1u);
    if ( Length >= sizeof(Mode) ) return false;
    memcpy(Mode, Target.Data + sizeof(Prefix) - 1u, Length); Mode[Length] = '\0';
    memset(&Context, 0, sizeof(Context)); Context.Request = Request;
    snprintf(Context.RequestId, sizeof(Context.RequestId), "fixture-purge");
    Data = xrtValueObject();
    if ( strcmp(Mode, "hold-session") == 0 ) {
        g_MdoPurgeHeldSession = MdoSessionLoad("purge-probe", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", &Error);
        Ok = g_MdoPurgeHeldSession != NULL;
    } else if ( strcmp(Mode, "release-session") == 0 ) {
        MdoSessionRelease(g_MdoPurgeHeldSession); g_MdoPurgeHeldSession = NULL;
    } else if ( strcmp(Mode, "retain-snapshots") == 0 ) {
        g_MdoPurgeOldSessions = MdoSessionCatalogSnapshot(&Error);
        g_MdoPurgeOldSchedules = MdoScheduleCatalogSnapshot(&Error);
        g_MdoPurgeOldMemory = MdoMemorySnapshotCreate(MDO_MEMORY_PROJECT, "purge-probe", &Error);
        Ok = g_MdoPurgeOldSessions != NULL && g_MdoPurgeOldSchedules != NULL && g_MdoPurgeOldMemory != NULL;
    } else if ( strcmp(Mode, "native-start") == 0 || strcmp(Mode, "trigger-removed") == 0 ) {
        xwork_schedule_claim Claim;
        xworkScheduleClaimInit(&Claim);
        Ok = xworkRuntimeTriggerSchedule(MdoBootstrapRuntime(), "purge-own-a", xrtNow(), &Claim, &Error);
        if ( strcmp(Mode, "trigger-removed") == 0 ) Ok = !Ok;
        else if ( Ok ) g_MdoPurgeNativeTask = Claim.uTaskId;
    } else if ( strcmp(Mode, "native-finish") == 0 ) {
        Ok = xworkRuntimeFinishScheduledTask(MdoBootstrapRuntime(), g_MdoPurgeNativeTask,
            XWORK_RESULT_OK, "bounded native completion", &Error);
    } else if ( strcmp(Mode, "native-enable-disabled") == 0 ) {
        Ok = xworkRuntimeSetScheduleEnabled(MdoBootstrapRuntime(), "purge-own-b", true, &Error);
    } else if ( strcmp(Mode, "native-unregister-disabled") == 0 ) {
        Ok = xworkRuntimeUnregisterSchedule(MdoBootstrapRuntime(), "purge-own-b", &Error);
    } else if ( strcmp(Mode, "guard-preconditions") == 0 ) {
        MdoProjectLease* Owner = MdoProjectLeaseAcquire("purge-probe", MDO_PROJECT_LEASE_EXCLUSIVE, &Error);
        MdoProjectLease* Wrong = MdoProjectLeaseAcquire("purge-other", MDO_PROJECT_LEASE_EXCLUSIVE, &Error);
        MdoSchedulePurgeGuard* Guard;
        uint64 Generation = MdoScheduleManagerGeneration();
        Guard = MdoSchedulesPurgeBegin("purge-probe", Wrong, Generation, &Error);
        Ok = Guard == NULL && Error.eCode == XWORK_ERROR_INVALID_ARGUMENT;
        MdoSchedulesPurgeFree(Guard);
        Guard = MdoSchedulesPurgeBegin("purge-probe", Owner, Generation + 1u, &Error);
        Ok = Ok && Guard == NULL && Error.eCode == XWORK_ERROR_CONTEXT;
        MdoSchedulesPurgeFree(Guard);
        Guard = MdoSchedulesPurgeBegin("purge-probe", Owner, Generation, &Error);
        Ok = Ok && Guard != NULL && MdoSchedulesPurgeCount(Guard) == 2u;
        MdoProjectLeaseRelease(Owner);
        Owner = MdoProjectLeaseAcquire("purge-probe", MDO_PROJECT_LEASE_SHARED, &Error);
        Ok = Ok && Owner == NULL;
        MdoProjectLeaseRelease(Owner);
        MdoSchedulesPurgeFree(Guard); MdoProjectLeaseRelease(Wrong);
    } else if ( strncmp(Mode, "fault-", 6u) == 0 ) {
        if ( strcmp(Mode, "fault-rollback") == 0 ) g_MdoPurgeFault = 1u;
        else if ( strcmp(Mode, "fault-unresolved") == 0 ) g_MdoPurgeFault = 2u;
        else if ( strcmp(Mode, "fault-cache") == 0 ) g_MdoPurgeFault = 3u;
        else if ( strcmp(Mode, "fault-crash") == 0 ) g_MdoPurgeFault = 4u;
        else if ( strcmp(Mode, "fault-cleanup") == 0 ) { g_MdoPurgeFault = 5u; g_MdoPurgeCleanups = 0u; }
        else if ( strcmp(Mode, "fault-cleanup-once") == 0 ) { g_MdoPurgeFault = 6u; g_MdoPurgeCleanups = 0u; }
        else if ( strcmp(Mode, "fault-none") == 0 ) g_MdoPurgeFault = 0u;
        else Ok = false;
    } else if ( strcmp(Mode, "purge") == 0 || strcmp(Mode, "purge-no-error") == 0 || strcmp(Mode, "stale") == 0 ||
                strcmp(Mode, "missing") == 0 || strcmp(Mode, "invalid") == 0 ) {
        MdoProjectPurgeResult Result;
        xworkErrorInit(&Error);
        MdoProjectPurgeStatus Status = MdoProjectPurgeExecute(
            strcmp(Mode, "missing") == 0 ? "purge-missing" : "purge-probe",
            strcmp(Mode, "stale") == 0 ? 2u : (strcmp(Mode, "invalid") == 0 ? 0u : 1u),
            &Result, strcmp(Mode, "purge-no-error") == 0 ? NULL : &Error);
        Ok = MdoApiValueSetUInt(Data, "status", Status) &&
            MdoApiValueSetBool(Data, "committed", Result.Committed) &&
            MdoApiValueSetBool(Data, "restart_required", Result.RestartRequired) &&
            MdoApiValueSetBool(Data, "selection_removed", Result.SelectionRemoved) &&
            MdoApiValueSetBool(Data, "global_draft_removed", Result.GlobalDraftRemoved) &&
            MdoApiValueSetUInt(Data, "targets", Result.Targets) &&
            MdoApiValueSetUInt(Data, "schedules", Result.Schedules) &&
            MdoApiValueSetString(Data, "error", Error.sMessage);
    } else if ( strcmp(Mode, "frozen-write") == 0 ) {
        MdoScheduleClaim Claim;
        char* NativePath;
        MdoScheduleClaimInit(&Claim);
        Ok = !MdoHomeAtomicWrite("data/should-not-exist.json", "{}", 2u, false);
        NativePath = MdoHomeExternalPath("data/should-not-exist.json");
        Ok = Ok && NativePath == NULL; xrtFree(NativePath);
        Ok = Ok && !MdoScheduleTrigger("purge-other", 1u, xrtNow(), &Claim, &Error);
    } else if ( strcmp(Mode, "state") != 0 ) Ok = false;
    if ( Ok && strcmp(Mode, "state") == 0 ) Ok = MdoPurgeFixtureState(Data);
    if ( !Ok ) {
        xrtValueRelease(Data);
        (void)MdoApiReplyError(&Context, 503u, "fixture_purge", "Purge fixture refused", NULL);
    } else (void)MdoApiReplySuccessTake(&Context, 200u, Data, NULL);
    return true;
}
