#include <stdlib.h>
#include <string.h>

#include "../../include/mdo/project_lifecycle.h"

#define MDO_PROJECT_LIFECYCLE_ID_CAPACITY 65u

typedef struct MdoProjectLeaseEntry {
    struct MdoProjectLeaseEntry* Next;
    char Key[MDO_PROJECT_LIFECYCLE_ID_CAPACITY];
    size_t Readers;
    bool Exclusive;
} MdoProjectLeaseEntry;

typedef struct MdoProjectLeaseRegistry {
    xmutex* Lock;
    MdoProjectLeaseEntry* Entries;
    size_t Refs; /* manager plus each independently acquired lease */
    bool Closed;
} MdoProjectLeaseRegistry;

struct MdoProjectLease {
    xatomic32 Refs;
    MdoProjectLeaseRegistry* Registry;
    MdoProjectLeaseEntry* Entry;
    MdoProjectLeaseMode Mode;
};

static MdoProjectLeaseRegistry* g_MdoProjectLeases;

static void MdoProjectLifecycleError(xwork_error* Error,
    xwork_error_code Code, const char* Message)
{
    if ( Error == NULL ) return;
    xworkErrorInit(Error);
    Error->eCode = Code;
    snprintf(Error->sMessage, sizeof(Error->sMessage), "%s", Message);
}

static bool MdoProjectLifecycleKey(const char* Id,
    char Key[MDO_PROJECT_LIFECYCLE_ID_CAPACITY])
{
    size_t Size;
    if ( Id == NULL || Id[0] == '\0' || Id[0] == '.' ) return false;
    for ( Size = 0u; Size < MDO_PROJECT_LIFECYCLE_ID_CAPACITY; ++Size ) {
        unsigned char Ch = (unsigned char)Id[Size];
        if ( Ch == '\0' ) break;
        if ( Ch >= 'A' && Ch <= 'Z' ) Ch = (unsigned char)(Ch + 'a' - 'A');
        if ( !((Ch >= 'a' && Ch <= 'z') || (Ch >= '0' && Ch <= '9') ||
                Ch == '-' || Ch == '_' || Ch == '.') ) return false;
        Key[Size] = (char)Ch;
    }
    if ( Size == MDO_PROJECT_LIFECYCLE_ID_CAPACITY ) return false;
    while ( Size != 0u && Key[Size - 1u] == '.' ) --Size;
    Key[Size] = '\0';
    return Size != 0u;
}

static void MdoProjectLifecycleDispose(MdoProjectLeaseRegistry* Registry)
{
    MdoProjectLeaseEntry* Entry = Registry->Entries;
    while ( Entry != NULL ) {
        MdoProjectLeaseEntry* Next = Entry->Next;
        xrtFree(Entry);
        Entry = Next;
    }
    xrtMutexDestroy(Registry->Lock);
    xrtFree(Registry);
}

bool MdoProjectLifecycleInit(void)
{
    MdoProjectLeaseRegistry* Registry;
    if ( g_MdoProjectLeases != NULL ) return true;
    Registry = (MdoProjectLeaseRegistry*)xrtCalloc(1u, sizeof(*Registry));
    if ( Registry == NULL ) return false;
    Registry->Lock = xrtMutexCreate();
    if ( Registry->Lock == NULL ) { xrtFree(Registry); return false; }
    Registry->Refs = 1u;
    g_MdoProjectLeases = Registry;
    return true;
}

void MdoProjectLifecycleUnit(void)
{
    MdoProjectLeaseRegistry* Registry = g_MdoProjectLeases;
    bool Dispose;
    if ( Registry == NULL ) return;
    g_MdoProjectLeases = NULL;
    xrtMutexLock(Registry->Lock);
    Registry->Closed = true;
    Dispose = --Registry->Refs == 0u;
    xrtMutexUnlock(Registry->Lock);
    if ( Dispose ) MdoProjectLifecycleDispose(Registry);
}

MdoProjectLease* MdoProjectLeaseAcquire(const char* ProjectId,
    MdoProjectLeaseMode Mode, xwork_error* Error)
{
    MdoProjectLeaseRegistry* Registry = g_MdoProjectLeases;
    MdoProjectLeaseEntry* Entry;
    MdoProjectLease* Lease;
    char Key[MDO_PROJECT_LIFECYCLE_ID_CAPACITY];
    xwork_error_code Failure = XWORK_ERROR_NONE;

    xworkErrorInit(Error);
    if ( !MdoProjectLifecycleKey(ProjectId, Key) ||
         (Mode != MDO_PROJECT_LEASE_SHARED &&
          Mode != MDO_PROJECT_LEASE_EXCLUSIVE) ) {
        MdoProjectLifecycleError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "invalid project lifecycle lease request");
        return NULL;
    }
    if ( Registry == NULL ) {
        MdoProjectLifecycleError(Error, XWORK_ERROR_CONTEXT,
            "project lifecycle service is unavailable");
        return NULL;
    }
    Lease = (MdoProjectLease*)xrtCalloc(1u, sizeof(*Lease));
    if ( Lease == NULL ) {
        MdoProjectLifecycleError(Error, XWORK_ERROR_OUT_OF_MEMORY,
            "cannot allocate a project lifecycle lease");
        return NULL;
    }
    xrtMutexLock(Registry->Lock);
    for ( Entry = Registry->Entries; Entry != NULL; Entry = Entry->Next )
        if ( strcmp(Entry->Key, Key) == 0 ) break;
    if ( Registry->Closed || (Entry != NULL && (Entry->Exclusive ||
            (Mode == MDO_PROJECT_LEASE_EXCLUSIVE && Entry->Readers != 0u))) )
        Failure = XWORK_ERROR_CONTEXT;
    else if ( Registry->Refs == SIZE_MAX ||
              (Entry != NULL && Entry->Readers == SIZE_MAX) )
        Failure = XWORK_ERROR_LIMIT;
    else if ( Entry == NULL ) {
        Entry = (MdoProjectLeaseEntry*)xrtCalloc(1u, sizeof(*Entry));
        if ( Entry == NULL ) Failure = XWORK_ERROR_OUT_OF_MEMORY;
        else {
            memcpy(Entry->Key, Key, strlen(Key) + 1u);
            Entry->Next = Registry->Entries;
            Registry->Entries = Entry;
        }
    }
    if ( Failure == XWORK_ERROR_NONE ) {
        if ( Mode == MDO_PROJECT_LEASE_SHARED ) ++Entry->Readers;
        else Entry->Exclusive = true;
        ++Registry->Refs;
        Lease->Registry = Registry;
        Lease->Entry = Entry;
        Lease->Mode = Mode;
        xrtAtomic32Init(&Lease->Refs, 1u);
    }
    xrtMutexUnlock(Registry->Lock);
    if ( Failure != XWORK_ERROR_NONE ) {
        xrtFree(Lease);
        MdoProjectLifecycleError(Error, Failure,
            Failure == XWORK_ERROR_CONTEXT ? "project lifecycle is busy" :
            "cannot reserve the project lifecycle");
        return NULL;
    }
    return Lease;
}

MdoProjectLease* MdoProjectLeaseRef(MdoProjectLease* Lease)
{
    uint32 Refs;
    if ( Lease == NULL ) return NULL;
    Refs = xrtAtomic32Load(&Lease->Refs, XMEMORY_ACQUIRE);
    while ( Refs != 0u && Refs != UINT32_MAX ) {
        uint32 Expected = Refs;
        if ( xrtAtomic32CompareExchange(&Lease->Refs, &Expected, Refs + 1u,
                XMEMORY_ACQ_REL, XMEMORY_ACQUIRE) ) return Lease;
        Refs = Expected;
    }
    return NULL;
}

void MdoProjectLeaseRelease(MdoProjectLease* Lease)
{
    MdoProjectLeaseRegistry* Registry;
    MdoProjectLeaseEntry* Entry;
    MdoProjectLeaseEntry** Link;
    bool Dispose;
    uint32 Previous;
    if ( Lease == NULL ) return;
    Previous = xrtAtomic32FetchSub(&Lease->Refs, 1u, XMEMORY_ACQ_REL);
    if ( Previous > 1u ) return;
    if ( Previous == 0u ) abort();
    Registry = Lease->Registry;
    Entry = Lease->Entry;
    xrtMutexLock(Registry->Lock);
    if ( Lease->Mode == MDO_PROJECT_LEASE_SHARED ) --Entry->Readers;
    else Entry->Exclusive = false;
    /* Idle keys do not accumulate as users visit or create projects. */
    if ( Entry->Readers == 0u && !Entry->Exclusive ) {
        for ( Link = &Registry->Entries; *Link != NULL && *Link != Entry;
              Link = &(*Link)->Next ) {}
        if ( *Link != Entry ) abort();
        *Link = Entry->Next;
        xrtFree(Entry);
    }
    Dispose = --Registry->Refs == 0u;
    xrtMutexUnlock(Registry->Lock);
    xrtFree(Lease);
    if ( Dispose ) MdoProjectLifecycleDispose(Registry);
}
