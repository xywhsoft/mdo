/* Included by queue.c. Input ownership is independent of Agent Start. The
 * cancellation fence survives queue/draft withdrawal and process restarts. */
static bool MdoQueueStatusReply(MdoApiContext* Context, const char* Project,
    const char* Session, const char* Id)
{
    bool Exists = false, Cancelled = false, Ok;
    char Run[MDO_RUN_ID_CAPACITY] = {0}, Reserved[MDO_RUN_ID_CAPACITY] = {0};
    uint64 Anchor = 0u;
    uint64 Input = 0u;
    bool Retained = false;
    xrtMutexLock(g_MdoQueueLock);
    Ok = MdoQueueCancelled(Project, Session, Id, &Cancelled) &&
        MdoQueueReceiptResolve(Project, Session, Id, &Exists, Run) &&
        MdoQueueReceiptReadEx(Project, Session, Id, &Exists, Run, Reserved, NULL) &&
        (!Cancelled || MdoQueueCancellationAnchor(Project, Session, Id, &Anchor));
    if (Ok) Ok = MdoQueueInputAnchor(Project, Session, Id, &Input);
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
    bool Executed = Input != 0u;
    bool Terminal = Live ? Info.Terminal : Cancelled;
    const char* Phase = Live && Info.Terminal && !Executed && !Info.CancelRequested ? "unconfirmed" : Live ? (Info.Terminal ?
        (Info.State == XWORK_RUN_SUCCEEDED ? "completed" :
         Info.State == XWORK_RUN_CANCELLED || Info.CancelRequested ? "cancelled" : "failed") :
        (Cancelled || Info.CancelRequested ? "stopping" : Executed ? "running" : "preparing")) :
        Cancelled ? "cancelled" : Executed ? "accepted" : "unconfirmed";
    xvalue* Data = xrtValueObject();
    Ok = Data && MdoApiValueSetString(Data, "id", Id) &&
        MdoApiValueSetString(Data, "state", Cancelled && Terminal && !Executed ? "cancelled" : Run[0] ? "accepted" : "starting") &&
        MdoApiValueSetString(Data, "phase", Phase) &&
        MdoApiValueSetBool(Data, "executed", Executed) &&
        (!Input || MdoApiValueSetUInt(Data, "input_event_id", Input)) &&
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
            Item->HoldForEdit = false;
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

bool MdoApiQueueStageReplacement(MdoApiContext* Context, const char* Id, const xvalue* Input)
{
    /* Store the edited payload before cutting any history. A held staged
     * input cannot be promoted by ordinary queue recovery or another page. */
    const xvalue* Text = xrtValueObjectGet(Input, XRT_STR_LITERAL("text"));
    const xvalue* Images = xrtValueObjectGet(Input, XRT_STR_LITERAL("attachments"));
    const xvalue* Profile = xrtValueObjectGet(Input, XRT_STR_LITERAL("profile"));
    if (xrtValueType(Input) != XVALUE_OBJECT || xrtValueCount(Input) != 3u ||
        xrtValueType(Text) != XVALUE_STRING || !Images || !Profile) return false;
    xvalue* Body = xrtValueObject();
    bool Ok = Body && MdoApiValueSetString(Body, "id", Id) &&
        xrtValueObjectSetNew(Body, XRT_STR_LITERAL("text"), xrtValueClone(Text)) &&
        xrtValueObjectSetNew(Body, XRT_STR_LITERAL("attachments"), xrtValueClone(Images)) &&
        xrtValueObjectSetNew(Body, XRT_STR_LITERAL("profile"), xrtValueClone(Profile)) &&
        MdoApiValueSetBool(Body, "first", true) && MdoApiValueSetBool(Body, "stage", true) &&
        MdoApiValueSetBool(Body, "managed", true) && MdoApiValueSetBool(Body, "hold_for_edit", true);
    MdoApiContext Staged = *Context;
    MdoApplicationResponse Response = {0};
    Staged.ApplicationBody = Body; Staged.ApplicationResponse = &Response;
    if (Ok) (void)MdoApiQueueRoute(&Staged);
    Ok = Ok && (Response.Status == 200u || Response.Status == 201u);
    if (Ok) {
        xvalue* Reply = xrtJsonParse(xrtStrViewN(Response.Body, Response.Bytes));
        const xvalue* Items = xrtValueObjectGet(xrtValueObjectGet(Reply, XRT_STR_LITERAL("data")), XRT_STR_LITERAL("items"));
        bool Held = false;
        for (size_t i = 0u; i < xrtValueCount(Items); ++i) {
            const xvalue* Item = xrtValueArrayGet(Items, i); xstrview Stored;
            if (MdoQueueString(Item, "id", &Stored) && Stored.Size == 32u && !memcmp(Stored.Data, Id, 32u))
                Ok = MdoQueueBool(Item, "hold_for_edit", &Held) && Held;
        }
        Ok = Ok && Held; xrtValueRelease(Reply);
    }
    xrtValueRelease(Body); xrtFree(Response.Body); xrtFree(Response.ContentType); xrtFree(Response.Revision);
    return Ok;
}

bool MdoApiQueueCommitReplacement(const char* Project, const char* Session, const char* Id)
{
    char Path[MDO_SESSION_PATH_CAPACITY]; MdoQueue Queue;
    if (!MdoQueueRunPath(Path, Project, Session)) return false;
    xrtMutexLock(g_MdoQueueLock);
    bool Ok = MdoQueueRead(Path, Project, Session, &Queue);
    if (Ok) {
        size_t Index = MdoQueueFind(&Queue, Id);
        bool Cancelled = false;
        Ok = MdoQueueCancelled(Project, Session, Id, &Cancelled) && !Cancelled;
        if (Ok && Index != SIZE_MAX && Queue.Items[Index].HoldForEdit && Queue.Items[Index].State == MDO_QUEUE_STAGED) {
            Queue.Items[Index].HoldForEdit = false; Queue.Items[Index].State = MDO_QUEUE_PENDING;
            Queue.Managed = true; Queue.Paused = false; Queue.Error[0] = '\0';
            Queue.LastRunId[0] = '\0'; Queue.CancelRunId[0] = '\0';
            Ok = MdoQueueWrite(Path, &Queue);
        } else if (Ok) {
            if (Index != SIZE_MAX) Ok = !Queue.Items[Index].HoldForEdit &&
                (Queue.Items[Index].State == MDO_QUEUE_PENDING || Queue.Items[Index].State == MDO_QUEUE_SENDING);
            else { bool Exists; char Run[MDO_RUN_ID_CAPACITY];
                Ok = MdoQueueReceiptRead(Project, Session, Id, &Exists, Run) && Exists && Run[0]; }
        }
    }
    MdoQueueRelease(&Queue); xrtMutexUnlock(g_MdoQueueLock);
    if (Ok) MdoApiLiveChanged(NULL);
    return Ok;
}
