#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"
#include "project_references.h"
#include "../../include/mdo/config.h"
#include "../../include/mdo/home.h"
#include "../../include/mdo/memory.h"
#include "../../include/mdo/projects.h"
#include "../../include/mdo/project_purge.h"
#include "../../include/mdo/runs.h"
#include "../../include/mdo/schedules.h"
#include "../../include/mdo/sessions.h"

#define MDO_API_INVENTORY_LIMIT 100u

typedef struct MdoApiProjectSummary {
    char Id[MDO_PROJECT_ID_CAPACITY];
    MdoProjectInfo Definition;
    bool Managed;
    size_t Sessions;
    size_t Schedules;
} MdoApiProjectSummary;

static MdoApiProjectSummary* MdoApiProjectFindOrInsert(
    MdoApiProjectSummary* Projects, size_t* pCount,
    bool* pTruncated, cstr Id)
{
    size_t Index;

    if ( Id == NULL || Id[0] == '\0' ) return NULL;
    for ( Index = 0u; Index < *pCount; Index++ ) {
        int Order = strcmp(Projects[Index].Id, Id);
        if ( Order == 0 ) return &Projects[Index];
        if ( Order > 0 ) break;
    }
    if ( *pCount == MDO_API_INVENTORY_LIMIT ) {
        *pTruncated = true;
        return NULL;
    }
    if ( Index < *pCount ) memmove(&Projects[Index + 1u], &Projects[Index],
        (*pCount - Index) * sizeof(Projects[0]));
    memset(&Projects[Index], 0, sizeof(Projects[Index]));
    (void)snprintf(Projects[Index].Id, sizeof(Projects[Index].Id), "%s", Id);
    (*pCount)++;
    return &Projects[Index];
}

static bool MdoApiProjectListRoute(MdoApiContext* Context)
{
    MdoApiProjectSummary* Projects = (MdoApiProjectSummary*)xrtCalloc(
        MDO_API_INVENTORY_LIMIT, sizeof(*Projects));
    MdoProjectInfo* Definitions = (MdoProjectInfo*)xrtCalloc(
        MDO_PROJECT_LIST_LIMIT, sizeof(*Definitions));
    xwork_error Error;
    MdoSessionCatalog* Sessions = NULL;
    MdoScheduleCatalog* Schedules = NULL;
    xvalue* Data = xrtValueObject();
    xvalue* Items = xrtValueArray();
    size_t Count = 0u;
    size_t DefinitionCount = 0u;
    size_t InvalidCount = 0u;
    size_t Index;
    bool Truncated = false;
    bool DefinitionTruncated = false;
    bool Ok = Projects != NULL && Definitions != NULL && Data != NULL &&
        Items != NULL;

    memset(&Error, 0, sizeof(Error));
    if ( Ok ) Ok = MdoProjectList(Definitions, MDO_PROJECT_LIST_LIMIT,
        &DefinitionCount, &InvalidCount, &DefinitionTruncated, &Error);
    for ( Index = 0u; Ok && Index < DefinitionCount; ++Index ) {
        MdoApiProjectSummary* Project = MdoApiProjectFindOrInsert(Projects,
            &Count, &Truncated, Definitions[Index].Id);
        if ( Project == NULL ) { Ok = false; break; }
        Project->Definition = Definitions[Index];
        Project->Managed = true;
    }
    if ( Ok ) Sessions = MdoSessionCatalogSnapshot(&Error);
    if ( Sessions != NULL ) Schedules = MdoScheduleCatalogSnapshot(&Error);
    Ok = Ok && Sessions != NULL && Schedules != NULL;
    for ( Index = 0u; Ok && Index < MdoSessionCatalogCount(Sessions); Index++ ) {
        MdoSessionInfo Info;
        MdoApiProjectSummary* Project;
        memset(&Info, 0, sizeof(Info)); Info.Size = sizeof(Info);
        Ok = MdoSessionCatalogAt(Sessions, Index, &Info);
        if ( !Ok ) break;
        Project = MdoApiProjectFindOrInsert(Projects, &Count, &Truncated,
            Info.ProjectId);
        if ( Project != NULL ) Project->Sessions++;
    }
    for ( Index = 0u; Ok && Index < MdoScheduleCatalogCount(Schedules); Index++ ) {
        MdoScheduleInfo Info;
        MdoApiProjectSummary* Project;
        memset(&Info, 0, sizeof(Info)); Info.Size = sizeof(Info);
        Ok = MdoScheduleCatalogAt(Schedules, Index, &Info);
        if ( !Ok ) break;
        Project = MdoApiProjectFindOrInsert(Projects, &Count, &Truncated,
            Info.ProjectId);
        if ( Project != NULL ) Project->Schedules++;
    }
    for ( Index = 0u; Ok && Index < Count; Index++ ) {
        xvalue* Item = xrtValueObject();
        Ok = Item != NULL &&
            MdoApiValueSetString(Item, "id", Projects[Index].Id) &&
            MdoApiValueSetString(Item, "name", Projects[Index].Managed ?
                Projects[Index].Definition.Name : Projects[Index].Id) &&
            MdoApiValueSetString(Item, "workspace_root",
                Projects[Index].Managed ?
                Projects[Index].Definition.WorkspaceRoot : "") &&
            MdoApiValueSetString(Item, "default_model_id",
                Projects[Index].Managed ?
                Projects[Index].Definition.DefaultModelId : "") &&
            MdoApiValueSetBool(Item, "managed", Projects[Index].Managed) &&
            MdoApiValueSetUInt(Item, "revision", Projects[Index].Managed ?
                Projects[Index].Definition.Revision : 0u) &&
            MdoApiValueSetUInt(Item, "session_count",
                Projects[Index].Sessions) &&
            MdoApiValueSetUInt(Item, "schedule_count",
                Projects[Index].Schedules) &&
            MdoApiValueAppendTake(Items, &Item);
        xrtValueRelease(Item);
    }
    if ( Ok ) Ok =
        MdoApiValueSetBool(Data, "derived", true) &&
        MdoApiValueSetBool(Data, "truncated",
            Truncated || DefinitionTruncated) &&
        MdoApiValueSetUInt(Data, "invalid_count", InvalidCount) &&
        MdoApiValueSetUInt(Data, "session_generation",
            MdoSessionCatalogGeneration(Sessions)) &&
        MdoApiValueSetUInt(Data, "schedule_generation",
            MdoScheduleCatalogGeneration(Schedules)) &&
        MdoApiValueSetTake(Data, "items", &Items);
    xrtValueRelease(Items);
    MdoScheduleCatalogRelease(Schedules);
    MdoSessionCatalogRelease(Sessions);
    xrtFree(Definitions);
    xrtFree(Projects);
    if ( !Ok ) { xrtValueRelease(Data); Data = NULL; }
    if ( Data == NULL ) return MdoApiReplyError(Context, 500u,
        "projects_unavailable", "Project summaries could not be created", NULL);
    return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
}

static bool MdoApiProjectString(const xvalue* Object, cstr Key,
    char* Output, size_t Capacity, bool Required, bool AllowEmpty,
    size_t* Present)
{
    const xvalue* Value = xrtValueObjectGet(Object, xrtStrView(Key));
    xstrview Text;
    if ( Value == NULL ) return !Required;
    ++*Present;
    if ( xrtValueType(Value) != XVALUE_STRING ||
         !xrtValueGetString(Value, &Text) ||
         (!AllowEmpty && Text.Size == 0u) ||
         Text.Size >= Capacity || memchr(Text.Data, 0, Text.Size) != NULL )
        return false;
    memcpy(Output, Text.Data, Text.Size);
    Output[Text.Size] = '\0';
    return true;
}

static bool MdoApiProjectCreateRoute(MdoApiContext* Context)
{
    MdoApiJsonBody Body;
    MdoApiBodyStatus BodyStatus;
    MdoProjectCreateOptions Options;
    MdoProjectInfo Info;
    xwork_error Error;
    xvalue* Data;
    char Id[MDO_PROJECT_ID_CAPACITY] = { 0 };
    char Name[MDO_PROJECT_NAME_CAPACITY] = { 0 };
    char Workspace[MDO_PROJECT_WORKSPACE_CAPACITY] = { 0 };
    char Model[MDO_PROJECT_MODEL_CAPACITY] = { 0 };
    char Tag[128];
    size_t Present = 0u;
    bool Valid;

    BodyStatus = MdoApiJsonBodyRead(Context, &Body);
    if ( BodyStatus != MDO_API_BODY_OK )
        return MdoApiReplyBodyError(Context, BodyStatus);
    Valid = xrtValueType(Body.Value) == XVALUE_OBJECT &&
        MdoApiProjectString(Body.Value, "id", Id, sizeof(Id), true, false,
            &Present) &&
        MdoApiProjectString(Body.Value, "name", Name, sizeof(Name), true,
            false,
            &Present) &&
        MdoApiProjectString(Body.Value, "workspace_root", Workspace,
            sizeof(Workspace), false, false, &Present) &&
        MdoApiProjectString(Body.Value, "default_model_id", Model,
            sizeof(Model), false, true, &Present) &&
        Present == xrtValueCount(Body.Value);
    MdoApiJsonBodyUnit(&Body);
    if ( !Valid ) return MdoApiReplyError(Context, 422u,
        "project_invalid", "The project definition is invalid", NULL);
    MdoProjectCreateOptionsInit(&Options);
    Options.Id = Id;
    Options.Name = Name;
    Options.WorkspaceRoot = Workspace;
    Options.DefaultModelId = Model;
    memset(&Info, 0, sizeof(Info)); Info.Size = sizeof(Info);
    memset(&Error, 0, sizeof(Error));
    if ( !MdoProjectCreate(&Options, &Info, &Error) ) {
        if ( Error.eCode == XWORK_ERROR_INVALID_ARGUMENT )
            return MdoApiReplyError(Context, 422u, "project_invalid",
                "The project fields are invalid", NULL);
        if ( strcmp(Error.sMessage, "project ID already exists") == 0 )
            return MdoApiReplyError(Context, 409u, "project_exists",
                "A project with this ID already exists", NULL);
        if ( Error.eCode == XWORK_ERROR_CONTEXT &&
             (strcmp(Error.sMessage, "project lifecycle is busy") == 0 ||
              strcmp(Error.sMessage, "project definition writer is busy") == 0) )
            return MdoApiReplyError(Context, 409u, "project_busy",
                "Project data is being changed; try again later", NULL);
        return MdoApiReplyError(Context, 503u, "project_unavailable",
            "The project could not be saved", NULL);
    }
    Data = xrtValueObject();
    Valid = Data != NULL &&
        MdoApiValueSetString(Data, "id", Info.Id) &&
        MdoApiValueSetString(Data, "name", Info.Name) &&
        MdoApiValueSetString(Data, "workspace_root", Info.WorkspaceRoot) &&
        MdoApiValueSetString(Data, "default_model_id", Info.DefaultModelId) &&
        MdoApiValueSetBool(Data, "managed", true) &&
        MdoApiValueSetUInt(Data, "revision", Info.Revision) &&
        MdoApiValueSetUInt(Data, "session_count", 0u) &&
        MdoApiValueSetUInt(Data, "schedule_count", 0u);
    if ( !Valid ) { xrtValueRelease(Data); Data = NULL; }
    if ( Data == NULL ) return MdoApiReplyError(Context, 500u,
        "project_result_unavailable",
        "The project was saved but its result is unavailable", NULL);
    snprintf(Tag, sizeof(Tag), "\"mdo-project-%s-%llu\"", Info.Id,
        (unsigned long long)Info.Revision);
    return MdoApiReplySuccessTakeEntityTag(Context, 201u, Data, Tag);
}

bool MdoApiProjectsRoute(MdoApiContext* Context)
{
    if ( Context->Request->head->MethodCode == XHTTP_METHOD_POST )
        return MdoApiProjectCreateRoute(Context);
    return MdoApiProjectListRoute(Context);
}

static bool MdoApiProjectReply(MdoApiContext* Context, uint16 Status,
    const MdoProjectInfo* Info)
{
    xvalue* Data = xrtValueObject();
    char Tag[128];
    bool Ok = Data != NULL &&
        MdoApiValueSetString(Data, "id", Info->Id) &&
        MdoApiValueSetString(Data, "name", Info->Name) &&
        MdoApiValueSetString(Data, "workspace_root", Info->WorkspaceRoot) &&
        MdoApiValueSetString(Data, "default_model_id",
            Info->DefaultModelId) &&
        MdoApiValueSetBool(Data, "managed", true) &&
        MdoApiValueSetUInt(Data, "revision", Info->Revision) &&
        MdoApiValueSetInt(Data, "created_at", Info->CreatedAt) &&
        MdoApiValueSetInt(Data, "updated_at", Info->UpdatedAt);
    if ( !Ok ) {
        xrtValueRelease(Data);
        return MdoApiReplyError(Context, 500u, "project_result_unavailable",
            "The project metadata is unavailable", NULL);
    }
    snprintf(Tag, sizeof(Tag), "\"mdo-project-%s-%llu\"", Info->Id,
        (unsigned long long)Info->Revision);
    return MdoApiReplySuccessTakeEntityTag(Context, Status, Data, Tag);
}

static bool MdoApiProjectNoBody(const MdoApiContext* Context)
{
    const xhttp1head* Head = Context->Request->head;
    return !(((Head->Flags & (uint32)XHTTP1_CONTENT_LENGTH) != 0u &&
              Head->ContentLength != 0u) ||
             (Head->Flags & (uint32)XHTTP1_TRANSFER_ENCODING) != 0u);
}

int MdoApiProjectExpectedRevision(const MdoApiContext* Context,
    const char* Id, uint64* Revision, bool* MatchesProject)
{
    static const char Prefix[] = "\"mdo-project-";
    const xhttpfield* Field = NULL;
    xhttpnext Next;
    xstrview Value;
    size_t Dash, Index;
    uint64 Number = 0u;
    size_t PrefixSize = sizeof(Prefix) - 1u;
    *MatchesProject = false;
    Next = xrtHttpFieldGetUnique(Context->Request->head->Fields,
        Context->Request->head->FieldCount, XRT_STR_LITERAL("If-Match"),
        &Field);
    if ( Next == XHTTP_NEXT_END ) return 0;
    if ( Next != XHTTP_NEXT_ITEM || Field == NULL ) return -1;
    Value = xrtStrTrim(Field->Value);
    if ( Value.Size < PrefixSize + 4u ||
         memcmp(Value.Data, Prefix, PrefixSize) != 0 ||
         Value.Data[Value.Size - 1u] != '"' ) return -1;
    Dash = Value.Size - 2u;
    while ( Dash > PrefixSize && Value.Data[Dash] != '-' ) --Dash;
    if ( Dash == PrefixSize || Value.Data[Dash] != '-' ||
         Dash + 1u >= Value.Size - 1u ) return -1;
    for ( Index = Dash + 1u; Index + 1u < Value.Size; ++Index ) {
        uint64 Digit;
        if ( Value.Data[Index] < '0' || Value.Data[Index] > '9' ) return -1;
        Digit = (uint64)(Value.Data[Index] - '0');
        if ( Number > (UINT64_MAX - Digit) / 10u ) return -1;
        Number = Number * 10u + Digit;
    }
    if ( Number == 0u ) return -1;
    *MatchesProject = Dash - PrefixSize == strlen(Id) &&
        memcmp(Value.Data + PrefixSize, Id, Dash - PrefixSize) == 0;
    *Revision = Number;
    return 1;
}

static bool MdoApiProjectMutationFailure(MdoApiContext* Context,
    MdoProjectMutationResult Result)
{
    switch ( Result ) {
    case MDO_PROJECT_MUTATION_INVALID:
        return MdoApiReplyError(Context, 422u, "project_invalid",
            "The project fields are invalid", NULL);
    case MDO_PROJECT_MUTATION_NOT_FOUND:
        return MdoApiReplyError(Context, 404u, "project_not_found",
            "The project definition was not found", NULL);
    case MDO_PROJECT_MUTATION_REVISION_CONFLICT:
        return MdoApiReplyError(Context, 412u, "revision_conflict",
            "The project changed; reload it before updating", NULL);
    case MDO_PROJECT_MUTATION_BUSY:
        return MdoApiReplyError(Context, 409u, "project_busy",
            "Project data is being changed; try again later", NULL);
    default:
        return MdoApiReplyError(Context, 503u, "project_unavailable",
            "The project definition could not be changed", NULL);
    }
}

static bool MdoApiProjectReadFailure(MdoApiContext* Context,
    const xwork_error* Error)
{
    if ( Error->eCode == XWORK_ERROR_INVALID_ARGUMENT )
        return MdoApiReplyError(Context, 400u, "invalid_project_path",
            "The project ID is invalid", NULL);
    return MdoApiReplyError(Context, 503u, "project_unavailable",
        "The project definition could not be read", NULL);
}

bool MdoApiProjectRoute(MdoApiContext* Context)
{
    char Id[MDO_PROJECT_ID_CAPACITY] = { 0 };
    char Name[MDO_PROJECT_NAME_CAPACITY] = { 0 };
    char Workspace[MDO_PROJECT_WORKSPACE_CAPACITY] = { 0 };
    char Model[MDO_PROJECT_MODEL_CAPACITY] = { 0 };
    MdoProjectInfo Info;
    MdoProjectCreateOptions Options;
    MdoApiJsonBody Body;
    MdoApiBodyStatus BodyStatus;
    MdoProjectMutationResult Result;
    xwork_error Error;
    xvalue* Data;
    uint64 ExpectedRevision = 0u;
    size_t Present = 0u;
    bool Found = false;
    bool MatchesProject = false;
    bool Valid;
    int Precondition;
    if ( Context->ParamCount != 1u || Context->Params[0].Size == 0u ||
         Context->Params[0].Size >= sizeof(Id) )
        return MdoApiReplyError(Context, 400u, "invalid_project_path",
            "The project ID is invalid", NULL);
    memcpy(Id, Context->Params[0].Data, Context->Params[0].Size);
    Id[Context->Params[0].Size] = '\0';
    memset(&Info, 0, sizeof(Info)); Info.Size = sizeof(Info);
    memset(&Error, 0, sizeof(Error));
    if ( Context->Request->head->MethodCode == XHTTP_METHOD_GET ||
         Context->Request->head->MethodCode == XHTTP_METHOD_HEAD ) {
        if ( !MdoProjectGet(Id, &Info, &Found, &Error) )
            return MdoApiProjectReadFailure(Context, &Error);
        if ( !Found ) return MdoApiReplyError(Context, 404u,
            "project_not_found", "The project definition was not found",
            NULL);
        return MdoApiProjectReply(Context, 200u, &Info);
    }
    if ( Context->Request->head->MethodCode == XHTTP_METHOD_DELETE &&
         !MdoApiProjectNoBody(Context) )
        return MdoApiReplyError(Context, 400u, "body_not_allowed",
            "This operation does not accept a request body", NULL);
    Precondition = MdoApiProjectExpectedRevision(Context, Id,
        &ExpectedRevision, &MatchesProject);
    if ( Precondition == 0 ) return MdoApiReplyError(Context, 428u,
        "precondition_required",
        "If-Match must contain the current project ETag", NULL);
    if ( Precondition < 0 ) return MdoApiReplyError(Context, 400u,
        "invalid_precondition",
        "If-Match must use the form \"mdo-project-ID-N\"", NULL);
    if ( !MdoProjectGet(Id, &Info, &Found, &Error) )
        return MdoApiProjectReadFailure(Context, &Error);
    if ( !Found ) return MdoApiReplyError(Context, 404u,
        "project_not_found", "The project definition was not found", NULL);
    if ( !MatchesProject || Info.Revision != ExpectedRevision )
        return MdoApiReplyError(Context, 412u, "revision_conflict",
            "The project changed; reload it before updating", NULL);
    if ( Context->Request->head->MethodCode == XHTTP_METHOD_DELETE ) {
        Result = MdoProjectUnregister(Id, ExpectedRevision, &Error);
        if ( Result != MDO_PROJECT_MUTATION_OK )
            return MdoApiProjectMutationFailure(Context, Result);
        Data = xrtValueObject();
        Valid = Data != NULL &&
            MdoApiValueSetString(Data, "id", Id) &&
            MdoApiValueSetUInt(Data, "revision", ExpectedRevision) &&
            MdoApiValueSetBool(Data, "removed", true);
        if ( !Valid ) {
            xrtValueRelease(Data);
            return MdoApiReplyError(Context, 500u,
                "project_result_unavailable",
                "The project was removed but its result is unavailable",
                NULL);
        }
        return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
    }
    BodyStatus = MdoApiJsonBodyRead(Context, &Body);
    if ( BodyStatus != MDO_API_BODY_OK )
        return MdoApiReplyBodyError(Context, BodyStatus);
    Valid = xrtValueType(Body.Value) == XVALUE_OBJECT &&
        MdoApiProjectString(Body.Value, "name", Name, sizeof(Name),
            true, false, &Present) &&
        MdoApiProjectString(Body.Value, "workspace_root", Workspace,
            sizeof(Workspace), true, false, &Present) &&
        MdoApiProjectString(Body.Value, "default_model_id", Model,
            sizeof(Model), true, true, &Present) &&
        Present == xrtValueCount(Body.Value);
    MdoApiJsonBodyUnit(&Body);
    if ( !Valid ) return MdoApiReplyError(Context, 422u,
        "project_invalid", "The project definition is invalid", NULL);
    MdoProjectCreateOptionsInit(&Options);
    Options.Id = Id;
    Options.Name = Name;
    Options.WorkspaceRoot = Workspace;
    Options.DefaultModelId = Model;
    Result = MdoProjectReplace(&Options, ExpectedRevision, &Info, &Error);
    if ( Result != MDO_PROJECT_MUTATION_OK )
        return MdoApiProjectMutationFailure(Context, Result);
    return MdoApiProjectReply(Context, 200u, &Info);
}

static bool MdoApiProjectPreviewPath(cstr Format, cstr Id,
    bool Directory, bool* pPresent)
{
    char Path[128];
    xfileinfo Info;
    int Written = snprintf(Path, sizeof(Path), Format, Id);

    if ( Written <= 0 || (size_t)Written >= sizeof(Path) ||
         !MdoHomeExternalStat(Path, pPresent, &Info) ) return false;
    return !*pPresent || Info.Type == (Directory ?
        XFILE_TYPE_DIRECTORY : XFILE_TYPE_FILE);
}

static int MdoApiProjectPreviewTargetCompare(const void* A, const void* B)
{
    return strcmp(((const MdoHomePurgeTarget*)A)->Path, ((const MdoHomePurgeTarget*)B)->Path);
}

/* This is an advisory inventory, not an authorization to remove files.  A
 * future purge transaction must rescan while holding all affected stores. */
bool MdoApiProjectPurgePreviewRoute(MdoApiContext* Context)
{
    char Id[MDO_PROJECT_ID_CAPACITY] = { 0 };
    char Tag[128];
    MdoProjectInfo Project;
    MdoSessionCatalog* Sessions = NULL;
    MdoScheduleCatalog* Schedules = NULL;
    MdoRunSnapshot* Runs = NULL;
    MdoMemorySnapshot* Memory = NULL;
    MdoProjectPurgeInventory* Inventory = NULL;
    MdoProjectPurgeInventoryInfo InventoryInfo;
    MdoProjectReferenceGuard* References = NULL;
    MdoHomePurgeTarget ReferenceTargets[2];
    MdoHomePurgeTarget* Candidates = NULL;
    size_t ReferenceCount = 0u;
    uint64 ReferenceBytes = 0u;
    bool SelectionPresent = false, GlobalDraftPresent = false;
    MdoScheduleExecutorSnapshot Executor;
    xwork_error Error;
    xvalue* Data = NULL;
    size_t SessionCount = 0u;
    size_t SessionRuntimeCount = 0u;
    size_t SessionDiagnosticCount = 0u;
    size_t SessionCatalogDiagnosticCount;
    size_t ScheduleCount = 0u;
    size_t ActiveRunCount = 0u;
    size_t Index;
    bool Found = false;
    bool MemoryPresent = false;
    bool ProjectBackupPresent = false;
    bool MemoryBackupPresent = false;
    bool SessionDirectoryPresent = false;
    bool MigrationSidecarPresent = false;
    bool Ok = false;

    if ( Context->ParamCount != 1u || Context->Params[0].Size == 0u ||
         Context->Params[0].Size >= sizeof(Id) )
        return MdoApiReplyError(Context, 400u, "invalid_project_path",
            "The project ID is invalid", NULL);
    memcpy(Id, Context->Params[0].Data, Context->Params[0].Size);
    Id[Context->Params[0].Size] = '\0';
    memset(&Project, 0, sizeof(Project)); Project.Size = sizeof(Project);
    memset(&Error, 0, sizeof(Error));
    if ( !MdoProjectGet(Id, &Project, &Found, &Error) )
        return MdoApiProjectReadFailure(Context, &Error);
    if ( !Found ) return MdoApiReplyError(Context, 404u,
        "project_not_found", "The project definition was not found", NULL);
    Inventory = MdoProjectPurgeInventoryCreate(Id, NULL, &Error);
    if ( Inventory == NULL || !MdoProjectPurgeInventoryGetInfo(Inventory, &InventoryInfo) ) goto done;
    Project = InventoryInfo.Project;
    /* Match the execution scope without making the shared preview a delete
     * token. Ordinary writers can change these records after it is released. */
    References = MdoApiProjectReferencesPreview(Id, &Error);
    if ( References == NULL ) goto done;
    ReferenceCount = MdoApiProjectReferencesCount(References);
    if ( ReferenceCount > 2u || InventoryInfo.Targets > MDO_PROJECT_PURGE_TARGET_LIMIT - ReferenceCount ||
         InventoryInfo.Files + InventoryInfo.Directories > MDO_PROJECT_PURGE_NODE_LIMIT - ReferenceCount ) goto done;
    for ( Index = 0u; Index < ReferenceCount; ++Index ) {
        if ( !MdoApiProjectReferencesAt(References, Index, &ReferenceTargets[Index]) ||
             ReferenceBytes > UINT64_MAX - ReferenceTargets[Index].Info.Size ) goto done;
        ReferenceBytes += ReferenceTargets[Index].Info.Size;
        SelectionPresent = SelectionPresent || strcmp(ReferenceTargets[Index].Path, "data/workspace-state.json") == 0;
        GlobalDraftPresent = GlobalDraftPresent || strcmp(ReferenceTargets[Index].Path, "data/draft.json") == 0;
    }
    if ( InventoryInfo.Bytes > UINT64_MAX - ReferenceBytes ) goto done;
    Candidates = (MdoHomePurgeTarget*)xrtCalloc(InventoryInfo.Targets + ReferenceCount, sizeof(*Candidates));
    if ( Candidates == NULL ) goto done;
    for ( Index = 0u; Index < InventoryInfo.Targets; ++Index )
        if ( !MdoProjectPurgeInventoryAt(Inventory, Index, &Candidates[Index]) ) goto done;
    for ( Index = 0u; Index < ReferenceCount; ++Index )
        Candidates[InventoryInfo.Targets + Index] = ReferenceTargets[Index];
    qsort(Candidates, InventoryInfo.Targets + ReferenceCount, sizeof(*Candidates), MdoApiProjectPreviewTargetCompare);
    if ( !MdoApiProjectPreviewPath("projects/%s.json.bak", Id,
            false, &ProjectBackupPresent) ||
         !MdoApiProjectPreviewPath("memory/projects/%s.json", Id,
            false, &MemoryPresent) ||
         !MdoApiProjectPreviewPath("memory/projects/%s.json.bak", Id,
            false, &MemoryBackupPresent) ||
         !MdoApiProjectPreviewPath("sessions/%s", Id,
            true, &SessionDirectoryPresent) ||
         !MdoApiProjectPreviewPath("migration/session-prompts/%s", Id,
            true, &MigrationSidecarPresent) ) goto done;
    Sessions = MdoSessionCatalogSnapshot(&Error);
    if ( Sessions == NULL ) goto done;
    Schedules = MdoScheduleCatalogSnapshot(&Error);
    if ( Schedules == NULL ) goto done;
    Runs = MdoRunSnapshotCreate(&Error);
    if ( Runs == NULL ) goto done;
    Memory = MdoMemorySnapshotCreate(MDO_MEMORY_PROJECT, Id, &Error);
    if ( Memory == NULL ) goto done;
    memset(&Executor, 0, sizeof(Executor)); Executor.Size = sizeof(Executor);
    if ( !MdoScheduleExecutorGetSnapshot(&Executor) ) goto done;
    for ( Index = 0u; Index < MdoSessionCatalogCount(Sessions); ++Index ) {
        MdoSessionInfo Info;
        memset(&Info, 0, sizeof(Info)); Info.Size = sizeof(Info);
        if ( !MdoSessionCatalogAt(Sessions, Index, &Info) ) goto done;
        if ( strcmp(Info.ProjectId, Id) != 0 ) continue;
        ++SessionCount;
        if ( Info.RuntimeOpen ) ++SessionRuntimeCount;
    }
    for ( Index = 0u; Index < MdoSessionCatalogDiagnosticCount(Sessions);
          ++Index ) {
        MdoSessionDiagnostic Diagnostic;
        memset(&Diagnostic, 0, sizeof(Diagnostic));
        Diagnostic.Size = sizeof(Diagnostic);
        if ( !MdoSessionCatalogDiagnosticAt(Sessions, Index,
                &Diagnostic) ) goto done;
        if ( strncmp(Diagnostic.Path, "sessions/", 9u) == 0 &&
             strncmp(Diagnostic.Path + 9u, Id, strlen(Id)) == 0 &&
             (Diagnostic.Path[9u + strlen(Id)] == '/' ||
              Diagnostic.Path[9u + strlen(Id)] == '\0') )
            ++SessionDiagnosticCount;
    }
    SessionCatalogDiagnosticCount =
        MdoSessionCatalogDiagnosticCount(Sessions);
    for ( Index = 0u; Index < MdoScheduleCatalogCount(Schedules); ++Index ) {
        MdoScheduleInfo Info;
        memset(&Info, 0, sizeof(Info)); Info.Size = sizeof(Info);
        if ( !MdoScheduleCatalogAt(Schedules, Index, &Info) ) goto done;
        if ( strcmp(Info.ProjectId, Id) == 0 ) ++ScheduleCount;
    }
    for ( Index = 0u; Index < MdoRunSnapshotCount(Runs); ++Index ) {
        MdoRunInfo Info;
        memset(&Info, 0, sizeof(Info)); Info.Size = sizeof(Info);
        if ( !MdoRunSnapshotAt(Runs, Index, &Info) ) goto done;
        if ( strcmp(Info.ProjectId, Id) == 0 && !Info.Terminal )
            ++ActiveRunCount;
    }
    Data = xrtValueObject();
    Ok = Data != NULL &&
        MdoApiValueSetString(Data, "id", Project.Id) &&
        MdoApiValueSetString(Data, "name", Project.Name) &&
        MdoApiValueSetUInt(Data, "revision", Project.Revision) &&
        MdoApiValueSetInt(Data, "created_at", InventoryInfo.Project.CreatedAt) &&
        MdoApiValueSetUInt(Data, "session_count", SessionCount) &&
        MdoApiValueSetUInt(Data, "session_runtime_count",
            SessionRuntimeCount) &&
        MdoApiValueSetUInt(Data, "session_diagnostic_count",
            SessionDiagnosticCount) &&
        MdoApiValueSetUInt(Data,
            "session_catalog_diagnostic_count_global",
            SessionCatalogDiagnosticCount) &&
        MdoApiValueSetUInt(Data, "schedule_count", ScheduleCount) &&
        MdoApiValueSetUInt(Data,
            "schedule_catalog_diagnostic_count_global",
            MdoScheduleCatalogDiagnosticCount(Schedules)) &&
        MdoApiValueSetBool(Data, "project_memory_present", MemoryPresent) &&
        MdoApiValueSetBool(Data, "project_definition_backup_present",
            ProjectBackupPresent) &&
        MdoApiValueSetBool(Data, "project_memory_backup_present",
            MemoryBackupPresent) &&
        MdoApiValueSetBool(Data, "session_directory_present",
            SessionDirectoryPresent) &&
        MdoApiValueSetBool(Data, "migration_sidecar_present",
            MigrationSidecarPresent) &&
        MdoApiValueSetUInt(Data, "project_memory_entry_count",
            MdoMemorySnapshotCount(Memory)) &&
        MdoApiValueSetUInt(Data, "active_interactive_run_count",
            ActiveRunCount) &&
        MdoApiValueSetUInt(Data, "active_scheduled_run_count_global",
            Executor.ActiveRuns) &&
        MdoApiValueSetUInt(Data, "session_generation",
            MdoSessionCatalogGeneration(Sessions)) &&
        MdoApiValueSetUInt(Data, "schedule_generation",
            MdoScheduleCatalogGeneration(Schedules)) &&
        MdoApiValueSetUInt(Data, "memory_generation",
            MdoMemorySnapshotGeneration(Memory)) &&
        MdoApiValueSetBool(Data, "advisory", true) &&
        MdoApiValueSetBool(Data, "selection_reference_present", SelectionPresent) &&
        MdoApiValueSetBool(Data, "global_draft_reference_present", GlobalDraftPresent) &&
        MdoApiValueSetUInt(Data, "target_count", InventoryInfo.Targets + ReferenceCount) &&
        MdoApiValueSetUInt(Data, "file_count", InventoryInfo.Files + ReferenceCount) &&
        MdoApiValueSetUInt(Data, "directory_count", InventoryInfo.Directories) &&
        MdoApiValueSetUInt(Data, "total_bytes", InventoryInfo.Bytes + ReferenceBytes) &&
        MdoApiValueSetBool(Data, "project_draft_present", InventoryInfo.ProjectDraft) &&
        MdoApiValueSetBool(Data, "project_draft_backup_present", InventoryInfo.ProjectDraftBackup) &&
        MdoApiValueSetUInt(Data, "schedule_backup_count", InventoryInfo.ScheduleBackups) &&
        MdoApiValueSetUInt(Data, "schedule_history_count", InventoryInfo.ScheduleHistories) &&
        MdoApiValueSetBool(Data, "shared_records_retained", true) &&
        MdoApiValueSetBool(Data, "workspace_files_removed", false);
    if ( Ok ) {
        xvalue* Targets = xrtValueArray();
        Ok = Targets != NULL;
        for ( Index = 0u; Ok && Index < InventoryInfo.Targets + ReferenceCount; ++Index ) {
            const MdoHomePurgeTarget* Target = &Candidates[Index];
            xvalue* Item = xrtValueObject();
            Ok = Item != NULL &&
                MdoApiValueSetString(Item, "path", Target->Path) &&
                MdoApiValueSetString(Item, "type", Target->Info.Type == XFILE_TYPE_DIRECTORY ?
                    "directory" : "file") && MdoApiValueAppendTake(Targets, &Item);
            xrtValueRelease(Item);
        }
        if ( Ok ) Ok = MdoApiValueSetTake(Data, "targets", &Targets);
        xrtValueRelease(Targets);
    }
done:
    xrtFree(Candidates);
    MdoApiProjectReferencesFree(References);
    MdoProjectPurgeInventoryFree(Inventory);
    MdoMemorySnapshotRelease(Memory);
    MdoRunSnapshotRelease(Runs);
    MdoScheduleCatalogRelease(Schedules);
    MdoSessionCatalogRelease(Sessions);
    if ( !Ok ) {
        xrtValueRelease(Data);
        return MdoApiReplyError(Context, 503u, "purge_preview_unavailable",
            "The project data inventory could not be completed", NULL);
    }
    snprintf(Tag, sizeof(Tag), "\"mdo-project-%s-%llu\"", Project.Id,
        (unsigned long long)Project.Revision);
    return MdoApiReplySuccessTakeEntityTag(Context, 200u, Data, Tag);
}

bool MdoApiPermissionsRoute(MdoApiContext* Context)
{
    MdoConfigSnapshot Snapshot;
    str Json = NULL;
    size_t JsonSize = 0u;
    xvalue* Effective = NULL;
    xvalue* Permissions = NULL;
    xvalue* Data = xrtValueObject();
    bool Ok;

    memset(&Snapshot, 0, sizeof(Snapshot)); Snapshot.Size = sizeof(Snapshot);
    Json = MdoConfigEffectiveJson(&JsonSize);
    if ( Json != NULL ) Effective = xrtJsonParse(xrtStrViewN(Json, JsonSize));
    xrtFree(Json);
    if ( Effective != NULL ) {
        const xvalue* Value = xrtValueObjectGet(Effective,
            XRT_STR_LITERAL("permissions"));
        if ( Value != NULL ) Permissions = xrtValueDeepClone(Value);
        const xvalue* Settings = xrtValueObjectGet(Effective,
            XRT_STR_LITERAL("settings"));
        const xvalue* Agent = Settings != NULL ? xrtValueObjectGet(Settings,
            XRT_STR_LITERAL("agent")) : NULL;
        const xvalue* Default = Agent != NULL ? xrtValueObjectGet(Agent,
            XRT_STR_LITERAL("permission_profile")) : NULL;
        if ( Permissions != NULL && Default != NULL &&
             !xrtValueObjectSet(Permissions, XRT_STR_LITERAL("default_profile"), Default) ) {
            xrtValueRelease(Permissions); Permissions = NULL;
        }
    }
    Ok = Data != NULL && Permissions != NULL &&
        MdoConfigGetSnapshot(&Snapshot) &&
        MdoApiValueSetUInt(Data, "revision", Snapshot.Revision) &&
        MdoApiValueSetTake(Data, "configuration", &Permissions);
    xrtValueRelease(Permissions);
    xrtValueRelease(Effective);
    if ( !Ok ) { xrtValueRelease(Data); Data = NULL; }
    if ( Data == NULL ) return MdoApiReplyError(Context, 500u,
        "permissions_unavailable", "Permission settings could not be created",
        NULL);
    return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
}
