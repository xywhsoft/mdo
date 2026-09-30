/* Included only by the API probe's copied service; never shipped with mdo. */
#include "../migration/internal.h"

static MdoProjectLease* g_MdoMigrationProbeExclusive;
static xatomic32 g_MdoMigrationProbeFailure;
static xatomic32 g_MdoMigrationProbeChecks[4];
static xatomic32 g_MdoMigrationProbeViolations, g_MdoMigrationProbeRemapped;

static void MdoApiProbeMigrationInit(void)
{
    size_t i;
    xrtAtomic32Init(&g_MdoMigrationProbeFailure, 0u);
    xrtAtomic32Init(&g_MdoMigrationProbeViolations, 0u);
    xrtAtomic32Init(&g_MdoMigrationProbeRemapped, 0u);
    for ( i = 0u; i < 4u; ++i )
        xrtAtomic32Init(&g_MdoMigrationProbeChecks[i], 0u);
}

bool MdoApiProbeMigrationCheckpoint(const MdoMigrationContext* Context,
    unsigned Phase, const char* Path, xwork_error* Error)
{
    size_t i;
    bool Protected = Context != NULL && Context->ProjectCount != 0u;
    uint32 Failure = xrtAtomic32Load(&g_MdoMigrationProbeFailure,
        XMEMORY_ACQUIRE);
    for ( i = 0u; Context != NULL && i < Context->ProjectCount; ++i ) {
        const MdoMigrationProjectMap* Map = &Context->Projects[i];
        xwork_error LeaseError;
        MdoProjectLease* Exclusive = MdoProjectLeaseAcquire(Map->NewId,
            MDO_PROJECT_LEASE_EXCLUSIVE, &LeaseError);
        Protected = Protected && Map->Lease != NULL && Exclusive == NULL &&
            LeaseError.eCode == XWORK_ERROR_CONTEXT;
        MdoProjectLeaseRelease(Exclusive);
        if ( strcmp(Map->OldId, "Legacy Project") == 0 &&
             strncmp(Map->NewId, "project-", 8u) == 0 )
            xrtAtomic32Store(&g_MdoMigrationProbeRemapped, 1u, XMEMORY_RELEASE);
    }
    if ( Phase < 4u ) (void)xrtAtomic32FetchAdd(
        &g_MdoMigrationProbeChecks[Phase], 1u, XMEMORY_RELAXED);
    if ( !Protected ) (void)xrtAtomic32FetchAdd(
        &g_MdoMigrationProbeViolations, 1u, XMEMORY_RELAXED);
    if ( !Protected || (Failure == 1u && Phase == 1u && Path != NULL &&
            strcmp(Path, "migration/report.json") == 0) ||
         (Failure == 2u && Phase == 2u) ) {
        MdoMigrationError(Error, XWORK_ERROR_IO, Protected ?
            "synthetic migration publication failure" :
            "migration lost a destination project lease");
        return false;
    }
    return true;
}

static bool MdoApiProbeMigrationLeaseControl(XS_HttpReq* Request)
{
    static const char Prefix[] = "/__fixture/migration-lease/";
    xstrview Target, Action;
    MdoApiContext Context;
    xwork_error Error;
    xvalue* Data;
    const char* Id = NULL;
    char Remapped[65];
    bool Acquire = false;
    bool Ok = true;
    if ( Request == NULL || Request->head == NULL ) return false;
    Target = Request->head->Target;
    if ( Target.Size < sizeof(Prefix) - 1u ||
         memcmp(Target.Data, Prefix, sizeof(Prefix) - 1u) != 0 ) return false;
    Action = xrtStrViewN(Target.Data + sizeof(Prefix) - 1u,
        Target.Size - sizeof(Prefix) + 1u);
    memset(&Context, 0, sizeof(Context)); Context.Request = Request;
    snprintf(Context.RequestId, sizeof(Context.RequestId), "fixture-migration");
    Data = xrtValueObject();
    xrtMutexLock(g_MdoApiProbeLeaseLock);
    if ( MdoApiViewEqualText(Action, "acquire-first") ) {
        Id = "API-LEGACY."; Acquire = true; /* native filesystem alias */
    } else if ( MdoApiViewEqualText(Action, "acquire-tasks") ) {
        Id = "tasks"; Acquire = true;
    } else if ( MdoApiViewEqualText(Action, "acquire-remapped") ||
                MdoApiViewEqualText(Action, "free-remapped") ) {
        Ok = MdoMigrationIdentifier("Legacy Project", "project", Remapped,
            sizeof(Remapped));
        Id = Remapped;
        Acquire = MdoApiViewEqualText(Action, "acquire-remapped");
    } else if ( MdoApiViewEqualText(Action, "free-first") ) Id = "api-legacy";
    else if ( MdoApiViewEqualText(Action, "free-tasks") ) Id = "tasks";
    else if ( MdoApiViewEqualText(Action, "release") ) {
        MdoProjectLeaseRelease(g_MdoMigrationProbeExclusive);
        g_MdoMigrationProbeExclusive = NULL;
    } else if ( MdoApiViewEqualText(Action, "fail-report") )
        xrtAtomic32Store(&g_MdoMigrationProbeFailure, 1u, XMEMORY_RELEASE);
    else if ( MdoApiViewEqualText(Action, "fail-publish") )
        xrtAtomic32Store(&g_MdoMigrationProbeFailure, 2u, XMEMORY_RELEASE);
    else if ( MdoApiViewEqualText(Action, "clear-failure") )
        xrtAtomic32Store(&g_MdoMigrationProbeFailure, 0u, XMEMORY_RELEASE);
    else if ( MdoApiViewEqualText(Action, "checkpoint") ) {
        static const char* Names[] = { "prepare", "write", "publish", "cleanup" };
        size_t i;
        for ( i = 0u; Ok && i < 4u; ++i )
            Ok = MdoApiValueSetUInt(Data, Names[i], xrtAtomic32Load(
                &g_MdoMigrationProbeChecks[i], XMEMORY_ACQUIRE));
        Ok = Ok && MdoApiValueSetUInt(Data, "violations", xrtAtomic32Load(
            &g_MdoMigrationProbeViolations, XMEMORY_ACQUIRE)) &&
            MdoApiValueSetBool(Data, "remapped", xrtAtomic32Load(
                &g_MdoMigrationProbeRemapped, XMEMORY_ACQUIRE) != 0u);
    } else if ( MdoApiViewEqualText(Action, "apply-null-error") ) {
        MdoMigrationPreview Preview;
        MdoMigrationApplyOptions Options;
        MdoMigrationApplyResult Result;
        memset(&Preview, 0, sizeof(Preview)); Preview.Size = sizeof(Preview);
        memset(&Result, 0, sizeof(Result)); Result.Size = sizeof(Result);
        MdoMigrationApplyOptionsInit(&Options);
        Options.SourceId = MDO_MIGRATION_SOURCE_USER_HOME;
        Ok = MdoLegacyMigrationPreview(Options.SourceId, &Preview, NULL);
        Options.PreviewToken = Preview.PreviewToken;
        Ok = Ok && !MdoLegacyMigrationApply(&Options, &Result, NULL);
    } else Ok = false;
    if ( Id != NULL && Ok ) {
        MdoProjectLease* Lease = MdoProjectLeaseAcquire(Id,
            MDO_PROJECT_LEASE_EXCLUSIVE, &Error);
        if ( Acquire ) {
            Ok = g_MdoMigrationProbeExclusive == NULL && Lease != NULL;
            if ( Ok ) g_MdoMigrationProbeExclusive = Lease;
            else MdoProjectLeaseRelease(Lease);
        } else {
            Ok = MdoApiValueSetBool(Data, "available", Lease != NULL);
            MdoProjectLeaseRelease(Lease);
        }
    }
    xrtMutexUnlock(g_MdoApiProbeLeaseLock);
    if ( Ok ) (void)MdoApiReplySuccessTake(&Context, 200u, Data, NULL);
    else {
        xrtValueRelease(Data);
        (void)MdoApiReplyError(&Context, 500u, "migration_fixture_failed",
            "Migration fixture failed", NULL);
    }
    return true;
}
