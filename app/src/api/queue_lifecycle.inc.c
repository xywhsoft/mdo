/* Included by queue.c. Input ownership is independent of Agent Start. The
 * cancellation fence survives queue/draft withdrawal and process restarts. */
static bool MdoQueueStatusReply(MdoApiContext* Context, const char* Project,
    const char* Session, const char* Id)
{
    bool Exists = false, Cancelled = false, Ok;
    char Run[MDO_RUN_ID_CAPACITY] = {0}, Reserved[MDO_RUN_ID_CAPACITY] = {0};
    uint64 Anchor = 0u;
    bool Retained = false;
    xrtMutexLock(g_MdoQueueLock);
    Ok = MdoQueueCancelled(Project, Session, Id, &Cancelled) &&
        MdoQueueReceiptResolve(Project, Session, Id, &Exists, Run) &&
        MdoQueueReceiptReadEx(Project, Session, Id, &Exists, Run, Reserved, NULL) &&
        (!Cancelled || MdoQueueCancellationAnchor(Project, Session, Id, &Anchor));
    if (Ok && Cancelled) {
        char Path[MDO_SESSION_PATH_CAPACITY]; MdoQueue Queue;
        Ok = MdoQueueRunPath(Path, Project, Session) && MdoQueueRead(Path, Project, Session, &Queue);
        if (Ok) { Retained = MdoQueueFind(&Queue, Id) != SIZE_MAX; MdoQueueRelease(&Queue); }
    }
    xrtMutexUnlock(g_MdoQueueLock);
    if ( !Ok ) return MdoApiReplyError(Context, 503u, "queue_unavailable",
        "The message status could not be read", NULL);
    if ( !Exists && !Cancelled ) return MdoApiReplyError(Context, 404u,
        "queue_receipt_not_found", "The queue receipt does not exist", NULL);
    const char* Identity = Run[0] ? Run : Reserved;
    MdoRunInfo Info = {0}; Info.Size = sizeof(Info);
    bool Live = Identity[0] && MdoRunGetInfo(Identity, &Info) &&
        !strcmp(Info.ProjectId, Project) && !strcmp(Info.SessionId, Session);
    bool Executed = Run[0] != '\0';
    bool Terminal = Live ? Info.Terminal : Cancelled && !Executed;
    const char* Phase = Live ? (Info.Terminal ?
        (Info.State == XWORK_RUN_SUCCEEDED ? "completed" :
         Info.State == XWORK_RUN_CANCELLED ? "cancelled" : "failed") :
        (Cancelled || Info.CancelRequested ? "stopping" : Executed ? "running" : "preparing")) :
        Cancelled && !Executed ? "cancelled" : Executed ? "accepted" : "unconfirmed";
    xvalue* Data = xrtValueObject();
    Ok = Data && MdoApiValueSetString(Data, "id", Id) &&
        MdoApiValueSetString(Data, "state", Executed ? "accepted" :
            Cancelled && Terminal ? "cancelled" : "starting") &&
        MdoApiValueSetString(Data, "phase", Phase) &&
        MdoApiValueSetBool(Data, "executed", Executed) &&
        MdoApiValueSetBool(Data, "terminal", Terminal) &&
        MdoApiValueSetBool(Data, "cancel_requested", Cancelled || (Live && Info.CancelRequested)) &&
        (!Cancelled || MdoApiValueSetUInt(Data, "after_event_id", Anchor)) &&
        (!Cancelled || MdoApiValueSetBool(Data, "retained_input", Retained)) &&
        (!Identity[0] || MdoApiValueSetString(Data, "run_id", Identity));
    if ( !Ok ) { xrtValueRelease(Data); return MdoApiReplyError(Context,
        503u, "queue_unavailable", "The message status could not be built", NULL); }
    return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
}

bool MdoApiQueueCancelRoute(MdoApiContext* Context)
{
    char Path[MDO_SESSION_PATH_CAPACITY], Project[MDO_PROJECT_ID_CAPACITY],
        Session[MDO_SESSION_ID_CAPACITY], Id[33];
    MdoSessionStatus Status;
    if ( !MdoQueuePath(Context, Path, &Status, Project, Session) || Context->ParamCount != 3u ||
         !MdoQueueId(Context->Params[2], Id) ) return MdoApiReplyError(Context,
        404u, "session_not_found", "The session does not exist", NULL);
    if ( Status != MDO_SESSION_ACTIVE ) return MdoApiReplyError(Context,
        409u, "session_state_conflict", "The session must be active", NULL);
    const xhttp1head* Head = Context->Request->head;
    if ( ((Head->Flags & (uint32)XHTTP1_CONTENT_LENGTH) && Head->ContentLength) ||
         (Head->Flags & (uint32)XHTTP1_TRANSFER_ENCODING) )
        return MdoApiReplyError(Context, 400u, "body_not_allowed", "This operation does not accept a body", NULL);
    MdoQueue Queue;
    char Run[MDO_RUN_ID_CAPACITY] = {0}, Reserved[MDO_RUN_ID_CAPACITY] = {0};
    bool Exists = false;
    xrtMutexLock(g_MdoQueueLock);
    bool Ok = MdoQueueRead(Path, Project, Session, &Queue);
    if ( Ok ) Ok = MdoQueueReceiptReadEx(Project, Session, Id, &Exists, Run, Reserved, NULL) &&
        MdoQueueCancelFence(Project, Session, Id);
    if ( Ok ) {
        size_t Index = MdoQueueFind(&Queue, Id);
        if ( Index != SIZE_MAX ) {
            MdoQueueItem* Item = &Queue.Items[Index];
            /* Never steal a pending atomic draft extraction. Its old ID is
             * already fenced by that operation and cannot execute. */
            if ( Item->State != MDO_QUEUE_EDITING ) Item->State = MDO_QUEUE_CANCELLED;
            Item->Priority = false; Item->StartClaimed = false; Item->RunId[0] = '\0';
            (void)MdoQueueMove(&Queue, Id, "");
        }
        Queue.Managed = true; Queue.Paused = true;
        snprintf(Queue.Error, sizeof(Queue.Error), "queue_stopped");
        Queue.CancelRunId[0] = '\0';
        Ok = MdoQueueWrite(Path, &Queue);
    }
    MdoQueueRelease(&Queue);
    xrtMutexUnlock(g_MdoQueueLock);
    if ( !Ok ) return MdoApiReplyError(Context, 503u,
        "queue_unavailable", "The message cancellation could not be saved", NULL);
    const char* Identity = Run[0] ? Run : Reserved;
    if ( Identity[0] ) {
        MdoRunInfo Info = {0}; Info.Size = sizeof(Info); xwork_error Error;
        if ( MdoRunGetInfo(Identity, &Info) && !strcmp(Info.ProjectId, Project) &&
             !strcmp(Info.SessionId, Session) ) (void)MdoRunCancel(Identity, NULL, &Error);
    }
    MdoApiLiveChanged(NULL);
    return MdoQueueStatusReply(Context, Project, Session, Id);
}
