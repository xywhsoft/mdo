#include <stdio.h>
#include <string.h>

#include "internal.h"
#include "../../include/mdo/session_backup.h"

#define MDO_DOWNLOAD_CHUNK 16384u
#define MDO_DOWNLOAD_TIMEOUT_US 30000000u

typedef struct MdoDownloadJob {
    MdoApiContext Context;
    XS_HttpReq Request;
    xhttp1head Head;
    MdoSession* Session;
    bool Sent;
} MdoDownloadJob;

/* One capture/expanded document per process. No idle executor thread, queue
 * of memory-heavy bundles, or file outside portable Home is created. */
typedef struct MdoDownloadState {
    xmutex* Lock;
    xtaskpool* Pool;
    xfuture* Future;
    MdoDownloadJob* Job;
    bool Stopping;
} MdoDownloadState;

static MdoDownloadState g_MdoDownloads;

static bool MdoDownloadLive(const MdoApiContext* Context)
{
    if ( Context == NULL || Context->Request == NULL ||
         xrtCancelRequested(Context->SendCancel) ||
         xrtDeadlineExpired(Context->SendDeadline) ) return false;
    if ( Context->Request->tls != NULL )
        return xrtTlsStreamState(Context->Request->tls) == XTLS_STREAM_OPEN;
    return Context->Request->tcp != NULL &&
        xrtNetStreamState(Context->Request->tcp) == XNET_STREAM_OPEN;
}

static bool MdoDownloadFuture(xfuture* Future, MdoApiContext* Context)
{
    bool Ok = Future != NULL && xrtFutureWaitUntilCancel(Future,
        Context->SendDeadline, Context->SendCancel) == XWAIT_OK &&
        xrtFutureState(Future) == XFUTURE_RESOLVED;
    if ( !Ok && Future != NULL ) (void)xrtFutureCancel(Future);
    /* A producer still owns any unfinished async copy. Stream ownership stays
     * with the job until every waiter is detached and the job is dropped. */
    xrtFutureDestroy(Future);
    return Ok;
}

/* Runs off the network worker. Each chunk is copied by the transport before
 * returning, and drained before the next chunk. TLS must use its async entry
 * point; its synchronous Send is restricted to the stream's own worker. */
bool MdoApiDownloadSend(MdoApiContext* Context, const void* Data, size_t Bytes)
{
    size_t Offset = 0u;
    if ( Context == NULL || Context->SendDeadline == 0u ||
         (Data == NULL && Bytes != 0u) ) return false;
    while ( Offset < Bytes ) {
        size_t Chunk = Bytes - Offset;
        XS_HttpReq* Request = Context->Request;
        if ( !MdoDownloadLive(Context) ) return false;
        if ( Chunk > MDO_DOWNLOAD_CHUNK ) Chunk = MDO_DOWNLOAD_CHUNK;
        if ( Request->tls != NULL ) {
            if ( !MdoDownloadFuture(xrtTlsStreamSendAsync(Request->tls,
                    (const char*)Data + Offset, Chunk), Context) ||
                 !MdoDownloadFuture(xrtTlsStreamWaitAsync(Request->tls,
                    XTLS_STREAM_WAIT_DRAIN), Context) ) return false;
        } else {
            size_t Limit = xrtNetStreamWriteLimit(Request->tcp);
            xnetresult Result;
            if ( Limit == 0u ) return false;
            if ( Chunk > Limit ) Chunk = Limit;
            Result = xrtNetStreamSend(Request->tcp, (const char*)Data + Offset, Chunk);
            if ( Result != XNET_RESULT_OK && Result != XNET_RESULT_AGAIN ) return false;
            if ( !xrtNetStreamWait(Request->tcp, XNET_STREAM_WAIT_DRAIN,
                    Context->SendDeadline, Context->SendCancel) ) return false;
            if ( Result == XNET_RESULT_AGAIN ) continue; /* no bytes accepted */
        }
        Offset += Chunk;
    }
    return true;
}

static void MdoDownloadDrop(ptr Value, ptr UserData)
{
    MdoDownloadJob* Job = (MdoDownloadJob*)Value;
    (void)UserData;
    MdoSessionRelease(Job->Session);
    if ( Job->Request.tls != NULL ) {
        if ( Job->Sent ) (void)xrtTlsStreamClose(Job->Request.tls);
        else (void)xrtTlsStreamAbort(Job->Request.tls);
        xrtTlsStreamDestroy(Job->Request.tls);
    }
    if ( Job->Request.tcp != NULL ) {
        if ( Job->Sent ) (void)xrtNetStreamClose(Job->Request.tcp);
        else (void)xrtNetStreamAbort(Job->Request.tcp);
        xrtNetStreamDestroy(Job->Request.tcp);
    }
    xrtMutexLock(g_MdoDownloads.Lock);
    if ( g_MdoDownloads.Job == Job ) g_MdoDownloads.Job = NULL;
    xrtMutexUnlock(g_MdoDownloads.Lock);
    xrtFree(Job);
}

static bool MdoDownloadFailure(MdoApiContext* Context, const xwork_error* Error)
{
    if ( Error->eCode == XWORK_ERROR_CONTEXT ) return MdoApiReplyError(Context,
        409u, "session_capture_busy", "Session data is changing; retry the backup", NULL);
    if ( Error->eCode == XWORK_ERROR_LIMIT ) return MdoApiReplyError(Context,
        413u, "session_backup_limit", "Session backup exceeds its size or time budget", NULL);
    if ( Error->eCode == XWORK_ERROR_OUT_OF_MEMORY ) return MdoApiReplyError(Context,
        503u, "session_backup_unavailable", "Session backup resources are unavailable", NULL);
    return MdoApiReplyError(Context, 422u, "session_backup_invalid",
        "Session backup contains unreadable content or missing resource references", NULL);
}

static xtaskoutcome MdoDownloadRun(xcancel* Cancel, ptr Value, xtaskvalue* Result)
{
    MdoDownloadJob* Job = (MdoDownloadJob*)Value;
    MdoApiSessionCaptureGuard Guard;
    MdoSessionBackupLimits Limits;
    MdoSessionBackup* Backup = NULL;
    MdoSessionInfo Info;
    xwork_error Error;
    str Document = NULL;
    size_t Bytes = 0u, i;
    uint8 Digest[XRT_SHA256_SIZE];
    char Hash[65], EntityTag[96], Disposition[128];
    static const char Hex[] = "0123456789abcdef";
    (void)Result;
    Job->Context.SendCancel = Cancel;
    if ( !MdoDownloadLive(&Job->Context) ) return XTASK_CANCELLED;
    memset(&Info, 0, sizeof(Info)); Info.Size = sizeof(Info);
    if ( !MdoSessionGetInfo(Job->Session, &Info) ) {
        Job->Sent = MdoApiReplyError(&Job->Context, 503u, "session_backup_unavailable",
            "The session metadata is unavailable", NULL);
        return XTASK_FAILED;
    }
    if ( !MdoApiSessionCaptureAcquire(&Guard) ) {
        Job->Sent = MdoApiReplyError(&Job->Context, 409u, "session_capture_busy",
            "Session data is changing; retry the backup", NULL);
        return XTASK_FAILED;
    }
    MdoSessionBackupLimitsInit(&Limits);
    Limits.Deadline = Job->Context.SendDeadline;
    Backup = MdoSessionBackupCapture(Job->Session, &Limits, &Error);
    MdoApiSessionCaptureRelease(&Guard);
    MdoSessionRelease(Job->Session); Job->Session = NULL;
    if ( Backup != NULL && MdoDownloadLive(&Job->Context) )
        Document = MdoSessionBackupEncode(Backup, &Limits, &Bytes, &Error);
    MdoSessionBackupRelease(Backup);
    if ( !MdoDownloadLive(&Job->Context) ) { xrtFree(Document); return XTASK_CANCELLED; }
    if ( Document == NULL ) {
        Job->Sent = MdoDownloadFailure(&Job->Context, &Error);
        return XTASK_FAILED;
    }
    if ( !xrtSha256(Document, Bytes, Digest) ) {
        xrtFree(Document);
        Job->Sent = MdoApiReplyError(&Job->Context, 503u, "session_backup_unavailable",
            "The session backup checksum is unavailable", NULL);
        return XTASK_FAILED;
    }
    for ( i = 0u; i < sizeof(Digest); ++i ) {
        Hash[i * 2u] = Hex[Digest[i] >> 4u]; Hash[i * 2u + 1u] = Hex[Digest[i] & 15u];
    }
    Hash[64] = '\0';
    snprintf(EntityTag, sizeof(EntityTag), "\"mdo-backup-sha256-%s\"", Hash);
    snprintf(Disposition, sizeof(Disposition), "attachment; filename=\"mdo-session-%s.backup.json\"", Info.Id);
    Job->Sent = MdoApiReplyBackupDownload(&Job->Context, Document, Bytes, Disposition, EntityTag);
    xrtFree(Document);
    return Job->Sent ? XTASK_SUCCESS : XTASK_FAILED;
}

bool MdoApiDownloadsInit(void)
{
    if ( g_MdoDownloads.Lock != NULL ) return true;
    memset(&g_MdoDownloads, 0, sizeof(g_MdoDownloads));
    g_MdoDownloads.Lock = xrtMutexCreate();
    return g_MdoDownloads.Lock != NULL;
}

/* xs retains the script through RequestProc/TAKEOVER until connection close.
 * On retirement, Unit joins the executor before xs can unload TCC code. Cancel
 * wakes transport waits even if Unit is running on the network worker. Native
 * capture/read/JSON operations cooperate only at their documented boundaries.
 * Stop request admission before Unit; do not invoke Unit from this executor. */
void MdoApiDownloadsUnit(void)
{
    xmutex* Lock = g_MdoDownloads.Lock;
    xtaskpool* Pool;
    if ( Lock == NULL ) return;
    xrtMutexLock(Lock); g_MdoDownloads.Stopping = true;
    Pool = g_MdoDownloads.Pool; xrtMutexUnlock(Lock);
    if ( Pool != NULL ) {
        (void)xrtTaskPoolCancel(Pool);
        (void)xrtTaskPoolWait(Pool);
        (void)xrtTaskPoolDestroy(Pool);
    }
    xrtFutureDestroy(g_MdoDownloads.Future);
    memset(&g_MdoDownloads, 0, sizeof(g_MdoDownloads));
    xrtMutexDestroy(Lock);
}

bool MdoApiSessionBackupStart(MdoApiContext* Context, MdoSession* Session)
{
    MdoDownloadJob* Job = NULL;
    xfuture* Future = NULL;
    xtaskargs Args = {0};
    bool Busy = false;
    if ( Context == NULL || Context->Request == NULL || Context->Request->head == NULL ||
         Session == NULL || g_MdoDownloads.Lock == NULL ) goto unavailable;
    Job = (MdoDownloadJob*)xrtCalloc(1u, sizeof(*Job));
    if ( Job == NULL ) goto unavailable;
    /* Never retain a view of the request target, headers, body or host. */
    Job->Head.MethodCode = Context->Request->head->MethodCode;
    Job->Request.head = &Job->Head;
    if ( Context->Request->tls != NULL ) Job->Request.tls = xrtTlsStreamRef(Context->Request->tls);
    else if ( Context->Request->tcp != NULL ) Job->Request.tcp = xrtNetStreamRef(Context->Request->tcp);
    if ( Job->Request.tls == NULL && Job->Request.tcp == NULL ) goto unavailable;
    Job->Context.Request = &Job->Request;
    memcpy(Job->Context.RequestId, Context->RequestId, sizeof(Job->Context.RequestId));
    Job->Context.SendDeadline = xrtDeadlineAfter(MDO_DOWNLOAD_TIMEOUT_US);
    Job->Context.CloseResponse = true;
    Job->Session = Session;
    xrtMutexLock(g_MdoDownloads.Lock);
    if ( g_MdoDownloads.Stopping || g_MdoDownloads.Job != NULL ) {
        Busy = !g_MdoDownloads.Stopping;
        goto unlock;
    }
    if ( g_MdoDownloads.Pool == NULL ) {
        xtaskpoolconfig Config = {0}; Config.Threads = 1u; Config.QueueLimit = 1u;
        g_MdoDownloads.Pool = xrtTaskPoolCreate(&Config);
        if ( g_MdoDownloads.Pool == NULL ) goto unlock;
    }
    xrtFutureDestroy(g_MdoDownloads.Future); g_MdoDownloads.Future = NULL;
    g_MdoDownloads.Job = Job;
    Args.Destroy = MdoDownloadDrop;
    Future = xrtTaskSubmit(g_MdoDownloads.Pool, MdoDownloadRun, Job, &Args);
    if ( Future == NULL ) g_MdoDownloads.Job = NULL;
    else g_MdoDownloads.Future = Future;
unlock:
    xrtMutexUnlock(g_MdoDownloads.Lock);
    if ( Future != NULL ) { Context->Takeover = true; return true; }
unavailable:
    /* Rejection never transfers the stream or closes the live request. */
    if ( Job != NULL ) {
        xrtTlsStreamDestroy(Job->Request.tls); xrtNetStreamDestroy(Job->Request.tcp);
        xrtFree(Job);
    }
    MdoSessionRelease(Session);
    return MdoApiReplyError(Context, 503u,
        Busy ? "session_backup_busy" : "session_backup_unavailable",
        Busy ? "Another session backup is in progress; retry later" :
            "The session backup service is unavailable", NULL);
}
