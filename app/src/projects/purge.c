#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../include/mdo/project_purge.h"
#include "../../include/mdo/runs.h"
#include "../api/project_references.h"
#include "../memory/internal.h"
#include "../schedules/internal.h"
#include "../sessions/internal.h"

static MdoProjectPurgeStatus MdoProjectPurgeError(xwork_error* Error,
    MdoProjectPurgeStatus Status, xwork_error_code Code, const char* Message)
{
    if ( Error != NULL ) {
        xworkErrorInit(Error); Error->eCode = Code;
        snprintf(Error->sMessage, sizeof(Error->sMessage), "%s", Message);
    }
    return Status;
}

/* Catalog limits and ambiguous project metadata cannot authorize deletion.
 * Known diagnostics for another project do not prevent removing this one. */
static bool MdoProjectPurgeRuntimeCheck(const char* ProjectId, xwork_error* Error)
{
    MdoSessionCatalog* Sessions = MdoSessionCatalogSnapshot(Error);
    MdoRunSnapshot* Runs = NULL;
    char Prefix[MDO_PROJECT_ID_CAPACITY + 16u];
    size_t i, PrefixSize;
    bool Ok = false;
    snprintf(Prefix, sizeof(Prefix), "sessions/%s", ProjectId);
    PrefixSize = strlen(Prefix);
    if ( Sessions == NULL ) return false;
    for ( i = 0u; i < MdoSessionCatalogDiagnosticCount(Sessions); ++i ) {
        MdoSessionDiagnostic Item;
        memset(&Item, 0, sizeof(Item)); Item.Size = sizeof(Item);
        if ( !MdoSessionCatalogDiagnosticAt(Sessions, i, &Item) ||
             strcmp(Item.Path, "sessions") == 0 ||
             (strncmp(Item.Path, Prefix, PrefixSize) == 0 &&
                (Item.Path[PrefixSize] == '\0' || Item.Path[PrefixSize] == '/')) ) goto invalid;
    }
    for ( i = 0u; i < MdoSessionCatalogCount(Sessions); ++i ) {
        MdoSessionInfo Item;
        memset(&Item, 0, sizeof(Item)); Item.Size = sizeof(Item);
        if ( !MdoSessionCatalogAt(Sessions, i, &Item) ) goto invalid;
        if ( strcmp(Item.ProjectId, ProjectId) == 0 && Item.RuntimeOpen ) goto busy;
    }
    Runs = MdoRunSnapshotCreate(Error);
    if ( Runs == NULL ) goto done;
    for ( i = 0u; i < MdoRunSnapshotCount(Runs); ++i ) {
        MdoRunInfo Item;
        memset(&Item, 0, sizeof(Item)); Item.Size = sizeof(Item);
        if ( !MdoRunSnapshotAt(Runs, i, &Item) ) goto invalid;
        if ( strcmp(Item.ProjectId, ProjectId) == 0 && !Item.Terminal ) goto busy;
    }
    Ok = true;
    goto done;
busy:
    (void)MdoProjectPurgeError(Error, MDO_PROJECT_PURGE_BUSY, XWORK_ERROR_CONTEXT,
        "project runtime objects are still active");
    goto done;
invalid:
    (void)MdoProjectPurgeError(Error, MDO_PROJECT_PURGE_UNAVAILABLE, XWORK_ERROR_IO,
        "project session metadata or catalog is incomplete");
done:
    MdoRunSnapshotRelease(Runs);
    MdoSessionCatalogRelease(Sessions);
    return Ok;
}

static int MdoProjectPurgeTargetCompare(const void* A, const void* B)
{
    return strcmp(((const MdoHomePurgeTarget*)A)->Path, ((const MdoHomePurgeTarget*)B)->Path);
}

/* Look up before acquiring exclusion: a terminal receipt remains readable
 * when a failed cache/cleanup step deliberately pins that project's gate. */
static bool MdoProjectPurgeReplay(const char* RequestId, const char* ProjectId,
    uint64 Revision, int64 CreatedAt, MdoProjectPurgeResult* Result,
    MdoProjectPurgeStatus* Status, xwork_error* Error)
{
    MdoHomePurgeReceipt Receipt;
    MdoHomeSnapshot Home;
    bool Found;
    if ( !MdoHomePurgeReceiptGet(RequestId, &Receipt, &Found) ) {
        *Status = MdoProjectPurgeError(Error, MDO_PROJECT_PURGE_UNAVAILABLE, XWORK_ERROR_IO,
            "cannot verify the project purge request/result record");
        return true;
    }
    if ( !Found ) return false;
    if ( strcmp(Receipt.Request.ProjectId, ProjectId) != 0 ||
         Receipt.Request.Revision != Revision || Receipt.Request.CreatedAt != (uint64)CreatedAt ) {
        *Status = MdoProjectPurgeError(Error, MDO_PROJECT_PURGE_REQUEST_CONFLICT, XWORK_ERROR_CONTEXT,
            "purge request ID belongs to another project version");
        return true;
    }
    Result->Targets = Receipt.Request.Targets; Result->Files = Receipt.Request.Files;
    Result->Directories = Receipt.Request.Directories; Result->Bytes = Receipt.Request.Bytes;
    Result->Schedules = Receipt.Request.Schedules;
    Result->Committed = Receipt.Committed;
    Result->Replayed = Receipt.Outcome != MDO_HOME_PURGE_PENDING;
    Result->SelectionRemoved = Result->Committed && Receipt.Request.Selection;
    Result->GlobalDraftRemoved = Result->Committed && Receipt.Request.GlobalDraft;
    memset(&Home, 0, sizeof(Home)); Home.Size = sizeof(Home);
    Result->RestartRequired = !MdoHomeGetSnapshot(&Home) || Home.RestartRequired ||
        Receipt.Outcome == MDO_HOME_PURGE_PENDING;
    if ( Result->RestartRequired )
        *Status = MdoProjectPurgeError(Error, MDO_PROJECT_PURGE_RESTART_REQUIRED, XWORK_ERROR_IO,
            "project purge result is recorded; restart to settle storage and caches");
    else if ( Result->Committed ) {
        *Status = MDO_PROJECT_PURGE_OK; xworkErrorInit(Error); xrtClearError();
    } else *Status = MdoProjectPurgeError(Error, MDO_PROJECT_PURGE_ABORTED, XWORK_ERROR_IO,
        "recorded project purge did not commit; use a new request after checking its result");
    return true;
}

static MdoProjectPurgeStatus MdoProjectPurgeExecuteImpl(const char* RequestId,
    const char* ProjectId, uint64 ExpectedRevision, int64 ExpectedCreatedAt,
    MdoProjectPurgeResult* Result, xwork_error* Error)
{
    MdoProjectLease* Owner = NULL;
    MdoProjectPurgeInventory* Inventory = NULL;
    MdoProjectReferenceGuard* References = NULL;
    MdoSchedulePurgeGuard* Schedules = NULL;
    MdoHomePurgeTarget* Targets = NULL;
    MdoProjectPurgeInventoryInfo Info;
    MdoProjectInfo Project;
    MdoHomeSnapshot Home;
    MdoHomePurgeRequest Request;
    xwork_error Failure, LocalError;
    char StorageMessage[256];
    MdoProjectPurgeStatus Status = MDO_PROJECT_PURGE_UNAVAILABLE;
    size_t i, ReferenceCount;
    bool Found, CacheOk, HasSessions = false, HasMemory = false;
    bool Selection = false, Draft = false, RequestConflict = false;
    if ( Result != NULL ) memset(Result, 0, sizeof(*Result));
    if ( Error == NULL ) Error = &LocalError;
    xworkErrorInit(Error);
    if ( Result == NULL || ProjectId == NULL || ExpectedRevision == 0u || ExpectedRevision == UINT64_MAX ||
         (RequestId != NULL && (!MdoHomePurgeRequestIdValid(RequestId) || ExpectedCreatedAt <= 0)) )
        return MdoProjectPurgeError(Error, MDO_PROJECT_PURGE_INVALID,
            XWORK_ERROR_INVALID_ARGUMENT, "purge requires a current project revision and result");
    if ( RequestId != NULL ) {
        snprintf(Result->RequestId, sizeof(Result->RequestId), "%s", RequestId);
        if ( MdoProjectPurgeReplay(RequestId, ProjectId, ExpectedRevision, ExpectedCreatedAt,
                Result, &Status, Error) ) return Status;
    }
    Owner = MdoProjectLeaseAcquire(ProjectId, MDO_PROJECT_LEASE_EXCLUSIVE, &Failure);
    if ( Owner == NULL ) {
        if ( Error != NULL ) *Error = Failure;
        if ( Failure.eCode == XWORK_ERROR_CONTEXT ) return MDO_PROJECT_PURGE_BUSY;
        return Failure.eCode == XWORK_ERROR_INVALID_ARGUMENT ?
            MDO_PROJECT_PURGE_INVALID : MDO_PROJECT_PURGE_UNAVAILABLE;
    }
    if ( RequestId != NULL && MdoProjectPurgeReplay(RequestId, ProjectId, ExpectedRevision,
            ExpectedCreatedAt, Result, &Status, Error) ) goto done;
    memset(&Project, 0, sizeof(Project)); Project.Size = sizeof(Project);
    if ( !MdoProjectGet(ProjectId, &Project, &Found, Error) ) goto done;
    if ( !Found ) {
        Status = MdoProjectPurgeError(Error, MDO_PROJECT_PURGE_NOT_FOUND, XWORK_ERROR_CONTEXT,
            "project definition was not found");
        goto done;
    }
    if ( Project.Revision != ExpectedRevision || (RequestId != NULL && Project.CreatedAt != ExpectedCreatedAt) ) {
        Status = MdoProjectPurgeError(Error, MDO_PROJECT_PURGE_REVISION_CONFLICT, XWORK_ERROR_CONTEXT,
            "project revision changed; refresh before purge");
        goto done;
    }
    if ( !MdoProjectPurgeRuntimeCheck(ProjectId, Error) ) {
        if ( Error != NULL && Error->eCode == XWORK_ERROR_CONTEXT ) Status = MDO_PROJECT_PURGE_BUSY;
        goto done;
    }
    Inventory = MdoProjectPurgeInventoryCreate(ProjectId, Owner, Error);
    if ( Inventory == NULL || !MdoProjectPurgeInventoryGetInfo(Inventory, &Info) ) goto done;
    if ( Info.Project.Revision != ExpectedRevision || (RequestId != NULL && Info.Project.CreatedAt != ExpectedCreatedAt) ) {
        Status = MdoProjectPurgeError(Error, MDO_PROJECT_PURGE_REVISION_CONFLICT, XWORK_ERROR_CONTEXT,
            "project changed during its exclusive purge scan");
        goto done;
    }
    References = MdoApiProjectReferencesBegin(ProjectId, Owner, Error);
    if ( References == NULL ) goto done;
    ReferenceCount = MdoApiProjectReferencesCount(References);
    if ( Info.Targets > MDO_PROJECT_PURGE_TARGET_LIMIT - ReferenceCount ||
         Info.Files + Info.Directories > MDO_PROJECT_PURGE_NODE_LIMIT - ReferenceCount ) {
        (void)MdoProjectPurgeError(Error, Status, XWORK_ERROR_LIMIT, "purge including references exceeds its limit");
        goto done;
    }
    Result->Targets = Info.Targets + ReferenceCount;
    Result->Files = Info.Files; Result->Directories = Info.Directories; Result->Bytes = Info.Bytes;
    Targets = (MdoHomePurgeTarget*)xrtCalloc(Result->Targets, sizeof(*Targets));
    if ( Targets == NULL ) {
        (void)MdoProjectPurgeError(Error, Status, XWORK_ERROR_OUT_OF_MEMORY, "cannot allocate coordinated purge targets");
        goto done;
    }
    for ( i = 0u; i < Info.Targets; ++i ) {
        if ( !MdoProjectPurgeInventoryAt(Inventory, i, &Targets[i]) ) goto done;
        HasSessions = HasSessions || strncmp(Targets[i].Path, "sessions/", 9u) == 0;
        HasMemory = HasMemory || strncmp(Targets[i].Path, "memory/projects/", 16u) == 0;
    }
    for ( i = 0u; i < ReferenceCount; ++i ) {
        MdoHomePurgeTarget* Target = &Targets[Info.Targets + i];
        if ( !MdoApiProjectReferencesAt(References, i, Target) ||
             Target->Info.Size > UINT64_MAX - Result->Bytes ) goto done;
        ++Result->Files; Result->Bytes += Target->Info.Size;
        Selection = Selection || strcmp(Target->Path, "data/workspace-state.json") == 0;
        Draft = Draft || strcmp(Target->Path, "data/draft.json") == 0;
    }
    if ( (HasSessions && MdoSessionManagerGeneration() == UINT64_MAX) ||
         (HasMemory && MdoMemoryManagerGeneration() == UINT64_MAX) ) {
        (void)MdoProjectPurgeError(Error, Status, XWORK_ERROR_LIMIT, "catalog generation is exhausted");
        goto done;
    }
    qsort(Targets, Result->Targets, sizeof(*Targets), MdoProjectPurgeTargetCompare);
    Schedules = MdoSchedulesPurgeBegin(ProjectId, Owner, Info.ScheduleGeneration, Error);
    if ( Schedules == NULL ) {
        if ( Error != NULL && Error->eCode == XWORK_ERROR_CONTEXT ) Status = MDO_PROJECT_PURGE_BUSY;
        goto done;
    }
    Result->Schedules = MdoSchedulesPurgeCount(Schedules);
    /* The durable commit marker, rather than cleanup's return value, decides
     * whether the in-memory catalogs must now forget the removed project. */
    if ( RequestId == NULL ) (void)MdoHomePurgeFiles(ProjectId, Targets, Result->Targets, &Result->Committed);
    else {
        memset(&Request, 0, sizeof(Request));
        snprintf(Request.Id, sizeof(Request.Id), "%s", RequestId);
        snprintf(Request.ProjectId, sizeof(Request.ProjectId), "%s", ProjectId);
        Request.Revision = ExpectedRevision; Request.CreatedAt = (uint64)ExpectedCreatedAt;
        Request.Targets = Result->Targets; Request.Files = Result->Files;
        Request.Directories = Result->Directories; Request.Schedules = Result->Schedules;
        Request.Bytes = Result->Bytes; Request.Selection = Selection; Request.GlobalDraft = Draft;
        (void)MdoHomePurgeFilesRequested(&Request, Targets, Result->Targets, &Result->Committed);
    }
    {
        const xerror* Cause = xrtGetError();
        RequestConflict = RequestId != NULL && Cause != NULL && xrtErrorKind(Cause) == XERR_ARGUMENT;
        snprintf(StorageMessage, sizeof(StorageMessage), "%s",
            Cause != NULL && xrtErrorMessage(Cause) != NULL ? xrtErrorMessage(Cause) :
            "project purge did not commit; its original data was restored");
    }
    memset(&Home, 0, sizeof(Home)); Home.Size = sizeof(Home);
    if ( !MdoHomeGetSnapshot(&Home) ) {
        Result->RestartRequired = true;
    } else Result->RestartRequired = Home.RestartRequired;
    if ( Result->Committed ) {
        Result->SelectionRemoved = Selection; Result->GlobalDraftRemoved = Draft;
        CacheOk = MdoSchedulesPurgeCommit(Schedules, &Failure);
        if ( HasSessions && !MdoSessionsProjectPurged(ProjectId, Owner) ) CacheOk = false;
        if ( HasMemory && !MdoMemoryProjectPurged(ProjectId, Owner) ) CacheOk = false;
        if ( !CacheOk || Result->RestartRequired ) {
            MdoSchedulesPurgeQuarantine(Schedules);
            Result->RestartRequired = true;
            Status = MdoProjectPurgeError(Error, MDO_PROJECT_PURGE_RESTART_REQUIRED, XWORK_ERROR_IO,
                "project purge committed; restart before further writes");
        } else {
            Status = MDO_PROJECT_PURGE_OK;
            xworkErrorInit(Error); xrtClearError();
        }
    } else if ( Result->RestartRequired ) {
        MdoSchedulesPurgeQuarantine(Schedules);
        Status = MdoProjectPurgeError(Error, MDO_PROJECT_PURGE_RESTART_REQUIRED, XWORK_ERROR_IO,
            "project purge is unresolved; restart to recover its journal");
    } else if ( RequestConflict ) {
        Status = MdoProjectPurgeError(Error, MDO_PROJECT_PURGE_REQUEST_CONFLICT, XWORK_ERROR_CONTEXT, StorageMessage);
    } else {
        Status = MdoProjectPurgeError(Error, MDO_PROJECT_PURGE_ABORTED, XWORK_ERROR_IO,
            StorageMessage);
    }
done:
    MdoSchedulesPurgeFree(Schedules);
    MdoApiProjectReferencesFree(References);
    MdoProjectPurgeInventoryFree(Inventory);
    xrtFree(Targets);
    MdoProjectLeaseRelease(Owner);
    return Status;
}

MdoProjectPurgeStatus MdoProjectPurgeExecute(const char* ProjectId,
    uint64 ExpectedRevision, MdoProjectPurgeResult* Result, xwork_error* Error)
{
    return MdoProjectPurgeExecuteImpl(NULL, ProjectId, ExpectedRevision, 0, Result, Error);
}

MdoProjectPurgeStatus MdoProjectPurgeExecuteRequested(const char* RequestId,
    const char* ProjectId, uint64 ExpectedRevision, int64 ExpectedCreatedAt,
    MdoProjectPurgeResult* Result, xwork_error* Error)
{
    if ( RequestId == NULL ) {
        if ( Result != NULL ) memset(Result, 0, sizeof(*Result));
        return MdoProjectPurgeError(Error, MDO_PROJECT_PURGE_INVALID, XWORK_ERROR_INVALID_ARGUMENT,
            "durable purge requires a client request ID");
    }
    return MdoProjectPurgeExecuteImpl(RequestId, ProjectId, ExpectedRevision, ExpectedCreatedAt, Result, Error);
}
