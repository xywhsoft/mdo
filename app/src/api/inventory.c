#include <stdio.h>
#include <string.h>

#include "internal.h"
#include "../../include/mdo/bootstrap.h"
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

static cstr MdoApiRunState(const xwork_event* Event)
{
    if ( Event->eKind == XWORK_EVENT_AGENT_DONE )
        return Event->bSuccess ? "succeeded" : "failed";
    if ( Event->eKind == XWORK_EVENT_ERROR ) return "failed";
    return "running";
}

typedef struct MdoApiRunSummary {
    uint64 RunId;
    uint64 AgentId;
    uint64 ParentRunId;
    uint64 LastEventId;
    int64 LastEventAt;
    cstr Model;
    cstr State;
} MdoApiRunSummary;

bool MdoApiRunsRoute(MdoApiContext* Context)
{
    xwork_runtime* Runtime = MdoBootstrapRuntime();
    xwork_error Error;
    xwork_event_snapshot* Probe = NULL;
    xwork_event_snapshot* Snapshot = NULL;
    xvalue* Data = xrtValueObject();
    xvalue* Items = xrtValueArray();
    uint64 Latest = 0u;
    uint64 After = 0u;
    MdoApiRunSummary Runs[MDO_API_INVENTORY_LIMIT];
    size_t RunCount = 0u;
    size_t EventCount;
    size_t Index;
    bool Ok;

    if ( Runtime == NULL ) return MdoApiReplyError(Context, 503u,
        "runtime_unavailable", "The run runtime is unavailable", NULL);
    memset(Runs, 0, sizeof(Runs));
    memset(&Error, 0, sizeof(Error));
    Probe = xworkRuntimeEventSnapshot(Runtime, 0u, 1u, &Error);
    if ( Probe != NULL ) Latest = xworkEventSnapshotLatestId(Probe);
    xworkEventSnapshotRelease(Probe);
    After = Latest > 512u ? Latest - 512u : 0u;
    memset(&Error, 0, sizeof(Error));
    Snapshot = xworkRuntimeEventSnapshot(Runtime, After, 512u, &Error);
    EventCount = Snapshot != NULL ? xworkEventSnapshotCount(Snapshot) : 0u;
    Ok = Snapshot != NULL && Data != NULL && Items != NULL;
    Index = EventCount;
    while ( Ok && Index != 0u ) {
        xwork_event Event;
        size_t RunIndex;
        Index--;
        memset(&Event, 0, sizeof(Event));
        if ( !xworkEventSnapshotAt(Snapshot, Index, &Event) ) { Ok = false; break; }
        if ( Event.uRunId == 0u ) continue;
        for ( RunIndex = 0u; RunIndex < RunCount; RunIndex++ )
            if ( Runs[RunIndex].RunId == Event.uRunId ) break;
        if ( RunIndex == RunCount ) {
            if ( RunCount == MDO_API_INVENTORY_LIMIT ) continue;
            Runs[RunIndex].RunId = Event.uRunId;
            Runs[RunIndex].AgentId = Event.uAgentId;
            Runs[RunIndex].ParentRunId = Event.uParentRunId;
            Runs[RunIndex].LastEventId = Event.uEventId;
            Runs[RunIndex].LastEventAt = Event.iOccurredAtUs;
            Runs[RunIndex].Model = Event.sModel;
            Runs[RunIndex].State = "running";
            RunCount++;
        } else if ( (Runs[RunIndex].Model == NULL ||
                    Runs[RunIndex].Model[0] == '\0') && Event.sModel != NULL ) {
            Runs[RunIndex].Model = Event.sModel;
        }
        if ( strcmp(Runs[RunIndex].State, "running") == 0 &&
             (Event.eKind == XWORK_EVENT_AGENT_DONE ||
              Event.eKind == XWORK_EVENT_ERROR) ) {
            Runs[RunIndex].State = MdoApiRunState(&Event);
        }
    }
    for ( Index = 0u; Ok && Index < RunCount; Index++ ) {
        xvalue* Item = xrtValueObject();
        Ok = Item != NULL &&
            MdoApiValueSetUInt(Item, "id", Runs[Index].RunId) &&
            MdoApiValueSetUInt(Item, "agent_id", Runs[Index].AgentId) &&
            MdoApiValueSetUInt(Item, "parent_run_id",
                Runs[Index].ParentRunId) &&
            MdoApiValueSetUInt(Item, "last_event_id",
                Runs[Index].LastEventId) &&
            MdoApiValueSetInt(Item, "last_event_at",
                Runs[Index].LastEventAt) &&
            MdoApiValueSetString(Item, "model", Runs[Index].Model) &&
            MdoApiValueSetString(Item, "state", Runs[Index].State) &&
            MdoApiValueAppendTake(Items, &Item);
        xrtValueRelease(Item);
    }
    if ( Ok ) Ok =
        MdoApiValueSetUInt(Data, "latest_event_id",
            xworkEventSnapshotLatestId(Snapshot)) &&
        MdoApiValueSetUInt(Data, "next_cursor",
            xworkEventSnapshotNextCursor(Snapshot)) &&
        MdoApiValueSetBool(Data, "history_lost",
            xworkEventSnapshotHistoryLost(Snapshot)) &&
        MdoApiValueSetBool(Data, "truncated",
            RunCount == MDO_API_INVENTORY_LIMIT && EventCount != 0u) &&
        MdoApiValueSetTake(Data, "items", &Items);
    xrtValueRelease(Items);
    xworkEventSnapshotRelease(Snapshot);
    if ( !Ok ) { xrtValueRelease(Data); Data = NULL; }
    if ( Data == NULL ) return MdoApiReplyError(Context, 500u,
        "runs_unavailable", "Recent runs could not be created", NULL);
    return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
}
