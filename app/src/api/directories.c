#include <stdlib.h>
#include <string.h>
#include "internal.h"

#define MDO_DIRECTORY_PATH_MAX 2048u
#define MDO_DIRECTORY_NAME_MAX 1024u
#define MDO_DIRECTORY_PAGE_MAX 256u
#define MDO_DIRECTORY_SCAN_MAX 1000000u

typedef struct MdoDirectoryQuery {
    char Path[MDO_DIRECTORY_PATH_MAX + 1u];
    char Filter[MDO_DIRECTORY_NAME_MAX + 1u];
    char Prefix[MDO_DIRECTORY_NAME_MAX + 1u];
    char After[MDO_DIRECTORY_NAME_MAX + 1u];
    size_t Limit;
} MdoDirectoryQuery;

static bool MdoDirectoryText(xstrview Text, size_t Maximum)
{
    if (Text.Size > Maximum || memchr(Text.Data, 0, Text.Size) || !xrtUtf8Valid(Text, NULL)) return false;
    for (size_t I = 0; I < Text.Size; ++I)
        if ((unsigned char)Text.Data[I] < 0x20u || Text.Data[I] == 0x7f) return false;
    return true;
}

static bool MdoDirectoryParseQuery(xstrview Raw, MdoDirectoryQuery* Query)
{
    size_t Offset = 0; unsigned Seen = 0;
    memset(Query, 0, sizeof(*Query)); Query->Limit = 128u;
    if (Raw.Size > 16384u) return false;
    while (Offset < Raw.Size) {
        size_t End = Offset, Equal, Size = 0, Maximum; unsigned Bit;
        char *Destination, Number[4];
        while (End < Raw.Size && Raw.Data[End] != '&') ++End;
        Equal = Offset; while (Equal < End && Raw.Data[Equal] != '=') ++Equal;
        if (Equal == End) return false;
        xstrview Key = xrtStrViewN(Raw.Data + Offset, Equal - Offset);
        if (xrtStrEqual(Key, XRT_STR_LITERAL("path"))) { Bit = 1; Destination = Query->Path; Maximum = MDO_DIRECTORY_PATH_MAX; }
        else if (xrtStrEqual(Key, XRT_STR_LITERAL("filter"))) { Bit = 2; Destination = Query->Filter; Maximum = MDO_DIRECTORY_NAME_MAX; }
        else if (xrtStrEqual(Key, XRT_STR_LITERAL("prefix"))) { Bit = 4; Destination = Query->Prefix; Maximum = MDO_DIRECTORY_NAME_MAX; }
        else if (xrtStrEqual(Key, XRT_STR_LITERAL("after"))) { Bit = 8; Destination = Query->After; Maximum = MDO_DIRECTORY_NAME_MAX; }
        else if (xrtStrEqual(Key, XRT_STR_LITERAL("limit"))) { Bit = 16; Destination = Number; Maximum = 3; }
        else return false;
        if ((Seen & Bit) || !xrtPercentDecode(xrtStrViewN(Raw.Data + Equal + 1, End - Equal - 1),
            Destination, Maximum, &Size) || !MdoDirectoryText(xrtStrViewN(Destination, Size), Maximum)) return false;
        Seen |= Bit; Destination[Size] = 0;
        if (Bit == 16) {
            size_t Limit = 0;
            if (!Size) return false;
            for (size_t I = 0; I < Size; ++I) {
                if (Number[I] < '0' || Number[I] > '9') return false;
                Limit = Limit * 10 + (size_t)(Number[I] - '0');
            }
            if (!Limit || Limit > MDO_DIRECTORY_PAGE_MAX) return false;
            Query->Limit = Limit;
        }
        Offset = End + 1;
        if (End < Raw.Size && Offset == Raw.Size) return false;
    }
    return true;
}

static unsigned char MdoDirectoryFold(unsigned char Byte)
{ return Byte >= 'A' && Byte <= 'Z' ? (unsigned char)(Byte + 'a' - 'A') : Byte; }

/* This deterministic natural order is also the page cursor order. */
static int MdoDirectoryCompare(cstr A, cstr B)
{
    cstr StartA = A, StartB = B;
    while (*A && *B) {
        if (*A >= '0' && *A <= '9' && *B >= '0' && *B <= '9') {
            cstr EndA = A, EndB = B, ValueA = A, ValueB = B;
            while (*EndA >= '0' && *EndA <= '9') ++EndA;
            while (*EndB >= '0' && *EndB <= '9') ++EndB;
            while (ValueA < EndA && *ValueA == '0') ++ValueA;
            while (ValueB < EndB && *ValueB == '0') ++ValueB;
            if (EndA - ValueA != EndB - ValueB) return EndA - ValueA < EndB - ValueB ? -1 : 1;
            int Value = memcmp(ValueA, ValueB, (size_t)(EndA - ValueA));
            if (Value) return Value;
            A = EndA; B = EndB; continue;
        }
        unsigned char Left = MdoDirectoryFold((unsigned char)*A), Right = MdoDirectoryFold((unsigned char)*B);
        if (Left != Right) return Left < Right ? -1 : 1;
        ++A; ++B;
    }
    if (*A || *B) return *A ? 1 : -1;
    return strcmp(StartA, StartB);
}

static int MdoDirectorySort(const void* A, const void* B)
{ return MdoDirectoryCompare(*(cstr const*)A, *(cstr const*)B); }

static bool MdoDirectoryMatch(cstr Name, cstr Needle, bool Prefix)
{
    if (!Needle[0]) return true;
    for (size_t Start = 0; Name[Start]; ++Start) {
        size_t I = 0;
        while (Needle[I] && Name[Start + I] && MdoDirectoryFold((unsigned char)Name[Start + I]) ==
            MdoDirectoryFold((unsigned char)Needle[I])) ++I;
        if (!Needle[I]) return true;
        if (Prefix) break;
    }
    return false;
}

static void MdoDirectoryHeapDown(char** Heap, size_t Count, size_t At)
{
    for (;;) {
        size_t Largest = At, Left = At * 2 + 1, Right = Left + 1;
        if (Left < Count && MdoDirectoryCompare(Heap[Left], Heap[Largest]) > 0) Largest = Left;
        if (Right < Count && MdoDirectoryCompare(Heap[Right], Heap[Largest]) > 0) Largest = Right;
        if (Largest == At) return;
        char* Swap = Heap[At]; Heap[At] = Heap[Largest]; Heap[Largest] = Swap; At = Largest;
    }
}

static bool MdoDirectoryShortcut(xvalue* Items, cstr Kind, cstr Path)
{
    if (!Path || !Path[0] || strlen(Path) > MDO_DIRECTORY_PATH_MAX) return true;
    xvalue* Item = xrtValueObject();
    bool Ok = Item && MdoApiValueSetString(Item, "kind", Kind) &&
        MdoApiValueSetString(Item, "path", Path) && MdoApiValueAppendTake(Items, &Item);
    xrtValueRelease(Item); return Ok;
}

#include "directories_locations.inc.c"

static bool MdoDirectoryCreate(MdoApiContext* Context)
{
    MdoApiJsonBody Body = {0};
    MdoApiBodyStatus Status = MdoApiJsonBodyRead(Context, &Body);
    if (Status != MDO_API_BODY_OK) return MdoApiReplyBodyError(Context, Status);
    xstrview Parent = {0}, Name = {0};
    bool Valid = !Context->Target.Query.Size && xrtValueType(Body.Value) == XVALUE_OBJECT &&
        xrtValueCount(Body.Value) == 2 &&
        xrtValueGetString(xrtValueObjectGet(Body.Value, XRT_STR_LITERAL("path")), &Parent) &&
        xrtValueGetString(xrtValueObjectGet(Body.Value, XRT_STR_LITERAL("name")), &Name) &&
        Parent.Size && Name.Size && MdoDirectoryText(Parent, MDO_DIRECTORY_PATH_MAX) &&
        MdoDirectoryText(Name, 255u) && !memchr(Name.Data, '/', Name.Size) &&
        !xrtStrEqual(Name, XRT_STR_LITERAL(".")) && !xrtStrEqual(Name, XRT_STR_LITERAL(".."));
#if defined(_WIN32)
    if (Valid) {
        for (size_t I = 0; I < Name.Size; ++I) if (strchr("\\:*?\"<>|", Name.Data[I])) Valid = false;
        if (Name.Data[Name.Size - 1] == '.' || Name.Data[Name.Size - 1] == ' ') Valid = false;
        char Base[5] = {0}; size_t I = 0;
        while (I < Name.Size && I < 4 && Name.Data[I] != '.') { Base[I] = (char)MdoDirectoryFold((unsigned char)Name.Data[I]); ++I; }
        if (!strcmp(Base, "con") || !strcmp(Base, "prn") || !strcmp(Base, "aux") || !strcmp(Base, "nul") ||
            ((!strncmp(Base, "com", 3) || !strncmp(Base, "lpt", 3)) && Base[3] >= '1' && Base[3] <= '9' &&
                (Name.Size == 4 || Name.Data[4] == '.'))) Valid = false;
    }
#endif
    char *ParentText = Valid ? xrtStrDupView(Parent) : NULL, *NameText = Valid ? xrtStrDupView(Name) : NULL;
    MdoApiJsonBodyUnit(&Body);
    if (!Valid) return MdoApiReplyError(Context, 400, "directory_name_invalid", "Use one directory name, without separators or reserved names", NULL);
    char *Absolute = ParentText ? xrtPathReal(ParentText) : NULL;
    char *Child = Absolute && NameText ? xrtPathJoin(Absolute, NameText) : NULL;
    xfileinfo Info = {0};
    bool ParentOk = Absolute && xrtPathStat(Absolute, true, &Info) && Info.Type == XFILE_TYPE_DIRECTORY;
    bool Exists = Child && xrtPathStat(Child, false, &Info);
    bool Created = ParentOk && Child && strlen(Child) <= MDO_DIRECTORY_PATH_MAX && !Exists && xrtDirCreate(Child);
    xvalue* Data = Created ? xrtValueObject() : NULL;
    bool Ok = Data && MdoApiValueSetString(Data, "path", Child) && MdoApiValueSetString(Data, "name", NameText);
    xrtFree(Child); xrtFree(Absolute); xrtFree(ParentText); xrtFree(NameText);
    if (Ok) return MdoApiReplySuccessTake(Context, 201, Data, NULL);
    xrtValueRelease(Data);
    return MdoApiReplyError(Context, Exists ? 409 : 422, Exists ? "directory_exists" : "directory_create_failed",
        Exists ? "A file or directory already exists with that name" : "The directory could not be created; check the parent and permissions", NULL);
}

bool MdoApiDirectoriesRoute(MdoApiContext* Context)
{
    if (Context->Request->head->MethodCode == XHTTP_METHOD_POST) return MdoDirectoryCreate(Context);
    MdoDirectoryQuery Query;
    if (!MdoDirectoryParseQuery(Context->Target.Query, &Query))
        return MdoApiReplyError(Context, 400, "invalid_directory_path", "Use UTF-8 path, filter, prefix, after and limit fields without duplicates", NULL);
    char *Path = xrtPathAbs(Query.Path), *Parent = NULL;
    char* Heap[MDO_DIRECTORY_PAGE_MAX + 1u] = {0};
    size_t Count = 0, Scanned = 0, Total = 0, Skipped = 0;
    xdir Directory = Path && strlen(Path) <= MDO_DIRECTORY_PATH_MAX ? xrtDirOpen(Path, 0) : NULL;
    xdirentry Entry; xdirnext Next = XDIR_NEXT_END;
    bool Ok = false, Limited = false;
    xvalue *Items = NULL, *Data = NULL;
    double Start = xrtTimer();
    if (!Directory) goto done;
    while ((Next = xrtDirNext(Directory, &Entry)) == XDIR_NEXT_ITEM) {
        if (++Scanned > MDO_DIRECTORY_SCAN_MAX || (!(Scanned % 256) && xrtTimer() - Start > 5.0)) { Limited = true; goto done; }
        if (!(Entry.Flags & XDIR_ENTRY_UTF8) || !Entry.Name.Size ||
            !MdoDirectoryText(Entry.Name, MDO_DIRECTORY_NAME_MAX) || strlen(Path) + Entry.Name.Size + 1 > MDO_DIRECTORY_PATH_MAX) { ++Skipped; continue; }
        char Name[MDO_DIRECTORY_NAME_MAX + 1u]; memcpy(Name, Entry.Name.Data, Entry.Name.Size); Name[Entry.Name.Size] = 0;
        if (Entry.Info.Type == XFILE_TYPE_NONE || Entry.Info.Type == XFILE_TYPE_LINK) {
            char* Child = xrtPathJoin(Path, Name); xfileinfo Info;
            if (!Child) goto done;
            bool IsDir = xrtPathStat(Child, true, &Info) && Info.Type == XFILE_TYPE_DIRECTORY;
            xrtFree(Child); xrtClearError(); if (!IsDir) continue;
        } else if (Entry.Info.Type != XFILE_TYPE_DIRECTORY) continue;
        if (!MdoDirectoryMatch(Name, Query.Filter, false) || !MdoDirectoryMatch(Name, Query.Prefix, true)) continue;
        ++Total;
        if (Query.After[0] && MdoDirectoryCompare(Name, Query.After) <= 0) continue;
        /* A page+1 max-heap bounds memory independently of directory size. */
        if (Count == Query.Limit + 1) {
            if (MdoDirectoryCompare(Name, Heap[0]) >= 0) continue;
            char* Copy = xrtStrDup(Name); if (!Copy) goto done;
            xrtFree(Heap[0]); Heap[0] = Copy; MdoDirectoryHeapDown(Heap, Count, 0);
        } else {
            char* Copy = xrtStrDup(Name); if (!Copy) goto done;
            size_t At = Count++; Heap[At] = Copy;
            while (At && MdoDirectoryCompare(Heap[At], Heap[(At - 1) / 2]) > 0) {
                size_t Up = (At - 1) / 2; char* Swap = Heap[At]; Heap[At] = Heap[Up]; Heap[Up] = Swap; At = Up;
            }
        }
    }
    if (Next == XDIR_NEXT_ERROR) goto done;
    if (!xrtDirClose(Directory)) { Directory = NULL; goto done; } Directory = NULL;
    qsort(Heap, Count, sizeof(*Heap), MdoDirectorySort);
    Data = xrtValueObject(); Items = xrtValueArray(); Parent = xrtPathParent(Path);
    if (!Data || !Items || !Parent) goto done;
    for (size_t I = 0; I < Count && I < Query.Limit; ++I) {
        xvalue* Name = xrtValueString(xrtStrView(Heap[I]));
        if (!Name || !MdoApiValueAppendTake(Items, &Name)) { xrtValueRelease(Name); goto done; }
    }
    char Separator[2] = {xrtPathSep(), 0};
    Ok = MdoApiValueSetString(Data, "path", Path) && MdoApiValueSetString(Data, "parent", xrtPathIsRoot(Path) ? "" : Parent) &&
        MdoApiValueSetString(Data, "separator", Separator) && MdoApiValueSetString(Data, "filter", Query.Filter) &&
        MdoApiValueSetUInt(Data, "total", Total) && MdoApiValueSetUInt(Data, "skipped", Skipped) &&
        MdoApiValueSetBool(Data, "truncated", false) && MdoApiValueSetBool(Data, "has_more", Count > Query.Limit) &&
        MdoApiValueSetString(Data, "next_cursor", Count > Query.Limit ? Heap[Query.Limit - 1] : "") &&
        MdoApiValueSetTake(Data, "directories", &Items);
done:
    if (Directory) (void)xrtDirClose(Directory);
    for (size_t I = 0; I < Count; ++I) xrtFree(Heap[I]);
    xrtFree(Path); xrtFree(Parent); xrtValueRelease(Items);
    if (Ok) return MdoApiReplySuccessTake(Context, 200, Data, NULL);
    xrtValueRelease(Data);
    return MdoApiReplyError(Context, 422, Limited ? "directory_scan_limit" : "directory_unavailable",
        Limited ? "The directory scan exceeded the time or entry budget" : "The directory could not be opened; check its path and access permissions", NULL);
}
