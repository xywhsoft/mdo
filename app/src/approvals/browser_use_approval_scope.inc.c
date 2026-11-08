/* Bound by the session manager only after validating the durable product
 * session. Descendants share this immutable origin, independently of run IDs.
 * These strings select a presentation scope; they never grant permission. */
bool MdoApprovalScopeBind(MdoApprovalScope* Scope,
    const char* ProjectId, const char* SessionId)
{
    char Project[MDO_APPROVAL_PROJECT_CAPACITY];
    char Session[MDO_APPROVAL_SESSION_CAPACITY];
    bool Ok = false;
    if ( !Scope || !MdoApprovalCopy(Project, sizeof(Project), ProjectId, true) ||
         !MdoApprovalCopy(Session, sizeof(Session), SessionId, true) ||
         !g_MdoApprovals.Lock || !xrtMutexLock(g_MdoApprovals.Lock) ) return false;
    if ( !g_MdoApprovals.Stopping ) {
        if ( Scope->ProjectId[0] )
            Ok = !strcmp(Scope->ProjectId, Project) &&
                 !strcmp(Scope->SessionId, Session);
        else {
            memcpy(Scope->ProjectId, Project, strlen(Project) + 1u);
            memcpy(Scope->SessionId, Session, strlen(Session) + 1u);
            Ok = true;
        }
    }
    (void)xrtMutexUnlock(g_MdoApprovals.Lock);
    return Ok;
}
