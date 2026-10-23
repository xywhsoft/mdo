#include "../../include/mdo/update.h"

static xthread* g_MdoQueueThread;
static xatomic32 g_MdoQueueStopping;

static void MdoQueueResponseUnit(MdoApplicationResponse* Response)
{
    xrtFree(Response->Body); xrtFree(Response->ContentType); xrtFree(Response->Revision);
    memset(Response, 0, sizeof(*Response));
}

static void MdoQueueDispatchSession(const MdoSessionInfo* Session)
{
    char Path[MDO_SESSION_PATH_CAPACITY];
    MdoQueue Queue;
    MdoRunInfo Active = {0}, Last = {0};
    xwork_error Error;
    MdoProjectLease* Lease = MdoProjectLeaseAcquire(Session->ProjectId, MDO_PROJECT_LEASE_SHARED, NULL);
    if ( !Lease ) return;
    MdoRunSnapshot* Runs = MdoRunSnapshotCreate(&Error);
    if ( !Runs ) { MdoProjectLeaseRelease(Lease); return; }
    for ( size_t i = 0u; i < MdoRunSnapshotCount(Runs); ++i ) {
        MdoRunInfo Run = {0}; Run.Size = sizeof(Run);
        if ( MdoRunSnapshotAt(Runs, i, &Run) && !Run.Terminal &&
             !strcmp(Run.ProjectId, Session->ProjectId) && !strcmp(Run.SessionId, Session->Id) ) Active = Run;
    }
    if ( !MdoQueueRunPath(Path, Session->ProjectId, Session->Id) ) goto done;
    xrtMutexLock(g_MdoQueueLock);
    bool Ok = MdoQueueRead(Path, Session->ProjectId, Session->Id, &Queue);
    xrtMutexUnlock(g_MdoQueueLock);
    if ( !Ok ) goto done;
    for ( size_t i = 0u; i < Queue.Count; ++i ) if ( Queue.Items[i].State == MDO_QUEUE_EDITING ) {
        (void)MdoQueueFinishEdit(Session->ProjectId, Session->Id, &Queue.Items[i]);
        MdoQueueRelease(&Queue); goto done;
    }
    if ( !Queue.Managed || !Queue.Count || Queue.Paused ) { MdoQueueRelease(&Queue); goto done; }
    /* Refresh after reading run state. All changes below are under the same
     * queue lock as reorder, extraction and HTTP run claims. */
    MdoQueueRelease(&Queue);
    xrtMutexLock(g_MdoQueueLock);
    Ok = MdoQueueRead(Path, Session->ProjectId, Session->Id, &Queue);
    if ( !Ok || !Queue.Managed || !Queue.Count || Queue.Paused ) {
        xrtMutexUnlock(g_MdoQueueLock); MdoQueueRelease(&Queue); goto done;
    }
    MdoQueueItem* Head = &Queue.Items[0];
    uint64 Revision = Queue.Revision;
    bool Start = false;
    char Cancel[MDO_RUN_ID_CAPACITY] = {0};
    if ( Head->State == MDO_QUEUE_SENDING && Head->RunId[0] ) {
        snprintf(Queue.LastRunId, sizeof(Queue.LastRunId), "%s", Head->RunId);
        Queue.CancelRunId[0] = '\0';
        MdoQueueRemove(&Queue, 0u);
        (void)MdoQueueWrite(Path, &Queue);
    } else if ( Head->State == MDO_QUEUE_SENDING ) {
        Queue.Paused = true;
        snprintf(Queue.Error, sizeof(Queue.Error), "%s", Head->StartClaimed ? "queue_run_starting" : "queue_confirmation_pending");
        (void)MdoQueueWrite(Path, &Queue);
    } else if ( Head->State == MDO_QUEUE_PENDING ) {
        if ( Active.Id[0] ) {
            bool Changed = strcmp(Queue.LastRunId, Active.Id) != 0;
            snprintf(Queue.LastRunId, sizeof(Queue.LastRunId), "%s", Active.Id);
            if ( Head->Priority && !Active.CancelRequested ) {
                if ( Queue.CancelRunId[0] && strcmp(Queue.CancelRunId, Active.Id) ) {
                    Queue.Paused = true;
                    snprintf(Queue.Error, sizeof(Queue.Error), "queue_run_changed");
                } else {
                    snprintf(Queue.CancelRunId, sizeof(Queue.CancelRunId), "%s", Active.Id);
                    snprintf(Cancel, sizeof(Cancel), "%s", Active.Id);
                }
                Changed = true;
            }
            if ( Changed && !MdoQueueWrite(Path, &Queue) ) Cancel[0] = '\0';
        } else {
            Last.Size = sizeof(Last);
            bool Found = Queue.LastRunId[0] && MdoRunSnapshotFind(Runs, Queue.LastRunId, &Last);
            if ( Queue.LastRunId[0] && (!Found || !Last.Terminal ||
                 (Last.State != XWORK_RUN_SUCCEEDED && !Head->Priority)) ) {
                Queue.Paused = true;
                snprintf(Queue.Error, sizeof(Queue.Error), "%s", !Found ? "queue_previous_run_unknown" :
                    Last.State == XWORK_RUN_CANCELLED ? "queue_stopped" : "queue_previous_run_failed");
                (void)MdoQueueWrite(Path, &Queue);
            } else {
                Head->State = MDO_QUEUE_SENDING;
                Queue.LastRunId[0] = '\0';
                Start = MdoQueueWrite(Path, &Queue);
            }
        }
    }
    xrtMutexUnlock(g_MdoQueueLock);
    if ( Cancel[0] ) { MdoRunInfo Info = {0}; Info.Size = sizeof(Info); (void)MdoRunCancel(Cancel, &Info, &Error); }
    if ( Start ) {
        /* Reuse the complete production start path: image capability/ownership,
         * profile, exact queue claim, prepared receipt and uncertain admission. */
        xvalue* Body = xrtValueObject();
        bool Built = Body && MdoApiValueSetString(Body, "prompt", Head->Text) &&
            MdoApiValueSetString(Body, "queue_item_id", Head->Id) &&
            (!Head->AttachmentCount || MdoAttachmentIdsWriteValue(Body, Head->Attachments, Head->AttachmentCount));
        xhttp1head Http = {0}; Http.MethodCode = XHTTP_METHOD_POST;
        xhttpfield ContentType = { XRT_STR_LITERAL("Content-Type"), XRT_STR_LITERAL("application/json") };
        Http.Fields = &ContentType; Http.FieldCount = 1u;
        XS_HttpReq Request = {0}; Request.head = &Http;
        MdoApplicationResponse Response = {0};
        MdoApiContext Context = {0}; Context.Request = &Request;
        Context.ParamCount = 2u; Context.Params[0] = xrtStrView(Session->ProjectId);
        Context.Params[1] = xrtStrView(Session->Id); Context.ApplicationBody = Body;
        Context.ApplicationResponse = &Response;
        if ( Built ) (void)MdoApiRunStartRoute(&Context);
        xvalue* Reply = Response.Body ? xrtJsonParse(xrtStrViewN(Response.Body, Response.Bytes)) : NULL;
        const xvalue* Data = xrtValueObjectGet(Reply, XRT_STR_LITERAL("data"));
        const xvalue* Failure = xrtValueObjectGet(Reply, XRT_STR_LITERAL("error"));
        xstrview Code = {0}, RunId = {0};
        (void)MdoQueueString(Failure, "code", &Code);
        (void)MdoQueueString(Data, "id", &RunId);
        xrtMutexLock(g_MdoQueueLock);
        MdoQueue After;
        if ( MdoQueueRead(Path, Session->ProjectId, Session->Id, &After) ) {
            size_t Index = MdoQueueFind(&After, Head->Id);
            if ( Index != SIZE_MAX && After.Items[Index].State == MDO_QUEUE_SENDING ) {
                MdoQueueItem* Sent = &After.Items[Index];
                if ( Response.Status == 202u && Sent->RunId[0] && RunId.Size ) {
                    snprintf(After.LastRunId, sizeof(After.LastRunId), "%s", Sent->RunId);
                    After.CancelRunId[0] = '\0';
                    MdoQueueRemove(&After, Index);
                } else if ( Code.Size == 12u && !memcmp(Code.Data, "session_busy", 12u) && !Sent->StartClaimed )
                    Sent->State = MDO_QUEUE_PENDING;
                else {
                    After.Paused = true;
                    if ( !Sent->StartClaimed && !Sent->RunId[0] ) Sent->State = MDO_QUEUE_PENDING;
                    snprintf(After.Error, sizeof(After.Error), "%.*s", (int)(Code.Size ? Code.Size : 17u),
                        Code.Size ? Code.Data : "queue_unavailable");
                }
                (void)MdoQueueWrite(Path, &After);
            }
            MdoQueueRelease(&After);
        }
        xrtMutexUnlock(g_MdoQueueLock);
        xrtValueRelease(Reply); xrtValueRelease(Body); MdoQueueResponseUnit(&Response);
        MdoApiLiveChanged(NULL);
    }
    if ( Queue.Revision != Revision ) MdoApiLiveChanged(NULL);
    MdoQueueRelease(&Queue);
done:
    MdoRunSnapshotRelease(Runs); MdoProjectLeaseRelease(Lease);
}

static int32 MdoQueueThread(ptr User)
{
    (void)User;
    size_t Cursor = 0u;
    while ( !xrtAtomic32Load(&g_MdoQueueStopping, XMEMORY_ACQUIRE) ) {
        (void)xrtSleep(250u);
        if ( MdoUpdateInstalling() || MdoUpdateBlocked() ) continue;
        /* Participate in purge/restore admission just like an HTTP writer. */
        char Token[MDO_API_WRITE_TOKEN_CAPACITY];
        if ( !MdoApiWriteToken(Token) ) continue;
        xhttpfield Field = { XRT_STR_LITERAL("X-Mdo-Write-Token"), xrtStrView(Token) };
        xhttp1head Head = {0}; Head.Fields = &Field; Head.FieldCount = 1u;
        XS_HttpReq Request = {0}; Request.head = &Head;
        MdoApplicationResponse Response = {0};
        MdoApiContext Gate = {0}; Gate.Request = &Request; Gate.ApplicationResponse = &Response;
        if ( MdoApiWriteEnter(&Gate, false, false) ) {
            xwork_error Error;
            MdoSessionCatalog* Catalog = MdoSessionCatalogSnapshot(&Error);
            size_t Count = MdoSessionCatalogCount(Catalog);
            for ( size_t i = 0u; i < Count && i < 32u &&
                    !xrtAtomic32Load(&g_MdoQueueStopping, XMEMORY_ACQUIRE); ++i ) {
                MdoSessionInfo Session = {0}; Session.Size = sizeof(Session);
                if ( MdoSessionCatalogAt(Catalog, (Cursor + i) % Count, &Session) &&
                     Session.Status == MDO_SESSION_ACTIVE ) MdoQueueDispatchSession(&Session);
            }
            Cursor = Count ? (Cursor + 32u) % Count : 0u;
            MdoSessionCatalogRelease(Catalog); MdoApiWriteLeave(&Gate);
        }
        MdoQueueResponseUnit(&Response);
    }
    return 0;
}

bool MdoApiQueueStart(void)
{
    if ( g_MdoQueueThread ) return true;
    if ( !g_MdoQueueLock ) return false;
    xrtAtomic32Store(&g_MdoQueueStopping, 0, XMEMORY_RELEASE);
    g_MdoQueueThread = xrtThreadCreate(MdoQueueThread, NULL, 0u);
    return g_MdoQueueThread != NULL;
}

void MdoApiQueueStop(void)
{
    xrtAtomic32Store(&g_MdoQueueStopping, 1, XMEMORY_RELEASE);
    if ( g_MdoQueueThread ) { (void)xrtThreadWait(g_MdoQueueThread); xrtThreadDestroy(g_MdoQueueThread); }
    g_MdoQueueThread = NULL;
}
