/* Copied-source adapter only. One owned, cancellable native worker executes
 * the production coordinator. HTTP controls only inspect or release it. */
#include "../../include/mdo/session_restore.h"

typedef struct RestoreFixtureJob {
    MdoSessionBackup* Backup;
    MdoSessionRestoreRequest Request;
    MdoSessionRestoreResult Result;
    xwork_error Error;
    xcancel* Cancel;
    xthread* Thread;
    xatomic32 Ready, Stop, Done;
    char Mode[32];
    bool Ok;
    uint64 Before;
} RestoreFixtureJob;

static RestoreFixtureJob* g_RestoreJob;
static unsigned g_RestoreChecks;

void RestoreFixturePause(void)
{
    RestoreFixtureJob* Job = g_RestoreJob;
    xdeadline Deadline;
    if ( Job == NULL || strcmp(Job->Mode, "hold") != 0 ) return;
    Deadline = xrtDeadlineAfter(5000000u);
    xrtAtomic32Store(&Job->Ready, 1u, XMEMORY_RELEASE);
    while ( !xrtAtomic32Load(&Job->Stop, XMEMORY_ACQUIRE) &&
            !xrtCancelRequested(Job->Cancel) && !xrtDeadlineExpired(Deadline) ) xrtSleep(1000u);
    if ( xrtDeadlineExpired(Deadline) ) (void)xrtCancelRequest(Job->Cancel);
}

void RestoreFixtureCommit(void)
{
    if ( g_RestoreJob != NULL && strcmp(g_RestoreJob->Mode, "cancel-commit") == 0 )
        (void)xrtCancelRequest(g_RestoreJob->Cancel);
}

void RestoreFixtureGap(cstr Project, cstr Session)
{
    char Path[256];
    xwork_error Error;
    MdoSessionDataLease* Writer;
    xfile File;
    if ( g_RestoreJob == NULL || strcmp(g_RestoreJob->Mode, "gap-writer") != 0 ) return;
    Writer = MdoSessionDataAcquire(Project, Session, MDO_SESSION_DATA_WRITE, &Error);
    if ( Writer == NULL ) abort();
    snprintf(Path, sizeof(Path), "sessions/%s/%s/foreign.txt", Project, Session);
    File = MdoHomeOpenWrite(Path, XFILE_CREATE | XFILE_EXCLUSIVE);
    if ( File == NULL || !xrtWriteFull(File, "foreign bytes", 13u, NULL) || !xrtClose(File) ) abort();
    MdoSessionDataRelease(Writer);
}

bool RestoreFixtureClose(xfile File)
{
    bool Ok = xrtClose(File);
    if ( g_RestoreJob != NULL && strcmp(g_RestoreJob->Mode, "close-fault") == 0 ) return false;
    return Ok;
}

bool RestoreFixtureRetire(const xfileinfo* Expected)
{
    if ( g_RestoreJob != NULL && strcmp(g_RestoreJob->Mode, "gc-fault") == 0 ) {
        char Path[256];
        xfileinfo Info;
        snprintf(Path, sizeof(Path), "sessions/%s/%s", g_RestoreJob->Request.Binding.ProjectId,
            g_RestoreJob->Request.SessionId);
        if ( xrtRootStat(g_MdoHome.Root, Path, false, &Info) ) return false;
    }
    return MdoHomePurgeMove(MDO_HOME_RESTORE_DIR, MDO_HOME_RESTORE_GC, Expected) && MdoHomeRestoreGc();
}

static int32 RestoreFixtureWorker(ptr Data)
{
    RestoreFixtureJob* Job = Data;
    MdoSessionBackupLimits Limits;
    uint64 Saved = 0u;
    MdoSessionBackupLimitsInit(&Limits);
    if ( strcmp(Job->Mode, "cancel") == 0 ) (void)xrtCancelRequest(Job->Cancel);
    if ( strcmp(Job->Mode, "deadline") == 0 ) Limits.Deadline = 1u;
    Job->Before = MdoSessionManagerGeneration();
    if ( strcmp(Job->Mode, "generation") == 0 ) {
        xrtMutexLock(g_MdoSessions.Lock); Saved = g_MdoSessions.Generation;
        g_MdoSessions.Generation = UINT64_MAX; xrtMutexUnlock(g_MdoSessions.Lock);
    }
    Job->Ok = MdoSessionRestorePublish(Job->Backup, &Job->Request, &Limits, Job->Cancel, &Job->Result, &Job->Error);
    if ( strcmp(Job->Mode, "generation") == 0 ) {
        xrtMutexLock(g_MdoSessions.Lock);
        if ( g_MdoSessions.Generation != UINT64_MAX ) abort();
        g_MdoSessions.Generation = Saved; xrtMutexUnlock(g_MdoSessions.Lock);
    }
    xrtAtomic32Store(&Job->Done, 1u, XMEMORY_RELEASE);
    return 0;
}

static void RestoreFixtureUnit(void)
{
    RestoreFixtureJob* Job = g_RestoreJob;
    if ( Job == NULL ) return;
    (void)xrtCancelRequest(Job->Cancel); xrtAtomic32Store(&Job->Stop, 1u, XMEMORY_RELEASE);
    if ( Job->Thread != NULL && xrtThreadWaitFor(Job->Thread, 30000000u) != XWAIT_OK ) abort();
    xrtThreadDestroy(Job->Thread); MdoSessionBackupRelease(Job->Backup);
    xrtCancelDestroy(Job->Cancel); xrtFree(Job); g_RestoreJob = NULL;
}

#define RESTORE_CHECK(Condition) do { if ( !(Condition) ) { \
    snprintf(Error->sMessage, sizeof(Error->sMessage), "restore check line %u", (unsigned)__LINE__); return false; } \
    ++g_RestoreChecks; } while (0)

static bool RestoreFixtureReservedChecks(xwork_error* Error)
{
    RestoreFixtureJob* Job = g_RestoreJob;
    MdoProjectLease* Owner;
    MdoSessionRestoreReservation* Other;
    MdoSessionDataLease* Data;
    MdoSessionCreateOptions Create;
    MdoSession* Session;
    char Alias[33];
    uint64 Before = MdoSessionManagerGeneration();
    size_t i;
    RESTORE_CHECK(Job != NULL && xrtAtomic32Load(&Job->Ready, XMEMORY_ACQUIRE) != 0u);
    Owner = MdoProjectLeaseAcquire(Job->Request.Binding.ProjectId, MDO_PROJECT_LEASE_SHARED, Error);
    RESTORE_CHECK(Owner != NULL);
    Other = MdoSessionsRestoreReserve(Job->Request.Binding.ProjectId, Job->Request.SessionId, Owner, Error);
    RESTORE_CHECK(Other == NULL && Error->eCode == XWORK_ERROR_CONTEXT);
    memcpy(Alias, Job->Request.SessionId, sizeof(Alias));
    for ( i = 0u; i < 32u; ++i ) if ( Alias[i] >= 'a' && Alias[i] <= 'f' ) Alias[i] -= 'a' - 'A';
    for ( i = 0u; i < 2u; ++i ) {
        cstr Id = i == 0u ? Job->Request.SessionId : Alias;
        MdoSessionCreateOptionsInit(&Create); Create.ProjectId = "RESTORE-TARGET."; Create.RequestedId = Id;
        Session = MdoSessionCreate(&Create, Error);
        RESTORE_CHECK(Session == NULL && Error->eCode == XWORK_ERROR_CONTEXT);
        Session = MdoSessionLoad("RESTORE-TARGET.", Id, Error);
        RESTORE_CHECK(Session == NULL && Error->eCode == XWORK_ERROR_CONTEXT);
        Session = MdoSessionOpen("RESTORE-TARGET.", Id, NULL, Error);
        RESTORE_CHECK(Session == NULL && Error->eCode == XWORK_ERROR_CONTEXT);
        Data = MdoSessionDataAcquire("RESTORE-TARGET.", Id, MDO_SESSION_DATA_WRITE, Error);
        RESTORE_CHECK(Data == NULL && Error->eCode == XWORK_ERROR_CONTEXT);
    }
    Data = MdoSessionDataAcquire("restore-target", "unrelated", MDO_SESSION_DATA_WRITE, Error);
    RESTORE_CHECK(Data != NULL); MdoSessionDataRelease(Data);
    RESTORE_CHECK(MdoSessionManagerGeneration() == Before);
    MdoProjectLeaseRelease(Owner); xworkErrorInit(Error);
    return true;
}

static bool RestoreFixtureCapacity(xwork_error* Error)
{
    MdoSessionRestoreReservation* Slots[8] = {0}, *Overflow;
    MdoProjectLease* Owner;
    MdoSessionRestoreRequest Invalid;
    uint32 Words[2] = { sizeof(uint32), UINT32_C(0x87654321) };
    MdoSessionRestoreResult* Small = (MdoSessionRestoreResult*)Words;
    char Id[33];
    size_t i;
    uint64 Before = MdoSessionManagerGeneration();
    MdoSessionRestoreRequestInit(&Invalid);
    RESTORE_CHECK(!MdoSessionRestorePublish(NULL, &Invalid, NULL, NULL, Small, Error) &&
        Words[0] == sizeof(uint32) && Words[1] == UINT32_C(0x87654321));
    RESTORE_CHECK(!MdoProjectBindingMatches(&Invalid.Binding, &Invalid.Binding));
    Owner = MdoProjectLeaseAcquire("restore-target", MDO_PROJECT_LEASE_SHARED, Error);
    RESTORE_CHECK(Owner != NULL);
    for ( i = 0u; i < 8u; ++i ) {
        snprintf(Id, sizeof(Id), "%032u", (unsigned)i + 100u);
        Slots[i] = MdoSessionsRestoreReserve("restore-target", Id, Owner, Error);
        RESTORE_CHECK(Slots[i] != NULL);
    }
    Overflow = MdoSessionsRestoreReserve("restore-target", "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee", Owner, Error);
    RESTORE_CHECK(Overflow == NULL && Error->eCode == XWORK_ERROR_LIMIT);
    for ( i = 0u; i < 8u; ++i ) RESTORE_CHECK(MdoSessionsRestoreRelease(&Slots[i], Error) && Slots[i] == NULL);
    RESTORE_CHECK(MdoSessionManagerGeneration() == Before);
    RESTORE_CHECK(g_MdoSessions.RestoreCount == 0u);
    /* No transaction has started: an old reservation can safely survive Unit,
     * but its address/data-registry pin must not authorize the fresh manager. */
    Overflow = MdoSessionsRestoreReserve("restore-target", "ffffffffffffffffffffffffffffffff", Owner, Error);
    RESTORE_CHECK(Overflow != NULL);
    MdoSessionManagerUnit();
    RESTORE_CHECK(MdoSessionManagerInit(MdoBootstrapRuntime()));
    {
        xroot Parent = NULL;
        MdoSessionDataLease* Writer;
        RESTORE_CHECK(!MdoSessionsRestoreStorageBegin(Overflow, &Parent, Error) && Parent == NULL);
        Slots[0] = MdoSessionsRestoreReserve("restore-target", "ffffffffffffffffffffffffffffffff", Owner, Error);
        RESTORE_CHECK(Slots[0] != NULL);
        RESTORE_CHECK(MdoSessionsRestoreRelease(&Overflow, Error) && Overflow == NULL);
        Writer = MdoSessionDataAcquire("restore-target", "ffffffffffffffffffffffffffffffff", MDO_SESSION_DATA_WRITE, Error);
        RESTORE_CHECK(Writer == NULL && Error->eCode == XWORK_ERROR_CONTEXT);
        RESTORE_CHECK(MdoSessionsRestoreRelease(&Slots[0], Error));
        Writer = MdoSessionDataAcquire("restore-target", "ffffffffffffffffffffffffffffffff", MDO_SESSION_DATA_WRITE, Error);
        RESTORE_CHECK(Writer != NULL); MdoSessionDataRelease(Writer);
    }
    MdoProjectLeaseRelease(Owner); xworkErrorInit(Error);
    return true;
}

static bool RestoreFixtureControl(XS_HttpReq* Request)
{
    static const char Prefix[] = "/__fixture/restore-coordinator/";
    xstrview Target = Request->head->Target;
    MdoApiContext Context = {0};
    xvalue* Value;
    xwork_error Error;
    bool Ok = true, Done;
    if ( Target.Size < sizeof(Prefix) - 1u || memcmp(Target.Data, Prefix, sizeof(Prefix) - 1u) != 0 ) return false;
    xworkErrorInit(&Error);
    if ( Target.Size > sizeof(Prefix) - 1u + 6u && memcmp(Target.Data + sizeof(Prefix) - 1u, "start/", 6u) == 0 ) {
        char Input[96], *Slash;
        RestoreFixtureJob* Job;
        MdoSessionBackupLimits Limits;
        size_t Size = Target.Size - (sizeof(Prefix) - 1u + 6u);
        if ( Size >= sizeof(Input) ) return false;
        memcpy(Input, Target.Data + sizeof(Prefix) - 1u + 6u, Size); Input[Size] = '\0';
        Slash = strchr(Input, '/');
        if ( Slash == NULL || (size_t)(Slash - Input) >= 32u || strlen(Slash + 1u) != 32u ) return false;
        *Slash++ = '\0'; RestoreFixtureUnit();
        Job = xrtCalloc(1u, sizeof(*Job)); if ( Job == NULL ) abort();
        MdoSessionRestoreRequestInit(&Job->Request); MdoSessionRestoreResultInit(&Job->Result);
        snprintf(Job->Request.SessionId, sizeof(Job->Request.SessionId), "%s", Slash);
        Job->Request.RestoredAt = INT64_C(1790900000000000);
        snprintf(Job->Mode, sizeof(Job->Mode), "%s", Input);
        Ok = MdoProjectBindingGet("restore-target", &Job->Request.Binding, &Error);
        if ( !MdoBackupLimits(NULL, &Limits, 30000000u, &Error) ) abort();
        Job->Backup = MdoBackupClone(g_DecodeFixtureBackup, true, &Limits, NULL, &Error);
        Job->Cancel = xrtCancelCreate();
        xrtAtomic32Init(&Job->Ready, 0u); xrtAtomic32Init(&Job->Stop, 0u); xrtAtomic32Init(&Job->Done, 0u);
        g_RestoreJob = Job; BackupDecodeFixtureUnit();
        if ( !Ok || Job->Backup == NULL || Job->Cancel == NULL ) abort();
        Job->Thread = xrtThreadCreate(RestoreFixtureWorker, Job, 0u); if ( Job->Thread == NULL ) abort();
    } else if ( MdoApiViewEqualText(Target, "/__fixture/restore-coordinator/resume") )
        xrtAtomic32Store(&g_RestoreJob->Stop, 1u, XMEMORY_RELEASE);
    else if ( MdoApiViewEqualText(Target, "/__fixture/restore-coordinator/cancel") )
        (void)xrtCancelRequest(g_RestoreJob->Cancel);
    else if ( MdoApiViewEqualText(Target, "/__fixture/restore-coordinator/reserved") ) Ok = RestoreFixtureReservedChecks(&Error);
    else if ( MdoApiViewEqualText(Target, "/__fixture/restore-coordinator/capacity") ) Ok = RestoreFixtureCapacity(&Error);
    /* Acquire completion before sampling the catalog. The generation-limit
     * worker restores its temporary injected MAX before publishing Done; the
     * inverse order could combine that transient value with completed facts. */
    Done = g_RestoreJob != NULL && xrtAtomic32Load(&g_RestoreJob->Done, XMEMORY_ACQUIRE) != 0u;
    Value = xrtValueObject();
    (void)MdoApiValueSetBool(Value, "ok", Ok);
    (void)MdoApiValueSetUInt(Value, "checks", g_RestoreChecks);
    (void)MdoApiValueSetString(Value, "error", Error.sMessage);
    (void)MdoApiValueSetUInt(Value, "generation", MdoSessionManagerGeneration());
    xrtMutexLock(g_MdoSessions.Lock);
    (void)MdoApiValueSetUInt(Value, "reservations", g_MdoSessions.RestoreCount);
    xrtMutexUnlock(g_MdoSessions.Lock);
    (void)MdoApiValueSetBool(Value, "restore_ready", false);
    if ( g_RestoreJob != NULL ) {
        RestoreFixtureJob* Job = g_RestoreJob;
        (void)MdoApiValueSetBool(Value, "ready", xrtAtomic32Load(&Job->Ready, XMEMORY_ACQUIRE) != 0u);
        (void)MdoApiValueSetBool(Value, "done", Done);
        if ( Done ) {
            (void)MdoApiValueSetBool(Value, "published", Job->Ok);
            (void)MdoApiValueSetBool(Value, "committed", Job->Result.Committed);
            (void)MdoApiValueSetBool(Value, "restart", Job->Result.RestartRequired);
            (void)MdoApiValueSetUInt(Value, "before", Job->Before);
            (void)MdoApiValueSetUInt(Value, "result_generation", Job->Result.CatalogGeneration);
            (void)MdoApiValueSetUInt(Value, "code", Job->Error.eCode);
            (void)MdoApiValueSetString(Value, "error", Job->Error.sMessage);
            (void)MdoApiValueSetBool(Value, "verified", Job->Result.Stage.Verified);
            (void)MdoApiValueSetUInt(Value, "matched", Job->Result.Stage.ModelHistory.MatchedUiRecords);
            (void)MdoApiValueSetUInt(Value, "inline_images", Job->Result.Stage.Images.InlineImages);
            (void)MdoApiValueSetUInt(Value, "facts_size", Job->Result.Restore.Size);
            (void)MdoApiValueSetString(Value, "workspace", Job->Request.Binding.WorkspaceRoot);
        }
    }
    Context.Request = Request; snprintf(Context.RequestId, sizeof(Context.RequestId), "restore-fixture");
    (void)MdoApiReplySuccessTake(&Context, 200u, Value, NULL); return true;
}
