#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../include/mdo/home.h"
#include "../../include/mdo/projects.h"

#define MDO_PROJECT_SCHEMA_VERSION 1u
#define MDO_PROJECT_STORE_LIMIT 8192u
#define MDO_PROJECT_SCAN_LIMIT 512u

static void MdoProjectsError(xwork_error* Error, xwork_error_code Code,
    const char* Message)
{
    if ( Error == NULL ) return;
    xworkErrorInit(Error);
    Error->eCode = Code;
    snprintf(Error->sMessage, sizeof(Error->sMessage), "%s", Message);
}

static bool MdoProjectsText(const char* Text, size_t Capacity,
    bool EmptyAllowed)
{
    size_t Size = 0u;
    size_t Index;
    if ( Text == NULL ) return false;
    while ( Size < Capacity && Text[Size] != '\0' ) ++Size;
    if ( Size >= Capacity || (!EmptyAllowed && Size == 0u) ||
         !xrtUtf8Valid(xrtStrViewN(Text, Size), NULL) ) return false;
    for ( Index = 0u; Index < Size; ++Index ) {
        unsigned char Byte = (unsigned char)Text[Index];
        if ( Byte < 0x20u || Byte == 0x7fu ) return false;
    }
    return true;
}

static bool MdoProjectsId(const char* Id)
{
    size_t Index;
    if ( !MdoProjectsText(Id, MDO_PROJECT_ID_CAPACITY, false) ||
         Id[0] == '.' ) return false;
    for ( Index = 0u; Id[Index] != '\0'; ++Index ) {
        unsigned char Byte = (unsigned char)Id[Index];
        if ( (Byte >= 'a' && Byte <= 'z') ||
             (Byte >= 'A' && Byte <= 'Z') ||
             (Byte >= '0' && Byte <= '9') || Byte == '-' ||
             Byte == '_' || Byte == '.' ) continue;
        return false;
    }
    return true;
}

static bool MdoProjectsPath(const char* Id, char Path[96])
{
    int Written = snprintf(Path, 96u, "projects/%s.json", Id);
    return Written > 0 && Written < 96;
}

static bool MdoProjectsCopyView(char* Target, size_t Capacity,
    xstrview View, bool EmptyAllowed)
{
    if ( View.Size >= Capacity || (!EmptyAllowed && View.Size == 0u) ||
         memchr(View.Data, '\0', View.Size) != NULL ||
         !xrtUtf8Valid(View, NULL) ) return false;
    memcpy(Target, View.Data, View.Size);
    Target[View.Size] = '\0';
    return MdoProjectsText(Target, Capacity, EmptyAllowed);
}

static bool MdoProjectsValueString(const xvalue* Object, const char* Key,
    xstrview* Result)
{
    const xvalue* Value = xrtValueObjectGet(Object, xrtStrView(Key));
    return Value != NULL && xrtValueType(Value) == XVALUE_STRING &&
        xrtValueGetString(Value, Result);
}

static bool MdoProjectsValueUInt(const xvalue* Object, const char* Key,
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

static bool MdoProjectsValueInt(const xvalue* Object, const char* Key,
    int64* Result)
{
    uint64 Unsigned;
    const xvalue* Value = xrtValueObjectGet(Object, xrtStrView(Key));
    if ( Value == NULL ) return false;
    if ( xrtValueType(Value) == XVALUE_INT )
        return xrtValueGetInt(Value, Result);
    if ( xrtValueType(Value) != XVALUE_UINT ||
         !xrtValueGetUInt(Value, &Unsigned) || Unsigned > INT64_MAX )
        return false;
    *Result = (int64)Unsigned;
    return true;
}

static bool MdoProjectsParse(const char* ExpectedId, xstrview Json,
    MdoProjectInfo* Info)
{
    xjsonreadconfig Config;
    xvalue* Root;
    xstrview Id, Name, Workspace, Model;
    uint64 Schema, Revision;
    int64 Created, Updated;
    bool Ok;
    xrtJsonReadConfigInit(&Config);
    Config.MaxInputBytes = MDO_PROJECT_STORE_LIMIT;
    Config.MaxDepth = 4u;
    Config.MaxValues = 32u;
    Config.MaxContainerItems = 16u;
    Root = xrtJsonRead(Json, &Config);
    Ok = Root != NULL && xrtValueType(Root) == XVALUE_OBJECT &&
        xrtValueCount(Root) == 8u &&
        MdoProjectsValueUInt(Root, "schema_version", &Schema) &&
        Schema == MDO_PROJECT_SCHEMA_VERSION &&
        MdoProjectsValueString(Root, "id", &Id) &&
        MdoProjectsValueString(Root, "name", &Name) &&
        MdoProjectsValueString(Root, "workspace_root", &Workspace) &&
        MdoProjectsValueString(Root, "default_model_id", &Model) &&
        MdoProjectsValueUInt(Root, "revision", &Revision) &&
        Revision != 0u &&
        MdoProjectsValueInt(Root, "created_at_us", &Created) &&
        MdoProjectsValueInt(Root, "updated_at_us", &Updated) &&
        Created > 0 && Updated >= Created;
    if ( Ok ) {
        memset(Info, 0, sizeof(*Info));
        Info->Size = sizeof(*Info);
        Ok = MdoProjectsCopyView(Info->Id, sizeof(Info->Id), Id, false) &&
            strcmp(Info->Id, ExpectedId) == 0 && MdoProjectsId(Info->Id) &&
            MdoProjectsCopyView(Info->Name, sizeof(Info->Name), Name, false) &&
            MdoProjectsCopyView(Info->WorkspaceRoot,
                sizeof(Info->WorkspaceRoot), Workspace, false) &&
            MdoProjectsCopyView(Info->DefaultModelId,
                sizeof(Info->DefaultModelId), Model, true) &&
            (Info->DefaultModelId[0] == '\0' ||
             MdoProjectsId(Info->DefaultModelId));
        Info->Revision = Revision;
        Info->CreatedAt = Created;
        Info->UpdatedAt = Updated;
    }
    xrtValueRelease(Root);
    return Ok;
}

static bool MdoProjectsRead(const char* Id, MdoProjectInfo* Info,
    xwork_error* Error)
{
    char Path[96];
    xfile File = NULL;
    xfileinfo Stat;
    char* Bytes = NULL;
    bool Ok = false;
    if ( !MdoProjectsPath(Id, Path) ) goto done;
    File = MdoHomeOpenRead(Path);
    if ( File == NULL || !xrtFileStat(File, &Stat) ||
         Stat.Type != XFILE_TYPE_FILE ||
         (Stat.Available & XFILE_INFO_SIZE) == 0u ||
         Stat.Size == 0u || Stat.Size > MDO_PROJECT_STORE_LIMIT ) goto done;
    Bytes = (char*)xrtMalloc((size_t)Stat.Size + 1u);
    if ( Bytes == NULL ||
         !xrtReadFull(File, Bytes, (size_t)Stat.Size, NULL) ) goto done;
    Bytes[Stat.Size] = '\0';
    Ok = MdoProjectsParse(Id, xrtStrViewN(Bytes, (size_t)Stat.Size), Info);
done:
    if ( File != NULL && !xrtClose(File) ) Ok = false;
    xrtFree(Bytes);
    if ( !Ok ) MdoProjectsError(Error, XWORK_ERROR_IO,
        "project definition is unreadable or invalid");
    return Ok;
}

static bool MdoProjectsObjectString(xvalue* Root, const char* Key,
    const char* Value)
{
    return xrtValueObjectSetNew(Root, xrtStrView(Key),
        xrtValueString(xrtStrView(Value)));
}

static bool MdoProjectsObjectUInt(xvalue* Root, const char* Key, uint64 Value)
{
    return xrtValueObjectSetNew(Root, xrtStrView(Key), xrtValueUInt(Value));
}

static bool MdoProjectsObjectInt(xvalue* Root, const char* Key, int64 Value)
{
    return xrtValueObjectSetNew(Root, xrtStrView(Key), xrtValueInt(Value));
}

static char* MdoProjectsJson(const MdoProjectInfo* Info, size_t* Size)
{
    xvalue* Root = xrtValueObject();
    char* Json = NULL;
    if ( Root != NULL &&
         MdoProjectsObjectUInt(Root, "schema_version",
            MDO_PROJECT_SCHEMA_VERSION) &&
         MdoProjectsObjectString(Root, "id", Info->Id) &&
         MdoProjectsObjectString(Root, "name", Info->Name) &&
         MdoProjectsObjectString(Root, "workspace_root",
            Info->WorkspaceRoot) &&
         MdoProjectsObjectString(Root, "default_model_id",
            Info->DefaultModelId) &&
         MdoProjectsObjectUInt(Root, "revision", Info->Revision) &&
         MdoProjectsObjectInt(Root, "created_at_us", Info->CreatedAt) &&
         MdoProjectsObjectInt(Root, "updated_at_us", Info->UpdatedAt) )
        Json = xrtJsonStringify(Root, true, Size);
    xrtValueRelease(Root);
    if ( Json != NULL && *Size > MDO_PROJECT_STORE_LIMIT ) {
        xrtFree(Json);
        Json = NULL;
    }
    return Json;
}

void MdoProjectCreateOptionsInit(MdoProjectCreateOptions* Options)
{
    if ( Options == NULL ) return;
    memset(Options, 0, sizeof(*Options));
    Options->Size = sizeof(*Options);
}

bool MdoProjectGet(const char* Id, MdoProjectInfo* Info, bool* Found,
    xwork_error* Error)
{
    char Path[96];
    xfileinfo Stat;
    xworkErrorInit(Error);
    if ( Found != NULL ) *Found = false;
    if ( !MdoProjectsId(Id) || Info == NULL ||
         Info->Size < sizeof(*Info) || Found == NULL ||
         !MdoProjectsPath(Id, Path) ) {
        MdoProjectsError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "invalid project read request");
        return false;
    }
    if ( !MdoHomeExternalStat(Path, Found, &Stat) ) {
        MdoProjectsError(Error, XWORK_ERROR_IO,
            "cannot inspect project definition");
        return false;
    }
    if ( !*Found ) return true;
    return MdoProjectsRead(Id, Info, Error);
}

static int MdoProjectsCompare(const void* Left, const void* Right)
{
    return strcmp(((const MdoProjectInfo*)Left)->Id,
        ((const MdoProjectInfo*)Right)->Id);
}

bool MdoProjectList(MdoProjectInfo* Items, size_t Capacity, size_t* Count,
    size_t* InvalidCount, bool* Truncated, xwork_error* Error)
{
    xfileinfo Stat;
    xdir Directory = NULL;
    xdirentry Entry;
    xdirnext Next = XDIR_NEXT_END;
    size_t Scanned = 0u;
    bool Exists = false;
    bool Ok = false;
    xworkErrorInit(Error);
    if ( Count != NULL ) *Count = 0u;
    if ( InvalidCount != NULL ) *InvalidCount = 0u;
    if ( Truncated != NULL ) *Truncated = false;
    if ( Items == NULL || Capacity == 0u || Capacity > MDO_PROJECT_LIST_LIMIT ||
         Count == NULL || InvalidCount == NULL || Truncated == NULL ) {
        MdoProjectsError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "invalid project listing request");
        return false;
    }
    if ( !MdoHomeExternalStat("projects", &Exists, &Stat) ) goto done;
    if ( !Exists ) return true;
    if ( Stat.Type != XFILE_TYPE_DIRECTORY ) goto done;
    Directory = MdoHomeOpenDirectory("projects", XDIR_STAT);
    if ( Directory == NULL ) goto done;
    while ( (Next = xrtDirNext(Directory, &Entry)) == XDIR_NEXT_ITEM ) {
        char Id[MDO_PROJECT_ID_CAPACITY];
        size_t NameSize;
        if ( ++Scanned > MDO_PROJECT_SCAN_LIMIT ) {
            *Truncated = true;
            break;
        }
        if ( Entry.Info.Type != XFILE_TYPE_FILE || Entry.Name.Size < 6u ||
             memcmp(Entry.Name.Data + Entry.Name.Size - 5u, ".json", 5u) != 0 )
            continue;
        NameSize = Entry.Name.Size - 5u;
        if ( NameSize >= sizeof(Id) || NameSize == 0u ) {
            ++*InvalidCount;
            continue;
        }
        memcpy(Id, Entry.Name.Data, NameSize);
        Id[NameSize] = '\0';
        if ( !MdoProjectsId(Id) ) {
            ++*InvalidCount;
            continue;
        }
        if ( *Count == Capacity ) {
            *Truncated = true;
            break;
        }
        Items[*Count].Size = sizeof(Items[*Count]);
        if ( !MdoProjectsRead(Id, &Items[*Count], NULL) ) {
            ++*InvalidCount;
            continue;
        }
        ++*Count;
    }
    Ok = Next == XDIR_NEXT_END || *Truncated;
    if ( Ok ) qsort(Items, *Count, sizeof(*Items), MdoProjectsCompare);
done:
    if ( Directory != NULL && !xrtDirClose(Directory) ) Ok = false;
    if ( !Ok ) MdoProjectsError(Error, XWORK_ERROR_IO,
        "cannot enumerate project definitions");
    return Ok;
}

bool MdoProjectCreate(const MdoProjectCreateOptions* Options,
    MdoProjectInfo* Info, xwork_error* Error)
{
    MdoProjectInfo Candidate;
    char Path[96];
    xfile Lock = NULL;
    xfileinfo Stat;
    char* Json = NULL;
    size_t Size = 0u;
    bool Exists = false;
    bool Ok = false;
    const char* Workspace;
    const char* Model;
    xworkErrorInit(Error);
    if ( Options == NULL || Options->Size < sizeof(*Options) ||
         (Info != NULL && Info->Size < sizeof(*Info)) ) {
        MdoProjectsError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "invalid project create request");
        return false;
    }
    Workspace = Options->WorkspaceRoot != NULL &&
        Options->WorkspaceRoot[0] != '\0' ? Options->WorkspaceRoot : ".";
    Model = Options->DefaultModelId != NULL ? Options->DefaultModelId : "";
    if ( !MdoProjectsId(Options->Id) ||
         !MdoProjectsText(Options->Name, MDO_PROJECT_NAME_CAPACITY, false) ||
         !MdoProjectsText(Workspace, MDO_PROJECT_WORKSPACE_CAPACITY, false) ||
         !MdoProjectsText(Model, MDO_PROJECT_MODEL_CAPACITY, true) ||
         (Model[0] != '\0' && !MdoProjectsId(Model)) ||
         !MdoProjectsPath(Options->Id, Path) ) {
        MdoProjectsError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "project fields are invalid or exceed their bounds");
        return false;
    }
    memset(&Candidate, 0, sizeof(Candidate));
    Candidate.Size = sizeof(Candidate);
    snprintf(Candidate.Id, sizeof(Candidate.Id), "%s", Options->Id);
    snprintf(Candidate.Name, sizeof(Candidate.Name), "%s", Options->Name);
    snprintf(Candidate.WorkspaceRoot, sizeof(Candidate.WorkspaceRoot),
        "%s", Workspace);
    snprintf(Candidate.DefaultModelId, sizeof(Candidate.DefaultModelId),
        "%s", Model);
    Candidate.Revision = 1u;
    Candidate.CreatedAt = xrtNow();
    Candidate.UpdatedAt = Candidate.CreatedAt;
    Json = MdoProjectsJson(&Candidate, &Size);
    if ( Json == NULL ) {
        MdoProjectsError(Error, XWORK_ERROR_OUT_OF_MEMORY,
            "cannot serialize project definition");
        return false;
    }
    Lock = MdoHomeOpenWrite("projects/.writer.lock",
        XFILE_READ | XFILE_CREATE | XFILE_SYNC);
    if ( Lock == NULL || !xrtFileLock(Lock, XFILE_LOCK_EXCLUSIVE, false) ) {
        MdoProjectsError(Error, XWORK_ERROR_CONTEXT,
            "project store is locked by another process");
        goto done;
    }
    if ( !MdoHomeExternalStat(Path, &Exists, &Stat) ) {
        MdoProjectsError(Error, XWORK_ERROR_IO,
            "cannot inspect project definition");
        goto done;
    }
    if ( Exists ) {
        MdoProjectsError(Error, XWORK_ERROR_CONTEXT,
            "project ID already exists");
        goto done;
    }
    if ( !MdoHomeAtomicWrite(Path, Json, Size, false) ) {
        MdoProjectsError(Error, XWORK_ERROR_IO,
            "cannot publish project definition");
        goto done;
    }
    if ( Info != NULL ) {
        uint32 OutputSize = Info->Size;
        *Info = Candidate;
        Info->Size = OutputSize;
    }
    Ok = true;
done:
    if ( Lock != NULL ) (void)xrtClose(Lock);
    xrtFree(Json);
    return Ok;
}
