#include <stdlib.h>
#include <string.h>

#include "../../include/mdo/home.h"
#include "../../include/mdo/attachments.h"
#include "../../include/mdo/sessions.h"
#include "internal.h"

#define MDO_SESSION_META_LIMIT (64u * 1024u)
#define MDO_SESSION_EXPORT_SNAPSHOT_LIMIT (32u * 1024u * 1024u)
#define MDO_SESSION_SEARCH_DEFAULT 100u
#define MDO_SESSION_SEARCH_MAX 1000u
#define MDO_SESSION_SEARCH_TEXT_LIMIT 1024u
#define MDO_SESSION_CATALOG_MAX 10000u
#define MDO_SESSION_DIAGNOSTIC_MAX 1024u
#define MDO_SESSION_SOURCE_ROOT "sessions"

typedef struct MdoSessionActive {
    char ProjectId[MDO_PROJECT_ID_CAPACITY];
    char SessionId[MDO_SESSION_ID_CAPACITY];
} MdoSessionActive;

typedef struct MdoSessionManagerState {
    xmutex* Lock;
    xwork_runtime* Runtime;
    uint64 Generation;
    MdoSessionActive* Active;
    size_t ActiveCount;
    size_t ActiveCapacity;
    bool Initialized;
} MdoSessionManagerState;

struct MdoSession {
    xatomic32 Refs;
    xmutex* Lock;
    MdoAgentSession* Agent;
    MdoSessionEventBridge* Bridge;
    MdoProjectLease* ProjectLease;
    MdoSessionInfo Info;
    char MetaPath[MDO_SESSION_PATH_CAPACITY];
};

struct MdoSessionCatalog {
    xatomic32 Refs;
    uint64 Generation;
    MdoSessionInfo* Items;
    size_t Count;
    size_t Capacity;
    MdoSessionDiagnostic* Diagnostics;
    size_t DiagnosticCount;
    size_t DiagnosticCapacity;
    bool Truncated;
};

static MdoSessionManagerState g_MdoSessions;

static bool MdoSessionsValidateCurrent(MdoSession* Session,
    xwork_error* Error);
static bool MdoSessionsCommit(MdoSession* Session,
    const MdoSessionInfo* Candidate, xwork_error* Error);

static void MdoSessionsError(xwork_error* Error, xwork_error_code Code,
    const char* Message)
{
    if ( Error == NULL ) return;
    xworkErrorInit(Error);
    Error->eCode = Code;
    snprintf(Error->sMessage, sizeof(Error->sMessage), "%s",
        Message != NULL && Message[0] != '\0' ? Message :
        "session operation failed");
}

static void MdoSessionsXrtError(xwork_error* Error, xwork_error_code Code,
    const char* Fallback)
{
    const xerror* Cause = xrtGetError();
    MdoSessionsError(Error, Code, Cause != NULL &&
        xrtErrorMessage(Cause) != NULL ? xrtErrorMessage(Cause) : Fallback);
}

static bool MdoSessionsGrow(void** Items, size_t* Capacity, size_t Count,
    size_t ItemSize)
{
    size_t Next;
    void* Value;
    if ( Count <= *Capacity ) return true;
    Next = *Capacity != 0u ? *Capacity : 8u;
    while ( Next < Count ) {
        if ( Next > SIZE_MAX / 2u ) return false;
        Next *= 2u;
    }
    if ( Next > SIZE_MAX / ItemSize ) return false;
    Value = xrtRealloc(*Items, Next * ItemSize);
    if ( Value == NULL ) return false;
    *Items = Value;
    *Capacity = Next;
    return true;
}

static bool MdoSessionsIdValid(const char* Text, size_t Capacity)
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

static bool MdoSessionsTextValid(const char* Text, size_t Capacity,
    bool EmptyAllowed)
{
    size_t Size;
    if ( Text == NULL ) return false;
    Size = strlen(Text);
    return (EmptyAllowed || Size != 0u) && Size < Capacity &&
        xrtUtf8Valid(xrtStrViewN(Text, Size), NULL);
}

static bool MdoSessionsCopy(char* Target, size_t Capacity, xstrview Text,
    bool EmptyAllowed)
{
    if ( Text.Size >= Capacity || (!EmptyAllowed && Text.Size == 0u) ||
         !xrtUtf8Valid(Text, NULL) ) return false;
    memcpy(Target, Text.Data, Text.Size);
    Target[Text.Size] = '\0';
    return true;
}

static bool MdoSessionsPath(char Output[MDO_SESSION_PATH_CAPACITY],
    const char* ProjectId, const char* SessionId, const char* Name)
{
    int Written = snprintf(Output, MDO_SESSION_PATH_CAPACITY,
        MDO_SESSION_SOURCE_ROOT "/%s/%s/%s", ProjectId, SessionId, Name);
    return Written > 0 && (size_t)Written < MDO_SESSION_PATH_CAPACITY;
}

static bool MdoSessionsDirectory(char Output[MDO_SESSION_PATH_CAPACITY],
    const char* ProjectId, const char* SessionId)
{
    int Written = snprintf(Output, MDO_SESSION_PATH_CAPACITY,
        MDO_SESSION_SOURCE_ROOT "/%s/%s", ProjectId, SessionId);
    return Written > 0 && (size_t)Written < MDO_SESSION_PATH_CAPACITY;
}

static void MdoSessionsRollbackDirectory(const char* ProjectId,
    const char* SessionId, const char* DirectoryPath)
{
    static const char* const Files[] = {
        "journal.jsonl", "snapshot.json", "meta.json", "ui-events.jsonl",
        "todo.json",
        ".runtime.lock"
    };
    char Relative[MDO_SESSION_PATH_CAPACITY];
    size_t i;
    MdoSessionAttachmentForkRollback(ProjectId, SessionId);
    for ( i = 0u; i < sizeof(Files) / sizeof(Files[0]); ++i ) {
        if ( MdoSessionsPath(Relative, ProjectId, SessionId, Files[i]) )
            (void)MdoHomeRemove(Relative, false);
        xrtClearError();
    }
    if ( MdoSessionsPath(Relative, ProjectId, SessionId, "artifacts") )
        (void)MdoHomeRemoveEmptyDirectory(Relative);
    xrtClearError();
    (void)MdoHomeRemoveEmptyDirectory(DirectoryPath);
    xrtClearError();
}

static const char* MdoSessionsStatusName(MdoSessionStatus Status)
{
    if ( Status == MDO_SESSION_ACTIVE ) return "active";
    if ( Status == MDO_SESSION_ARCHIVED ) return "archived";
    if ( Status == MDO_SESSION_TRASH ) return "trash";
    return NULL;
}

static MdoSessionStatus MdoSessionsStatusParse(xstrview Text)
{
    if ( Text.Size == 6u && memcmp(Text.Data, "active", 6u) == 0 )
        return MDO_SESSION_ACTIVE;
    if ( Text.Size == 8u && memcmp(Text.Data, "archived", 8u) == 0 )
        return MDO_SESSION_ARCHIVED;
    if ( Text.Size == 5u && memcmp(Text.Data, "trash", 5u) == 0 )
        return MDO_SESSION_TRASH;
    return 0;
}

static MdoModelProtocol MdoSessionsProtocolParse(xstrview Text)
{
    MdoModelProtocol Protocol;
    for ( Protocol = MDO_MODEL_PROTOCOL_OPENAI_CHAT_COMPLETIONS;
          Protocol <= MDO_MODEL_PROTOCOL_ANTHROPIC_MESSAGES;
          Protocol = (MdoModelProtocol)((int)Protocol + 1) ) {
        const char* Name = MdoModelProtocolName(Protocol);
        if ( Name != NULL && Text.Size == strlen(Name) &&
             memcmp(Text.Data, Name, Text.Size) == 0 ) return Protocol;
    }
    return 0;
}

static bool MdoSessionsObjectTake(xvalue* Object, const char* Key,
    xvalue* Value)
{
    bool Ok = Object != NULL && Value != NULL &&
        xrtValueObjectSetTake(Object, xrtStrView(Key), &Value);
    xrtValueRelease(Value);
    return Ok;
}

static bool MdoSessionsObjectString(xvalue* Object, const char* Key,
    const char* Text)
{
    return MdoSessionsObjectTake(Object, Key,
        xrtValueString(xrtStrView(Text != NULL ? Text : "")));
}

char* MdoSessionsInternalMetaJson(const MdoSessionInfo* Info, size_t* Size)
{
    xvalue* Object = xrtValueObject();
    const char* Status = MdoSessionsStatusName(Info->Status);
    const char* Previous = MdoSessionsStatusName(Info->PreviousStatus);
    const char* Protocol = MdoModelProtocolName(Info->Protocol);
    str Json = NULL;
    if ( Object == NULL || Status == NULL || Previous == NULL ||
         Protocol == NULL ||
         !MdoSessionsObjectTake(Object, "schema_version",
            xrtValueUInt(MDO_SESSION_SCHEMA_VERSION)) ||
         !MdoSessionsObjectTake(Object, "revision",
            xrtValueUInt(Info->Revision)) ||
         !MdoSessionsObjectString(Object, "id", Info->Id) ||
         !MdoSessionsObjectString(Object, "project_id", Info->ProjectId) ||
         !MdoSessionsObjectString(Object, "title", Info->Title) ||
         !MdoSessionsObjectString(Object, "agent_id", Info->AgentId) ||
         !MdoSessionsObjectString(Object, "model_id", Info->ModelId) ||
         !MdoSessionsObjectString(Object, "protocol", Protocol) ||
         !MdoSessionsObjectString(Object, "reasoning_effort",
            Info->ReasoningEffort) ||
         !MdoSessionsObjectString(Object, "permission_profile",
            Info->PermissionProfile) ||
         !MdoSessionsObjectTake(Object, "max_output_tokens",
            xrtValueUInt(Info->MaxOutputTokens)) ||
         !MdoSessionsObjectString(Object, "workspace_root",
            Info->WorkspaceRoot) ||
         !MdoSessionsObjectTake(Object, "created_at_us",
            xrtValueInt(Info->CreatedAt)) ||
         !MdoSessionsObjectTake(Object, "updated_at_us",
            xrtValueInt(Info->UpdatedAt)) ||
         !MdoSessionsObjectString(Object, "status", Status) ||
         !MdoSessionsObjectString(Object, "previous_status", Previous) ||
         !MdoSessionsObjectTake(Object, "pinned", xrtValueBool(Info->Pinned)) ||
         !MdoSessionsObjectTake(Object, "config_revision",
            xrtValueUInt(Info->ConfigRevision)) ||
         !MdoSessionsObjectTake(Object, "model_generation",
            xrtValueUInt(Info->ModelGeneration)) ||
         !MdoSessionsObjectTake(Object, "module_generation",
            xrtValueUInt(Info->ModuleGeneration)) ||
         !MdoSessionsObjectTake(Object, "skill_generation",
            xrtValueUInt(Info->SkillGeneration)) ||
         !MdoSessionsObjectString(Object, "parent_session_id",
            Info->ParentSessionId) ||
         !MdoSessionsObjectTake(Object, "forked_through_sequence",
            xrtValueUInt(Info->ForkedThroughSequence)) ) goto done;
    Json = xrtJsonStringify(Object, true, Size);
done:
    xrtValueRelease(Object);
    return Json;
}

static bool MdoSessionsValueString(const xvalue* Object, const char* Key,
    xstrview* Text)
{
    const xvalue* Value = xrtValueObjectGet(Object, xrtStrView(Key));
    return Value != NULL && xrtValueType(Value) == XVALUE_STRING &&
        xrtValueGetString(Value, Text);
}

static bool MdoSessionsValueUInt(const xvalue* Object, const char* Key,
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

static bool MdoSessionsReadBounded(const char* Path, size_t Limit,
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
         Info.Size > Limit || Info.Size > SIZE_MAX - 1u )
        goto done;
    Bytes = (char*)xrtMalloc((size_t)Info.Size + 1u);
    if ( Bytes == NULL ||
         (Info.Size != 0u && !xrtReadFull(File, Bytes,
            (size_t)Info.Size, NULL)) ) goto done;
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

bool MdoSessionsInternalMetaParse(const char* ExpectedProject,
    const char* ExpectedId, xstrview Json, MdoSessionInfo* Info)
{
    xjsonreadconfig Config;
    xvalue* Root;
    xstrview Id;
    xstrview Project;
    xstrview Title;
    xstrview Agent;
    xstrview Model;
    xstrview Protocol;
    xstrview Reasoning;
    xstrview Permission;
    xstrview Workspace;
    xstrview Status;
    xstrview Previous;
    xstrview Parent;
    uint64 Schema;
    uint64 Revision;
    uint64 MaxOutput;
    uint64 Created;
    uint64 Updated;
    uint64 ConfigRevision;
    uint64 ModelGeneration;
    uint64 ModuleGeneration;
    uint64 SkillGeneration;
    uint64 ForkedThrough;
    bool Pinned;
    const xvalue* PinnedValue;
    bool Ok = false;

    xrtJsonReadConfigInit(&Config);
    Config.MaxInputBytes = MDO_SESSION_META_LIMIT;
    Config.MaxDepth = 8u;
    Config.MaxValues = 32u;
    Config.MaxContainerItems = 24u;
    Root = xrtJsonRead(Json, &Config);
    memset(Info, 0, sizeof(*Info));
    Info->Size = sizeof(*Info);
    PinnedValue = Root != NULL ? xrtValueObjectGet(Root,
        xrtStrView("pinned")) : NULL;
    if ( Root == NULL || xrtValueType(Root) != XVALUE_OBJECT ||
         !MdoSessionsValueUInt(Root, "schema_version", &Schema) ||
         (Schema != 1u && Schema != 2u &&
          Schema != MDO_SESSION_SCHEMA_VERSION) ||
         ((Schema == 1u && xrtValueCount(Root) != 20u) ||
          (Schema == 2u && xrtValueCount(Root) != 22u) ||
          (Schema == MDO_SESSION_SCHEMA_VERSION &&
           xrtValueCount(Root) != 23u)) ||
         !MdoSessionsValueUInt(Root, "revision", &Revision) ||
         Revision == 0u ||
         !MdoSessionsValueString(Root, "id", &Id) ||
         !MdoSessionsValueString(Root, "project_id", &Project) ||
         !MdoSessionsValueString(Root, "title", &Title) ||
         !MdoSessionsValueString(Root, "agent_id", &Agent) ||
         !MdoSessionsValueString(Root, "model_id", &Model) ||
         !MdoSessionsValueString(Root, "protocol", &Protocol) ||
         !MdoSessionsValueString(Root, "reasoning_effort", &Reasoning) ||
         !MdoSessionsValueUInt(Root, "max_output_tokens", &MaxOutput) ||
         MaxOutput == 0u || MaxOutput > UINT32_MAX ||
         !MdoSessionsValueString(Root, "workspace_root", &Workspace) ||
         !MdoSessionsValueUInt(Root, "created_at_us", &Created) ||
         Created > INT64_MAX ||
         !MdoSessionsValueUInt(Root, "updated_at_us", &Updated) ||
         Updated > INT64_MAX || Updated < Created ||
         !MdoSessionsValueString(Root, "status", &Status) ||
         !MdoSessionsValueString(Root, "previous_status", &Previous) ||
         PinnedValue == NULL || xrtValueType(PinnedValue) != XVALUE_BOOL ||
         !xrtValueGetBool(PinnedValue, &Pinned) ||
         !MdoSessionsValueUInt(Root, "config_revision", &ConfigRevision) ||
         !MdoSessionsValueUInt(Root, "model_generation", &ModelGeneration) ||
         !MdoSessionsValueUInt(Root, "module_generation", &ModuleGeneration) ||
         !MdoSessionsValueUInt(Root, "skill_generation", &SkillGeneration) ||
         !MdoSessionsCopy(Info->Id, sizeof(Info->Id), Id, false) ||
         !MdoSessionsCopy(Info->ProjectId, sizeof(Info->ProjectId),
            Project, false) ||
         !MdoSessionsCopy(Info->Title, sizeof(Info->Title), Title, true) ||
         !MdoSessionsCopy(Info->AgentId, sizeof(Info->AgentId), Agent, false) ||
         !MdoSessionsCopy(Info->ModelId, sizeof(Info->ModelId), Model, false) ||
         !MdoSessionsCopy(Info->ReasoningEffort,
            sizeof(Info->ReasoningEffort), Reasoning, false) ||
         !MdoSessionsCopy(Info->WorkspaceRoot,
            sizeof(Info->WorkspaceRoot), Workspace, false) ) goto done;
    if ( Schema >= 2u ) {
        if ( !MdoSessionsValueString(Root, "parent_session_id", &Parent) ||
             !MdoSessionsValueUInt(Root, "forked_through_sequence",
                &ForkedThrough) ||
             !MdoSessionsCopy(Info->ParentSessionId,
                sizeof(Info->ParentSessionId), Parent, true) ||
             (Info->ParentSessionId[0] == '\0' && ForkedThrough != 0u) ||
             (Info->ParentSessionId[0] != '\0' &&
              !MdoSessionsIdValid(Info->ParentSessionId,
                sizeof(Info->ParentSessionId))) ) goto done;
        Info->ForkedThroughSequence = ForkedThrough;
    }
    if ( Schema >= 3u ) {
        if ( !MdoSessionsValueString(Root, "permission_profile", &Permission) ||
             !MdoSessionsCopy(Info->PermissionProfile,
                sizeof(Info->PermissionProfile), Permission, true) ) goto done;
    }
    Info->Status = MdoSessionsStatusParse(Status);
    Info->PreviousStatus = MdoSessionsStatusParse(Previous);
    Info->Protocol = MdoSessionsProtocolParse(Protocol);
    if ( Info->Status == 0 ||
         (Info->PreviousStatus != MDO_SESSION_ACTIVE &&
          Info->PreviousStatus != MDO_SESSION_ARCHIVED) ||
         Info->Protocol == 0 ||
         !MdoSessionsIdValid(Info->Id, sizeof(Info->Id)) ||
         !MdoSessionsIdValid(Info->ProjectId, sizeof(Info->ProjectId)) ||
         strcmp(Info->Id, ExpectedId) != 0 ||
         strcmp(Info->ProjectId, ExpectedProject) != 0 ) goto done;
    Info->Revision = Revision;
    Info->CreatedAt = (int64)Created;
    Info->UpdatedAt = (int64)Updated;
    Info->Pinned = Pinned;
    Info->MaxOutputTokens = (uint32)MaxOutput;
    Info->ConfigRevision = ConfigRevision;
    Info->ModelGeneration = ModelGeneration;
    Info->ModuleGeneration = ModuleGeneration;
    Info->SkillGeneration = SkillGeneration;
    Ok = true;
done:
    xrtValueRelease(Root);
    return Ok;
}

static bool MdoSessionsMetaRead(const char* ProjectId, const char* SessionId,
    MdoSessionInfo* Info)
{
    char Path[MDO_SESSION_PATH_CAPACITY];
    char* Data = NULL;
    size_t Size = 0u;
    bool Ok;
    if ( !MdoSessionsPath(Path, ProjectId, SessionId, "meta.json") ||
         !MdoSessionsReadBounded(Path, MDO_SESSION_META_LIMIT,
            &Data, &Size) ) return false;
    Ok = MdoSessionsInternalMetaParse(ProjectId, SessionId,
        xrtStrViewN(Data, Size), Info);
    xrtFree(Data);
    if ( !Ok ) {
        xerror* Error = xrtErrorCreate(XERR_PROTOCOL, "mdo.sessions", 1,
            "session meta.json does not satisfy schema version 1, 2 or 3");
        if ( Error != NULL ) xrtSetErrorTake(Error);
    }
    return Ok;
}

static bool MdoSessionsMetaWrite(const char* Path,
    const MdoSessionInfo* Info)
{
    char* Json;
    size_t Size = 0u;
    bool Ok;
    Json = MdoSessionsInternalMetaJson(Info, &Size);
    if ( Json == NULL ) return false;
    Ok = MdoHomeAtomicWrite(Path, Json, Size, true);
    xrtFree(Json);
    return Ok;
}

static MdoSession* MdoSessionsHandleCreate(MdoAgentSession* Agent,
    MdoSessionEventBridge* Bridge, MdoProjectLease* ProjectLease,
    const MdoSessionInfo* Info,
    const char* MetaPath)
{
    MdoSession* Session = (MdoSession*)xrtCalloc(1u, sizeof(*Session));
    if ( Session == NULL ) return NULL;
    xrtAtomic32Init(&Session->Refs, 1u);
    Session->Lock = xrtMutexCreate();
    if ( Session->Lock == NULL ) {
        xrtFree(Session);
        return NULL;
    }
    Session->ProjectLease = MdoProjectLeaseRef(ProjectLease);
    if ( Session->ProjectLease == NULL ||
         (Bridge != NULL && !MdoSessionEventBridgeRef(Bridge)) ) {
        MdoProjectLeaseRelease(Session->ProjectLease);
        xrtMutexDestroy(Session->Lock);
        xrtFree(Session);
        return NULL;
    }
    Session->Agent = Agent;
    Session->Bridge = Bridge;
    Session->Info = *Info;
    snprintf(Session->MetaPath, sizeof(Session->MetaPath), "%s", MetaPath);
    return Session;
}

static void MdoSessionsGenerationAdvance(void)
{
    if ( g_MdoSessions.Generation != UINT64_MAX )
        ++g_MdoSessions.Generation;
}

static size_t MdoSessionsActiveFind(const char* ProjectId,
    const char* SessionId)
{
    size_t i;
    for ( i = 0u; i < g_MdoSessions.ActiveCount; ++i ) {
        MdoSessionActive* Active = &g_MdoSessions.Active[i];
        if ( strcmp(Active->ProjectId, ProjectId) == 0 &&
             strcmp(Active->SessionId, SessionId) == 0 ) return i;
    }
    return SIZE_MAX;
}

static bool MdoSessionsActiveAdd(const char* ProjectId,
    const char* SessionId)
{
    MdoSessionActive* Active;
    if ( MdoSessionsActiveFind(ProjectId, SessionId) != SIZE_MAX )
        return false;
    if ( !MdoSessionsGrow((void**)&g_MdoSessions.Active,
            &g_MdoSessions.ActiveCapacity, g_MdoSessions.ActiveCount + 1u,
            sizeof(*g_MdoSessions.Active)) ) return false;
    Active = &g_MdoSessions.Active[g_MdoSessions.ActiveCount++];
    memset(Active, 0, sizeof(*Active));
    snprintf(Active->ProjectId, sizeof(Active->ProjectId), "%s", ProjectId);
    snprintf(Active->SessionId, sizeof(Active->SessionId), "%s", SessionId);
    MdoSessionsGenerationAdvance();
    return true;
}

void MdoSessionsInternalActiveRelease(const char* ProjectId,
    const char* SessionId)
{
    size_t Index;
    if ( !g_MdoSessions.Initialized || g_MdoSessions.Lock == NULL ) return;
    xrtMutexLock(g_MdoSessions.Lock);
    Index = MdoSessionsActiveFind(ProjectId, SessionId);
    if ( Index != SIZE_MAX ) {
        --g_MdoSessions.ActiveCount;
        if ( Index != g_MdoSessions.ActiveCount )
            g_MdoSessions.Active[Index] =
                g_MdoSessions.Active[g_MdoSessions.ActiveCount];
        MdoSessionsGenerationAdvance();
    }
    xrtMutexUnlock(g_MdoSessions.Lock);
}

bool MdoSessionManagerInit(xwork_runtime* Runtime)
{
    if ( g_MdoSessions.Initialized ) return true;
    if ( Runtime == NULL ) return false;
    memset(&g_MdoSessions, 0, sizeof(g_MdoSessions));
    g_MdoSessions.Lock = xrtMutexCreate();
    g_MdoSessions.Runtime = xworkRuntimeRef(Runtime);
    if ( g_MdoSessions.Lock == NULL || g_MdoSessions.Runtime == NULL ) {
        MdoSessionManagerUnit();
        return false;
    }
    g_MdoSessions.Generation = 1u;
    g_MdoSessions.Initialized = true;
    return true;
}

void MdoSessionManagerUnit(void)
{
    g_MdoSessions.Initialized = false;
    if ( g_MdoSessions.Runtime != NULL )
        xworkRuntimeRelease(g_MdoSessions.Runtime);
    if ( g_MdoSessions.Lock != NULL ) xrtMutexDestroy(g_MdoSessions.Lock);
    xrtFree(g_MdoSessions.Active);
    memset(&g_MdoSessions, 0, sizeof(g_MdoSessions));
}

uint64 MdoSessionManagerGeneration(void)
{
    uint64 Generation = 0u;
    if ( !g_MdoSessions.Initialized ) return 0u;
    xrtMutexLock(g_MdoSessions.Lock);
    Generation = g_MdoSessions.Generation;
    xrtMutexUnlock(g_MdoSessions.Lock);
    return Generation;
}

bool MdoSessionsProjectPurged(const char* ProjectId, const MdoProjectLease* Owner)
{
    bool Ok;
    if ( !g_MdoSessions.Initialized ||
         !MdoProjectLeaseProtects(Owner, ProjectId, MDO_PROJECT_LEASE_EXCLUSIVE) ) return false;
    xrtMutexLock(g_MdoSessions.Lock);
    Ok = g_MdoSessions.Generation != UINT64_MAX;
    if ( Ok ) ++g_MdoSessions.Generation;
    xrtMutexUnlock(g_MdoSessions.Lock);
    return Ok;
}

void MdoSessionCreateOptionsInit(MdoSessionCreateOptions* Options)
{
    if ( Options == NULL ) return;
    memset(Options, 0, sizeof(*Options));
    Options->Size = sizeof(*Options);
    MdoAgentSessionOptionsInit(&Options->Agent);
}

void MdoSessionRuntimeOptionsInit(MdoSessionRuntimeOptions* Options)
{
    if ( Options == NULL ) return;
    memset(Options, 0, sizeof(*Options));
    Options->Size = sizeof(*Options);
    Options->Deadline = XRT_DEADLINE_NEVER;
}

void MdoSessionForkOptionsInit(MdoSessionForkOptions* Options)
{
    if ( Options == NULL ) return;
    memset(Options, 0, sizeof(*Options));
    Options->Size = sizeof(*Options);
    Options->ThroughSequence = UINT64_MAX;
    MdoSessionRuntimeOptionsInit(&Options->Runtime);
}

void MdoSessionQueryInit(MdoSessionQuery* Query)
{
    if ( Query == NULL ) return;
    memset(Query, 0, sizeof(*Query));
    Query->Size = sizeof(*Query);
}

static void MdoSessionsRuntimeApply(MdoAgentSessionOptions* Agent,
    const MdoSessionRuntimeOptions* Runtime)
{
    Agent->Cancel = Runtime->Cancel;
    Agent->Deadline = Runtime->Deadline;
    Agent->OnApproval = Runtime->OnApproval;
    Agent->ApprovalUserData = Runtime->ApprovalUserData;
    Agent->OnPermission = Runtime->OnPermission;
    Agent->PermissionUserData = Runtime->PermissionUserData;
    Agent->UseRunPermissionScope = Runtime->UseRunPermissionScope;
    Agent->OnHook = Runtime->OnHook;
    Agent->HookUserData = Runtime->HookUserData;
    Agent->OnEvent = Runtime->OnEvent;
    Agent->EventUserData = Runtime->EventUserData;
    Agent->OnModelComplete = Runtime->OnModelComplete;
    Agent->ModelUserData = Runtime->ModelUserData;
    Agent->OwnerUserData = Runtime->OwnerUserData;
    Agent->OnOwnerRetain = Runtime->OnOwnerRetain;
    Agent->OnOwnerRelease = Runtime->OnOwnerRelease;
}

MdoSession* MdoSessionCreate(const MdoSessionCreateOptions* Options,
    xwork_error* Error)
{
    MdoSessionCreateOptions Defaults;
    MdoAgentSessionOptions AgentOptions;
    MdoAgentSessionInfo AgentInfo;
    MdoAgentSession* Agent = NULL;
    MdoSessionEventBridge* Bridge = NULL;
    MdoProjectLease* ProjectLease = NULL;
    MdoSession* Session = NULL;
    MdoSessionInfo Info;
    char* SessionId = NULL;
    char* SnapshotPath = NULL;
    char* JournalPath = NULL;
    char* ArtifactPath = NULL;
    char* Workspace = NULL;
    char Relative[MDO_SESSION_PATH_CAPACITY];
    char DirectoryPath[MDO_SESSION_PATH_CAPACITY];
    char MetaPath[MDO_SESSION_PATH_CAPACITY];
    const char* Title;
    bool DirectoryCreated = false;
    bool DirectoryExists = false;

    xworkErrorInit(Error);
    if ( Options == NULL ) {
        MdoSessionCreateOptionsInit(&Defaults);
        Options = &Defaults;
    }
    if ( !g_MdoSessions.Initialized || Options->Size < sizeof(*Options) ||
         Options->Agent.Size < sizeof(Options->Agent) ||
         !MdoSessionsIdValid(Options->ProjectId,
            MDO_PROJECT_ID_CAPACITY) ||
         (Options->RequestedId != NULL &&
          !MdoSessionsIdValid(Options->RequestedId,
            MDO_SESSION_ID_CAPACITY)) ||
         Options->Agent.SessionPath != NULL ||
         Options->Agent.JournalPath != NULL || Options->Agent.Recover ||
         Options->Agent.ArtifactDirectory != NULL ||
         ((Options->Agent.OnOwnerRetain != NULL) !=
          (Options->Agent.OnOwnerRelease != NULL)) ) {
        MdoSessionsError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "invalid managed session create request");
        return NULL;
    }
    Title = Options->Title != NULL ? Options->Title : "New session";
    if ( !MdoSessionsTextValid(Title, MDO_SESSION_TITLE_CAPACITY, true) ) {
        MdoSessionsError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "session title is not bounded UTF-8 text");
        return NULL;
    }
    ProjectLease = MdoProjectLeaseAcquire(Options->ProjectId,
        MDO_PROJECT_LEASE_SHARED, Error);
    if ( ProjectLease == NULL ) return NULL;
    SessionId = Options->RequestedId != NULL ?
        xrtStrDup(Options->RequestedId) : xrtXidMakeString();
    Workspace = xrtPathAbs(Options->Agent.WorkspaceRoot != NULL &&
        Options->Agent.WorkspaceRoot[0] != '\0' ?
        Options->Agent.WorkspaceRoot : ".");
    if ( SessionId == NULL || Workspace == NULL ||
         !MdoSessionsIdValid(SessionId, MDO_SESSION_ID_CAPACITY) ||
         !MdoSessionsTextValid(Workspace, MDO_SESSION_WORKSPACE_CAPACITY,
            false) ||
         !MdoSessionsDirectory(DirectoryPath, Options->ProjectId, SessionId) ||
         !MdoSessionsPath(MetaPath, Options->ProjectId, SessionId,
            "meta.json") ||
         !MdoSessionsPath(Relative, Options->ProjectId, SessionId,
            "snapshot.json") ) goto memory;
    SnapshotPath = MdoHomeExternalPath(Relative);
    if ( !MdoSessionsPath(Relative, Options->ProjectId, SessionId,
            "journal.jsonl") ) goto memory;
    JournalPath = MdoHomeExternalPath(Relative);
    if ( !MdoSessionsPath(Relative, Options->ProjectId, SessionId,
            "artifacts") ) goto memory;
    ArtifactPath = MdoHomeExternalPath(Relative);
    if ( SnapshotPath == NULL || JournalPath == NULL || ArtifactPath == NULL )
        goto memory;
    if ( Options->RequestedId != NULL ) {
        if ( !MdoHomeExternalStat(DirectoryPath, &DirectoryExists, NULL) ) {
            MdoSessionsXrtError(Error, XWORK_ERROR_IO,
                "cannot inspect the requested session directory");
            goto done;
        }
        if ( DirectoryExists ) {
            MdoSessionsError(Error, XWORK_ERROR_CONTEXT,
                "the requested session ID already exists");
            goto done;
        }
    }
    if ( !MdoHomeCreateDirectory(DirectoryPath) ) {
        MdoSessionsXrtError(Error, XWORK_ERROR_IO,
            "cannot create the managed session directory");
        goto done;
    }
    DirectoryCreated = true;
    Bridge = MdoSessionEventBridgeCreate(Options->ProjectId, SessionId,
        ProjectLease,
        Options->Agent.OnEvent, Options->Agent.EventUserData,
        Options->Agent.OwnerUserData, Options->Agent.OnOwnerRetain,
        Options->Agent.OnOwnerRelease, Error);
    if ( Bridge == NULL ) goto done;
    xrtMutexLock(g_MdoSessions.Lock);
    if ( !MdoSessionsActiveAdd(Options->ProjectId, SessionId) ) {
        xrtMutexUnlock(g_MdoSessions.Lock);
        MdoSessionsError(Error, XWORK_ERROR_OUT_OF_MEMORY,
            "cannot reserve the managed session runtime");
        goto done;
    }
    MdoSessionEventBridgeSetRegistered(Bridge);
    xrtMutexUnlock(g_MdoSessions.Lock);
    AgentOptions = Options->Agent;
    AgentOptions.WorkspaceRoot = Workspace;
    AgentOptions.ProjectId = Options->ProjectId;
    AgentOptions.ProductSessionId = SessionId;
    AgentOptions.SessionPath = SnapshotPath;
    AgentOptions.JournalPath = JournalPath;
    AgentOptions.ArtifactDirectory = ArtifactPath;
    AgentOptions.Recover = false;
    AgentOptions.OnEvent = MdoSessionEventBridgeOnEvent;
    AgentOptions.EventUserData = Bridge;
    AgentOptions.OwnerUserData = Bridge;
    AgentOptions.OnOwnerRetain = MdoSessionEventBridgeRef;
    AgentOptions.OnOwnerRelease = MdoSessionEventBridgeRelease;
    Agent = MdoAgentSessionCreateWithRuntime(g_MdoSessions.Runtime,
        &AgentOptions, Error);
    if ( Agent == NULL ) goto done;
    memset(&AgentInfo, 0, sizeof(AgentInfo));
    AgentInfo.Size = sizeof(AgentInfo);
    if ( !MdoAgentSessionGetInfo(Agent, &AgentInfo) ||
         !MdoSessionEventBridgeSetProfile(Bridge, AgentInfo.ModelId,
            AgentInfo.ContextWindowTokens) ) {
        MdoSessionsError(Error, XWORK_ERROR_CONTEXT,
            "cannot inspect the created Agent session");
        goto done;
    }
    memset(&Info, 0, sizeof(Info));
    Info.Size = sizeof(Info);
    Info.Revision = 1u;
    Info.CreatedAt = xrtNow();
    Info.UpdatedAt = Info.CreatedAt;
    Info.Status = MDO_SESSION_ACTIVE;
    Info.PreviousStatus = MDO_SESSION_ACTIVE;
    Info.RuntimeOpen = true;
    Info.Protocol = AgentInfo.Protocol;
    Info.MaxOutputTokens = AgentInfo.MaxOutputTokens;
    Info.ConfigRevision = AgentInfo.ConfigRevision;
    Info.ModelGeneration = AgentInfo.ModelGeneration;
    Info.ModuleGeneration = AgentInfo.ModuleGeneration;
    Info.SkillGeneration = AgentInfo.SkillGeneration;
    snprintf(Info.Id, sizeof(Info.Id), "%s", SessionId);
    snprintf(Info.ProjectId, sizeof(Info.ProjectId), "%s", Options->ProjectId);
    snprintf(Info.Title, sizeof(Info.Title), "%s", Title);
    snprintf(Info.AgentId, sizeof(Info.AgentId), "%s", AgentInfo.AgentId);
    snprintf(Info.ModelId, sizeof(Info.ModelId), "%s", AgentInfo.ModelId);
    snprintf(Info.ReasoningEffort, sizeof(Info.ReasoningEffort), "%s",
        AgentInfo.ReasoningEffort);
    snprintf(Info.PermissionProfile, sizeof(Info.PermissionProfile), "%s",
        AgentInfo.PermissionProfile);
    snprintf(Info.WorkspaceRoot, sizeof(Info.WorkspaceRoot), "%s", Workspace);
    Session = MdoSessionsHandleCreate(Agent, Bridge, ProjectLease, &Info, MetaPath);
    if ( Session == NULL ) goto memory;
    Agent = NULL;
    xrtMutexLock(g_MdoSessions.Lock);
    if ( !MdoSessionsMetaWrite(MetaPath, &Info) ) {
        xrtMutexUnlock(g_MdoSessions.Lock);
        MdoSessionsXrtError(Error, XWORK_ERROR_IO,
            "cannot publish session metadata");
        MdoSessionRelease(Session);
        Session = NULL;
        goto done;
    }
    MdoSessionsGenerationAdvance();
    xrtMutexUnlock(g_MdoSessions.Lock);
    goto done;
memory:
    MdoSessionsError(Error, XWORK_ERROR_OUT_OF_MEMORY,
        "cannot allocate managed session state");
done:
    MdoAgentSessionRelease(Agent);
    MdoSessionEventBridgeRelease(Bridge);
    if ( Session == NULL && DirectoryCreated ) {
        xerror* Saved = xrtTakeError();
        MdoSessionsRollbackDirectory(Options->ProjectId, SessionId,
            DirectoryPath);
        if ( Saved != NULL ) xrtSetErrorTake(Saved);
    }
    xrtFree(SessionId);
    xrtFree(SnapshotPath);
    xrtFree(JournalPath);
    xrtFree(ArtifactPath);
    xrtFree(Workspace);
    MdoProjectLeaseRelease(ProjectLease);
    return Session;
}

MdoSession* MdoSessionOpen(const char* ProjectId, const char* SessionId,
    const MdoSessionRuntimeOptions* Options, xwork_error* Error)
{
    MdoSessionRuntimeOptions Defaults;
    MdoSessionInfo Info;
    MdoSessionInfo Candidate;
    MdoAgentSessionOptions AgentOptions;
    MdoAgentSessionInfo AgentInfo;
    MdoAgentSession* Agent = NULL;
    MdoSessionEventBridge* Bridge = NULL;
    MdoProjectLease* ProjectLease = NULL;
    MdoSession* Session = NULL;
    char Relative[MDO_SESSION_PATH_CAPACITY];
    char MetaPath[MDO_SESSION_PATH_CAPACITY];
    char* SnapshotPath = NULL;
    char* JournalPath = NULL;
    char* ArtifactPath = NULL;
    bool ProfileChanged = false;
    bool RecoveryRequired = false;
    bool Committed;

    xworkErrorInit(Error);
    if ( Options == NULL ) {
        MdoSessionRuntimeOptionsInit(&Defaults);
        Options = &Defaults;
    }
    if ( !g_MdoSessions.Initialized || Options->Size < sizeof(*Options) ||
         !MdoSessionsIdValid(ProjectId, MDO_PROJECT_ID_CAPACITY) ||
         !MdoSessionsIdValid(SessionId, MDO_SESSION_ID_CAPACITY) ||
         ((Options->OnOwnerRetain != NULL) !=
          (Options->OnOwnerRelease != NULL)) ||
         ((Options->ProfileModelId == NULL) !=
          (Options->ProfileReasoningEffort == NULL)) ||
         ((Options->ProfileModelId == NULL) !=
          (Options->ProfilePermissionProfile == NULL)) ||
         (Options->ProfileModelId != NULL &&
          (!MdoSessionsTextValid(Options->ProfileModelId,
                MDO_SESSION_IDENTITY_CAPACITY, false) ||
           !MdoSessionsTextValid(Options->ProfileReasoningEffort,
                MDO_SESSION_REASONING_CAPACITY, false) ||
           (strcmp(Options->ProfilePermissionProfile, "read-only") != 0 &&
            strcmp(Options->ProfilePermissionProfile, "balanced") != 0 &&
            strcmp(Options->ProfilePermissionProfile,
                "full-access") != 0))) ||
         !MdoSessionsPath(MetaPath, ProjectId, SessionId, "meta.json") ) {
        MdoSessionsError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "invalid managed session open request");
        return NULL;
    }
    ProjectLease = MdoProjectLeaseAcquire(ProjectId,
        MDO_PROJECT_LEASE_SHARED, Error);
    if ( ProjectLease == NULL ) return NULL;
    xrtMutexLock(g_MdoSessions.Lock);
    if ( !MdoSessionsMetaRead(ProjectId, SessionId, &Info) ) {
        xrtMutexUnlock(g_MdoSessions.Lock);
        MdoSessionsXrtError(Error, XWORK_ERROR_IO,
            "cannot read session metadata");
        goto done;
    }
    if ( Info.Status != MDO_SESSION_ACTIVE ) {
        xrtMutexUnlock(g_MdoSessions.Lock);
        MdoSessionsError(Error, XWORK_ERROR_POLICY,
            "archived or trashed sessions must be restored before opening");
        goto done;
    }
    if ( MdoSessionsActiveFind(ProjectId, SessionId) != SIZE_MAX ) {
        xrtMutexUnlock(g_MdoSessions.Lock);
        MdoSessionsError(Error, XWORK_ERROR_CONTEXT,
            "session already has an active runtime");
        goto done;
    }
    xrtMutexUnlock(g_MdoSessions.Lock);
    if ( !MdoSessionsPath(Relative, ProjectId, SessionId, "snapshot.json") )
        goto memory;
    SnapshotPath = MdoHomeExternalPath(Relative);
    if ( !MdoSessionsPath(Relative, ProjectId, SessionId, "journal.jsonl") )
        goto memory;
    JournalPath = MdoHomeExternalPath(Relative);
    if ( !MdoSessionsPath(Relative, ProjectId, SessionId,
            "artifacts") ) goto memory;
    ArtifactPath = MdoHomeExternalPath(Relative);
    if ( SnapshotPath == NULL || JournalPath == NULL || ArtifactPath == NULL )
        goto memory;
    Bridge = MdoSessionEventBridgeCreate(ProjectId, SessionId, ProjectLease,
        Options->OnEvent, Options->EventUserData, Options->OwnerUserData,
        Options->OnOwnerRetain, Options->OnOwnerRelease, Error);
    if ( Bridge == NULL ) goto done;
    xrtMutexLock(g_MdoSessions.Lock);
    if ( !MdoSessionsMetaRead(ProjectId, SessionId, &Info) ) {
        xrtMutexUnlock(g_MdoSessions.Lock);
        MdoSessionsXrtError(Error, XWORK_ERROR_IO,
            "cannot revalidate session metadata");
        goto done;
    }
    if ( Info.Status != MDO_SESSION_ACTIVE ||
         MdoSessionsActiveFind(ProjectId, SessionId) != SIZE_MAX ) {
        xrtMutexUnlock(g_MdoSessions.Lock);
        MdoSessionsError(Error, XWORK_ERROR_CONTEXT,
            "session changed or acquired another runtime while opening");
        goto done;
    }
    if ( !MdoSessionsActiveAdd(ProjectId, SessionId) ) {
        xrtMutexUnlock(g_MdoSessions.Lock);
        MdoSessionsError(Error, XWORK_ERROR_OUT_OF_MEMORY,
            "cannot reserve the recovered session runtime");
        goto done;
    }
    MdoSessionEventBridgeSetRegistered(Bridge);
    xrtMutexUnlock(g_MdoSessions.Lock);
    ProfileChanged = Options->ProfileModelId != NULL &&
        (strcmp(Options->ProfileModelId, Info.ModelId) != 0 ||
         strcmp(Options->ProfileReasoningEffort,
            Info.ReasoningEffort) != 0 ||
         strcmp(Options->ProfilePermissionProfile,
            Info.PermissionProfile) != 0);
    if ( ProfileChanged && Info.Revision == UINT64_MAX ) {
        MdoSessionsError(Error, XWORK_ERROR_LIMIT,
            "session metadata revision is exhausted");
        goto done;
    }
    MdoAgentSessionOptionsInit(&AgentOptions);
    AgentOptions.AgentId = Info.AgentId;
    AgentOptions.ModelId = Options->ProfileModelId != NULL ?
        Options->ProfileModelId : Info.ModelId;
    AgentOptions.Protocol = Options->ProfileModelId != NULL &&
        strcmp(Options->ProfileModelId, Info.ModelId) != 0 ?
        0 : Info.Protocol;
    AgentOptions.ReasoningEffort =
        Options->ProfileReasoningEffort != NULL ?
        Options->ProfileReasoningEffort : Info.ReasoningEffort;
    AgentOptions.PermissionProfile =
        Options->ProfilePermissionProfile != NULL ?
        Options->ProfilePermissionProfile : Info.PermissionProfile;
    AgentOptions.MaxOutputTokens = AgentOptions.Protocol == 0 ?
        0u : Info.MaxOutputTokens;
    AgentOptions.WorkspaceRoot = Info.WorkspaceRoot;
    AgentOptions.ProjectId = Info.ProjectId;
    AgentOptions.ProductSessionId = Info.Id;
    AgentOptions.SessionPath = SnapshotPath;
    AgentOptions.JournalPath = JournalPath;
    AgentOptions.ArtifactDirectory = ArtifactPath;
    AgentOptions.Recover = true;
    MdoSessionsRuntimeApply(&AgentOptions, Options);
    AgentOptions.OnEvent = MdoSessionEventBridgeOnEvent;
    AgentOptions.EventUserData = Bridge;
    AgentOptions.OwnerUserData = Bridge;
    AgentOptions.OnOwnerRetain = MdoSessionEventBridgeRef;
    AgentOptions.OnOwnerRelease = MdoSessionEventBridgeRelease;
    Agent = MdoAgentSessionCreateWithRuntime(g_MdoSessions.Runtime,
        &AgentOptions, Error);
    if ( Agent == NULL ) goto done;
    memset(&AgentInfo, 0, sizeof(AgentInfo));
    AgentInfo.Size = sizeof(AgentInfo);
    if ( !MdoAgentSessionGetInfo(Agent, &AgentInfo) ||
         !MdoSessionEventBridgeSetProfile(Bridge, AgentInfo.ModelId,
            AgentInfo.ContextWindowTokens) ) {
        MdoSessionsError(Error, XWORK_ERROR_CONTEXT,
            "cannot inspect the opened Agent session");
        goto done;
    }
    if ( ProfileChanged ) {
        if ( !MdoAgentSessionRecoveryRequired(Agent, &RecoveryRequired,
                Error) ) goto done;
        if ( RecoveryRequired ) {
            MdoSessionsError(Error, XWORK_ERROR_CONTEXT,
                "resolve interrupted Agent calls before changing the profile");
            goto done;
        }
        Candidate = Info;
        ++Candidate.Revision;
        Candidate.UpdatedAt = xrtNow();
        if ( Candidate.UpdatedAt < Candidate.CreatedAt )
            Candidate.UpdatedAt = Candidate.CreatedAt;
        Candidate.Protocol = AgentInfo.Protocol;
        Candidate.MaxOutputTokens = AgentInfo.MaxOutputTokens;
        Candidate.ConfigRevision = AgentInfo.ConfigRevision;
        Candidate.ModelGeneration = AgentInfo.ModelGeneration;
        Candidate.ModuleGeneration = AgentInfo.ModuleGeneration;
        Candidate.SkillGeneration = AgentInfo.SkillGeneration;
        snprintf(Candidate.ModelId, sizeof(Candidate.ModelId), "%s",
            AgentInfo.ModelId);
        snprintf(Candidate.ReasoningEffort,
            sizeof(Candidate.ReasoningEffort), "%s",
            AgentInfo.ReasoningEffort);
        snprintf(Candidate.PermissionProfile,
            sizeof(Candidate.PermissionProfile), "%s",
            AgentInfo.PermissionProfile);
        Candidate.RuntimeOpen = true;
    }
    Info.RuntimeOpen = true;
    Session = MdoSessionsHandleCreate(Agent, Bridge, ProjectLease, &Info, MetaPath);
    if ( Session == NULL ) goto memory;
    Agent = NULL;
    if ( ProfileChanged ) {
        xrtMutexLock(Session->Lock);
        xrtMutexLock(g_MdoSessions.Lock);
        Committed = MdoSessionsValidateCurrent(Session, Error) &&
            MdoSessionsCommit(Session, &Candidate, Error);
        xrtMutexUnlock(g_MdoSessions.Lock);
        xrtMutexUnlock(Session->Lock);
        if ( !Committed ) {
            MdoSessionRelease(Session);
            Session = NULL;
            goto done;
        }
    }
    goto done;
memory:
    MdoSessionsError(Error, XWORK_ERROR_OUT_OF_MEMORY,
        "cannot allocate recovered session state");
done:
    MdoAgentSessionRelease(Agent);
    MdoSessionEventBridgeRelease(Bridge);
    xrtFree(SnapshotPath);
    xrtFree(JournalPath);
    xrtFree(ArtifactPath);
    MdoProjectLeaseRelease(ProjectLease);
    return Session;
}

MdoSession* MdoSessionFork(MdoSession* Source,
    const MdoSessionForkOptions* Options, xwork_error* Error)
{
    MdoSessionForkOptions Defaults;
    MdoAgentSessionOptions AgentOptions;
    MdoAgentSessionInfo AgentInfo;
    MdoAgentSession* Agent = NULL;
    MdoSessionEventBridge* Bridge = NULL;
    MdoProjectLease* ProjectLease = NULL;
    MdoSession* Session = NULL;
    MdoSessionInfo SourceInfo;
    MdoSessionInfo Info;
    char* SessionId = NULL;
    char* SnapshotPath = NULL;
    char* JournalPath = NULL;
    char* ArtifactPath = NULL;
    char Relative[MDO_SESSION_PATH_CAPACITY];
    char DirectoryPath[MDO_SESSION_PATH_CAPACITY];
    char MetaPath[MDO_SESSION_PATH_CAPACITY];
    char ProjectId[MDO_PROJECT_ID_CAPACITY];
    char Title[MDO_SESSION_TITLE_CAPACITY];
    uint64 SavedThrough = 0u;
    bool DirectoryCreated = false;
    bool SourceLocked = false;
    bool ManagerLocked = false;

    xworkErrorInit(Error);
    if ( Options == NULL ) {
        MdoSessionForkOptionsInit(&Defaults);
        Options = &Defaults;
    }
    if ( !g_MdoSessions.Initialized || Source == NULL ||
         Options->Size < sizeof(*Options) ||
         Options->Runtime.Size < sizeof(Options->Runtime) ||
         (Options->Title != NULL &&
          !MdoSessionsTextValid(Options->Title,
            MDO_SESSION_TITLE_CAPACITY, true)) ||
         ((Options->Runtime.OnOwnerRetain != NULL) !=
          (Options->Runtime.OnOwnerRelease != NULL)) ) {
        MdoSessionsError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "invalid managed session fork request");
        return NULL;
    }
    /* The source already owns the project's shared lease. Pin it before
     * touching either manager lock; fork and rollback retain this pin. */
    ProjectLease = MdoProjectLeaseRef(Source->ProjectLease);
    if ( ProjectLease == NULL ) {
        MdoSessionsError(Error, XWORK_ERROR_LIMIT,
            "cannot retain the fork source project lease");
        return NULL;
    }
    xrtMutexLock(Source->Lock);
    SourceLocked = true;
    if ( Source->Agent == NULL || Source->Info.Status != MDO_SESSION_ACTIVE ) {
        MdoSessionsError(Error, XWORK_ERROR_CONTEXT,
            "an open active session is required as a fork source");
        goto done;
    }
    snprintf(ProjectId, sizeof(ProjectId), "%s", Source->Info.ProjectId);
    snprintf(Title, sizeof(Title), "%s", Options->Title != NULL ?
        Options->Title : Source->Info.Title);
    SessionId = xrtXidMakeString();
    if ( SessionId == NULL ||
         !MdoSessionsIdValid(SessionId, MDO_SESSION_ID_CAPACITY) ||
         !MdoSessionsDirectory(DirectoryPath, ProjectId, SessionId) ||
         !MdoSessionsPath(MetaPath, ProjectId, SessionId, "meta.json") ||
         !MdoSessionsPath(Relative, ProjectId, SessionId, "snapshot.json") )
        goto memory;
    SnapshotPath = MdoHomeExternalPath(Relative);
    if ( !MdoSessionsPath(Relative, ProjectId, SessionId, "journal.jsonl") )
        goto memory;
    JournalPath = MdoHomeExternalPath(Relative);
    if ( !MdoSessionsPath(Relative, ProjectId, SessionId, "artifacts") )
        goto memory;
    ArtifactPath = MdoHomeExternalPath(Relative);
    if ( SnapshotPath == NULL || JournalPath == NULL || ArtifactPath == NULL )
        goto memory;
    if ( !MdoHomeCreateDirectory(DirectoryPath) ) {
        MdoSessionsXrtError(Error, XWORK_ERROR_IO,
            "cannot create the fork session directory");
        goto done;
    }
    DirectoryCreated = true;
    Bridge = MdoSessionEventBridgeCreate(ProjectId, SessionId, ProjectLease,
        Options->Runtime.OnEvent, Options->Runtime.EventUserData,
        Options->Runtime.OwnerUserData, Options->Runtime.OnOwnerRetain,
        Options->Runtime.OnOwnerRelease, Error);
    if ( Bridge == NULL ) goto done;
    xrtMutexLock(g_MdoSessions.Lock);
    ManagerLocked = true;
    if ( !MdoSessionsValidateCurrent(Source, Error) ||
         MdoSessionsActiveFind(ProjectId, Source->Info.Id) == SIZE_MAX ) {
        if ( Error != NULL && Error->eCode == XWORK_ERROR_NONE )
            MdoSessionsError(Error, XWORK_ERROR_CONTEXT,
                "fork source no longer owns an active runtime");
        goto done;
    }
    if ( !MdoSessionsActiveAdd(ProjectId, SessionId) ) {
        MdoSessionsError(Error, XWORK_ERROR_OUT_OF_MEMORY,
            "cannot reserve the fork session runtime");
        goto done;
    }
    MdoSessionEventBridgeSetRegistered(Bridge);
    SourceInfo = Source->Info;
    xrtMutexUnlock(g_MdoSessions.Lock);
    ManagerLocked = false;
    if ( !MdoAgentSessionSaveFork(Source->Agent, Options->ThroughSequence,
            SnapshotPath, &SavedThrough, Error) ) goto done;
    if ( !MdoSessionEventBridgeClonePrefix(Bridge, ProjectId,
            SourceInfo.Id, SavedThrough, Error) ) goto done;
    xrtMutexUnlock(Source->Lock);
    SourceLocked = false;

    MdoAgentSessionOptionsInit(&AgentOptions);
    AgentOptions.AgentId = SourceInfo.AgentId;
    AgentOptions.ModelId = SourceInfo.ModelId;
    AgentOptions.Protocol = SourceInfo.Protocol;
    AgentOptions.ReasoningEffort = SourceInfo.ReasoningEffort;
    AgentOptions.PermissionProfile = SourceInfo.PermissionProfile;
    AgentOptions.MaxOutputTokens = SourceInfo.MaxOutputTokens;
    AgentOptions.WorkspaceRoot = SourceInfo.WorkspaceRoot;
    AgentOptions.ProjectId = ProjectId;
    AgentOptions.ProductSessionId = SessionId;
    AgentOptions.SessionPath = SnapshotPath;
    AgentOptions.JournalPath = JournalPath;
    AgentOptions.ArtifactDirectory = ArtifactPath;
    AgentOptions.Recover = true;
    MdoSessionsRuntimeApply(&AgentOptions, &Options->Runtime);
    AgentOptions.OnEvent = MdoSessionEventBridgeOnEvent;
    AgentOptions.EventUserData = Bridge;
    AgentOptions.OwnerUserData = Bridge;
    AgentOptions.OnOwnerRetain = MdoSessionEventBridgeRef;
    AgentOptions.OnOwnerRelease = MdoSessionEventBridgeRelease;
    Agent = MdoAgentSessionCreateWithRuntime(g_MdoSessions.Runtime,
        &AgentOptions, Error);
    if ( Agent == NULL ) goto done;
    memset(&AgentInfo, 0, sizeof(AgentInfo));
    AgentInfo.Size = sizeof(AgentInfo);
    if ( !MdoAgentSessionGetInfo(Agent, &AgentInfo) ||
         !MdoSessionEventBridgeSetProfile(Bridge, AgentInfo.ModelId,
            AgentInfo.ContextWindowTokens) ) {
        MdoSessionsError(Error, XWORK_ERROR_CONTEXT,
            "cannot inspect the forked Agent session");
        goto done;
    }
    memset(&Info, 0, sizeof(Info));
    Info.Size = sizeof(Info);
    Info.Revision = 1u;
    Info.CreatedAt = xrtNow();
    Info.UpdatedAt = Info.CreatedAt;
    Info.Status = MDO_SESSION_ACTIVE;
    Info.PreviousStatus = MDO_SESSION_ACTIVE;
    Info.RuntimeOpen = true;
    Info.Protocol = AgentInfo.Protocol;
    Info.MaxOutputTokens = AgentInfo.MaxOutputTokens;
    Info.ConfigRevision = AgentInfo.ConfigRevision;
    Info.ModelGeneration = AgentInfo.ModelGeneration;
    Info.ModuleGeneration = AgentInfo.ModuleGeneration;
    Info.SkillGeneration = AgentInfo.SkillGeneration;
    Info.ForkedThroughSequence = SavedThrough;
    snprintf(Info.Id, sizeof(Info.Id), "%s", SessionId);
    snprintf(Info.ProjectId, sizeof(Info.ProjectId), "%s", ProjectId);
    snprintf(Info.ParentSessionId, sizeof(Info.ParentSessionId), "%s",
        SourceInfo.Id);
    snprintf(Info.Title, sizeof(Info.Title), "%s", Title);
    snprintf(Info.AgentId, sizeof(Info.AgentId), "%s", AgentInfo.AgentId);
    snprintf(Info.ModelId, sizeof(Info.ModelId), "%s", AgentInfo.ModelId);
    snprintf(Info.ReasoningEffort, sizeof(Info.ReasoningEffort), "%s",
        AgentInfo.ReasoningEffort);
    snprintf(Info.PermissionProfile, sizeof(Info.PermissionProfile), "%s",
        AgentInfo.PermissionProfile);
    snprintf(Info.WorkspaceRoot, sizeof(Info.WorkspaceRoot), "%s",
        SourceInfo.WorkspaceRoot);
    Session = MdoSessionsHandleCreate(Agent, Bridge, ProjectLease, &Info, MetaPath);
    if ( Session == NULL ) goto memory;
    Agent = NULL;
    xrtMutexLock(g_MdoSessions.Lock);
    ManagerLocked = true;
    if ( !MdoSessionsMetaWrite(MetaPath, &Info) ) {
        xrtMutexUnlock(g_MdoSessions.Lock);
        ManagerLocked = false;
        MdoSessionsXrtError(Error, XWORK_ERROR_IO,
            "cannot publish fork session metadata");
        MdoSessionRelease(Session);
        Session = NULL;
        goto done;
    }
    MdoSessionsGenerationAdvance();
    xrtMutexUnlock(g_MdoSessions.Lock);
    ManagerLocked = false;
    goto done;
memory:
    MdoSessionsError(Error, XWORK_ERROR_OUT_OF_MEMORY,
        "cannot allocate fork session state");
done:
    if ( ManagerLocked ) xrtMutexUnlock(g_MdoSessions.Lock);
    if ( SourceLocked ) xrtMutexUnlock(Source->Lock);
    MdoAgentSessionRelease(Agent);
    MdoSessionEventBridgeRelease(Bridge);
    if ( Session == NULL && DirectoryCreated ) {
        xerror* Saved = xrtTakeError();
        MdoSessionsRollbackDirectory(ProjectId, SessionId, DirectoryPath);
        if ( Saved != NULL ) xrtSetErrorTake(Saved);
    }
    xrtFree(SessionId);
    xrtFree(SnapshotPath);
    xrtFree(JournalPath);
    xrtFree(ArtifactPath);
    MdoProjectLeaseRelease(ProjectLease);
    return Session;
}

MdoSession* MdoSessionRef(MdoSession* Session)
{
    uint32 Refs;
    if ( Session == NULL ) return NULL;
    Refs = xrtAtomic32Load(&Session->Refs, XMEMORY_ACQUIRE);
    for ( ; ; ) {
        uint32 Expected = Refs;
        if ( Refs == 0u || Refs == UINT32_MAX ) return NULL;
        if ( xrtAtomic32CompareExchange(&Session->Refs, &Expected, Refs + 1u,
                XMEMORY_ACQ_REL, XMEMORY_ACQUIRE) ) return Session;
        Refs = Expected;
    }
}

MdoSession* MdoSessionLoad(const char* ProjectId, const char* SessionId,
    xwork_error* Error)
{
    MdoSessionInfo Info;
    MdoSession* Session;
    MdoProjectLease* ProjectLease;
    char MetaPath[MDO_SESSION_PATH_CAPACITY];
    xworkErrorInit(Error);
    if ( !g_MdoSessions.Initialized ||
         !MdoSessionsIdValid(ProjectId, MDO_PROJECT_ID_CAPACITY) ||
         !MdoSessionsIdValid(SessionId, MDO_SESSION_ID_CAPACITY) ||
         !MdoSessionsPath(MetaPath, ProjectId, SessionId, "meta.json") ) {
        MdoSessionsError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "invalid managed session load request");
        return NULL;
    }
    ProjectLease = MdoProjectLeaseAcquire(ProjectId,
        MDO_PROJECT_LEASE_SHARED, Error);
    if ( ProjectLease == NULL ) return NULL;
    xrtMutexLock(g_MdoSessions.Lock);
    if ( !MdoSessionsMetaRead(ProjectId, SessionId, &Info) ) {
        xrtMutexUnlock(g_MdoSessions.Lock);
        MdoSessionsXrtError(Error, XWORK_ERROR_IO,
            "cannot read session metadata");
        MdoProjectLeaseRelease(ProjectLease);
        return NULL;
    }
    Session = MdoSessionsHandleCreate(NULL, NULL, ProjectLease, &Info, MetaPath);
    if ( Session != NULL )
        Session->Info.RuntimeOpen =
            MdoSessionsActiveFind(ProjectId, SessionId) != SIZE_MAX;
    xrtMutexUnlock(g_MdoSessions.Lock);
    MdoProjectLeaseRelease(ProjectLease);
    if ( Session == NULL )
        MdoSessionsError(Error, XWORK_ERROR_OUT_OF_MEMORY,
            "cannot allocate loaded session metadata");
    return Session;
}

void MdoSessionRelease(MdoSession* Session)
{
    uint32 Previous;
    if ( Session == NULL ) return;
    Previous = xrtAtomic32FetchSub(&Session->Refs, 1u, XMEMORY_ACQ_REL);
    if ( Previous > 1u ) return;
    if ( Previous == 0u ) abort();
    MdoAgentSessionRelease(Session->Agent);
    MdoSessionEventBridgeRelease(Session->Bridge);
    xrtMutexDestroy(Session->Lock);
    MdoProjectLeaseRelease(Session->ProjectLease);
    memset(Session, 0, sizeof(*Session));
    xrtFree(Session);
}

bool MdoSessionGetInfo(MdoSession* Session, MdoSessionInfo* Info)
{
    uint32 Size;
    if ( Session == NULL || Info == NULL || Info->Size < sizeof(*Info) )
        return false;
    Size = Info->Size;
    xrtMutexLock(Session->Lock);
    *Info = Session->Info;
    xrtMutexUnlock(Session->Lock);
    Info->Size = Size;
    return true;
}

bool MdoSessionAttachmentPendingSet(MdoSession* Session, uint64 AgentRunId,
    const char Ids[4][33], size_t Count, bool EmptyPrompt,
    const char* QueueItemId)
{
    bool Ok;
    if ( Session == NULL ) return false;
    xrtMutexLock(Session->Lock);
    Ok = MdoSessionEventBridgePendingSet(Session->Bridge,
        AgentRunId, Ids, Count, EmptyPrompt, QueueItemId);
    xrtMutexUnlock(Session->Lock);
    return Ok;
}

void MdoSessionAttachmentPendingClear(MdoSession* Session,
    uint64 AgentRunId)
{
    if ( Session == NULL ) return;
    xrtMutexLock(Session->Lock);
    MdoSessionEventBridgePendingClear(Session->Bridge, AgentRunId);
    xrtMutexUnlock(Session->Lock);
}

MdoAgentSession* MdoSessionAgentRef(MdoSession* Session)
{
    MdoAgentSession* Agent;
    if ( Session == NULL ) return NULL;
    xrtMutexLock(Session->Lock);
    Agent = MdoAgentSessionRef(Session->Agent);
    xrtMutexUnlock(Session->Lock);
    return Agent;
}

static bool MdoSessionsCommit(MdoSession* Session,
    const MdoSessionInfo* Candidate, xwork_error* Error)
{
    if ( !MdoSessionsMetaWrite(Session->MetaPath, Candidate) ) {
        MdoSessionsXrtError(Error, XWORK_ERROR_IO,
            "cannot update session metadata");
        return false;
    }
    Session->Info = *Candidate;
    MdoSessionsGenerationAdvance();
    return true;
}

static bool MdoSessionsValidateCurrent(MdoSession* Session,
    xwork_error* Error)
{
    MdoSessionInfo Current;
    if ( !MdoSessionsMetaRead(Session->Info.ProjectId, Session->Info.Id,
            &Current) ) {
        MdoSessionsXrtError(Error, XWORK_ERROR_IO,
            "cannot validate current session metadata");
        return false;
    }
    if ( Current.Revision != Session->Info.Revision ) {
        MdoSessionsError(Error, XWORK_ERROR_CONTEXT,
            "session metadata changed; reload before updating it");
        return false;
    }
    return true;
}

static bool MdoSessionsCandidate(MdoSession* Session,
    MdoSessionInfo* Candidate, xwork_error* Error)
{
    if ( Session->Info.Revision == UINT64_MAX ) {
        MdoSessionsError(Error, XWORK_ERROR_LIMIT,
            "session metadata revision is exhausted");
        return false;
    }
    *Candidate = Session->Info;
    ++Candidate->Revision;
    Candidate->UpdatedAt = xrtNow();
    if ( Candidate->UpdatedAt < Candidate->CreatedAt )
        Candidate->UpdatedAt = Candidate->CreatedAt;
    return true;
}

bool MdoSessionSetProfile(MdoSession* Session, const char* ModelId,
    const char* ReasoningEffort, const char* PermissionProfile,
    const MdoSessionRuntimeOptions* Runtime, xwork_error* Error)
{
    MdoSessionRuntimeOptions Defaults;
    MdoSessionInfo Candidate;
    MdoAgentSessionOptions Options;
    MdoAgentSessionInfo AgentInfo;
    MdoAgentSession* Agent = NULL;
    char Relative[MDO_SESSION_PATH_CAPACITY];
    char* Snapshot = NULL;
    char* Journal = NULL;
    char* Artifacts = NULL;
    bool Reserved = false;
    bool RecoveryRequired = false;
    bool Ok = false;

    xworkErrorInit(Error);
    if ( Runtime == NULL ) {
        MdoSessionRuntimeOptionsInit(&Defaults);
        Runtime = &Defaults;
    }
    if ( Session == NULL || Runtime->Size < sizeof(*Runtime) ||
         ((Runtime->OnOwnerRetain != NULL) !=
          (Runtime->OnOwnerRelease != NULL)) ||
         (ModelId != NULL &&
          !MdoSessionsTextValid(ModelId,
            MDO_SESSION_IDENTITY_CAPACITY, false)) ||
         (ReasoningEffort != NULL &&
          !MdoSessionsTextValid(ReasoningEffort,
            MDO_SESSION_REASONING_CAPACITY, false)) ||
         (PermissionProfile != NULL &&
          strcmp(PermissionProfile, "read-only") != 0 &&
          strcmp(PermissionProfile, "balanced") != 0 &&
          strcmp(PermissionProfile, "full-access") != 0) ||
         (ModelId == NULL && ReasoningEffort == NULL &&
          PermissionProfile == NULL) ) {
        MdoSessionsError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "invalid session profile request");
        return false;
    }
    xrtMutexLock(Session->Lock);
    xrtMutexLock(g_MdoSessions.Lock);
    if ( !MdoSessionsValidateCurrent(Session, Error) )
        goto unlock_manager;
    if ( Session->Info.Status != MDO_SESSION_ACTIVE ||
         Session->Agent != NULL ||
         MdoSessionsActiveFind(Session->Info.ProjectId,
            Session->Info.Id) != SIZE_MAX ) {
        MdoSessionsError(Error, XWORK_ERROR_CONTEXT,
            "the session must be active and idle to change its profile");
        goto unlock_manager;
    }
    if ( (ModelId == NULL ||
          strcmp(ModelId, Session->Info.ModelId) == 0) &&
         (ReasoningEffort == NULL ||
          strcmp(ReasoningEffort, Session->Info.ReasoningEffort) == 0) &&
         (PermissionProfile == NULL ||
          strcmp(PermissionProfile,
            Session->Info.PermissionProfile) == 0) ) {
        Ok = true;
        goto unlock_manager;
    }
    if ( !MdoSessionsCandidate(Session, &Candidate, Error) )
        goto unlock_manager;
    if ( ModelId != NULL )
        snprintf(Candidate.ModelId, sizeof(Candidate.ModelId), "%s", ModelId);
    if ( ReasoningEffort != NULL )
        snprintf(Candidate.ReasoningEffort,
            sizeof(Candidate.ReasoningEffort), "%s", ReasoningEffort);
    if ( PermissionProfile != NULL )
        snprintf(Candidate.PermissionProfile,
            sizeof(Candidate.PermissionProfile), "%s", PermissionProfile);
    if ( !MdoSessionsActiveAdd(Candidate.ProjectId, Candidate.Id) ) {
        MdoSessionsError(Error, XWORK_ERROR_OUT_OF_MEMORY,
            "cannot reserve the session profile update");
        goto unlock_manager;
    }
    Reserved = true;
    xrtMutexUnlock(g_MdoSessions.Lock);

    if ( !MdoSessionsPath(Relative, Candidate.ProjectId, Candidate.Id,
            "snapshot.json") ||
         (Snapshot = MdoHomeExternalPath(Relative)) == NULL ||
         !MdoSessionsPath(Relative, Candidate.ProjectId, Candidate.Id,
            "journal.jsonl") ||
         (Journal = MdoHomeExternalPath(Relative)) == NULL ||
         !MdoSessionsPath(Relative, Candidate.ProjectId, Candidate.Id,
            "artifacts") ||
         (Artifacts = MdoHomeExternalPath(Relative)) == NULL ) {
        MdoSessionsError(Error, XWORK_ERROR_OUT_OF_MEMORY,
            "cannot resolve session profile paths");
        goto done;
    }
    MdoAgentSessionOptionsInit(&Options);
    Options.AgentId = Candidate.AgentId;
    Options.ModelId = Candidate.ModelId;
    Options.Protocol = ModelId != NULL &&
        strcmp(ModelId, Session->Info.ModelId) != 0 ?
        0 : Candidate.Protocol;
    Options.ReasoningEffort = Candidate.ReasoningEffort;
    Options.PermissionProfile = Candidate.PermissionProfile;
    Options.MaxOutputTokens = Options.Protocol == 0 ?
        0u : Candidate.MaxOutputTokens;
    Options.WorkspaceRoot = Candidate.WorkspaceRoot;
    Options.ProjectId = Candidate.ProjectId;
    Options.ProductSessionId = Candidate.Id;
    Options.SessionPath = Snapshot;
    Options.JournalPath = Journal;
    Options.ArtifactDirectory = Artifacts;
    Options.Recover = true;
    MdoSessionsRuntimeApply(&Options, Runtime);
    Agent = MdoAgentSessionCreateWithRuntime(g_MdoSessions.Runtime,
        &Options, Error);
    if ( Agent == NULL ) goto done;
    if ( !MdoAgentSessionRecoveryRequired(Agent, &RecoveryRequired,
            Error) ) goto done;
    if ( RecoveryRequired ) {
        MdoSessionsError(Error, XWORK_ERROR_CONTEXT,
            "resolve interrupted Agent calls before changing the profile");
        goto done;
    }
    memset(&AgentInfo, 0, sizeof(AgentInfo));
    AgentInfo.Size = sizeof(AgentInfo);
    if ( !MdoAgentSessionGetInfo(Agent, &AgentInfo) ) {
        MdoSessionsError(Error, XWORK_ERROR_CONTEXT,
            "cannot inspect the selected Agent profile");
        goto done;
    }
    Candidate.Protocol = AgentInfo.Protocol;
    Candidate.MaxOutputTokens = AgentInfo.MaxOutputTokens;
    Candidate.ConfigRevision = AgentInfo.ConfigRevision;
    Candidate.ModelGeneration = AgentInfo.ModelGeneration;
    Candidate.ModuleGeneration = AgentInfo.ModuleGeneration;
    Candidate.SkillGeneration = AgentInfo.SkillGeneration;
    snprintf(Candidate.ModelId, sizeof(Candidate.ModelId), "%s",
        AgentInfo.ModelId);
    snprintf(Candidate.ReasoningEffort, sizeof(Candidate.ReasoningEffort),
        "%s", AgentInfo.ReasoningEffort);
    snprintf(Candidate.PermissionProfile,
        sizeof(Candidate.PermissionProfile), "%s",
        AgentInfo.PermissionProfile);
    xrtMutexLock(g_MdoSessions.Lock);
    if ( MdoSessionsValidateCurrent(Session, Error) )
        Ok = MdoSessionsCommit(Session, &Candidate, Error);
    xrtMutexUnlock(g_MdoSessions.Lock);
    goto done;

unlock_manager:
    xrtMutexUnlock(g_MdoSessions.Lock);
done:
    MdoAgentSessionRelease(Agent);
    xrtFree(Snapshot);
    xrtFree(Journal);
    xrtFree(Artifacts);
    if ( Reserved ) MdoSessionsInternalActiveRelease(
        Candidate.ProjectId, Candidate.Id);
    xrtMutexUnlock(Session->Lock);
    return Ok;
}

bool MdoSessionRename(MdoSession* Session, const char* Title,
    xwork_error* Error)
{
    MdoSessionInfo Candidate;
    bool Ok;
    xworkErrorInit(Error);
    if ( Session == NULL ||
         !MdoSessionsTextValid(Title, MDO_SESSION_TITLE_CAPACITY, true) ) {
        MdoSessionsError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "session title is not bounded UTF-8 text");
        return false;
    }
    xrtMutexLock(Session->Lock);
    xrtMutexLock(g_MdoSessions.Lock);
    if ( !MdoSessionsValidateCurrent(Session, Error) ) Ok = false;
    else if ( strcmp(Session->Info.Title, Title) == 0 ) Ok = true;
    else if ( MdoSessionsCandidate(Session, &Candidate, Error) ) {
        snprintf(Candidate.Title, sizeof(Candidate.Title), "%s", Title);
        Ok = MdoSessionsCommit(Session, &Candidate, Error);
    } else Ok = false;
    xrtMutexUnlock(g_MdoSessions.Lock);
    xrtMutexUnlock(Session->Lock);
    return Ok;
}

bool MdoSessionSetPinned(MdoSession* Session, bool Pinned,
    xwork_error* Error)
{
    MdoSessionInfo Candidate;
    bool Ok;
    xworkErrorInit(Error);
    if ( Session == NULL ) {
        MdoSessionsError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "session is required");
        return false;
    }
    xrtMutexLock(Session->Lock);
    xrtMutexLock(g_MdoSessions.Lock);
    if ( !MdoSessionsValidateCurrent(Session, Error) ) Ok = false;
    else if ( Session->Info.Pinned == Pinned ) Ok = true;
    else if ( MdoSessionsCandidate(Session, &Candidate, Error) ) {
        Candidate.Pinned = Pinned;
        Ok = MdoSessionsCommit(Session, &Candidate, Error);
    } else Ok = false;
    xrtMutexUnlock(g_MdoSessions.Lock);
    xrtMutexUnlock(Session->Lock);
    return Ok;
}

bool MdoSessionSetArchived(MdoSession* Session, bool Archived,
    xwork_error* Error)
{
    MdoSessionInfo Candidate;
    MdoSessionStatus Target = Archived ? MDO_SESSION_ARCHIVED :
        MDO_SESSION_ACTIVE;
    bool Ok;
    xworkErrorInit(Error);
    if ( Session == NULL ) {
        MdoSessionsError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "session is required");
        return false;
    }
    xrtMutexLock(Session->Lock);
    xrtMutexLock(g_MdoSessions.Lock);
    if ( !MdoSessionsValidateCurrent(Session, Error) ) Ok = false;
    else if ( Archived && MdoSessionsActiveFind(Session->Info.ProjectId,
            Session->Info.Id) != SIZE_MAX ) {
        MdoSessionsError(Error, XWORK_ERROR_CONTEXT,
            "release the active session runtime before archiving");
        Ok = false;
    } else if ( Session->Info.Status == MDO_SESSION_TRASH ) {
        MdoSessionsError(Error, XWORK_ERROR_POLICY,
            "restore a trashed session before changing archive state");
        Ok = false;
    } else if ( Session->Info.Status == Target ) Ok = true;
    else if ( MdoSessionsCandidate(Session, &Candidate, Error) ) {
        Candidate.Status = Target;
        Candidate.PreviousStatus = Target;
        Ok = MdoSessionsCommit(Session, &Candidate, Error);
    } else Ok = false;
    xrtMutexUnlock(g_MdoSessions.Lock);
    xrtMutexUnlock(Session->Lock);
    return Ok;
}

bool MdoSessionMoveToTrash(MdoSession* Session, xwork_error* Error)
{
    MdoSessionInfo Candidate;
    bool Ok;
    xworkErrorInit(Error);
    if ( Session == NULL ) {
        MdoSessionsError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "session is required");
        return false;
    }
    xrtMutexLock(Session->Lock);
    xrtMutexLock(g_MdoSessions.Lock);
    if ( !MdoSessionsValidateCurrent(Session, Error) ) Ok = false;
    else if ( MdoSessionsActiveFind(Session->Info.ProjectId,
            Session->Info.Id) != SIZE_MAX ) {
        MdoSessionsError(Error, XWORK_ERROR_CONTEXT,
            "release the active session runtime before moving it to trash");
        Ok = false;
    } else if ( Session->Info.Status == MDO_SESSION_TRASH ) Ok = true;
    else if ( MdoSessionsCandidate(Session, &Candidate, Error) ) {
        Candidate.PreviousStatus = Candidate.Status;
        Candidate.Status = MDO_SESSION_TRASH;
        Candidate.Pinned = false;
        Ok = MdoSessionsCommit(Session, &Candidate, Error);
    } else Ok = false;
    xrtMutexUnlock(g_MdoSessions.Lock);
    xrtMutexUnlock(Session->Lock);
    return Ok;
}

bool MdoSessionRestore(MdoSession* Session, xwork_error* Error)
{
    MdoSessionInfo Candidate;
    bool Ok;
    xworkErrorInit(Error);
    if ( Session == NULL ) {
        MdoSessionsError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "session is required");
        return false;
    }
    xrtMutexLock(Session->Lock);
    xrtMutexLock(g_MdoSessions.Lock);
    if ( !MdoSessionsValidateCurrent(Session, Error) ) Ok = false;
    else if ( Session->Info.Status != MDO_SESSION_TRASH ) Ok = true;
    else if ( MdoSessionsCandidate(Session, &Candidate, Error) ) {
        Candidate.Status = Candidate.PreviousStatus;
        Ok = MdoSessionsCommit(Session, &Candidate, Error);
    } else Ok = false;
    xrtMutexUnlock(g_MdoSessions.Lock);
    xrtMutexUnlock(Session->Lock);
    return Ok;
}

bool MdoSessionLastSequence(MdoSession* Session, uint64* LastSequence,
    xwork_error* Error)
{
    bool Ok;
    xworkErrorInit(Error);
    if ( Session == NULL || LastSequence == NULL ) {
        MdoSessionsError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "session and last sequence output are required");
        return false;
    }
    *LastSequence = 0u;
    xrtMutexLock(Session->Lock);
    if ( Session->Agent == NULL || Session->Info.Status != MDO_SESSION_ACTIVE ) {
        MdoSessionsError(Error, XWORK_ERROR_CONTEXT,
            "an open active session is required to inspect the ledger");
        Ok = false;
    } else Ok = MdoAgentSessionLastSequence(Session->Agent,
        LastSequence, Error);
    xrtMutexUnlock(Session->Lock);
    return Ok;
}

bool MdoSessionFinishInterrupted(MdoSession* Session,
    uint64 ExpectedRevision, uint64 ExpectedLastSequence,
    uint64* FinishedSequence, xwork_error* Error)
{
    MdoSessionInfo Candidate;
    bool Ok = false;
    xworkErrorInit(Error);
    if ( FinishedSequence != NULL ) *FinishedSequence = 0u;
    if ( Session == NULL || FinishedSequence == NULL ||
         ExpectedRevision == 0u || ExpectedLastSequence == 0u ) {
        MdoSessionsError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "session revision and interrupted sequence are required");
        return false;
    }
    xrtMutexLock(Session->Lock);
    xrtMutexLock(g_MdoSessions.Lock);
    if ( Session->Agent == NULL ||
         Session->Info.Status != MDO_SESSION_ACTIVE ||
         Session->Info.Revision != ExpectedRevision ||
         !MdoSessionsValidateCurrent(Session, Error) ) {
        if ( Error != NULL && Error->eCode == XWORK_ERROR_NONE )
            MdoSessionsError(Error, XWORK_ERROR_CONTEXT,
                "the interrupted session revision changed");
    } else if ( MdoSessionsCandidate(Session, &Candidate, Error) &&
                MdoAgentSessionFinishInterrupted(Session->Agent,
                    ExpectedLastSequence, FinishedSequence, Error) ) {
        Ok = MdoSessionsCommit(Session, &Candidate, Error);
    }
    xrtMutexUnlock(g_MdoSessions.Lock);
    xrtMutexUnlock(Session->Lock);
    return Ok;
}

static bool MdoSessionsLedgerMutation(MdoSession* Session,
    bool Clear, uint64 ThroughSequence, uint64 SourceEventId,
    bool* MessageChanged, xwork_error* Error)
{
    MdoSessionInfo Candidate;
    MdoSessionEventTrimPlan* Trim = NULL;
    bool Ok = false;
    xworkErrorInit(Error);
    if ( MessageChanged != NULL ) *MessageChanged = false;
    if ( Session == NULL ) {
        MdoSessionsError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "session is required");
        return false;
    }
    xrtMutexLock(Session->Lock);
    xrtMutexLock(g_MdoSessions.Lock);
    if ( Session->Agent == NULL ||
         Session->Info.Status != MDO_SESSION_ACTIVE ||
         !MdoSessionsValidateCurrent(Session, Error) ) {
        if ( Error != NULL && Error->eCode == XWORK_ERROR_NONE )
            MdoSessionsError(Error, XWORK_ERROR_CONTEXT,
                "an open active session is required for ledger maintenance");
        goto done;
    }
    Trim = MdoSessionEventTrimPrepare(Session->Bridge,
        ThroughSequence, Clear, SourceEventId, MessageChanged, Error);
    if ( Trim == NULL ) goto done;
    Ok = Clear ? MdoAgentSessionClear(Session->Agent, Error) :
        MdoAgentSessionTruncateAfter(Session->Agent, ThroughSequence, Error);
    if ( !Ok ) goto done;
    if ( !MdoSessionEventTrimApply(Trim, Error) ||
         !MdoSessionEventTrimReconcileTodo(Trim, Error) ) {
        Ok = false;
        goto done;
    }
    if ( !MdoSessionAttachmentPruneRemoved(Session->Info.ProjectId,
            Session->Info.Id) ) {
        MdoSessionsError(Error, XWORK_ERROR_IO,
            "cannot reconcile removed image references");
        Ok = false;
        goto done;
    }
    if ( !MdoSessionsCandidate(Session, &Candidate, Error) ||
         !MdoSessionsCommit(Session, &Candidate, Error) ) Ok = false;
done:
    MdoSessionEventTrimPlanRelease(Trim);
    xrtMutexUnlock(g_MdoSessions.Lock);
    xrtMutexUnlock(Session->Lock);
    return Ok;
}

bool MdoSessionClear(MdoSession* Session, xwork_error* Error)
{
    return MdoSessionsLedgerMutation(Session, true, 0u, 0u, NULL, Error);
}

bool MdoSessionTruncateAfter(MdoSession* Session, uint64 ThroughSequence,
    xwork_error* Error)
{
    return MdoSessionsLedgerMutation(Session, false, ThroughSequence, 0u,
        NULL, Error);
}

MdoSessionMessageMutationResult MdoSessionTruncateMessage(MdoSession* Session,
    uint64 ThroughSequence, uint64 SourceEventId, xwork_error* Error)
{
    bool MessageChanged = false;
    xworkErrorInit(Error);
    if ( SourceEventId == 0u || ThroughSequence == UINT64_MAX ) {
        MdoSessionsError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "a source event and a valid message boundary are required");
        return MDO_SESSION_MESSAGE_FAILED;
    }
    if ( MdoSessionsLedgerMutation(Session, false, ThroughSequence,
            SourceEventId, &MessageChanged, Error) ) return MDO_SESSION_MESSAGE_OK;
    return MessageChanged ? MDO_SESSION_MESSAGE_CHANGED : MDO_SESSION_MESSAGE_FAILED;
}

typedef struct MdoSessionExportCapture {
    MdoSession* Session;
    str Data;
    size_t Size;
} MdoSessionExportCapture;

/* The caller holds Session->Lock and the Agent run claim throughout this
 * capture. Serialize metadata validation/read with mutations from other
 * handles; release that manager lock before copying the bounded snapshot. */
static bool MdoSessionsExportCapture(void* UserData, xwork_error* Error)
{
    MdoSessionExportCapture* Capture = (MdoSessionExportCapture*)UserData;
    MdoSession* Session = Capture->Session;
    static const char Middle[] = ",\"snapshot\":";
    static const char Suffix[] = "}\n";
    char SnapshotPath[MDO_SESSION_PATH_CAPACITY];
    char Header[96];
    char* Meta = NULL;
    char* Snapshot = NULL;
    char* Result = NULL;
    size_t MetaSize = 0u;
    size_t SnapshotSize = 0u;
    size_t HeaderSize;
    size_t Total;
    int Written;
    bool MetaOk;
    xrtMutexLock(g_MdoSessions.Lock);
    MetaOk = MdoSessionsValidateCurrent(Session, Error);
    if ( MetaOk && !MdoSessionsReadBounded(Session->MetaPath,
            MDO_SESSION_META_LIMIT, &Meta, &MetaSize) ) {
        MdoSessionsXrtError(Error, XWORK_ERROR_IO,
            "cannot read the session export metadata");
        MetaOk = false;
    }
    xrtMutexUnlock(g_MdoSessions.Lock);
    if ( !MetaOk ) goto done;
    if ( !MdoSessionsPath(SnapshotPath, Session->Info.ProjectId,
            Session->Info.Id, "snapshot.json") ||
         !MdoSessionsReadBounded(SnapshotPath,
            MDO_SESSION_EXPORT_SNAPSHOT_LIMIT, &Snapshot, &SnapshotSize) ) {
        MdoSessionsXrtError(Error, XWORK_ERROR_IO,
            "cannot read the checkpointed session export");
        goto done;
    }
    Written = snprintf(Header, sizeof(Header),
        "{\"export_schema\":1,\"exported_at_us\":%lld,\"meta\":",
        (long long)xrtNow());
    if ( Written <= 0 || (size_t)Written >= sizeof(Header) ) goto memory;
    HeaderSize = (size_t)Written;
    if ( HeaderSize > SIZE_MAX - MetaSize ||
         HeaderSize + MetaSize > SIZE_MAX - (sizeof(Middle) - 1u) ||
         HeaderSize + MetaSize + sizeof(Middle) - 1u >
            SIZE_MAX - SnapshotSize ||
         HeaderSize + MetaSize + sizeof(Middle) - 1u + SnapshotSize >
            SIZE_MAX - sizeof(Suffix) ) goto memory;
    Total = HeaderSize + MetaSize + sizeof(Middle) - 1u + SnapshotSize +
        sizeof(Suffix) - 1u;
    Result = (char*)xrtMalloc(Total + 1u);
    if ( Result == NULL ) goto memory;
    memcpy(Result, Header, HeaderSize);
    memcpy(Result + HeaderSize, Meta, MetaSize);
    memcpy(Result + HeaderSize + MetaSize, Middle, sizeof(Middle) - 1u);
    memcpy(Result + HeaderSize + MetaSize + sizeof(Middle) - 1u,
        Snapshot, SnapshotSize);
    memcpy(Result + Total - (sizeof(Suffix) - 1u), Suffix,
        sizeof(Suffix) - 1u);
    Result[Total] = '\0';
    Capture->Data = Result;
    Capture->Size = Total;
    goto done;
memory:
    MdoSessionsError(Error, XWORK_ERROR_OUT_OF_MEMORY,
        "cannot allocate the session export");
    xrtFree(Result);
    Result = NULL;
done:
    xrtFree(Meta);
    xrtFree(Snapshot);
    return Capture->Data != NULL;
}

str MdoSessionExportJson(MdoSession* Session, size_t* Size,
    xwork_error* Error)
{
    MdoSessionExportCapture Capture;
    xworkErrorInit(Error);
    if ( Size != NULL ) *Size = 0u;
    if ( Session == NULL || Size == NULL ) {
        MdoSessionsError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "session and export size are required");
        return NULL;
    }
    memset(&Capture, 0, sizeof(Capture));
    Capture.Session = Session;
    xrtMutexLock(Session->Lock);
    if ( Session->Agent == NULL || Session->Info.Status != MDO_SESSION_ACTIVE ) {
        MdoSessionsError(Error, XWORK_ERROR_CONTEXT,
            "an open active session is required for export");
    } else if ( MdoAgentSessionWithCheckpoint(Session->Agent,
            MdoSessionsExportCapture, &Capture, Error) ) {
        *Size = Capture.Size;
    }
    xrtMutexUnlock(Session->Lock);
    return Capture.Data;
}

static bool MdoSessionsCatalogDiagnostic(MdoSessionCatalog* Catalog,
    const char* Path, const char* Message)
{
    MdoSessionDiagnostic* Item;
    if ( Catalog->DiagnosticCount >= MDO_SESSION_DIAGNOSTIC_MAX ) return true;
    if ( !MdoSessionsGrow((void**)&Catalog->Diagnostics,
            &Catalog->DiagnosticCapacity, Catalog->DiagnosticCount + 1u,
            sizeof(*Catalog->Diagnostics)) ) return false;
    Item = &Catalog->Diagnostics[Catalog->DiagnosticCount++];
    memset(Item, 0, sizeof(*Item));
    Item->Size = sizeof(*Item);
    snprintf(Item->Path, sizeof(Item->Path), "%s", Path != NULL ? Path : "");
    snprintf(Item->Message, sizeof(Item->Message), "%s",
        Message != NULL ? Message : "invalid session metadata");
    return true;
}

static bool MdoSessionsCatalogItem(MdoSessionCatalog* Catalog,
    const MdoSessionInfo* Info)
{
    if ( Catalog->Count >= MDO_SESSION_CATALOG_MAX ) {
        Catalog->Truncated = true;
        return true;
    }
    if ( !MdoSessionsGrow((void**)&Catalog->Items, &Catalog->Capacity,
            Catalog->Count + 1u, sizeof(*Catalog->Items)) ) return false;
    Catalog->Items[Catalog->Count++] = *Info;
    return true;
}

static int MdoSessionsCatalogCompare(const void* LeftValue,
    const void* RightValue)
{
    const MdoSessionInfo* Left = (const MdoSessionInfo*)LeftValue;
    const MdoSessionInfo* Right = (const MdoSessionInfo*)RightValue;
    if ( Left->Status != Right->Status )
        return Left->Status < Right->Status ? -1 : 1;
    if ( Left->Pinned != Right->Pinned ) return Left->Pinned ? -1 : 1;
    if ( Left->UpdatedAt != Right->UpdatedAt )
        return Left->UpdatedAt > Right->UpdatedAt ? -1 : 1;
    return strcmp(Left->Id, Right->Id);
}

static bool MdoSessionsScanProject(MdoSessionCatalog* Catalog,
    const char* ProjectId)
{
    char ProjectPath[MDO_SESSION_PATH_CAPACITY];
    xdir Directory;
    xdirentry Entry;
    xdirnext Next;
    int Written = snprintf(ProjectPath, sizeof(ProjectPath), "sessions/%s",
        ProjectId);
    if ( Written <= 0 || (size_t)Written >= sizeof(ProjectPath) ) return false;
    Directory = MdoHomeOpenDirectory(ProjectPath, XDIR_STAT);
    if ( Directory == NULL ) return false;
    while ( (Next = xrtDirNext(Directory, &Entry)) == XDIR_NEXT_ITEM ) {
        char SessionId[MDO_SESSION_ID_CAPACITY];
        char MetaPath[MDO_SESSION_PATH_CAPACITY];
        MdoSessionInfo Info;
        const xerror* Cause;
        if ( Entry.Info.Type != XFILE_TYPE_DIRECTORY ) continue;
        if ( Entry.Name.Size >= sizeof(SessionId) ||
             !xrtUtf8Valid(Entry.Name, NULL) ) {
            if ( !MdoSessionsCatalogDiagnostic(Catalog, ProjectPath,
                    "session directory name is invalid") ) goto fail;
            continue;
        }
        memcpy(SessionId, Entry.Name.Data, Entry.Name.Size);
        SessionId[Entry.Name.Size] = '\0';
        if ( !MdoSessionsIdValid(SessionId, sizeof(SessionId)) ||
             !MdoSessionsPath(MetaPath, ProjectId, SessionId, "meta.json") ) {
            if ( !MdoSessionsCatalogDiagnostic(Catalog, ProjectPath,
                    "session directory identifier is invalid") ) goto fail;
            continue;
        }
        if ( !MdoSessionsMetaRead(ProjectId, SessionId, &Info) ) {
            Cause = xrtGetError();
            if ( !MdoSessionsCatalogDiagnostic(Catalog, MetaPath,
                    Cause != NULL ? xrtErrorMessage(Cause) :
                    "cannot read session metadata") ) goto fail;
            xrtClearError();
            continue;
        }
        Info.RuntimeOpen =
            MdoSessionsActiveFind(ProjectId, SessionId) != SIZE_MAX;
        if ( !MdoSessionsCatalogItem(Catalog, &Info) ) goto fail;
        if ( Catalog->Truncated ) break;
    }
    if ( Next == XDIR_NEXT_ERROR || !xrtDirClose(Directory) ) return false;
    return true;
fail:
    (void)xrtDirClose(Directory);
    return false;
}

MdoSessionCatalog* MdoSessionCatalogSnapshot(xwork_error* Error)
{
    MdoSessionCatalog* Catalog;
    xdir Directory = NULL;
    xdirentry Entry;
    xdirnext Next = XDIR_NEXT_END;
    xfileinfo Info;
    bool Exists = false;
    xworkErrorInit(Error);
    if ( !g_MdoSessions.Initialized ) {
        MdoSessionsError(Error, XWORK_ERROR_CONTEXT,
            "session manager is not initialized");
        return NULL;
    }
    Catalog = (MdoSessionCatalog*)xrtCalloc(1u, sizeof(*Catalog));
    if ( Catalog == NULL ) goto memory;
    xrtAtomic32Init(&Catalog->Refs, 1u);
    xrtMutexLock(g_MdoSessions.Lock);
    Catalog->Generation = g_MdoSessions.Generation;
    if ( !MdoHomeExternalStat(MDO_SESSION_SOURCE_ROOT, &Exists, &Info) )
        goto io_locked;
    if ( !Exists ) {
        xrtMutexUnlock(g_MdoSessions.Lock);
        return Catalog;
    }
    if ( Info.Type != XFILE_TYPE_DIRECTORY ) {
        if ( !MdoSessionsCatalogDiagnostic(Catalog,
                MDO_SESSION_SOURCE_ROOT,
                "sessions path is not a directory") ) goto memory_locked;
        xrtMutexUnlock(g_MdoSessions.Lock);
        return Catalog;
    }
    Directory = MdoHomeOpenDirectory(MDO_SESSION_SOURCE_ROOT, XDIR_STAT);
    if ( Directory == NULL ) goto io_locked;
    while ( (Next = xrtDirNext(Directory, &Entry)) == XDIR_NEXT_ITEM ) {
        char ProjectId[MDO_PROJECT_ID_CAPACITY];
        if ( Entry.Info.Type != XFILE_TYPE_DIRECTORY ) continue;
        if ( Entry.Name.Size >= sizeof(ProjectId) ||
             !xrtUtf8Valid(Entry.Name, NULL) ) {
            if ( !MdoSessionsCatalogDiagnostic(Catalog,
                    MDO_SESSION_SOURCE_ROOT,
                    "project directory name is invalid") )
                goto memory_locked;
            continue;
        }
        memcpy(ProjectId, Entry.Name.Data, Entry.Name.Size);
        ProjectId[Entry.Name.Size] = '\0';
        if ( !MdoSessionsIdValid(ProjectId, sizeof(ProjectId)) ) {
            if ( !MdoSessionsCatalogDiagnostic(Catalog,
                    MDO_SESSION_SOURCE_ROOT,
                    "project directory identifier is invalid") )
                goto memory_locked;
            continue;
        }
        if ( !MdoSessionsScanProject(Catalog, ProjectId) ) goto io_locked;
        if ( Catalog->Truncated ) {
            if ( !MdoSessionsCatalogDiagnostic(Catalog,
                    MDO_SESSION_SOURCE_ROOT,
                    "session catalog limit reached; remaining entries omitted") )
                goto memory_locked;
            break;
        }
    }
    if ( Next == XDIR_NEXT_ERROR || !xrtDirClose(Directory) ) {
        Directory = NULL;
        goto io_locked;
    }
    Directory = NULL;
    if ( Catalog->Count > 1u ) qsort(Catalog->Items, Catalog->Count,
        sizeof(*Catalog->Items), MdoSessionsCatalogCompare);
    xrtMutexUnlock(g_MdoSessions.Lock);
    return Catalog;
memory_locked:
    if ( Directory != NULL ) (void)xrtDirClose(Directory);
    xrtMutexUnlock(g_MdoSessions.Lock);
memory:
    MdoSessionCatalogRelease(Catalog);
    MdoSessionsError(Error, XWORK_ERROR_OUT_OF_MEMORY,
        "cannot allocate the session catalog");
    return NULL;
io_locked:
    if ( Directory != NULL ) (void)xrtDirClose(Directory);
    xrtMutexUnlock(g_MdoSessions.Lock);
    MdoSessionCatalogRelease(Catalog);
    MdoSessionsXrtError(Error, XWORK_ERROR_IO,
        "cannot scan the session catalog");
    return NULL;
}

static unsigned char MdoSessionsAsciiFold(unsigned char Byte)
{
    return Byte >= 'A' && Byte <= 'Z' ?
        (unsigned char)(Byte + ('a' - 'A')) : Byte;
}

static bool MdoSessionsContains(const char* Text, const char* Needle)
{
    const unsigned char* Start = (const unsigned char*)Text;
    size_t NeedleSize = strlen(Needle);
    if ( NeedleSize == 0u ) return true;
    for ( ; *Start != '\0'; ++Start ) {
        size_t i;
        for ( i = 0u; i < NeedleSize && Start[i] != '\0'; ++i ) {
            if ( MdoSessionsAsciiFold(Start[i]) !=
                 MdoSessionsAsciiFold((unsigned char)Needle[i]) ) break;
        }
        if ( i == NeedleSize ) return true;
    }
    return false;
}

static uint32 MdoSessionsStatusFlag(MdoSessionStatus Status)
{
    switch ( Status ) {
    case MDO_SESSION_ACTIVE: return MDO_SESSION_STATUS_ACTIVE_FLAG;
    case MDO_SESSION_ARCHIVED: return MDO_SESSION_STATUS_ARCHIVED_FLAG;
    case MDO_SESSION_TRASH: return MDO_SESSION_STATUS_TRASH_FLAG;
    default: return 0u;
    }
}

static bool MdoSessionsQueryMatch(const MdoSessionInfo* Info,
    const MdoSessionQuery* Query, uint32 StatusFlags)
{
    if ( Query->ProjectId != NULL &&
         strcmp(Info->ProjectId, Query->ProjectId) != 0 ) return false;
    if ( (MdoSessionsStatusFlag(Info->Status) & StatusFlags) == 0u ||
         (Query->PinnedOnly && !Info->Pinned) ) return false;
    if ( Query->Text == NULL || Query->Text[0] == '\0' ) return true;
    return MdoSessionsContains(Info->Title, Query->Text) ||
        MdoSessionsContains(Info->Id, Query->Text) ||
        MdoSessionsContains(Info->ProjectId, Query->Text) ||
        MdoSessionsContains(Info->ParentSessionId, Query->Text) ||
        MdoSessionsContains(Info->AgentId, Query->Text) ||
        MdoSessionsContains(Info->ModelId, Query->Text) ||
        MdoSessionsContains(Info->WorkspaceRoot, Query->Text);
}

MdoSessionCatalog* MdoSessionCatalogSearch(const MdoSessionQuery* Query,
    xwork_error* Error)
{
    MdoSessionCatalog* Source = NULL;
    MdoSessionCatalog* Result = NULL;
    uint32 StatusFlags;
    size_t Limit;
    size_t i;
    xworkErrorInit(Error);
    if ( Query == NULL || Query->Size < sizeof(*Query) ||
         (Query->ProjectId != NULL &&
          !MdoSessionsIdValid(Query->ProjectId, MDO_PROJECT_ID_CAPACITY)) ||
         (Query->Text != NULL &&
          !MdoSessionsTextValid(Query->Text, MDO_SESSION_SEARCH_TEXT_LIMIT,
            true)) ||
         (Query->StatusFlags & ~MDO_SESSION_STATUS_ALL_FLAGS) != 0u ||
         Query->Limit > MDO_SESSION_SEARCH_MAX ) {
        MdoSessionsError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "invalid session search query");
        return NULL;
    }
    StatusFlags = Query->StatusFlags != 0u ? Query->StatusFlags :
        MDO_SESSION_STATUS_ALL_FLAGS;
    Limit = Query->Limit != 0u ? Query->Limit : MDO_SESSION_SEARCH_DEFAULT;
    Source = MdoSessionCatalogSnapshot(Error);
    if ( Source == NULL ) return NULL;
    Result = (MdoSessionCatalog*)xrtCalloc(1u, sizeof(*Result));
    if ( Result == NULL ) goto memory;
    xrtAtomic32Init(&Result->Refs, 1u);
    Result->Generation = Source->Generation;
    if ( Source->DiagnosticCount != 0u ) {
        if ( !MdoSessionsGrow((void**)&Result->Diagnostics,
                &Result->DiagnosticCapacity, Source->DiagnosticCount,
                sizeof(*Result->Diagnostics)) ) goto memory;
        memcpy(Result->Diagnostics, Source->Diagnostics,
            Source->DiagnosticCount * sizeof(*Result->Diagnostics));
        Result->DiagnosticCount = Source->DiagnosticCount;
    }
    Result->Truncated = Source->Truncated;
    for ( i = 0u; i < Source->Count && Result->Count < Limit; ++i ) {
        if ( MdoSessionsQueryMatch(&Source->Items[i], Query, StatusFlags) &&
             !MdoSessionsCatalogItem(Result, &Source->Items[i]) ) goto memory;
    }
    MdoSessionCatalogRelease(Source);
    return Result;
memory:
    MdoSessionCatalogRelease(Source);
    MdoSessionCatalogRelease(Result);
    MdoSessionsError(Error, XWORK_ERROR_OUT_OF_MEMORY,
        "cannot allocate the session search result");
    return NULL;
}

MdoSessionCatalog* MdoSessionCatalogRef(MdoSessionCatalog* Catalog)
{
    uint32 Refs;
    if ( Catalog == NULL ) return NULL;
    Refs = xrtAtomic32Load(&Catalog->Refs, XMEMORY_ACQUIRE);
    for ( ; ; ) {
        uint32 Expected = Refs;
        if ( Refs == 0u || Refs == UINT32_MAX ) return NULL;
        if ( xrtAtomic32CompareExchange(&Catalog->Refs, &Expected, Refs + 1u,
                XMEMORY_ACQ_REL, XMEMORY_ACQUIRE) ) return Catalog;
        Refs = Expected;
    }
}

void MdoSessionCatalogRelease(MdoSessionCatalog* Catalog)
{
    uint32 Previous;
    if ( Catalog == NULL ) return;
    Previous = xrtAtomic32FetchSub(&Catalog->Refs, 1u, XMEMORY_ACQ_REL);
    if ( Previous > 1u ) return;
    if ( Previous == 0u ) abort();
    xrtFree(Catalog->Items);
    xrtFree(Catalog->Diagnostics);
    xrtFree(Catalog);
}

uint64 MdoSessionCatalogGeneration(const MdoSessionCatalog* Catalog)
{
    return Catalog != NULL ? Catalog->Generation : 0u;
}

size_t MdoSessionCatalogCount(const MdoSessionCatalog* Catalog)
{
    return Catalog != NULL ? Catalog->Count : 0u;
}

bool MdoSessionCatalogAt(const MdoSessionCatalog* Catalog, size_t Index,
    MdoSessionInfo* Info)
{
    uint32 Size;
    if ( Catalog == NULL || Index >= Catalog->Count || Info == NULL ||
         Info->Size < sizeof(*Info) ) return false;
    Size = Info->Size;
    *Info = Catalog->Items[Index];
    Info->Size = Size;
    return true;
}

size_t MdoSessionCatalogDiagnosticCount(const MdoSessionCatalog* Catalog)
{
    return Catalog != NULL ? Catalog->DiagnosticCount : 0u;
}

bool MdoSessionCatalogDiagnosticAt(const MdoSessionCatalog* Catalog,
    size_t Index, MdoSessionDiagnostic* Diagnostic)
{
    uint32 Size;
    if ( Catalog == NULL || Index >= Catalog->DiagnosticCount ||
         Diagnostic == NULL || Diagnostic->Size < sizeof(*Diagnostic) )
        return false;
    Size = Diagnostic->Size;
    *Diagnostic = Catalog->Diagnostics[Index];
    Diagnostic->Size = Size;
    return true;
}
