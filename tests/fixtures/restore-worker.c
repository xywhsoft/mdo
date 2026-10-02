/* Only included in copied probe sources. No production control endpoints. */
static xatomic32 g_RestoreWorkerMode, g_RestoreWorkerReady, g_RestoreWorkerStop;
static xatomic32 g_RestoreWorkerRuns, g_RestoreWorkerSubmits, g_RestoreWorkerChecks;

void RestoreWorkerFixturePause(xcancel* Cancel, bool Drop)
{
    uint32 Mode = xrtAtomic32Load(&g_RestoreWorkerMode, XMEMORY_ACQUIRE);
    xdeadline Deadline = xrtDeadlineAfter(5000000u);
    if ( !Drop ) (void)xrtAtomic32FetchAdd(&g_RestoreWorkerRuns, 1u, XMEMORY_RELAXED);
    if ( (!Drop && Mode != 1u) || (Drop && Mode != 6u) ) return;
    xrtAtomic32Store(&g_RestoreWorkerReady, 1u, XMEMORY_RELEASE);
    while ( !xrtAtomic32Load(&g_RestoreWorkerStop, XMEMORY_ACQUIRE) &&
            !xrtCancelRequested(Cancel) && !xrtDeadlineExpired(Deadline) ) xrtSleep(1000u);
    if ( xrtDeadlineExpired(Deadline) ) (void)xrtCancelRequest(Cancel);
}

void RestoreWorkerFixtureBeforeSubmit(MdoBackupRestoreJob* Job)
{
    uint32 Mode = xrtAtomic32Load(&g_RestoreWorkerMode, XMEMORY_ACQUIRE);
    MdoSessionRestoreOperation* Operation = Job->Operation;
    uint32 Words[2] = { sizeof(uint32), UINT32_C(0x76543210) };
    xwork_error Error;
    (void)xrtAtomic32FetchAdd(&g_RestoreWorkerSubmits, 1u, XMEMORY_RELAXED);
    if ( MdoSessionRestoreExecute(&Job->Operation, NULL, (MdoSessionRestoreResult*)Words, &Error) ||
         Job->Operation != Operation || Words[0] != sizeof(uint32) || Words[1] != UINT32_C(0x76543210) ||
         MdoSessionRestoreDiscard(&Job->Operation, (MdoSessionRestoreResult*)Words, &Error) ||
         Job->Operation != Operation || Words[1] != UINT32_C(0x76543210) ) abort();
    (void)xrtAtomic32FetchAdd(&g_RestoreWorkerChecks, 1u, XMEMORY_RELAXED);
    if ( Mode == 2u ) (void)xrtCancelRequest(Job->Cancel);
    if ( Mode == 3u ) (void)xrtTaskPoolCancel(Job->Store->Pool);
    if ( Mode == 4u ) Job->Operation->Budget.Deadline = 1u;
}

bool RestoreWorkerFixtureRetire(const xfileinfo* Expected)
{
    MdoBackupRestoreStore* Store = g_MdoBackupRestores;
    char Path[256];
    xfileinfo Info;
    if ( xrtAtomic32Load(&g_RestoreWorkerMode, XMEMORY_ACQUIRE) == 5u && Store != NULL && Store->Active != NULL ) {
        snprintf(Path, sizeof(Path), "sessions/%s/%s", Store->Active->Info.Request.Binding.ProjectId,
            Store->Active->Info.Request.SessionId);
        if ( xrtRootStat(g_MdoHome.Root, Path, false, &Info) ) return false;
    }
    return RestoreFixtureRetire(Expected);
}

static bool RestoreWorkerFixtureControl(XS_HttpReq* Request)
{
    cstr Prefix = "/__fixture/restore-worker/";
    char Action[64];
    xstrview Target = Request->head->Target;
    MdoApiContext Context = {0};
    xvalue* Value;
    size_t Size = strlen(Prefix);
    unsigned Mode;
    size_t Reservations;
    if ( Target.Size < Size || memcmp(Target.Data, Prefix, Size) != 0 ) return false;
    if ( Target.Size - Size >= sizeof(Action) ) abort();
    memcpy(Action, Target.Data + Size, Target.Size - Size); Action[Target.Size - Size] = '\0';
    if ( sscanf(Action, "mode/%u", &Mode) == 1 ) {
        xrtAtomic32Store(&g_RestoreWorkerReady, 0u, XMEMORY_RELEASE);
        xrtAtomic32Store(&g_RestoreWorkerStop, 0u, XMEMORY_RELEASE);
        xrtAtomic32Store(&g_RestoreWorkerMode, Mode, XMEMORY_RELEASE);
    } else if ( strcmp(Action, "resume") == 0 ) xrtAtomic32Store(&g_RestoreWorkerStop, 1u, XMEMORY_RELEASE);
    else if ( strcmp(Action, "reset") == 0 ) {
        MdoApiBackupRestoresUnit();
        if ( !MdoApiBackupRestoresInit() ) abort();
    } else if ( strcmp(Action, "expire") == 0 && g_MdoBackupRestores != NULL ) {
        xrtMutexLock(g_MdoBackupRestores->Lock);
        if ( g_MdoBackupRestores->Slot != NULL ) g_MdoBackupRestores->Slot->Info.ExpiresAt = 1u;
        xrtMutexUnlock(g_MdoBackupRestores->Lock);
    }
    Value = xrtValueObject();
    (void)MdoApiValueSetBool(Value, "ready", xrtAtomic32Load(&g_RestoreWorkerReady, XMEMORY_ACQUIRE) != 0u);
    (void)MdoApiValueSetUInt(Value, "runs", xrtAtomic32Load(&g_RestoreWorkerRuns, XMEMORY_ACQUIRE));
    (void)MdoApiValueSetUInt(Value, "submits", xrtAtomic32Load(&g_RestoreWorkerSubmits, XMEMORY_ACQUIRE));
    (void)MdoApiValueSetUInt(Value, "checks", xrtAtomic32Load(&g_RestoreWorkerChecks, XMEMORY_ACQUIRE));
    (void)MdoApiValueSetUInt(Value, "generation", MdoSessionManagerGeneration());
    xrtMutexLock(g_MdoSessions.Lock); Reservations = g_MdoSessions.RestoreCount;
    xrtMutexUnlock(g_MdoSessions.Lock);
    (void)MdoApiValueSetUInt(Value, "reservations", Reservations);
    Context.Request = Request; snprintf(Context.RequestId, sizeof(Context.RequestId), "restore-worker-fixture");
    (void)MdoApiReplySuccessTake(&Context, 200u, Value, NULL); return true;
}
