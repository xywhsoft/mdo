/* Copied-source fixture only. A single cancellable wait at a selected gate
 * makes cleanup, result publication and Unit deterministic without large or
 * repeated loads. No fixture controls are compiled into product sources. */
static xmutex* g_PreviewFixtureLock;
static xpromise* g_PreviewFixturePromise;
static xfuture* g_PreviewFixtureFuture;
static xatomic32 g_PreviewFixtureHold, g_PreviewFixtureEntered;
static xatomic32 g_PreviewFixtureShort, g_PreviewFixturePrecancel;
static xatomic32 g_PreviewFixtureRefuse, g_PreviewFixtureLockFailures;
static MdoBackupPreviewDocument* g_PreviewFixturePins[2];
static xpromise* g_PreviewFixtureReleasePromise;
static xfuture* g_PreviewFixtureReleaseFuture;
static xthread* g_PreviewFixtureReleaseThread;
static xatomic32 g_PreviewFixtureReleaseHold, g_PreviewFixtureReleaseEntered, g_PreviewFixtureReleaseDone;

static void BackupPreviewFixtureInit(void)
{
    g_PreviewFixtureLock = xrtMutexCreate();
    xrtAtomic32Init(&g_PreviewFixtureHold, 0u); xrtAtomic32Init(&g_PreviewFixtureEntered, 0u);
    xrtAtomic32Init(&g_PreviewFixtureShort, 0u); xrtAtomic32Init(&g_PreviewFixturePrecancel, 0u);
    xrtAtomic32Init(&g_PreviewFixtureRefuse, 0u); xrtAtomic32Init(&g_PreviewFixtureLockFailures, 0u);
    xrtAtomic32Init(&g_PreviewFixtureReleaseHold, 0u);
    xrtAtomic32Init(&g_PreviewFixtureReleaseEntered, 0u); xrtAtomic32Init(&g_PreviewFixtureReleaseDone, 0u);
}

static void BackupPreviewFixtureReleaseJoin(void)
{
    if ( g_PreviewFixtureReleaseThread == NULL ) return;
    if ( xrtThreadWaitFor(g_PreviewFixtureReleaseThread, 6000000u) != XWAIT_OK ) abort();
    xrtThreadDestroy(g_PreviewFixtureReleaseThread); g_PreviewFixtureReleaseThread = NULL;
}

static void BackupPreviewFixtureUnit(void)
{
    xrtAtomic32Store(&g_PreviewFixtureReleaseHold, 0u, XMEMORY_RELEASE);
    (void)xrtPromiseResolve(g_PreviewFixtureReleasePromise, NULL);
    BackupPreviewFixtureReleaseJoin();
    MdoApiBackupPreviewRelease(g_PreviewFixturePins[0]); MdoApiBackupPreviewRelease(g_PreviewFixturePins[1]);
    g_PreviewFixturePins[0] = NULL; g_PreviewFixturePins[1] = NULL;
    xrtPromiseDestroy(g_PreviewFixtureReleasePromise); xrtFutureDestroy(g_PreviewFixtureReleaseFuture);
    xrtPromiseDestroy(g_PreviewFixturePromise); xrtFutureDestroy(g_PreviewFixtureFuture);
    xrtMutexDestroy(g_PreviewFixtureLock);
}

void BackupPreviewFixtureRetirePause(void)
{
    if ( !xrtAtomic32Load(&g_PreviewFixtureReleaseHold, XMEMORY_ACQUIRE) ) return;
    xrtAtomic32Store(&g_PreviewFixtureReleaseEntered, 1u, XMEMORY_RELEASE);
    (void)xrtFutureWaitUntil(g_PreviewFixtureReleaseFuture, xrtDeadlineAfter(5000000u));
}

static int32 BackupPreviewFixtureReleaseWorker(ptr Data)
{
    MdoApiBackupPreviewRelease(Data);
    xrtAtomic32Store(&g_PreviewFixtureReleaseDone, 1u, XMEMORY_RELEASE); return 0;
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
    size_t Pins = 0u, Retained = 0u, Files = 0u, Bytes = 0u, i;
    MdoBackupPreviewAccess Access = MDO_BACKUP_PREVIEW_ACCESS_UNAVAILABLE;
    MdoBackupPreviewDocument* Document;
    MdoSessionBackupPreview Preview = {0};
    char Id[33], Hash[65] = {0}, ContentHash[65] = {0};
    uint8 Digest[XRT_SHA256_SIZE];
    xsha256 Sha;
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
        xrtMutexLock(g_MdoBackupPreviews->Lock); g_MdoBackupPreviews->Info.ExpiresAt = xrtClock();
        xrtMutexUnlock(g_MdoBackupPreviews->Lock);
    } else if ( MdoApiViewEqualText(Target, "/__fixture/backup-preview/reset") ) {
        MdoApiBackupPreviewsUnit();
        if ( !MdoApiBackupPreviewsInit() ) return false;
    } else if ( MdoApiViewEqualText(Target, "/__fixture/backup-preview/release-first") ) {
        MdoApiBackupPreviewRelease(g_PreviewFixturePins[0]); g_PreviewFixturePins[0] = NULL;
    } else if ( MdoApiViewEqualText(Target, "/__fixture/backup-preview/release-second") ) {
        MdoApiBackupPreviewRelease(g_PreviewFixturePins[1]); g_PreviewFixturePins[1] = NULL;
    } else if ( MdoApiViewEqualText(Target, "/__fixture/backup-preview/hold-release") ) {
        BackupPreviewFixtureReleaseJoin();
        xrtPromiseDestroy(g_PreviewFixtureReleasePromise); xrtFutureDestroy(g_PreviewFixtureReleaseFuture);
        g_PreviewFixtureReleasePromise = xrtPromiseCreate(&g_PreviewFixtureReleaseFuture, NULL);
        xrtAtomic32Store(&g_PreviewFixtureReleaseEntered, 0u, XMEMORY_RELEASE);
        xrtAtomic32Store(&g_PreviewFixtureReleaseDone, 0u, XMEMORY_RELEASE);
        xrtAtomic32Store(&g_PreviewFixtureReleaseHold, 1u, XMEMORY_RELEASE);
    } else if ( MdoApiViewEqualText(Target, "/__fixture/backup-preview/release-async") ) {
        if ( g_PreviewFixtureReleaseThread != NULL || g_PreviewFixturePins[0] == NULL ) abort();
        Document = g_PreviewFixturePins[0]; g_PreviewFixturePins[0] = NULL;
        g_PreviewFixtureReleaseThread = xrtThreadCreate(BackupPreviewFixtureReleaseWorker, Document, 0u);
        if ( g_PreviewFixtureReleaseThread == NULL ) abort();
    } else if ( MdoApiViewEqualText(Target, "/__fixture/backup-preview/resume-release") ) {
        xrtAtomic32Store(&g_PreviewFixtureReleaseHold, 0u, XMEMORY_RELEASE);
        (void)xrtPromiseResolve(g_PreviewFixtureReleasePromise, NULL);
    } else if ( MdoApiViewEqualText(Target, "/__fixture/backup-preview/join-release") ) {
        BackupPreviewFixtureReleaseJoin();
    } else if ( Target.Size == sizeof(Prefix) - 1u + 4u + 32u &&
                memcmp(Target.Data + sizeof(Prefix) - 1u, "pin/", 4u) == 0 ) {
        memcpy(Id, Target.Data + sizeof(Prefix) - 1u + 4u, 32u); Id[32] = '\0';
        MdoApiBackupPreviewRelease(g_PreviewFixturePins[0]);
        g_PreviewFixturePins[0] = MdoApiBackupPreviewAcquire(Id, &Access);
    } else if ( Target.Size == sizeof(Prefix) - 1u + 11u + 32u &&
                memcmp(Target.Data + sizeof(Prefix) - 1u, "pin-second/", 11u) == 0 ) {
        memcpy(Id, Target.Data + sizeof(Prefix) - 1u + 11u, 32u); Id[32] = '\0';
        MdoApiBackupPreviewRelease(g_PreviewFixturePins[1]);
        g_PreviewFixturePins[1] = MdoApiBackupPreviewAcquire(Id, &Access);
    }
    xrtMutexLock(g_MdoBackupUploads->Lock);
    if ( g_MdoBackupUploads->Slot != NULL ) Pins = g_MdoBackupUploads->Slot->Pins;
    xrtMutexUnlock(g_MdoBackupUploads->Lock);
    xrtMutexLock(g_MdoBackupPreviews->Lock);
    if ( g_MdoBackupPreviews->Slot != NULL ) Retained = MdoSessionBackupFileCount(g_MdoBackupPreviews->Slot->Backup);
    xrtMutexUnlock(g_MdoBackupPreviews->Lock);
    /* Read every immutable file through the production API, without a store
     * lock. A separate SHA covers paths and exact bytes after delete/expiry. */
    Document = g_PreviewFixturePins[0] != NULL ? g_PreviewFixturePins[0] : g_PreviewFixturePins[1];
    if ( Document != NULL ) {
        const MdoSessionBackup* Backup = MdoApiBackupPreviewData(Document);
        Files = MdoSessionBackupFileCount(Backup); Preview.Size = sizeof(Preview);
        if ( !MdoApiBackupPreviewHash(Document, Hash) || !MdoSessionBackupPreviewGet(Backup, &Preview) ) abort();
        xrtSha256Init(&Sha);
        for ( i = 0u; i < Files; ++i ) {
            MdoSessionBackupFile File;
            if ( !MdoSessionBackupFileGet(Backup, i, &File) ||
                 !xrtSha256Update(&Sha, File.Path, strlen(File.Path) + 1u) ||
                 !xrtSha256Update(&Sha, File.Data, File.Bytes) ) abort();
            Bytes += File.Bytes;
        }
        if ( !xrtSha256Final(&Sha, Digest) ) abort();
        MdoUploadHash(Digest, ContentHash);
    }
    Value = xrtValueObject();
    (void)MdoApiValueSetUInt(Value, "entered", xrtAtomic32Load(&g_PreviewFixtureEntered, XMEMORY_ACQUIRE));
    (void)MdoApiValueSetUInt(Value, "upload_pins", Pins);
    (void)MdoApiValueSetUInt(Value, "retained_files", Retained);
    (void)MdoApiValueSetUInt(Value, "lock_failures", xrtAtomic32Load(&g_PreviewFixtureLockFailures, XMEMORY_ACQUIRE));
    (void)MdoApiValueSetUInt(Value, "access", Access);
    (void)MdoApiValueSetUInt(Value, "pins_held", (g_PreviewFixturePins[0] != NULL) + (g_PreviewFixturePins[1] != NULL));
    (void)MdoApiValueSetUInt(Value, "pin_files", Files); (void)MdoApiValueSetUInt(Value, "pin_bytes", Bytes);
    (void)MdoApiValueSetString(Value, "pin_hash", Hash); (void)MdoApiValueSetString(Value, "pin_content_sha256", ContentHash);
    (void)MdoApiValueSetString(Value, "pin_title", Preview.Info.Title);
    (void)MdoApiValueSetBool(Value, "release_entered", xrtAtomic32Load(&g_PreviewFixtureReleaseEntered, XMEMORY_ACQUIRE) != 0u);
    (void)MdoApiValueSetBool(Value, "release_done", xrtAtomic32Load(&g_PreviewFixtureReleaseDone, XMEMORY_ACQUIRE) != 0u);
    (void)MdoApiReplySuccessTake(&Context, 200u, Value, NULL); return true;
}
