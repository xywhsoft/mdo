#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../memory/internal.h"
#include "internal.h"

#define MDO_MIGRATION_MEMORY_MAX_ENTRIES 256u
#define MDO_MIGRATION_MEMORY_CONTENT_LIMIT (16u * 1024u)

static bool MdoMigrationMemoryTake(xvalue* Object, const char* Key,
    xvalue* Value)
{
    bool Ok = Object != NULL && Value != NULL &&
        xrtValueObjectSetTake(Object, xrtStrView(Key), &Value);
    xrtValueRelease(Value);
    return Ok;
}

static bool MdoMigrationMemoryTakeOwned(xvalue* Object, const char* Key,
    xvalue** Value)
{
    return Object != NULL && Value != NULL && *Value != NULL &&
        xrtValueObjectSetTake(Object, xrtStrView(Key), Value);
}

static bool MdoMigrationMemoryString(xvalue* Object, const char* Key,
    const char* Text)
{
    return MdoMigrationMemoryTake(Object, Key,
        xrtValueString(xrtStrView(Text != NULL ? Text : "")));
}

static bool MdoMigrationMemoryAppendString(xvalue* Array, const char* Text)
{
    xvalue* Value = xrtValueString(xrtStrView(Text));
    bool Ok = Value != NULL && xrtValueArrayAppendTake(Array, &Value);
    xrtValueRelease(Value);
    return Ok;
}

static bool MdoMigrationMemoryRelative(const char* Path,
    const char* Project, const char** Relative)
{
    size_t PrefixSize;
    if ( Project == NULL ) {
        if ( strncmp(Path, "memory/", 7u) != 0 ) return false;
        *Relative = Path + 7u;
        return (*Relative)[0] != '\0';
    }
    PrefixSize = strlen(Project);
    if ( strncmp(Path, "projects/", 9u) != 0 ||
         strncmp(Path + 9u, Project, PrefixSize) != 0 ||
         strncmp(Path + 9u + PrefixSize, "/memory/", 8u) != 0 )
        return false;
    *Relative = Path + 9u + PrefixSize + 8u;
    return (*Relative)[0] != '\0';
}

static bool MdoMigrationMemoryId(const char* Relative, char* Id,
    size_t Capacity)
{
    size_t Size = strlen(Relative);
    char* Stem;
    bool Ok;
    if ( Size <= 3u || strcmp(Relative + Size - 3u, ".md") != 0 )
        return false;
    Stem = xrtStrDupN(Relative, Size - 3u);
    if ( Stem == NULL ) return false;
    Ok = MdoMigrationIdentifier(Stem, "memory", Id, Capacity);
    xrtFree(Stem);
    return Ok;
}

static bool MdoMigrationMemoryTitle(const char* Relative, const char* Data,
    size_t Size, char* Title, size_t Capacity)
{
    const char* Cursor = Data;
    const char* End = Data + Size;
    size_t RelativeSize = strlen(Relative);
    while ( Cursor < End ) {
        const char* LineEnd = memchr(Cursor, '\n', (size_t)(End - Cursor));
        size_t LineSize = LineEnd != NULL ? (size_t)(LineEnd - Cursor) :
            (size_t)(End - Cursor);
        while ( LineSize != 0u && Cursor[LineSize - 1u] == '\r' ) --LineSize;
        if ( LineSize > 2u && Cursor[0] == '#' && Cursor[1] == ' ' ) {
            size_t TitleSize = LineSize - 2u;
            if ( TitleSize < Capacity &&
                 xrtUtf8Valid(xrtStrViewN(Cursor + 2u, TitleSize), NULL) ) {
                memcpy(Title, Cursor + 2u, TitleSize);
                Title[TitleSize] = '\0';
                return true;
            }
        }
        if ( LineEnd == NULL ) break;
        Cursor = LineEnd + 1u;
    }
    if ( RelativeSize > 3u ) RelativeSize -= 3u;
    if ( RelativeSize >= Capacity ) return false;
    memcpy(Title, Relative, RelativeSize);
    Title[RelativeSize] = '\0';
    return xrtUtf8Valid(xrtStrViewN(Title, RelativeSize), NULL);
}

static xvalue* MdoMigrationMemoryEntry(const char* Id, const char* Title,
    const char* Content, int64 Now)
{
    xvalue* Entry = xrtValueObject();
    xvalue* Tags = xrtValueArray();
    if ( Entry == NULL || Tags == NULL ||
         !MdoMigrationMemoryAppendString(Tags, "legacy-import") ||
         !MdoMigrationMemoryString(Entry, "id", Id) ||
         !MdoMigrationMemoryString(Entry, "title", Title) ||
         !MdoMigrationMemoryString(Entry, "content", Content) ||
         !MdoMigrationMemoryTakeOwned(Entry, "tags", &Tags) ||
         !MdoMigrationMemoryTake(Entry, "pinned", xrtValueBool(false)) ||
         !MdoMigrationMemoryTake(Entry, "revision", xrtValueUInt(1u)) ||
         !MdoMigrationMemoryTake(Entry, "created_at_us",
            xrtValueInt(Now)) ||
         !MdoMigrationMemoryTake(Entry, "updated_at_us",
            xrtValueInt(Now)) ) {
        xrtValueRelease(Tags);
        xrtValueRelease(Entry);
        return NULL;
    }
    return Entry;
}

static char* MdoMigrationMemoryDocument(xvalue* Entries, int64 Now,
    size_t* Size)
{
    xvalue* Root = xrtValueObject();
    xvalue* Copy = xrtValueDeepClone(Entries);
    char* Json = NULL;
    if ( Root == NULL || Copy == NULL ||
         !MdoMigrationMemoryTake(Root, "schema_version", xrtValueUInt(1u)) ||
         !MdoMigrationMemoryTake(Root, "revision", xrtValueUInt(1u)) ||
         !MdoMigrationMemoryTake(Root, "updated_at_us", xrtValueInt(Now)) ||
         !MdoMigrationMemoryTakeOwned(Root, "entries", &Copy) ) goto done;
    Json = xrtJsonStringify(Root, true, Size);
done:
    xrtValueRelease(Copy);
    xrtValueRelease(Root);
    return Json;
}

static bool MdoMigrationMemoryCandidateValid(MdoMemoryScope Scope,
    const char* ProjectId, const xvalue* Entry, int64 Now)
{
    xvalue* Entries = xrtValueArray();
    xvalue* Copy = xrtValueDeepClone(Entry);
    MdoMemorySnapshot* Snapshot = NULL;
    char* Json = NULL;
    size_t Size = 0u;
    bool Ok = Entries != NULL && Copy != NULL &&
        xrtValueArrayAppendTake(Entries, &Copy);
    xrtValueRelease(Copy);
    if ( Ok ) Json = MdoMigrationMemoryDocument(Entries, Now, &Size);
    if ( Json != NULL ) Snapshot = MdoMemoryInternalParse(Scope,
        ProjectId, xrtStrViewN(Json, Size));
    Ok = Snapshot != NULL;
    MdoMemorySnapshotRelease(Snapshot);
    xrtFree(Json);
    xrtValueRelease(Entries);
    return Ok;
}

static bool MdoMigrationMemoryStore(MdoMigrationContext* Context,
    const char* OldProject, const MdoMigrationProjectMap* Project,
    xwork_error* Error)
{
    MdoMemoryScope Scope = Project == NULL ? MDO_MEMORY_GLOBAL :
        MDO_MEMORY_PROJECT;
    const char* ProjectId = Project != NULL ? Project->NewId : NULL;
    xvalue* Entries = xrtValueArray();
    int64 Now = xrtNow();
    size_t i;
    size_t Count = 0u;
    bool Ok = false;
    if ( Entries == NULL ) goto memory;
    for ( i = 0u; i < Context->Scan.Count; ++i ) {
        MdoMigrationFile* File = &Context->Scan.Files[i];
        const char* Relative;
        char Id[MDO_MEMORY_ID_CAPACITY];
        char Title[MDO_MEMORY_TITLE_CAPACITY];
        char* Content = NULL;
        size_t Size = 0u;
        xvalue* Entry;
        if ( File->Kind != MDO_MIGRATION_FILE_MEMORY ||
             !MdoMigrationMemoryRelative(File->Path, OldProject,
                &Relative) ) continue;
        if ( Count >= MDO_MIGRATION_MEMORY_MAX_ENTRIES ||
             File->Size == 0u ||
             File->Size > MDO_MIGRATION_MEMORY_CONTENT_LIMIT ||
             !MdoMigrationMemoryId(Relative, Id, sizeof(Id)) ||
             !MdoMigrationRead(Context->SourceRoot, File,
                MDO_MIGRATION_MEMORY_CONTENT_LIMIT, &Content, &Size, Error) ||
             !xrtUtf8Valid(xrtStrViewN(Content, Size), NULL) ||
             !MdoMigrationMemoryTitle(Relative, Content, Size, Title,
                sizeof(Title)) ) {
            xrtFree(Content);
            ++Context->UnsupportedMemory;
            ++Context->Result->SkippedItems;
            if ( Error != NULL && Error->eCode != XWORK_ERROR_NONE )
                goto done;
            continue;
        }
        Entry = MdoMigrationMemoryEntry(Id, Title, Content, Now);
        if ( Entry == NULL ) {
            xrtFree(Content);
            goto memory;
        }
        if ( !MdoMigrationMemoryCandidateValid(Scope, ProjectId, Entry,
                Now) ) {
            xrtValueRelease(Entry);
            xrtFree(Content);
            ++Context->UnsupportedMemory;
            ++Context->Result->SkippedItems;
            continue;
        }
        if ( !xrtValueArrayAppendTake(Entries, &Entry) ) {
            xrtValueRelease(Entry);
            xrtFree(Content);
            goto memory;
        }
        xrtFree(Content);
        ++Count;
    }
    if ( Count != 0u ) {
        MdoMemorySnapshot* Snapshot;
        char* Json;
        size_t Size = 0u;
        char Path[256];
        int Written;
        Json = MdoMigrationMemoryDocument(Entries, Now, &Size);
        Snapshot = Json != NULL ? MdoMemoryInternalParse(Scope, ProjectId,
            xrtStrViewN(Json, Size)) : NULL;
        if ( Snapshot == NULL ) {
            MdoMemorySnapshotRelease(Snapshot);
            xrtFree(Json);
            goto invalid;
        }
        MdoMemorySnapshotRelease(Snapshot);
        Written = Project == NULL ? snprintf(Path, sizeof(Path),
            "memory/global.json") : snprintf(Path, sizeof(Path),
            "memory/projects/%s.json", Project->NewId);
        if ( Written <= 0 || (size_t)Written >= sizeof(Path) ||
             !MdoMigrationStageWrite(Context, Path, Json, Size, 0600u,
                Error) ) {
            xrtFree(Json);
            goto done;
        }
        xrtFree(Json);
        Context->Result->ImportedMemoryEntries += Count;
    }
    Ok = true;
    goto done;
invalid:
    MdoMigrationError(Error, XWORK_ERROR_INVALID_ARGUMENT,
        "migrated memory failed current schema validation");
    goto done;
memory:
    MdoMigrationError(Error, XWORK_ERROR_OUT_OF_MEMORY,
        "cannot build migrated memory store");
done:
    xrtValueRelease(Entries);
    return Ok;
}

bool MdoMigrationConvertMemory(MdoMigrationContext* Context,
    xwork_error* Error)
{
    size_t i;
    if ( !MdoMigrationMemoryStore(Context, NULL, NULL, Error) ) return false;
    for ( i = 0u; i < Context->ProjectCount; ++i )
        if ( !MdoMigrationMemoryStore(Context, Context->Projects[i].OldId,
                &Context->Projects[i], Error) ) return false;
    return true;
}
