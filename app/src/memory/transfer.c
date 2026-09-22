#include <stdlib.h>
#include <string.h>

#include "../../include/mdo/home.h"
#include "../../include/mdo/memory.h"
#include "internal.h"

#define MDO_MEMORY_TRANSFER_SCHEMA 1u
#define MDO_MEMORY_TRANSFER_STORES 257u
#define MDO_MEMORY_TRANSFER_TOTAL (64u * 1024u * 1024u)
#define MDO_MEMORY_TRANSFER_STORE (5u * 1024u * 1024u)
#define MDO_MEMORY_TRANSFER_MANIFEST (1024u * 1024u)
#define MDO_MEMORY_TRANSFER_PATH 160u
#define MDO_MEMORY_TRANSFER_SHA256 65u

typedef struct MdoMemoryTransferStore {
    MdoMemoryScope Scope;
    char ProjectId[MDO_MEMORY_PROJECT_CAPACITY];
    char Path[MDO_MEMORY_TRANSFER_PATH];
    MdoMemorySnapshot* Snapshot;
    char* Json;
    size_t JsonSize;
    char Sha256[MDO_MEMORY_TRANSFER_SHA256];
} MdoMemoryTransferStore;

typedef struct MdoMemoryTransfer {
    MdoMemoryTransferStore* Stores;
    size_t StoreCount;
    size_t ProjectCount;
    size_t EntryCount;
    uint64 TotalBytes;
    uint64 Generation;
} MdoMemoryTransfer;

static void MdoMemoryTransferError(xwork_error* Error,
    xwork_error_code Code, const char* Message)
{
    if ( Error == NULL ) return;
    xworkErrorInit(Error);
    Error->eCode = Code;
    snprintf(Error->sMessage, sizeof(Error->sMessage), "%s", Message);
}

static void MdoMemoryTransferXrtError(xwork_error* Error,
    const char* Fallback)
{
    const xerror* Cause = xrtGetError();
    MdoMemoryTransferError(Error, XWORK_ERROR_IO,
        Cause != NULL && xrtErrorMessage(Cause) != NULL ?
        xrtErrorMessage(Cause) : Fallback);
}

static bool MdoMemoryTransferDirectoryValid(const char* Directory)
{
    size_t Size = 0u;
    if ( Directory == NULL ) return false;
    while ( Size < 4096u && Directory[Size] != '\0' ) ++Size;
    return Size != 0u && Size < 4096u &&
        xrtUtf8Valid(xrtStrViewN(Directory, Size), NULL);
}

static void MdoMemoryTransferUnit(MdoMemoryTransfer* Transfer)
{
    size_t i;
    if ( Transfer == NULL ) return;
    for ( i = 0u; i < Transfer->StoreCount; ++i ) {
        MdoMemorySnapshotRelease(Transfer->Stores[i].Snapshot);
        xrtFree(Transfer->Stores[i].Json);
    }
    xrtFree(Transfer->Stores);
    memset(Transfer, 0, sizeof(*Transfer));
}

static bool MdoMemoryTransferHash(const void* Data, size_t Size,
    char Output[MDO_MEMORY_TRANSFER_SHA256])
{
    static const char Hex[] = "0123456789abcdef";
    uint8 Digest[XRT_SHA256_SIZE];
    size_t i;
    if ( !xrtSha256(Data, Size, Digest) ) return false;
    for ( i = 0u; i < XRT_SHA256_SIZE; ++i ) {
        Output[i * 2u] = Hex[Digest[i] >> 4u];
        Output[i * 2u + 1u] = Hex[Digest[i] & 0x0fu];
    }
    Output[XRT_SHA256_SIZE * 2u] = '\0';
    return true;
}

static bool MdoMemoryTransferReadRoot(xroot Root, const char* Path,
    size_t Limit, char** Data, size_t* Size, xwork_error* Error)
{
    xfileoptions Options;
    xfile File = NULL;
    xfileinfo Info;
    char* Bytes = NULL;
    bool Ok = false;
    *Data = NULL;
    *Size = 0u;
    if ( !xrtRootStat(Root, Path, false, &Info) ||
         Info.Type != XFILE_TYPE_FILE ||
         (Info.Available & XFILE_INFO_SIZE) == 0u || Info.Size > Limit ||
         Info.Size > SIZE_MAX - 1u ) goto invalid;
    Bytes = (char*)xrtMalloc((size_t)Info.Size + 1u);
    if ( Bytes == NULL ) {
        MdoMemoryTransferError(Error, XWORK_ERROR_OUT_OF_MEMORY,
            "cannot allocate imported memory file");
        goto done;
    }
    xrtFileOptionsInit(&Options);
    Options.Flags = XFILE_READ | XFILE_NOFOLLOW;
    File = xrtRootFileOpen(Root, Path, &Options);
    if ( File == NULL || (Info.Size != 0u &&
         !xrtReadFull(File, Bytes, (size_t)Info.Size, NULL)) ) goto io;
    Bytes[Info.Size] = '\0';
    *Data = Bytes;
    *Size = (size_t)Info.Size;
    Bytes = NULL;
    Ok = true;
    goto done;
invalid:
    MdoMemoryTransferError(Error, XWORK_ERROR_INVALID_ARGUMENT,
        "memory import contains an invalid or oversized regular file");
    goto done;
io:
    MdoMemoryTransferXrtError(Error, "cannot read imported memory file");
done:
    if ( File != NULL && !xrtClose(File) && Ok ) {
        Ok = false;
        MdoMemoryTransferXrtError(Error, "cannot close imported memory file");
    }
    xrtFree(Bytes);
    if ( !Ok ) {
        xrtFree(*Data);
        *Data = NULL;
        *Size = 0u;
    }
    return Ok;
}

static bool MdoMemoryTransferReadHome(const char* Path, char** Data,
    size_t* Size, xwork_error* Error)
{
    xfile File = NULL;
    xfileinfo Info;
    char* Bytes = NULL;
    bool Ok = false;
    *Data = NULL;
    *Size = 0u;
    File = MdoHomeOpenRead(Path);
    if ( File == NULL || !xrtFileStat(File, &Info) ||
         Info.Type != XFILE_TYPE_FILE ||
         (Info.Available & XFILE_INFO_SIZE) == 0u ||
         Info.Size > MDO_MEMORY_TRANSFER_STORE ||
         Info.Size > SIZE_MAX - 1u ) goto io;
    Bytes = (char*)xrtMalloc((size_t)Info.Size + 1u);
    if ( Bytes == NULL ) {
        MdoMemoryTransferError(Error, XWORK_ERROR_OUT_OF_MEMORY,
            "cannot allocate memory export file");
        goto done;
    }
    if ( Info.Size != 0u &&
         !xrtReadFull(File, Bytes, (size_t)Info.Size, NULL) ) goto io;
    Bytes[Info.Size] = '\0';
    *Data = Bytes;
    *Size = (size_t)Info.Size;
    Bytes = NULL;
    Ok = true;
    goto done;
io:
    MdoMemoryTransferXrtError(Error, "cannot read memory export store");
done:
    if ( File != NULL && !xrtClose(File) && Ok ) {
        Ok = false;
        MdoMemoryTransferXrtError(Error, "cannot close memory export store");
    }
    xrtFree(Bytes);
    if ( !Ok ) {
        xrtFree(*Data);
        *Data = NULL;
        *Size = 0u;
    }
    return Ok;
}

static bool MdoMemoryTransferPush(MdoMemoryTransfer* Transfer,
    MdoMemoryScope Scope, const char* ProjectId, const char* Path,
    MdoMemorySnapshot* Snapshot, char* Json, size_t JsonSize,
    xwork_error* Error)
{
    MdoMemoryTransferStore* Stores;
    MdoMemoryTransferStore* Store;
    size_t Entries = MdoMemorySnapshotCount(Snapshot);
    size_t i;
    if ( Transfer->StoreCount >= MDO_MEMORY_TRANSFER_STORES ||
         JsonSize > MDO_MEMORY_TRANSFER_STORE ||
         Transfer->TotalBytes > MDO_MEMORY_TRANSFER_TOTAL - JsonSize ||
         Entries > SIZE_MAX - Transfer->EntryCount ) {
        MdoMemoryTransferError(Error, XWORK_ERROR_LIMIT,
            "memory directory exceeds its bounded transfer limits");
        return false;
    }
    for ( i = 0u; i < Transfer->StoreCount; ++i )
        if ( strcmp(Transfer->Stores[i].Path, Path) == 0 ) {
            MdoMemoryTransferError(Error, XWORK_ERROR_INVALID_ARGUMENT,
                "memory directory contains duplicate stores");
            return false;
        }
    Stores = (MdoMemoryTransferStore*)xrtRealloc(Transfer->Stores,
        (Transfer->StoreCount + 1u) * sizeof(*Stores));
    if ( Stores == NULL ) {
        MdoMemoryTransferError(Error, XWORK_ERROR_OUT_OF_MEMORY,
            "cannot grow memory transfer catalog");
        return false;
    }
    Transfer->Stores = Stores;
    Store = &Stores[Transfer->StoreCount];
    memset(Store, 0, sizeof(*Store));
    Store->Scope = Scope;
    snprintf(Store->ProjectId, sizeof(Store->ProjectId), "%s",
        ProjectId != NULL ? ProjectId : "");
    snprintf(Store->Path, sizeof(Store->Path), "%s", Path);
    Store->Snapshot = Snapshot;
    Store->Json = Json;
    Store->JsonSize = JsonSize;
    if ( !MdoMemoryTransferHash(Json, JsonSize, Store->Sha256) ) {
        MdoMemoryTransferError(Error, XWORK_ERROR_CONTEXT,
            "cannot hash memory transfer store");
        memset(Store, 0, sizeof(*Store));
        return false;
    }
    ++Transfer->StoreCount;
    if ( Scope == MDO_MEMORY_PROJECT ) ++Transfer->ProjectCount;
    Transfer->EntryCount += Entries;
    Transfer->TotalBytes += JsonSize;
    return true;
}

static int MdoMemoryTransferStoreCompare(const void* LeftValue,
    const void* RightValue)
{
    const MdoMemoryTransferStore* Left =
        (const MdoMemoryTransferStore*)LeftValue;
    const MdoMemoryTransferStore* Right =
        (const MdoMemoryTransferStore*)RightValue;
    return strcmp(Left->Path, Right->Path);
}

static bool MdoMemoryTransferProjectName(xstrview Name,
    char ProjectId[MDO_MEMORY_PROJECT_CAPACITY], bool* Backup)
{
    static const char Json[] = ".json";
    static const char BackupSuffix[] = ".json.bak";
    size_t IdSize;
    *Backup = false;
    if ( (Name.Size > sizeof(BackupSuffix) - 1u) &&
         memcmp(Name.Data + Name.Size - (sizeof(BackupSuffix) - 1u),
            BackupSuffix, sizeof(BackupSuffix) - 1u) == 0 ) {
        IdSize = Name.Size - (sizeof(BackupSuffix) - 1u);
        *Backup = true;
    } else if ( (Name.Size > sizeof(Json) - 1u) &&
                memcmp(Name.Data + Name.Size - (sizeof(Json) - 1u), Json,
                    sizeof(Json) - 1u) == 0 ) {
        IdSize = Name.Size - (sizeof(Json) - 1u);
    } else return false;
    if ( IdSize >= MDO_MEMORY_PROJECT_CAPACITY ||
         memchr(Name.Data, '\0', IdSize) != NULL ) return false;
    memcpy(ProjectId, Name.Data, IdSize);
    ProjectId[IdSize] = '\0';
    return MdoMemoryInternalId(ProjectId, MDO_MEMORY_PROJECT_CAPACITY);
}

static bool MdoMemoryTransferAddImported(MdoMemoryTransfer* Transfer,
    xroot Root, MdoMemoryScope Scope, const char* ProjectId,
    const char* Path, xwork_error* Error)
{
    char* Json = NULL;
    size_t JsonSize = 0u;
    MdoMemorySnapshot* Snapshot = NULL;
    if ( !MdoMemoryTransferReadRoot(Root, Path, MDO_MEMORY_TRANSFER_STORE,
            &Json, &JsonSize, Error) ) return false;
    Snapshot = MdoMemoryInternalParse(Scope, ProjectId,
        xrtStrViewN(Json, JsonSize));
    if ( Snapshot == NULL ) {
        xrtFree(Json);
        MdoMemoryTransferError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "imported memory store does not satisfy schema version 1");
        return false;
    }
    if ( !MdoMemoryTransferPush(Transfer, Scope, ProjectId, Path, Snapshot,
            Json, JsonSize, Error) ) {
        MdoMemorySnapshotRelease(Snapshot);
        xrtFree(Json);
        return false;
    }
    return true;
}

static bool MdoMemoryTransferAddExported(MdoMemoryTransfer* Transfer,
    MdoMemoryScope Scope, const char* ProjectId, const char* Path,
    xwork_error* Error)
{
    char* Source = NULL;
    size_t SourceSize = 0u;
    char* Json = NULL;
    size_t JsonSize = 0u;
    MdoMemorySnapshot* Snapshot = NULL;
    if ( !MdoMemoryTransferReadHome(Path, &Source, &SourceSize, Error) )
        return false;
    Snapshot = MdoMemoryInternalParse(Scope, ProjectId,
        xrtStrViewN(Source, SourceSize));
    xrtFree(Source);
    if ( Snapshot == NULL ) {
        MdoMemoryTransferError(Error, XWORK_ERROR_IO,
            "active memory store does not satisfy schema version 1");
        return false;
    }
    Json = MdoMemoryInternalJson(Snapshot, &JsonSize);
    if ( Json == NULL ) {
        MdoMemorySnapshotRelease(Snapshot);
        MdoMemoryTransferError(Error, XWORK_ERROR_OUT_OF_MEMORY,
            "cannot serialize memory export store");
        return false;
    }
    if ( !MdoMemoryTransferPush(Transfer, Scope, ProjectId, Path + 7u,
            Snapshot, Json, JsonSize, Error) ) {
        MdoMemorySnapshotRelease(Snapshot);
        xrtFree(Json);
        return false;
    }
    return true;
}

static bool MdoMemoryTransferEntryName(const xdirentry* Entry,
    const char* Expected)
{
    size_t Size = strlen(Expected);
    return Entry->Name.Size == Size &&
        memcmp(Entry->Name.Data, Expected, Size) == 0;
}

static bool MdoMemoryTransferEnumerateImport(MdoMemoryTransfer* Transfer,
    xroot Root, xwork_error* Error)
{
    xdir Directory = NULL;
    xdir Projects = NULL;
    xdirentry Entry;
    xdirnext Next;
    bool Manifest = false;
    bool Global = false;
    bool ProjectDirectory = false;
    bool Ok = false;
    Directory = xrtRootDirOpen(Root, ".", XDIR_STAT);
    if ( Directory == NULL ) goto io;
    memset(&Entry, 0, sizeof(Entry));
    while ( (Next = xrtDirNext(Directory, &Entry)) == XDIR_NEXT_ITEM ) {
        if ( (Entry.Flags & XDIR_ENTRY_UTF8) == 0u ) goto invalid;
        if ( MdoMemoryTransferEntryName(&Entry, "manifest.json") ) {
            if ( Manifest || Entry.Info.Type != XFILE_TYPE_FILE ) goto invalid;
            Manifest = true;
        } else if ( MdoMemoryTransferEntryName(&Entry, "global.json") ) {
            if ( Global || Entry.Info.Type != XFILE_TYPE_FILE ) goto invalid;
            Global = true;
        } else if ( MdoMemoryTransferEntryName(&Entry, "projects") ) {
            if ( ProjectDirectory ||
                 Entry.Info.Type != XFILE_TYPE_DIRECTORY ) goto invalid;
            ProjectDirectory = true;
        } else goto invalid;
    }
    if ( Next == XDIR_NEXT_ERROR ) goto io;
    if ( !xrtDirClose(Directory) ) { Directory = NULL; goto io; }
    Directory = NULL;
    if ( !Manifest || !ProjectDirectory ) goto invalid;
    if ( Global && !MdoMemoryTransferAddImported(Transfer, Root,
            MDO_MEMORY_GLOBAL, NULL, "global.json", Error) ) goto done;
    Projects = xrtRootDirOpen(Root, "projects", XDIR_STAT);
    if ( Projects == NULL ) goto io;
    memset(&Entry, 0, sizeof(Entry));
    while ( (Next = xrtDirNext(Projects, &Entry)) == XDIR_NEXT_ITEM ) {
        char ProjectId[MDO_MEMORY_PROJECT_CAPACITY];
        char Path[MDO_MEMORY_TRANSFER_PATH];
        bool Backup = false;
        int Written;
        if ( (Entry.Flags & XDIR_ENTRY_UTF8) == 0u ||
             Entry.Info.Type != XFILE_TYPE_FILE ||
             !MdoMemoryTransferProjectName(Entry.Name, ProjectId, &Backup) ||
             Backup ) goto invalid;
        Written = snprintf(Path, sizeof(Path), "projects/%s.json", ProjectId);
        if ( Written <= 0 || (size_t)Written >= sizeof(Path) ||
             !MdoMemoryTransferAddImported(Transfer, Root,
                MDO_MEMORY_PROJECT, ProjectId, Path, Error) ) goto done;
    }
    if ( Next == XDIR_NEXT_ERROR ) goto io;
    if ( !xrtDirClose(Projects) ) { Projects = NULL; goto io; }
    Projects = NULL;
    if ( Transfer->StoreCount > 1u ) qsort(Transfer->Stores,
        Transfer->StoreCount, sizeof(*Transfer->Stores),
        MdoMemoryTransferStoreCompare);
    Ok = true;
    goto done;
invalid:
    MdoMemoryTransferError(Error, XWORK_ERROR_INVALID_ARGUMENT,
        "memory import directory has an unknown, duplicate, or unsafe entry");
    goto done;
io:
    MdoMemoryTransferXrtError(Error, "cannot enumerate memory import directory");
done:
    if ( Directory != NULL ) (void)xrtDirClose(Directory);
    if ( Projects != NULL ) (void)xrtDirClose(Projects);
    return Ok;
}

static bool MdoMemoryTransferValueUInt(const xvalue* Object,
    const char* Key, uint64* Result)
{
    const xvalue* Value = xrtValueObjectGet(Object, xrtStrView(Key));
    int64 Signed;
    if ( Value == NULL ) return false;
    if ( xrtValueType(Value) == XVALUE_UINT )
        return xrtValueGetUInt(Value, Result);
    if ( xrtValueType(Value) != XVALUE_INT ||
         !xrtValueGetInt(Value, &Signed) || Signed < 0 ) return false;
    *Result = (uint64)Signed;
    return true;
}

static bool MdoMemoryTransferValueString(const xvalue* Object,
    const char* Key, xstrview* Result)
{
    const xvalue* Value = xrtValueObjectGet(Object, xrtStrView(Key));
    return Value != NULL && xrtValueType(Value) == XVALUE_STRING &&
        xrtValueGetString(Value, Result) &&
        memchr(Result->Data, '\0', Result->Size) == NULL &&
        xrtUtf8Valid(*Result, NULL);
}

static bool MdoMemoryTransferViewEquals(xstrview View, const char* Text)
{
    size_t Size = strlen(Text);
    return View.Size == Size && memcmp(View.Data, Text, Size) == 0;
}

static MdoMemoryTransferStore* MdoMemoryTransferFind(
    MdoMemoryTransfer* Transfer, xstrview Path)
{
    size_t i;
    for ( i = 0u; i < Transfer->StoreCount; ++i )
        if ( Path.Size == strlen(Transfer->Stores[i].Path) &&
             memcmp(Path.Data, Transfer->Stores[i].Path, Path.Size) == 0 )
            return &Transfer->Stores[i];
    return NULL;
}

static bool MdoMemoryTransferValidateManifest(MdoMemoryTransfer* Transfer,
    xroot Root, xwork_error* Error)
{
    xjsonreadconfig Config;
    char* Json = NULL;
    size_t JsonSize = 0u;
    xvalue* Manifest = NULL;
    const xvalue* Files;
    bool Seen[MDO_MEMORY_TRANSFER_STORES];
    xstrview Kind;
    uint64 Schema;
    uint64 Exported;
    uint64 Stores;
    uint64 Projects;
    uint64 Entries;
    uint64 Bytes;
    size_t i;
    bool Ok = false;
    memset(Seen, 0, sizeof(Seen));
    if ( !MdoMemoryTransferReadRoot(Root, "manifest.json",
            MDO_MEMORY_TRANSFER_MANIFEST, &Json, &JsonSize, Error) )
        return false;
    xrtJsonReadConfigInit(&Config);
    Config.MaxInputBytes = MDO_MEMORY_TRANSFER_MANIFEST;
    Config.MaxDepth = 8u;
    Config.MaxValues = 4096u;
    Config.MaxContainerItems = MDO_MEMORY_TRANSFER_STORES + 16u;
    Manifest = xrtJsonRead(xrtStrViewN(Json, JsonSize), &Config);
    Files = Manifest != NULL ? xrtValueObjectGet(Manifest,
        xrtStrView("files")) : NULL;
    if ( Manifest == NULL || xrtValueType(Manifest) != XVALUE_OBJECT ||
         xrtValueCount(Manifest) != 8u ||
         !MdoMemoryTransferValueUInt(Manifest, "schema_version", &Schema) ||
         Schema != MDO_MEMORY_TRANSFER_SCHEMA ||
         !MdoMemoryTransferValueString(Manifest, "kind", &Kind) ||
         !MdoMemoryTransferViewEquals(Kind, "mdo-memory-directory") ||
         !MdoMemoryTransferValueUInt(Manifest, "exported_at_us", &Exported) ||
         Exported > INT64_MAX ||
         !MdoMemoryTransferValueUInt(Manifest, "store_count", &Stores) ||
         !MdoMemoryTransferValueUInt(Manifest, "project_count", &Projects) ||
         !MdoMemoryTransferValueUInt(Manifest, "entry_count", &Entries) ||
         !MdoMemoryTransferValueUInt(Manifest, "total_bytes", &Bytes) ||
         Stores != Transfer->StoreCount ||
         Projects != Transfer->ProjectCount ||
         Entries != Transfer->EntryCount || Bytes != Transfer->TotalBytes ||
         Files == NULL || xrtValueType(Files) != XVALUE_ARRAY ||
         xrtValueCount(Files) != Transfer->StoreCount ) goto invalid;
    for ( i = 0u; i < Transfer->StoreCount; ++i ) {
        const xvalue* File = xrtValueArrayGet(Files, i);
        MdoMemoryTransferStore* Store;
        xstrview Path;
        xstrview Scope;
        xstrview ProjectId;
        xstrview Sha256;
        uint64 Revision;
        uint64 EntryCount;
        uint64 FileBytes;
        size_t Index;
        if ( File == NULL || xrtValueType(File) != XVALUE_OBJECT ||
             xrtValueCount(File) != 7u ||
             !MdoMemoryTransferValueString(File, "path", &Path) ||
             !MdoMemoryTransferValueString(File, "scope", &Scope) ||
             !MdoMemoryTransferValueString(File, "project_id", &ProjectId) ||
             !MdoMemoryTransferValueUInt(File, "revision", &Revision) ||
             !MdoMemoryTransferValueUInt(File, "entry_count", &EntryCount) ||
             !MdoMemoryTransferValueUInt(File, "bytes", &FileBytes) ||
             !MdoMemoryTransferValueString(File, "sha256", &Sha256) )
            goto invalid;
        Store = MdoMemoryTransferFind(Transfer, Path);
        if ( Store == NULL ) goto invalid;
        Index = (size_t)(Store - Transfer->Stores);
        if ( Seen[Index] || Revision != MdoMemorySnapshotRevision(Store->Snapshot) ||
             EntryCount != MdoMemorySnapshotCount(Store->Snapshot) ||
             FileBytes != Store->JsonSize ||
             !MdoMemoryTransferViewEquals(Scope,
                Store->Scope == MDO_MEMORY_GLOBAL ? "global" : "project") ||
             !MdoMemoryTransferViewEquals(ProjectId, Store->ProjectId) ||
             !MdoMemoryTransferViewEquals(Sha256, Store->Sha256) )
            goto invalid;
        Seen[Index] = true;
    }
    Ok = true;
    goto done;
invalid:
    MdoMemoryTransferError(Error, XWORK_ERROR_INVALID_ARGUMENT,
        "memory import manifest does not match the validated store files");
done:
    xrtValueRelease(Manifest);
    xrtFree(Json);
    return Ok;
}

static bool MdoMemoryTransferLoadDirectory(const char* Directory,
    MdoMemoryTransfer* Transfer, xwork_error* Error)
{
    xfileinfo Info;
    xroot Root = NULL;
    bool Ok = false;
    memset(Transfer, 0, sizeof(*Transfer));
    if ( !MdoMemoryTransferDirectoryValid(Directory) ) {
        MdoMemoryTransferError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "invalid memory import directory");
        return false;
    }
    if ( !xrtPathStat(Directory, false, &Info) ||
         Info.Type != XFILE_TYPE_DIRECTORY ) {
        MdoMemoryTransferError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "memory import source must be a real directory");
        return false;
    }
    Root = xrtRootOpen(Directory);
    if ( Root == NULL ) goto io;
    if ( !MdoMemoryTransferEnumerateImport(Transfer, Root, Error) ||
         !MdoMemoryTransferValidateManifest(Transfer, Root, Error) ) goto done;
    Ok = true;
    goto done;
io:
    MdoMemoryTransferXrtError(Error, "cannot open memory import directory");
done:
    if ( Root != NULL && !xrtRootClose(Root) && Ok ) {
        Ok = false;
        MdoMemoryTransferXrtError(Error, "cannot close memory import directory");
    }
    if ( !Ok ) MdoMemoryTransferUnit(Transfer);
    return Ok;
}

static bool MdoMemoryTransferGatherExport(MdoMemoryTransfer* Transfer,
    xwork_error* Error)
{
    xfileinfo Info;
    bool Exists;
    xdir Directory = NULL;
    xdirentry Entry;
    xdirnext Next;
    bool Ok = false;
    memset(Transfer, 0, sizeof(*Transfer));
    if ( !MdoMemoryInternalTransferBegin(&Transfer->Generation, Error) )
        return false;
    if ( !MdoHomeExternalStat("memory/global.json", &Exists, &Info) )
        goto io;
    if ( Exists ) {
        if ( Info.Type != XFILE_TYPE_FILE ) goto invalid;
        if ( !MdoMemoryTransferAddExported(Transfer, MDO_MEMORY_GLOBAL,
                NULL, "memory/global.json", Error) ) goto done;
    }
    if ( !MdoHomeExternalStat("memory/projects", &Exists, &Info) ) goto io;
    if ( !Exists ) { Ok = true; goto done; }
    if ( Info.Type != XFILE_TYPE_DIRECTORY ) goto invalid;
    Directory = MdoHomeOpenDirectory("memory/projects", XDIR_STAT);
    if ( Directory == NULL ) goto io;
    memset(&Entry, 0, sizeof(Entry));
    while ( (Next = xrtDirNext(Directory, &Entry)) == XDIR_NEXT_ITEM ) {
        char ProjectId[MDO_MEMORY_PROJECT_CAPACITY];
        char Path[MDO_MEMORY_TRANSFER_PATH];
        bool Backup = false;
        int Written;
        if ( (Entry.Flags & XDIR_ENTRY_UTF8) == 0u ||
             Entry.Info.Type != XFILE_TYPE_FILE ||
             !MdoMemoryTransferProjectName(Entry.Name, ProjectId, &Backup) )
            goto invalid;
        if ( Backup ) continue;
        Written = snprintf(Path, sizeof(Path), "memory/projects/%s.json",
            ProjectId);
        if ( Written <= 0 || (size_t)Written >= sizeof(Path) ||
             !MdoMemoryTransferAddExported(Transfer, MDO_MEMORY_PROJECT,
                ProjectId, Path, Error) ) goto done;
    }
    if ( Next == XDIR_NEXT_ERROR ) goto io;
    if ( !xrtDirClose(Directory) ) { Directory = NULL; goto io; }
    Directory = NULL;
    if ( Transfer->StoreCount > 1u ) qsort(Transfer->Stores,
        Transfer->StoreCount, sizeof(*Transfer->Stores),
        MdoMemoryTransferStoreCompare);
    Ok = true;
    goto done;
invalid:
    MdoMemoryTransferError(Error, XWORK_ERROR_IO,
        "active memory directory contains an unsafe or unknown entry");
    goto done;
io:
    MdoMemoryTransferXrtError(Error, "cannot enumerate active memory stores");
done:
    if ( Directory != NULL ) (void)xrtDirClose(Directory);
    MdoMemoryInternalTransferEnd();
    if ( !Ok ) MdoMemoryTransferUnit(Transfer);
    return Ok;
}

static bool MdoMemoryTransferObjectTake(xvalue* Object, const char* Key,
    xvalue* Value)
{
    bool Ok = Value != NULL &&
        xrtValueObjectSetTake(Object, xrtStrView(Key), &Value);
    xrtValueRelease(Value);
    return Ok;
}

static bool MdoMemoryTransferObjectString(xvalue* Object, const char* Key,
    const char* Value)
{
    return MdoMemoryTransferObjectTake(Object, Key,
        xrtValueString(xrtStrView(Value)));
}

static char* MdoMemoryTransferManifest(const MdoMemoryTransfer* Transfer,
    size_t* Size)
{
    xvalue* Root = xrtValueObject();
    xvalue* Files = xrtValueArray();
    char* Json = NULL;
    size_t i;
    if ( Root == NULL || Files == NULL ||
         !MdoMemoryTransferObjectTake(Root, "schema_version",
            xrtValueUInt(MDO_MEMORY_TRANSFER_SCHEMA)) ||
         !MdoMemoryTransferObjectString(Root, "kind",
            "mdo-memory-directory") ||
         !MdoMemoryTransferObjectTake(Root, "exported_at_us",
            xrtValueInt(xrtNow())) ||
         !MdoMemoryTransferObjectTake(Root, "store_count",
            xrtValueUInt(Transfer->StoreCount)) ||
         !MdoMemoryTransferObjectTake(Root, "project_count",
            xrtValueUInt(Transfer->ProjectCount)) ||
         !MdoMemoryTransferObjectTake(Root, "entry_count",
            xrtValueUInt(Transfer->EntryCount)) ||
         !MdoMemoryTransferObjectTake(Root, "total_bytes",
            xrtValueUInt(Transfer->TotalBytes)) ) goto done;
    for ( i = 0u; i < Transfer->StoreCount; ++i ) {
        const MdoMemoryTransferStore* Store = &Transfer->Stores[i];
        xvalue* File = xrtValueObject();
        if ( File == NULL ||
             !MdoMemoryTransferObjectString(File, "path", Store->Path) ||
             !MdoMemoryTransferObjectString(File, "scope",
                Store->Scope == MDO_MEMORY_GLOBAL ? "global" : "project") ||
             !MdoMemoryTransferObjectString(File, "project_id",
                Store->ProjectId) ||
             !MdoMemoryTransferObjectTake(File, "revision",
                xrtValueUInt(MdoMemorySnapshotRevision(Store->Snapshot))) ||
             !MdoMemoryTransferObjectTake(File, "entry_count",
                xrtValueUInt(MdoMemorySnapshotCount(Store->Snapshot))) ||
             !MdoMemoryTransferObjectTake(File, "bytes",
                xrtValueUInt(Store->JsonSize)) ||
             !MdoMemoryTransferObjectString(File, "sha256", Store->Sha256) ||
             !xrtValueArrayAppendTake(Files, &File) ) {
            xrtValueRelease(File);
            goto done;
        }
    }
    if ( !xrtValueObjectSetTake(Root, xrtStrView("files"), &Files) ) goto done;
    Json = xrtJsonStringify(Root, true, Size);
    if ( Json != NULL && *Size > MDO_MEMORY_TRANSFER_MANIFEST ) {
        xrtFree(Json);
        Json = NULL;
    }
done:
    xrtValueRelease(Files);
    xrtValueRelease(Root);
    return Json;
}

static bool MdoMemoryTransferWriteRoot(xroot Root, const char* Path,
    const void* Data, size_t Size, xwork_error* Error)
{
    xfileoptions Options;
    xfile File;
    bool Ok;
    xrtFileOptionsInit(&Options);
    Options.Flags = XFILE_WRITE | XFILE_CREATE | XFILE_EXCLUSIVE |
        XFILE_SYNC | XFILE_NOFOLLOW;
    File = xrtRootFileOpen(Root, Path, &Options);
    if ( File == NULL ) goto io;
    Ok = (Size == 0u || xrtWriteFull(File, Data, Size, NULL)) &&
        xrtFlush(File);
    if ( !xrtClose(File) ) Ok = false;
    if ( Ok ) return true;
io:
    MdoMemoryTransferXrtError(Error, "cannot write memory export file");
    return false;
}

static bool MdoMemoryTransferPublish(const char* Directory,
    const MdoMemoryTransfer* Transfer, xwork_error* Error)
{
    xfileinfo Info;
    xroot Root = NULL;
    char* Manifest = NULL;
    size_t ManifestSize = 0u;
    size_t i;
    bool Created = false;
    bool Ok = false;
    if ( xrtPathStat(Directory, false, &Info) ) {
        MdoMemoryTransferError(Error, XWORK_ERROR_CONTEXT,
            "memory export destination already exists");
        return false;
    }
    if ( xrtGetError() == NULL ||
         xrtErrorKind(xrtGetError()) != XERR_NOT_FOUND ) goto io;
    xrtClearError();
    if ( !xrtDirCreateMode(Directory, 0700u) ) goto io;
    Created = true;
    Root = xrtRootOpen(Directory);
    if ( Root == NULL || !xrtRootDirCreate(Root, "projects", 0700u) ) goto io;
    for ( i = 0u; i < Transfer->StoreCount; ++i )
        if ( !MdoMemoryTransferWriteRoot(Root, Transfer->Stores[i].Path,
                Transfer->Stores[i].Json, Transfer->Stores[i].JsonSize,
                Error) ) goto done;
    Manifest = MdoMemoryTransferManifest(Transfer, &ManifestSize);
    if ( Manifest == NULL ) {
        MdoMemoryTransferError(Error, XWORK_ERROR_OUT_OF_MEMORY,
            "cannot serialize memory export manifest");
        goto done;
    }
    if ( !MdoMemoryTransferWriteRoot(Root, "manifest.json", Manifest,
            ManifestSize, Error) ) goto done;
    Ok = true;
    goto done;
io:
    MdoMemoryTransferXrtError(Error, "cannot create memory export directory");
done:
    if ( !Ok && Root != NULL ) {
        (void)xrtRootRemove(Root, "manifest.json");
        xrtClearError();
        for ( i = 0u; i < Transfer->StoreCount; ++i ) {
            (void)xrtRootRemove(Root, Transfer->Stores[i].Path);
            xrtClearError();
        }
        (void)xrtRootRemove(Root, "projects");
        xrtClearError();
    }
    if ( Root != NULL && !xrtRootClose(Root) && Ok ) xrtClearError();
    if ( !Ok && Created ) {
        (void)xrtDirRemove(Directory);
        xrtClearError();
    }
    xrtFree(Manifest);
    return Ok;
}

static bool MdoMemoryTransferSummaryValid(MdoMemoryTransferSummary* Summary)
{
    return Summary != NULL && Summary->Size >= sizeof(*Summary);
}

static void MdoMemoryTransferSetSummary(MdoMemoryTransferSummary* Summary,
    const MdoMemoryTransfer* Transfer)
{
    uint32 Size = Summary->Size;
    memset(Summary, 0, sizeof(*Summary));
    Summary->Size = Size;
    Summary->Generation = Transfer->Generation;
    Summary->StoreCount = Transfer->StoreCount;
    Summary->ProjectCount = Transfer->ProjectCount;
    Summary->EntryCount = Transfer->EntryCount;
    Summary->TotalBytes = Transfer->TotalBytes;
}

void MdoMemoryImportOptionsInit(MdoMemoryImportOptions* Options)
{
    if ( Options == NULL ) return;
    memset(Options, 0, sizeof(*Options));
    Options->Size = sizeof(*Options);
    Options->ExpectedGeneration = UINT64_MAX;
}

bool MdoMemoryExportDirectory(const char* Directory,
    MdoMemoryTransferSummary* Summary, xwork_error* Error)
{
    MdoMemoryTransfer Transfer;
    bool Ok = false;
    xworkErrorInit(Error);
    if ( !MdoMemoryTransferDirectoryValid(Directory) ||
         !MdoMemoryTransferSummaryValid(Summary) ) {
        MdoMemoryTransferError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "invalid memory directory export request");
        return false;
    }
    if ( !MdoMemoryTransferGatherExport(&Transfer, Error) ) return false;
    if ( !MdoMemoryTransferPublish(Directory, &Transfer, Error) ) goto done;
    MdoMemoryTransferSetSummary(Summary, &Transfer);
    Ok = true;
done:
    MdoMemoryTransferUnit(&Transfer);
    return Ok;
}

bool MdoMemoryPreviewImportDirectory(const char* Directory,
    MdoMemoryTransferSummary* Summary, xwork_error* Error)
{
    MdoMemoryTransfer Transfer;
    bool Ok = false;
    xworkErrorInit(Error);
    if ( !MdoMemoryTransferSummaryValid(Summary) ) {
        MdoMemoryTransferError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "memory import preview requires a summary structure");
        return false;
    }
    if ( !MdoMemoryTransferLoadDirectory(Directory, &Transfer, Error) )
        return false;
    Transfer.Generation = MdoMemoryManagerGeneration();
    if ( Transfer.Generation == 0u ) {
        MdoMemoryTransferError(Error, XWORK_ERROR_CONTEXT,
            "memory manager is not initialized");
        goto done;
    }
    MdoMemoryTransferSetSummary(Summary, &Transfer);
    Ok = true;
done:
    MdoMemoryTransferUnit(&Transfer);
    return Ok;
}

bool MdoMemoryImportDirectory(const MdoMemoryImportOptions* Options,
    MdoMemoryTransferSummary* Summary, xwork_error* Error)
{
    MdoMemoryTransfer Transfer;
    MdoMemoryImportCandidate* Candidates = NULL;
    uint64 Generation = 0u;
    size_t i;
    bool Ok = false;
    xworkErrorInit(Error);
    if ( Options == NULL || Options->Size < sizeof(*Options) ||
         !MdoMemoryTransferDirectoryValid(Options->Directory) ||
         !MdoMemoryTransferSummaryValid(Summary) ) {
        MdoMemoryTransferError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "invalid memory directory import request");
        return false;
    }
    if ( !MdoMemoryTransferLoadDirectory(Options->Directory, &Transfer,
            Error) ) return false;
    if ( Transfer.StoreCount != 0u ) {
        Candidates = (MdoMemoryImportCandidate*)xrtCalloc(
            Transfer.StoreCount, sizeof(*Candidates));
        if ( Candidates == NULL ) {
            MdoMemoryTransferError(Error, XWORK_ERROR_OUT_OF_MEMORY,
                "cannot allocate memory import transaction");
            goto done;
        }
    }
    for ( i = 0u; i < Transfer.StoreCount; ++i ) {
        Candidates[i].Scope = Transfer.Stores[i].Scope;
        Candidates[i].ProjectId = Transfer.Stores[i].ProjectId;
        Candidates[i].Path = Transfer.Stores[i].Scope == MDO_MEMORY_GLOBAL ?
            "memory/global.json" : NULL;
        Candidates[i].Snapshot = Transfer.Stores[i].Snapshot;
        if ( Candidates[i].Path == NULL ) {
            static const char Prefix[] = "memory/";
            size_t PathSize = strlen(Transfer.Stores[i].Path);
            char* Path = (char*)xrtMalloc(sizeof(Prefix) - 1u + PathSize + 1u);
            if ( Path == NULL ) {
                MdoMemoryTransferError(Error, XWORK_ERROR_OUT_OF_MEMORY,
                    "cannot allocate imported memory path");
                goto done;
            }
            memcpy(Path, Prefix, sizeof(Prefix) - 1u);
            memcpy(Path + sizeof(Prefix) - 1u, Transfer.Stores[i].Path,
                PathSize + 1u);
            Candidates[i].Path = Path;
        }
    }
    if ( !MdoMemoryInternalImportEmpty(Candidates, Transfer.StoreCount,
            Options->ExpectedGeneration, Options->Actor, Options->Reason,
            &Generation, Error) ) goto done;
    Transfer.Generation = Generation;
    MdoMemoryTransferSetSummary(Summary, &Transfer);
    Ok = true;
done:
    if ( Candidates != NULL ) {
        for ( i = 0u; i < Transfer.StoreCount; ++i )
            if ( Candidates[i].Scope == MDO_MEMORY_PROJECT )
                xrtFree((void*)Candidates[i].Path);
    }
    xrtFree(Candidates);
    MdoMemoryTransferUnit(&Transfer);
    return Ok;
}
