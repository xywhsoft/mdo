#include <stdio.h>
#include <string.h>

#include "internal.h"
#include "../../include/mdo/home.h"
#include "../memory/internal.h"

#define MDO_API_MEMORY_CONTENT_CAPACITY (16u * 1024u + 1u)
#define MDO_API_MEMORY_TAG_LIMIT 16u

static bool MdoApiMemoryPath(MdoApiContext* Context, bool Entry,
    MdoMemoryScope* Scope, char Project[MDO_MEMORY_PROJECT_CAPACITY],
    char Id[MDO_MEMORY_ID_CAPACITY])
{
    static const char Global[] = "/api/v1/memory/global";
    bool IsGlobal = Context->Target.Path.Size >= sizeof(Global) - 1u &&
        memcmp(Context->Target.Path.Data, Global, sizeof(Global) - 1u) == 0;
    size_t Expected = (IsGlobal ? 0u : 1u) + (Entry ? 1u : 0u);
    xstrview Part;
    if ( Context->ParamCount != Expected ) return false;
    *Scope = IsGlobal ? MDO_MEMORY_GLOBAL : MDO_MEMORY_PROJECT;
    Project[0] = '\0';
    Id[0] = '\0';
    if ( !IsGlobal ) {
        Part = Context->Params[0];
        if ( Part.Size == 0u || Part.Size >= MDO_MEMORY_PROJECT_CAPACITY )
            return false;
        memcpy(Project, Part.Data, Part.Size);
        Project[Part.Size] = '\0';
        if ( !MdoMemoryInternalId(Project, MDO_MEMORY_PROJECT_CAPACITY) )
            return false;
    }
    if ( Entry ) {
        Part = Context->Params[Expected - 1u];
        if ( Part.Size == 0u || Part.Size >= MDO_MEMORY_ID_CAPACITY )
            return false;
        memcpy(Id, Part.Data, Part.Size);
        Id[Part.Size] = '\0';
        if ( !MdoMemoryInternalId(Id, MDO_MEMORY_ID_CAPACITY) )
            return false;
    }
    return true;
}

static void MdoApiMemoryTag(char Tag[64], uint64 Revision)
{
    (void)snprintf(Tag, 64u, "\"mdo-memory-%llu\"",
        (unsigned long long)Revision);
}

/* Store-wide ETags protect both edits and removals, including writes by agents. */
static int MdoApiMemoryMatch(MdoApiContext* Context, uint64 Revision)
{
    const xhttpfield* Field = NULL;
    xhttpnext Next = xrtHttpFieldGetUnique(Context->Request->head->Fields,
        Context->Request->head->FieldCount, XRT_STR_LITERAL("If-Match"),
        &Field);
    char Tag[64];
    xstrview Value;
    if ( Next == XHTTP_NEXT_END ) return 428;
    if ( Next != XHTTP_NEXT_ITEM || Field == NULL ) return 400;
    MdoApiMemoryTag(Tag, Revision);
    Value = xrtStrTrim(Field->Value);
    return Value.Size == strlen(Tag) &&
        memcmp(Value.Data, Tag, Value.Size) == 0 ? 0 : 412;
}

static bool MdoApiMemoryPreconditionError(MdoApiContext* Context,
    int Status)
{
    if ( Status == 428 ) return MdoApiReplyError(Context, 428u,
        "precondition_required", "If-Match must contain the current memory ETag", NULL);
    if ( Status == 400 ) return MdoApiReplyError(Context, 400u,
        "invalid_precondition", "If-Match must contain one memory ETag", NULL);
    return MdoApiReplyError(Context, 412u, "revision_conflict",
        "Memory changed; reload before saving", NULL);
}

static bool MdoApiMemoryFailure(MdoApiContext* Context,
    const xwork_error* Error)
{
    if ( Error->eCode == XWORK_ERROR_CONTEXT )
        return MdoApiMemoryPreconditionError(Context, 412);
    if ( Error->eCode == XWORK_ERROR_INVALID_ARGUMENT )
        return MdoApiReplyError(Context, 422u, "memory_invalid",
            "Memory fields are invalid or contain sensitive data", NULL);
    if ( Error->eCode == XWORK_ERROR_LIMIT )
        return MdoApiReplyError(Context, 409u, "memory_limit",
            "Memory has reached its limit", NULL);
    return MdoApiReplyError(Context, 503u, "memory_unavailable",
        "Memory storage is unavailable", NULL);
}

static bool MdoApiMemoryString(const xvalue* Object, cstr Key,
    char* Output, size_t Capacity)
{
    const xvalue* Value = xrtValueObjectGet(Object, xrtStrView(Key));
    xstrview Text;
    if ( Value == NULL || xrtValueType(Value) != XVALUE_STRING ||
         !xrtValueGetString(Value, &Text) || Text.Size >= Capacity ||
         memchr(Text.Data, 0, Text.Size) != NULL ) return false;
    memcpy(Output, Text.Data, Text.Size);
    Output[Text.Size] = '\0';
    return true;
}

static bool MdoApiMemoryInfoValue(xvalue* Item,
    const MdoMemoryEntryInfo* Info, bool IncludeContent)
{
    return MdoApiValueSetString(Item, "id", Info->Id) &&
        MdoApiValueSetString(Item, "title", Info->Title) &&
        MdoApiValueSetBool(Item, "pinned", Info->Pinned) &&
        MdoApiValueSetUInt(Item, "revision", Info->Revision) &&
        MdoApiValueSetInt(Item, "created_at", Info->CreatedAt) &&
        MdoApiValueSetInt(Item, "updated_at", Info->UpdatedAt) &&
        (!IncludeContent ||
            (MdoApiValueSetStrings(Item, "tags", Info->Tags, Info->TagCount) &&
             MdoApiValueSetString(Item, "content", Info->Content)));
}

static bool MdoApiMemoryRead(MdoApiContext* Context,
    const MdoMemorySnapshot* Snapshot, const char* Id)
{
    xvalue* Data = xrtValueObject();
    xvalue* Items = NULL;
    char Tag[64];
    size_t i;
    bool Ok = Data != NULL;
    MdoApiMemoryTag(Tag, MdoMemorySnapshotRevision(Snapshot));
    if ( Id == NULL ) {
        Items = xrtValueArray();
        Ok = Ok && Items != NULL;
        for ( i = 0u; Ok && i < MdoMemorySnapshotCount(Snapshot); ++i ) {
            MdoMemoryEntryInfo Info = { 0 };
            xvalue* Item = xrtValueObject();
            Info.Size = sizeof(Info);
            Ok = Item != NULL && MdoMemorySnapshotAt(Snapshot, i, &Info) &&
                MdoApiMemoryInfoValue(Item, &Info, false) &&
                MdoApiValueAppendTake(Items, &Item);
            xrtValueRelease(Item);
        }
        Ok = Ok && MdoApiValueSetUInt(Data, "revision",
            MdoMemorySnapshotRevision(Snapshot)) &&
            MdoApiValueSetUInt(Data, "generation",
            MdoMemorySnapshotGeneration(Snapshot)) &&
            MdoApiValueSetTake(Data, "items", &Items);
    } else {
        bool Found = false;
        for ( i = 0u; Ok && i < MdoMemorySnapshotCount(Snapshot); ++i ) {
            MdoMemoryEntryInfo Info = { 0 };
            Info.Size = sizeof(Info);
            if ( !MdoMemorySnapshotAt(Snapshot, i, &Info) ) { Ok = false; break; }
            if ( strcmp(Info.Id, Id) == 0 ) {
                Found = true;
                Ok = MdoApiMemoryInfoValue(Data, &Info, true);
                break;
            }
        }
        if ( Ok && !Found ) {
            xrtValueRelease(Data);
            return MdoApiReplyError(Context, 404u, "memory_not_found",
                "Memory entry was not found", NULL);
        }
    }
    xrtValueRelease(Items);
    if ( !Ok ) {
        xrtValueRelease(Data);
        return MdoApiReplyError(Context, 500u, "memory_response_unavailable",
            "Memory response could not be created", NULL);
    }
    return MdoApiReplySuccessTakeEntityTag(Context, 200u, Data, Tag);
}

static bool MdoApiMemoryWrite(MdoApiContext* Context,
    MdoMemoryScope Scope, const char* Project, uint64 Revision)
{
    MdoApiJsonBody Body;
    MdoApiBodyStatus Status = MdoApiJsonBodyRead(Context, &Body);
    MdoMemoryWriteOptions Options;
    xwork_error Error;
    char Id[MDO_MEMORY_ID_CAPACITY] = { 0 };
    char Title[MDO_MEMORY_TITLE_CAPACITY] = { 0 };
    char Content[MDO_API_MEMORY_CONTENT_CAPACITY] = { 0 };
    char Tags[MDO_API_MEMORY_TAG_LIMIT][MDO_MEMORY_TAG_CAPACITY];
    const char* TagPointers[MDO_API_MEMORY_TAG_LIMIT];
    const xvalue* TagValues;
    const xvalue* PinnedValue;
    xvalue* Data;
    char Tag[64];
    size_t i, TagCount = 0u;
    bool Pinned = false;
    bool Valid;
    if ( Status != MDO_API_BODY_OK ) return MdoApiReplyBodyError(Context, Status);
    TagValues = xrtValueObjectGet(Body.Value, XRT_STR_LITERAL("tags"));
    PinnedValue = xrtValueObjectGet(Body.Value, XRT_STR_LITERAL("pinned"));
    Valid = xrtValueType(Body.Value) == XVALUE_OBJECT &&
        xrtValueCount(Body.Value) == 5u &&
        MdoApiMemoryString(Body.Value, "id", Id, sizeof(Id)) &&
        MdoApiMemoryString(Body.Value, "title", Title, sizeof(Title)) &&
        MdoApiMemoryString(Body.Value, "content", Content, sizeof(Content)) &&
        TagValues != NULL && xrtValueType(TagValues) == XVALUE_ARRAY &&
        (TagCount = xrtValueCount(TagValues)) <= MDO_API_MEMORY_TAG_LIMIT &&
        PinnedValue != NULL && xrtValueType(PinnedValue) == XVALUE_BOOL &&
        xrtValueGetBool(PinnedValue, &Pinned);
    for ( i = 0u; Valid && i < TagCount; ++i ) {
        const xvalue* Value = xrtValueArrayGet(TagValues, i);
        xstrview Text;
        Valid = Value != NULL && xrtValueType(Value) == XVALUE_STRING &&
            xrtValueGetString(Value, &Text) &&
            Text.Size < sizeof(Tags[i]) &&
            memchr(Text.Data, 0, Text.Size) == NULL;
        if ( Valid ) {
            memcpy(Tags[i], Text.Data, Text.Size);
            Tags[i][Text.Size] = '\0';
            TagPointers[i] = Tags[i];
        }
    }
    if ( !Valid ) {
        MdoApiJsonBodyUnit(&Body);
        return MdoApiReplyError(Context, 422u, "memory_invalid",
            "Memory fields are invalid", NULL);
    }
    MdoMemoryWriteOptionsInit(&Options);
    Options.Scope = Scope;
    Options.ProjectId = Scope == MDO_MEMORY_PROJECT ? Project : NULL;
    Options.Id = Id;
    Options.Title = Title;
    Options.Content = Content;
    Options.Tags = TagPointers;
    Options.TagCount = TagCount;
    Options.Pinned = Pinned;
    Options.ExpectedRevision = Revision;
    Options.Actor = "web-ui";
    Valid = MdoMemoryUpsert(&Options, &Error);
    MdoApiJsonBodyUnit(&Body);
    if ( !Valid ) return MdoApiMemoryFailure(Context, &Error);
    Data = xrtValueObject();
    Valid = Data != NULL && MdoApiValueSetString(Data, "id", Id) &&
        MdoApiValueSetUInt(Data, "revision", Revision + 1u);
    if ( !Valid ) {
        xrtValueRelease(Data);
        return MdoApiReplyError(Context, 500u, "memory_response_unavailable",
            "Memory was saved but its response is unavailable", NULL);
    }
    MdoApiMemoryTag(Tag, Revision + 1u);
    return MdoApiReplySuccessTakeEntityTag(Context, 200u, Data, Tag);
}

static bool MdoApiMemoryRoute(MdoApiContext* Context, bool Entry)
{
    MdoMemoryScope Scope;
    char Project[MDO_MEMORY_PROJECT_CAPACITY];
    char Id[MDO_MEMORY_ID_CAPACITY];
    MdoMemorySnapshot* Snapshot;
    xwork_error Error;
    uint64 Revision;
    int Precondition;
    bool Result;
    if ( !MdoApiMemoryPath(Context, Entry, &Scope, Project, Id) )
        return MdoApiReplyError(Context, 400u, "invalid_memory_path",
            "Memory path is invalid", NULL);
    Snapshot = MdoMemorySnapshotCreate(Scope,
        Scope == MDO_MEMORY_PROJECT ? Project : NULL, &Error);
    if ( Snapshot == NULL ) return MdoApiMemoryFailure(Context, &Error);
    if ( Context->Request->head->MethodCode == XHTTP_METHOD_GET ||
         Context->Request->head->MethodCode == XHTTP_METHOD_HEAD ) {
        Result = MdoApiMemoryRead(Context, Snapshot, Entry ? Id : NULL);
        MdoMemorySnapshotRelease(Snapshot);
        return Result;
    }
    Revision = MdoMemorySnapshotRevision(Snapshot);
    MdoMemorySnapshotRelease(Snapshot);
    Precondition = MdoApiMemoryMatch(Context, Revision);
    if ( Precondition != 0 )
        return MdoApiMemoryPreconditionError(Context, Precondition);
    if ( !Entry ) return MdoApiMemoryWrite(Context, Scope, Project, Revision);
    {
        MdoMemoryRemoveOptions Options;
        xvalue* Data;
        char Tag[64];
        MdoMemoryRemoveOptionsInit(&Options);
        Options.Scope = Scope;
        Options.ProjectId = Scope == MDO_MEMORY_PROJECT ? Project : NULL;
        Options.Id = Id;
        Options.ExpectedRevision = Revision;
        Options.Actor = "web-ui";
        if ( !MdoMemoryRemove(&Options, &Error) )
            return MdoApiMemoryFailure(Context, &Error);
        Data = xrtValueObject();
        Result = Data != NULL && MdoApiValueSetString(Data, "id", Id) &&
            MdoApiValueSetBool(Data, "removed", true) &&
            MdoApiValueSetUInt(Data, "revision", Revision + 1u);
        if ( !Result ) {
            xrtValueRelease(Data);
            return MdoApiReplyError(Context, 500u,
                "memory_response_unavailable",
                "Memory was removed but its response is unavailable", NULL);
        }
        MdoApiMemoryTag(Tag, Revision + 1u);
        return MdoApiReplySuccessTakeEntityTag(Context, 200u, Data, Tag);
    }
}

bool MdoApiMemoryCollectionRoute(MdoApiContext* Context)
{
    return MdoApiMemoryRoute(Context, false);
}

bool MdoApiMemoryEntryRoute(MdoApiContext* Context)
{
    return MdoApiMemoryRoute(Context, true);
}

/* A user gesture explicitly materializes the containing Home directory. The
 * route accepts no filesystem path from the browser; project IDs are checked
 * by the same parser used for memory reads and writes. */
bool MdoApiMemoryOpenDirectoryRoute(MdoApiContext* Context)
{
    MdoMemoryScope Scope;
    char Project[MDO_MEMORY_PROJECT_CAPACITY];
    char Id[MDO_MEMORY_ID_CAPACITY];
    const char* Directory;
    MdoApiJsonBody Body;
    MdoApiBodyStatus BodyStatus;
    xfileinfo Info;
    bool Exists = false;
    str NativePath;
    bool Opened;
    xvalue* Data;

    if ( !MdoApiMemoryPath(Context, false, &Scope, Project, Id) )
        return MdoApiReplyError(Context, 400u, "invalid_memory_path",
            "Memory path is invalid", NULL);
    BodyStatus = MdoApiJsonBodyRead(Context, &Body);
    if ( BodyStatus != MDO_API_BODY_OK )
        return MdoApiReplyBodyError(Context, BodyStatus);
    Opened = Body.Value != NULL &&
        xrtValueType(Body.Value) == XVALUE_OBJECT &&
        xrtValueCount(Body.Value) == 0u;
    MdoApiJsonBodyUnit(&Body);
    if ( !Opened ) return MdoApiReplyError(Context, 400u,
        "invalid_request", "An empty JSON object is required", NULL);

    Directory = Scope == MDO_MEMORY_GLOBAL ? "memory" : "memory/projects";
    if ( !MdoHomeExternalStat(Directory, &Exists, &Info) )
        return MdoApiReplyError(Context, 503u, "memory_directory_unavailable",
            "Memory directory could not be inspected", NULL);
    if ( Exists && Info.Type != XFILE_TYPE_DIRECTORY )
        return MdoApiReplyError(Context, 409u, "memory_directory_conflict",
            "Memory directory path is occupied by another object", NULL);
    if ( !Exists && !MdoHomeCreateDirectory(Directory) ) {
        /* Another local writer may have created the same directory. */
        xrtClearError();
        if ( !MdoHomeExternalStat(Directory, &Exists, &Info) ||
             !Exists || Info.Type != XFILE_TYPE_DIRECTORY )
            return MdoApiReplyError(Context, 503u,
                "memory_directory_unavailable",
                "Memory directory could not be created", NULL);
    }

    Data = xrtValueObject();
    if ( Data == NULL ||
         !MdoApiValueSetBool(Data, "opened", true) ) {
        xrtValueRelease(Data);
        return MdoApiReplyError(Context, 500u,
            "memory_response_unavailable", "Cannot prepare directory response",
            NULL);
    }
    NativePath = MdoHomeExternalPath(Directory);
    if ( NativePath == NULL ) {
        xrtValueRelease(Data);
        return MdoApiReplyError(Context, 503u,
            "memory_directory_unavailable", "Memory directory path is unavailable",
            NULL);
    }
    Opened = xrtProcessOpen(NativePath);
    xrtFree(NativePath);
    if ( !Opened ) {
        xrtValueRelease(Data);
        return MdoApiReplyError(Context, 503u, "desktop_unavailable",
            "Could not open the directory on this device", NULL);
    }
    return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
}
