/* Copied-source fixture only. A single cancellable wait at a selected gate
 * makes cleanup, result publication and Unit deterministic without large or
 * repeated loads. No fixture controls are compiled into product sources. */
static xmutex* g_PreviewFixtureLock;
static xpromise* g_PreviewFixturePromise;
static xfuture* g_PreviewFixtureFuture;
static xatomic32 g_PreviewFixtureHold, g_PreviewFixtureEntered;
static xatomic32 g_PreviewFixtureShort, g_PreviewFixturePrecancel;
static xatomic32 g_PreviewFixtureRefuse, g_PreviewFixtureLockFailures;

static void BackupPreviewFixtureInit(void)
{
    g_PreviewFixtureLock = xrtMutexCreate();
    xrtAtomic32Init(&g_PreviewFixtureHold, 0u); xrtAtomic32Init(&g_PreviewFixtureEntered, 0u);
    xrtAtomic32Init(&g_PreviewFixtureShort, 0u); xrtAtomic32Init(&g_PreviewFixturePrecancel, 0u);
    xrtAtomic32Init(&g_PreviewFixtureRefuse, 0u); xrtAtomic32Init(&g_PreviewFixtureLockFailures, 0u);
}

static void BackupPreviewFixtureUnit(void)
{
    xrtPromiseDestroy(g_PreviewFixturePromise); xrtFutureDestroy(g_PreviewFixtureFuture);
    xrtMutexDestroy(g_PreviewFixtureLock);
}

void BackupPreviewFixturePause(unsigned Completed, xcancel* Cancel, xdeadline Deadline)
{
    xfuture* Future;
    if ( xrtAtomic32Load(&g_PreviewFixtureHold, XMEMORY_ACQUIRE) != Completed + 1u ) return;
    xrtMutexLock(g_PreviewFixtureLock); Future = xrtFutureRef(g_PreviewFixtureFuture);
    xrtMutexUnlock(g_PreviewFixtureLock);
    xrtAtomic32Store(&g_PreviewFixtureEntered, Completed + 1u, XMEMORY_RELEASE);
    if ( Deadline > xrtDeadlineAfter(5000000u) ) Deadline = xrtDeadlineAfter(5000000u);
    (void)xrtFutureWaitUntilCancel(Future, Deadline, Cancel); xrtFutureDestroy(Future);
}

void BackupPreviewFixtureBudget(MdoSessionBackupLimits* Limits)
{
    if ( xrtAtomic32Exchange(&g_PreviewFixtureShort, 0u, XMEMORY_ACQ_REL) != 0u ) Limits->Deadline = xrtClock();
}

bool BackupPreviewFixtureLock(xmutex* Lock)
{
    bool Ok = xrtMutexLock(Lock);
    if ( !Ok ) (void)xrtAtomic32FetchAdd(&g_PreviewFixtureLockFailures, 1u, XMEMORY_ACQ_REL);
    return Ok;
}

void BackupPreviewFixtureBeforeSubmit(xcancel* Cancel, xtaskpool* Pool)
{
    if ( xrtAtomic32Exchange(&g_PreviewFixturePrecancel, 0u, XMEMORY_ACQ_REL) != 0u )
        (void)xrtCancelRequest(Cancel);
    if ( xrtAtomic32Exchange(&g_PreviewFixtureRefuse, 0u, XMEMORY_ACQ_REL) != 0u )
        (void)xrtTaskPoolClose(Pool);
}

static bool BackupPreviewFixtureControl(XS_HttpReq* Request)
{
    static const char Prefix[] = "/__fixture/backup-preview/";
    MdoApiContext Context = {0};
    xstrview Target = Request->head->Target;
    xvalue* Value;
    size_t Pins = 0u, Retained = 0u;
    if ( Target.Size < sizeof(Prefix) - 1u || memcmp(Target.Data, Prefix, sizeof(Prefix) - 1u) != 0 ) return false;
    Context.Request = Request; snprintf(Context.RequestId, sizeof(Context.RequestId), "preview-fixture");
    if ( Target.Size == sizeof(Prefix) - 1u + 6u &&
         memcmp(Target.Data + sizeof(Prefix) - 1u, "hold-", 5u) == 0 &&
         Target.Data[Target.Size - 1u] >= '0' && Target.Data[Target.Size - 1u] <= '3' ) {
        uint32 Phase = (uint32)(Target.Data[Target.Size - 1u] - '0') + 1u;
        xrtMutexLock(g_PreviewFixtureLock);
        xrtPromiseDestroy(g_PreviewFixturePromise); xrtFutureDestroy(g_PreviewFixtureFuture);
        g_PreviewFixturePromise = xrtPromiseCreate(&g_PreviewFixtureFuture, NULL);
        xrtMutexUnlock(g_PreviewFixtureLock);
        xrtAtomic32Store(&g_PreviewFixtureEntered, 0u, XMEMORY_RELEASE);
        xrtAtomic32Store(&g_PreviewFixtureHold, Phase, XMEMORY_RELEASE);
    } else if ( MdoApiViewEqualText(Target, "/__fixture/backup-preview/resume") ) {
        xrtAtomic32Store(&g_PreviewFixtureHold, 0u, XMEMORY_RELEASE);
        (void)xrtPromiseResolve(g_PreviewFixturePromise, NULL);
    } else if ( MdoApiViewEqualText(Target, "/__fixture/backup-preview/short") )
        xrtAtomic32Store(&g_PreviewFixtureShort, 1u, XMEMORY_RELEASE);
    else if ( MdoApiViewEqualText(Target, "/__fixture/backup-preview/precancel") )
        xrtAtomic32Store(&g_PreviewFixturePrecancel, 1u, XMEMORY_RELEASE);
    else if ( MdoApiViewEqualText(Target, "/__fixture/backup-preview/refuse") )
        xrtAtomic32Store(&g_PreviewFixtureRefuse, 1u, XMEMORY_RELEASE);
    else if ( MdoApiViewEqualText(Target, "/__fixture/backup-preview/expire") ) {
        xrtMutexLock(g_MdoBackupPreviews.Lock); g_MdoBackupPreviews.Info.ExpiresAt = xrtClock();
        xrtMutexUnlock(g_MdoBackupPreviews.Lock);
    } else if ( MdoApiViewEqualText(Target, "/__fixture/backup-preview/reset") ) {
        MdoApiBackupPreviewsUnit();
        if ( !MdoApiBackupPreviewsInit() ) return false;
    }
    xrtMutexLock(g_MdoBackupUploads->Lock);
    if ( g_MdoBackupUploads->Slot != NULL ) Pins = g_MdoBackupUploads->Slot->Pins;
    xrtMutexUnlock(g_MdoBackupUploads->Lock);
    xrtMutexLock(g_MdoBackupPreviews.Lock);
    if ( g_MdoBackupPreviews.Backup != NULL ) Retained = MdoSessionBackupFileCount(g_MdoBackupPreviews.Backup);
    xrtMutexUnlock(g_MdoBackupPreviews.Lock);
    Value = xrtValueObject();
    (void)MdoApiValueSetUInt(Value, "entered", xrtAtomic32Load(&g_PreviewFixtureEntered, XMEMORY_ACQUIRE));
    (void)MdoApiValueSetUInt(Value, "upload_pins", Pins);
    (void)MdoApiValueSetUInt(Value, "retained_files", Retained);
    (void)MdoApiValueSetUInt(Value, "lock_failures", xrtAtomic32Load(&g_PreviewFixtureLockFailures, XMEMORY_ACQUIRE));
    (void)MdoApiReplySuccessTake(&Context, 200u, Value, NULL); return true;
}
