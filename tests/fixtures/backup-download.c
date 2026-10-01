/* Injected into an isolated copy only. Pause exactly one bounded executor
 * operation; cancellation must wake its real Future wait on the network worker
 * shutdown path. Production sources contain no pause/fault controls. */
static xmutex* g_BackupFixtureLock;
static xpromise* g_BackupFixturePromise;
static xfuture* g_BackupFixtureFuture;
static xatomic32 g_BackupFixturePhase;
static xatomic32 g_BackupFixtureEntered;
static xatomic32 g_BackupFixtureShort;
static xatomic32 g_BackupFixtureFiles;

static void BackupDownloadFixtureInit(void)
{
    g_BackupFixtureLock = xrtMutexCreate();
    xrtAtomic32Init(&g_BackupFixturePhase, 0u);
    xrtAtomic32Init(&g_BackupFixtureEntered, 0u);
    xrtAtomic32Init(&g_BackupFixtureShort, 0u);
    xrtAtomic32Init(&g_BackupFixtureFiles, 0u);
}

static void BackupDownloadFixtureUnit(void)
{
    xrtPromiseDestroy(g_BackupFixturePromise);
    xrtFutureDestroy(g_BackupFixtureFuture);
    xrtMutexDestroy(g_BackupFixtureLock);
}

uint64 BackupDownloadFixtureTimeout(void)
{
    return 30000000u;
}

bool BackupDownloadFixtureLowFiles(void)
{
    return xrtAtomic32Exchange(&g_BackupFixtureFiles, 0u, XMEMORY_ACQ_REL) != 0u;
}

bool BackupDownloadFixturePause(unsigned Phase, MdoApiContext* Context)
{
    xfuture* Future;
    xdeadline Deadline;
    bool Ok;
    if ( xrtAtomic32Load(&g_BackupFixturePhase, XMEMORY_ACQUIRE) != Phase ) return true;
    xrtMutexLock(g_BackupFixtureLock);
    Future = xrtFutureRef(g_BackupFixtureFuture);
    xrtMutexUnlock(g_BackupFixtureLock);
    if ( Future == NULL ) return false;
    /* Start the short production deadline at the send checkpoint, on the
     * owning executor thread. Slow capture/encoding cannot consume this test's
     * observation window, and there is no concurrent deadline-field write. */
    if ( Phase == 2u && xrtAtomic32Exchange(&g_BackupFixtureShort, 0u, XMEMORY_ACQ_REL) )
        Context->SendDeadline = xrtDeadlineAfter(250000u);
    Deadline = Context->SendDeadline;
    xrtAtomic32Store(&g_BackupFixtureEntered, Phase, XMEMORY_RELEASE);
    /* A fixture itself never waits more than five seconds. */
    if ( Deadline > xrtDeadlineAfter(5000000u) ) Deadline = xrtDeadlineAfter(5000000u);
    Ok = xrtFutureWaitUntilCancel(Future, Deadline, Context->SendCancel) == XWAIT_OK &&
        xrtFutureState(Future) == XFUTURE_RESOLVED;
    xrtFutureDestroy(Future);
    return Ok;
}

static bool BackupDownloadFixtureControl(XS_HttpReq* Request)
{
    MdoApiContext Context = {0};
    xstrview Target = Request->head->Target;
    xvalue* Data;
    MdoApiSessionCaptureGuard Guard;
    bool Free;
    bool Small = true;
    size_t Pending = 0u;
    static const char Prefix[] = "/__fixture/backup-download/";
    if ( Target.Size < sizeof(Prefix) - 1u ||
         memcmp(Target.Data, Prefix, sizeof(Prefix) - 1u) != 0 ) return false;
    Context.Request = Request;
    snprintf(Context.RequestId, sizeof(Context.RequestId), "backup-fixture");
    if ( MdoApiViewEqualText(Target, "/__fixture/backup-download/hold-capture") ||
         MdoApiViewEqualText(Target, "/__fixture/backup-download/hold-send") ) {
        unsigned Phase = MdoApiViewEqualText(Target, "/__fixture/backup-download/hold-capture") ? 1u : 2u;
        xrtMutexLock(g_BackupFixtureLock);
        xrtPromiseDestroy(g_BackupFixturePromise); xrtFutureDestroy(g_BackupFixtureFuture);
        g_BackupFixturePromise = xrtPromiseCreate(&g_BackupFixtureFuture, NULL);
        xrtMutexUnlock(g_BackupFixtureLock);
        xrtAtomic32Store(&g_BackupFixtureEntered, 0u, XMEMORY_RELEASE);
        xrtAtomic32Store(&g_BackupFixturePhase, Phase, XMEMORY_RELEASE);
    } else if ( MdoApiViewEqualText(Target, "/__fixture/backup-download/resume") ) {
        xrtAtomic32Store(&g_BackupFixturePhase, 0u, XMEMORY_RELEASE);
        (void)xrtPromiseResolve(g_BackupFixturePromise, NULL);
    } else if ( MdoApiViewEqualText(Target, "/__fixture/backup-download/short") ) {
        xrtAtomic32Store(&g_BackupFixtureShort, 1u, XMEMORY_RELEASE);
    } else if ( MdoApiViewEqualText(Target, "/__fixture/backup-download/file-limit") ) {
        xrtAtomic32Store(&g_BackupFixtureFiles, 1u, XMEMORY_RELEASE);
    } else if ( MdoApiViewEqualText(Target, "/__fixture/backup-download/small-socket") ) {
        xnetstream* Transport = NULL;
        xrtMutexLock(g_MdoDownloads.Lock);
        if (g_MdoDownloads.Job) {
            MdoDownloadJob* Job = g_MdoDownloads.Job;
            Transport = xrtNetStreamRef(Job->Request.tcp ? Job->Request.tcp :
                xrtTlsStreamTransport(Job->Request.tls));
        }
        xrtMutexUnlock(g_MdoDownloads.Lock);
        Small = Transport && xrtNetSocketSet(xrtNetStreamSocket(Transport),
            XNET_OPTION_SEND_BUFFER, 4096);
        xrtNetStreamDestroy(Transport);
    } else if ( MdoApiViewEqualText(Target, "/__fixture/backup-download/unit") ) {
        MdoApiDownloadsUnit();
        xrtAtomic32Store(&g_BackupFixturePhase, 0u, XMEMORY_RELEASE);
        if ( !MdoApiDownloadsInit() ) return false;
    }
    Data = xrtValueObject();
    xrtMutexLock(g_MdoDownloads.Lock);
    (void)MdoApiValueSetBool(Data, "busy", g_MdoDownloads.Job != NULL);
    if (g_MdoDownloads.Job) {
        MdoDownloadJob* Job = g_MdoDownloads.Job;
        Pending = Job->Request.tls ? xrtTlsStreamPending(Job->Request.tls) :
            xrtNetStreamPending(Job->Request.tcp);
    }
    xrtMutexUnlock(g_MdoDownloads.Lock);
    Free = MdoApiSessionCaptureAcquire(&Guard);
    if ( Free ) MdoApiSessionCaptureRelease(&Guard);
    (void)MdoApiValueSetBool(Data, "storage_free", Free);
    (void)MdoApiValueSetBool(Data, "small_socket", Small);
    (void)MdoApiValueSetUInt(Data, "pending", Pending);
    (void)MdoApiValueSetUInt(Data, "entered",
        xrtAtomic32Load(&g_BackupFixtureEntered, XMEMORY_ACQUIRE));
    (void)MdoApiReplySuccessTake(&Context, 200u, Data, NULL);
    return true;
}
