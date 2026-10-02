#include <string.h>

#include "internal.h"

#define MDO_DIRECTORY_PATH_MAX 2048u
#define MDO_DIRECTORY_ITEMS_MAX 128u
#define MDO_DIRECTORY_SCAN_MAX 4096u

/* This picker browses the machine running mdo, including when the page is
 * opened on a phone. It never reads files, walks descendants or changes cwd. */
static bool MdoDirectoryQuery(xstrview Raw,
    char Path[MDO_DIRECTORY_PATH_MAX + 1u])
{
    size_t Size = 0u;
    size_t Index;
    Path[0] = '\0';
    if ( Raw.Size == 0u ) return true;
    if ( Raw.Size < 5u || Raw.Size > 5u + MDO_DIRECTORY_PATH_MAX * 3u ||
         memcmp(Raw.Data, "path=", 5u) != 0 ||
         memchr(Raw.Data + 5u, '&', Raw.Size - 5u) != NULL ||
         !xrtPercentDecode(xrtStrViewN(Raw.Data + 5u, Raw.Size - 5u),
             Path, MDO_DIRECTORY_PATH_MAX, &Size) ||
         memchr(Path, 0, Size) != NULL ||
         !xrtUtf8Valid(xrtStrViewN(Path, Size), NULL) ) return false;
    for ( Index = 0u; Index < Size; ++Index )
        if ( (unsigned char)Path[Index] < 0x20u || Path[Index] == 0x7f )
            return false;
    Path[Size] = '\0';
    return true;
}

static bool MdoDirectoryShortcut(xvalue* Items, cstr Kind, cstr Path)
{
    xvalue* Item;
    bool Ok;
    if ( Path == NULL || Path[0] == '\0' ||
         strlen(Path) > MDO_DIRECTORY_PATH_MAX ) return true;
    Item = xrtValueObject();
    Ok = Item != NULL && MdoApiValueSetString(Item, "kind", Kind) &&
        MdoApiValueSetString(Item, "path", Path) &&
        MdoApiValueAppendTake(Items, &Item);
    xrtValueRelease(Item);
    return Ok;
}

bool MdoApiDirectoriesRoute(MdoApiContext* Context)
{
    char Query[MDO_DIRECTORY_PATH_MAX + 1u];
    char Separator[2] = {0, 0};
    char* Path = NULL;
    char* Parent = NULL;
    char* Home = NULL;
    char* Cwd = NULL;
    xdir Directory = NULL;
    xdirentry Entry;
    xdirnext Next = XDIR_NEXT_END;
    xvalue* Data = NULL;
    xvalue* Items = NULL;
    xvalue* Shortcuts = NULL;
    size_t Count = 0u;
    size_t Scanned = 0u;
    bool Truncated = false;
    bool Ok = false;

    if ( !MdoDirectoryQuery(Context->Target.Query, Query) )
        return MdoApiReplyError(Context, 400u, "invalid_directory_path",
            "Use one UTF-8 path of at most 2048 bytes without control characters", NULL);
    Path = xrtPathAbs(Query);
    if ( Path == NULL || strlen(Path) > MDO_DIRECTORY_PATH_MAX ) goto done;
    /* Do not stat every child: one protected or broken child must not make a
     * readable parent (notably a Windows drive root) impossible to browse. */
    Directory = xrtDirOpen(Path, 0u);
    if ( Directory == NULL ) goto done;
    Items = xrtValueArray();
    if ( Items == NULL ) goto done;
    memset(&Entry, 0, sizeof(Entry));
    while ( (Next = xrtDirNext(Directory, &Entry)) == XDIR_NEXT_ITEM ) {
        size_t Index;
        bool Valid = true;
        if ( ++Scanned > MDO_DIRECTORY_SCAN_MAX ) {
            Truncated = true;
            break;
        }
        if ( (Entry.Flags & XDIR_ENTRY_UTF8) == 0u || Entry.Name.Size == 0u ||
             Entry.Name.Size > 1024u ||
             strlen(Path) + Entry.Name.Size + 1u > MDO_DIRECTORY_PATH_MAX ) {
            Truncated = true;
            continue;
        }
        for ( Index = 0u; Index < Entry.Name.Size; ++Index ) {
            unsigned char Byte = (unsigned char)Entry.Name.Data[Index];
            if ( Byte < 0x20u || Byte == 0x7fu ) { Valid = false; break; }
        }
        if ( !Valid ) { Truncated = true; continue; }
        if ( Entry.Info.Type == XFILE_TYPE_NONE ||
             Entry.Info.Type == XFILE_TYPE_LINK ) {
            char Name[1025];
            char* Child;
            xfileinfo Info;
            memcpy(Name, Entry.Name.Data, Entry.Name.Size);
            Name[Entry.Name.Size] = '\0';
            Child = xrtPathJoin(Path, Name);
            if ( Child == NULL ) goto done;
            if ( !xrtPathStat(Child, true, &Info) ) {
                xrtFree(Child); xrtClearError(); Truncated = true; continue;
            }
            xrtFree(Child);
            if ( Info.Type != XFILE_TYPE_DIRECTORY ) continue;
        } else if ( Entry.Info.Type != XFILE_TYPE_DIRECTORY ) continue;
        if ( Count >= MDO_DIRECTORY_ITEMS_MAX ) { Truncated = true; break; }
        /* Names only keep the bounded response small even for long paths. */
        {
            xvalue* Name = xrtValueString(Entry.Name);
            if ( Name == NULL || !MdoApiValueAppendTake(Items, &Name) ) {
                xrtValueRelease(Name);
                goto done;
            }
        }
        ++Count;
    }
    if ( Next == XDIR_NEXT_ERROR ) goto done;
    if ( !xrtDirClose(Directory) ) { Directory = NULL; goto done; }
    Directory = NULL;
    Parent = xrtPathParent(Path);
    Cwd = xrtPathCwd();
    Home = xrtPathHome();
    Data = xrtValueObject();
    Shortcuts = xrtValueArray();
    Separator[0] = xrtPathSep();
    Ok = Parent != NULL && Data != NULL && Shortcuts != NULL &&
        MdoApiValueSetString(Data, "path", Path) &&
        MdoApiValueSetString(Data, "parent", xrtPathIsRoot(Path) ? "" : Parent) &&
        MdoApiValueSetString(Data, "separator", Separator) &&
        MdoApiValueSetBool(Data, "truncated", Truncated) &&
        MdoDirectoryShortcut(Shortcuts, "cwd", Cwd) &&
        MdoDirectoryShortcut(Shortcuts, "home", Home) &&
        MdoApiValueSetTake(Data, "directories", &Items) &&
        MdoApiValueSetTake(Data, "shortcuts", &Shortcuts);
done:
    if ( Directory != NULL ) (void)xrtDirClose(Directory);
    xrtFree(Path); xrtFree(Parent); xrtFree(Cwd); xrtFree(Home);
    xrtValueRelease(Items); xrtValueRelease(Shortcuts);
    if ( Ok ) return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
    xrtValueRelease(Data);
    return MdoApiReplyError(Context, 422u, "directory_unavailable",
        "The directory could not be opened; check its path and access permissions", NULL);
}
