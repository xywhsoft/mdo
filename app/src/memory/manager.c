#include <stdlib.h>
#include <string.h>

#include "../../include/mdo/home.h"
#include "../../include/mdo/memory.h"

#define MDO_MEMORY_SCHEMA_VERSION 1u
#define MDO_MEMORY_MAX_ENTRIES 256u
#define MDO_MEMORY_MAX_TAGS 16u
#define MDO_MEMORY_CONTENT_LIMIT (16u * 1024u)
#define MDO_MEMORY_STORE_LIMIT (5u * 1024u * 1024u)
#define MDO_MEMORY_AUDIT_LIMIT (8u * 1024u * 1024u)
#define MDO_MEMORY_AUDIT_RETAIN (4u * 1024u * 1024u)
#define MDO_MEMORY_AUDIT_TEXT_LIMIT 257u

typedef struct MdoMemoryEntry {
    char* Id;
    char* Title;
    char* Content;
    char** Tags;
    size_t TagCount;
    uint64 Revision;
    int64 CreatedAt;
    int64 UpdatedAt;
    bool Pinned;
} MdoMemoryEntry;

struct MdoMemorySnapshot {
    xatomic32 Refs;
    MdoMemoryScope Scope;
    char ProjectId[MDO_MEMORY_PROJECT_CAPACITY];
    uint64 Revision;
    uint64 Generation;
    int64 UpdatedAt;
    MdoMemoryEntry* Entries;
    size_t Count;
};

typedef struct MdoMemoryState {
    xmutex* Lock;
    xfile WriterLock;
    xwork_runtime* Runtime;
    uint64 Generation;
    bool Initialized;
} MdoMemoryState;

static MdoMemoryState g_MdoMemory;

static void MdoMemoryError(xwork_error* Error, xwork_error_code Code,
    const char* Message)
{
    if ( Error == NULL ) return;
    xworkErrorInit(Error);
    Error->eCode = Code;
    snprintf(Error->sMessage, sizeof(Error->sMessage), "%s",
        Message != NULL ? Message : "memory operation failed");
}

static void MdoMemoryXrtError(xwork_error* Error, const char* Fallback)
{
    const xerror* Cause = xrtGetError();
    MdoMemoryError(Error, XWORK_ERROR_IO,
        Cause != NULL && xrtErrorMessage(Cause) != NULL ?
        xrtErrorMessage(Cause) : Fallback);
}

static bool MdoMemoryText(const char* Text, size_t Capacity,
    bool EmptyAllowed)
{
    size_t Size = 0u;
    if ( Text == NULL ) return false;
    while ( Size < Capacity && Text[Size] != '\0' ) ++Size;
    return Size < Capacity && (EmptyAllowed || Size != 0u) &&
        xrtUtf8Valid(xrtStrViewN(Text, Size), NULL);
}

static bool MdoMemoryId(const char* Text, size_t Capacity)
{
    size_t i = 0u;
    if ( !MdoMemoryText(Text, Capacity, false) ) return false;
    for ( ; Text[i] != '\0'; ++i ) {
        unsigned char Byte = (unsigned char)Text[i];
        if ( (Byte >= 'a' && Byte <= 'z') ||
             (Byte >= 'A' && Byte <= 'Z') ||
             (Byte >= '0' && Byte <= '9') || Byte == '-' || Byte == '_' ||
             (Byte == '.' && i != 0u) ) continue;
        return false;
    }
    return !(i == 1u && Text[0] == '.') &&
        !(i == 2u && Text[0] == '.' && Text[1] == '.');
}

static unsigned char MdoMemoryFold(unsigned char Byte)
{
    return Byte >= 'A' && Byte <= 'Z' ?
        (unsigned char)(Byte + ('a' - 'A')) : Byte;
}

static bool MdoMemoryContainsAscii(const char* Text, const char* Needle)
{
    const unsigned char* Start = (const unsigned char*)Text;
    size_t NeedleSize = strlen(Needle);
    for ( ; *Start != '\0'; ++Start ) {
        size_t i;
        for ( i = 0u; i < NeedleSize && Start[i] != '\0'; ++i )
            if ( MdoMemoryFold(Start[i]) !=
                 MdoMemoryFold((unsigned char)Needle[i]) ) break;
        if ( i == NeedleSize ) return true;
    }
    return false;
}

static bool MdoMemorySensitive(const char* Text)
{
    static const char* const Patterns[] = {
        "api_key", "api-key", "authorization:", "bearer ",
        "password=", "password:", "access_token", "refresh_token",
        "token=", "-----begin private key", "-----begin rsa private key"
    };
    size_t i;
    for ( i = 0u; i < sizeof(Patterns) / sizeof(Patterns[0]); ++i )
        if ( MdoMemoryContainsAscii(Text, Patterns[i]) ) return true;
    return false;
}

static bool MdoMemoryRequest(MdoMemoryScope Scope, const char* ProjectId,
    char Path[MDO_MEMORY_PATH_CAPACITY])
{
    int Written;
    if ( Scope == MDO_MEMORY_GLOBAL ) {
        if ( ProjectId != NULL && ProjectId[0] != '\0' ) return false;
        Written = snprintf(Path, MDO_MEMORY_PATH_CAPACITY,
            "memory/global.json");
    } else if ( Scope == MDO_MEMORY_PROJECT &&
               MdoMemoryId(ProjectId, MDO_MEMORY_PROJECT_CAPACITY) ) {
        Written = snprintf(Path, MDO_MEMORY_PATH_CAPACITY,
            "memory/projects/%s.json", ProjectId);
    } else return false;
    return Written > 0 && (size_t)Written < MDO_MEMORY_PATH_CAPACITY;
}

static void MdoMemoryEntryUnit(MdoMemoryEntry* Entry)
{
    size_t i;
    if ( Entry == NULL ) return;
    xrtFree(Entry->Id);
    xrtFree(Entry->Title);
    xrtFree(Entry->Content);
    for ( i = 0u; i < Entry->TagCount; ++i ) xrtFree(Entry->Tags[i]);
    xrtFree(Entry->Tags);
    memset(Entry, 0, sizeof(*Entry));
}

static void MdoMemorySnapshotFree(MdoMemorySnapshot* Snapshot)
{
    size_t i;
    if ( Snapshot == NULL ) return;
    for ( i = 0u; i < Snapshot->Count; ++i )
        MdoMemoryEntryUnit(&Snapshot->Entries[i]);
    xrtFree(Snapshot->Entries);
    memset(Snapshot, 0, sizeof(*Snapshot));
    xrtFree(Snapshot);
}

static MdoMemorySnapshot* MdoMemorySnapshotEmpty(MdoMemoryScope Scope,
    const char* ProjectId)
{
    MdoMemorySnapshot* Snapshot = (MdoMemorySnapshot*)xrtCalloc(1u,
        sizeof(*Snapshot));
    if ( Snapshot == NULL ) return NULL;
    xrtAtomic32Init(&Snapshot->Refs, 1u);
    Snapshot->Scope = Scope;
    if ( ProjectId != NULL ) snprintf(Snapshot->ProjectId,
        sizeof(Snapshot->ProjectId), "%s", ProjectId);
    return Snapshot;
}

static bool MdoMemoryReadBounded(const char* Path, size_t Limit,
    char** Data, size_t* Size)
{
    xfile File = NULL;
    xfileinfo Info;
    char* Bytes = NULL;
    bool Ok = false;
    *Data = NULL;
    *Size = 0u;
    File = MdoHomeOpenRead(Path);
    if ( File == NULL || !xrtFileStat(File, &Info) ||
         (Info.Available & XFILE_INFO_SIZE) == 0u ||
         Info.Type != XFILE_TYPE_FILE || Info.Size > Limit ||
         Info.Size > SIZE_MAX - 1u ) goto done;
    Bytes = (char*)xrtMalloc((size_t)Info.Size + 1u);
    if ( Bytes == NULL || (Info.Size != 0u &&
         !xrtReadFull(File, Bytes, (size_t)Info.Size, NULL)) ) goto done;
    Bytes[Info.Size] = '\0';
    *Data = Bytes;
    *Size = (size_t)Info.Size;
    Bytes = NULL;
    Ok = true;
done:
    if ( File != NULL && !xrtClose(File) ) Ok = false;
    xrtFree(Bytes);
    if ( !Ok ) {
        xrtFree(*Data);
        *Data = NULL;
        *Size = 0u;
    }
    return Ok;
}

static bool MdoMemoryObjectTake(xvalue* Object, const char* Key,
    xvalue* Value)
{
    bool Ok = Object != NULL && Value != NULL &&
        xrtValueObjectSetTake(Object, xrtStrView(Key), &Value);
    xrtValueRelease(Value);
    return Ok;
}

static bool MdoMemoryObjectString(xvalue* Object, const char* Key,
    const char* Text)
{
    return MdoMemoryObjectTake(Object, Key,
        xrtValueString(xrtStrView(Text != NULL ? Text : "")));
}

static bool MdoMemoryValueString(const xvalue* Object, const char* Key,
    xstrview* Text)
{
    const xvalue* Value = xrtValueObjectGet(Object, xrtStrView(Key));
    return Value != NULL && xrtValueType(Value) == XVALUE_STRING &&
        xrtValueGetString(Value, Text);
}

static bool MdoMemoryValueUInt(const xvalue* Object, const char* Key,
    uint64* Result)
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

static bool MdoMemoryCopyView(char** Target, xstrview View, size_t Capacity,
    bool EmptyAllowed)
{
    if ( View.Size >= Capacity || (!EmptyAllowed && View.Size == 0u) ||
         !xrtUtf8Valid(View, NULL) ) return false;
    *Target = xrtStrDupN(View.Data, View.Size);
    return *Target != NULL;
}

static bool MdoMemoryParseEntry(const xvalue* Value, MdoMemoryEntry* Entry)
{
    const xvalue* Tags;
    const xvalue* PinnedValue;
    xstrview Id;
    xstrview Title;
    xstrview Content;
    uint64 Revision;
    uint64 Created;
    uint64 Updated;
    bool Pinned;
    size_t i;
    if ( Value == NULL || xrtValueType(Value) != XVALUE_OBJECT ||
         xrtValueCount(Value) != 8u ||
         !MdoMemoryValueString(Value, "id", &Id) ||
         !MdoMemoryValueString(Value, "title", &Title) ||
         !MdoMemoryValueString(Value, "content", &Content) ||
         !MdoMemoryValueUInt(Value, "revision", &Revision) || Revision == 0u ||
         !MdoMemoryValueUInt(Value, "created_at_us", &Created) ||
         !MdoMemoryValueUInt(Value, "updated_at_us", &Updated) ||
         Created > INT64_MAX || Updated > INT64_MAX || Updated < Created )
        return false;
    Tags = xrtValueObjectGet(Value, xrtStrView("tags"));
    PinnedValue = xrtValueObjectGet(Value, xrtStrView("pinned"));
    if ( Tags == NULL || xrtValueType(Tags) != XVALUE_ARRAY ||
         xrtValueCount(Tags) > MDO_MEMORY_MAX_TAGS || PinnedValue == NULL ||
         xrtValueType(PinnedValue) != XVALUE_BOOL ||
         !xrtValueGetBool(PinnedValue, &Pinned) ||
         !MdoMemoryCopyView(&Entry->Id, Id, MDO_MEMORY_ID_CAPACITY, false) ||
         !MdoMemoryCopyView(&Entry->Title, Title,
            MDO_MEMORY_TITLE_CAPACITY, true) ||
         !MdoMemoryCopyView(&Entry->Content, Content,
            MDO_MEMORY_CONTENT_LIMIT + 1u, false) ||
         !MdoMemoryId(Entry->Id, MDO_MEMORY_ID_CAPACITY) ||
         MdoMemorySensitive(Entry->Title) ||
         MdoMemorySensitive(Entry->Content) ) return false;
    Entry->TagCount = xrtValueCount(Tags);
    if ( Entry->TagCount != 0u ) {
        Entry->Tags = (char**)xrtCalloc(Entry->TagCount, sizeof(*Entry->Tags));
        if ( Entry->Tags == NULL ) return false;
    }
    for ( i = 0u; i < Entry->TagCount; ++i ) {
        const xvalue* TagValue = xrtValueArrayGet(Tags, i);
        xstrview Tag;
        size_t j;
        if ( TagValue == NULL || xrtValueType(TagValue) != XVALUE_STRING ||
             !xrtValueGetString(TagValue, &Tag) ||
             !MdoMemoryCopyView(&Entry->Tags[i], Tag,
                MDO_MEMORY_TAG_CAPACITY, false) ||
             MdoMemorySensitive(Entry->Tags[i]) ) return false;
        for ( j = 0u; j < i; ++j )
            if ( strcmp(Entry->Tags[j], Entry->Tags[i]) == 0 ) return false;
    }
    Entry->Revision = Revision;
    Entry->CreatedAt = (int64)Created;
    Entry->UpdatedAt = (int64)Updated;
    Entry->Pinned = Pinned;
    return true;
}

static int MdoMemoryEntryCompare(const void* LeftValue,
    const void* RightValue)
{
    const MdoMemoryEntry* Left = (const MdoMemoryEntry*)LeftValue;
    const MdoMemoryEntry* Right = (const MdoMemoryEntry*)RightValue;
    if ( Left->Pinned != Right->Pinned ) return Left->Pinned ? -1 : 1;
    if ( Left->UpdatedAt != Right->UpdatedAt )
        return Left->UpdatedAt > Right->UpdatedAt ? -1 : 1;
    return strcmp(Left->Id, Right->Id);
}

static MdoMemorySnapshot* MdoMemoryParse(MdoMemoryScope Scope,
    const char* ProjectId, xstrview Json)
{
    xjsonreadconfig Config;
    xvalue* Root = NULL;
    const xvalue* Entries;
    MdoMemorySnapshot* Snapshot = NULL;
    uint64 Schema;
    uint64 Revision;
    uint64 Updated;
    size_t i;
    xrtJsonReadConfigInit(&Config);
    Config.MaxInputBytes = MDO_MEMORY_STORE_LIMIT;
    Config.MaxDepth = 8u;
    Config.MaxValues = 8192u;
    Config.MaxContainerItems = MDO_MEMORY_MAX_ENTRIES + MDO_MEMORY_MAX_TAGS;
    Root = xrtJsonRead(Json, &Config);
    Entries = Root != NULL ? xrtValueObjectGet(Root,
        xrtStrView("entries")) : NULL;
    if ( Root == NULL || xrtValueType(Root) != XVALUE_OBJECT ||
         xrtValueCount(Root) != 4u ||
         !MdoMemoryValueUInt(Root, "schema_version", &Schema) ||
         Schema != MDO_MEMORY_SCHEMA_VERSION ||
         !MdoMemoryValueUInt(Root, "revision", &Revision) || Revision == 0u ||
         !MdoMemoryValueUInt(Root, "updated_at_us", &Updated) ||
         Updated > INT64_MAX || Entries == NULL ||
         xrtValueType(Entries) != XVALUE_ARRAY ||
         xrtValueCount(Entries) > MDO_MEMORY_MAX_ENTRIES ) goto done;
    Snapshot = MdoMemorySnapshotEmpty(Scope, ProjectId);
    if ( Snapshot == NULL ) goto done;
    Snapshot->Revision = Revision;
    Snapshot->UpdatedAt = (int64)Updated;
    Snapshot->Count = xrtValueCount(Entries);
    if ( Snapshot->Count != 0u ) {
        Snapshot->Entries = (MdoMemoryEntry*)xrtCalloc(Snapshot->Count,
            sizeof(*Snapshot->Entries));
        if ( Snapshot->Entries == NULL ) goto invalid;
    }
    for ( i = 0u; i < Snapshot->Count; ++i ) {
        size_t j;
        if ( !MdoMemoryParseEntry(xrtValueArrayGet(Entries, i),
                &Snapshot->Entries[i]) ) goto invalid;
        for ( j = 0u; j < i; ++j )
            if ( strcmp(Snapshot->Entries[j].Id,
                    Snapshot->Entries[i].Id) == 0 ) goto invalid;
    }
    if ( Snapshot->Count > 1u ) qsort(Snapshot->Entries, Snapshot->Count,
        sizeof(*Snapshot->Entries), MdoMemoryEntryCompare);
    goto done;
invalid:
    MdoMemorySnapshotFree(Snapshot);
    Snapshot = NULL;
done:
    xrtValueRelease(Root);
    return Snapshot;
}

static MdoMemorySnapshot* MdoMemoryLoad(MdoMemoryScope Scope,
    const char* ProjectId, const char* Path, xwork_error* Error)
{
    bool Exists = false;
    xfileinfo Info;
    char* Data = NULL;
    size_t Size = 0u;
    MdoMemorySnapshot* Snapshot;
    if ( !MdoHomeExternalStat(Path, &Exists, &Info) ) {
        MdoMemoryXrtError(Error, "cannot inspect the memory store");
        return NULL;
    }
    if ( !Exists ) {
        Snapshot = MdoMemorySnapshotEmpty(Scope, ProjectId);
        if ( Snapshot == NULL ) MdoMemoryError(Error,
            XWORK_ERROR_OUT_OF_MEMORY, "cannot allocate an empty memory view");
        return Snapshot;
    }
    if ( Info.Type != XFILE_TYPE_FILE ||
         !MdoMemoryReadBounded(Path, MDO_MEMORY_STORE_LIMIT, &Data, &Size) ) {
        MdoMemoryXrtError(Error, "cannot read the memory store");
        return NULL;
    }
    Snapshot = MdoMemoryParse(Scope, ProjectId, xrtStrViewN(Data, Size));
    xrtFree(Data);
    if ( Snapshot == NULL ) MdoMemoryError(Error, XWORK_ERROR_IO,
        "memory store does not satisfy schema version 1");
    return Snapshot;
}

static char* MdoMemoryJson(const MdoMemorySnapshot* Snapshot, size_t* Size)
{
    xvalue* Root = xrtValueObject();
    xvalue* Entries = xrtValueArray();
    char* Json = NULL;
    size_t i;
    if ( Root == NULL || Entries == NULL ||
         !MdoMemoryObjectTake(Root, "schema_version",
            xrtValueUInt(MDO_MEMORY_SCHEMA_VERSION)) ||
         !MdoMemoryObjectTake(Root, "revision",
            xrtValueUInt(Snapshot->Revision)) ||
         !MdoMemoryObjectTake(Root, "updated_at_us",
            xrtValueInt(Snapshot->UpdatedAt)) ) goto done;
    for ( i = 0u; i < Snapshot->Count; ++i ) {
        const MdoMemoryEntry* Entry = &Snapshot->Entries[i];
        xvalue* Value = xrtValueObject();
        xvalue* Tags = xrtValueArray();
        size_t j;
        if ( Value == NULL || Tags == NULL ) {
            xrtValueRelease(Value);
            xrtValueRelease(Tags);
            goto done;
        }
        for ( j = 0u; j < Entry->TagCount; ++j ) {
            xvalue* Tag = xrtValueString(xrtStrView(Entry->Tags[j]));
            if ( Tag == NULL || !xrtValueArrayAppendTake(Tags, &Tag) ) {
                xrtValueRelease(Tag);
                xrtValueRelease(Value);
                xrtValueRelease(Tags);
                goto done;
            }
        }
        if ( !MdoMemoryObjectString(Value, "id", Entry->Id) ||
             !MdoMemoryObjectString(Value, "title", Entry->Title) ||
             !MdoMemoryObjectString(Value, "content", Entry->Content) ||
             !xrtValueObjectSetTake(Value, xrtStrView("tags"), &Tags) ||
             !MdoMemoryObjectTake(Value, "pinned",
                xrtValueBool(Entry->Pinned)) ||
             !MdoMemoryObjectTake(Value, "revision",
                xrtValueUInt(Entry->Revision)) ||
             !MdoMemoryObjectTake(Value, "created_at_us",
                xrtValueInt(Entry->CreatedAt)) ||
             !MdoMemoryObjectTake(Value, "updated_at_us",
                xrtValueInt(Entry->UpdatedAt)) ||
             !xrtValueArrayAppendTake(Entries, &Value) ) {
            xrtValueRelease(Tags);
            xrtValueRelease(Value);
            goto done;
        }
    }
    if ( !xrtValueObjectSetTake(Root, xrtStrView("entries"), &Entries) )
        goto done;
    Json = xrtJsonStringify(Root, true, Size);
    if ( Json != NULL && *Size > MDO_MEMORY_STORE_LIMIT ) {
        xrtFree(Json);
        Json = NULL;
    }
done:
    xrtValueRelease(Entries);
    xrtValueRelease(Root);
    return Json;
}

static bool MdoMemoryWriterLock(xwork_error* Error)
{
    xfile File;
    if ( g_MdoMemory.WriterLock != NULL ) return true;
    File = MdoHomeOpenWrite("memory/.writer.lock",
        XFILE_READ | XFILE_CREATE | XFILE_SYNC);
    if ( File == NULL || !xrtFileLock(File, XFILE_LOCK_EXCLUSIVE, false) ) {
        if ( File != NULL ) (void)xrtClose(File);
        MdoMemoryError(Error, XWORK_ERROR_CONTEXT,
            "memory store is locked by another process");
        return false;
    }
    g_MdoMemory.WriterLock = File;
    return true;
}

static bool MdoMemoryAudit(const char* Operation, MdoMemoryScope Scope,
    const char* ProjectId, const char* EntryId, uint64 Previous,
    uint64 Next, const char* Actor, const char* SessionId, const char* Reason,
    const char* Content, size_t ContentBytes, xwork_error* Error)
{
    static const char Hex[] = "0123456789abcdef";
    xvalue* Root = xrtValueObject();
    char* AuditId = xrtXidMakeString();
    uint8 Digest[XRT_SHA256_SIZE];
    char ContentHash[XRT_SHA256_SIZE * 2u + 1u];
    char* Json = NULL;
    size_t Size = 0u;
    size_t i;
    xfile File = NULL;
    xfileinfo Info;
    bool Exists = false;
    bool Ok = false;
    ContentHash[0] = '\0';
    if ( Content != NULL ) {
        if ( !xrtSha256(Content, ContentBytes, Digest) ) goto memory;
        for ( i = 0u; i < XRT_SHA256_SIZE; ++i ) {
            ContentHash[i * 2u] = Hex[Digest[i] >> 4u];
            ContentHash[i * 2u + 1u] = Hex[Digest[i] & 0x0fu];
        }
        ContentHash[sizeof(ContentHash) - 1u] = '\0';
    }
    if ( Root == NULL || AuditId == NULL ||
         !MdoMemoryObjectTake(Root, "schema_version", xrtValueUInt(1u)) ||
         !MdoMemoryObjectString(Root, "audit_id", AuditId) ||
         !MdoMemoryObjectTake(Root, "occurred_at_us", xrtValueInt(xrtNow())) ||
         !MdoMemoryObjectString(Root, "phase", "prepared") ||
         !MdoMemoryObjectString(Root, "operation", Operation) ||
         !MdoMemoryObjectString(Root, "scope",
            Scope == MDO_MEMORY_GLOBAL ? "global" : "project") ||
         !MdoMemoryObjectString(Root, "project_id",
            ProjectId != NULL ? ProjectId : "") ||
         !MdoMemoryObjectString(Root, "entry_id", EntryId) ||
         !MdoMemoryObjectTake(Root, "previous_revision",
            xrtValueUInt(Previous)) ||
         !MdoMemoryObjectTake(Root, "next_revision", xrtValueUInt(Next)) ||
         !MdoMemoryObjectString(Root, "actor",
            Actor != NULL ? Actor : "host") ||
         !MdoMemoryObjectString(Root, "session_id",
            SessionId != NULL ? SessionId : "") ||
         !MdoMemoryObjectString(Root, "reason",
            Reason != NULL ? Reason : "") ||
         !MdoMemoryObjectTake(Root, "content_bytes",
            xrtValueUInt(ContentBytes)) ||
         !MdoMemoryObjectString(Root, "content_sha256", ContentHash) )
        goto memory;
    Json = xrtJsonStringify(Root, false, &Size);
    if ( Json == NULL || Size > 4096u ) goto memory;
    if ( !MdoHomeExternalStat("memory/audit.jsonl", &Exists, &Info) )
        goto io;
    if ( Exists && ((Info.Available & XFILE_INFO_SIZE) == 0u ||
         Info.Type != XFILE_TYPE_FILE || Info.Size > MDO_MEMORY_AUDIT_LIMIT) ) {
        MdoMemoryError(Error, XWORK_ERROR_LIMIT,
            "memory audit reached its bounded file limit");
        goto done;
    }
    if ( Exists && Info.Size > MDO_MEMORY_AUDIT_LIMIT - Size - 1u ) {
        char* PreviousData = NULL;
        size_t PreviousSize = 0u;
        size_t Start;
        if ( !MdoMemoryReadBounded("memory/audit.jsonl",
                MDO_MEMORY_AUDIT_LIMIT, &PreviousData, &PreviousSize) )
            goto io;
        Start = PreviousSize > MDO_MEMORY_AUDIT_RETAIN ?
            PreviousSize - MDO_MEMORY_AUDIT_RETAIN : 0u;
        if ( Start != 0u ) {
            while ( Start < PreviousSize && PreviousData[Start] != '\n' )
                ++Start;
            if ( Start < PreviousSize ) ++Start;
        }
        if ( !MdoHomeAtomicWrite("memory/audit.jsonl", PreviousData + Start,
                PreviousSize - Start, false) ) {
            xrtFree(PreviousData);
            goto io;
        }
        xrtFree(PreviousData);
    }
    File = MdoHomeOpenWrite("memory/audit.jsonl",
        XFILE_CREATE | XFILE_APPEND | XFILE_SYNC);
    if ( File == NULL || !xrtWriteFull(File, Json, Size, NULL) ||
         !xrtWriteFull(File, "\n", 1u, NULL) || !xrtFlush(File) ) goto io;
    if ( !xrtClose(File) ) { File = NULL; goto io; }
    File = NULL;
    Ok = true;
    goto done;
memory:
    MdoMemoryError(Error, XWORK_ERROR_OUT_OF_MEMORY,
        "cannot allocate the memory audit record");
    goto done;
io:
    MdoMemoryXrtError(Error, "cannot persist the memory audit record");
done:
    if ( File != NULL ) (void)xrtClose(File);
    xrtFree(Json);
    xrtFree(AuditId);
    xrtValueRelease(Root);
    return Ok;
}

static bool MdoMemoryWriteStore(const char* Path,
    const MdoMemorySnapshot* Snapshot, xwork_error* Error)
{
    char* Json;
    size_t Size = 0u;
    bool Ok;
    Json = MdoMemoryJson(Snapshot, &Size);
    if ( Json == NULL ) {
        MdoMemoryError(Error, XWORK_ERROR_LIMIT,
            "memory store exceeds its size or allocation limit");
        return false;
    }
    Ok = MdoHomeAtomicWrite(Path, Json, Size, true);
    xrtFree(Json);
    if ( !Ok ) MdoMemoryXrtError(Error, "cannot publish the memory store");
    return Ok;
}

static size_t MdoMemoryFind(const MdoMemorySnapshot* Snapshot,
    const char* Id)
{
    size_t i;
    for ( i = 0u; i < Snapshot->Count; ++i )
        if ( strcmp(Snapshot->Entries[i].Id, Id) == 0 ) return i;
    return SIZE_MAX;
}

static bool MdoMemoryAuditTextValid(const char* Text, bool EmptyAllowed)
{
    return Text == NULL || MdoMemoryText(Text, MDO_MEMORY_AUDIT_TEXT_LIMIT,
        EmptyAllowed);
}

bool MdoMemoryManagerInit(xwork_runtime* Runtime)
{
    if ( g_MdoMemory.Initialized ) return true;
    if ( Runtime == NULL ) return false;
    memset(&g_MdoMemory, 0, sizeof(g_MdoMemory));
    g_MdoMemory.Lock = xrtMutexCreate();
    g_MdoMemory.Runtime = xworkRuntimeRef(Runtime);
    if ( g_MdoMemory.Lock == NULL || g_MdoMemory.Runtime == NULL ) {
        MdoMemoryManagerUnit();
        return false;
    }
    g_MdoMemory.Generation = 1u;
    g_MdoMemory.Initialized = true;
    return true;
}

void MdoMemoryManagerUnit(void)
{
    g_MdoMemory.Initialized = false;
    if ( g_MdoMemory.WriterLock != NULL ) {
        (void)xrtFileUnlock(g_MdoMemory.WriterLock);
        (void)xrtClose(g_MdoMemory.WriterLock);
    }
    if ( g_MdoMemory.Runtime != NULL )
        xworkRuntimeRelease(g_MdoMemory.Runtime);
    if ( g_MdoMemory.Lock != NULL ) xrtMutexDestroy(g_MdoMemory.Lock);
    memset(&g_MdoMemory, 0, sizeof(g_MdoMemory));
}

uint64 MdoMemoryManagerGeneration(void)
{
    uint64 Generation = 0u;
    if ( !g_MdoMemory.Initialized ) return 0u;
    xrtMutexLock(g_MdoMemory.Lock);
    Generation = g_MdoMemory.Generation;
    xrtMutexUnlock(g_MdoMemory.Lock);
    return Generation;
}

void MdoMemoryWriteOptionsInit(MdoMemoryWriteOptions* Options)
{
    if ( Options == NULL ) return;
    memset(Options, 0, sizeof(*Options));
    Options->Size = sizeof(*Options);
    Options->ExpectedRevision = UINT64_MAX;
}

void MdoMemoryRemoveOptionsInit(MdoMemoryRemoveOptions* Options)
{
    if ( Options == NULL ) return;
    memset(Options, 0, sizeof(*Options));
    Options->Size = sizeof(*Options);
    Options->ExpectedRevision = UINT64_MAX;
}

MdoMemorySnapshot* MdoMemorySnapshotCreate(MdoMemoryScope Scope,
    const char* ProjectId, xwork_error* Error)
{
    char Path[MDO_MEMORY_PATH_CAPACITY];
    MdoMemorySnapshot* Snapshot;
    xworkErrorInit(Error);
    if ( !g_MdoMemory.Initialized ||
         !MdoMemoryRequest(Scope, ProjectId, Path) ) {
        MdoMemoryError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "invalid memory snapshot request");
        return NULL;
    }
    xrtMutexLock(g_MdoMemory.Lock);
    Snapshot = MdoMemoryLoad(Scope, ProjectId, Path, Error);
    if ( Snapshot != NULL ) Snapshot->Generation = g_MdoMemory.Generation;
    xrtMutexUnlock(g_MdoMemory.Lock);
    return Snapshot;
}

MdoMemorySnapshot* MdoMemorySnapshotRef(MdoMemorySnapshot* Snapshot)
{
    uint32 Refs;
    if ( Snapshot == NULL ) return NULL;
    Refs = xrtAtomic32Load(&Snapshot->Refs, XMEMORY_ACQUIRE);
    for ( ; ; ) {
        uint32 Expected = Refs;
        if ( Refs == 0u || Refs == UINT32_MAX ) return NULL;
        if ( xrtAtomic32CompareExchange(&Snapshot->Refs, &Expected, Refs + 1u,
                XMEMORY_ACQ_REL, XMEMORY_ACQUIRE) ) return Snapshot;
        Refs = Expected;
    }
}

void MdoMemorySnapshotRelease(MdoMemorySnapshot* Snapshot)
{
    uint32 Previous;
    if ( Snapshot == NULL ) return;
    Previous = xrtAtomic32FetchSub(&Snapshot->Refs, 1u, XMEMORY_ACQ_REL);
    if ( Previous > 1u ) return;
    if ( Previous == 0u ) abort();
    MdoMemorySnapshotFree(Snapshot);
}

MdoMemoryScope MdoMemorySnapshotScope(const MdoMemorySnapshot* Snapshot)
{
    return Snapshot != NULL ? Snapshot->Scope : 0;
}

const char* MdoMemorySnapshotProjectId(const MdoMemorySnapshot* Snapshot)
{
    return Snapshot != NULL ? Snapshot->ProjectId : NULL;
}

uint64 MdoMemorySnapshotRevision(const MdoMemorySnapshot* Snapshot)
{
    return Snapshot != NULL ? Snapshot->Revision : 0u;
}

uint64 MdoMemorySnapshotGeneration(const MdoMemorySnapshot* Snapshot)
{
    return Snapshot != NULL ? Snapshot->Generation : 0u;
}

size_t MdoMemorySnapshotCount(const MdoMemorySnapshot* Snapshot)
{
    return Snapshot != NULL ? Snapshot->Count : 0u;
}

bool MdoMemorySnapshotAt(const MdoMemorySnapshot* Snapshot, size_t Index,
    MdoMemoryEntryInfo* Info)
{
    const MdoMemoryEntry* Entry;
    uint32 Size;
    if ( Snapshot == NULL || Index >= Snapshot->Count || Info == NULL ||
         Info->Size < sizeof(*Info) ) return false;
    Size = Info->Size;
    Entry = &Snapshot->Entries[Index];
    memset(Info, 0, sizeof(*Info));
    Info->Size = Size;
    Info->Revision = Entry->Revision;
    Info->CreatedAt = Entry->CreatedAt;
    Info->UpdatedAt = Entry->UpdatedAt;
    Info->Pinned = Entry->Pinned;
    Info->Id = Entry->Id;
    Info->Title = Entry->Title;
    Info->Content = Entry->Content;
    Info->Tags = (const char* const*)Entry->Tags;
    Info->TagCount = Entry->TagCount;
    return true;
}

static bool MdoMemoryWriteValid(const MdoMemoryWriteOptions* Options,
    char Path[MDO_MEMORY_PATH_CAPACITY])
{
    size_t i;
    if ( Options == NULL || Options->Size < sizeof(*Options) ||
         !MdoMemoryRequest(Options->Scope, Options->ProjectId, Path) ||
         !MdoMemoryId(Options->Id, MDO_MEMORY_ID_CAPACITY) ||
         !MdoMemoryText(Options->Title, MDO_MEMORY_TITLE_CAPACITY, true) ||
         !MdoMemoryText(Options->Content, MDO_MEMORY_CONTENT_LIMIT + 1u,
            false) || Options->TagCount > MDO_MEMORY_MAX_TAGS ||
         (Options->TagCount != 0u && Options->Tags == NULL) ||
         !MdoMemoryAuditTextValid(Options->Actor, false) ||
         !MdoMemoryAuditTextValid(Options->SessionId, true) ||
         !MdoMemoryAuditTextValid(Options->Reason, true) ||
         MdoMemorySensitive(Options->Title) ||
         MdoMemorySensitive(Options->Content) ||
         (Options->Actor != NULL && MdoMemorySensitive(Options->Actor)) ||
         (Options->SessionId != NULL &&
          MdoMemorySensitive(Options->SessionId)) ||
         (Options->Reason != NULL && MdoMemorySensitive(Options->Reason)) )
        return false;
    for ( i = 0u; i < Options->TagCount; ++i ) {
        size_t j;
        if ( !MdoMemoryText(Options->Tags[i], MDO_MEMORY_TAG_CAPACITY,
                false) || MdoMemorySensitive(Options->Tags[i]) ) return false;
        for ( j = 0u; j < i; ++j )
            if ( strcmp(Options->Tags[j], Options->Tags[i]) == 0 ) return false;
    }
    return true;
}

bool MdoMemoryUpsert(const MdoMemoryWriteOptions* Options,
    xwork_error* Error)
{
    char Path[MDO_MEMORY_PATH_CAPACITY];
    MdoMemorySnapshot* Snapshot = NULL;
    MdoMemoryEntry* Entry;
    char* Id = NULL;
    char* Title = NULL;
    char* Content = NULL;
    char** Tags = NULL;
    size_t Index;
    size_t i;
    int64 Now;
    bool Created = false;
    bool Ok = false;
    xworkErrorInit(Error);
    if ( !g_MdoMemory.Initialized || !MdoMemoryWriteValid(Options, Path) ) {
        MdoMemoryError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "invalid or sensitive memory write request");
        return false;
    }
    Id = xrtStrDup(Options->Id);
    Title = xrtStrDup(Options->Title);
    Content = xrtStrDup(Options->Content);
    if ( Options->TagCount != 0u )
        Tags = (char**)xrtCalloc(Options->TagCount, sizeof(*Tags));
    if ( Id == NULL || Title == NULL || Content == NULL ||
         (Options->TagCount != 0u && Tags == NULL) ) goto memory;
    for ( i = 0u; i < Options->TagCount; ++i ) {
        Tags[i] = xrtStrDup(Options->Tags[i]);
        if ( Tags[i] == NULL ) goto memory;
    }
    xrtMutexLock(g_MdoMemory.Lock);
    if ( !MdoMemoryWriterLock(Error) ) goto done_locked;
    Snapshot = MdoMemoryLoad(Options->Scope, Options->ProjectId, Path, Error);
    if ( Snapshot == NULL ) goto done_locked;
    if ( Options->ExpectedRevision != UINT64_MAX &&
         Options->ExpectedRevision != Snapshot->Revision ) {
        MdoMemoryError(Error, XWORK_ERROR_CONTEXT,
            "memory store revision changed; reload before writing");
        goto done_locked;
    }
    if ( Snapshot->Revision == UINT64_MAX ) {
        MdoMemoryError(Error, XWORK_ERROR_LIMIT,
            "memory store revision is exhausted");
        goto done_locked;
    }
    Index = MdoMemoryFind(Snapshot, Options->Id);
    if ( Index == SIZE_MAX ) {
        MdoMemoryEntry* Entries;
        if ( Snapshot->Count >= MDO_MEMORY_MAX_ENTRIES ) {
            MdoMemoryError(Error, XWORK_ERROR_LIMIT,
                "memory scope reached its entry limit");
            goto done_locked;
        }
        Entries = (MdoMemoryEntry*)xrtRealloc(Snapshot->Entries,
            (Snapshot->Count + 1u) * sizeof(*Entries));
        if ( Entries == NULL ) goto memory_locked;
        Snapshot->Entries = Entries;
        Index = Snapshot->Count++;
        Created = true;
        memset(&Snapshot->Entries[Index], 0, sizeof(Snapshot->Entries[Index]));
    }
    Entry = &Snapshot->Entries[Index];
    if ( Entry->Revision == UINT64_MAX ) {
        MdoMemoryError(Error, XWORK_ERROR_LIMIT,
            "memory entry revision is exhausted");
        goto done_locked;
    }
    Now = xrtNow();
    xrtFree(Entry->Id);
    xrtFree(Entry->Title);
    xrtFree(Entry->Content);
    for ( i = 0u; i < Entry->TagCount; ++i ) xrtFree(Entry->Tags[i]);
    xrtFree(Entry->Tags);
    Entry->Id = Id; Id = NULL;
    Entry->Title = Title; Title = NULL;
    Entry->Content = Content; Content = NULL;
    Entry->Tags = Tags; Tags = NULL;
    Entry->TagCount = Options->TagCount;
    Entry->Pinned = Options->Pinned;
    Entry->CreatedAt = Entry->CreatedAt != 0 ? Entry->CreatedAt : Now;
    Entry->UpdatedAt = Now < Entry->CreatedAt ? Entry->CreatedAt : Now;
    ++Entry->Revision;
    if ( Entry->Revision == 0u ) Entry->Revision = 1u;
    {
        uint64 Previous = Snapshot->Revision;
        ++Snapshot->Revision;
        Snapshot->UpdatedAt = Entry->UpdatedAt;
        if ( Snapshot->Count > 1u ) qsort(Snapshot->Entries, Snapshot->Count,
            sizeof(*Snapshot->Entries), MdoMemoryEntryCompare);
        if ( !MdoMemoryAudit(Created ? "create" : "update",
                Options->Scope, Options->ProjectId, Options->Id,
                Previous, Snapshot->Revision, Options->Actor,
                Options->SessionId, Options->Reason,
                Options->Content, strlen(Options->Content), Error) ||
             !MdoMemoryWriteStore(Path, Snapshot, Error) ) goto done_locked;
    }
    if ( g_MdoMemory.Generation != UINT64_MAX ) ++g_MdoMemory.Generation;
    Ok = true;
    goto done_locked;
memory_locked:
    MdoMemoryError(Error, XWORK_ERROR_OUT_OF_MEMORY,
        "cannot grow the memory store");
done_locked:
    MdoMemorySnapshotRelease(Snapshot);
    xrtMutexUnlock(g_MdoMemory.Lock);
    goto done;
memory:
    MdoMemoryError(Error, XWORK_ERROR_OUT_OF_MEMORY,
        "cannot copy the memory entry");
done:
    xrtFree(Id);
    xrtFree(Title);
    xrtFree(Content);
    if ( Tags != NULL ) {
        for ( i = 0u; i < Options->TagCount; ++i ) xrtFree(Tags[i]);
        xrtFree(Tags);
    }
    return Ok;
}

bool MdoMemoryRemove(const MdoMemoryRemoveOptions* Options,
    xwork_error* Error)
{
    char Path[MDO_MEMORY_PATH_CAPACITY];
    MdoMemorySnapshot* Snapshot = NULL;
    size_t Index;
    uint64 Previous;
    bool Ok = false;
    xworkErrorInit(Error);
    if ( !g_MdoMemory.Initialized || Options == NULL ||
         Options->Size < sizeof(*Options) ||
         !MdoMemoryRequest(Options->Scope, Options->ProjectId, Path) ||
         !MdoMemoryId(Options->Id, MDO_MEMORY_ID_CAPACITY) ||
         !MdoMemoryAuditTextValid(Options->Actor, false) ||
         !MdoMemoryAuditTextValid(Options->SessionId, true) ||
         !MdoMemoryAuditTextValid(Options->Reason, true) ||
         (Options->Actor != NULL && MdoMemorySensitive(Options->Actor)) ||
         (Options->SessionId != NULL &&
          MdoMemorySensitive(Options->SessionId)) ||
         (Options->Reason != NULL && MdoMemorySensitive(Options->Reason)) ) {
        MdoMemoryError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "invalid memory remove request");
        return false;
    }
    xrtMutexLock(g_MdoMemory.Lock);
    if ( !MdoMemoryWriterLock(Error) ) goto done;
    Snapshot = MdoMemoryLoad(Options->Scope, Options->ProjectId, Path, Error);
    if ( Snapshot == NULL ) goto done;
    if ( Options->ExpectedRevision != UINT64_MAX &&
         Options->ExpectedRevision != Snapshot->Revision ) {
        MdoMemoryError(Error, XWORK_ERROR_CONTEXT,
            "memory store revision changed; reload before removing");
        goto done;
    }
    Index = MdoMemoryFind(Snapshot, Options->Id);
    if ( Index == SIZE_MAX ) {
        MdoMemoryError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "memory entry does not exist");
        goto done;
    }
    if ( Snapshot->Revision == UINT64_MAX ) {
        MdoMemoryError(Error, XWORK_ERROR_LIMIT,
            "memory store revision is exhausted");
        goto done;
    }
    Previous = Snapshot->Revision++;
    Snapshot->UpdatedAt = xrtNow();
    if ( Snapshot->UpdatedAt < Snapshot->Entries[Index].CreatedAt )
        Snapshot->UpdatedAt = Snapshot->Entries[Index].CreatedAt;
    if ( !MdoMemoryAudit("remove", Options->Scope, Options->ProjectId,
            Options->Id, Previous, Snapshot->Revision, Options->Actor,
            Options->SessionId, Options->Reason, NULL, 0u, Error) ) goto done;
    MdoMemoryEntryUnit(&Snapshot->Entries[Index]);
    --Snapshot->Count;
    if ( Index != Snapshot->Count )
        Snapshot->Entries[Index] = Snapshot->Entries[Snapshot->Count];
    memset(&Snapshot->Entries[Snapshot->Count], 0,
        sizeof(*Snapshot->Entries));
    if ( Snapshot->Count > 1u ) qsort(Snapshot->Entries, Snapshot->Count,
        sizeof(*Snapshot->Entries), MdoMemoryEntryCompare);
    if ( !MdoMemoryWriteStore(Path, Snapshot, Error) ) goto done;
    if ( g_MdoMemory.Generation != UINT64_MAX ) ++g_MdoMemory.Generation;
    Ok = true;
done:
    MdoMemorySnapshotRelease(Snapshot);
    xrtMutexUnlock(g_MdoMemory.Lock);
    return Ok;
}
