#include <stdio.h>
#include <string.h>

#include "internal.h"
#include "project_references.h"
#include "../../include/mdo/home.h"
#include "../../include/mdo/sessions.h"

#define MDO_WORKSPACE_STATE_PATH "data/workspace-state.json"
#define MDO_WORKSPACE_STATE_MAX_BYTES 512u

typedef struct MdoWorkspaceState {
    char ProjectId[MDO_PROJECT_ID_CAPACITY];
    char SessionId[MDO_SESSION_ID_CAPACITY];
} MdoWorkspaceState;

static xmutex* g_MdoWorkspaceStateLock;

struct MdoProjectReferenceGuard {
    MdoProjectLease* Owner;
    MdoHomePurgeTarget Targets[2];
    size_t Count;
    bool SelectionLocked, DraftLocked;
};

bool MdoApiWorkspaceStateInit(void)
{
    if ( g_MdoWorkspaceStateLock != NULL ) return true;
    g_MdoWorkspaceStateLock = xrtMutexCreate();
    return g_MdoWorkspaceStateLock != NULL;
}

void MdoApiWorkspaceStateUnit(void)
{
    if ( g_MdoWorkspaceStateLock != NULL ) xrtMutexDestroy(g_MdoWorkspaceStateLock);
    g_MdoWorkspaceStateLock = NULL;
}

static bool MdoWorkspaceStateId(const xvalue* Object, cstr Name,
    char* Output, size_t Capacity)
{
    const xvalue* Value = xrtValueObjectGet(Object, xrtStrView(Name));
    xstrview Text;
    size_t Index;
    if ( xrtValueType(Value) != XVALUE_STRING ||
         !xrtValueGetString(Value, &Text) || Text.Size == 0u ||
         Text.Size >= Capacity ) return false;
    for ( Index = 0u; Index < Text.Size; ++Index ) {
        unsigned char Byte = (unsigned char)Text.Data[Index];
        if ( (Byte >= 'a' && Byte <= 'z') ||
             (Byte >= 'A' && Byte <= 'Z') ||
             (Byte >= '0' && Byte <= '9') || Byte == '-' || Byte == '_' ||
             (Index != 0u && Byte == '.') ) continue;
        return false;
    }
    memcpy(Output, Text.Data, Text.Size);
    Output[Text.Size] = '\0';
    return true;
}

static bool MdoWorkspaceStateRead(MdoWorkspaceState* State)
{
    bool Exists = false;
    xfileinfo Info;
    xfile File = NULL;
    char Bytes[MDO_WORKSPACE_STATE_MAX_BYTES + 1u];
    xjsonreadconfig Config;
    xvalue* Root = NULL;
    const xvalue* Version;
    uint64 Schema = 0u;
    int64 Signed = 0;
    bool Ok = false;

    memset(State, 0, sizeof(*State));
    if ( !MdoHomeExternalStat(MDO_WORKSPACE_STATE_PATH, &Exists, &Info) )
        return false;
    if ( !Exists ) return true;
    if ( Info.Type != XFILE_TYPE_FILE ||
         (Info.Available & XFILE_INFO_SIZE) == 0u ||
         Info.Size > MDO_WORKSPACE_STATE_MAX_BYTES ) return false;
    File = MdoHomeOpenRead(MDO_WORKSPACE_STATE_PATH);
    if ( File == NULL ||
         (Info.Size != 0u &&
          !xrtReadFull(File, Bytes, (size_t)Info.Size, NULL)) ) goto done;
    Bytes[Info.Size] = '\0';
    xrtJsonReadConfigInit(&Config);
    Config.MaxInputBytes = MDO_WORKSPACE_STATE_MAX_BYTES;
    Config.MaxDepth = 3u;
    Config.MaxValues = 8u;
    Config.MaxContainerItems = 4u;
    Root = xrtJsonRead(xrtStrViewN(Bytes, (size_t)Info.Size), &Config);
    Version = xrtValueObjectGet(Root, XRT_STR_LITERAL("schema_version"));
    if ( xrtValueType(Root) != XVALUE_OBJECT ||
         xrtValueCount(Root) != 3u ||
         !((xrtValueType(Version) == XVALUE_UINT &&
            xrtValueGetUInt(Version, &Schema) && Schema == 1u) ||
           (xrtValueType(Version) == XVALUE_INT &&
            xrtValueGetInt(Version, &Signed) && Signed == 1)) ||
         !MdoWorkspaceStateId(Root, "project_id", State->ProjectId,
            sizeof(State->ProjectId)) ||
         !MdoWorkspaceStateId(Root, "session_id", State->SessionId,
            sizeof(State->SessionId)) ) goto done;
    Ok = true;
done:
    xrtValueRelease(Root);
    if ( File != NULL && !xrtClose(File) ) Ok = false;
    return Ok;
}

static bool MdoWorkspaceStateWrite(const MdoWorkspaceState* State)
{
    xvalue* Root = xrtValueObject();
    char* Json = NULL;
    size_t Size = 0u;
    bool Ok = Root != NULL &&
        MdoApiValueSetUInt(Root, "schema_version", 1u) &&
        MdoApiValueSetString(Root, "project_id", State->ProjectId) &&
        MdoApiValueSetString(Root, "session_id", State->SessionId);
    if ( Ok ) Json = xrtJsonStringify(Root, false, &Size);
    Ok = Json != NULL && Size <= MDO_WORKSPACE_STATE_MAX_BYTES &&
        MdoHomeAtomicWrite(MDO_WORKSPACE_STATE_PATH, Json, Size, false);
    xrtFree(Json);
    xrtValueRelease(Root);
    return Ok;
}

void MdoApiProjectReferencesFree(MdoProjectReferenceGuard* Guard)
{
    if ( Guard == NULL ) return;
    if ( Guard->DraftLocked ) MdoApiDraftReferenceUnlock();
    if ( Guard->SelectionLocked ) xrtMutexUnlock(g_MdoWorkspaceStateLock);
    MdoProjectLeaseRelease(Guard->Owner);
    xrtFree(Guard);
}

MdoProjectReferenceGuard* MdoApiProjectReferencesBegin(const char* ProjectId,
    MdoProjectLease* Owner, xwork_error* Error)
{
    MdoProjectReferenceGuard* Guard;
    MdoWorkspaceState State;
    xfileinfo Before, After;
    bool Exists, AfterExists, DraftPresent;
    xworkErrorInit(Error);
    if ( g_MdoWorkspaceStateLock == NULL ||
         !MdoProjectLeaseProtects(Owner, ProjectId, MDO_PROJECT_LEASE_EXCLUSIVE) ) {
        if ( Error != NULL ) {
            Error->eCode = XWORK_ERROR_INVALID_ARGUMENT;
            snprintf(Error->sMessage, sizeof(Error->sMessage),
                "project references require a current exclusive lease");
        }
        return NULL;
    }
    Guard = (MdoProjectReferenceGuard*)xrtMalloc(sizeof(*Guard));
    if ( Guard == NULL ) {
        if ( Error != NULL ) Error->eCode = XWORK_ERROR_OUT_OF_MEMORY;
        return NULL;
    }
    memset(Guard, 0, sizeof(*Guard));
    Guard->Owner = MdoProjectLeaseRef(Owner);
    if ( Guard->Owner == NULL ) goto failed;
    xrtMutexLock(g_MdoWorkspaceStateLock);
    Guard->SelectionLocked = true;
    if ( !MdoHomeExternalStat(MDO_WORKSPACE_STATE_PATH, &Exists, &Before) ||
         (Exists && !MdoReferenceInfoUsable(&Before)) ||
         !MdoWorkspaceStateRead(&State) ||
         !MdoHomeExternalStat(MDO_WORKSPACE_STATE_PATH, &AfterExists, &After) ||
         Exists != AfterExists || (Exists && !MdoReferenceInfoSame(&Before, &After)) )
        goto failed;
    if ( Exists && strcmp(State.ProjectId, ProjectId) == 0 ) {
        snprintf(Guard->Targets[Guard->Count].Path,
            sizeof(Guard->Targets[Guard->Count].Path), "%s", MDO_WORKSPACE_STATE_PATH);
        Guard->Targets[Guard->Count++].Info = After;
    }
    if ( !MdoApiDraftReferenceLock(ProjectId, Owner,
            &Guard->Targets[Guard->Count], &DraftPresent) ) goto failed;
    Guard->DraftLocked = true;
    if ( DraftPresent ) ++Guard->Count;
    return Guard;
failed:
    MdoApiProjectReferencesFree(Guard);
    if ( Error != NULL ) {
        Error->eCode = XWORK_ERROR_IO;
        snprintf(Error->sMessage, sizeof(Error->sMessage),
            "global project references are invalid or changed during inspection");
    }
    return NULL;
}

size_t MdoApiProjectReferencesCount(const MdoProjectReferenceGuard* Guard)
{
    return Guard != NULL ? Guard->Count : 0u;
}

bool MdoApiProjectReferencesAt(const MdoProjectReferenceGuard* Guard,
    size_t Index, MdoHomePurgeTarget* Target)
{
    if ( Guard == NULL || Target == NULL || Index >= Guard->Count ) return false;
    *Target = Guard->Targets[Index];
    return true;
}

bool MdoApiWorkspaceStateRoute(MdoApiContext* Context)
{
    MdoWorkspaceState State;
    MdoApiJsonBody Body;
    MdoApiBodyStatus BodyStatus;
    MdoSession* Session;
    xwork_error Error;
    xvalue* Data;
    bool Ok;

    if ( Context->Request->head->MethodCode == XHTTP_METHOD_PUT ) {
        BodyStatus = MdoApiJsonBodyRead(Context, &Body);
        if ( BodyStatus != MDO_API_BODY_OK )
            return MdoApiReplyBodyError(Context, BodyStatus);
        Ok = Body.Size <= MDO_WORKSPACE_STATE_MAX_BYTES &&
            xrtValueType(Body.Value) == XVALUE_OBJECT &&
            xrtValueCount(Body.Value) == 2u &&
            MdoWorkspaceStateId(Body.Value, "project_id", State.ProjectId,
                sizeof(State.ProjectId)) &&
            MdoWorkspaceStateId(Body.Value, "session_id", State.SessionId,
                sizeof(State.SessionId));
        MdoApiJsonBodyUnit(&Body);
        if ( !Ok ) return MdoApiReplyError(Context, 422u,
            "workspace_state_invalid", "Expected valid project and session IDs",
            NULL);
        memset(&Error, 0, sizeof(Error));
        Session = MdoSessionLoad(State.ProjectId, State.SessionId, &Error);
        if ( Session == NULL ) return MdoApiReplyError(Context, 404u,
            "session_not_found", "The selected session does not exist", NULL);
        /* Keep the session's project lease through publication. Releasing it
         * before this write lets project exclusion race a stale selection. */
        xrtMutexLock(g_MdoWorkspaceStateLock);
        Ok = MdoWorkspaceStateWrite(&State);
        xrtMutexUnlock(g_MdoWorkspaceStateLock);
        MdoSessionRelease(Session);
        if ( !Ok )
            return MdoApiReplyError(Context, 503u, "workspace_state_unavailable",
                "The last session could not be saved", NULL);
    } else {
        xrtMutexLock(g_MdoWorkspaceStateLock);
        Ok = MdoWorkspaceStateRead(&State);
        xrtMutexUnlock(g_MdoWorkspaceStateLock);
        if ( !Ok ) return MdoApiReplyError(Context, 503u,
            "workspace_state_unavailable", "The last session could not be read", NULL);
    }

    Data = xrtValueObject();
    Ok = Data != NULL &&
        MdoApiValueSetString(Data, "project_id", State.ProjectId) &&
        MdoApiValueSetString(Data, "session_id", State.SessionId);
    if ( !Ok ) { xrtValueRelease(Data); Data = NULL; }
    if ( Data == NULL ) return MdoApiReplyError(Context, 503u,
        "workspace_state_unavailable", "The last session is unavailable", NULL);
    return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
}
