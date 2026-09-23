#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"

#define MDO_MIGRATION_COPY_CHUNK (64u * 1024u)

static bool MdoMigrationIdValid(const char* Text, size_t Capacity)
{
    size_t i;
    size_t Size;
    if ( Text == NULL || Text[0] == '\0' ) return false;
    Size = strlen(Text);
    if ( Size >= Capacity || (Size == 1u && Text[0] == '.') ||
         (Size == 2u && Text[0] == '.' && Text[1] == '.') ) return false;
    for ( i = 0u; i < Size; ++i ) {
        unsigned char Byte = (unsigned char)Text[i];
        if ( (Byte >= 'a' && Byte <= 'z') ||
             (Byte >= 'A' && Byte <= 'Z') ||
             (Byte >= '0' && Byte <= '9') || Byte == '-' || Byte == '_' ||
             (Byte == '.' && i != 0u) ) continue;
        return false;
    }
    return true;
}

bool MdoMigrationIdentifier(const char* Text, const char* Prefix,
    char* Output, size_t Capacity)
{
    static const char Hex[] = "0123456789abcdef";
    uint8 Digest[XRT_SHA256_SIZE];
    size_t PrefixSize;
    size_t TextSize;
    size_t Out = 0u;
    size_t i;
    if ( Text == NULL || Prefix == NULL || Output == NULL || Capacity < 12u )
        return false;
    if ( MdoMigrationIdValid(Text, Capacity) )
        return MdoMigrationCopy(Output, Capacity, Text);
    TextSize = strlen(Text);
    if ( TextSize == 0u || TextSize > 4096u ||
         !xrtUtf8Valid(xrtStrViewN(Text, TextSize), NULL) ||
         !xrtSha256(Text, TextSize, Digest) ) return false;
    PrefixSize = strlen(Prefix);
    for ( i = 0u; i < PrefixSize && Out + 10u < Capacity; ++i ) {
        unsigned char Byte = (unsigned char)Prefix[i];
        if ( isalnum(Byte) ) Output[Out++] = (char)tolower(Byte);
    }
    if ( Out == 0u ) Output[Out++] = 'i';
    if ( Out + 10u >= Capacity ) return false;
    Output[Out++] = '-';
    for ( i = 0u; i < 4u; ++i ) {
        Output[Out++] = Hex[Digest[i] >> 4u];
        Output[Out++] = Hex[Digest[i] & 0x0fu];
    }
    Output[Out] = '\0';
    return true;
}

static bool MdoMigrationStageEnsureParents(MdoMigrationContext* Context,
    const char* Path, xwork_error* Error)
{
    char* Parent = xrtStrDup(Path);
    char* Cursor;
    if ( Parent == NULL ) {
        MdoMigrationError(Error, XWORK_ERROR_OUT_OF_MEMORY,
            "cannot allocate migration staging path");
        return false;
    }
    for ( Cursor = Parent; *Cursor != '\0'; ++Cursor ) {
        xfileinfo Info;
        const xerror* Cause;
        if ( *Cursor != '/' ) continue;
        *Cursor = '\0';
        if ( xrtRootStat(Context->StageRoot, Parent, false, &Info) ) {
            if ( Info.Type != XFILE_TYPE_DIRECTORY ) {
                xrtFree(Parent);
                MdoMigrationError(Error, XWORK_ERROR_IO,
                    "migration staging parent is not a directory");
                return false;
            }
        } else {
            Cause = xrtGetError();
            if ( Cause == NULL || xrtErrorKind(Cause) != XERR_NOT_FOUND ) {
                xrtFree(Parent);
                MdoMigrationXrtError(Error,
                    "cannot inspect migration staging parent");
                return false;
            }
            xrtClearError();
            if ( !xrtRootDirCreate(Context->StageRoot, Parent, 0700u) ) {
                xrtFree(Parent);
                MdoMigrationXrtError(Error,
                    "cannot create migration staging parent");
                return false;
            }
        }
        *Cursor = '/';
    }
    xrtFree(Parent);
    return true;
}

bool MdoMigrationStageDirectory(MdoMigrationContext* Context,
    const char* Path, xwork_error* Error)
{
    xfileinfo Info;
    const xerror* Cause;
    if ( Context == NULL || Context->StageRoot == NULL || Path == NULL ||
         Path[0] == '\0' || !MdoMigrationStageEnsureParents(Context, Path,
            Error) ) return false;
    if ( xrtRootStat(Context->StageRoot, Path, false, &Info) ) {
        if ( Info.Type == XFILE_TYPE_DIRECTORY ) return true;
        MdoMigrationError(Error, XWORK_ERROR_IO,
            "migration path is not a directory");
        return false;
    }
    Cause = xrtGetError();
    if ( Cause == NULL || xrtErrorKind(Cause) != XERR_NOT_FOUND ) {
        MdoMigrationXrtError(Error, "cannot inspect migration directory");
        return false;
    }
    xrtClearError();
    if ( !xrtRootDirCreate(Context->StageRoot, Path, 0700u) ) {
        MdoMigrationXrtError(Error, "cannot create migration directory");
        return false;
    }
    return true;
}

bool MdoMigrationStageWrite(MdoMigrationContext* Context, const char* Path,
    const void* Data, size_t Size, uint32 Mode, xwork_error* Error)
{
    xfileoptions Options;
    xfile File = NULL;
    bool Ok = false;
    if ( Context == NULL || Context->StageRoot == NULL || Path == NULL ||
         Path[0] == '\0' || (Data == NULL && Size != 0u) ||
         !MdoMigrationStageEnsureParents(Context, Path, Error) ) return false;
    xrtFileOptionsInit(&Options);
    Options.Flags = XFILE_WRITE | XFILE_CREATE | XFILE_EXCLUSIVE |
        XFILE_NOFOLLOW | XFILE_SYNC;
    Options.Mode = Mode;
    File = xrtRootFileOpen(Context->StageRoot, Path, &Options);
    if ( File == NULL || (Size != 0u &&
         !xrtWriteFull(File, Data, Size, NULL)) || !xrtFlush(File) ) {
        MdoMigrationXrtError(Error, "cannot write migration staging file");
        goto done;
    }
    Ok = true;
done:
    if ( File != NULL && !xrtClose(File) && Ok ) {
        Ok = false;
        MdoMigrationXrtError(Error, "cannot close migration staging file");
    }
    if ( Ok && Context->Result != NULL ) {
        ++Context->Result->WrittenFiles;
        if ( Context->Result->WrittenBytes <= UINT64_MAX - Size )
            Context->Result->WrittenBytes += Size;
    }
    return Ok;
}

bool MdoMigrationStageCopy(MdoMigrationContext* Context,
    const MdoMigrationFile* Source, const char* Target, uint32 Mode,
    xwork_error* Error)
{
    xfileoptions Options;
    xfile Input = NULL;
    xfile Output = NULL;
    xfileinfo Info;
    uint8* Buffer = NULL;
    uint64 Remaining;
    bool Ok = false;
    if ( Context == NULL || Source == NULL || Target == NULL ||
         !MdoMigrationStageEnsureParents(Context, Target, Error) ) return false;
    Buffer = (uint8*)xrtMalloc(MDO_MIGRATION_COPY_CHUNK);
    if ( Buffer == NULL ) {
        MdoMigrationError(Error, XWORK_ERROR_OUT_OF_MEMORY,
            "cannot allocate migration copy buffer");
        goto done;
    }
    xrtFileOptionsInit(&Options);
    Options.Flags = XFILE_READ | XFILE_NOFOLLOW;
    Input = xrtRootFileOpen(Context->SourceRoot, Source->Path, &Options);
    if ( Input == NULL || !xrtFileStat(Input, &Info) ||
         Info.Type != XFILE_TYPE_FILE ||
         (Info.Available & XFILE_INFO_SIZE) == 0u ||
         Info.Size != Source->Size ) goto changed;
    xrtFileOptionsInit(&Options);
    Options.Flags = XFILE_WRITE | XFILE_CREATE | XFILE_EXCLUSIVE |
        XFILE_NOFOLLOW | XFILE_SYNC;
    Options.Mode = Mode;
    Output = xrtRootFileOpen(Context->StageRoot, Target, &Options);
    if ( Output == NULL ) goto io;
    Remaining = Source->Size;
    while ( Remaining != 0u ) {
        size_t Chunk = Remaining > MDO_MIGRATION_COPY_CHUNK ?
            MDO_MIGRATION_COPY_CHUNK : (size_t)Remaining;
        if ( !xrtReadFull(Input, Buffer, Chunk, NULL) ||
             !xrtWriteFull(Output, Buffer, Chunk, NULL) ) goto io;
        Remaining -= Chunk;
    }
    if ( !xrtFlush(Output) ) goto io;
    Ok = true;
    goto done;
changed:
    MdoMigrationError(Error, XWORK_ERROR_CONTEXT,
        "legacy source changed during migration");
    goto done;
io:
    MdoMigrationXrtError(Error, "cannot copy legacy migration file");
done:
    if ( Output != NULL && !xrtClose(Output) && Ok ) {
        Ok = false;
        MdoMigrationXrtError(Error,
            "cannot close copied migration destination");
    }
    if ( Input != NULL && !xrtClose(Input) && Ok ) {
        Ok = false;
        MdoMigrationXrtError(Error, "cannot close legacy migration source");
    }
    xrtFree(Buffer);
    if ( Ok && Context->Result != NULL ) {
        ++Context->Result->WrittenFiles;
        if ( Context->Result->WrittenBytes <= UINT64_MAX - Source->Size )
            Context->Result->WrittenBytes += Source->Size;
    }
    return Ok;
}

bool MdoMigrationMeasureStage(MdoMigrationContext* Context,
    xwork_error* Error)
{
    MdoMigrationScan Scan;
    if ( Context == NULL || Context->StageRoot == NULL ||
         Context->Result == NULL ) return false;
    memset(&Scan, 0, sizeof(Scan));
    if ( !MdoMigrationScanDirectory(Context->StageRoot, ".", 0u, &Scan,
            Error) ) {
        MdoMigrationScanUnit(&Scan);
        return false;
    }
    Context->Result->WrittenFiles = Scan.Count;
    Context->Result->WrittenBytes = Scan.TotalBytes;
    MdoMigrationScanUnit(&Scan);
    return true;
}

MdoMigrationModelMap* MdoMigrationModelFind(MdoMigrationContext* Context,
    const char* OldId)
{
    size_t i;
    if ( Context == NULL || OldId == NULL ) return NULL;
    for ( i = 0u; i < Context->ModelCount; ++i )
        if ( strcmp(Context->Models[i].OldId, OldId) == 0 )
            return &Context->Models[i];
    return NULL;
}

MdoMigrationProjectMap* MdoMigrationProjectFind(
    MdoMigrationContext* Context, const char* OldId)
{
    size_t i;
    if ( Context == NULL || OldId == NULL ) return NULL;
    for ( i = 0u; i < Context->ProjectCount; ++i )
        if ( strcmp(Context->Projects[i].OldId, OldId) == 0 )
            return &Context->Projects[i];
    return NULL;
}

void MdoMigrationContextUnit(MdoMigrationContext* Context)
{
    size_t i;
    if ( Context == NULL ) return;
    for ( i = 0u; i < Context->ModelCount; ++i ) {
        xrtFree(Context->Models[i].OldId);
        xrtFree(Context->Models[i].NewId);
    }
    for ( i = 0u; i < Context->ProjectCount; ++i ) {
        xrtFree(Context->Projects[i].OldId);
        xrtFree(Context->Projects[i].NewId);
        xrtFree(Context->Projects[i].WorkspaceRoot);
    }
    xrtFree(Context->Models);
    xrtFree(Context->Projects);
    MdoMigrationScanUnit(&Context->Scan);
    xrtFree(Context->StagePath);
    memset(Context, 0, sizeof(*Context));
}
