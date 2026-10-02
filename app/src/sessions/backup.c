#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../include/mdo/home.h"
#include "../../include/mdo/session_backup.h"
#include "internal.h"
#include "backup_internal.h"

#define MDO_BACKUP_SCAN_NODES 4096u
#define MDO_BACKUP_READ_CHUNK 65536u
#define MDO_BACKUP_EVENT_BYTES (96u * 1024u)

const char* const MdoBackupOptionalFiles[MDO_BACKUP_OPTIONAL_FILES] = {
    "journal.jsonl", "ui-events.jsonl", "todo.json", "draft.json", "queue.json", "feedback.json"
};

bool MdoBackupError(xwork_error* Error, xwork_error_code Code,
    const char* Message, const char* Path)
{
    if ( Error != NULL ) {
        xworkErrorInit(Error);
        Error->eCode = Code;
        snprintf(Error->sMessage, sizeof(Error->sMessage), "%s%s%s",
            Message, Path != NULL ? ": " : "", Path != NULL ? Path : "");
    }
    return false;
}

void MdoSessionBackupLimitsInit(MdoSessionBackupLimits* Limits)
{
    if ( Limits == NULL ) return;
    memset(Limits, 0, sizeof(*Limits));
    Limits->Size = sizeof(*Limits);
    Limits->Files = MDO_SESSION_BACKUP_MAX_FILES;
    Limits->FileBytes = MDO_SESSION_BACKUP_MAX_FILE_BYTES;
    Limits->TotalBytes = MDO_SESSION_BACKUP_MAX_TOTAL_BYTES;
    Limits->DocumentBytes = MDO_SESSION_BACKUP_MAX_DOCUMENT_BYTES;
}

bool MdoBackupLimits(const MdoSessionBackupLimits* Input,
    MdoSessionBackupLimits* Output, uint64 Timeout, xwork_error* Error)
{
    MdoSessionBackupLimitsInit(Output);
    if ( Input != NULL ) {
        if ( Input->Size != sizeof(*Input) || Input->Files == 0u ||
             Input->Files > Output->Files || Input->FileBytes == 0u ||
             Input->FileBytes > Output->FileBytes || Input->TotalBytes == 0u ||
             Input->TotalBytes > Output->TotalBytes || Input->DocumentBytes == 0u ||
             Input->DocumentBytes > Output->DocumentBytes )
            return MdoBackupError(Error, XWORK_ERROR_INVALID_ARGUMENT,
                "invalid session backup budgets", NULL);
        *Output = *Input;
    }
    {
        xdeadline Maximum = xrtDeadlineAfter(Timeout);
        if ( Output->Deadline == 0u || Output->Deadline > Maximum ) Output->Deadline = Maximum;
    }
    return true;
}

static bool MdoBackupTime(const MdoSessionBackupLimits* Limits, xwork_error* Error)
{
    return !xrtDeadlineExpired(Limits->Deadline) ||
        MdoBackupError(Error, XWORK_ERROR_LIMIT, "session backup deadline exceeded", NULL);
}

bool MdoBackupCheck(const MdoSessionBackupLimits* Limits, const xcancel* Cancel,
    xwork_error* Error)
{
    if ( xrtCancelRequested(Cancel) )
        return MdoBackupError(Error, XWORK_ERROR_CANCELLED, "session backup validation cancelled", NULL);
    return MdoBackupTime(Limits, Error);
}

bool MdoBackupDigits(const char* Text, size_t Size, bool Hex)
{
    size_t i;
    if ( Size == 0u ) return false;
    for ( i = 0u; i < Size; ++i ) {
        char Ch = Text[i];
        if ( !(Ch >= '0' && Ch <= '9') &&
             !(Hex && Ch >= 'a' && Ch <= 'f') ) return false;
    }
    return true;
}

static bool MdoBackupDecimal(const char* Text, size_t Size)
{
    uint64 Value = 0u;
    size_t i;
    if ( Size > 20u || !MdoBackupDigits(Text, Size, false) ) return false;
    for ( i = 0u; i < Size; ++i ) {
        unsigned Digit = (unsigned)(Text[i] - '0');
        if ( Value > (UINT64_MAX - Digit) / 10u ) return false;
        Value = Value * 10u + Digit;
    }
    return Value != 0u;
}

static bool MdoBackupSuffix(const char* Text, const char* Suffix)
{
    size_t Size = strlen(Text), Tail = strlen(Suffix);
    return Size >= Tail && strcmp(Text + Size - Tail, Suffix) == 0;
}

/* Whitelist the current logical files; unknown content fails rather than
 * silently creating an allegedly complete bundle. No native path is exported. */
size_t MdoBackupPathLimit(const char* Path, bool Directory)
{
    const char* Name;
    size_t Size, i;
    if ( Directory ) {
        if ( strcmp(Path, "") == 0 || strcmp(Path, "attachments") == 0 ||
             strcmp(Path, "attachments/events") == 0 ||
             strcmp(Path, "attachments/runs") == 0 ||
             strcmp(Path, "queue-receipts") == 0 || strcmp(Path, "artifacts") == 0 )
            return 1u;
        return strncmp(Path, "artifacts/run-", 14u) == 0 && strlen(Path) == 34u &&
            MdoBackupDecimal(Path + 14u, 20u) ? 1u : 0u;
    }
    if ( strcmp(Path, "meta.json") == 0 ) return 64u * 1024u;
    if ( strcmp(Path, "snapshot.json") == 0 || strcmp(Path, "journal.jsonl") == 0 )
        return MDO_SESSION_BACKUP_MAX_FILE_BYTES;
    if ( strcmp(Path, "ui-events.jsonl") == 0 ) return 16u * 1024u * 1024u;
    if ( strcmp(Path, "todo.json") == 0 ) return 16u * 1024u;
    if ( strcmp(Path, "draft.json") == 0 || strcmp(Path, "queue.json") == 0 )
        return 256u * 1024u;
    if ( strcmp(Path, "feedback.json") == 0 ) return 32u * 1024u;
    if ( strcmp(Path, "restore-inputs.json") == 0 ) return 8u * 1024u * 1024u;
    if ( strncmp(Path, "attachments/", 12u) == 0 ) {
        Name = Path + 12u; Size = strlen(Name);
        if ( Size == 36u && MdoBackupDigits(Name, 32u, true) &&
             strcmp(Name + 32u, ".bin") == 0 ) return 8u * 1024u * 1024u;
        if ( Size == 37u && MdoBackupDigits(Name, 32u, true) &&
             strcmp(Name + 32u, ".json") == 0 ) return 4096u;
        if ( strncmp(Name, "events/", 7u) != 0 && strncmp(Name, "runs/", 5u) != 0 )
            return 0u;
        Name += strncmp(Name, "events/", 7u) == 0 ? 7u : 5u;
        Size = strlen(Name);
        return Size > 5u && strcmp(Name + Size - 5u, ".json") == 0 &&
            MdoBackupDecimal(Name, Size - 5u) ? 320u : 0u;
    }
    if ( strncmp(Path, "queue-receipts/", 15u) == 0 ) {
        Name = Path + 15u;
        return strlen(Name) == 37u && MdoBackupDigits(Name, 32u, true) &&
            strcmp(Name + 32u, ".json") == 0 ? 512u : 0u;
    }
    if ( strncmp(Path, "artifacts/run-", 14u) != 0 || strlen(Path) < 60u ||
         Path[34] != '/' || !MdoBackupDecimal(Path + 14u, 20u) ) return 0u;
    Name = Path + 35u; Size = strlen(Name);
    if ( Size < 26u || Size > 153u || Name[20] != '-' ||
         !MdoBackupDecimal(Name, 20u) || !MdoBackupSuffix(Name, ".txt") ) return 0u;
    for ( i = 21u; i < Size - 4u; ++i ) {
        char Ch = Name[i];
        if ( !((Ch >= 'a' && Ch <= 'z') || (Ch >= 'A' && Ch <= 'Z') ||
               (Ch >= '0' && Ch <= '9') || Ch == '-' || Ch == '_') ) return 0u;
    }
    return MDO_SESSION_BACKUP_MAX_FILE_BYTES;
}

static bool MdoBackupExcluded(const char* Path)
{
    char Base[MDO_SESSION_BACKUP_PATH_CAPACITY];
    size_t Size = strlen(Path), Tail = 0u;
    if ( strcmp(Path, ".runtime.lock") == 0 ) return true;
    if ( MdoBackupSuffix(Path, ".bak.tmp") ) Tail = 8u;
    else if ( MdoBackupSuffix(Path, ".bak") || MdoBackupSuffix(Path, ".tmp") ) Tail = 4u;
    if ( Tail == 0u || Size <= Tail || Size - Tail >= sizeof(Base) ) return false;
    memcpy(Base, Path, Size - Tail); Base[Size - Tail] = '\0';
    return MdoBackupPathLimit(Base, false) != 0u;
}

static bool MdoBackupSame(const xfileinfo* A, const xfileinfo* B)
{
    return (A->Available & B->Available & XFILE_INFO_IDENTITY) != 0u &&
        A->Identity != 0u && A->Identity == B->Identity && A->Device == B->Device &&
        A->Type == B->Type && (A->Type != XFILE_TYPE_FILE || A->Size == B->Size);
}

static bool MdoBackupRead(MdoSessionBackup* Backup, const char* HomePath,
    const char* Path, const xfileinfo* Before, size_t Limit, xwork_error* Error)
{
    MdoBackupOwnedFile* File;
    xfile Input = NULL;
    xfileinfo Opened, After;
    bool Exists, Ok = false;
    char* Data = NULL;
    size_t Offset = 0u, Bytes;
    if ( Backup->Count >= Backup->Limits.Files ||
         (Before->Available & XFILE_INFO_SIZE) == 0u || Before->Size > Limit ||
         Before->Size > Backup->Limits.FileBytes ||
         Before->Size > Backup->Limits.TotalBytes - Backup->Bytes )
        return MdoBackupError(Error, XWORK_ERROR_LIMIT,
            "session backup file or total budget exceeded", Path);
    Bytes = (size_t)Before->Size;
    Input = MdoHomeOpenRead(HomePath);
    if ( Input == NULL || !xrtFileStat(Input, &Opened) || !MdoBackupSame(Before, &Opened) )
        goto io;
    Data = (char*)xrtMalloc(Bytes + 1u);
    if ( Data == NULL ) goto memory;
    while ( Offset < Bytes ) {
        size_t Chunk = Bytes - Offset;
        if ( !MdoBackupTime(&Backup->Limits, Error) ) goto done;
        if ( Chunk > MDO_BACKUP_READ_CHUNK ) Chunk = MDO_BACKUP_READ_CHUNK;
        if ( !xrtReadFull(Input, Data + Offset, Chunk, NULL) ) goto io;
        Offset += Chunk;
    }
    Data[Bytes] = '\0';
    if ( !xrtFileStat(Input, &After) || !MdoBackupSame(Before, &After) ||
         !MdoHomeExternalStat(HomePath, &Exists, &After) || !Exists ||
         !MdoBackupSame(Before, &After) ) goto io;
    if ( !xrtClose(Input) ) { Input = NULL; goto io; }
    Input = NULL;
    if ( Backup->Count == Backup->Capacity ) {
        size_t Capacity = Backup->Capacity != 0u ? Backup->Capacity * 2u : 16u;
        void* Files;
        if ( Capacity > Backup->Limits.Files ) Capacity = Backup->Limits.Files;
        Files = xrtRealloc(Backup->Files, Capacity * sizeof(*File));
        if ( Files == NULL ) goto memory;
        Backup->Files = (MdoBackupOwnedFile*)Files; Backup->Capacity = Capacity;
    }
    File = &Backup->Files[Backup->Count++];
    snprintf(File->Path, sizeof(File->Path), "%s", Path);
    File->Data = Data; File->Bytes = Bytes; Backup->Bytes += Bytes;
    Data = NULL; Ok = true;
    goto done;
memory:
    (void)MdoBackupError(Error, XWORK_ERROR_OUT_OF_MEMORY,
        "cannot allocate session backup", Path);
    goto done;
io:
    (void)MdoBackupError(Error, XWORK_ERROR_IO,
        "session backup file is unavailable or changed during capture", Path);
done:
    if ( Input != NULL && !xrtClose(Input) && Ok ) Ok = false;
    xrtFree(Data);
    return Ok;
}

static bool MdoBackupScan(MdoSessionBackup* Backup, const char* Base,
    const char* Path, unsigned Depth, xwork_error* Error)
{
    char HomePath[512];
    xfileinfo Before, After;
    xdir Directory = NULL;
    xdirentry Entry;
    xdirnext Next = XDIR_NEXT_ERROR;
    bool Exists, Ok = false;
    int Written = snprintf(HomePath, sizeof(HomePath), "%s%s%s", Base,
        Path[0] != '\0' ? "/" : "", Path);
    if ( Written <= 0 || (size_t)Written >= sizeof(HomePath) || Depth > 3u ||
         ++Backup->Nodes > MDO_BACKUP_SCAN_NODES )
        return MdoBackupError(Error, XWORK_ERROR_LIMIT, "session backup traversal limit exceeded", Path);
    if ( !MdoBackupTime(&Backup->Limits, Error) ) return false;
    if ( !MdoHomeExternalStat(HomePath, &Exists, &Before) || !Exists ) goto io;
    if ( Before.Type != XFILE_TYPE_FILE && Before.Type != XFILE_TYPE_DIRECTORY )
        return MdoBackupError(Error, XWORK_ERROR_IO, "session backup rejects links and special files", Path);
    /* Even excluded names must be regular files. Never traverse an alias. */
    if ( Before.Type == XFILE_TYPE_FILE && MdoBackupExcluded(Path) ) return true;
    if ( MdoBackupPathLimit(Path, Before.Type == XFILE_TYPE_DIRECTORY) == 0u )
        return MdoBackupError(Error, XWORK_ERROR_IO, "unknown session backup content", Path);
    if ( Before.Type == XFILE_TYPE_FILE )
        return MdoBackupRead(Backup, HomePath, Path, &Before, MdoBackupPathLimit(Path, false), Error);
    Directory = MdoHomeOpenDirectory(HomePath, XDIR_STAT);
    if ( Directory == NULL ) goto io;
    memset(&Entry, 0, sizeof(Entry));
    while ( (Next = xrtDirNext(Directory, &Entry)) == XDIR_NEXT_ITEM ) {
        char Child[MDO_SESSION_BACKUP_PATH_CAPACITY];
        size_t i;
        if ( (Entry.Flags & XDIR_ENTRY_UTF8) == 0u || Entry.Name.Size == 0u ||
             Entry.Name.Size > 153u ) goto io;
        for ( i = 0u; i < Entry.Name.Size; ++i ) {
            unsigned char Ch = (unsigned char)Entry.Name.Data[i];
            if ( Ch < 0x21u || Ch > 0x7eu || Ch == '/' || Ch == '\\' || Ch == ':' ) goto io;
        }
        Written = snprintf(Child, sizeof(Child), "%s%s%.*s", Path,
            Path[0] != '\0' ? "/" : "", (int)Entry.Name.Size, Entry.Name.Data);
        if ( Written <= 0 || (size_t)Written >= sizeof(Child) ) goto io;
        if ( !MdoBackupScan(Backup, Base, Child, Depth + 1u, Error) ) goto done;
    }
    if ( Next == XDIR_NEXT_ERROR || !MdoHomeExternalStat(HomePath, &Exists, &After) ||
         !Exists || !MdoBackupSame(&Before, &After) ) goto io;
    Ok = MdoBackupTime(&Backup->Limits, Error);
    goto done;
io:
    (void)MdoBackupError(Error, XWORK_ERROR_IO, "cannot read session backup directory", Path);
done:
    if ( Directory != NULL && !xrtDirClose(Directory) ) {
        Ok = false;
        (void)MdoBackupError(Error, XWORK_ERROR_IO, "cannot close session backup directory", Path);
    }
    return Ok;
}

int MdoBackupCompare(const void* Left, const void* Right)
{
    return strcmp(((const MdoBackupOwnedFile*)Left)->Path,
        ((const MdoBackupOwnedFile*)Right)->Path);
}

const MdoBackupOwnedFile* MdoBackupFind(const MdoSessionBackup* Backup, const char* Path)
{
    MdoBackupOwnedFile Key;
    memset(&Key, 0, sizeof(Key));
    if ( strlen(Path) >= sizeof(Key.Path) || Backup->Count == 0u ) return NULL;
    snprintf(Key.Path, sizeof(Key.Path), "%s", Path);
    return (const MdoBackupOwnedFile*)bsearch(&Key, Backup->Files,
        Backup->Count, sizeof(Key), MdoBackupCompare);
}

static bool MdoBackupCaptureRead(const MdoSessionInfo* Info, void* UserData, xwork_error* Error)
{
    MdoSessionBackup* Backup = (MdoSessionBackup*)UserData;
    char Base[MDO_SESSION_PATH_CAPACITY];
    int Written = snprintf(Base, sizeof(Base), "sessions/%s/%s", Info->ProjectId, Info->Id);
    if ( Written <= 0 || (size_t)Written >= sizeof(Base) )
        return MdoBackupError(Error, XWORK_ERROR_LIMIT, "session backup path exceeds its budget", NULL);
    Backup->Info = *Info;
    Backup->CapturedAt = xrtNow();
    return MdoBackupScan(Backup, Base, "", 0u, Error);
}

MdoSessionBackup* MdoSessionBackupCapture(MdoSession* Session,
    const MdoSessionBackupLimits* Limits, xwork_error* Error)
{
    MdoSessionBackup* Backup;
    MdoSessionBackupLimits Budget;
    xworkErrorInit(Error);
    if ( !MdoBackupLimits(Limits, &Budget, 5000000u, Error) ) return NULL;
    if ( !MdoBackupTime(&Budget, Error) ) return NULL;
    if ( Session == NULL ) {
        (void)MdoBackupError(Error, XWORK_ERROR_INVALID_ARGUMENT, "session is required for backup", NULL);
        return NULL;
    }
    Backup = (MdoSessionBackup*)xrtCalloc(1u, sizeof(*Backup));
    if ( Backup == NULL ) {
        (void)MdoBackupError(Error, XWORK_ERROR_OUT_OF_MEMORY, "cannot allocate session backup", NULL);
        return NULL;
    }
    Backup->Limits = Budget;
    Backup->Schema = MDO_SESSION_BACKUP_SCHEMA;
    if ( !MdoSessionWithCapture(Session, MdoBackupCaptureRead, Backup, Error) ) goto failed;
    qsort(Backup->Files, Backup->Count, sizeof(*Backup->Files), MdoBackupCompare);
    if ( MdoBackupFind(Backup, "meta.json") == NULL ||
         MdoBackupFind(Backup, "snapshot.json") == NULL ) {
        (void)MdoBackupError(Error, XWORK_ERROR_IO, "session backup metadata or checkpoint is missing", NULL);
        goto failed;
    }
    return Backup;
failed:
    MdoSessionBackupRelease(Backup);
    return NULL;
}

void MdoSessionBackupRelease(MdoSessionBackup* Backup)
{
    size_t i;
    if ( Backup == NULL ) return;
    for ( i = 0u; i < Backup->Count; ++i ) xrtFree(Backup->Files[i].Data);
    xrtFree(Backup->Files); xrtFree(Backup);
}

size_t MdoSessionBackupFileCount(const MdoSessionBackup* Backup)
{
    return Backup != NULL ? Backup->Count : 0u;
}

MdoSessionBackup* MdoBackupClone(const MdoSessionBackup* Source, bool Data,
    const MdoSessionBackupLimits* Limits, const xcancel* Cancel, xwork_error* Error)
{
    MdoSessionBackup* Copy = (MdoSessionBackup*)xrtCalloc(1u, sizeof(*Copy));
    size_t i;
    if ( Copy == NULL ) goto memory;
    Copy->Files = (MdoBackupOwnedFile*)xrtCalloc(Source->Count, sizeof(*Copy->Files));
    if ( Copy->Files == NULL ) goto memory;
    Copy->Count = Copy->Capacity = Source->Count; Copy->Bytes = Source->Bytes;
    Copy->Info = Source->Info; Copy->CapturedAt = Source->CapturedAt;
    Copy->Limits = *Limits; Copy->Schema = Source->Schema; Copy->Decoded = Source->Decoded;
    Copy->History = Source->History; Copy->Relations = Source->Relations;
    for ( i = 0u; i < Copy->Count; ++i ) {
        size_t Offset = 0u;
        MdoBackupOwnedFile* File = &Copy->Files[i];
        if ( !MdoBackupCheck(Limits, Cancel, Error) ) goto fail;
        snprintf(File->Path, sizeof(File->Path), "%s", Source->Files[i].Path);
        File->Bytes = Source->Files[i].Bytes;
        if ( !Data ) continue;
        File->Data = (char*)xrtMalloc(File->Bytes + 1u);
        if ( File->Data == NULL ) goto memory;
        while ( Offset < File->Bytes ) {
            size_t Chunk = File->Bytes - Offset;
            if ( Chunk > MDO_BACKUP_READ_CHUNK ) Chunk = MDO_BACKUP_READ_CHUNK;
            if ( !MdoBackupCheck(Limits, Cancel, Error) ) goto fail;
            memcpy(File->Data + Offset, Source->Files[i].Data + Offset, Chunk); Offset += Chunk;
        }
        File->Data[File->Bytes] = '\0';
    }
    return Copy;
memory:
    (void)MdoBackupError(Error, XWORK_ERROR_OUT_OF_MEMORY, "cannot clone owned session backup", NULL);
fail:
    MdoSessionBackupRelease(Copy); return NULL;
}

/* Prepare the entire new JSON allocation before replacing an owned file.
 * Limits apply to the resulting bundle as well as to the original input. */
bool MdoBackupReplaceJson(MdoSessionBackup* Copy, const char* Path, const xvalue* Root,
    const MdoSessionBackupLimits* Limits, const xcancel* Cancel, xwork_error* Error)
{
    MdoBackupOwnedFile* File = NULL;
    size_t i, Bytes = 0u, Before = 0u;
    str Json;
    if ( !MdoBackupCheck(Limits, Cancel, Error) ) return false;
    Json = xrtJsonStringify(Root, false, &Bytes);
    if ( Json == NULL ) return MdoBackupError(Error, XWORK_ERROR_OUT_OF_MEMORY, "cannot encode backup sidecar", Path);
    for ( i = 0u; i < Copy->Count; ++i )
        if ( strcmp(Copy->Files[i].Path, Path) == 0 ) { File = &Copy->Files[i]; Before = File->Bytes; break; }
    if ( MdoBackupPathLimit(Path, false) == 0u || Bytes > MdoBackupPathLimit(Path, false) || Bytes > Limits->FileBytes ||
         Copy->Bytes - Before > Limits->TotalBytes || Bytes > Limits->TotalBytes - (Copy->Bytes - Before) ||
         (File == NULL && Copy->Count >= Limits->Files) ) {
        xrtFree(Json);
        return MdoBackupError(Error, XWORK_ERROR_LIMIT, "updated sidecar exceeds backup budget", Path);
    }
    if ( !MdoBackupCheck(Limits, Cancel, Error) ) { xrtFree(Json); return false; }
    if ( File == NULL ) {
        if ( Copy->Count == Copy->Capacity ) {
            MdoBackupOwnedFile* Files = (MdoBackupOwnedFile*)xrtRealloc(Copy->Files,
                (Copy->Capacity + 1u) * sizeof(*Copy->Files));
            if ( Files == NULL ) { xrtFree(Json); return MdoBackupError(Error, XWORK_ERROR_OUT_OF_MEMORY, "cannot grow updated backup", Path); }
            Copy->Files = Files; ++Copy->Capacity;
        }
        File = &Copy->Files[Copy->Count++]; memset(File, 0, sizeof(*File));
        snprintf(File->Path, sizeof(File->Path), "%s", Path);
    }
    xrtFree(File->Data); File->Data = Json; File->Bytes = Bytes;
    Copy->Bytes = Copy->Bytes - Before + Bytes;
    qsort(Copy->Files, Copy->Count, sizeof(*Copy->Files), MdoBackupCompare);
    return true;
}

bool MdoSessionBackupFileGet(const MdoSessionBackup* Backup, size_t Index, MdoSessionBackupFile* File)
{
    if ( File != NULL ) memset(File, 0, sizeof(*File));
    if ( Backup == NULL || File == NULL || Index >= Backup->Count ) return false;
    File->Path = Backup->Files[Index].Path;
    File->Data = Backup->Files[Index].Data;
    File->Bytes = Backup->Files[Index].Bytes;
    return true;
}

bool MdoBackupUInt(const xvalue* Value, const char* Key, uint64* Number)
{
    const xvalue* Child = xrtValueObjectGet(Value, xrtStrView(Key));
    int64 Signed;
    if ( xrtValueType(Child) == XVALUE_UINT ) return xrtValueGetUInt(Child, Number);
    if ( !xrtValueGetInt(Child, &Signed) || Signed < 0 ) return false;
    *Number = (uint64)Signed; return true;
}

bool MdoBackupView(const xvalue* Value, const char* Key, xstrview* Text)
{
    return xrtValueGetString(xrtValueObjectGet(Value, xrtStrView(Key)), Text);
}

xvalue* MdoBackupJson(const void* Data, size_t Bytes)
{
    xjsonreadconfig Config;
    xrtJsonReadConfigInit(&Config);
    Config.MaxInputBytes = MDO_SESSION_BACKUP_MAX_FILE_BYTES;
    Config.MaxDepth = 32u; Config.MaxValues = MDO_BACKUP_JSON_VALUES;
    return xrtJsonRead(xrtStrViewN((const char*)Data, Bytes), &Config);
}

static bool MdoBackupImage(const MdoSessionBackup* Backup, xstrview Id)
{
    char Path[64];
    const MdoBackupOwnedFile* Bytes;
    const MdoBackupOwnedFile* Meta;
    if ( Id.Size != 32u || !MdoBackupDigits(Id.Data, Id.Size, true) ) return false;
    snprintf(Path, sizeof(Path), "attachments/%.*s.bin", 32, Id.Data);
    Bytes = MdoBackupFind(Backup, Path);
    snprintf(Path, sizeof(Path), "attachments/%.*s.json", 32, Id.Data);
    Meta = MdoBackupFind(Backup, Path);
    return Bytes != NULL && Meta != NULL && Bytes->Bytes != 0u;
}

/* Only resource-bearing product fields are examined. Prompt text and tool
 * output stay opaque: strings mentioning filenames are not executable refs. */
static bool MdoBackupReferences(const MdoSessionBackup* Backup, const xvalue* Value, unsigned Depth)
{
    size_t i, Count = xrtValueCount(Value);
    xvaluetype Type = xrtValueType(Value);
    if ( Depth > 32u ) return false;
    for ( i = 0u; i < Count; ++i ) {
        xstrview Key = {0};
        const xvalue* Child = Type == XVALUE_OBJECT ?
            xrtValueObjectAt(Value, i, &Key) : xrtValueArrayGet(Value, i);
        if ( Key.Size == 11u && memcmp(Key.Data, "attachments", 11u) == 0 ) {
            size_t j;
            if ( xrtValueType(Child) != XVALUE_ARRAY || xrtValueCount(Child) > 4u ) return false;
            for ( j = 0u; j < xrtValueCount(Child); ++j ) {
                xstrview Id;
                if ( !xrtValueGetString(xrtValueArrayGet(Child, j), &Id) ||
                     !MdoBackupImage(Backup, Id) ) return false;
            }
        }
        if ( (xrtValueType(Child) == XVALUE_OBJECT || xrtValueType(Child) == XVALUE_ARRAY) &&
             !MdoBackupReferences(Backup, Child, Depth + 1u) ) return false;
    }
    return true;
}

static bool MdoBackupImageMeta(const MdoSessionBackup* Backup,
    const MdoBackupOwnedFile* File, const xvalue* Root)
{
    xstrview Id, Mime, Name;
    uint64 Schema, Bytes, Created;
    char Binary[64];
    const MdoBackupOwnedFile* Image;
    if ( !MdoBackupUInt(Root, "schema_version", &Schema) || (Schema != 1u && Schema != 2u) ||
         xrtValueCount(Root) != (Schema == 1u ? 5u : 6u) ||
         !MdoBackupView(Root, "id", &Id) || Id.Size != 32u ||
         memcmp(Id.Data, File->Path + 12u, 32u) != 0 || !MdoBackupImage(Backup, Id) ||
         !MdoBackupUInt(Root, "size", &Bytes) ||
         !MdoBackupUInt(Root, "created_at", &Created) || Created == 0u || Created > INT64_MAX ||
         !MdoBackupView(Root, "mime_type", &Mime) ) return false;
    snprintf(Binary, sizeof(Binary), "attachments/%.*s.bin", 32, Id.Data);
    Image = MdoBackupFind(Backup, Binary);
    if ( Bytes != Image->Bytes ||
         !((Mime.Size == 9u && memcmp(Mime.Data, "image/png", 9u) == 0) ||
           (Mime.Size == 10u && memcmp(Mime.Data, "image/jpeg", 10u) == 0) ||
           (Mime.Size == 10u && memcmp(Mime.Data, "image/webp", 10u) == 0)) ) return false;
    if ( Schema == 2u ) {
        size_t i;
        if ( !MdoBackupView(Root, "file_name", &Name) || Name.Size == 0u || Name.Size > 1024u ||
             !xrtUtf8Valid(Name, NULL) ) return false;
        for ( i = 0u; i < Name.Size; ++i ) {
            unsigned char Ch = (unsigned char)Name.Data[i];
            if ( Ch < 0x20u || Ch == 0x7fu || Ch == '/' || Ch == '\\' ) return false;
        }
    }
    return true;
}

static bool MdoBackupEventArtifact(const MdoSessionBackup* Backup, const xvalue* Event)
{
    xstrview Path;
    char Relative[MDO_SESSION_BACKUP_PATH_CAPACITY];
    size_t i, Start = SIZE_MAX;
    if ( !MdoBackupView(Event, "artifact_path", &Path) || Path.Size == 0u ) return true;
    /* The old event path is provenance, never a restore/write target. Accept
     * slash variants from either platform, match an exact manifest tail. */
    for ( i = 0u; i + 14u < Path.Size; ++i ) {
        if ( (i == 0u || Path.Data[i - 1u] == '/' || Path.Data[i - 1u] == '\\') &&
             memcmp(Path.Data + i, "artifacts", 9u) == 0 &&
             (Path.Data[i + 9u] == '/' || Path.Data[i + 9u] == '\\') &&
             memcmp(Path.Data + i + 10u, "run-", 4u) == 0 ) {
            if ( Start != SIZE_MAX ) return false;
            Start = i;
        }
    }
    if ( Start == SIZE_MAX || Path.Size - Start >= sizeof(Relative) ) return false;
    for ( i = 0u; i < Path.Size - Start; ++i )
        Relative[i] = Path.Data[Start + i] == '\\' ? '/' : Path.Data[Start + i];
    Relative[i] = '\0';
    return MdoBackupPathLimit(Relative, false) != 0u && MdoBackupFind(Backup, Relative) != NULL;
}

bool MdoBackupValidate(const MdoSessionBackup* Backup,
    const MdoSessionBackupLimits* Limits, const xcancel* Cancel,
    MdoBackupHistory* History, xwork_error* Error)
{
    size_t i;
    memset(History, 0, sizeof(*History));
    if ( !MdoBackupModelValidate(Backup, Limits, Cancel, Error) ) return false;
    for ( i = 0u; i < Backup->Count; ++i ) {
        const MdoBackupOwnedFile* File = &Backup->Files[i];
        size_t Offset = 0u;
        bool Lines = MdoBackupSuffix(File->Path, ".jsonl");
        bool Ui = strcmp(File->Path, "ui-events.jsonl") == 0;
        if ( !MdoBackupCheck(Limits, Cancel, Error) ) return false;
        if ( strcmp(File->Path, "snapshot.json") == 0 || strcmp(File->Path, "journal.jsonl") == 0 ) continue;
        if ( MdoBackupSuffix(File->Path, ".bin") ) {
            if ( !MdoBackupImage(Backup, xrtStrViewN(File->Path + 12u, 32u)) ) goto invalid;
            continue;
        }
        if ( !Lines && !MdoBackupSuffix(File->Path, ".json") ) continue;
        do {
            size_t Bytes = File->Bytes - Offset;
            const char* End = Lines ? (const char*)memchr(File->Data + Offset, '\n', Bytes) : NULL;
            xvalue* Root;
            bool Ok;
            if ( Lines && Bytes == 0u ) break;
            /* Partial journal tails are not silently presented as a complete capture. */
            if ( Lines && End == NULL ) goto invalid;
            if ( End != NULL ) Bytes = (size_t)(End - (File->Data + Offset));
            if ( Bytes == 0u || (Ui && Bytes > MDO_BACKUP_EVENT_BYTES) ) goto invalid;
            Root = MdoBackupJson(File->Data + Offset, Bytes);
            if ( Root == NULL && xrtGetError() != NULL && xrtErrorKind(xrtGetError()) == XERR_MEMORY )
                return MdoBackupError(Error, XWORK_ERROR_OUT_OF_MEMORY,
                    "cannot parse session backup file", File->Path);
            Ok = xrtValueType(Root) == XVALUE_OBJECT;
            if ( Ok && strcmp(File->Path, "restore-inputs.json") == 0 &&
                 !MdoBackupInputsArchiveValid(Root, Limits, Cancel, Error) ) {
                xrtValueRelease(Root); return false;
            }
            if ( Ok && strcmp(File->Path, "meta.json") == 0 ) {
                MdoSessionInfo Meta;
                Ok = MdoSessionsInternalMetaParse(Backup->Info.ProjectId, Backup->Info.Id,
                    xrtStrViewN(File->Data, File->Bytes), &Meta) && Meta.Revision == Backup->Info.Revision;
            }
            if ( Ok && strncmp(File->Path, "attachments/", 12u) == 0 && strlen(File->Path) == 49u )
                Ok = MdoBackupImageMeta(Backup, File, Root);
            if ( Ok && (strcmp(File->Path, "draft.json") == 0 || strcmp(File->Path, "queue.json") == 0 ||
                        strncmp(File->Path, "attachments/", 12u) == 0) )
                Ok = MdoBackupReferences(Backup, Root, 0u);
            if ( Ok && Ui ) {
                uint64 Id;
                Ok = MdoBackupUInt(Root, "event_id", &Id) && Id != 0u && Id > History->Last &&
                    MdoBackupEventArtifact(Backup, Root);
                if ( Ok ) {
                    if ( History->First == 0u ) History->First = Id;
                    History->Last = Id; ++History->Records;
                }
            }
            xrtValueRelease(Root);
            if ( !Ok ) goto invalid;
            Offset += Bytes + (Lines ? 1u : 0u);
            if ( !MdoBackupCheck(Limits, Cancel, Error) ) return false;
        } while ( Lines && Offset < File->Bytes );
        continue;
invalid:
        return MdoBackupError(Error, XWORK_ERROR_IO,
            "session backup contains invalid content or a missing resource reference", File->Path);
    }
    return true;
}

static bool MdoBackupSet(xvalue* Object, const char* Key, xvalue* Value)
{
    return xrtValueObjectSetNew(Object, xrtStrView(Key), Value);
}

static bool MdoBackupString(xvalue* Object, const char* Key, const char* Value)
{
    return MdoBackupSet(Object, Key, xrtValueString(xrtStrView(Value)));
}

/* A small manifest is stringified once. File payloads are written directly into
 * the final allocation, avoiding a second base64-sized value tree. Paths are
 * whitelisted ASCII, so this manual JSON fragment needs no arbitrary escaping. */
str MdoSessionBackupEncode(const MdoSessionBackup* Backup,
    const MdoSessionBackupLimits* Limits, size_t* Size, xwork_error* Error)
{
    MdoSessionBackupLimits Budget;
    MdoBackupHistory History;
    xvalue* Root = NULL;
    xvalue* Absent = NULL;
    char* Manifest = NULL;
    char* Output = NULL;
    size_t ManifestBytes = 0u, Total, Used, i;
    bool Ok = false;
    xworkErrorInit(Error);
    if ( Size != NULL ) *Size = 0u;
    if ( Backup == NULL || Size == NULL ) {
        (void)MdoBackupError(Error, XWORK_ERROR_INVALID_ARGUMENT, "backup and document size are required", NULL);
        return NULL;
    }
    if ( Backup->Schema != MDO_SESSION_BACKUP_SCHEMA ) {
        (void)MdoBackupError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "legacy model-only export cannot become a full session backup", NULL);
        return NULL;
    }
    if ( !MdoBackupLimits(Limits, &Budget, 30000000u, Error) ) return NULL;
    if ( Backup->Count > Budget.Files || Backup->Bytes > Budget.TotalBytes ) goto limit;
    if ( !MdoBackupValidate(Backup, &Budget, NULL, &History, Error) ) goto done;
    Root = xrtValueObject(); Absent = xrtValueArray();
    if ( Root == NULL || Absent == NULL ) goto memory;
    for ( i = 0u; i < MDO_BACKUP_OPTIONAL_FILES; ++i ) {
        if ( MdoBackupFind(Backup, MdoBackupOptionalFiles[i]) == NULL &&
             !xrtValueArrayAppendNew(Absent, xrtValueString(xrtStrView(MdoBackupOptionalFiles[i]))) ) goto memory;
    }
    if ( !MdoBackupString(Root, "format", "mdo-session-backup") ||
         !MdoBackupSet(Root, "export_schema", xrtValueUInt(MDO_SESSION_BACKUP_SCHEMA)) ||
         !MdoBackupSet(Root, "captured_at_us", xrtValueInt(Backup->CapturedAt)) ||
         !MdoBackupString(Root, "project_id", Backup->Info.ProjectId) ||
         !MdoBackupString(Root, "session_id", Backup->Info.Id) ||
         !MdoBackupSet(Root, "revision", xrtValueUInt(Backup->Info.Revision)) ||
         !MdoBackupString(Root, "source_workspace", Backup->Info.WorkspaceRoot) ||
         !MdoBackupString(Root, "scope", "retained-session-files") ||
         !MdoBackupString(Root, "history_retention", "earlier-content-may-have-been-pruned") ||
         !MdoBackupString(Root, "validation", "json-syntax-and-resource-references") ||
         !MdoBackupSet(Root, "restore_ready", xrtValueBool(false)) ||
         !MdoBackupString(Root, "checksum", "sha256") ||
         !MdoBackupString(Root, "queue_restore_policy", "require-user-confirmation") ||
         !MdoBackupSet(Root, "file_count", xrtValueUInt(Backup->Count)) ||
         !MdoBackupSet(Root, "total_bytes", xrtValueUInt(Backup->Bytes)) ||
         !MdoBackupSet(Root, "ui_first_event_id", xrtValueUInt(History.First)) ||
         !MdoBackupSet(Root, "ui_last_event_id", xrtValueUInt(History.Last)) ||
         !MdoBackupSet(Root, "ui_records", xrtValueUInt(History.Records)) ) goto memory;
    if ( !xrtValueObjectSetTake(Root, XRT_STR_LITERAL("absent_files"), &Absent) ) goto memory;
    Manifest = xrtJsonStringify(Root, false, &ManifestBytes);
    if ( Manifest == NULL || ManifestBytes == 0u || Manifest[ManifestBytes - 1u] != '}' ) goto memory;
    /* Header + each bounded path/hash/length/encoding fragment + encoded bytes
     * + tail. Budget is checked before allocating the expanded JSON. */
    Total = ManifestBytes + 16u;
    for ( i = 0u; i < Backup->Count; ++i ) {
        size_t Encoded, Extra = strlen(Backup->Files[i].Path) + 192u;
        if ( Backup->Files[i].Bytes > Budget.FileBytes ||
             !xrtBase64Encode(Backup->Files[i].Data, Backup->Files[i].Bytes, NULL, 0u, &Encoded, NULL) ||
             Total > Budget.DocumentBytes || Extra > Budget.DocumentBytes - Total ||
             Encoded > Budget.DocumentBytes - Total - Extra ) goto limit;
        Total += Extra + Encoded;
    }
    if ( Total > Budget.DocumentBytes ) goto limit;
    Output = (char*)xrtMalloc(Total + 1u);
    if ( Output == NULL ) goto memory;
    memcpy(Output, Manifest, ManifestBytes - 1u); Used = ManifestBytes - 1u;
    memcpy(Output + Used, ",\"files\":[", 10u); Used += 10u;
    for ( i = 0u; i < Backup->Count; ++i ) {
        const MdoBackupOwnedFile* File = &Backup->Files[i];
        static const char Hex[] = "0123456789abcdef";
        uint8 Digest[XRT_SHA256_SIZE]; char Hash[65];
        size_t j, Encoded;
        int Written;
        if ( !MdoBackupTime(&Budget, Error) ) goto done;
        if ( !xrtSha256(File->Data, File->Bytes, Digest) ) {
            (void)MdoBackupError(Error, XWORK_ERROR_IO, "cannot hash session backup file", File->Path);
            goto done;
        }
        for ( j = 0u; j < sizeof(Digest); ++j ) {
            Hash[j * 2u] = Hex[Digest[j] >> 4u]; Hash[j * 2u + 1u] = Hex[Digest[j] & 15u];
        }
        Hash[64] = '\0';
        Written = snprintf(Output + Used, Total + 1u - Used,
            "%s{\"path\":\"%s\",\"bytes\":%llu,\"sha256\":\"%s\",\"encoding\":\"base64\",\"data\":\"",
            i != 0u ? "," : "", File->Path, (unsigned long long)File->Bytes, Hash);
        if ( Written <= 0 || (size_t)Written >= Total + 1u - Used ) goto limit;
        Used += (size_t)Written;
        if ( !xrtBase64Encode(File->Data, File->Bytes, Output + Used,
                Total + 1u - Used, &Encoded, NULL) ) goto memory;
        Used += Encoded;
        memcpy(Output + Used, "\"}", 2u); Used += 2u;
    }
    memcpy(Output + Used, "]}\n", 3u); Used += 3u; Output[Used] = '\0';
    if ( !MdoBackupTime(&Budget, Error) ) goto done;
    *Size = Used; Ok = true;
    goto done;
limit:
    (void)MdoBackupError(Error, XWORK_ERROR_LIMIT, "session backup document budget exceeded", NULL);
    goto done;
memory:
    (void)MdoBackupError(Error, XWORK_ERROR_OUT_OF_MEMORY, "cannot encode session backup", NULL);
done:
    xrtValueRelease(Absent); xrtValueRelease(Root); xrtFree(Manifest);
    if ( !Ok ) { xrtFree(Output); Output = NULL; }
    return Output;
}
