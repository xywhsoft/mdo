#include <stdio.h>
#include <string.h>

#include "internal.h"
#include "../../include/mdo/config.h"
#include "../../include/mdo/projects.h"
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
    char* Output, size_t Capacity, bool Required, size_t* Present)
{
    const xvalue* Value = xrtValueObjectGet(Object, xrtStrView(Key));
    xstrview Text;
    if ( Value == NULL ) return !Required;
    ++*Present;
    if ( xrtValueType(Value) != XVALUE_STRING ||
         !xrtValueGetString(Value, &Text) || Text.Size == 0u ||
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
        MdoApiProjectString(Body.Value, "id", Id, sizeof(Id), true,
            &Present) &&
        MdoApiProjectString(Body.Value, "name", Name, sizeof(Name), true,
            &Present) &&
        MdoApiProjectString(Body.Value, "workspace_root", Workspace,
            sizeof(Workspace), false, &Present) &&
        MdoApiProjectString(Body.Value, "default_model_id", Model,
            sizeof(Model), false, &Present) &&
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
        if ( Value != NULL ) Permissions = xrtValueRetain(Value);
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
