#include <stdlib.h>
#include <string.h>

#include "../../include/mdo/home.h"
#include "../../include/mdo/migration.h"

#define MDO_MIGRATION_MAX_FILES 4096u
#define MDO_MIGRATION_MAX_ENTRIES 8192u
#define MDO_MIGRATION_MAX_DEPTH 8u
#define MDO_MIGRATION_MAX_PATH 1024u
#define MDO_MIGRATION_MAX_TOTAL (UINT64_C(256) * 1024u * 1024u)
#define MDO_MIGRATION_MAX_FILE (UINT64_C(64) * 1024u * 1024u)
#define MDO_MIGRATION_MAX_JSON (1024u * 1024u)
#define MDO_MIGRATION_READ_CHUNK (64u * 1024u)

typedef enum MdoMigrationFileKind {
    MDO_MIGRATION_FILE_OTHER = 0,
    MDO_MIGRATION_FILE_CONFIG,
    MDO_MIGRATION_FILE_PROJECT,
    MDO_MIGRATION_FILE_SESSION_META,
    MDO_MIGRATION_FILE_SESSION_JOURNAL,
    MDO_MIGRATION_FILE_SESSION_SNAPSHOT,
    MDO_MIGRATION_FILE_SESSION_UI,
    MDO_MIGRATION_FILE_MEMORY,
    MDO_MIGRATION_FILE_SCHEDULE,
    MDO_MIGRATION_FILE_AUDIT
} MdoMigrationFileKind;

typedef struct MdoMigrationFile {
    char* Path;
    uint64 Size;
    MdoMigrationFileKind Kind;
} MdoMigrationFile;

typedef struct MdoMigrationScan {
    MdoMigrationFile* Files;
    size_t Count;
    size_t EntryCount;
    uint64 TotalBytes;
} MdoMigrationScan;

static void MdoMigrationError(xwork_error* Error, xwork_error_code Code,
    const char* Message)
{
    if ( Error == NULL ) return;
    xworkErrorInit(Error);
    Error->eCode = Code;
    snprintf(Error->sMessage, sizeof(Error->sMessage), "%s", Message);
}

static void MdoMigrationXrtError(xwork_error* Error, const char* Fallback)
{
    const xerror* Cause = xrtGetError();
    MdoMigrationError(Error, XWORK_ERROR_IO,
        Cause != NULL && xrtErrorMessage(Cause) != NULL ?
        xrtErrorMessage(Cause) : Fallback);
}

static bool MdoMigrationCopy(char* Output, size_t Capacity,
    const char* Text)
{
    size_t Size;
    if ( Output == NULL || Capacity == 0u || Text == NULL ) return false;
    Size = strlen(Text);
    if ( Size >= Capacity ) return false;
    memcpy(Output, Text, Size + 1u);
    return true;
}

static bool MdoMigrationEndsWith(const char* Text, const char* Suffix)
{
    size_t TextSize = strlen(Text);
    size_t SuffixSize = strlen(Suffix);
    return TextSize >= SuffixSize &&
        memcmp(Text + TextSize - SuffixSize, Suffix, SuffixSize) == 0;
}

static size_t MdoMigrationSlashCount(const char* Text)
{
    size_t Count = 0u;
    for ( ; *Text != '\0'; ++Text ) if ( *Text == '/' ) ++Count;
    return Count;
}

static MdoMigrationFileKind MdoMigrationClassify(const char* Path)
{
    size_t Slashes = MdoMigrationSlashCount(Path);
    if ( strcmp(Path, "config.json") == 0 )
        return MDO_MIGRATION_FILE_CONFIG;
    if ( strcmp(Path, "audit.log") == 0 )
        return MDO_MIGRATION_FILE_AUDIT;
    if ( strncmp(Path, "memory/", 7u) == 0 &&
         MdoMigrationEndsWith(Path, ".md") )
        return MDO_MIGRATION_FILE_MEMORY;
    if ( strncmp(Path, "schedules/", 10u) == 0 && Slashes == 1u &&
         MdoMigrationEndsWith(Path, ".json") )
        return MDO_MIGRATION_FILE_SCHEDULE;
    if ( strncmp(Path, "projects/", 9u) != 0 )
        return MDO_MIGRATION_FILE_OTHER;
    if ( Slashes == 2u && MdoMigrationEndsWith(Path, "/project.json") )
        return MDO_MIGRATION_FILE_PROJECT;
    if ( Slashes >= 3u && strstr(Path, "/memory/") != NULL &&
         MdoMigrationEndsWith(Path, ".md") )
        return MDO_MIGRATION_FILE_MEMORY;
    if ( Slashes != 3u || strstr(Path, "/sessions/") == NULL )
        return MDO_MIGRATION_FILE_OTHER;
    if ( MdoMigrationEndsWith(Path, ".meta.json") )
        return MDO_MIGRATION_FILE_SESSION_META;
    if ( MdoMigrationEndsWith(Path, ".ui.jsonl") )
        return MDO_MIGRATION_FILE_SESSION_UI;
    if ( MdoMigrationEndsWith(Path, ".jsonl") )
        return MDO_MIGRATION_FILE_SESSION_JOURNAL;
    if ( MdoMigrationEndsWith(Path, ".snap") )
        return MDO_MIGRATION_FILE_SESSION_SNAPSHOT;
    return MDO_MIGRATION_FILE_OTHER;
}

static bool MdoMigrationNameValid(xstrview Name)
{
    size_t i;
    if ( Name.Size == 0u || Name.Size > 255u ||
         memchr(Name.Data, '\0', Name.Size) != NULL ||
         !xrtUtf8Valid(Name, NULL) ) return false;
    if ( (Name.Size == 1u && Name.Data[0] == '.') ||
         (Name.Size == 2u && Name.Data[0] == '.' && Name.Data[1] == '.') )
        return false;
    for ( i = 0u; i < Name.Size; ++i )
        if ( Name.Data[i] == '/' || Name.Data[i] == '\\' ) return false;
    return true;
}

static char* MdoMigrationChildPath(const char* Parent, xstrview Name)
{
    size_t ParentSize = strcmp(Parent, ".") == 0 ? 0u : strlen(Parent);
    size_t Total = ParentSize + (ParentSize != 0u ? 1u : 0u) + Name.Size;
    char* Result;
    if ( Total == 0u || Total > MDO_MIGRATION_MAX_PATH ) return NULL;
    Result = (char*)xrtMalloc(Total + 1u);
    if ( Result == NULL ) return NULL;
    if ( ParentSize != 0u ) {
        memcpy(Result, Parent, ParentSize);
        Result[ParentSize++] = '/';
    }
    memcpy(Result + ParentSize, Name.Data, Name.Size);
    Result[Total] = '\0';
    return Result;
}

static void MdoMigrationScanUnit(MdoMigrationScan* Scan)
{
    size_t i;
    if ( Scan == NULL ) return;
    for ( i = 0u; i < Scan->Count; ++i ) xrtFree(Scan->Files[i].Path);
    xrtFree(Scan->Files);
    memset(Scan, 0, sizeof(*Scan));
}

static bool MdoMigrationScanPush(MdoMigrationScan* Scan, char* Path,
    uint64 Size, xwork_error* Error)
{
    MdoMigrationFile* Files;
    if ( Scan->Count >= MDO_MIGRATION_MAX_FILES ||
         Size > MDO_MIGRATION_MAX_FILE ||
         Scan->TotalBytes > MDO_MIGRATION_MAX_TOTAL - Size ) {
        MdoMigrationError(Error, XWORK_ERROR_LIMIT,
            "legacy source exceeds migration preview limits");
        return false;
    }
    Files = (MdoMigrationFile*)xrtRealloc(Scan->Files,
        (Scan->Count + 1u) * sizeof(*Files));
    if ( Files == NULL ) {
        MdoMigrationError(Error, XWORK_ERROR_OUT_OF_MEMORY,
            "cannot grow legacy migration inventory");
        return false;
    }
    Scan->Files = Files;
    Files[Scan->Count].Path = Path;
    Files[Scan->Count].Size = Size;
    Files[Scan->Count].Kind = MdoMigrationClassify(Path);
    ++Scan->Count;
    Scan->TotalBytes += Size;
    return true;
}

static bool MdoMigrationScanDirectory(xroot Root, const char* Path,
    size_t Depth, MdoMigrationScan* Scan, xwork_error* Error)
{
    xdir Directory = NULL;
    xdirentry Entry;
    xdirnext Next;
    bool Ok = false;
    Directory = xrtRootDirOpen(Root, Path, XDIR_STAT);
    if ( Directory == NULL ) goto io;
    memset(&Entry, 0, sizeof(Entry));
    while ( (Next = xrtDirNext(Directory, &Entry)) == XDIR_NEXT_ITEM ) {
        char* Child;
        if ( ++Scan->EntryCount > MDO_MIGRATION_MAX_ENTRIES ) {
            MdoMigrationError(Error, XWORK_ERROR_LIMIT,
                "legacy source contains too many directory entries");
            goto done;
        }
        if ( (Entry.Flags & XDIR_ENTRY_UTF8) == 0u ||
             !MdoMigrationNameValid(Entry.Name) ) {
            MdoMigrationError(Error, XWORK_ERROR_INVALID_ARGUMENT,
                "legacy source contains a non-portable path");
            goto done;
        }
        Child = MdoMigrationChildPath(Path, Entry.Name);
        if ( Child == NULL ) {
            MdoMigrationError(Error, XWORK_ERROR_LIMIT,
                "legacy source contains an oversized path");
            goto done;
        }
        if ( Entry.Info.Type == XFILE_TYPE_DIRECTORY ) {
            if ( Depth >= MDO_MIGRATION_MAX_DEPTH ) {
                xrtFree(Child);
                MdoMigrationError(Error, XWORK_ERROR_LIMIT,
                    "legacy source directory depth exceeds the migration limit");
                goto done;
            }
            if ( !MdoMigrationScanDirectory(Root, Child, Depth + 1u,
                    Scan, Error) ) {
                xrtFree(Child);
                goto done;
            }
            xrtFree(Child);
        } else if ( Entry.Info.Type == XFILE_TYPE_FILE &&
                    (Entry.Info.Available & XFILE_INFO_SIZE) != 0u ) {
            if ( !MdoMigrationScanPush(Scan, Child, Entry.Info.Size, Error) ) {
                xrtFree(Child);
                goto done;
            }
        } else {
            xrtFree(Child);
            MdoMigrationError(Error, XWORK_ERROR_INVALID_ARGUMENT,
                "legacy source contains a link or unsupported file type");
            goto done;
        }
    }
    if ( Next == XDIR_NEXT_ERROR ) goto io;
    Ok = true;
    goto done;
io:
    MdoMigrationXrtError(Error, "cannot enumerate legacy migration source");
done:
    if ( Directory != NULL && !xrtDirClose(Directory) && Ok ) {
        Ok = false;
        MdoMigrationXrtError(Error, "cannot close legacy source directory");
    }
    return Ok;
}

static int MdoMigrationFileCompare(const void* LeftValue,
    const void* RightValue)
{
    const MdoMigrationFile* Left = (const MdoMigrationFile*)LeftValue;
    const MdoMigrationFile* Right = (const MdoMigrationFile*)RightValue;
    return strcmp(Left->Path, Right->Path);
}

static MdoMigrationFile* MdoMigrationFind(MdoMigrationScan* Scan,
    const char* Path)
{
    size_t i;
    for ( i = 0u; i < Scan->Count; ++i )
        if ( strcmp(Scan->Files[i].Path, Path) == 0 ) return &Scan->Files[i];
    return NULL;
}

static bool MdoMigrationRead(xroot Root, const MdoMigrationFile* Item,
    size_t Limit, char** Data, size_t* Size, xwork_error* Error)
{
    xfileoptions Options;
    xfile File = NULL;
    xfileinfo Info;
    char* Bytes = NULL;
    bool Ok = false;
    *Data = NULL;
    *Size = 0u;
    if ( Item->Size > Limit || Item->Size > SIZE_MAX - 1u ) {
        MdoMigrationError(Error, XWORK_ERROR_LIMIT,
            "legacy JSON file exceeds the migration limit");
        return false;
    }
    xrtFileOptionsInit(&Options);
    Options.Flags = XFILE_READ | XFILE_NOFOLLOW;
    File = xrtRootFileOpen(Root, Item->Path, &Options);
    if ( File == NULL || !xrtFileStat(File, &Info) ||
         Info.Type != XFILE_TYPE_FILE ||
         (Info.Available & XFILE_INFO_SIZE) == 0u ||
         Info.Size != Item->Size ) goto changed;
    Bytes = (char*)xrtMalloc((size_t)Item->Size + 1u);
    if ( Bytes == NULL ) {
        MdoMigrationError(Error, XWORK_ERROR_OUT_OF_MEMORY,
            "cannot allocate legacy JSON buffer");
        goto done;
    }
    if ( Item->Size != 0u && !xrtReadFull(File, Bytes,
            (size_t)Item->Size, NULL) ) goto io;
    Bytes[Item->Size] = '\0';
    *Data = Bytes;
    *Size = (size_t)Item->Size;
    Bytes = NULL;
    Ok = true;
    goto done;
changed:
    MdoMigrationError(Error, XWORK_ERROR_CONTEXT,
        "legacy source changed while its preview was being generated");
    goto done;
io:
    MdoMigrationXrtError(Error, "cannot read legacy migration file");
done:
    if ( File != NULL && !xrtClose(File) && Ok ) {
        Ok = false;
        MdoMigrationXrtError(Error, "cannot close legacy migration file");
    }
    xrtFree(Bytes);
    if ( !Ok ) {
        xrtFree(*Data);
        *Data = NULL;
        *Size = 0u;
    }
    return Ok;
}

static bool MdoMigrationString(const xvalue* Object, const char* Key,
    bool Required)
{
    const xvalue* Value = xrtValueObjectGet(Object, xrtStrView(Key));
    xstrview Text;
    if ( Value == NULL ) return !Required;
    return xrtValueType(Value) == XVALUE_STRING &&
        xrtValueGetString(Value, &Text) && (!Required || Text.Size != 0u) &&
        Text.Size <= 4096u &&
        memchr(Text.Data, '\0', Text.Size) == NULL &&
        xrtUtf8Valid(Text, NULL);
}

static xvalue* MdoMigrationParseJson(xroot Root,
    const MdoMigrationFile* Item, xwork_error* Error)
{
    xjsonreadconfig Config;
    char* Text = NULL;
    size_t Size = 0u;
    xvalue* Value = NULL;
    if ( !MdoMigrationRead(Root, Item, MDO_MIGRATION_MAX_JSON,
            &Text, &Size, Error) ) return NULL;
    xrtJsonReadConfigInit(&Config);
    Config.MaxInputBytes = MDO_MIGRATION_MAX_JSON;
    Config.MaxDepth = 16u;
    Config.MaxValues = 8192u;
    Config.MaxContainerItems = 4096u;
    Value = xrtJsonRead(xrtStrViewN(Text, Size), &Config);
    xrtFree(Text);
    if ( Value == NULL )
        MdoMigrationError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "legacy source contains malformed JSON");
    return Value;
}

static bool MdoMigrationValidateConfig(xroot Root, MdoMigrationScan* Scan,
    MdoMigrationPreview* Preview, xwork_error* Error)
{
    MdoMigrationFile* Item = MdoMigrationFind(Scan, "config.json");
    xvalue* Config;
    const xvalue* Models;
    const xvalue* Settings;
    size_t i;
    bool Ok = false;
    if ( Item == NULL || Item->Kind != MDO_MIGRATION_FILE_CONFIG ) {
        MdoMigrationError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "legacy source does not contain config.json");
        return false;
    }
    Config = MdoMigrationParseJson(Root, Item, Error);
    if ( Config == NULL ) return false;
    Models = xrtValueObjectGet(Config, xrtStrView("models"));
    Settings = xrtValueObjectGet(Config, xrtStrView("settings"));
    if ( xrtValueType(Config) != XVALUE_OBJECT || Models == NULL ||
         xrtValueType(Models) != XVALUE_ARRAY ||
         xrtValueCount(Models) > 64u ||
         (Settings != NULL && xrtValueType(Settings) != XVALUE_OBJECT) ||
         !MdoMigrationString(Config, "defaultModel", false) ||
         !MdoMigrationString(Config, "activeProject", false) ) goto invalid;
    for ( i = 0u; i < xrtValueCount(Models); ++i ) {
        const xvalue* Model = xrtValueArrayGet(Models, i);
        if ( Model == NULL || xrtValueType(Model) != XVALUE_OBJECT ||
             !MdoMigrationString(Model, "id", true) ||
             !MdoMigrationString(Model, "name", false) ||
             !MdoMigrationString(Model, "baseUrl", false) ||
             !MdoMigrationString(Model, "apiKey", false) ||
             !MdoMigrationString(Model, "model", false) ||
             !MdoMigrationString(Model, "dialect", false) ||
             !MdoMigrationString(Model, "reasoning", false) ) goto invalid;
    }
    Preview->ModelCount = xrtValueCount(Models);
    Ok = true;
    goto done;
invalid:
    MdoMigrationError(Error, XWORK_ERROR_INVALID_ARGUMENT,
        "legacy config.json does not satisfy the supported schema");
done:
    xrtValueRelease(Config);
    return Ok;
}

static bool MdoMigrationValidateObjects(xroot Root, MdoMigrationScan* Scan,
    MdoMigrationPreview* Preview, xwork_error* Error)
{
    size_t i;
    for ( i = 0u; i < Scan->Count; ++i ) {
        MdoMigrationFile* Item = &Scan->Files[i];
        xvalue* Value;
        const xvalue* Kind;
        xstrview KindText;
        switch ( Item->Kind ) {
        case MDO_MIGRATION_FILE_PROJECT:
            Value = MdoMigrationParseJson(Root, Item, Error);
            if ( Value == NULL ) return false;
            if ( xrtValueType(Value) != XVALUE_OBJECT ||
                 !MdoMigrationString(Value, "name", true) ||
                 !MdoMigrationString(Value, "path", true) ||
                 !MdoMigrationString(Value, "defaultModel", false) ) {
                xrtValueRelease(Value);
                MdoMigrationError(Error, XWORK_ERROR_INVALID_ARGUMENT,
                    "legacy project metadata does not satisfy the supported schema");
                return false;
            }
            xrtValueRelease(Value);
            ++Preview->ProjectCount;
            break;
        case MDO_MIGRATION_FILE_SESSION_META:
            Value = MdoMigrationParseJson(Root, Item, Error);
            if ( Value == NULL ) return false;
            if ( xrtValueType(Value) != XVALUE_OBJECT ||
                 !MdoMigrationString(Value, "id", true) ||
                 !MdoMigrationString(Value, "title", true) ||
                 !MdoMigrationString(Value, "model", true) ||
                 !MdoMigrationString(Value, "project", false) ||
                 !MdoMigrationString(Value, "userPrompt", false) ) {
                xrtValueRelease(Value);
                MdoMigrationError(Error, XWORK_ERROR_INVALID_ARGUMENT,
                    "legacy session metadata does not satisfy the supported schema");
                return false;
            }
            xrtValueRelease(Value);
            ++Preview->SessionCount;
            break;
        case MDO_MIGRATION_FILE_SCHEDULE:
            Value = MdoMigrationParseJson(Root, Item, Error);
            if ( Value == NULL ) return false;
            Kind = xrtValueObjectGet(Value, xrtStrView("kind"));
            if ( xrtValueType(Value) != XVALUE_OBJECT ||
                 !MdoMigrationString(Value, "id", true) ||
                 !MdoMigrationString(Value, "title", true) ||
                 !MdoMigrationString(Value, "prompt", true) ||
                 !MdoMigrationString(Value, "kind", true) ||
                 Kind == NULL || xrtValueType(Kind) != XVALUE_STRING ||
                 !xrtValueGetString(Kind, &KindText) ) {
                xrtValueRelease(Value);
                MdoMigrationError(Error, XWORK_ERROR_INVALID_ARGUMENT,
                    "legacy schedule does not satisfy the supported schema");
                return false;
            }
            ++Preview->ScheduleCount;
            if ( KindText.Size == 4u &&
                 memcmp(KindText.Data, "cron", 4u) == 0 )
                ++Preview->UnsupportedCount;
            xrtValueRelease(Value);
            break;
        case MDO_MIGRATION_FILE_MEMORY:
            ++Preview->MemoryFileCount;
            break;
        case MDO_MIGRATION_FILE_SESSION_UI:
        case MDO_MIGRATION_FILE_OTHER:
            ++Preview->UnsupportedCount;
            break;
        default:
            break;
        }
    }
    return true;
}

static bool MdoMigrationHashUInt64(xsha256* Hash, uint64 Value)
{
    uint8 Bytes[8];
    size_t i;
    for ( i = 0u; i < sizeof(Bytes); ++i ) {
        Bytes[i] = (uint8)(Value & 0xffu);
        Value >>= 8u;
    }
    return xrtSha256Update(Hash, Bytes, sizeof(Bytes));
}

static bool MdoMigrationHashFiles(xroot Root, const char* SourceId,
    const MdoMigrationPreview* Preview, MdoMigrationScan* Scan,
    char Output[MDO_MIGRATION_TOKEN_CAPACITY], xwork_error* Error)
{
    static const char Hex[] = "0123456789abcdef";
    xsha256 Hash;
    uint8 Digest[XRT_SHA256_SIZE];
    uint8* Buffer = NULL;
    size_t i;
    bool Ok = false;
    xrtSha256Init(&Hash);
    if ( !xrtSha256Update(&Hash, "mdo-legacy-migration-preview-v1", 31u) ||
         !MdoMigrationHashUInt64(&Hash, (uint64)strlen(SourceId)) ||
         !xrtSha256Update(&Hash, SourceId, strlen(SourceId)) ||
         !MdoMigrationHashUInt64(&Hash,
            (uint64)strlen(Preview->SourcePath)) ||
         !xrtSha256Update(&Hash, Preview->SourcePath,
            strlen(Preview->SourcePath)) ||
         !MdoMigrationHashUInt64(&Hash,
            (uint64)strlen(Preview->TargetPath)) ||
         !xrtSha256Update(&Hash, Preview->TargetPath,
            strlen(Preview->TargetPath)) ) goto crypto;
    Buffer = (uint8*)xrtMalloc(MDO_MIGRATION_READ_CHUNK);
    if ( Buffer == NULL ) {
        MdoMigrationError(Error, XWORK_ERROR_OUT_OF_MEMORY,
            "cannot allocate migration hash buffer");
        goto done;
    }
    for ( i = 0u; i < Scan->Count; ++i ) {
        const MdoMigrationFile* Item = &Scan->Files[i];
        xfileoptions Options;
        xfile File = NULL;
        xfileinfo Info;
        uint64 Remaining = Item->Size;
        bool FileOk = false;
        if ( !MdoMigrationHashUInt64(&Hash,
                (uint64)strlen(Item->Path)) ||
             !xrtSha256Update(&Hash, Item->Path, strlen(Item->Path)) )
            goto crypto;
        if ( !MdoMigrationHashUInt64(&Hash, Item->Size) ) goto crypto;
        xrtFileOptionsInit(&Options);
        Options.Flags = XFILE_READ | XFILE_NOFOLLOW;
        File = xrtRootFileOpen(Root, Item->Path, &Options);
        if ( File == NULL || !xrtFileStat(File, &Info) ||
             Info.Type != XFILE_TYPE_FILE ||
             (Info.Available & XFILE_INFO_SIZE) == 0u ||
             Info.Size != Item->Size ) goto changed;
        while ( Remaining != 0u ) {
            size_t Chunk = Remaining > MDO_MIGRATION_READ_CHUNK ?
                MDO_MIGRATION_READ_CHUNK : (size_t)Remaining;
            if ( !xrtReadFull(File, Buffer, Chunk, NULL) ||
                 !xrtSha256Update(&Hash, Buffer, Chunk) ) goto file_done;
            Remaining -= Chunk;
        }
        {
            size_t Extra = 0u;
            if ( !xrtRead(File, Buffer, 1u, &Extra) || Extra != 0u )
                goto changed;
        }
        FileOk = true;
file_done:
        if ( File != NULL && !xrtClose(File) ) FileOk = false;
        if ( !FileOk ) {
            if ( Error == NULL || Error->eCode == XWORK_ERROR_NONE )
                MdoMigrationXrtError(Error,
                    "cannot hash legacy migration file");
            goto done;
        }
        continue;
changed:
        MdoMigrationError(Error, XWORK_ERROR_CONTEXT,
            "legacy source changed while its preview was being generated");
        goto file_done;
    }
    if ( !xrtSha256Final(&Hash, Digest) ) goto crypto;
    for ( i = 0u; i < XRT_SHA256_SIZE; ++i ) {
        Output[i * 2u] = Hex[Digest[i] >> 4u];
        Output[i * 2u + 1u] = Hex[Digest[i] & 0x0fu];
    }
    Output[XRT_SHA256_SIZE * 2u] = '\0';
    Ok = true;
    goto done;
crypto:
    MdoMigrationError(Error, XWORK_ERROR_CONTEXT,
        "cannot hash legacy migration preview");
done:
    xrtFree(Buffer);
    return Ok;
}

static bool MdoMigrationSamePath(const char* Left, const char* Right)
{
    unsigned char A;
    unsigned char B;
    for ( ; ; ++Left, ++Right ) {
        A = (unsigned char)*Left;
        B = (unsigned char)*Right;
#if defined(_WIN32) || defined(_WIN64)
        if ( A == '\\' ) A = '/';
        if ( B == '\\' ) B = '/';
        if ( A >= 'A' && A <= 'Z' ) A = (unsigned char)(A + ('a' - 'A'));
        if ( B >= 'A' && B <= 'Z' ) B = (unsigned char)(B + ('a' - 'A'));
#endif
        if ( A != B ) return false;
        if ( A == '\0' ) return true;
    }
}

static char* MdoMigrationSourcePath(const char* SourceId)
{
    char* Base = NULL;
    char* Parent = NULL;
    char* Result = NULL;
    if ( strcmp(SourceId, MDO_MIGRATION_SOURCE_PORTABLE_DATA) == 0 ) {
        Base = xrtPathExecutable();
        if ( Base != NULL ) Parent = xrtPathParent(Base);
        if ( Parent != NULL ) Result = xrtPathJoin(Parent, "data");
    } else if ( strcmp(SourceId, MDO_MIGRATION_SOURCE_USER_HOME) == 0 ) {
        Base = xrtPathHome();
        if ( Base != NULL ) Result = xrtPathJoin(Base, ".mdo");
    }
    xrtFree(Parent);
    xrtFree(Base);
    return Result;
}

bool MdoLegacyMigrationPreview(const char* SourceId,
    MdoMigrationPreview* Preview, xwork_error* Error)
{
    MdoHomeSnapshot Home;
    MdoMigrationScan Scan;
    char* Source = NULL;
    char* SourceAbsolute = NULL;
    char* TargetAbsolute = NULL;
    xfileinfo Info;
    xroot Root = NULL;
    const xerror* Cause;
    bool Ok = false;
    if ( Error != NULL ) xworkErrorInit(Error);
    if ( SourceId == NULL || Preview == NULL ||
         Preview->Size < sizeof(*Preview) ||
         (strcmp(SourceId, MDO_MIGRATION_SOURCE_PORTABLE_DATA) != 0 &&
          strcmp(SourceId, MDO_MIGRATION_SOURCE_USER_HOME) != 0) ) {
        MdoMigrationError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "invalid legacy migration preview request");
        return false;
    }
    memset(Preview, 0, sizeof(*Preview));
    Preview->Size = sizeof(*Preview);
    memset(&Scan, 0, sizeof(Scan));
    memset(&Home, 0, sizeof(Home));
    Home.Size = sizeof(Home);
    if ( !MdoMigrationCopy(Preview->SourceId, sizeof(Preview->SourceId),
            SourceId) || !MdoHomeGetSnapshot(&Home) ) goto io;
    Source = MdoMigrationSourcePath(SourceId);
    SourceAbsolute = Source != NULL ? xrtPathAbs(Source) : NULL;
    TargetAbsolute = Home.Path != NULL ? xrtPathAbs(Home.Path) : NULL;
    if ( SourceAbsolute == NULL || TargetAbsolute == NULL ||
         !MdoMigrationCopy(Preview->SourcePath, sizeof(Preview->SourcePath),
            SourceAbsolute) ||
         !MdoMigrationCopy(Preview->TargetPath, sizeof(Preview->TargetPath),
            TargetAbsolute) ) {
        MdoMigrationError(Error, XWORK_ERROR_LIMIT,
            "migration source or target path is unavailable or too long");
        goto done;
    }
    if ( !xrtPathStat(SourceAbsolute, false, &Info) ) {
        Cause = xrtGetError();
        if ( Cause != NULL && xrtErrorKind(Cause) == XERR_NOT_FOUND ) {
            xrtClearError();
            snprintf(Preview->Message, sizeof(Preview->Message),
                "legacy source was not found");
            Ok = true;
            goto done;
        }
        goto io;
    }
    Preview->Found = true;
    if ( Info.Type != XFILE_TYPE_DIRECTORY ) {
        snprintf(Preview->Message, sizeof(Preview->Message),
            "legacy source is not a real directory");
        Ok = true;
        goto done;
    }
    if ( xrtPathStat(TargetAbsolute, false, &Info) ) {
        Preview->ConflictCount = 1u;
        Preview->TargetAvailable = false;
    } else {
        Cause = xrtGetError();
        if ( Cause == NULL || xrtErrorKind(Cause) != XERR_NOT_FOUND ) goto io;
        xrtClearError();
        Preview->TargetAvailable = true;
    }
    if ( MdoMigrationSamePath(SourceAbsolute, TargetAbsolute) ) {
        Preview->TargetAvailable = false;
        if ( Preview->ConflictCount == 0u ) Preview->ConflictCount = 1u;
    }
    Root = xrtRootOpen(SourceAbsolute);
    if ( Root == NULL ) goto io;
    if ( !MdoMigrationScanDirectory(Root, ".", 0u, &Scan, Error) )
        goto invalid;
    if ( Scan.Count > 1u ) qsort(Scan.Files, Scan.Count,
        sizeof(*Scan.Files), MdoMigrationFileCompare);
    Preview->FileCount = Scan.Count;
    Preview->TotalBytes = Scan.TotalBytes;
    if ( !MdoMigrationValidateConfig(Root, &Scan, Preview, Error) ||
         !MdoMigrationValidateObjects(Root, &Scan, Preview, Error) )
        goto invalid;
    Preview->EstimatedWriteCount = 3u + Preview->ModelCount +
        Preview->ProjectCount + (Preview->SessionCount * 3u) +
        Preview->ScheduleCount + Preview->MemoryFileCount;
    if ( !MdoMigrationHashFiles(Root, SourceId, Preview, &Scan,
            Preview->PreviewToken, Error) ) goto invalid;
    Preview->Valid = true;
    Preview->Importable = Preview->TargetAvailable;
    snprintf(Preview->Message, sizeof(Preview->Message), "%s",
        Preview->Importable ? "legacy source is ready to import" :
        "target Home already exists; migration will not overwrite it");
    Ok = true;
    goto done;
invalid:
    if ( Error != NULL && Error->sMessage[0] != '\0' )
        snprintf(Preview->Message, sizeof(Preview->Message), "%s",
            Error->sMessage);
    if ( Error != NULL ) xworkErrorInit(Error);
    xrtClearError();
    Ok = true;
    goto done;
io:
    MdoMigrationXrtError(Error, "cannot inspect legacy migration source");
done:
    if ( Root != NULL && !xrtRootClose(Root) && Ok ) {
        Ok = false;
        MdoMigrationXrtError(Error, "cannot close legacy migration source");
    }
    MdoMigrationScanUnit(&Scan);
    xrtFree(TargetAbsolute);
    xrtFree(SourceAbsolute);
    xrtFree(Source);
    return Ok;
}

bool MdoLegacyMigrationDiscover(MdoMigrationPreview* Previews,
    size_t Capacity, size_t* Count, xwork_error* Error)
{
    static const char* const Sources[] = {
        MDO_MIGRATION_SOURCE_PORTABLE_DATA,
        MDO_MIGRATION_SOURCE_USER_HOME
    };
    size_t Written = 0u;
    size_t i;
    if ( Count == NULL || (Previews == NULL && Capacity != 0u) ) {
        MdoMigrationError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "invalid legacy migration discovery request");
        return false;
    }
    if ( Error != NULL ) xworkErrorInit(Error);
    for ( i = 0u; i < sizeof(Sources) / sizeof(Sources[0]); ++i ) {
        MdoMigrationPreview Preview;
        memset(&Preview, 0, sizeof(Preview));
        Preview.Size = sizeof(Preview);
        if ( !MdoLegacyMigrationPreview(Sources[i], &Preview, Error) )
            return false;
        if ( Written != 0u && MdoMigrationSamePath(
                Previews[Written - 1u].SourcePath, Preview.SourcePath) )
            continue;
        if ( Written >= Capacity ) {
            MdoMigrationError(Error, XWORK_ERROR_LIMIT,
                "legacy migration discovery output is too small");
            return false;
        }
        Previews[Written++] = Preview;
    }
    *Count = Written;
    return true;
}
