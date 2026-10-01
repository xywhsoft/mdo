/* Copied-source fixture only. Every pause is bounded and cancellable. Stream
 * inspection owns references; no pointer to an executor's stack escapes. */
static xmutex* g_ImageFixtureLock;
static xpromise* g_ImageFixturePromise;
static xfuture* g_ImageFixtureFuture;
static xnetstream* g_ImageFixtureTcp;
static xtlsstream* g_ImageFixtureTls;
static xatomic32 g_ImageFixturePhase;
static xatomic32 g_ImageFixtureEntered;
static xatomic32 g_ImageFixtureShort;

static void ImageDownloadFixtureInit(void)
{
    g_ImageFixtureLock = xrtMutexCreate();
    xrtAtomic32Init(&g_ImageFixturePhase, 0u);
    xrtAtomic32Init(&g_ImageFixtureEntered, 0u);
    xrtAtomic32Init(&g_ImageFixtureShort, 0u);
}

static void ImageDownloadFixtureUnit(void)
{
    MdoApiImageDownloadsUnit();
    xrtPromiseDestroy(g_ImageFixturePromise);
    xrtFutureDestroy(g_ImageFixtureFuture);
    xrtNetStreamDestroy(g_ImageFixtureTcp);
    xrtTlsStreamDestroy(g_ImageFixtureTls);
    xrtMutexDestroy(g_ImageFixtureLock);
}

bool ImageDownloadFixturePause(unsigned Phase, MdoApiContext* Context)
{
    xfuture* Future;
    xdeadline Deadline;
    bool Ok;
    if ( xrtAtomic32Load(&g_ImageFixturePhase, XMEMORY_ACQUIRE) != Phase ) return true;
    xrtMutexLock(g_ImageFixtureLock);
    Future = xrtFutureRef(g_ImageFixtureFuture);
    xrtNetStreamDestroy(g_ImageFixtureTcp);
    xrtTlsStreamDestroy(g_ImageFixtureTls);
    g_ImageFixtureTcp = xrtNetStreamRef(Context->Request->tcp);
    g_ImageFixtureTls = xrtTlsStreamRef(Context->Request->tls);
    xrtMutexUnlock(g_ImageFixtureLock);
    if ( Future == NULL ) return false;
    if ( Phase == 2u && xrtAtomic32Exchange(&g_ImageFixtureShort, 0u, XMEMORY_ACQ_REL) )
        Context->SendDeadline = xrtDeadlineAfter(250000u);
    Deadline = Context->SendDeadline;
    if ( Deadline > xrtDeadlineAfter(5000000u) ) Deadline = xrtDeadlineAfter(5000000u);
    (void)xrtAtomic32FetchAdd(&g_ImageFixtureEntered, 1u, XMEMORY_ACQ_REL);
    Ok = xrtFutureWaitUntilCancel(Future, Deadline, Context->SendCancel) == XWAIT_OK &&
        xrtFutureState(Future) == XFUTURE_RESOLVED;
    xrtFutureDestroy(Future);
    return Ok;
}

static bool ImageDownloadFixtureControl(XS_HttpReq* Request)
{
    MdoApiContext Context = {0};
    xstrview Target = Request->head->Target;
    xvalue* Data;
    MdoApiSessionCaptureGuard Guard;
    bool Free, Small = true;
    size_t Pending = 0u, Count;
    static const char Prefix[] = "/__fixture/image-download/";
    if ( Target.Size < sizeof(Prefix) - 1u ||
         memcmp(Target.Data, Prefix, sizeof(Prefix) - 1u) != 0 ) return false;
    Context.Request = Request;
    snprintf(Context.RequestId, sizeof(Context.RequestId), "image-fixture");
    if ( MdoApiViewEqualText(Target, "/__fixture/image-download/hold-read") ||
         MdoApiViewEqualText(Target, "/__fixture/image-download/hold-send") ) {
        unsigned Phase = MdoApiViewEqualText(Target, "/__fixture/image-download/hold-read") ? 1u : 2u;
        xrtMutexLock(g_ImageFixtureLock);
        xrtPromiseDestroy(g_ImageFixturePromise); xrtFutureDestroy(g_ImageFixtureFuture);
        g_ImageFixturePromise = xrtPromiseCreate(&g_ImageFixtureFuture, NULL);
        xrtNetStreamDestroy(g_ImageFixtureTcp); xrtTlsStreamDestroy(g_ImageFixtureTls);
        g_ImageFixtureTcp = NULL; g_ImageFixtureTls = NULL;
        xrtMutexUnlock(g_ImageFixtureLock);
        xrtAtomic32Store(&g_ImageFixtureEntered, 0u, XMEMORY_RELEASE);
        xrtAtomic32Store(&g_ImageFixturePhase, Phase, XMEMORY_RELEASE);
    } else if ( MdoApiViewEqualText(Target, "/__fixture/image-download/resume") ) {
        xrtAtomic32Store(&g_ImageFixturePhase, 0u, XMEMORY_RELEASE);
        (void)xrtPromiseResolve(g_ImageFixturePromise, NULL);
    } else if ( MdoApiViewEqualText(Target, "/__fixture/image-download/short") ) {
        xrtAtomic32Store(&g_ImageFixtureShort, 1u, XMEMORY_RELEASE);
    } else if ( MdoApiViewEqualText(Target, "/__fixture/image-download/small-socket") ) {
        xnetstream* Transport;
        xrtMutexLock(g_ImageFixtureLock);
        Transport = xrtNetStreamRef(g_ImageFixtureTcp ? g_ImageFixtureTcp :
            xrtTlsStreamTransport(g_ImageFixtureTls));
        xrtMutexUnlock(g_ImageFixtureLock);
        Small = Transport && xrtNetSocketSet(xrtNetStreamSocket(Transport),
            XNET_OPTION_SEND_BUFFER, 4096);
        xrtNetStreamDestroy(Transport);
    } else if ( MdoApiViewEqualText(Target, "/__fixture/image-download/unit") ) {
        MdoApiImageDownloadsUnit();
        xrtAtomic32Store(&g_ImageFixturePhase, 0u, XMEMORY_RELEASE);
        if ( !MdoApiImageDownloadsInit() ) return false;
    }
    xrtMutexLock(g_MdoImageDownloads.Lock);
    Count = g_MdoImageDownloads.Count;
    xrtMutexUnlock(g_MdoImageDownloads.Lock);
    xrtMutexLock(g_ImageFixtureLock);
    if ( g_ImageFixtureTls ) Pending = xrtTlsStreamPending(g_ImageFixtureTls);
    else if ( g_ImageFixtureTcp ) Pending = xrtNetStreamPending(g_ImageFixtureTcp);
    xrtMutexUnlock(g_ImageFixtureLock);
    Free = MdoApiSessionCaptureAcquire(&Guard);
    if ( Free ) MdoApiSessionCaptureRelease(&Guard);
    Data = xrtValueObject();
    (void)MdoApiValueSetUInt(Data, "count", Count);
    (void)MdoApiValueSetUInt(Data, "pending", Pending);
    (void)MdoApiValueSetUInt(Data, "entered",
        xrtAtomic32Load(&g_ImageFixtureEntered, XMEMORY_ACQUIRE));
    (void)MdoApiValueSetBool(Data, "storage_free", Free);
    (void)MdoApiValueSetBool(Data, "small_socket", Small);
    (void)MdoApiReplySuccessTake(&Context, 200u, Data, NULL);
    return true;
}
