#include <stdio.h>
#include <string.h>

#include "../../include/mdo/home.h"
#include "../../include/mdo/sessions.h"
#include "../../include/mdo/project_lifecycle.h"
#include "data_gate.h"

#define MDO_TODO_FILE_MAX (16u * 1024u)
#define MDO_TODO_INPUT_MAX 12288u
#define MDO_TODO_ITEMS_MAX 24u
#define MDO_TODO_TEXT_MAX 1024u

static bool MdoTodoIdValid(const char* Text, size_t Capacity)
{
    size_t i;
    size_t Size;
    if ( Text == NULL || Text[0] == '\0' ) return false;
    Size = strlen(Text);
    if ( Size >= Capacity || Text[0] == '.' ) return false;
    for ( i = 0u; i < Size; ++i ) {
        unsigned char Byte = (unsigned char)Text[i];
        if ( (Byte >= 'a' && Byte <= 'z') ||
             (Byte >= 'A' && Byte <= 'Z') ||
             (Byte >= '0' && Byte <= '9') || Byte == '-' || Byte == '_' ||
             Byte == '.' ) continue;
        return false;
    }
    return true;
}

static bool MdoTodoPath(char Path[MDO_SESSION_PATH_CAPACITY],
    const char* ProjectId, const char* SessionId)
{
    int Written;
    if ( !MdoTodoIdValid(ProjectId, MDO_PROJECT_ID_CAPACITY) ||
         !MdoTodoIdValid(SessionId, MDO_SESSION_ID_CAPACITY) ) return false;
    Written = snprintf(Path, MDO_SESSION_PATH_CAPACITY,
        "sessions/%s/%s/todo.json", ProjectId, SessionId);
    return Written > 0 && (size_t)Written < MDO_SESSION_PATH_CAPACITY;
}

static bool MdoTodoUInt(const xvalue* Root, const char* Name,
    uint64* Result)
{
    const xvalue* Value = xrtValueObjectGet(Root, xrtStrView(Name));
    int64 Signed;
    if ( xrtValueType(Value) == XVALUE_UINT )
        return xrtValueGetUInt(Value, Result);
    if ( xrtValueType(Value) != XVALUE_INT ||
         !xrtValueGetInt(Value, &Signed) || Signed < 0 ) return false;
    *Result = (uint64)Signed;
    return true;
}

static bool MdoTodoObjectTake(xvalue* Root, const char* Name,
    xvalue* Value)
{
    bool Ok = Root != NULL && Value != NULL &&
        xrtValueObjectSetTake(Root, xrtStrView(Name), &Value);
    xrtValueRelease(Value);
    return Ok;
}

static bool MdoTodoValid(const xvalue* Root, bool Stored,
    uint64* EventId)
{
    const xvalue* Items = xrtValueObjectGet(Root,
        XRT_STR_LITERAL("items"));
    size_t i;
    uint64 Version;
    if ( xrtValueType(Root) != XVALUE_OBJECT ||
         xrtValueCount(Root) != (Stored ? 3u : 1u) ||
         xrtValueType(Items) != XVALUE_ARRAY ||
         xrtValueCount(Items) > MDO_TODO_ITEMS_MAX ) return false;
    if ( Stored && (!MdoTodoUInt(Root, "schema_version", &Version) ||
         Version != 1u || !MdoTodoUInt(Root, "event_id", EventId)) )
        return false;
    for ( i = 0u; i < xrtValueCount(Items); ++i ) {
        const xvalue* Item = xrtValueArrayGet(Items, i);
        const xvalue* Text = xrtValueObjectGet(Item,
            XRT_STR_LITERAL("text"));
        const xvalue* Done = xrtValueObjectGet(Item,
            XRT_STR_LITERAL("done"));
        xstrview View;
        bool Value;
        if ( xrtValueType(Item) != XVALUE_OBJECT ||
             xrtValueCount(Item) != 2u ||
             xrtValueType(Text) != XVALUE_STRING ||
             !xrtValueGetString(Text, &View) || View.Size == 0u ||
             View.Size > MDO_TODO_TEXT_MAX ||
             memchr(View.Data, 0, View.Size) != NULL ||
             !xrtUtf8Valid(View, NULL) ||
             xrtValueType(Done) != XVALUE_BOOL ||
             !xrtValueGetBool(Done, &Value) ) return false;
    }
    return true;
}

static xvalue* MdoTodoParse(xstrview Json, bool Stored)
{
    xjsonreadconfig Config;
    xvalue* Root;
    uint64 EventId = 0u;
    xrtJsonReadConfigInit(&Config);
    Config.MaxInputBytes = Stored ? MDO_TODO_FILE_MAX : MDO_TODO_INPUT_MAX;
    Config.MaxDepth = 4u;
    Config.MaxValues = 80u;
    Config.MaxContainerItems = MDO_TODO_ITEMS_MAX;
    Root = xrtJsonRead(Json, &Config);
    if ( !MdoTodoValid(Root, Stored, &EventId) ) {
        xrtValueRelease(Root);
        return NULL;
    }
    return Root;
}

static xvalue* MdoTodoEmpty(void)
{
    xvalue* Root = xrtValueObject();
    if ( Root == NULL ||
         !MdoTodoObjectTake(Root, "schema_version", xrtValueUInt(1u)) ||
         !MdoTodoObjectTake(Root, "event_id", xrtValueUInt(0u)) ||
         !MdoTodoObjectTake(Root, "items", xrtValueArray()) ) {
        xrtValueRelease(Root);
        return NULL;
    }
    return Root;
}

bool MdoSessionTodoProject(const char* ProjectId, const char* SessionId,
    uint64 EventId, const xwork_event* Event)
{
    char Path[MDO_SESSION_PATH_CAPACITY];
    xvalue* Root = NULL;
    char* Json = NULL;
    size_t Size = 0u;
    bool Ok = false;
    MdoProjectLease* Lease = NULL;
    MdoSessionDataLease* DataLease = NULL;
    if ( Event == NULL || Event->eKind != XWORK_EVENT_TOOL_DONE ||
         !Event->bSuccess || Event->uAgentDepth != 0u ||
         Event->sToolName == NULL ||
         strcmp(Event->sToolName, "mdo.todo") != 0 ) return true;
    if ( EventId == 0u || Event->sText == NULL ||
         Event->iTextLength == 0u ||
         Event->iTextLength > MDO_TODO_INPUT_MAX ||
         Event->bTextTruncated ||
         !MdoTodoPath(Path, ProjectId, SessionId) ) return false;
    Lease = MdoProjectLeaseAcquire(ProjectId, MDO_PROJECT_LEASE_SHARED, NULL);
    if ( Lease == NULL ) return false;
    DataLease = MdoSessionDataAcquire(ProjectId, SessionId,
        MDO_SESSION_DATA_WRITE, NULL);
    if ( DataLease == NULL ) goto done;
    Root = MdoTodoParse(xrtStrViewN(Event->sText,
        Event->iTextLength), false);
    if ( Root == NULL ||
         !MdoTodoObjectTake(Root, "schema_version", xrtValueUInt(1u)) ||
         !MdoTodoObjectTake(Root, "event_id", xrtValueUInt(EventId)) )
        goto done;
    Json = xrtJsonStringify(Root, false, &Size);
    Ok = Json != NULL && Size <= MDO_TODO_FILE_MAX &&
        MdoHomeAtomicWrite(Path, Json, Size, false);
done:
    xrtFree(Json);
    xrtValueRelease(Root);
    MdoProjectLeaseRelease(Lease);
    MdoSessionDataRelease(DataLease);
    return Ok;
}

/* A missing sidecar is the normal single-file state and must not create Home. */
bool MdoSessionTodoLoad(const char* ProjectId, const char* SessionId,
    xvalue** Output)
{
    char Path[MDO_SESSION_PATH_CAPACITY];
    xfileinfo Info;
    bool Exists = false;
    xfile File = NULL;
    char* Bytes = NULL;
    xvalue* Root = NULL;
    bool Ok = false;
    if ( Output == NULL || !MdoTodoPath(Path, ProjectId, SessionId) )
        return false;
    *Output = NULL;
    if ( !MdoHomeExternalStat(Path, &Exists, &Info) ) return false;
    if ( !Exists ) {
        *Output = MdoTodoEmpty();
        return *Output != NULL;
    }
    if ( Info.Type != XFILE_TYPE_FILE ||
         (Info.Available & XFILE_INFO_SIZE) == 0u ||
         Info.Size > MDO_TODO_FILE_MAX || Info.Size > SIZE_MAX - 1u )
        return false;
    File = MdoHomeOpenRead(Path);
    Bytes = (char*)xrtMalloc((size_t)Info.Size + 1u);
    if ( File == NULL || Bytes == NULL ||
         (Info.Size != 0u && !xrtReadFull(File, Bytes,
            (size_t)Info.Size, NULL)) ) goto done;
    Bytes[Info.Size] = '\0';
    Root = MdoTodoParse(xrtStrViewN(Bytes, (size_t)Info.Size), true);
    Ok = Root != NULL;
done:
    xrtFree(Bytes);
    if ( File != NULL && !xrtClose(File) ) Ok = false;
    if ( Ok ) *Output = Root;
    else xrtValueRelease(Root);
    return Ok;
}

bool MdoSessionTodoReset(const char* ProjectId, const char* SessionId)
{
    char Path[MDO_SESSION_PATH_CAPACITY];
    xvalue* Root = NULL;
    char* Json = NULL;
    size_t Size = 0u;
    bool Ok = false;
    MdoProjectLease* Lease = NULL;
    MdoSessionDataLease* DataLease = NULL;
    if ( !MdoTodoPath(Path, ProjectId, SessionId) ) return false;
    Lease = MdoProjectLeaseAcquire(ProjectId, MDO_PROJECT_LEASE_SHARED, NULL);
    if ( Lease == NULL ) return false;
    DataLease = MdoSessionDataAcquire(ProjectId, SessionId,
        MDO_SESSION_DATA_WRITE, NULL);
    if ( DataLease == NULL ) goto done;
    Root = MdoTodoEmpty();
    if ( Root == NULL ) goto done;
    Json = xrtJsonStringify(Root, false, &Size);
    Ok = Json != NULL && Size <= MDO_TODO_FILE_MAX &&
        MdoHomeAtomicWrite(Path, Json, Size, false);
done:
    xrtFree(Json);
    xrtValueRelease(Root);
    MdoProjectLeaseRelease(Lease);
    MdoSessionDataRelease(DataLease);
    return Ok;
}
