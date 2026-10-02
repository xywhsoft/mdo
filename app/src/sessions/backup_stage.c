#include <stdio.h>
#include <string.h>
#include "backup_internal.h"

#define MDO_STAGE_CHUNK 65536u
#define MDO_STAGE_DIRECTORIES (MDO_SESSION_BACKUP_MAX_FILES + 5u)
#define MDO_STAGE_ATTEMPTS 32u

typedef struct MdoStagePath {
    char Path[MDO_SESSION_BACKUP_PATH_CAPACITY];
    xfileinfo Identity;
    bool Owned;
} MdoStagePath;

struct MdoSessionBackupStage {
    xroot Parent, Directory;
    MdoStagePath Root;
    MdoStagePath* Files;
    MdoStagePath* Directories;
    size_t Count, DirectoryCount;
    MdoSessionBackup* Bytes;
    MdoSessionBackupStageInfo Info;
};

static bool MdoStageSame(const xfileinfo* A, const xfileinfo* B)
{
    return (A->Available & B->Available & XFILE_INFO_IDENTITY) != 0u &&
        A->Identity != 0u && A->Identity == B->Identity && A->Device == B->Device && A->Type == B->Type;
}

static bool MdoStageIO(xwork_error* Error, const char* Path)
{
    const xerror* Cause = xrtGetError();
    return MdoBackupError(Error, Cause != NULL && xrtErrorKind(Cause) == XERR_MEMORY ?
        XWORK_ERROR_OUT_OF_MEMORY : XWORK_ERROR_IO, "cannot access private session staging", Path);
}

static bool MdoStageMatches(xroot Root, const MdoStagePath* Path)
{
    xfileinfo Current;
    return xrtRootStat(Root, Path->Path, false, &Current) && MdoStageSame(&Path->Identity, &Current);
}

static bool MdoStageCloseDirectory(MdoSessionBackupStage* Stage)
{
    xroot Directory = Stage->Directory;
    Stage->Directory = NULL;
    return Directory == NULL || xrtRootClose(Directory);
}

/* Retain the parent anchor between calls, but not a nested directory handle.
 * Windows cannot move an ancestor while some nested directory handles are
 * open. Reacquire the child by relative name and its recorded identity for
 * each synchronous operation; the displayed native path is never reopened. */
static bool MdoStageOpenDirectory(MdoSessionBackupStage* Stage)
{
    xfileinfo Opened;
    if ( !MdoStageMatches(Stage->Parent, &Stage->Root) ) return false;
    if ( Stage->Directory == NULL ) Stage->Directory = xrtRootOpenIn(Stage->Parent, Stage->Root.Path);
    if ( Stage->Directory == NULL || !xrtRootStat(Stage->Directory, ".", false, &Opened) ||
         !MdoStageSame(&Opened, &Stage->Root.Identity) || !MdoStageMatches(Stage->Parent, &Stage->Root) ) {
        (void)MdoStageCloseDirectory(Stage); return false;
    }
    return true;
}

static bool MdoStageDirectory(MdoSessionBackupStage* Stage, const char* Path, xwork_error* Error)
{
    size_t i;
    MdoStagePath* Item;
    for ( i = 0u; i < Stage->DirectoryCount; ++i ) {
        Item = &Stage->Directories[i];
        if ( strcmp(Item->Path, Path) == 0 )
            return MdoStageMatches(Stage->Directory, Item) || MdoStageIO(Error, Path);
    }
    if ( Stage->DirectoryCount >= MDO_STAGE_DIRECTORIES || !MdoBackupPathLimit(Path, true) )
        return MdoBackupError(Error, XWORK_ERROR_LIMIT, "invalid staging directory budget or path", Path);
    Item = &Stage->Directories[Stage->DirectoryCount];
    snprintf(Item->Path, sizeof(Item->Path), "%s", Path);
    if ( !xrtRootDirCreate(Stage->Directory, Path, 0700u) ) return MdoStageIO(Error, Path);
    Item->Owned = true; ++Stage->DirectoryCount;
    return xrtRootStat(Stage->Directory, Path, false, &Item->Identity) &&
        Item->Identity.Type == XFILE_TYPE_DIRECTORY ? true : MdoStageIO(Error, Path);
}

static bool MdoStageParents(MdoSessionBackupStage* Stage, const char* Path, xwork_error* Error)
{
    char Prefix[MDO_SESSION_BACKUP_PATH_CAPACITY];
    size_t i;
    for ( i = 0u; Path[i] != '\0'; ++i ) {
        if ( Path[i] != '/' ) continue;
        memcpy(Prefix, Path, i); Prefix[i] = '\0';
        if ( !MdoStageDirectory(Stage, Prefix, Error) ) return false;
    }
    return true;
}

static bool MdoStageWrite(MdoSessionBackupStage* Stage, size_t Index,
    const MdoBackupOwnedFile* Source, const MdoSessionBackupLimits* Limits,
    const xcancel* Cancel, xwork_error* Error)
{
    xfileoptions Options;
    xfile File;
    size_t Offset = 0u;
    bool Ok;
    MdoStagePath* Item = &Stage->Files[Index];
    if ( !MdoStageParents(Stage, Source->Path, Error) ) return false;
    xrtFileOptionsInit(&Options);
    Options.Flags = XFILE_WRITE | XFILE_CREATE | XFILE_EXCLUSIVE | XFILE_NOFOLLOW;
    Options.Mode = 0600u;
    File = xrtRootFileOpen(Stage->Directory, Item->Path, &Options);
    if ( File == NULL ) return MdoStageIO(Error, Item->Path);
    Item->Owned = true;
    Ok = xrtFileStat(File, &Item->Identity) && Item->Identity.Type == XFILE_TYPE_FILE;
    while ( Ok && Offset < Source->Bytes ) {
        size_t Chunk = Source->Bytes - Offset;
        if ( Chunk > MDO_STAGE_CHUNK ) Chunk = MDO_STAGE_CHUNK;
        Ok = MdoBackupCheck(Limits, Cancel, Error) &&
            xrtWriteFull(File, Source->Data + Offset, Chunk, NULL);
        Offset += Chunk;
    }
    if ( Ok ) Ok = MdoBackupCheck(Limits, Cancel, Error) && xrtFlush(File);
    if ( !xrtClose(File) ) Ok = false;
    if ( !Ok && (Error == NULL || Error->eCode == XWORK_ERROR_NONE) ) return MdoStageIO(Error, Item->Path);
    return Ok;
}

static bool MdoStageRead(MdoSessionBackupStage* Stage, size_t Index,
    const MdoBackupOwnedFile* Expected, MdoBackupOwnedFile* Result,
    const MdoSessionBackupLimits* Limits, const xcancel* Cancel, xwork_error* Error)
{
    xfileoptions Options;
    xfileinfo Before, After;
    xfile File;
    size_t Offset = 0u;
    bool Ok;
    xrtFileOptionsInit(&Options); Options.Flags = XFILE_READ | XFILE_NOFOLLOW;
    File = xrtRootFileOpen(Stage->Directory, Expected->Path, &Options);
    if ( File == NULL ) return MdoStageIO(Error, Expected->Path);
    Ok = xrtFileStat(File, &Before) && MdoStageSame(&Before, &Stage->Files[Index].Identity) &&
        Before.Type == XFILE_TYPE_FILE && (Before.Available & XFILE_INFO_SIZE) != 0u && Before.Size == Expected->Bytes;
    if ( Ok ) {
        Result->Data = (char*)xrtMalloc(Expected->Bytes + 1u);
        if ( Result->Data == NULL ) {
            (void)MdoBackupError(Error, XWORK_ERROR_OUT_OF_MEMORY, "cannot read staged backup bytes", Expected->Path);
            Ok = false;
        }
    }
    while ( Ok && Offset < Expected->Bytes ) {
        size_t Chunk = Expected->Bytes - Offset;
        if ( Chunk > MDO_STAGE_CHUNK ) Chunk = MDO_STAGE_CHUNK;
        Ok = MdoBackupCheck(Limits, Cancel, Error) && xrtReadFull(File, Result->Data + Offset, Chunk, NULL) &&
            memcmp(Result->Data + Offset, Expected->Data + Offset, Chunk) == 0;
        Offset += Chunk;
    }
    if ( Ok ) {
        Result->Data[Expected->Bytes] = '\0';
        Ok = xrtFileStat(File, &After) && MdoStageSame(&Before, &After) && After.Size == Before.Size &&
            MdoStageMatches(Stage->Directory, &Stage->Files[Index]);
    }
    if ( !xrtClose(File) ) Ok = false;
    if ( !Ok && (Error == NULL || Error->eCode == XWORK_ERROR_NONE) )
        return MdoBackupError(Error, XWORK_ERROR_IO, "staged backup file changed or cannot be read", Expected->Path);
    return Ok;
}

/* Enumerate only to reject unknown content. Cleanup never uses this scanner
 * to delete a discovered path. A foreign object must survive a failed discard. */
static bool MdoStageInventory(MdoSessionBackupStage* Stage, const char* Path,
    unsigned Depth, size_t* Seen, const MdoSessionBackupLimits* Limits,
    const xcancel* Cancel, xwork_error* Error)
{
    xdir Dir;
    xdirentry Entry;
    xdirnext Next = XDIR_NEXT_ERROR;
    bool Ok = false;
    if ( Depth > 3u || !MdoBackupCheck(Limits, Cancel, Error) ) return false;
    Dir = xrtRootDirOpen(Stage->Directory, Path[0] ? Path : ".", XDIR_STAT);
    if ( Dir == NULL ) return MdoStageIO(Error, Path);
    while ( (Next = xrtDirNext(Dir, &Entry)) == XDIR_NEXT_ITEM ) {
        char Child[MDO_SESSION_BACKUP_PATH_CAPACITY];
        const MdoStagePath* Owned = NULL;
        size_t i;
        int Written;
        if ( !MdoBackupCheck(Limits, Cancel, Error) ) goto done;
        if ( ++*Seen > Stage->Count + Stage->DirectoryCount ) goto invalid;
        if ( Entry.Name.Size >= sizeof(Child) || memchr(Entry.Name.Data, 0, Entry.Name.Size) != NULL ) goto invalid;
        Written = snprintf(Child, sizeof(Child), "%s%s%.*s", Path, Path[0] ? "/" : "",
            (int)Entry.Name.Size, Entry.Name.Data);
        if ( Written <= 0 || (size_t)Written >= sizeof(Child) ) goto invalid;
        if ( Entry.Info.Type == XFILE_TYPE_DIRECTORY ) {
            for ( i = 0u; i < Stage->DirectoryCount; ++i )
                if ( strcmp(Child, Stage->Directories[i].Path) == 0 ) { Owned = &Stage->Directories[i]; break; }
            if ( Owned == NULL || !MdoStageMatches(Stage->Directory, Owned) ||
                 !MdoStageInventory(Stage, Child, Depth + 1u, Seen, Limits, Cancel, Error) ) goto invalid;
        } else {
            for ( i = 0u; i < Stage->Count; ++i )
                if ( strcmp(Child, Stage->Files[i].Path) == 0 ) { Owned = &Stage->Files[i]; break; }
            if ( Entry.Info.Type != XFILE_TYPE_FILE || Owned == NULL ||
                 !MdoStageMatches(Stage->Directory, Owned) ) goto invalid;
        }
    }
    Ok = Next != XDIR_NEXT_ERROR;
    if ( !Ok ) (void)MdoStageIO(Error, Path);
    goto done;
invalid:
    if ( Error == NULL || Error->eCode == XWORK_ERROR_NONE )
        (void)MdoBackupError(Error, XWORK_ERROR_IO, "private session staging inventory changed", Path);
done:
    if ( !xrtDirClose(Dir) ) { Ok = false; if ( Error == NULL || Error->eCode == XWORK_ERROR_NONE ) (void)MdoStageIO(Error, Path); }
    return Ok;
}

static bool MdoStageBudget(const MdoSessionBackup* Backup, const MdoSessionBackupLimits* Limits, xwork_error* Error)
{
    size_t i;
    if ( Backup->Count == 0u || Backup->Count > Limits->Files || Backup->Bytes > Limits->TotalBytes ) goto limit;
    for ( i = 0u; i < Backup->Count; ++i ) {
        size_t Maximum = MdoBackupPathLimit(Backup->Files[i].Path, false);
        if ( Maximum == 0u || Backup->Files[i].Bytes > Maximum || Backup->Files[i].Bytes > Limits->FileBytes ) goto limit;
    }
    return true;
limit:
    return MdoBackupError(Error, XWORK_ERROR_LIMIT, "backup exceeds private staging budgets", NULL);
}

static bool MdoStageVerify(MdoSessionBackupStage* Stage, const MdoSessionBackup* Expected,
    const MdoSessionBackupLimits* Limits, const xcancel* Cancel, xwork_error* Error)
{
    MdoSessionBackup* Read = NULL;
    MdoSessionBackupStageInfo Facts = {0};
    size_t i, Seen = 0u;
    bool Ok = false;
    Stage->Info.Verified = false;
    memset(&Stage->Info.DirectoryIdentity, 0, sizeof(Stage->Info.DirectoryIdentity));
    memset(&Stage->Info.ModelHistory, 0, sizeof(Stage->Info.ModelHistory));
    memset(&Stage->Info.Images, 0, sizeof(Stage->Info.Images));
    if ( !MdoBackupCheck(Limits, Cancel, Error) || !MdoStageBudget(Expected, Limits, Error) ) return false;
    if ( !MdoStageOpenDirectory(Stage) ) return MdoStageIO(Error, Stage->Root.Path);
    Read = MdoBackupClone(Expected, false, Limits, Cancel, Error);
    if ( Read == NULL ) goto done;
    for ( i = 0u; i < Read->Count; ++i )
        if ( !MdoStageRead(Stage, i, &Expected->Files[i], &Read->Files[i], Limits, Cancel, Error) ) goto done;
    if ( !MdoStageInventory(Stage, "", 0u, &Seen, Limits, Cancel, Error) ||
         Seen != Stage->Count + Stage->DirectoryCount ||
         !MdoBackupValidate(Read, Limits, Cancel, &Read->History, Error) ||
         !MdoBackupRelationsValidate(Read, Limits, Cancel, &Read->Relations, Error) ) goto done;
    Facts.Size = sizeof(Facts); Facts.Source.Size = sizeof(Facts.Source);
    Facts.ModelHistory.Size = sizeof(Facts.ModelHistory); Facts.Images.Size = sizeof(Facts.Images);
    if ( !MdoSessionBackupPreviewGet(Read, &Facts.Source) ||
         !MdoSessionBackupCheckModelHistory(Read, Limits, Cancel, &Facts.ModelHistory, Error) ||
         !MdoSessionBackupCheckImages(Read, Limits, Cancel, &Facts.Images, Error) ||
         !MdoBackupCheck(Limits, Cancel, Error) || !MdoStageMatches(Stage->Parent, &Stage->Root) ) goto done;
    if ( !MdoStageCloseDirectory(Stage) ) { (void)MdoStageIO(Error, Stage->Root.Path); goto done; }
    Facts.Verified = true;
    Facts.DirectoryIdentity = Stage->Root.Identity;
    snprintf(Facts.DirectoryName, sizeof(Facts.DirectoryName), "%s", Stage->Root.Path);
    MdoSessionBackupRelease(Stage->Bytes); Stage->Bytes = Read; Read = NULL;
    Stage->Info = Facts; Ok = true;
done:
    if ( !MdoStageCloseDirectory(Stage) ) { Ok = false; if ( Error == NULL || Error->eCode == XWORK_ERROR_NONE ) (void)MdoStageIO(Error, Stage->Root.Path); }
    MdoSessionBackupRelease(Read);
    if ( !Ok && (Error == NULL || Error->eCode == XWORK_ERROR_NONE) ) (void)MdoStageIO(Error, Stage->Root.Path);
    return Ok;
}

bool MdoSessionBackupStageInfoGet(const MdoSessionBackupStage* Stage, MdoSessionBackupStageInfo* Info)
{
    if ( Info == NULL || Info->Size != sizeof(*Info) ) return false;
    memset(Info, 0, sizeof(*Info)); Info->Size = sizeof(*Info);
    if ( Stage == NULL ) return false;
    *Info = Stage->Info; return true;
}

bool MdoSessionBackupStageCheck(MdoSessionBackupStage* Stage,
    const MdoSessionBackupLimits* Limits, const xcancel* Cancel, xwork_error* Error)
{
    MdoSessionBackupLimits Budget;
    xwork_error Local;
    if ( Error == NULL ) Error = &Local;
    xworkErrorInit(Error);
    if ( Stage == NULL || Stage->Bytes == NULL )
        return MdoBackupError(Error, XWORK_ERROR_INVALID_ARGUMENT, "a prepared stage is required", NULL);
    Stage->Info.Verified = false;
    memset(&Stage->Info.DirectoryIdentity, 0, sizeof(Stage->Info.DirectoryIdentity));
    memset(&Stage->Info.ModelHistory, 0, sizeof(Stage->Info.ModelHistory));
    memset(&Stage->Info.Images, 0, sizeof(Stage->Info.Images));
    if ( !MdoBackupLimits(Limits, &Budget, 30000000u, Error) ) return false;
    return MdoStageVerify(Stage, Stage->Bytes, &Budget, Cancel, Error);
}

bool MdoSessionBackupStageRelease(MdoSessionBackupStage** Pointer, xwork_error* Error)
{
    MdoSessionBackupStage* Stage;
    bool Ok = true;
    if ( Error != NULL ) xworkErrorInit(Error);
    if ( Pointer == NULL ) return MdoBackupError(Error, XWORK_ERROR_INVALID_ARGUMENT, "stage pointer is required", NULL);
    Stage = *Pointer;
    if ( Stage == NULL ) return true;
    if ( !MdoStageCloseDirectory(Stage) ) Ok = false;
    if ( Stage->Parent != NULL && !xrtRootClose(Stage->Parent) ) Ok = false;
    MdoSessionBackupRelease(Stage->Bytes); xrtFree(Stage->Files); xrtFree(Stage->Directories);
    xrtFree(Stage); *Pointer = NULL;
    return Ok || MdoStageIO(Error, NULL);
}

bool MdoSessionBackupStageDiscard(MdoSessionBackupStage** Pointer, xwork_error* Error)
{
    MdoSessionBackupStage* Stage;
    size_t i;
    if ( Error != NULL ) xworkErrorInit(Error);
    if ( Pointer == NULL ) return MdoBackupError(Error, XWORK_ERROR_INVALID_ARGUMENT, "stage pointer is required", NULL);
    Stage = *Pointer;
    if ( Stage == NULL ) return true;
    Stage->Info.Verified = false;
    if ( Stage->Root.Owned ) {
        if ( !MdoStageOpenDirectory(Stage) ) return MdoStageIO(Error, Stage->Root.Path);
        for ( i = Stage->Count; i != 0u; --i ) {
            MdoStagePath* Item = &Stage->Files[i - 1u];
            if ( !Item->Owned ) continue;
            if ( !MdoStageMatches(Stage->Directory, Item) || !xrtRootRemove(Stage->Directory, Item->Path) ) {
                (void)MdoStageCloseDirectory(Stage); return MdoStageIO(Error, Item->Path);
            }
            Item->Owned = false;
        }
        for ( i = Stage->DirectoryCount; i != 0u; --i ) {
            MdoStagePath* Item = &Stage->Directories[i - 1u];
            if ( !Item->Owned ) continue;
            if ( !MdoStageMatches(Stage->Directory, Item) || !xrtRootRemove(Stage->Directory, Item->Path) ) {
                (void)MdoStageCloseDirectory(Stage); return MdoStageIO(Error, Item->Path);
            }
            Item->Owned = false;
        }
        if ( !MdoStageCloseDirectory(Stage) || !MdoStageMatches(Stage->Parent, &Stage->Root) ||
             !xrtRootRemove(Stage->Parent, Stage->Root.Path) ) return MdoStageIO(Error, Stage->Root.Path);
        Stage->Root.Owned = false;
    }
    return MdoSessionBackupStageRelease(Pointer, Error);
}

bool MdoSessionBackupStagePrepare(const MdoSessionBackup* Backup, xroot Parent,
    const MdoSessionBackupLimits* Limits, const xcancel* Cancel,
    MdoSessionBackupStage** Output, xwork_error* Error)
{
    MdoSessionBackupStage* Stage;
    MdoSessionBackupLimits Budget;
    xwork_error Local, Cleanup;
    size_t i;
    unsigned Attempt;
    if ( Error == NULL ) Error = &Local;
    xworkErrorInit(Error);
    if ( Output == NULL || *Output != NULL || Backup == NULL || !Backup->Decoded ||
         Backup->Schema != MDO_SESSION_BACKUP_SCHEMA || Parent == NULL )
        return MdoBackupError(Error, XWORK_ERROR_INVALID_ARGUMENT, "decoded v2 backup, staging parent and empty output are required", NULL);
    if ( !MdoBackupLimits(Limits, &Budget, 30000000u, Error) ||
         !MdoBackupCheck(&Budget, Cancel, Error) || !MdoStageBudget(Backup, &Budget, Error) ) return false;
    Stage = (MdoSessionBackupStage*)xrtCalloc(1u, sizeof(*Stage));
    if ( Stage == NULL ) return MdoBackupError(Error, XWORK_ERROR_OUT_OF_MEMORY, "cannot allocate private session staging", NULL);
    *Output = Stage; Stage->Count = Backup->Count; Stage->Info.Size = sizeof(Stage->Info);
    Stage->Files = (MdoStagePath*)xrtCalloc(Stage->Count, sizeof(*Stage->Files));
    Stage->Directories = (MdoStagePath*)xrtCalloc(MDO_STAGE_DIRECTORIES, sizeof(*Stage->Directories));
    if ( Stage->Files == NULL || Stage->Directories == NULL ) {
        (void)MdoBackupError(Error, XWORK_ERROR_OUT_OF_MEMORY, "cannot allocate staging ownership index", NULL); goto fail;
    }
    Stage->Parent = xrtRootOpenIn(Parent, ".");
    if ( Stage->Parent == NULL ) { (void)MdoStageIO(Error, NULL); goto fail; }
    for ( Attempt = 0u; Attempt < MDO_STAGE_ATTEMPTS; ++Attempt ) {
        str Id;
        if ( !MdoBackupCheck(&Budget, Cancel, Error) ) goto fail;
        Id = xrtSecureStringFrom(XRT_STR_LITERAL("0123456789abcdef"), 32u);
        if ( Id == NULL ) { (void)MdoStageIO(Error, NULL); goto fail; }
        snprintf(Stage->Root.Path, sizeof(Stage->Root.Path), "restore-%s", Id); xrtFree(Id);
        if ( xrtRootDirCreate(Stage->Parent, Stage->Root.Path, 0700u) ) { Stage->Root.Owned = true; break; }
        if ( xrtErrorKind(xrtGetError()) != XERR_EXISTS ) { (void)MdoStageIO(Error, Stage->Root.Path); goto fail; }
        xrtClearError();
    }
    if ( !Stage->Root.Owned ) { (void)MdoBackupError(Error, XWORK_ERROR_LIMIT, "private staging name collision budget exhausted", NULL); goto fail; }
    snprintf(Stage->Info.DirectoryName, sizeof(Stage->Info.DirectoryName), "%s", Stage->Root.Path);
    if ( !xrtRootStat(Stage->Parent, Stage->Root.Path, false, &Stage->Root.Identity) ||
         Stage->Root.Identity.Type != XFILE_TYPE_DIRECTORY ||
         (Stage->Directory = xrtRootOpenIn(Stage->Parent, Stage->Root.Path)) == NULL ) {
        (void)MdoStageIO(Error, Stage->Root.Path); goto fail;
    }
    for ( i = 0u; i < Stage->Count; ++i ) {
        snprintf(Stage->Files[i].Path, sizeof(Stage->Files[i].Path), "%s", Backup->Files[i].Path);
        if ( !MdoBackupCheck(&Budget, Cancel, Error) ||
             !MdoStageWrite(Stage, i, &Backup->Files[i], &Budget, Cancel, Error) ) goto fail;
    }
    if ( MdoStageVerify(Stage, Backup, &Budget, Cancel, Error) ) return true;
fail:
    if ( !MdoSessionBackupStageDiscard(Output, &Cleanup) && *Output != NULL ) {
        char Reason[256];
        snprintf(Reason, sizeof(Reason), "%s", Error->sMessage);
        (void)MdoBackupError(Error, Error->eCode, "staging failed; retain cleanup handle", Reason);
    }
    return false;
}
