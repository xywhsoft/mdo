#include <stdio.h>
#include <string.h>

#include "internal.h"
#include "../../include/mdo/config.h"
#include "../../include/mdo/schedules.h"
#include "../../include/mdo/sessions.h"

#define MDO_API_INVENTORY_LIMIT 100u

typedef struct MdoApiProjectSummary {
    char Id[MDO_PROJECT_ID_CAPACITY];
    size_t Sessions;
    size_t Schedules;
} MdoApiProjectSummary;

static MdoApiProjectSummary* MdoApiProjectFindOrInsert(
    MdoApiProjectSummary Projects[MDO_API_INVENTORY_LIMIT], size_t* pCount,
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

bool MdoApiProjectsRoute(MdoApiContext* Context)
{
    MdoApiProjectSummary Projects[MDO_API_INVENTORY_LIMIT];
    xwork_error Error;
    MdoSessionCatalog* Sessions;
    MdoScheduleCatalog* Schedules;
    xvalue* Data = xrtValueObject();
    xvalue* Items = xrtValueArray();
    size_t Count = 0u;
    size_t Index;
    bool Truncated = false;
    bool Ok;

    memset(Projects, 0, sizeof(Projects));
    memset(&Error, 0, sizeof(Error));
    Sessions = MdoSessionCatalogSnapshot(&Error);
    memset(&Error, 0, sizeof(Error));
    Schedules = MdoScheduleCatalogSnapshot(&Error);
    Ok = Sessions != NULL && Schedules != NULL && Data != NULL && Items != NULL;
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
            MdoApiValueSetUInt(Item, "session_count",
                Projects[Index].Sessions) &&
            MdoApiValueSetUInt(Item, "schedule_count",
                Projects[Index].Schedules) &&
            MdoApiValueAppendTake(Items, &Item);
        xrtValueRelease(Item);
    }
    if ( Ok ) Ok =
        MdoApiValueSetBool(Data, "derived", true) &&
        MdoApiValueSetBool(Data, "truncated", Truncated) &&
        MdoApiValueSetUInt(Data, "session_generation",
            MdoSessionCatalogGeneration(Sessions)) &&
        MdoApiValueSetUInt(Data, "schedule_generation",
            MdoScheduleCatalogGeneration(Schedules)) &&
        MdoApiValueSetTake(Data, "items", &Items);
    xrtValueRelease(Items);
    MdoScheduleCatalogRelease(Schedules);
    MdoSessionCatalogRelease(Sessions);
    if ( !Ok ) { xrtValueRelease(Data); Data = NULL; }
    if ( Data == NULL ) return MdoApiReplyError(Context, 500u,
        "projects_unavailable", "Project summaries could not be created", NULL);
    return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
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
