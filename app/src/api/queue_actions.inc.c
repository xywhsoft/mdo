/* Included by queue.c. The editing row is a write-ahead handoff: it owns the
 * payload/images until the draft's restore ID and a withdrawal tombstone are
 * durable. Neither network retries nor process restart may execute it. */
static int MdoQueueFinishEdit(const char* Project, const char* Session,
    const MdoQueueItem* Item)
{
    int Result = MdoApiDraftQueueRestore(Project, Session, Item->Id,
        Item->Edit.DraftRevision, xrtStrViewN(Item->Text, Item->TextSize),
        Item->Attachments, Item->AttachmentCount, &Item->Profile);
    char Path[MDO_SESSION_PATH_CAPACITY];
    MdoQueue Queue;
    bool Ok = true;
    if ( Result == 0 ) return 0;
    if ( Result == 1 ) {
        xvalue* Receipt = MdoQueueActionValue(&Item->Edit);
        size_t Size = 0u;
        char* Json = Receipt ? xrtJsonStringify(Receipt, false, &Size) : NULL;
        Ok = Json && MdoQueueWithdrawPath(Path, Project, Session, Item->Id) &&
            MdoHomeAtomicWrite(Path, Json, Size, false);
        xrtFree(Json); xrtValueRelease(Receipt);
        if ( !Ok ) return 0;
    }
    if ( !MdoQueueRunPath(Path, Project, Session) ) return 0;
    xrtMutexLock(g_MdoQueueLock);
    Ok = MdoQueueRead(Path, Project, Session, &Queue);
    if ( Ok ) {
        size_t Index = MdoQueueFind(&Queue, Item->Id);
        if ( Index != SIZE_MAX && Queue.Items[Index].State == MDO_QUEUE_EDITING &&
             !strcmp(Queue.Items[Index].Edit.Id, Item->Edit.Id) ) {
            if ( Result == 1 ) {
                MdoQueueActionRemember(&Queue, &Item->Edit);
                MdoQueueRemove(&Queue, Index);
            } else {
                Queue.Items[Index].State = MDO_QUEUE_PENDING;
                memset(&Queue.Items[Index].Edit, 0, sizeof(Queue.Items[Index].Edit));
            }
            Ok = MdoQueueWrite(Path, &Queue);
        }
        MdoQueueRelease(&Queue);
    }
    xrtMutexUnlock(g_MdoQueueLock);
    if ( Ok ) MdoApiLiveChanged(NULL);
    return Ok ? Result : 0;
}

static int MdoQueueWithdrawReplay(const char* Project, const char* Session,
    const MdoQueueAction* Action)
{
    char Path[MDO_SESSION_PATH_CAPACITY], Bytes[1024];
    xfileinfo Info; bool Exists;
    if ( !MdoQueueWithdrawPath(Path, Project, Session, Action->Item) ||
         !MdoHomeExternalStat(Path, &Exists, &Info) ) return -1;
    if ( !Exists ) return 0;
    if ( Info.Type != XFILE_TYPE_FILE || !(Info.Available & XFILE_INFO_SIZE) ||
         !Info.Size || Info.Size >= sizeof(Bytes) ) return -1;
    xfile File = MdoHomeOpenRead(Path);
    bool Ok = File && xrtReadFull(File, Bytes, (size_t)Info.Size, NULL);
    if ( File && !xrtClose(File) ) Ok = false;
    if ( !Ok ) return -1;
    xvalue* Value = xrtJsonParse(xrtStrViewN(Bytes, (size_t)Info.Size));
    MdoQueueAction Saved;
    Ok = MdoQueueActionParse(Value, &Saved);
    xrtValueRelease(Value);
    if ( !Ok ) return -1;
    return !memcmp(&Saved, Action, sizeof(Saved)) ? 1 : 2;
}

bool MdoApiQueueActionsRoute(MdoApiContext* Context)
{
    char Path[MDO_SESSION_PATH_CAPACITY], Project[MDO_PROJECT_ID_CAPACITY],
        Session[MDO_SESSION_ID_CAPACITY];
    MdoSessionStatus Status;
    MdoApiJsonBody Body;
    MdoQueueAction Action;
    if ( !MdoQueuePath(Context, Path, &Status, Project, Session) )
        return MdoApiReplyError(Context, 404u, "session_not_found", "The session does not exist", NULL);
    if ( Status != MDO_SESSION_ACTIVE ) return MdoApiReplyError(Context, 409u,
        "session_state_conflict", "Restore the session before changing its queue", NULL);
    MdoApiBodyStatus BodyStatus = MdoApiJsonBodyRead(Context, &Body);
    if ( BodyStatus != MDO_API_BODY_OK ) return MdoApiReplyBodyError(Context, BodyStatus);
    bool Valid = MdoQueueActionParse(Body.Value, &Action);
    MdoApiJsonBodyUnit(&Body);
    if ( !Valid ) return MdoApiReplyError(Context, 422u, "queue_action_invalid", "Invalid queue action", NULL);
    MdoQueue Queue;
    bool Ok, Edit = false, Duplicate = false;
    const char* Conflict = NULL;
    xrtMutexLock(g_MdoQueueLock);
    Ok = MdoQueueRead(Path, Project, Session, &Queue);
    if ( Ok ) {
        for ( size_t i = 0u; i < Queue.ActionCount; ++i ) {
            if ( strcmp(Queue.Actions[i].Id, Action.Id) ) continue;
            if ( memcmp(&Queue.Actions[i], &Action, sizeof(Action)) ) Conflict = "queue_action_conflict";
            else Duplicate = true;
            break;
        }
        if ( !Duplicate && !Conflict && !strcmp(Action.Kind, "withdraw") ) {
            int Replay = MdoQueueWithdrawReplay(Project, Session, &Action);
            if ( Replay < 0 ) Ok = false;
            else if ( Replay == 1 ) Duplicate = true;
            else if ( Replay == 2 ) Conflict = "queue_item_consumed";
        }
        if ( Ok && !Duplicate && !Conflict ) {
            size_t Index = MdoQueueFind(&Queue, Action.Item);
            MdoQueueItem* Item = Index != SIZE_MAX ? &Queue.Items[Index] : NULL;
            if ( Item && Queue.Paused && Item->State == MDO_QUEUE_PENDING ) Item->Priority = false;
            if ( Item && Item->State == MDO_QUEUE_EDITING &&
                 !memcmp(&Item->Edit, &Action, sizeof(Action)) ) Edit = true;
            else if ( Queue.Revision != Action.Revision ) Conflict = "queue_revision_conflict";
            else if ( !strcmp(Action.Kind, "resume") ) {
                if ( Queue.Count && Queue.Items[0].State == MDO_QUEUE_SENDING ) {
                    MdoQueueItem* Head = &Queue.Items[0];
                    if ( Head->StartClaimed || Head->RunId[0] ) Conflict = "queue_run_starting";
                    else Head->State = MDO_QUEUE_PENDING;
                }
                if ( !Conflict ) {
                    Queue.Paused = false; Queue.Error[0] = '\0';
                    Queue.LastRunId[0] = '\0'; Queue.CancelRunId[0] = '\0';
                    for ( size_t i = 0u; i < Queue.Count; ++i ) Queue.Items[i].Priority = false;
                }
            } else if ( !Item || Item->State != MDO_QUEUE_PENDING ||
                        Item->StartClaimed || Item->RunId[0] || Item->Priority ) Conflict = "queue_state_conflict";
            else if ( !strcmp(Action.Kind, "withdraw") ) {
                for ( size_t i = 0u; i < Queue.Count; ++i )
                    if ( Queue.Items[i].State == MDO_QUEUE_EDITING ) Conflict = "queue_state_conflict";
                if ( !Conflict ) { Item->State = MDO_QUEUE_EDITING; Item->Edit = Action; Edit = true; }
            } else if ( !strcmp(Action.Kind, "move") ) {
                size_t Before = MdoQueueFind(&Queue, Action.Before);
                if ( Action.Before[0] && (Before == SIZE_MAX ||
                     Queue.Items[Before].State != MDO_QUEUE_PENDING ||
                     (Queue.Items[Before].Priority && !Queue.Paused)) ) Conflict = "queue_state_conflict";
                else if ( !MdoQueueMove(&Queue, Action.Item, Action.Before) ) Conflict = "queue_state_conflict";
            } else {
                /* A start already reserved at the front must finish admission;
                 * the chosen row becomes the next head without stealing it. */
                size_t First = 0u;
                while ( First < Queue.Count && Queue.Items[First].State == MDO_QUEUE_SENDING ) ++First;
                for ( size_t i = 0u; i < Queue.Count; ++i )
                    if ( Queue.Items[i].Priority && !Queue.Paused ) Conflict = "queue_state_conflict";
                if ( First == Queue.Count ) Conflict = "queue_state_conflict";
                if ( !Conflict ) {
                    char Before[33]; memcpy(Before, Queue.Items[First].Id, sizeof(Before));
                    for ( size_t i = 0u; i < Queue.Count; ++i ) Queue.Items[i].Priority = false;
                    MdoQueueMove(&Queue, Action.Item, Before);
                    Queue.Items[MdoQueueFind(&Queue, Action.Item)].Priority = true;
                    Queue.Paused = false; Queue.Error[0] = '\0'; Queue.CancelRunId[0] = '\0';
                }
            }
            if ( !Conflict ) {
                Queue.Managed = true;
                if ( !Edit ) MdoQueueActionRemember(&Queue, &Action);
                Ok = MdoQueueWrite(Path, &Queue);
            }
        }
    }
    xrtMutexUnlock(g_MdoQueueLock);
    if ( Ok && Edit && !Conflict ) {
        size_t Index = MdoQueueFind(&Queue, Action.Item);
        int Result = MdoQueueFinishEdit(Project, Session, &Queue.Items[Index]);
        if ( Result < 0 ) Conflict = "draft_conflict";
        Ok = Result != 0;
        MdoQueueRelease(&Queue);
        xrtMutexLock(g_MdoQueueLock);
        bool Read = MdoQueueRead(Path, Project, Session, &Queue);
        xrtMutexUnlock(g_MdoQueueLock);
        Ok = Ok && Read;
    }
    if ( !Ok || Conflict ) {
        MdoQueueRelease(&Queue);
        return MdoApiReplyError(Context, Conflict ? 409u : 503u,
            Conflict ? Conflict : "queue_unavailable",
            Conflict ? "The queue or draft changed; refresh before retrying" : "The queue action could not be confirmed", NULL);
    }
    return MdoQueueReply(Context, 200u, &Queue);
}
