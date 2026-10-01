#include <string.h>

#include "internal.h"
#include "../../include/mdo/project_lifecycle.h"
#include "../../include/mdo/sessions.h"

#define MDO_IMAGE_DOWNLOAD_WORKERS 4u
#define MDO_IMAGE_DOWNLOAD_QUEUE 16u
#define MDO_IMAGE_DOWNLOAD_TIMEOUT_US 30000000u

typedef struct MdoImageDownloadJob {
    MdoApiContext Context;
    XS_HttpReq Request;
    xhttp1head Head;
    char Project[MDO_PROJECT_ID_CAPACITY];
    char Session[MDO_SESSION_ID_CAPACITY];
    char Id[33];
    bool Sent;
} MdoImageDownloadJob;

/* Queued requests own only IDs and a connection, never expanded images. Four
 * readers cover one composer's four thumbnails without sharing the backup
 * capture slot. Active image bodies are bounded by 4 * 8 MiB; each transport
 * copies and drains at most one 16 KiB chunk at a time. */
typedef struct MdoImageDownloadState {
    xmutex* Lock;
    xtaskpool* Pool;
    size_t Count;
    bool Stopping;
} MdoImageDownloadState;

static MdoImageDownloadState g_MdoImageDownloads;

static void MdoImageDownloadDrop(ptr Value, ptr UserData)
{
    MdoImageDownloadJob* Job = (MdoImageDownloadJob*)Value;
    (void)UserData;
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
    xrtMutexLock(g_MdoImageDownloads.Lock);
    --g_MdoImageDownloads.Count;
    xrtMutexUnlock(g_MdoImageDownloads.Lock);
    xrtFree(Job);
}

static xtaskoutcome MdoImageDownloadRun(xcancel* Cancel, ptr Value,
    xtaskvalue* Result)
{
    MdoImageDownloadJob* Job = (MdoImageDownloadJob*)Value;
    MdoProjectLease* Lease;
    xwork_error Error;
    char* Data = NULL;
    size_t Bytes = 0u;
    cstr Mime = NULL;
    bool Read = false;
    (void)Result;
    Job->Context.SendCancel = Cancel;
    if ( !MdoApiDownloadLive(&Job->Context) ) return XTASK_CANCELLED;
    /* The route's lease ends when RequestProc returns. Acquire our own before
     * storage locks; purge may legitimately win while this job is queued. */
    Lease = MdoProjectLeaseAcquire(Job->Project, MDO_PROJECT_LEASE_SHARED, &Error);
    if ( Lease == NULL ) {
        Job->Sent = MdoApiReplyError(&Job->Context,
            Error.eCode == XWORK_ERROR_CONTEXT ? 409u : 503u,
            Error.eCode == XWORK_ERROR_CONTEXT ? "project_busy" : "project_unavailable",
            "Project data is unavailable for image reading", NULL);
        return XTASK_FAILED;
    }
    if ( MdoApiAttachmentLock() ) {
        Read = MdoAttachmentReadForRun(Job->Project, Job->Session, Job->Id,
            &Data, &Bytes, &Mime);
        MdoApiAttachmentUnlock();
    }
    MdoProjectLeaseRelease(Lease);
    /* Only immutable owned bytes and a static MIME string survive capture.
     * Never retain a manager/file lock, session handle or lease while sending.
     * Native file reads and mutex acquisition cooperate at these boundaries. */
    if ( !MdoApiDownloadLive(&Job->Context) ) {
        xrtFree(Data);
        return XTASK_CANCELLED;
    }
    if ( !Read ) {
        Job->Sent = MdoApiReplyError(&Job->Context, 404u, "attachment_not_found",
            "The image does not exist in this session", NULL);
        return XTASK_FAILED;
    }
    Job->Sent = MdoApiReplyImage(&Job->Context, Data, Bytes, Mime);
    xrtFree(Data);
    return Job->Sent ? XTASK_SUCCESS : XTASK_FAILED;
}

bool MdoApiImageDownloadsInit(void)
{
    if ( g_MdoImageDownloads.Lock != NULL ) return true;
    memset(&g_MdoImageDownloads, 0, sizeof(g_MdoImageDownloads));
    g_MdoImageDownloads.Lock = xrtMutexCreate();
    return g_MdoImageDownloads.Lock != NULL;
}

/* Stop request admission first. xs pins TCC code through TAKEOVER; cancel and
 * join every accepted callback before attachment/Home managers or that code
 * are retired. No caller Future is retained after a successful submission. */
void MdoApiImageDownloadsUnit(void)
{
    xmutex* Lock = g_MdoImageDownloads.Lock;
    xtaskpool* Pool;
    if ( Lock == NULL ) return;
    xrtMutexLock(Lock);
    g_MdoImageDownloads.Stopping = true;
    Pool = g_MdoImageDownloads.Pool;
    xrtMutexUnlock(Lock);
    if ( Pool != NULL ) {
        (void)xrtTaskPoolCancel(Pool);
        (void)xrtTaskPoolWait(Pool);
        (void)xrtTaskPoolDestroy(Pool);
    }
    memset(&g_MdoImageDownloads, 0, sizeof(g_MdoImageDownloads));
    xrtMutexDestroy(Lock);
}

bool MdoApiImageDownloadStart(MdoApiContext* Context, cstr Project,
    cstr Session, cstr Id)
{
    MdoImageDownloadJob* Job = NULL;
    xfuture* Future = NULL;
    xtaskargs Args = {0};
    if ( Context == NULL || Context->Request == NULL || Context->Request->head == NULL ||
         Project == NULL || Session == NULL || Id == NULL ||
         strlen(Project) >= MDO_PROJECT_ID_CAPACITY ||
         strlen(Session) >= MDO_SESSION_ID_CAPACITY || strlen(Id) != 32u ||
         g_MdoImageDownloads.Lock == NULL ) goto unavailable;
    Job = (MdoImageDownloadJob*)xrtCalloc(1u, sizeof(*Job));
    if ( Job == NULL ) goto unavailable;
    strcpy(Job->Project, Project); strcpy(Job->Session, Session); strcpy(Job->Id, Id);
    Job->Head.MethodCode = Context->Request->head->MethodCode;
    Job->Request.head = &Job->Head;
    if ( Context->Request->tls != NULL ) Job->Request.tls = xrtTlsStreamRef(Context->Request->tls);
    else if ( Context->Request->tcp != NULL ) Job->Request.tcp = xrtNetStreamRef(Context->Request->tcp);
    if ( Job->Request.tls == NULL && Job->Request.tcp == NULL ) goto unavailable;
    Job->Context.Request = &Job->Request;
    memcpy(Job->Context.RequestId, Context->RequestId, sizeof(Job->Context.RequestId));
    Job->Context.SendDeadline = xrtDeadlineAfter(MDO_IMAGE_DOWNLOAD_TIMEOUT_US);
    Job->Context.CloseResponse = true;
    xrtMutexLock(g_MdoImageDownloads.Lock);
    if ( g_MdoImageDownloads.Stopping || g_MdoImageDownloads.Count >=
            MDO_IMAGE_DOWNLOAD_WORKERS + MDO_IMAGE_DOWNLOAD_QUEUE ) goto unlock;
    if ( g_MdoImageDownloads.Pool == NULL ) {
        xtaskpoolconfig Config = {0};
        Config.Threads = MDO_IMAGE_DOWNLOAD_WORKERS;
        Config.QueueLimit = MDO_IMAGE_DOWNLOAD_QUEUE;
        g_MdoImageDownloads.Pool = xrtTaskPoolCreate(&Config);
        if ( g_MdoImageDownloads.Pool == NULL ) goto unlock;
    }
    ++g_MdoImageDownloads.Count;
    Args.Destroy = MdoImageDownloadDrop;
    Future = xrtTaskSubmit(g_MdoImageDownloads.Pool, MdoImageDownloadRun, Job, &Args);
    if ( Future == NULL ) --g_MdoImageDownloads.Count;
unlock:
    xrtMutexUnlock(g_MdoImageDownloads.Lock);
    if ( Future != NULL ) {
        xrtFutureDestroy(Future); /* executor owns the accepted job until Drop */
        Context->Takeover = true;
        return true;
    }
unavailable:
    /* Rejection never closes or consumes the original request stream. */
    if ( Job != NULL ) {
        xrtTlsStreamDestroy(Job->Request.tls); xrtNetStreamDestroy(Job->Request.tcp);
        xrtFree(Job);
    }
    return MdoApiReplyError(Context, 503u, "attachment_unavailable",
        "Image delivery is unavailable; retry later", NULL);
}
