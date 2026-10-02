/* Included by manager.c after its path/active/generation helpers. All list and
 * storage settlement calls hold the manager lock. Project/data registries are
 * acquired before it, and released after it; no model or bridge runs here. */
#define MDO_RESTORE_RESERVATION_LIMIT 8u

struct MdoSessionRestoreReservation {
    struct MdoSessionRestoreReservation* Next;
    MdoProjectLease* Owner;
    MdoSessionDataLease* Data;
    MdoHomeSessionRestore* Storage;
    char ProjectId[MDO_PROJECT_ID_CAPACITY];
    char SessionId[MDO_SESSION_ID_CAPACITY];
    bool Settled;
};

static bool MdoSessionsRestoreAlias(cstr A, cstr B)
{
    size_t i, Left = strlen(A), Right = strlen(B);
    while ( Left != 0u && A[Left - 1u] == '.' ) --Left;
    while ( Right != 0u && B[Right - 1u] == '.' ) --Right;
    if ( Left != Right ) return false;
    for ( i = 0u; i < Left; ++i ) {
        unsigned char X = (unsigned char)A[i], Y = (unsigned char)B[i];
        if ( X >= 'A' && X <= 'Z' ) X += 'a' - 'A';
        if ( Y >= 'A' && Y <= 'Z' ) Y += 'a' - 'A';
        if ( X != Y ) return false;
    }
    return true;
}

static bool MdoSessionsRestoreReserved(cstr ProjectId, cstr SessionId)
{
    MdoSessionRestoreReservation* Restore;
    for ( Restore = g_MdoSessions.Restores; Restore != NULL; Restore = Restore->Next )
        if ( MdoSessionsRestoreAlias(Restore->ProjectId, ProjectId) &&
             MdoSessionsRestoreAlias(Restore->SessionId, SessionId) ) return true;
    return false;
}

static MdoSessionRestoreReservation** MdoSessionsRestoreMember(MdoSessionRestoreReservation* Restore)
{
    MdoSessionRestoreReservation** Link;
    for ( Link = &g_MdoSessions.Restores; *Link != NULL; Link = &(*Link)->Next )
        if ( *Link == Restore ) return Link;
    return NULL;
}

/* The native mkdir and absent check are in the same manager boundary as
 * reservation acquisition, so checking before a separate mkdir is insufficient. */
static bool MdoSessionsCreateDirectory(cstr ProjectId, cstr SessionId,
    cstr Path, xwork_error* Error)
{
    bool Exists, Ok = false;
    xrtMutexLock(g_MdoSessions.Lock);
    if ( MdoSessionsRestoreReserved(ProjectId, SessionId) )
        MdoSessionsError(Error, XWORK_ERROR_CONTEXT, "session ID is reserved for restore");
    else if ( !MdoHomeExternalStat(Path, &Exists, NULL) )
        MdoSessionsXrtError(Error, XWORK_ERROR_IO, "cannot inspect the requested session directory");
    else if ( Exists )
        MdoSessionsError(Error, XWORK_ERROR_CONTEXT, "the requested session ID already exists");
    else if ( !MdoHomeCreateDirectory(Path) )
        MdoSessionsXrtError(Error, XWORK_ERROR_IO, "cannot create the managed session directory");
    else Ok = true;
    xrtMutexUnlock(g_MdoSessions.Lock);
    return Ok;
}

MdoSessionRestoreReservation* MdoSessionsRestoreReserve(cstr ProjectId,
    cstr SessionId, MdoProjectLease* Owner, xwork_error* Error)
{
    MdoSessionRestoreReservation* Restore;
    char Path[MDO_SESSION_PATH_CAPACITY];
    bool Exists, Ok = false;
    size_t i;
    xworkErrorInit(Error);
    if ( !g_MdoSessions.Initialized || !MdoSessionsIdValid(ProjectId, MDO_PROJECT_ID_CAPACITY) ||
         SessionId == NULL || strlen(SessionId) != 32u ||
         !MdoProjectLeaseProtects(Owner, ProjectId, MDO_PROJECT_LEASE_SHARED) ) goto invalid;
    for ( i = 0u; i < 32u; ++i )
        if ( !((SessionId[i] >= '0' && SessionId[i] <= '9') ||
               (SessionId[i] >= 'a' && SessionId[i] <= 'f')) ) goto invalid;
    if ( !MdoSessionsDirectory(Path, ProjectId, SessionId) ) goto invalid;
    Restore = xrtCalloc(1u, sizeof(*Restore));
    if ( Restore == NULL ) { MdoSessionsError(Error, XWORK_ERROR_OUT_OF_MEMORY, "cannot reserve restore identity"); return NULL; }
    Restore->Owner = MdoProjectLeaseRef(Owner);
    if ( Restore->Owner == NULL ) { MdoSessionsError(Error, XWORK_ERROR_LIMIT, "cannot pin restore project owner"); goto done; }
    snprintf(Restore->ProjectId, sizeof(Restore->ProjectId), "%s", ProjectId);
    snprintf(Restore->SessionId, sizeof(Restore->SessionId), "%s", SessionId);
    xrtMutexLock(g_MdoSessions.Lock);
    if ( g_MdoSessions.RestoreCount >= MDO_RESTORE_RESERVATION_LIMIT )
        MdoSessionsError(Error, XWORK_ERROR_LIMIT, "too many pending session restore identities");
    else if ( MdoSessionsRestoreReserved(ProjectId, SessionId) )
        MdoSessionsError(Error, XWORK_ERROR_CONTEXT, "session ID is reserved for restore");
    else if ( !MdoHomeExternalStat(Path, &Exists, NULL) )
        MdoSessionsXrtError(Error, XWORK_ERROR_IO, "cannot inspect restore target identity");
    else if ( Exists )
        MdoSessionsError(Error, XWORK_ERROR_CONTEXT, "restore target session ID already exists");
    else {
        Ok = true;
        for ( i = 0u; i < g_MdoSessions.ActiveCount; ++i ) {
            MdoSessionActive* Active = &g_MdoSessions.Active[i];
            if ( MdoSessionsRestoreAlias(ProjectId, Active->ProjectId) &&
                 MdoSessionsRestoreAlias(SessionId, Active->SessionId) ) { Ok = false; break; }
        }
        if ( !Ok ) MdoSessionsError(Error, XWORK_ERROR_CONTEXT, "restore target has an active runtime");
        else {
            Restore->Next = g_MdoSessions.Restores; g_MdoSessions.Restores = Restore;
            ++g_MdoSessions.RestoreCount;
        }
    }
    xrtMutexUnlock(g_MdoSessions.Lock);
    if ( Ok ) {
        /* Install the identity before acquiring its data lease. This prevents
         * a new Create from entering its native mkdir while we establish data
         * exclusion, without inverting registry/manager locks. A sidecar writer
         * that finished in this short gap is detected by the second stat. */
        Restore->Data = MdoSessionDataAcquire(ProjectId, SessionId, MDO_SESSION_DATA_CAPTURE, Error);
        Ok = Restore->Data != NULL;
        xrtMutexLock(g_MdoSessions.Lock);
        if ( Ok && (!MdoHomeExternalStat(Path, &Exists, NULL) || Exists) ) {
            Ok = false;
            MdoSessionsError(Error, XWORK_ERROR_CONTEXT, "restore target appeared while reserving session data");
        }
        if ( !Ok ) {
            MdoSessionRestoreReservation** Link = MdoSessionsRestoreMember(Restore);
            if ( Link == NULL ) abort();
            *Link = Restore->Next; --g_MdoSessions.RestoreCount;
        }
        xrtMutexUnlock(g_MdoSessions.Lock);
    }
done:
    if ( Ok ) return Restore;
    MdoSessionDataRelease(Restore->Data); MdoProjectLeaseRelease(Restore->Owner); xrtFree(Restore);
    return NULL;
invalid:
    MdoSessionsError(Error, XWORK_ERROR_INVALID_ARGUMENT, "invalid current session restore reservation request");
    return NULL;
}

static bool MdoSessionsRestoreStorageStart(MdoSessionRestoreReservation* Restore,
    const MdoHomeSessionRestoreRequest* Request, xroot* Parent, xwork_error* Error)
{
    bool Ok = false;
    xworkErrorInit(Error);
    if ( Restore == NULL || Parent == NULL || *Parent != NULL || !g_MdoSessions.Initialized ||
         !MdoProjectLeaseProtects(Restore->Owner, Restore->ProjectId, MDO_PROJECT_LEASE_SHARED) ) goto invalid;
    if ( Request != NULL && (Request->Size != sizeof(*Request) ||
         memchr(Request->ProjectId, '\0', sizeof(Request->ProjectId)) == NULL ||
         memchr(Request->SessionId, '\0', sizeof(Request->SessionId)) == NULL ||
         strcmp(Request->ProjectId, Restore->ProjectId) != 0 ||
         strcmp(Request->SessionId, Restore->SessionId) != 0) ) goto invalid;
    xrtMutexLock(g_MdoSessions.Lock);
    if ( MdoSessionsRestoreMember(Restore) != NULL && !Restore->Settled && Restore->Storage == NULL ) {
        Restore->Storage = Request != NULL ? MdoHomeSessionRestoreBeginRequested(Request, Parent) :
            MdoHomeSessionRestoreBegin(Restore->ProjectId, Restore->SessionId, Parent);
        Ok = Restore->Storage != NULL;
        if ( !Ok ) MdoSessionsXrtError(Error, XWORK_ERROR_IO, "cannot begin reserved restore storage");
    } else MdoSessionsError(Error, XWORK_ERROR_CONTEXT, "restore reservation is not current or already settled");
    xrtMutexUnlock(g_MdoSessions.Lock);
    return Ok;
invalid:
    MdoSessionsError(Error, XWORK_ERROR_INVALID_ARGUMENT, "invalid current restore storage request");
    return false;
}

bool MdoSessionsRestoreStorageBegin(MdoSessionRestoreReservation* Restore,
    xroot* Parent, xwork_error* Error)
{
    return MdoSessionsRestoreStorageStart(Restore, NULL, Parent, Error);
}

bool MdoSessionsRestoreStorageBeginRequested(MdoSessionRestoreReservation* Restore,
    const MdoHomeSessionRestoreRequest* Request, xroot* Parent, xwork_error* Error)
{
    if ( Request == NULL ) {
        MdoSessionsError(Error, XWORK_ERROR_INVALID_ARGUMENT, "restore request identity is required");
        return false;
    }
    return MdoSessionsRestoreStorageStart(Restore, Request, Parent, Error);
}

bool MdoSessionsRestorePublish(MdoSessionRestoreReservation* Restore,
    cstr Name, const xfileinfo* Identity, bool* Committed, uint64* Generation, xwork_error* Error)
{
    bool Ok = false;
    xworkErrorInit(Error);
    if ( Committed != NULL ) *Committed = false;
    if ( Generation != NULL ) *Generation = 0u;
    if ( Restore == NULL || Committed == NULL || Generation == NULL || !g_MdoSessions.Initialized ||
         !MdoProjectLeaseProtects(Restore->Owner, Restore->ProjectId, MDO_PROJECT_LEASE_SHARED) ) goto invalid;
    xrtMutexLock(g_MdoSessions.Lock);
    if ( MdoSessionsRestoreMember(Restore) == NULL || Restore->Storage == NULL || Restore->Settled )
        MdoSessionsError(Error, XWORK_ERROR_CONTEXT, "restore reservation has no current publication transaction");
    else if ( g_MdoSessions.Generation == UINT64_MAX )
        MdoSessionsError(Error, XWORK_ERROR_LIMIT, "session catalog generation is exhausted");
    else {
        Ok = MdoHomeSessionRestoreEnd(Restore->Storage, Name, Identity, true, Committed);
        Restore->Storage = NULL; Restore->Settled = true;
        if ( *Committed ) MdoSessionsGenerationAdvance();
        if ( !Ok ) MdoSessionsXrtError(Error, XWORK_ERROR_IO, "restore storage publication/cleanup failed");
    }
    *Generation = g_MdoSessions.Generation;
    xrtMutexUnlock(g_MdoSessions.Lock);
    return Ok;
invalid:
    MdoSessionsError(Error, XWORK_ERROR_INVALID_ARGUMENT, "invalid current restore publication request");
    return false;
}

bool MdoSessionsRestoreRelease(MdoSessionRestoreReservation** Value, xwork_error* Error)
{
    MdoSessionRestoreReservation* Restore;
    MdoSessionRestoreReservation** Link = NULL;
    bool Ok = true, Committed = false;
    xworkErrorInit(Error);
    if ( Value == NULL ) { MdoSessionsError(Error, XWORK_ERROR_INVALID_ARGUMENT, "restore release output is required"); return false; }
    Restore = *Value;
    if ( Restore == NULL ) return true;
    if ( g_MdoSessions.Initialized ) {
        xrtMutexLock(g_MdoSessions.Lock);
        Link = MdoSessionsRestoreMember(Restore);
    }
    if ( Link == NULL && Restore->Storage != NULL ) {
        if ( g_MdoSessions.Initialized ) xrtMutexUnlock(g_MdoSessions.Lock);
        MdoSessionsError(Error, XWORK_ERROR_CONTEXT, "active restore must be consumed before manager/Home Unit");
        return false;
    }
    if ( Restore->Storage != NULL ) {
        Ok = MdoHomeSessionRestoreEnd(Restore->Storage, NULL, NULL, false, &Committed);
        Restore->Storage = NULL;
        if ( Committed ) MdoSessionsGenerationAdvance();
        if ( !Ok ) MdoSessionsXrtError(Error, XWORK_ERROR_IO, "restore private storage cleanup failed");
    }
    if ( Link != NULL ) { *Link = Restore->Next; --g_MdoSessions.RestoreCount; }
    if ( g_MdoSessions.Initialized ) xrtMutexUnlock(g_MdoSessions.Lock);
    *Value = NULL;
    MdoSessionDataRelease(Restore->Data); MdoProjectLeaseRelease(Restore->Owner); xrtFree(Restore);
    return Ok;
}
