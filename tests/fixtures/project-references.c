/* Included only by the copied HTTP probe application. Mutexes are acquired
 * and released on one fixture thread; no guard is passed between requests. */
#include "../api/project_references.h"
#include "../../include/mdo/home_purge.h"
#include <stdlib.h>

static xthread* g_MdoReferenceProbeThread;
static xatomic32 g_MdoReferenceProbeReady, g_MdoReferenceProbeStop;
static xatomic32 g_MdoReferenceProbeDone, g_MdoReferenceProbeFault;
static xatomic32 g_MdoReferenceProbeChecks, g_MdoReferenceProbeViolations;
static xatomic32 g_MdoReferenceProbeDraftWaiters, g_MdoReferenceProbeSelectionWaiters;
static bool g_MdoReferenceProbeMove, g_MdoReferenceProbeOk, g_MdoReferenceProbeCommitted;
static MdoHomePurgeTarget g_MdoReferenceProbeTargets[2];
static size_t g_MdoReferenceProbeCount;
static unsigned g_MdoReferenceProbeAutoHold;

void MdoApiReferenceProbeBeforeLock(bool Draft)
{
    (void)xrtAtomic32FetchAdd(Draft ? &g_MdoReferenceProbeDraftWaiters :
        &g_MdoReferenceProbeSelectionWaiters, 1u, XMEMORY_RELEASE);
}

bool MdoApiReferenceProbeCommitAllowed(void)
{
    return !xrtAtomic32Load(&g_MdoReferenceProbeFault, XMEMORY_ACQUIRE);
}

bool MdoApiReferenceProbeWriteCheckpoint(const char* ProjectId)
{
    MdoProjectLease* Owner;
    xwork_error Error;
    bool Blocked;
    if ( strcmp(ProjectId, "reference-probe") != 0 ) return true;
    Owner = MdoProjectLeaseAcquire(ProjectId, MDO_PROJECT_LEASE_EXCLUSIVE, &Error);
    Blocked = Owner == NULL && Error.eCode == XWORK_ERROR_CONTEXT;
    MdoProjectLeaseRelease(Owner);
    (void)xrtAtomic32FetchAdd(&g_MdoReferenceProbeChecks, 1u, XMEMORY_RELAXED);
    if ( !Blocked ) (void)xrtAtomic32FetchAdd(&g_MdoReferenceProbeViolations, 1u, XMEMORY_RELAXED);
    return Blocked;
}

static int MdoReferenceProbeCompare(const void* A, const void* B)
{
    return strcmp(((const MdoHomePurgeTarget*)A)->Path, ((const MdoHomePurgeTarget*)B)->Path);
}

static int32 MdoReferenceProbeHold(ptr Data)
{
    MdoProjectLease* Owner, *Reader;
    MdoProjectReferenceGuard* Guard = NULL;
    MdoHomePurgeTarget Targets[4];
    xwork_error Error;
    bool Exists;
    size_t Count = 0u, i;
    uint32 Waiters;
    (void)Data;
    Owner = MdoProjectLeaseAcquire("reference-probe", MDO_PROJECT_LEASE_EXCLUSIVE, &Error);
    if ( Owner != NULL ) Guard = MdoApiProjectReferencesBegin("reference-probe", Owner, &Error);
    if ( Guard == NULL ) goto failed;
    g_MdoReferenceProbeCount = MdoApiProjectReferencesCount(Guard);
    for ( i = 0u; i < g_MdoReferenceProbeCount; ++i )
        if ( !MdoApiProjectReferencesAt(Guard, i, &g_MdoReferenceProbeTargets[i]) ) goto failed;
    /* The guard independently pins exclusion, even after its caller drops
     * the original reference. A live guard may never permit another reader. */
    MdoProjectLeaseRelease(Owner); Owner = NULL;
    Reader = MdoProjectLeaseAcquire("reference-probe", MDO_PROJECT_LEASE_SHARED, &Error);
    if ( Reader != NULL ) { MdoProjectLeaseRelease(Reader); goto failed; }
    Waiters = xrtAtomic32Load(g_MdoReferenceProbeAutoHold == 1u ?
        &g_MdoReferenceProbeDraftWaiters : &g_MdoReferenceProbeSelectionWaiters, XMEMORY_ACQUIRE);
    xrtAtomic32Store(&g_MdoReferenceProbeReady, 1u, XMEMORY_RELEASE);
    for ( i = 0u; i < 800u && !xrtAtomic32Load(&g_MdoReferenceProbeStop, XMEMORY_ACQUIRE); ++i ) {
        if ( g_MdoReferenceProbeAutoHold != 0u && xrtAtomic32Load(
                g_MdoReferenceProbeAutoHold == 1u ? &g_MdoReferenceProbeDraftWaiters :
                &g_MdoReferenceProbeSelectionWaiters, XMEMORY_ACQUIRE) > Waiters ) {
            printf("reference_waiter=%u\n", g_MdoReferenceProbeAutoHold); fflush(stdout);
            /* A blocking native mutex can occupy the HTTP listener's worker.
             * Release from this owning thread after an observed real waiter;
             * do not require another HTTP request to unlock the application. */
            xrtSleep(500u);
            xrtAtomic32Store(&g_MdoReferenceProbeStop, 1u, XMEMORY_RELEASE);
            break;
        }
        xrtSleep(10u);
    }
    if ( !xrtAtomic32Load(&g_MdoReferenceProbeStop, XMEMORY_ACQUIRE) ) goto failed;
    g_MdoReferenceProbeOk = true;
    if ( g_MdoReferenceProbeMove ) {
        static const char* const Paths[] = { "projects/reference-probe.json", "sessions/reference-probe" };
        memset(Targets, 0, sizeof(Targets));
        for ( i = 0u; i < 2u; ++i ) {
            if ( !MdoHomeExternalStat(Paths[i], &Exists, &Targets[Count].Info) ) goto failed;
            if ( Exists ) snprintf(Targets[Count++].Path, sizeof(Targets[0].Path), "%s", Paths[i]);
        }
        for ( i = 0u; i < g_MdoReferenceProbeCount; ++i ) Targets[Count++] = g_MdoReferenceProbeTargets[i];
        qsort(Targets, Count, sizeof(Targets[0]), MdoReferenceProbeCompare);
        g_MdoReferenceProbeOk = MdoHomePurgeFiles("reference-probe", Targets, Count,
            &g_MdoReferenceProbeCommitted);
    }
    goto done;
failed:
    g_MdoReferenceProbeOk = false;
    xrtAtomic32Store(&g_MdoReferenceProbeReady, 2u, XMEMORY_RELEASE);
done:
    MdoApiProjectReferencesFree(Guard);
    MdoProjectLeaseRelease(Owner);
    xrtAtomic32Store(&g_MdoReferenceProbeDone, 1u, XMEMORY_RELEASE);
    return 0;
}

static void MdoApiReferenceProbeInit(void)
{
    xrtAtomic32Init(&g_MdoReferenceProbeReady, 0u);
    xrtAtomic32Init(&g_MdoReferenceProbeStop, 0u);
    xrtAtomic32Init(&g_MdoReferenceProbeDone, 0u);
    xrtAtomic32Init(&g_MdoReferenceProbeFault, 0u);
    xrtAtomic32Init(&g_MdoReferenceProbeChecks, 0u);
    xrtAtomic32Init(&g_MdoReferenceProbeViolations, 0u);
    xrtAtomic32Init(&g_MdoReferenceProbeDraftWaiters, 0u);
    xrtAtomic32Init(&g_MdoReferenceProbeSelectionWaiters, 0u);
}

static void MdoApiReferenceProbeUnit(void)
{
    xrtAtomic32Store(&g_MdoReferenceProbeStop, 1u, XMEMORY_RELEASE);
    if ( g_MdoReferenceProbeThread != NULL ) {
        (void)xrtThreadWait(g_MdoReferenceProbeThread);
        xrtThreadDestroy(g_MdoReferenceProbeThread);
        g_MdoReferenceProbeThread = NULL;
    }
}

static bool MdoApiReferenceProbeControl(XS_HttpReq* Request)
{
    static const char Prefix[] = "/__fixture/project-references/";
    MdoApiContext Context;
    MdoProjectLease* Owner, *Wrong;
    MdoProjectReferenceGuard* Guard;
    xwork_error Error;
    xvalue* Data;
    xstrview Target;
    bool Ok = true;
    size_t i, Count = 0u;
    if ( Request == NULL || Request->head == NULL ) return false;
    Target = Request->head->Target;
    if ( Target.Size < sizeof(Prefix) - 1u || memcmp(Target.Data, Prefix, sizeof(Prefix) - 1u) != 0 )
        return false;
    memset(&Context, 0, sizeof(Context)); Context.Request = Request;
    snprintf(Context.RequestId, sizeof(Context.RequestId), "fixture-references");
    Data = xrtValueObject();
    xrtMutexLock(g_MdoApiProbeLeaseLock);
    if ( MdoApiViewEqualText(Target, "/__fixture/project-references/snapshot") ) {
        Owner = MdoProjectLeaseAcquire("reference-probe", MDO_PROJECT_LEASE_EXCLUSIVE, &Error);
        Guard = Owner != NULL ? MdoApiProjectReferencesBegin("reference-probe", Owner, &Error) : NULL;
        Ok = Guard != NULL;
        if ( Ok ) {
            Count = MdoApiProjectReferencesCount(Guard);
            for ( i = 0u; i < Count; ++i ) {
                MdoHomePurgeTarget Item;
                Ok = Ok && MdoApiProjectReferencesAt(Guard, i, &Item) &&
                    MdoApiValueSetString(Data, i == 0u ? "first" : "second", Item.Path);
            }
            Ok = Ok && !MdoApiProjectReferencesAt(Guard, Count, NULL);
        }
        MdoApiProjectReferencesFree(Guard);
        MdoProjectLeaseRelease(Owner);
    } else if ( MdoApiViewEqualText(Target, "/__fixture/project-references/invalid-owner") ) {
        Guard = MdoApiProjectReferencesBegin("reference-probe", NULL, &Error);
        Ok = Guard == NULL && Error.eCode == XWORK_ERROR_INVALID_ARGUMENT;
        MdoApiProjectReferencesFree(Guard);
        Wrong = MdoProjectLeaseAcquire("other-reference-owner", MDO_PROJECT_LEASE_EXCLUSIVE, &Error);
        Guard = MdoApiProjectReferencesBegin("reference-probe", Wrong, NULL);
        Ok = Ok && Wrong != NULL && Guard == NULL;
        MdoApiProjectReferencesFree(Guard); MdoProjectLeaseRelease(Wrong);
        Wrong = MdoProjectLeaseAcquire("reference-probe", MDO_PROJECT_LEASE_SHARED, &Error);
        Guard = MdoApiProjectReferencesBegin("reference-probe", Wrong, &Error);
        Ok = Ok && Wrong != NULL && Guard == NULL && Error.eCode == XWORK_ERROR_INVALID_ARGUMENT;
        MdoApiProjectReferencesFree(Guard); MdoProjectLeaseRelease(Wrong);
    } else if ( MdoApiViewEqualText(Target, "/__fixture/project-references/hold") ||
                MdoApiViewEqualText(Target, "/__fixture/project-references/hold-draft") ||
                MdoApiViewEqualText(Target, "/__fixture/project-references/hold-selection") ||
                MdoApiViewEqualText(Target, "/__fixture/project-references/move") ||
                MdoApiViewEqualText(Target, "/__fixture/project-references/rollback") ) {
        if ( g_MdoReferenceProbeThread != NULL &&
             !xrtAtomic32Load(&g_MdoReferenceProbeDone, XMEMORY_ACQUIRE) ) Ok = false;
        else {
            MdoApiReferenceProbeUnit();
            xrtAtomic32Store(&g_MdoReferenceProbeReady, 0u, XMEMORY_RELEASE);
            xrtAtomic32Store(&g_MdoReferenceProbeDone, 0u, XMEMORY_RELEASE);
            xrtAtomic32Store(&g_MdoReferenceProbeStop, 0u, XMEMORY_RELEASE);
            g_MdoReferenceProbeMove = MdoApiViewEqualText(Target, "/__fixture/project-references/move") ||
                MdoApiViewEqualText(Target, "/__fixture/project-references/rollback");
            g_MdoReferenceProbeAutoHold = MdoApiViewEqualText(Target,
                "/__fixture/project-references/hold-draft") ? 1u :
                (MdoApiViewEqualText(Target, "/__fixture/project-references/hold-selection") ? 2u : 0u);
            xrtAtomic32Store(&g_MdoReferenceProbeFault,
                MdoApiViewEqualText(Target, "/__fixture/project-references/rollback"), XMEMORY_RELEASE);
            g_MdoReferenceProbeOk = g_MdoReferenceProbeCommitted = false;
            g_MdoReferenceProbeCount = 0u;
            g_MdoReferenceProbeThread = xrtThreadCreate(MdoReferenceProbeHold, NULL, 0u);
            Ok = g_MdoReferenceProbeThread != NULL;
        }
    } else if ( MdoApiViewEqualText(Target, "/__fixture/project-references/release") ) {
        MdoApiReferenceProbeUnit();
        xrtAtomic32Store(&g_MdoReferenceProbeFault, 0u, XMEMORY_RELEASE);
    } else if ( !MdoApiViewEqualText(Target, "/__fixture/project-references/status") ) Ok = false;
    if ( !MdoApiViewEqualText(Target, "/__fixture/project-references/snapshot") ) {
        uint32 Ready = xrtAtomic32Load(&g_MdoReferenceProbeReady, XMEMORY_ACQUIRE);
        uint32 Done = xrtAtomic32Load(&g_MdoReferenceProbeDone, XMEMORY_ACQUIRE);
        Ok = Ok && MdoApiValueSetUInt(Data, "ready", Ready) &&
            MdoApiValueSetUInt(Data, "done", Done);
        if ( Ready == 1u ) Count = g_MdoReferenceProbeCount;
        if ( Done ) Ok = Ok && MdoApiValueSetBool(Data, "ok", g_MdoReferenceProbeOk) &&
            MdoApiValueSetBool(Data, "committed", g_MdoReferenceProbeCommitted);
    }
    Ok = Ok && MdoApiValueSetUInt(Data, "count", Count) &&
        MdoApiValueSetUInt(Data, "draft_waiters", xrtAtomic32Load(&g_MdoReferenceProbeDraftWaiters, XMEMORY_ACQUIRE)) &&
        MdoApiValueSetUInt(Data, "selection_waiters", xrtAtomic32Load(&g_MdoReferenceProbeSelectionWaiters, XMEMORY_ACQUIRE)) &&
        MdoApiValueSetUInt(Data, "checks", xrtAtomic32Load(&g_MdoReferenceProbeChecks, XMEMORY_ACQUIRE)) &&
        MdoApiValueSetUInt(Data, "violations", xrtAtomic32Load(&g_MdoReferenceProbeViolations, XMEMORY_ACQUIRE));
    xrtMutexUnlock(g_MdoApiProbeLeaseLock);
    if ( !Ok ) {
        xrtValueRelease(Data);
        (void)MdoApiReplyError(&Context, 503u, "fixture_references", "Reference fixture refused", NULL);
    } else (void)MdoApiReplySuccessTake(&Context, 200u, Data, NULL);
    return true;
}
