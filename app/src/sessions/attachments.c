#include <stdio.h>
#include <string.h>

#include "../../include/mdo/attachments.h"
#include "../../include/mdo/home.h"
#include "../../include/mdo/sessions.h"
#include "../../include/mdo/project_lifecycle.h"
#include "data_gate.h"
#include "sidecars/binding.h"

#define MDO_IMAGE_FILE_MAX (8u * 1024u * 1024u)
#define MDO_IMAGE_META_MAX MDO_ATTACHMENT_META_MAX_BYTES
#define MDO_IMAGE_TRIM_LIMIT 65536u

typedef struct MdoImageRemovedRange {
    uint64 First;
    uint64 End;
} MdoImageRemovedRange;


static bool MdoImageRunPath(char Output[MDO_SESSION_PATH_CAPACITY],
    const char* ProjectId, const char* SessionId, uint64 AgentRunId)
{
    int Written;
    if ( ProjectId == NULL || SessionId == NULL || AgentRunId == 0u )
        return false;
    Written = snprintf(Output, MDO_SESSION_PATH_CAPACITY,
        "sessions/%s/%s/attachments/runs/%llu.json",
        ProjectId, SessionId, (unsigned long long)AgentRunId);
    return Written > 0 && (size_t)Written < MDO_SESSION_PATH_CAPACITY;
}

static bool MdoImageEventPath(char Output[MDO_SESSION_PATH_CAPACITY],
    const char* ProjectId, const char* SessionId, uint64 EventId)
{
    int Written;
    if ( ProjectId == NULL || SessionId == NULL || EventId == 0u )
        return false;
    Written = snprintf(Output, MDO_SESSION_PATH_CAPACITY,
        "sessions/%s/%s/attachments/events/%llu.json",
        ProjectId, SessionId, (unsigned long long)EventId);
    return Written > 0 && (size_t)Written < MDO_SESSION_PATH_CAPACITY;
}


static bool MdoImageRecordWrite(const char* Path, uint64 AgentRunId,
    const char Ids[4][33], size_t Count)
{
    char Document[MDO_IMAGE_RUN_RECORD_MAX];
    size_t Used;
    size_t i;
    int Written;
    if ( Path == NULL || AgentRunId == 0u ||
         (Count != 0u && Ids == NULL) || Count > 4u ) return false;
    Written = snprintf(Document, sizeof(Document),
        "{\"schema_version\":1,\"run_id\":%llu,\"attachments\":[",
        (unsigned long long)AgentRunId);
    if ( Written <= 0 || (size_t)Written >= sizeof(Document) ) return false;
    Used = (size_t)Written;
    for ( i = 0u; i < Count; ++i ) {
        char Checked[33];
        size_t j;
        if ( !MdoImageHexId(xrtStrView(Ids[i]), Checked) ) return false;
        for ( j = 0u; j < i; ++j )
            if ( strcmp(Ids[i], Ids[j]) == 0 ) return false;
        Written = snprintf(Document + Used, sizeof(Document) - Used,
            "%s\"%s\"", i != 0u ? "," : "", Ids[i]);
        if ( Written <= 0 || (size_t)Written >= sizeof(Document) - Used )
            return false;
        Used += (size_t)Written;
    }
    Written = snprintf(Document + Used, sizeof(Document) - Used, "]}");
    if ( Written != 2 ) return false;
    Used += (size_t)Written;
    return MdoHomeAtomicWrite(Path, Document, Used, false);
}

static bool MdoImageRecordRead(const char* Path, uint64 AgentRunId,
    char Ids[4][33], size_t* Count)
{
    char Document[MDO_IMAGE_RUN_RECORD_MAX + 1u];
    bool Exists = false;
    xfileinfo Info;
    xfile File = NULL;
    uint64 ParsedRun;
    bool Ok = false;
    if ( Path == NULL || Ids == NULL || Count == NULL ) return false;
    *Count = 0u;
    if ( !MdoHomeExternalStat(Path, &Exists, &Info) ) return false;
    if ( !Exists ) return true;
    if ( Info.Type != XFILE_TYPE_FILE || (Info.Available & XFILE_INFO_SIZE) == 0u ||
         Info.Size == 0u || Info.Size > MDO_IMAGE_RUN_RECORD_MAX ) return false;
    File = MdoHomeOpenRead(Path);
    if ( File != NULL && xrtReadFull(File, Document, (size_t)Info.Size, NULL) )
        Ok = MdoImageBindingParse(xrtStrViewN(Document, (size_t)Info.Size),
            AgentRunId, &ParsedRun, Ids, Count);
    if ( File != NULL && !xrtClose(File) ) Ok = false;
    if ( !Ok ) *Count = 0u;
    return Ok;
}

bool MdoSessionAttachmentRunRead(const char* ProjectId,
    const char* SessionId, uint64 AgentRunId,
    char Ids[4][33], size_t* Count)
{
    char Path[MDO_SESSION_PATH_CAPACITY];
    return MdoImageRunPath(Path, ProjectId, SessionId, AgentRunId) &&
        MdoImageRecordRead(Path, AgentRunId, Ids, Count);
}

bool MdoSessionAttachmentEventWrite(const char* ProjectId,
    const char* SessionId, uint64 EventId, uint64 AgentRunId,
    const char Ids[4][33], size_t Count)
{
    char EventPath[MDO_SESSION_PATH_CAPACITY];
    char LegacyPath[MDO_SESSION_PATH_CAPACITY];
    bool Exists;
    xfileinfo Info;
    MdoProjectLease* Lease;
    MdoSessionDataLease* DataLease = NULL;
    bool Ok = false;
    if ( !MdoImageEventPath(EventPath, ProjectId, SessionId, EventId) ||
         !MdoImageRunPath(LegacyPath, ProjectId, SessionId, AgentRunId) )
        return false;
    Lease = MdoProjectLeaseAcquire(ProjectId, MDO_PROJECT_LEASE_SHARED, NULL);
    if ( Lease == NULL ) return false;
    DataLease = MdoSessionDataAcquire(ProjectId, SessionId,
        MDO_SESSION_DATA_WRITE, NULL);
    if ( DataLease == NULL ) goto done;
    if ( Count == 0u ) {
        if ( !MdoHomeExternalStat(LegacyPath, &Exists, &Info) ) goto done;
        if ( !Exists ) { Ok = true; goto done; }
    }
    Ok = MdoImageRecordWrite(EventPath, AgentRunId, Ids, Count);
done:
    MdoProjectLeaseRelease(Lease);
    MdoSessionDataRelease(DataLease);
    return Ok;
}

bool MdoSessionAttachmentEventRead(const char* ProjectId,
    const char* SessionId, uint64 EventId, uint64 AgentRunId,
    char Ids[4][33], size_t* Count)
{
    char Path[MDO_SESSION_PATH_CAPACITY];
    bool Exists;
    xfileinfo Info;
    if ( !MdoImageEventPath(Path, ProjectId, SessionId, EventId) ||
         !MdoHomeExternalStat(Path, &Exists, &Info) ) return false;
    return Exists ? MdoImageRecordRead(Path, AgentRunId, Ids, Count) :
        MdoSessionAttachmentRunRead(ProjectId, SessionId, AgentRunId,
            Ids, Count);
}

static bool MdoImageRecordDirectoryReferences(const char* ProjectId,
    const char* SessionId, const char* Subdirectory, const char* Id,
    bool* Referenced)
{
    char Directory[MDO_SESSION_PATH_CAPACITY];
    bool Exists = false;
    xfileinfo Info;
    xdir Dir = NULL;
    xdirentry Entry;
    xdirnext Next = XDIR_NEXT_END;
    size_t Visited = 0u;
    bool Ok = true;
    int Written = snprintf(Directory, sizeof(Directory),
        "sessions/%s/%s/attachments/%s", ProjectId, SessionId,
        Subdirectory);
    if ( Written <= 0 || (size_t)Written >= sizeof(Directory) ||
         !MdoHomeExternalStat(Directory, &Exists, &Info) ) return false;
    if ( !Exists ) return true;
    if ( Info.Type != XFILE_TYPE_DIRECTORY ) return false;
    Dir = MdoHomeOpenDirectory(Directory, XDIR_STAT);
    if ( Dir == NULL ) return false;
    memset(&Entry, 0, sizeof(Entry));
    while ( (Next = xrtDirNext(Dir, &Entry)) == XDIR_NEXT_ITEM ) {
        char Path[MDO_SESSION_PATH_CAPACITY];
        char Ids[4][33] = {{ 0 }};
        size_t Count = 0u;
        size_t i;
        if ( ++Visited > 65536u ) { Ok = false; break; }
        if ( Entry.Info.Type != XFILE_TYPE_FILE ||
             Entry.Name.Size < 6u || Entry.Name.Size > 25u ||
             memcmp(Entry.Name.Data + Entry.Name.Size - 5u,
                ".json", 5u) != 0 ) continue;
        for ( i = 0u; i < Entry.Name.Size - 5u; ++i )
            if ( Entry.Name.Data[i] < '0' ||
                 Entry.Name.Data[i] > '9' ) break;
        if ( i != Entry.Name.Size - 5u ) continue;
        Written = snprintf(Path, sizeof(Path), "%s/%.*s", Directory,
            (int)Entry.Name.Size, Entry.Name.Data);
        if ( Written <= 0 || (size_t)Written >= sizeof(Path) ||
             !MdoImageRecordRead(Path, 0u, Ids, &Count) ) {
            Ok = false;
            break;
        }
        for ( i = 0u; i < Count; ++i )
            if ( strcmp(Ids[i], Id) == 0 ) { *Referenced = true; break; }
        if ( *Referenced ) break;
    }
    if ( Next == XDIR_NEXT_ERROR ) Ok = false;
    if ( !xrtDirClose(Dir) ) Ok = false;
    return Ok;
}

/* Check both current event references and legacy run references. The latter
 * may retain extra images, but never discards an older conversation image. */
bool MdoSessionAttachmentRecordReferenced(const char* ProjectId,
    const char* SessionId, const char* Id, bool* Referenced)
{
    if ( ProjectId == NULL || SessionId == NULL || Id == NULL ||
         Referenced == NULL ) return false;
    *Referenced = false;
    return MdoImageRecordDirectoryReferences(ProjectId, SessionId,
        "events", Id, Referenced) &&
        (*Referenced || MdoImageRecordDirectoryReferences(ProjectId,
            SessionId, "runs", Id, Referenced));
}

/* Event reference files survive journal retention. Remove only IDs covered
 * by durable history markers; a retry can repair a crash after journal write. */
bool MdoSessionAttachmentPruneRemoved(const char* ProjectId,
    const char* SessionId)
{
    MdoImageRemovedRange* Ranges = NULL;
    uint64* Removed = NULL;
    size_t RangeCount = 0u;
    size_t RangeCapacity = 0u;
    size_t RemovedCount = 0u;
    size_t RemovedCapacity = 0u;
    uint64 Cursor = 0u;
    char Directory[MDO_SESSION_PATH_CAPACITY];
    bool Exists = false;
    xfileinfo Info;
    xdir Dir = NULL;
    xdirentry Entry;
    xdirnext Next = XDIR_NEXT_END;
    size_t Visited = 0u;
    size_t i;
    bool Ok = false;
    int Written;
    MdoSessionDataLease* DataLease = NULL;
    MdoProjectLease* Lease = MdoProjectLeaseAcquire(ProjectId,
        MDO_PROJECT_LEASE_SHARED, NULL);
    if ( Lease == NULL ) return false;
    DataLease = MdoSessionDataAcquire(ProjectId, SessionId,
        MDO_SESSION_DATA_WRITE, NULL);
    if ( DataLease == NULL ) goto done;
    for ( ; ; ) {
        MdoSessionEventSnapshot* Snapshot;
        xwork_error Error;
        size_t PageCount;
        uint64 NextCursor, Latest;
        memset(&Error, 0, sizeof(Error));
        Snapshot = MdoSessionEventReplay(ProjectId, SessionId, Cursor,
            1000u, &Error);
        if ( Snapshot == NULL ) goto done;
        PageCount = MdoSessionEventSnapshotCount(Snapshot);
        for ( i = 0u; i < PageCount; ++i ) {
            MdoSessionEventInfo Event;
            MdoImageRemovedRange* Grown;
            memset(&Event, 0, sizeof(Event)); Event.Size = sizeof(Event);
            if ( !MdoSessionEventSnapshotAt(Snapshot, i, &Event) ) {
                MdoSessionEventSnapshotRelease(Snapshot);
                goto done;
            }
            if ( Event.Kind != MDO_SESSION_EVENT_HISTORY_TRUNCATED ||
                 Event.SourceEventId == 0u ||
                 Event.SourceEventId >= Event.EventId ) continue;
            if ( RangeCount == MDO_IMAGE_TRIM_LIMIT ) {
                MdoSessionEventSnapshotRelease(Snapshot);
                goto done;
            }
            if ( RangeCount == RangeCapacity ) {
                size_t Capacity = RangeCapacity != 0u ?
                    RangeCapacity * 2u : 16u;
                if ( Capacity > MDO_IMAGE_TRIM_LIMIT )
                    Capacity = MDO_IMAGE_TRIM_LIMIT;
                Grown = (MdoImageRemovedRange*)xrtRealloc(Ranges,
                    Capacity * sizeof(*Ranges));
                if ( Grown == NULL ) {
                    MdoSessionEventSnapshotRelease(Snapshot);
                    goto done;
                }
                Ranges = Grown;
                RangeCapacity = Capacity;
            }
            Ranges[RangeCount].First = Event.SourceEventId;
            Ranges[RangeCount].End = Event.EventId;
            ++RangeCount;
        }
        NextCursor = MdoSessionEventSnapshotNextCursor(Snapshot);
        Latest = MdoSessionEventSnapshotLatestId(Snapshot);
        MdoSessionEventSnapshotRelease(Snapshot);
        if ( PageCount < 1000u || NextCursor >= Latest ) break;
        if ( NextCursor <= Cursor ) goto done;
        Cursor = NextCursor;
    }
    if ( RangeCount == 0u ) { Ok = true; goto done; }
    Written = snprintf(Directory, sizeof(Directory),
        "sessions/%s/%s/attachments/events", ProjectId, SessionId);
    if ( Written <= 0 || (size_t)Written >= sizeof(Directory) ||
         !MdoHomeExternalStat(Directory, &Exists, &Info) ) goto done;
    if ( !Exists ) { Ok = true; goto done; }
    if ( Info.Type != XFILE_TYPE_DIRECTORY ) goto done;
    Dir = MdoHomeOpenDirectory(Directory, XDIR_STAT);
    if ( Dir == NULL ) goto done;
    memset(&Entry, 0, sizeof(Entry));
    while ( (Next = xrtDirNext(Dir, &Entry)) == XDIR_NEXT_ITEM ) {
        uint64 EventId = 0u;
        size_t Length;
        if ( ++Visited > MDO_IMAGE_TRIM_LIMIT ) goto done;
        if ( Entry.Info.Type != XFILE_TYPE_FILE ||
             Entry.Name.Size < 6u || Entry.Name.Size > 25u ||
             memcmp(Entry.Name.Data + Entry.Name.Size - 5u,
                ".json", 5u) != 0 ) continue;
        Length = Entry.Name.Size - 5u;
        if ( Length > 1u && Entry.Name.Data[0] == '0' ) continue;
        for ( i = 0u; i < Length; ++i ) {
            unsigned char Digit = (unsigned char)Entry.Name.Data[i];
            if ( Digit < '0' || Digit > '9' ||
                 EventId > (UINT64_MAX - (Digit - '0')) / 10u ) break;
            EventId = EventId * 10u + (Digit - '0');
        }
        if ( i != Length || EventId == 0u ) continue;
        for ( i = 0u; i < RangeCount; ++i )
            if ( EventId >= Ranges[i].First &&
                 EventId < Ranges[i].End ) break;
        if ( i == RangeCount ) continue;
        if ( RemovedCount == MDO_IMAGE_TRIM_LIMIT ) goto done;
        if ( RemovedCount == RemovedCapacity ) {
            size_t Capacity = RemovedCapacity != 0u ?
                RemovedCapacity * 2u : 16u;
            uint64* Grown;
            if ( Capacity > MDO_IMAGE_TRIM_LIMIT )
                Capacity = MDO_IMAGE_TRIM_LIMIT;
            Grown = (uint64*)xrtRealloc(Removed,
                Capacity * sizeof(*Removed));
            if ( Grown == NULL ) goto done;
            Removed = Grown;
            RemovedCapacity = Capacity;
        }
        Removed[RemovedCount++] = EventId;
    }
    if ( Next == XDIR_NEXT_ERROR ) goto done;
    if ( !xrtDirClose(Dir) ) { Dir = NULL; goto done; }
    Dir = NULL;
    for ( i = 0u; i < RemovedCount; ++i ) {
        char Path[MDO_SESSION_PATH_CAPACITY];
        Written = snprintf(Path, sizeof(Path), "%s/%llu.json", Directory,
            (unsigned long long)Removed[i]);
        if ( Written <= 0 || (size_t)Written >= sizeof(Path) ||
             !MdoHomeRemove(Path, false) ) goto done;
    }
    Ok = true;
done:
    if ( Dir != NULL ) (void)xrtDirClose(Dir);
    xrtFree(Removed);
    xrtFree(Ranges);
    MdoProjectLeaseRelease(Lease);
    MdoSessionDataRelease(DataLease);
    return Ok;
}

static bool MdoImageFilePath(char Path[MDO_SESSION_PATH_CAPACITY],
    const char* ProjectId, const char* SessionId, const char* Id,
    const char* Extension)
{
    int Written = snprintf(Path, MDO_SESSION_PATH_CAPACITY,
        "sessions/%s/%s/attachments/%s.%s", ProjectId, SessionId,
        Id, Extension);
    return Written > 0 && (size_t)Written < MDO_SESSION_PATH_CAPACITY;
}

static bool MdoImageCopyFile(const char* SourcePath, const char* TargetPath,
    size_t Limit)
{
    xfileinfo Info;
    bool Exists;
    xfile File = NULL;
    char* Data = NULL;
    bool Ok = false;
    if ( !MdoHomeExternalStat(SourcePath, &Exists, &Info) || !Exists ||
         Info.Type != XFILE_TYPE_FILE ||
         (Info.Available & XFILE_INFO_SIZE) == 0u ||
         Info.Size == 0u || Info.Size > Limit ) return false;
    Data = (char*)xrtMalloc((size_t)Info.Size);
    if ( Data == NULL ) return false;
    File = MdoHomeOpenRead(SourcePath);
    if ( File == NULL ||
         !xrtReadFull(File, Data, (size_t)Info.Size, NULL) ) goto done;
    if ( !xrtClose(File) ) { File = NULL; goto done; }
    File = NULL;
    Ok = MdoHomeAtomicWrite(TargetPath, Data, (size_t)Info.Size, false);
done:
    if ( File != NULL ) (void)xrtClose(File);
    xrtFree(Data);
    return Ok;
}

bool MdoSessionAttachmentEventClone(const char* SourceProjectId,
    const char* SourceSessionId, uint64 SourceEventId,
    const char* TargetProjectId, const char* TargetSessionId,
    uint64 TargetEventId, uint64 AgentRunId)
{
    char Ids[4][33] = {{ 0 }};
    size_t Count = 0u;
    size_t i;
    MdoProjectLease* SourceLease = NULL;
    MdoProjectLease* TargetLease = NULL;
    MdoSessionDataLease* SourceDataLease = NULL;
    MdoSessionDataLease* TargetDataLease = NULL;
    bool Ok = false;
    /* Older UI journals may contain a synthetic top-level start without a
     * runtime run identity. Such events could never own uploaded images. */
    if ( AgentRunId == 0u ) return true;
    SourceLease = MdoProjectLeaseAcquire(SourceProjectId,
        MDO_PROJECT_LEASE_SHARED, NULL);
    if ( SourceLease == NULL ) return false;
    TargetLease = MdoProjectLeaseAcquire(TargetProjectId,
        MDO_PROJECT_LEASE_SHARED, NULL);
    if ( TargetLease == NULL ) goto done;
    SourceDataLease = MdoSessionDataAcquire(SourceProjectId, SourceSessionId,
        MDO_SESSION_DATA_WRITE, NULL);
    if ( SourceDataLease == NULL ) goto done;
    TargetDataLease = MdoSessionDataAcquire(TargetProjectId, TargetSessionId,
        MDO_SESSION_DATA_WRITE, NULL);
    if ( TargetDataLease == NULL ) goto done;
    if ( !MdoSessionAttachmentEventRead(SourceProjectId, SourceSessionId,
            SourceEventId, AgentRunId, Ids, &Count) ) goto done;
    for ( i = 0u; i < Count; ++i ) {
        char SourceData[MDO_SESSION_PATH_CAPACITY];
        char SourceMeta[MDO_SESSION_PATH_CAPACITY];
        char TargetData[MDO_SESSION_PATH_CAPACITY];
        char TargetMeta[MDO_SESSION_PATH_CAPACITY];
        bool DataExists;
        bool MetaExists;
        xfileinfo Info;
        if ( !MdoImageFilePath(SourceData, SourceProjectId,
                SourceSessionId, Ids[i], "bin") ||
             !MdoImageFilePath(SourceMeta, SourceProjectId,
                SourceSessionId, Ids[i], "json") ||
             !MdoImageFilePath(TargetData, TargetProjectId,
                TargetSessionId, Ids[i], "bin") ||
             !MdoImageFilePath(TargetMeta, TargetProjectId,
                TargetSessionId, Ids[i], "json") ||
             !MdoHomeExternalStat(TargetData, &DataExists, &Info) ||
             !MdoHomeExternalStat(TargetMeta, &MetaExists, &Info) ||
             DataExists != MetaExists ) goto done;
        if ( DataExists ) continue;
        if ( !MdoImageCopyFile(SourceData, TargetData,
                MDO_IMAGE_FILE_MAX) ||
             !MdoImageCopyFile(SourceMeta, TargetMeta,
                MDO_IMAGE_META_MAX) ) goto done;
    }
    Ok = Count == 0u || MdoSessionAttachmentEventWrite(TargetProjectId,
        TargetSessionId, TargetEventId, AgentRunId, Ids, Count);
done:
    MdoProjectLeaseRelease(TargetLease);
    MdoProjectLeaseRelease(SourceLease);
    MdoSessionDataRelease(TargetDataLease);
    MdoSessionDataRelease(SourceDataLease);
    return Ok;
}

static void MdoImageRollbackFiles(const char* Directory)
{
    xfileinfo Info;
    bool Exists;
    xdir Dir;
    xdirentry Entry;
    xdirnext Next;
    size_t Visited = 0u;
    if ( !MdoHomeExternalStat(Directory, &Exists, &Info) || !Exists ||
         Info.Type != XFILE_TYPE_DIRECTORY ) return;
    Dir = MdoHomeOpenDirectory(Directory, XDIR_STAT);
    if ( Dir == NULL ) return;
    memset(&Entry, 0, sizeof(Entry));
    while ( Visited++ < 8192u &&
            (Next = xrtDirNext(Dir, &Entry)) == XDIR_NEXT_ITEM ) {
        char Path[MDO_SESSION_PATH_CAPACITY];
        size_t i;
        int Written;
        if ( Entry.Info.Type != XFILE_TYPE_FILE ||
             Entry.Name.Size == 0u || Entry.Name.Size > 64u ) continue;
        for ( i = 0u; i < Entry.Name.Size; ++i )
            if ( Entry.Name.Data[i] == '/' ||
                 Entry.Name.Data[i] == '\\' ) break;
        if ( i != Entry.Name.Size ) continue;
        Written = snprintf(Path, sizeof(Path), "%s/%.*s", Directory,
            (int)Entry.Name.Size, Entry.Name.Data);
        if ( Written > 0 && (size_t)Written < sizeof(Path) )
            (void)MdoHomeRemove(Path, false);
        xrtClearError();
    }
    (void)xrtDirClose(Dir);
    xrtClearError();
    (void)MdoHomeRemoveEmptyDirectory(Directory);
    xrtClearError();
}

void MdoSessionAttachmentForkRollback(const char* ProjectId,
    const char* SessionId)
{
    char Directory[MDO_SESSION_PATH_CAPACITY];
    int Written;
    MdoProjectLease* Lease;
    MdoSessionDataLease* DataLease;
    if ( ProjectId == NULL || SessionId == NULL ) return;
    Lease = MdoProjectLeaseAcquire(ProjectId, MDO_PROJECT_LEASE_SHARED, NULL);
    if ( Lease == NULL ) return;
    DataLease = MdoSessionDataAcquire(ProjectId, SessionId,
        MDO_SESSION_DATA_WRITE, NULL);
    if ( DataLease == NULL ) { MdoProjectLeaseRelease(Lease); return; }
    Written = snprintf(Directory, sizeof(Directory),
        "sessions/%s/%s/attachments/events", ProjectId, SessionId);
    if ( Written > 0 && (size_t)Written < sizeof(Directory) )
        MdoImageRollbackFiles(Directory);
    Written = snprintf(Directory, sizeof(Directory),
        "sessions/%s/%s/attachments/runs", ProjectId, SessionId);
    if ( Written > 0 && (size_t)Written < sizeof(Directory) )
        MdoImageRollbackFiles(Directory);
    Written = snprintf(Directory, sizeof(Directory),
        "sessions/%s/%s/attachments", ProjectId, SessionId);
    if ( Written > 0 && (size_t)Written < sizeof(Directory) )
        MdoImageRollbackFiles(Directory);
    MdoProjectLeaseRelease(Lease);
    MdoSessionDataRelease(DataLease);
}
