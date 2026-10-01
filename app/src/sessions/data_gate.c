#include <stdlib.h>
#include <string.h>

#include "data_gate.h"

#define MDO_SESSION_DATA_ID_CAPACITY 65u
#define MDO_SESSION_DATA_KEY_CAPACITY (2u * MDO_SESSION_DATA_ID_CAPACITY)

typedef struct MdoSessionDataEntry {
    struct MdoSessionDataEntry* Next;
    char Key[MDO_SESSION_DATA_KEY_CAPACITY];
    size_t Writers;
    bool Capturing;
} MdoSessionDataEntry;

typedef struct MdoSessionDataRegistry {
    xmutex* Lock;
    MdoSessionDataEntry* Entries;
    size_t Refs;
    bool Closed;
} MdoSessionDataRegistry;

struct MdoSessionDataLease {
    MdoSessionDataRegistry* Registry;
    MdoSessionDataEntry* Entry;
    MdoSessionDataMode Mode;
};

static MdoSessionDataRegistry* g_MdoSessionData;

static void MdoSessionDataError(xwork_error* Error, xwork_error_code Code,
    const char* Message)
{
    if ( Error == NULL ) return;
    xworkErrorInit(Error);
    Error->eCode = Code;
    snprintf(Error->sMessage, sizeof(Error->sMessage), "%s", Message);
}

static bool MdoSessionDataId(const char* Id,
    char Key[MDO_SESSION_DATA_ID_CAPACITY])
{
    size_t Size;
    if ( Id == NULL || Id[0] == '\0' || Id[0] == '.' ) return false;
    for ( Size = 0u; Size < MDO_SESSION_DATA_ID_CAPACITY; ++Size ) {
        unsigned char Byte = (unsigned char)Id[Size];
        if ( Byte == '\0' ) break;
        if ( Byte >= 'A' && Byte <= 'Z' ) Byte += 'a' - 'A';
        if ( !((Byte >= 'a' && Byte <= 'z') ||
               (Byte >= '0' && Byte <= '9') || Byte == '-' ||
               Byte == '_' || Byte == '.') ) return false;
        Key[Size] = (char)Byte;
    }
    if ( Size == MDO_SESSION_DATA_ID_CAPACITY ) return false;
    while ( Size != 0u && Key[Size - 1u] == '.' ) --Size;
    Key[Size] = '\0';
    return Size != 0u;
}

static void MdoSessionDataDispose(MdoSessionDataRegistry* Registry)
{
    /* Every key is removed by its last owner; a closed registry has no idle
     * entries and no writer can outlive the final registry reference. */
    if ( Registry->Entries != NULL ) abort();
    xrtMutexDestroy(Registry->Lock);
    xrtFree(Registry);
}

bool MdoSessionDataInit(void)
{
    MdoSessionDataRegistry* Registry;
    if ( g_MdoSessionData != NULL ) return true;
    Registry = (MdoSessionDataRegistry*)xrtCalloc(1u, sizeof(*Registry));
    if ( Registry == NULL ) return false;
    Registry->Lock = xrtMutexCreate();
    if ( Registry->Lock == NULL ) { xrtFree(Registry); return false; }
    Registry->Refs = 1u;
    g_MdoSessionData = Registry;
    return true;
}

void MdoSessionDataUnit(void)
{
    MdoSessionDataRegistry* Registry = g_MdoSessionData;
    bool Dispose;
    if ( Registry == NULL ) return;
    g_MdoSessionData = NULL;
    xrtMutexLock(Registry->Lock);
    Registry->Closed = true;
    Dispose = --Registry->Refs == 0u;
    xrtMutexUnlock(Registry->Lock);
    if ( Dispose ) MdoSessionDataDispose(Registry);
}

MdoSessionDataLease* MdoSessionDataAcquire(const char* ProjectId,
    const char* SessionId, MdoSessionDataMode Mode, xwork_error* Error)
{
    MdoSessionDataRegistry* Registry = g_MdoSessionData;
    MdoSessionDataEntry* Entry;
    MdoSessionDataLease* Lease;
    char Project[MDO_SESSION_DATA_ID_CAPACITY];
    char Session[MDO_SESSION_DATA_ID_CAPACITY];
    char Key[MDO_SESSION_DATA_KEY_CAPACITY];
    xwork_error_code Failure = XWORK_ERROR_NONE;
    xworkErrorInit(Error);
    if ( !MdoSessionDataId(ProjectId, Project) ||
         !MdoSessionDataId(SessionId, Session) ||
         (Mode != MDO_SESSION_DATA_WRITE && Mode != MDO_SESSION_DATA_CAPTURE) ) {
        MdoSessionDataError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "invalid session data lease request");
        return NULL;
    }
    if ( Registry == NULL ) {
        MdoSessionDataError(Error, XWORK_ERROR_CONTEXT,
            "session data service is unavailable");
        return NULL;
    }
    snprintf(Key, sizeof(Key), "%s/%s", Project, Session);
    Lease = (MdoSessionDataLease*)xrtMalloc(sizeof(*Lease));
    if ( Lease == NULL ) {
        MdoSessionDataError(Error, XWORK_ERROR_OUT_OF_MEMORY,
            "cannot allocate the session data lease");
        return NULL;
    }
    /* The registry mutex protects only counters and key allocation, never I/O
     * or callbacks. Busy file operations therefore cannot stall acquisition. */
    xrtMutexLock(Registry->Lock);
    for ( Entry = Registry->Entries; Entry != NULL; Entry = Entry->Next )
        if ( strcmp(Entry->Key, Key) == 0 ) break;
    if ( Registry->Closed || (Entry != NULL && (Entry->Capturing ||
            (Mode == MDO_SESSION_DATA_CAPTURE && Entry->Writers != 0u))) )
        Failure = XWORK_ERROR_CONTEXT;
    else if ( Registry->Refs == SIZE_MAX ||
              (Entry != NULL && Entry->Writers == SIZE_MAX) )
        Failure = XWORK_ERROR_LIMIT;
    else if ( Entry == NULL ) {
        Entry = (MdoSessionDataEntry*)xrtCalloc(1u, sizeof(*Entry));
        if ( Entry == NULL ) Failure = XWORK_ERROR_OUT_OF_MEMORY;
        else {
            memcpy(Entry->Key, Key, strlen(Key) + 1u);
            Entry->Next = Registry->Entries;
            Registry->Entries = Entry;
        }
    }
    if ( Failure == XWORK_ERROR_NONE ) {
        if ( Mode == MDO_SESSION_DATA_WRITE ) ++Entry->Writers;
        else Entry->Capturing = true;
        ++Registry->Refs;
        Lease->Registry = Registry;
        Lease->Entry = Entry;
        Lease->Mode = Mode;
    }
    xrtMutexUnlock(Registry->Lock);
    if ( Failure != XWORK_ERROR_NONE ) {
        xrtFree(Lease);
        MdoSessionDataError(Error, Failure,
            Failure == XWORK_ERROR_CONTEXT ? "session data is busy" :
                "cannot reserve session data");
        return NULL;
    }
    return Lease;
}

void MdoSessionDataRelease(MdoSessionDataLease* Lease)
{
    MdoSessionDataRegistry* Registry;
    MdoSessionDataEntry* Entry;
    MdoSessionDataEntry** Link;
    bool Dispose;
    if ( Lease == NULL ) return;
    Registry = Lease->Registry;
    Entry = Lease->Entry;
    xrtMutexLock(Registry->Lock);
    if ( Lease->Mode == MDO_SESSION_DATA_WRITE ) --Entry->Writers;
    else Entry->Capturing = false;
    if ( Entry->Writers == 0u && !Entry->Capturing ) {
        for ( Link = &Registry->Entries; *Link != NULL && *Link != Entry;
              Link = &(*Link)->Next ) {}
        if ( *Link != Entry ) abort();
        *Link = Entry->Next;
        xrtFree(Entry);
    }
    Dispose = --Registry->Refs == 0u;
    xrtMutexUnlock(Registry->Lock);
    xrtFree(Lease);
    if ( Dispose ) MdoSessionDataDispose(Registry);
}
