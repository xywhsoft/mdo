/* Included by sessions.c after its ordinary validation/reply helpers. The
 * existing session mutex serializes keyed edits and reply publication. Only
 * completed replies are retained, for longer than the client's retry budget. */
static bool MdoApiSessionEditOnce(MdoApiContext* Context,
    uint64 ThroughSequence, uint64 SourceEventId, const char* EditId)
{
    char Project[MDO_PROJECT_ID_CAPACITY];
    char SessionId[MDO_SESSION_ID_CAPACITY];
    char WriteToken[MDO_API_WRITE_TOKEN_CAPACITY];
    MdoApiSessionPreconditionStatus Precondition;
    MdoSessionInfo Info;
    MdoSession* Session = NULL;
    MdoApiEditReply* Reply = NULL;
    MdoApiEditReply* Free = NULL;
    xwork_error Error;
    uint64 ExpectedRevision = 0u, Now = xrtClock();
    uint32 Status = 0u;
    const char* Code = NULL;
    const char* Message = NULL;
    size_t Index;
    bool MatchesSession = false, ActiveFailure = false, LoadFailure = false;
    bool MutationFailure = false;

    if ( !MdoApiSessionPath(Context, Project, SessionId) )
        return MdoApiReplyError(Context, 400u, "invalid_session_path",
            "The project or session ID is invalid", NULL);
    Precondition = MdoApiSessionExpectedRevision(Context, SessionId,
        &ExpectedRevision, &MatchesSession);
    if ( Precondition == MDO_API_SESSION_PRECONDITION_MISSING )
        return MdoApiReplyError(Context, 428u, "precondition_required",
            "If-Match must contain the original session ETag", NULL);
    if ( Precondition != MDO_API_SESSION_PRECONDITION_OK )
        return MdoApiReplyError(Context, 400u, "invalid_precondition",
            "If-Match must use the original session ETag", NULL);
    if ( !MatchesSession )
        return MdoApiReplyError(Context, 412u, "revision_conflict",
            "If-Match belongs to a different session", NULL);
    if ( !MdoApiWriteToken(WriteToken) || g_MdoApiSessionCreateLock == NULL ||
         !xrtMutexLock(g_MdoApiSessionCreateLock) )
        return MdoApiReplyError(Context, 503u, "session_service_unavailable",
            "The session service is unavailable", NULL);

    memset(&Error, 0, sizeof(Error));
    memset(&Info, 0, sizeof(Info)); Info.Size = sizeof(Info);
    for ( Index = 0u; Index < MDO_API_EDIT_REPLIES; ++Index ) {
        MdoApiEditReply* Item = &g_MdoApiEditReplies[Index];
        /* Old write epochs cannot replay and need not occupy this epoch's
         * bounded cache until their original TTL expires. */
        if ( Item->ExpiresAt <= Now || strcmp(Item->WriteToken, WriteToken) != 0 ) {
            if ( Free == NULL ) Free = Item;
            continue;
        }
        if ( strcmp(Item->Project, Project) == 0 &&
             strcmp(Item->Session, SessionId) == 0 &&
             strcmp(Item->Id, EditId) == 0 &&
             strcmp(Item->WriteToken, WriteToken) == 0 ) { Reply = Item; break; }
    }
    if ( Reply != NULL ) {
        if ( Reply->BaseRevision != ExpectedRevision ||
             Reply->ThroughSequence != ThroughSequence ||
             Reply->SourceEventId != SourceEventId ) {
            Status = 409u; Code = "session_edit_conflict";
            Message = "The same edit ID was used for a different history edit";
            goto done;
        }
        /* Metadata-only replay never opens an Agent or trims files again.
         * Later changes and a recreated session invalidate this old reply. */
        Session = MdoSessionLoad(Project, SessionId, &Error);
        if ( Session == NULL ) { LoadFailure = true; goto done; }
        if ( !MdoSessionGetInfo(Session, &Info) ) goto unavailable;
        if ( Info.CreatedAt != Reply->CreatedAt ||
             Info.Revision != Reply->Revision || Info.Status != MDO_SESSION_ACTIVE )
            goto changed;
        goto done;
    }
    if ( Free == NULL ) {
        Status = 503u; Code = "session_service_unavailable";
        Message = "History edit reply capacity is temporarily full";
        goto done;
    }
    Session = MdoSessionOpen(Project, SessionId, NULL, &Error);
    if ( Session == NULL ) { ActiveFailure = true; goto done; }
    if ( !MdoSessionGetInfo(Session, &Info) ) goto unavailable;
    if ( Info.Revision != ExpectedRevision ) goto changed;
    {
        MdoSessionMessageMutationResult Result = MdoSessionTruncateMessage(
            Session, ThroughSequence, SourceEventId, &Error);
        if ( Result == MDO_SESSION_MESSAGE_CHANGED ) {
            Status = 409u; Code = "session_message_changed";
            Message = "The original message changed; refresh before editing";
            goto done;
        }
        if ( Result != MDO_SESSION_MESSAGE_OK ) { MutationFailure = true; goto done; }
    }
    if ( !MdoSessionGetInfo(Session, &Info) ) goto unavailable;
    memset(Free, 0, sizeof(*Free));
    snprintf(Free->Project, sizeof(Free->Project), "%s", Project);
    snprintf(Free->Session, sizeof(Free->Session), "%s", SessionId);
    snprintf(Free->Id, sizeof(Free->Id), "%s", EditId);
    snprintf(Free->WriteToken, sizeof(Free->WriteToken), "%s", WriteToken);
    Free->BaseRevision = ExpectedRevision;
    Free->Revision = Info.Revision;
    Free->CreatedAt = Info.CreatedAt;
    Free->ThroughSequence = ThroughSequence;
    Free->SourceEventId = SourceEventId;
    Free->ExpiresAt = xrtClock() + MDO_API_EDIT_REPLY_TTL_US;
    goto done;
changed:
    Status = 412u; Code = "revision_conflict";
    Message = "The session changed; reload before continuing this edit";
    goto done;
unavailable:
    Status = 500u; Code = "session_result_unavailable";
    Message = "The session result is unavailable";
done:
    MdoSessionRelease(Session);
    xrtMutexUnlock(g_MdoApiSessionCreateLock);
    if ( Status != 0u ) return MdoApiReplyError(Context, Status, Code, Message, NULL);
    if ( ActiveFailure ) return MdoApiSessionActiveFailure(Context, &Error);
    if ( LoadFailure ) return MdoApiSessionLoadFailure(Context, &Error);
    if ( MutationFailure ) return MdoApiSessionMutationFailure(Context,
        Project, SessionId, ExpectedRevision, &Error);
    return MdoApiSessionReply(Context, 200u, &Info);
}
