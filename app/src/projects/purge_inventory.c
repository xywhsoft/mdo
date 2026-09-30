#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../include/mdo/home.h"
#include "../../include/mdo/project_purge.h"
#include "../schedules/internal.h"

#define MDO_PURGE_SCHEDULE_BYTES (256u * 1024u)

struct MdoProjectPurgeInventory {
    MdoProjectPurgeInventoryInfo Info;
    MdoProjectPurgeTarget* Targets;
    size_t Capacity, Nodes;
};

static bool MdoPurgeError(xwork_error* Error, xwork_error_code Code,
    const char* Message)
{
    if ( Error != NULL ) {
        xworkErrorInit(Error);
        Error->eCode = Code;
        snprintf(Error->sMessage, sizeof(Error->sMessage), "%s", Message);
    }
    return false;
}

static bool MdoPurgeIdentity(const xfileinfo* Info)
{
    return (Info->Available & XFILE_INFO_IDENTITY) != 0u && Info->Identity != 0u;
}

static bool MdoPurgeSame(const xfileinfo* A, const xfileinfo* B)
{
    return MdoPurgeIdentity(A) && MdoPurgeIdentity(B) && A->Type == B->Type &&
        A->Device == B->Device && A->Identity == B->Identity &&
        (A->Type != XFILE_TYPE_FILE || A->Size == B->Size);
}

static bool MdoPurgeName(xstrview Name, const char* Text)
{
    return Name.Size == strlen(Text) && memcmp(Name.Data, Text, Name.Size) == 0;
}

static bool MdoPurgeComponent(xstrview Name)
{
    size_t i;
    if ( Name.Size == 0u || Name.Size > 255u || !xrtUtf8Valid(Name, NULL) ||
         MdoPurgeName(Name, ".") || MdoPurgeName(Name, "..") ) return false;
    for ( i = 0u; i < Name.Size; ++i ) {
        unsigned char Ch = (unsigned char)Name.Data[i];
        if ( Ch < 0x20u || Ch == 0x7fu || Ch == '/' || Ch == '\\' || Ch == ':' )
            return false;
    }
    return true;
}

static bool MdoPurgeTree(MdoProjectPurgeInventory* Inventory, const char* Path,
    const xfileinfo* Before, unsigned Depth, xwork_error* Error)
{
    xdir Directory = NULL;
    xdirentry Entry;
    xdirnext Next;
    xfileinfo After;
    bool Exists, Ok = false;
    if ( Depth > MDO_PROJECT_PURGE_DEPTH_LIMIT ||
         ++Inventory->Nodes > MDO_PROJECT_PURGE_NODE_LIMIT )
        return MdoPurgeError(Error, XWORK_ERROR_LIMIT,
            "project purge inventory exceeds its traversal limit");
    if ( !MdoPurgeIdentity(Before) )
        return MdoPurgeError(Error, XWORK_ERROR_IO,
            "project purge inventory requires stable filesystem identities");
    if ( Before->Type == XFILE_TYPE_FILE ) {
        if ( (Before->Available & XFILE_INFO_SIZE) == 0u ||
             Before->Size > UINT64_MAX - Inventory->Info.Bytes )
            return MdoPurgeError(Error, XWORK_ERROR_LIMIT,
                "project purge inventory byte count is unavailable or exhausted");
        ++Inventory->Info.Files;
        Inventory->Info.Bytes += Before->Size;
        return true;
    }
    if ( Before->Type != XFILE_TYPE_DIRECTORY )
        return MdoPurgeError(Error, XWORK_ERROR_IO,
            "project purge inventory contains a link or unsupported entry");
    ++Inventory->Info.Directories;
    Directory = MdoHomeOpenDirectory(Path, XDIR_STAT);
    if ( Directory == NULL ) goto done;
    memset(&Entry, 0, sizeof(Entry));
    while ( (Next = xrtDirNext(Directory, &Entry)) == XDIR_NEXT_ITEM ) {
        char Child[4096];
        xfileinfo ChildInfo;
        bool ChildExists;
        int Written;
        if ( (Entry.Flags & XDIR_ENTRY_UTF8) == 0u || !MdoPurgeComponent(Entry.Name) )
            goto unsupported;
        Written = snprintf(Child, sizeof(Child), "%s/%.*s", Path,
            (int)Entry.Name.Size, Entry.Name.Data);
        if ( Written <= 0 || (size_t)Written >= sizeof(Child) ) {
            (void)MdoPurgeError(Error, XWORK_ERROR_LIMIT,
                "project purge inventory contains an overlong path");
            goto done;
        }
        if ( !MdoHomeExternalStat(Child, &ChildExists, &ChildInfo) || !ChildExists ||
             ChildInfo.Type != Entry.Info.Type ) goto changed;
        if ( !MdoPurgeTree(Inventory, Child, &ChildInfo, Depth + 1u, Error) ) goto done;
    }
    if ( Next == XDIR_NEXT_ERROR ||
         !MdoHomeExternalStat(Path, &Exists, &After) || !Exists ||
         !MdoPurgeSame(Before, &After) ) goto changed;
    Ok = true;
    goto done;
unsupported:
    (void)MdoPurgeError(Error, XWORK_ERROR_IO,
        "project purge inventory contains an unsupported filename");
    goto done;
changed:
    (void)MdoPurgeError(Error, XWORK_ERROR_CONTEXT,
        "project purge inventory changed during traversal");
done:
    if ( Directory != NULL && !xrtDirClose(Directory) ) Ok = false;
    if ( !Ok && (Error == NULL || Error->eCode == XWORK_ERROR_NONE) )
        (void)MdoPurgeError(Error, XWORK_ERROR_IO,
            "project purge inventory could not read a directory");
    return Ok;
}

static bool MdoPurgeAdd(MdoProjectPurgeInventory* Inventory, const char* Path,
    bool Directory, bool Required, bool* Present, xwork_error* Error)
{
    xfileinfo Info;
    MdoProjectPurgeTarget* Target;
    size_t i;
    bool Exists;
    if ( Present != NULL ) *Present = false;
    if ( !MdoHomeExternalStat(Path, &Exists, &Info) )
        return MdoPurgeError(Error, XWORK_ERROR_IO, "cannot inspect a purge target");
    if ( !Exists ) return !Required ? true : MdoPurgeError(Error,
        XWORK_ERROR_CONTEXT, "a required purge target is missing");
    if ( Info.Type != (Directory ? XFILE_TYPE_DIRECTORY : XFILE_TYPE_FILE) )
        return MdoPurgeError(Error, XWORK_ERROR_IO,
            "project purge target has an unexpected type");
    if ( strlen(Path) >= MDO_PROJECT_PURGE_PATH_CAPACITY )
        return MdoPurgeError(Error, XWORK_ERROR_LIMIT, "purge target path is too long");
    for ( i = 0u; i < Inventory->Info.Targets; ++i )
        if ( strcmp(Inventory->Targets[i].Path, Path) == 0 )
            return MdoPurgeError(Error, XWORK_ERROR_IO, "duplicate purge target");
    if ( Inventory->Info.Targets >= MDO_PROJECT_PURGE_TARGET_LIMIT )
        return MdoPurgeError(Error, XWORK_ERROR_LIMIT, "too many purge targets");
    if ( Inventory->Info.Targets == Inventory->Capacity ) {
        size_t Capacity = Inventory->Capacity != 0u ? Inventory->Capacity * 2u : 16u;
        void* Targets = xrtRealloc(Inventory->Targets, Capacity * sizeof(*Target));
        if ( Targets == NULL ) return MdoPurgeError(Error, XWORK_ERROR_OUT_OF_MEMORY,
            "cannot allocate project purge targets");
        Inventory->Targets = (MdoProjectPurgeTarget*)Targets;
        Inventory->Capacity = Capacity;
    }
    if ( !MdoPurgeTree(Inventory, Path, &Info, 0u, Error) ) return false;
    Target = &Inventory->Targets[Inventory->Info.Targets++];
    memset(Target, 0, sizeof(*Target));
    snprintf(Target->Path, sizeof(Target->Path), "%s", Path);
    Target->Info = Info;
    if ( Present != NULL ) *Present = true;
    return true;
}

static bool MdoPurgeScheduleRead(const char* Path, const MdoScheduleInfo* Expected,
    bool Backup, xwork_error* Error)
{
    xfile File = NULL;
    xfileinfo Before, After;
    char* Text = NULL;
    MdoScheduleInfo Actual;
    bool Exists, Ok = false;
    if ( !MdoHomeExternalStat(Path, &Exists, &Before) || !Exists ||
         Before.Type != XFILE_TYPE_FILE || !MdoPurgeIdentity(&Before) ||
         (Before.Available & XFILE_INFO_SIZE) == 0u ||
         Before.Size > MDO_PURGE_SCHEDULE_BYTES ) goto done;
    Text = (char*)xrtMalloc((size_t)Before.Size + 1u);
    File = Text != NULL ? MdoHomeOpenRead(Path) : NULL;
    if ( File == NULL || !xrtReadFull(File, Text, (size_t)Before.Size, NULL) ) goto done;
    Ok = xrtClose(File); File = NULL;
    Text[Before.Size] = '\0';
    Ok = Ok && MdoSchedulesInternalParse(Expected->Id,
        xrtStrViewN(Text, (size_t)Before.Size), &Actual) &&
        MdoHomeExternalStat(Path, &Exists, &After) && Exists && MdoPurgeSame(&Before, &After);
    /* A backup belongs to the schedule's current namespace even if the
     * schedule was reassigned. Do not infer current ownership from old JSON. */
    if ( Ok && !Backup ) Ok = strcmp(Actual.ProjectId, Expected->ProjectId) == 0 &&
        Actual.Revision == Expected->Revision;
done:
    if ( File != NULL && !xrtClose(File) ) Ok = false;
    xrtFree(Text);
    return Ok ? true : MdoPurgeError(Error, XWORK_ERROR_IO,
        "schedule files do not match the current valid catalog");
}

static bool MdoPurgeScheduleName(xstrview Name, bool History,
    char Id[MDO_SCHEDULE_ID_CAPACITY], bool* Backup)
{
    const char* Suffix = History ? ".jsonl" : ".json";
    size_t Size, i;
    *Backup = false;
    if ( !History && Name.Size > 9u &&
         memcmp(Name.Data + Name.Size - 9u, ".json.bak", 9u) == 0 ) {
        Suffix = ".json.bak";
        *Backup = true;
    }
    Size = strlen(Suffix);
    if ( Name.Size <= Size || memcmp(Name.Data + Name.Size - Size, Suffix, Size) != 0 )
        return false;
    Size = Name.Size - Size;
    if ( Size >= MDO_SCHEDULE_ID_CAPACITY || Name.Data[0] == '.' ) return false;
    for ( i = 0u; i < Size; ++i ) {
        unsigned char Ch = (unsigned char)Name.Data[i];
        if ( !((Ch >= 'a' && Ch <= 'z') || (Ch >= 'A' && Ch <= 'Z') ||
                (Ch >= '0' && Ch <= '9') || Ch == '-' || Ch == '_' || Ch == '.') )
            return false;
    }
    memcpy(Id, Name.Data, Size); Id[Size] = '\0';
    return true;
}

static bool MdoPurgeSchedules(MdoProjectPurgeInventory* Inventory,
    const MdoScheduleCatalog* Catalog, bool History, xwork_error* Error)
{
    const char* Base = History ? "schedules/history" : "schedules";
    xfileinfo Before, After;
    xdir Directory = NULL;
    xdirentry Entry;
    xdirnext Next;
    bool Exists, Ok = false;
    size_t Count = 0u, Primaries = 0u;
    if ( !MdoHomeExternalStat(Base, &Exists, &Before) ) goto done;
    if ( !Exists ) return History || MdoScheduleCatalogCount(Catalog) == 0u ? true :
        MdoPurgeError(Error, XWORK_ERROR_CONTEXT, "schedule directory is missing");
    if ( Before.Type != XFILE_TYPE_DIRECTORY || !MdoPurgeIdentity(&Before) ) goto done;
    Directory = MdoHomeOpenDirectory(Base, XDIR_STAT);
    if ( Directory == NULL ) goto done;
    memset(&Entry, 0, sizeof(Entry));
    while ( (Next = xrtDirNext(Directory, &Entry)) == XDIR_NEXT_ITEM ) {
        char Id[MDO_SCHEDULE_ID_CAPACITY], Path[MDO_PROJECT_PURGE_PATH_CAPACITY];
        MdoScheduleInfo Info;
        bool Backup;
        int Written;
        if ( ++Count > MDO_PROJECT_PURGE_TARGET_LIMIT ||
             (Entry.Flags & XDIR_ENTRY_UTF8) == 0u ) goto done;
        if ( !History && MdoPurgeName(Entry.Name, "history") &&
             Entry.Info.Type == XFILE_TYPE_DIRECTORY ) continue;
        if ( !History && Entry.Info.Type == XFILE_TYPE_FILE &&
             (MdoPurgeName(Entry.Name, "audit.jsonl") ||
              MdoPurgeName(Entry.Name, ".writer.lock")) ) continue;
        memset(&Info, 0, sizeof(Info)); Info.Size = sizeof(Info);
        if ( Entry.Info.Type != XFILE_TYPE_FILE ||
             !MdoPurgeScheduleName(Entry.Name, History, Id, &Backup) ||
             !MdoScheduleCatalogFind(Catalog, Id, &Info) ) goto done;
        Written = snprintf(Path, sizeof(Path), "%s/%.*s", Base,
            (int)Entry.Name.Size, Entry.Name.Data);
        if ( Written <= 0 || (size_t)Written >= sizeof(Path) ) goto done;
        if ( !History ) {
            if ( !MdoPurgeScheduleRead(Path, &Info, Backup, Error) ) goto done;
            if ( !Backup ) ++Primaries;
        }
        if ( strcmp(Info.ProjectId, Inventory->Info.Project.Id) != 0 ) continue;
        if ( !MdoPurgeAdd(Inventory, Path, false, true, NULL, Error) ) goto done;
        if ( History ) ++Inventory->Info.ScheduleHistories;
        else if ( Backup ) ++Inventory->Info.ScheduleBackups;
    }
    if ( Next == XDIR_NEXT_ERROR || (!History && Primaries != MdoScheduleCatalogCount(Catalog)) ||
         !MdoHomeExternalStat(Base, &Exists, &After) || !Exists ||
         !MdoPurgeSame(&Before, &After) ) goto done;
    Ok = true;
done:
    if ( Directory != NULL && !xrtDirClose(Directory) ) Ok = false;
    if ( !Ok && (Error == NULL || Error->eCode == XWORK_ERROR_NONE) )
        (void)MdoPurgeError(Error, XWORK_ERROR_IO,
            "schedule inventory contains unknown, orphaned or changed data");
    return Ok;
}

static int MdoPurgeCompare(const void* Left, const void* Right)
{
    return strcmp(((const MdoProjectPurgeTarget*)Left)->Path,
        ((const MdoProjectPurgeTarget*)Right)->Path);
}

MdoProjectPurgeInventory* MdoProjectPurgeInventoryCreate(const char* ProjectId,
    const MdoProjectLease* Owner, xwork_error* Error)
{
    static const struct { const char* Format; bool Directory; } Paths[] = {
        { "projects/%s.json", false }, { "projects/%s.json.bak", false },
        { "data/project-drafts/%s.json", false }, { "data/project-drafts/%s.json.bak", false },
        { "memory/projects/%s.json", false }, { "memory/projects/%s.json.bak", false },
        { "sessions/%s", true }, { "migration/session-prompts/%s", true }
    };
    MdoProjectLease* Lease = NULL;
    MdoProjectPurgeInventory* Inventory = NULL;
    MdoScheduleCatalog* Schedules = NULL;
    bool Found = false, Ok = false;
    size_t i;
    xworkErrorInit(Error);
    if ( Owner != NULL ) {
        if ( !MdoProjectLeaseProtects(Owner, ProjectId, MDO_PROJECT_LEASE_EXCLUSIVE) ) {
            (void)MdoPurgeError(Error, XWORK_ERROR_INVALID_ARGUMENT,
                "purge rescan requires its project's current exclusive lease");
            goto done;
        }
    } else {
        Lease = MdoProjectLeaseAcquire(ProjectId, MDO_PROJECT_LEASE_SHARED, Error);
        if ( Lease == NULL ) goto done;
    }
    Inventory = (MdoProjectPurgeInventory*)xrtCalloc(1u, sizeof(*Inventory));
    if ( Inventory == NULL ) {
        (void)MdoPurgeError(Error, XWORK_ERROR_OUT_OF_MEMORY, "cannot allocate purge inventory");
        goto done;
    }
    Inventory->Info.Project.Size = sizeof(Inventory->Info.Project);
    if ( !MdoProjectGet(ProjectId, &Inventory->Info.Project, &Found, Error) ) goto done;
    if ( !Found ) {
        (void)MdoPurgeError(Error, XWORK_ERROR_CONTEXT, "project definition was not found");
        goto done;
    }
    for ( i = 0u; i < sizeof(Paths) / sizeof(Paths[0]); ++i ) {
        char Path[MDO_PROJECT_PURGE_PATH_CAPACITY];
        bool Present;
        int Written = snprintf(Path, sizeof(Path), Paths[i].Format, ProjectId);
        if ( Written <= 0 || (size_t)Written >= sizeof(Path) ||
             !MdoPurgeAdd(Inventory, Path, Paths[i].Directory, i == 0u, &Present, Error) ) goto done;
        if ( i == 2u ) Inventory->Info.ProjectDraft = Present;
        if ( i == 3u ) Inventory->Info.ProjectDraftBackup = Present;
    }
    Schedules = MdoScheduleCatalogSnapshot(Error);
    if ( Schedules == NULL ) goto done;
    if ( MdoScheduleCatalogDiagnosticCount(Schedules) != 0u ) {
        (void)MdoPurgeError(Error, XWORK_ERROR_IO, "schedule catalog has unresolved diagnostics");
        goto done;
    }
    if ( !MdoPurgeSchedules(Inventory, Schedules, false, Error) ||
         !MdoPurgeSchedules(Inventory, Schedules, true, Error) ) goto done;
    if ( MdoScheduleCatalogGeneration(Schedules) != MdoScheduleManagerGeneration() ) {
        (void)MdoPurgeError(Error, XWORK_ERROR_CONTEXT, "schedule catalog changed during purge scan");
        goto done;
    }
    if ( Inventory->Info.Targets > 1u ) qsort(Inventory->Targets,
        Inventory->Info.Targets, sizeof(*Inventory->Targets), MdoPurgeCompare);
    Ok = true;
done:
    MdoScheduleCatalogRelease(Schedules);
    MdoProjectLeaseRelease(Lease);
    if ( !Ok ) { MdoProjectPurgeInventoryFree(Inventory); Inventory = NULL; }
    return Inventory;
}

void MdoProjectPurgeInventoryFree(MdoProjectPurgeInventory* Inventory)
{
    if ( Inventory == NULL ) return;
    xrtFree(Inventory->Targets);
    xrtFree(Inventory);
}

bool MdoProjectPurgeInventoryGetInfo(const MdoProjectPurgeInventory* Inventory,
    MdoProjectPurgeInventoryInfo* Info)
{
    if ( Inventory == NULL || Info == NULL ) return false;
    *Info = Inventory->Info;
    return true;
}

bool MdoProjectPurgeInventoryAt(const MdoProjectPurgeInventory* Inventory,
    size_t Index, MdoProjectPurgeTarget* Target)
{
    if ( Inventory == NULL || Target == NULL || Index >= Inventory->Info.Targets ) return false;
    *Target = Inventory->Targets[Index];
    return true;
}
