#include <stdio.h>
#include <string.h>

#include "internal.h"
#include "../../include/mdo/sessions.h"

#define MDO_WS_QUERY_MAX 128u
#define MDO_WS_PATH_MAX 512u
#define MDO_WS_RESULTS_MAX 12u
#define MDO_WS_ENTRIES_MAX 5000u
#define MDO_WS_DIRS_MAX 128u
#define MDO_WS_DEPTH_MAX 5u

typedef struct MdoWsMatch {
    char Path[MDO_WS_PATH_MAX];
    unsigned Score;
} MdoWsMatch;

typedef struct MdoWsScan {
    char Query[MDO_WS_QUERY_MAX + 1u];
    MdoWsMatch Matches[MDO_WS_RESULTS_MAX];
    size_t Count;
    size_t Matched;
    size_t Entries;
    size_t Directories;
    bool Truncated;
} MdoWsScan;

static bool MdoWsId(xstrview Text, char* Output, size_t Capacity)
{
    size_t Index;
    if ( Text.Size == 0u || Text.Size >= Capacity || Text.Data[0] == '.' )
        return false;
    for ( Index = 0u; Index < Text.Size; ++Index ) {
        unsigned char Byte = (unsigned char)Text.Data[Index];
        if ( (Byte >= 'a' && Byte <= 'z') ||
             (Byte >= 'A' && Byte <= 'Z') ||
             (Byte >= '0' && Byte <= '9') || Byte == '-' || Byte == '_' ||
             Byte == '.' ) continue;
        return false;
    }
    memcpy(Output, Text.Data, Text.Size);
    Output[Text.Size] = '\0';
    return true;
}

static bool MdoWsQuery(xstrview Raw, char Output[MDO_WS_QUERY_MAX + 1u])
{
    xstrview Encoded;
    size_t Size = 0u;
    size_t Index;
    if ( Raw.Size < 3u || Raw.Size > MDO_WS_QUERY_MAX * 3u + 2u ||
         memcmp(Raw.Data, "q=", 2u) != 0 ||
         memchr(Raw.Data + 2u, '&', Raw.Size - 2u) != NULL )
        return false;
    Encoded = xrtStrViewN(Raw.Data + 2u, Raw.Size - 2u);
    if ( !xrtPercentDecode(Encoded, Output, MDO_WS_QUERY_MAX, &Size) ||
         Size == 0u || Size > MDO_WS_QUERY_MAX ||
         memchr(Output, 0, Size) != NULL ||
         !xrtUtf8Valid(xrtStrViewN(Output, Size), NULL) ) return false;
    for ( Index = 0u; Index < Size; ++Index ) {
        unsigned char Byte = (unsigned char)Output[Index];
        if ( Byte < 0x20u || Byte == 0x7fu ) return false;
        if ( Byte == '\\' ) Output[Index] = '/';
    }
    Output[Size] = '\0';
    return true;
}

static bool MdoWsIgnore(const char* Name, bool Directory)
{
    static const char* const Names[] = {
        "node_modules", "dist", "build", "target", "out",
        "__pycache__", "venv", "mdo-home", NULL
    };
    size_t Index;
    if ( Name[0] == '.' &&
         (Directory || strcmp(Name, ".gitignore") != 0) )
        return true;
    if ( !Directory ) return false;
    for ( Index = 0u; Names[Index] != NULL; ++Index )
        if ( strcmp(Name, Names[Index]) == 0 ) return true;
    return false;
}

static unsigned char MdoWsFold(unsigned char Byte)
{
    return Byte >= 'A' && Byte <= 'Z' ? (unsigned char)(Byte + 32u) : Byte;
}

static bool MdoWsContains(const char* Text, const char* Query)
{
    size_t Index;
    size_t Part;
    for ( Index = 0u; Text[Index] != '\0'; ++Index ) {
        for ( Part = 0u; Query[Part] != '\0'; ++Part ) {
            if ( Text[Index + Part] == '\0' ||
                 MdoWsFold((unsigned char)Text[Index + Part]) !=
                 MdoWsFold((unsigned char)Query[Part]) ) break;
        }
        if ( Query[Part] == '\0' ) return true;
    }
    return false;
}

static bool MdoWsPrefix(const char* Text, const char* Query)
{
    size_t Index;
    for ( Index = 0u; Query[Index] != '\0'; ++Index )
        if ( Text[Index] == '\0' ||
             MdoWsFold((unsigned char)Text[Index]) !=
             MdoWsFold((unsigned char)Query[Index]) ) return false;
    return true;
}

static bool MdoWsBefore(const MdoWsMatch* Left, const MdoWsMatch* Right)
{
    return Left->Score < Right->Score ||
        (Left->Score == Right->Score &&
         strcmp(Left->Path, Right->Path) < 0);
}

static void MdoWsInsert(MdoWsScan* Scan, const char* Path, unsigned Depth)
{
    const char* Base = strrchr(Path, '/');
    MdoWsMatch Next;
    size_t Position;
    size_t Size = strlen(Path);
    if ( !MdoWsContains(Path, Scan->Query) ) return;
    ++Scan->Matched;
    if ( Size >= sizeof(Next.Path) ) { Scan->Truncated = true; return; }
    Base = Base != NULL ? Base + 1u : Path;
    memset(&Next, 0, sizeof(Next));
    memcpy(Next.Path, Path, Size + 1u);
    Next.Score = (MdoWsPrefix(Base, Scan->Query) ? 0u :
        MdoWsContains(Base, Scan->Query) ? 1000u : 2000u) +
        Depth * 100u + (unsigned)(Size < 99u ? Size : 99u);
    Position = 0u;
    while ( Position < Scan->Count &&
            !MdoWsBefore(&Next, &Scan->Matches[Position]) ) ++Position;
    if ( Position >= MDO_WS_RESULTS_MAX ) return;
    if ( Scan->Count < MDO_WS_RESULTS_MAX ) ++Scan->Count;
    if ( Position + 1u < Scan->Count )
        memmove(&Scan->Matches[Position + 1u], &Scan->Matches[Position],
            (Scan->Count - Position - 1u) * sizeof(Scan->Matches[0]));
    Scan->Matches[Position] = Next;
}

static bool MdoWsWalk(MdoWsScan* Scan, const char* Absolute,
    const char* Relative, unsigned Depth)
{
    xdir Directory;
    xdirentry Entry;
    xdirnext Next;
    bool Root = Depth == 0u;
    if ( Scan->Directories >= MDO_WS_DIRS_MAX ) {
        Scan->Truncated = true;
        return true;
    }
    ++Scan->Directories;
    Directory = xrtDirOpen(Absolute, XDIR_STAT);
    if ( Directory == NULL ) {
        if ( !Root ) { Scan->Truncated = true; xrtClearError(); }
        return !Root;
    }
    memset(&Entry, 0, sizeof(Entry));
    while ( (Next = xrtDirNext(Directory, &Entry)) == XDIR_NEXT_ITEM ) {
        char Name[256];
        char ChildRelative[MDO_WS_PATH_MAX];
        char* ChildAbsolute;
        bool IsDirectory;
        int Written;
        if ( Scan->Entries >= MDO_WS_ENTRIES_MAX ) {
            Scan->Truncated = true;
            break;
        }
        ++Scan->Entries;
        if ( (Entry.Flags & XDIR_ENTRY_UTF8) == 0u ||
             Entry.Name.Size == 0u || Entry.Name.Size >= sizeof(Name) ||
             memchr(Entry.Name.Data, '/', Entry.Name.Size) != NULL ||
             memchr(Entry.Name.Data, '\\', Entry.Name.Size) != NULL ) continue;
        if ( Entry.Info.Type != XFILE_TYPE_FILE &&
             Entry.Info.Type != XFILE_TYPE_DIRECTORY ) continue;
        memcpy(Name, Entry.Name.Data, Entry.Name.Size);
        Name[Entry.Name.Size] = '\0';
        IsDirectory = Entry.Info.Type == XFILE_TYPE_DIRECTORY;
        if ( MdoWsIgnore(Name, IsDirectory) ) continue;
        Written = snprintf(ChildRelative, sizeof(ChildRelative),
            "%s%s%s", Relative, Relative[0] != '\0' ? "/" : "", Name);
        if ( Written < 0 || (size_t)Written >= sizeof(ChildRelative) ) {
            Scan->Truncated = true;
            continue;
        }
        if ( !IsDirectory ) {
            MdoWsInsert(Scan, ChildRelative, Depth);
            continue;
        }
        if ( Depth + 1u >= MDO_WS_DEPTH_MAX ) {
            Scan->Truncated = true;
            continue;
        }
        ChildAbsolute = xrtPathJoin(Absolute, Name);
        if ( ChildAbsolute == NULL ) {
            (void)xrtDirClose(Directory);
            return false;
        }
        if ( !MdoWsWalk(Scan, ChildAbsolute, ChildRelative, Depth + 1u) ) {
            xrtFree(ChildAbsolute);
            (void)xrtDirClose(Directory);
            return false;
        }
        xrtFree(ChildAbsolute);
        if ( Scan->Entries >= MDO_WS_ENTRIES_MAX ) break;
    }
    if ( Next == XDIR_NEXT_ERROR ) {
        Scan->Truncated = true;
        xrtClearError();
    }
    if ( !xrtDirClose(Directory) ) return false;
    return true;
}

bool MdoApiWorkspaceFilesRoute(MdoApiContext* Context)
{
    char ProjectId[MDO_PROJECT_ID_CAPACITY];
    char SessionId[MDO_SESSION_ID_CAPACITY];
    MdoSession* Session;
    MdoSessionInfo Info;
    MdoWsScan* Scan;
    xwork_error Error;
    xvalue* Data;
    xvalue* Items;
    size_t Index;
    bool Ok;
    if ( Context->ParamCount != 2u ||
         !MdoWsId(Context->Params[0], ProjectId, sizeof(ProjectId)) ||
         !MdoWsId(Context->Params[1], SessionId, sizeof(SessionId)) )
        return MdoApiReplyError(Context, 404u, "session_not_found",
            "The requested session does not exist", NULL);
    Scan = (MdoWsScan*)xrtCalloc(1u, sizeof(*Scan));
    if ( Scan == NULL ) goto unavailable;
    if ( !MdoWsQuery(Context->Target.Query, Scan->Query) ) {
        xrtFree(Scan);
        return MdoApiReplyError(Context, 400u, "invalid_query",
            "Use a nonempty UTF-8 q query of at most 128 bytes", NULL);
    }
    memset(&Error, 0, sizeof(Error));
    Session = MdoSessionLoad(ProjectId, SessionId, &Error);
    if ( Session == NULL ) {
        xrtFree(Scan);
        return MdoApiReplyError(Context, 404u, "session_not_found",
            "The requested session does not exist", NULL);
    }
    memset(&Info, 0, sizeof(Info));
    Info.Size = sizeof(Info);
    Ok = MdoSessionGetInfo(Session, &Info);
    MdoSessionRelease(Session);
    if ( !Ok || Info.WorkspaceRoot[0] == '\0' ) {
        xrtFree(Scan);
        goto unavailable;
    }
    if ( !MdoWsWalk(Scan, Info.WorkspaceRoot, "", 0u) ) {
        xrtFree(Scan);
        goto unavailable;
    }
    Data = xrtValueObject();
    Items = xrtValueArray();
    Ok = Data != NULL && Items != NULL &&
        MdoApiValueSetString(Data, "query", Scan->Query) &&
        MdoApiValueSetBool(Data, "truncated",
            Scan->Truncated || Scan->Matched > Scan->Count) &&
        MdoApiValueSetUInt(Data, "scanned", Scan->Entries);
    for ( Index = 0u; Ok && Index < Scan->Count; ++Index )
        Ok = MdoApiValueAppendString(Items, Scan->Matches[Index].Path);
    if ( Ok ) Ok = MdoApiValueSetTake(Data, "items", &Items);
    xrtValueRelease(Items);
    xrtFree(Scan);
    if ( !Ok ) { xrtValueRelease(Data); goto unavailable; }
    return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
unavailable:
    return MdoApiReplyError(Context, 503u, "workspace_files_unavailable",
        "Workspace files could not be listed", NULL);
}
