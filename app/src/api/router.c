#include <stdio.h>
#include <string.h>

#include "internal.h"
#include "purge_intent.h"
#include "write_admission.h"
#include "backup_upload.h"
#include "backup_preview.h"
#include "backup_restore.h"
#include "../../include/mdo/project_lifecycle.h"
#include "../../include/mdo/projects.h"
#include "../../include/mdo/home.h"
#include "../../include/mdo/update.h"

typedef struct MdoApiRoute {
    cstr Path;
    xhttpmethod Methods;
    cstr Allow;
    MdoApiRouteProc Proc;
    /* First capture is the project ID. Keep a lease through the entire
     * handler, including lazy repair and cleanup after a session is closed. */
    bool ProjectLease;
} MdoApiRoute;

static xatomic64 g_MdoApiFallbackId;
static bool g_MdoApiInitialized;

static const MdoApiRoute g_MdoApiRoutes[] = {
    { "/api/v1/distribution", XHTTP_METHOD_GET | XHTTP_METHOD_POST, "GET, POST, OPTIONS", MdoApiDistributionRoute, false },
    { "/api/v1/remote", XHTTP_METHOD_GET | XHTTP_METHOD_POST,
      "GET, POST, OPTIONS", MdoApiRemoteRoute, false },
    { "/api/v1/connector/state", XHTTP_METHOD_GET, "GET, OPTIONS", MdoApiRemoteRoute, false },
    { "/api/v1/connector/devices", XHTTP_METHOD_GET | XHTTP_METHOD_POST,
      "GET, POST, OPTIONS", MdoApiRemoteRoute, false },
    { "/api/v1/connector/ticket", XHTTP_METHOD_POST, "POST, OPTIONS", MdoApiRemoteRoute, false },
    { "/api/v1/live", XHTTP_METHOD_GET, "GET, OPTIONS", MdoApiLiveRoute, false },
    { "/api/v1/update", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD | XHTTP_METHOD_POST,
      "GET, HEAD, POST, OPTIONS", MdoApiUpdateRoute, false },
    { "/api/v1/update/download", XHTTP_METHOD_POST | XHTTP_METHOD_DELETE,
      "POST, DELETE, OPTIONS", MdoApiUpdateDownloadRoute, false },
    { "/api/v1/update/install", XHTTP_METHOD_POST,
      "POST, OPTIONS", MdoApiUpdateInstallRoute, false },
    { "/api/v1/update/exit", XHTTP_METHOD_POST,
      "POST, OPTIONS", MdoApiUpdateExitRoute, false },
    { "/api/v1/account", XHTTP_METHOD_GET, "GET, OPTIONS", MdoApiAccountRoute, false },
    { "/api/v1/account/login", XHTTP_METHOD_POST, "POST, OPTIONS", MdoApiAccountRoute, false },
    { "/api/v1/account/callback", XHTTP_METHOD_GET | XHTTP_METHOD_POST, "GET, POST, OPTIONS", MdoApiAccountRoute, false },
    { "/api/v1/account/cancel", XHTTP_METHOD_POST, "POST, OPTIONS", MdoApiAccountRoute, false },
    { "/api/v1/account/logout", XHTTP_METHOD_POST, "POST, OPTIONS", MdoApiAccountRoute, false },
    { "/api/v1/account/refresh", XHTTP_METHOD_POST, "POST, OPTIONS", MdoApiAccountRoute, false },
    { "/api/v1/account/website", XHTTP_METHOD_POST, "POST, OPTIONS", MdoApiAccountRoute, false },
    { "/api/v1/account/search/skip", XHTTP_METHOD_POST, "POST, OPTIONS", MdoApiAccountRoute, false },
    { "/api/v1/bootstrap", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiBootstrapRoute, false },
    { "/api/v1/settings", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiSettingsRoute, false },
    { "/api/v1/workspace-state",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD | XHTTP_METHOD_PUT,
      "GET, HEAD, PUT, OPTIONS", MdoApiWorkspaceStateRoute, false },
    { "/api/v1/pane-layout",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD | XHTTP_METHOD_PUT,
      "GET, HEAD, PUT, OPTIONS", MdoApiPaneLayoutRoute, false },
    { "/api/v1/models", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiModelsRoute, false },
    { "/api/v1/models/config", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiModelConfigRoute, false },
    { "/api/v1/models/setup", XHTTP_METHOD_POST, "POST, OPTIONS", MdoApiModelSetupRoute, false },
    { "/api/v1/models/discover", XHTTP_METHOD_POST, "POST, OPTIONS", MdoApiModelDiscoverRoute, false },
    { "/api/v1/models/test", XHTTP_METHOD_POST, "POST, OPTIONS", MdoApiModelTestRoute, false },
    { "/api/v1/agents", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiAgentsRoute, false },
    { "/api/v1/session-backups/uploads", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD | XHTTP_METHOD_POST,
      "GET, HEAD, POST, OPTIONS", MdoApiBackupUploadsRoute, false },
    { "/api/v1/session-backups/uploads/{upload}",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD | XHTTP_METHOD_DELETE,
      "GET, HEAD, DELETE, OPTIONS", MdoApiBackupUploadRoute, false },
    { "/api/v1/session-backups/uploads/{upload}/chunks/{offset}", XHTTP_METHOD_PUT,
      "PUT, OPTIONS", MdoApiBackupUploadChunkRoute, false },
    { "/api/v1/session-backups/uploads/{upload}/seal", XHTTP_METHOD_POST,
      "POST, OPTIONS", MdoApiBackupUploadSealRoute, false },
    { "/api/v1/session-backups/uploads/{upload}/preview", XHTTP_METHOD_POST,
      "POST, OPTIONS", MdoApiBackupPreviewStartRoute, false },
    { "/api/v1/session-backups/previews", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiBackupPreviewsRoute, false },
    { "/api/v1/session-backups/previews/{preview}", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD | XHTTP_METHOD_DELETE,
      "GET, HEAD, DELETE, OPTIONS", MdoApiBackupPreviewRoute, false },
    { "/api/v1/session-backups/previews/{preview}/restore-review", XHTTP_METHOD_POST,
      "POST, OPTIONS", MdoApiBackupRestoreReviewRoute, false },
    { "/api/v1/session-backups/restores/{request}/apply", XHTTP_METHOD_POST,
      "POST, OPTIONS", MdoApiBackupRestoreApplyRoute, false },
    { "/api/v1/session-backups/restores", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiBackupRestoresRoute, false },
    { "/api/v1/session-backups/restores/{request}", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD | XHTTP_METHOD_DELETE,
      "GET, HEAD, DELETE, OPTIONS", MdoApiBackupRestoreRoute, false },
    { "/api/v1/modules", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiModulesRoute, false },
    { "/api/v1/skills", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiSkillsRoute, false },
    { "/api/v1/mcp", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiMcpRoute, false },
    { "/api/v1/projects", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD |
      XHTTP_METHOD_POST, "GET, HEAD, POST, OPTIONS", MdoApiProjectsRoute, false },
    { "/api/v1/workspace/directories", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiDirectoriesRoute, false },
    { "/api/v1/projects/{project}", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD |
      XHTTP_METHOD_PUT | XHTTP_METHOD_DELETE,
      "GET, HEAD, PUT, DELETE, OPTIONS", MdoApiProjectRoute, true },
    { "/api/v1/projects/{project}/purge-preview",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiProjectPurgePreviewRoute, true },
    /* The purge coordinator acquires exclusion itself and looks up durable
     * receipts before reading the current (possibly deleted) definition. */
    { "/api/v1/projects/{project}/purge", XHTTP_METHOD_POST,
      "POST, OPTIONS", MdoApiProjectPurgeRoute, false },
    { "/api/v1/projects/{project}/purge-cancel", XHTTP_METHOD_POST,
      "POST, OPTIONS", MdoApiProjectPurgeCancelRoute, false },
    { "/api/v1/project-purges/{request}", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiProjectPurgeResultRoute, false },
    { "/api/v1/projects/{project}/purge-intent", XHTTP_METHOD_POST,
      "POST, OPTIONS", MdoApiProjectPurgeIntentPrepareRoute, false },
    { "/api/v1/project-purge-intent", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD | XHTTP_METHOD_DELETE,
      "GET, HEAD, DELETE, OPTIONS", MdoApiProjectPurgeIntentRoute, false },
    { "/api/v1/projects/{project}/workspace/files",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiProjectWorkspaceFilesRoute, false },
    { "/api/v1/memory/global", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD |
      XHTTP_METHOD_PUT, "GET, HEAD, PUT, OPTIONS", MdoApiMemoryCollectionRoute, false },
    { "/api/v1/memory/global/open-directory", XHTTP_METHOD_POST,
      "POST, OPTIONS", MdoApiMemoryOpenDirectoryRoute, false },
    { "/api/v1/memory/global/{entry}", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD |
      XHTTP_METHOD_DELETE, "GET, HEAD, DELETE, OPTIONS", MdoApiMemoryEntryRoute, false },
    { "/api/v1/memory/projects/{project}", XHTTP_METHOD_GET |
      XHTTP_METHOD_HEAD | XHTTP_METHOD_PUT,
      "GET, HEAD, PUT, OPTIONS", MdoApiMemoryCollectionRoute, true },
    { "/api/v1/memory/projects/{project}/open-directory", XHTTP_METHOD_POST,
      "POST, OPTIONS", MdoApiMemoryOpenDirectoryRoute, true },
    { "/api/v1/memory/projects/{project}/{entry}", XHTTP_METHOD_GET |
      XHTTP_METHOD_HEAD | XHTTP_METHOD_DELETE,
      "GET, HEAD, DELETE, OPTIONS", MdoApiMemoryEntryRoute, true },
    { "/api/v1/sessions",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD | XHTTP_METHOD_POST,
      "GET, HEAD, POST, OPTIONS", MdoApiSessionsRoute, false },
    { "/api/v1/draft",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD | XHTTP_METHOD_PUT,
      "GET, HEAD, PUT, OPTIONS", MdoApiDraftRoute, false },
    { "/api/v1/projects/{project}/draft",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD | XHTTP_METHOD_PUT,
      "GET, HEAD, PUT, OPTIONS", MdoApiDraftRoute, true },
    { "/api/v1/runs", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiRunsRoute, false },
    { "/api/v1/schedules",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD | XHTTP_METHOD_POST,
      "GET, HEAD, POST, OPTIONS", MdoApiSchedulesRoute, false },
    { "/api/v1/tasks", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiTasksRoute, false },
    { "/api/v1/approvals", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiApprovalsRoute, false },
    { "/api/v1/projects/{project}/sessions/{session}/asks",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiAsksRoute, false },
    { "/api/v1/projects/{project}/sessions/{session}/workspace/files",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiWorkspaceFilesRoute, false },
    { "/api/v1/artifacts", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiArtifactsRoute, false },
    { "/api/v1/permissions", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiPermissionsRoute, false },
    { "/api/v1/diagnostics", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiDiagnosticsRoute, false },
    { "/api/v1/storage", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiStorageRoute, false },
    { "/api/v1/migrations/legacy",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD | XHTTP_METHOD_POST,
      "GET, HEAD, POST, OPTIONS", MdoApiLegacyMigrationsRoute, false },
    { "/api/v1/operations", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiOperationsRoute, false },
    { "/api/v1/settings/{domain}/preview",
      XHTTP_METHOD_POST | XHTTP_METHOD_PATCH,
      "POST, PATCH, OPTIONS", MdoApiSettingsPreviewRoute, false },
    { "/api/v1/settings/{domain}",
      XHTTP_METHOD_PUT | XHTTP_METHOD_PATCH | XHTTP_METHOD_DELETE,
      "PUT, PATCH, DELETE, OPTIONS", MdoApiSettingsMutationRoute, false },
    { "/api/v1/models/reload", XHTTP_METHOD_POST,
      "POST, OPTIONS", MdoApiModelsReloadRoute, false },
    { "/api/v1/skills/reload", XHTTP_METHOD_POST,
      "POST, OPTIONS", MdoApiSkillsReloadRoute, false },
    { "/api/v1/modules/reload", XHTTP_METHOD_POST,
      "POST, OPTIONS", MdoApiModulesReloadRoute, false },
    { "/api/v1/mcp/reload", XHTTP_METHOD_POST,
      "POST, OPTIONS", MdoApiMcpReloadRoute, false },
    { "/api/v1/mcp/{server}/enabled", XHTTP_METHOD_PUT,
      "PUT, OPTIONS", MdoApiMcpEnabledRoute, false },
    { "/api/v1/mcp/{server}/disconnect", XHTTP_METHOD_POST,
      "POST, OPTIONS", MdoApiMcpDisconnectRoute, false },
    { "/api/v1/mcp/{server}/refresh", XHTTP_METHOD_POST,
      "POST, OPTIONS", MdoApiMcpRefreshRoute, false },
    { "/api/v1/operations/{operation}",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD | XHTTP_METHOD_DELETE,
      "GET, HEAD, DELETE, OPTIONS", MdoApiOperationRoute, false },
    { "/api/v1/projects/{project}/sessions/{session}",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD | XHTTP_METHOD_PATCH |
        XHTTP_METHOD_DELETE,
      "GET, HEAD, PATCH, DELETE, OPTIONS",
      MdoApiSessionRoute, true },
    { "/api/v1/projects/{project}/sessions/{session}/restore",
      XHTTP_METHOD_POST, "POST, OPTIONS", MdoApiSessionRestoreRoute, true },
    { "/api/v1/projects/{project}/sessions/{session}/profile",
      XHTTP_METHOD_PUT, "PUT, OPTIONS", MdoApiSessionProfileRoute, true },
    { "/api/v1/projects/{project}/sessions/{session}/history",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD, "GET, HEAD, OPTIONS",
      MdoApiSessionHistoryRoute, true },
    { "/api/v1/projects/{project}/sessions/{session}/attachments",
      XHTTP_METHOD_POST, "POST, OPTIONS", MdoApiAttachmentsRoute, true },
    { "/api/v1/projects/{project}/sessions/{session}/attachments/{attachment}/info",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD, "GET, HEAD, OPTIONS",
      MdoApiAttachmentInfoRoute, true },
    { "/api/v1/projects/{project}/sessions/{session}/attachments/{attachment}",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD | XHTTP_METHOD_DELETE,
      "GET, HEAD, DELETE, OPTIONS",
      MdoApiAttachmentRoute, true },
    { "/api/v1/projects/{project}/sessions/{session}/artifacts/{event}",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiSessionArtifactRoute, false },
    { "/api/v1/projects/{project}/sessions/{session}/recovery",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD, "GET, HEAD, OPTIONS",
      MdoApiSessionRecoveryRoute, true },
    { "/api/v1/projects/{project}/sessions/{session}/resume",
      XHTTP_METHOD_POST, "POST, OPTIONS", MdoApiSessionResumeRoute, true },
    { "/api/v1/projects/{project}/sessions/{session}/abandon",
      XHTTP_METHOD_POST, "POST, OPTIONS", MdoApiSessionAbandonRoute, true },
    { "/api/v1/projects/{project}/sessions/{session}/fork",
      XHTTP_METHOD_POST, "POST, OPTIONS", MdoApiSessionForkRoute, true },
    { "/api/v1/projects/{project}/sessions/{session}/truncate",
      XHTTP_METHOD_POST, "POST, OPTIONS", MdoApiSessionTruncateRoute, true },
    { "/api/v1/projects/{project}/sessions/{session}/clear",
      XHTTP_METHOD_POST, "POST, OPTIONS", MdoApiSessionClearRoute, true },
    { "/api/v1/projects/{project}/sessions/{session}/export",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD, "GET, HEAD, OPTIONS",
      MdoApiSessionExportRoute, true },
    { "/api/v1/projects/{project}/sessions/{session}/backup",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD, "GET, HEAD, OPTIONS",
      MdoApiSessionBackupRoute, true },
    { "/api/v1/projects/{project}/sessions/{session}/runs",
      XHTTP_METHOD_POST, "POST, OPTIONS", MdoApiRunStartRoute, true },
    { "/api/v1/runs/{run}",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD | XHTTP_METHOD_DELETE,
      "GET, HEAD, DELETE, OPTIONS", MdoApiRunRoute, false },
    { "/api/v1/schedules/{schedule}",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD | XHTTP_METHOD_PUT |
        XHTTP_METHOD_DELETE,
      "GET, HEAD, PUT, DELETE, OPTIONS", MdoApiScheduleRoute, false },
    { "/api/v1/schedules/{schedule}/enabled", XHTTP_METHOD_PUT,
      "PUT, OPTIONS", MdoApiScheduleEnabledRoute, false },
    { "/api/v1/schedules/{schedule}/history",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiScheduleHistoryRoute, false },
    { "/api/v1/schedules/{schedule}/run", XHTTP_METHOD_POST,
      "POST, OPTIONS", MdoApiScheduleRunRoute, false },
    { "/api/v1/tasks/{task}/output", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiTaskOutputRoute, false },
    { "/api/v1/tasks/{task}/asks", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiTaskAsksRoute, false },
    { "/api/v1/tasks/{task}/asks/{ask}", XHTTP_METHOD_PUT,
      "PUT, OPTIONS", MdoApiTaskAskRoute, false },
    { "/api/v1/approvals/{approval}", XHTTP_METHOD_PUT,
      "PUT, OPTIONS", MdoApiApprovalRoute, false },
    { "/api/v1/projects/{project}/sessions/{session}/asks/{ask}",
      XHTTP_METHOD_PUT, "PUT, OPTIONS", MdoApiAskRoute, false },
    { "/api/v1/artifacts/{artifact}",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiArtifactRoute, false },
    { "/api/v1/tasks/{task}",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD | XHTTP_METHOD_DELETE,
      "GET, HEAD, DELETE, OPTIONS", MdoApiTaskRoute, false },
    { "/api/v1/projects/{project}/sessions/{session}/turns",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD, "GET, HEAD, OPTIONS",
      MdoApiSessionTurnsRoute, true },
    { "/api/v1/projects/{project}/sessions/{session}/events",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD, "GET, HEAD, OPTIONS",
      MdoApiSessionEventsRoute, false },
    { "/api/v1/projects/{project}/sessions/{session}/draft",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD | XHTTP_METHOD_PUT,
      "GET, HEAD, PUT, OPTIONS", MdoApiDraftRoute, true },
    { "/api/v1/projects/{project}/sessions/{session}/draft/submissions",
      XHTTP_METHOD_POST, "POST, OPTIONS",
      MdoApiDraftSubmissionAppendRoute, true },
    { "/api/v1/projects/{project}/sessions/{session}/draft/submissions/{submission}",
      XHTTP_METHOD_PUT | XHTTP_METHOD_DELETE,
      "PUT, DELETE, OPTIONS", MdoApiDraftSubmissionRoute, true },
    { "/api/v1/projects/{project}/sessions/{session}/queue",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD | XHTTP_METHOD_POST,
      "GET, HEAD, POST, OPTIONS", MdoApiQueueRoute, true },
    { "/api/v1/projects/{project}/sessions/{session}/queue/discard-images/{attachment}",
      XHTTP_METHOD_POST, "POST, OPTIONS", MdoApiQueueDiscardRoute, true },
    { "/api/v1/projects/{project}/sessions/{session}/queue/{item}",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD | XHTTP_METHOD_PUT |
          XHTTP_METHOD_DELETE,
      "GET, HEAD, PUT, DELETE, OPTIONS", MdoApiQueueItemRoute, true },
    { "/api/v1/projects/{project}/sessions/{session}/todo",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiTodoRoute, true },
};

static bool MdoApiViewEqualText(xstrview View, cstr Text)
{
    size_t Size = strlen(Text);
    return View.Size == Size && memcmp(View.Data, Text, Size) == 0;
}

static bool MdoApiPath(xstrview Path)
{
    static const char Prefix[] = "/api/";
    return (Path.Size == 4u && memcmp(Path.Data, "/api", 4u) == 0) ||
        (Path.Size >= sizeof(Prefix) - 1u &&
         memcmp(Path.Data, Prefix, sizeof(Prefix) - 1u) == 0);
}

static bool MdoApiRouteMatch(MdoApiContext* Context, cstr Pattern)
{
    size_t PatternIndex = 0u;
    size_t PathIndex = 0u;

    Context->ParamCount = 0u;
    while ( Pattern[PatternIndex] != '\0' ) {
        if ( Pattern[PatternIndex] == '{' ) {
            size_t Close = PatternIndex + 1u;
            size_t Start = PathIndex;
            while ( Pattern[Close] != '\0' && Pattern[Close] != '}' ) Close++;
            if ( Pattern[Close] != '}' || Context->ParamCount ==
                    MDO_API_ROUTE_PARAM_MAX ) return false;
            while ( PathIndex < Context->Target.Path.Size &&
                    Context->Target.Path.Data[PathIndex] != '/' ) PathIndex++;
            if ( PathIndex == Start ) return false;
            Context->Params[Context->ParamCount++] = xrtStrViewN(
                Context->Target.Path.Data + Start, PathIndex - Start);
            PatternIndex = Close + 1u;
            continue;
        }
        if ( PathIndex >= Context->Target.Path.Size ||
             Pattern[PatternIndex] != Context->Target.Path.Data[PathIndex] )
            return false;
        PatternIndex++;
        PathIndex++;
    }
    return PathIndex == Context->Target.Path.Size;
}

static void MdoApiRequestId(char Output[MDO_API_REQUEST_ID_CAPACITY])
{
    static const char Hex[] = "0123456789abcdef";
    uint8 Random[12];
    size_t Index;

    if ( xrtSecureRandom(Random, sizeof(Random)) ) {
        memcpy(Output, "mdo-", 4u);
        for ( Index = 0u; Index < sizeof(Random); Index++ ) {
            Output[4u + Index * 2u] = Hex[Random[Index] >> 4u];
            Output[5u + Index * 2u] = Hex[Random[Index] & 15u];
        }
        Output[4u + sizeof(Random) * 2u] = '\0';
        return;
    }
    (void)snprintf(Output, MDO_API_REQUEST_ID_CAPACITY, "mdo-%016llx",
        (unsigned long long)(xrtAtomic64FetchAdd(&g_MdoApiFallbackId, 1u,
            XMEMORY_RELAXED) + 1u));
}

bool MdoApiInit(void)
{
    if ( g_MdoApiInitialized ) return true;
    if ( !MdoApiSessionsInit() ) return false;
    if ( !MdoApiWorkspaceStateInit() ) {
        MdoApiSessionsUnit();
        return false;
    }
    if ( !MdoApiDraftInit() ) {
        MdoApiWorkspaceStateUnit();
        MdoApiSessionsUnit();
        return false;
    }
    if ( !MdoApiQueueInit() ) {
        MdoApiWorkspaceStateUnit();
        MdoApiDraftUnit();
        MdoApiSessionsUnit();
        return false;
    }
    if ( !MdoApiAttachmentsInit() ) {
        MdoApiWorkspaceStateUnit();
        MdoApiQueueUnit();
        MdoApiDraftUnit();
        MdoApiSessionsUnit();
        return false;
    }
    if ( !MdoApiPurgeIntentInit() ) {
        MdoApiAttachmentsUnit();
        MdoApiWorkspaceStateUnit();
        MdoApiQueueUnit();
        MdoApiDraftUnit();
        MdoApiSessionsUnit();
        return false;
    }
    if ( !MdoApiWriteInit() ) {
        MdoApiPurgeIntentUnit();
        MdoApiAttachmentsUnit();
        MdoApiWorkspaceStateUnit();
        MdoApiQueueUnit();
        MdoApiDraftUnit();
        MdoApiSessionsUnit();
        return false;
    }
    if ( !MdoApiBackupUploadsInit() || !MdoApiBackupPreviewsInit() || !MdoApiBackupRestoresInit() || !MdoApiDownloadsInit() ||
         !MdoApiImageDownloadsInit() || !MdoApiLiveInit() || !MdoApiModelProbesInit() ) {
        MdoApiModelProbesUnit();
        MdoApiLiveUnit(); MdoApiLiveRelease();
        MdoApiImageDownloadsUnit(); MdoApiDownloadsUnit();
        MdoApiBackupRestoresUnit();
        MdoApiBackupPreviewsUnit();
        MdoApiBackupUploadsUnit();
        MdoApiWriteUnit(); MdoApiPurgeIntentUnit(); MdoApiAttachmentsUnit();
        MdoApiWorkspaceStateUnit(); MdoApiQueueUnit(); MdoApiDraftUnit();
        MdoApiSessionsUnit();
        return false;
    }
    xrtAtomic64Init(&g_MdoApiFallbackId, 0u);
    g_MdoApiInitialized = true;
    return true;
}

void MdoApiUnit(void)
{
    g_MdoApiInitialized = false;
    MdoApiModelProbesUnit();
    MdoApiLiveUnit();
    MdoApiDownloadsUnit();
    MdoApiImageDownloadsUnit();
    MdoApiBackupRestoresUnit();
    MdoApiBackupPreviewsUnit();
    MdoApiBackupUploadsUnit();
    MdoApiWriteUnit();
    MdoApiPurgeIntentUnit();
    MdoApiWorkspaceStateUnit();
    MdoApiDraftUnit();
    MdoApiQueueUnit();
    MdoApiAttachmentsUnit();
    MdoApiSessionsUnit();
}

static bool MdoApiRouteInvokeData(MdoApiContext* Context,
    const MdoApiRoute* Route)
{
    char ProjectId[MDO_PROJECT_ID_CAPACITY];
    MdoProjectLease* Lease;
    xwork_error Error;
    bool Ok;
    MdoHomeSnapshot Home;
    memset(&Home, 0, sizeof(Home)); Home.Size = sizeof(Home);
    if ( (Context->Request->head->MethodCode &
            (XHTTP_METHOD_GET | XHTTP_METHOD_HEAD)) == 0u && MdoHomeGetSnapshot(&Home) ) {
        /* These request handlers must replay committed/pending facts even when
         * the failure deliberately froze Home. Its storage boundary still
         * refuses every new mutation until recovery; import fencing stays. */
        if ( Home.RestartRequired && Route->Proc != MdoApiProjectPurgeRoute &&
             Route->Proc != MdoApiProjectPurgeCancelRoute &&
             Route->Proc != MdoApiProjectPurgeIntentPrepareRoute &&
             Route->Proc != MdoApiProjectPurgeIntentRoute &&
             Route->Proc != MdoApiBackupRestoreApplyRoute &&
             Route->Proc != MdoApiBackupRestoreRoute ) return MdoApiReplyError(Context, 503u,
            "home_restart_required", "Restart mdo before changing imported data", NULL);
        if ( Home.ImportInProgress ) return MdoApiReplyError(Context, 409u,
            "home_import_busy", "Home import is in progress; wait before changing data", NULL);
    }
    if ( !Route->ProjectLease ) return Route->Proc(Context);
    /* Invalid captures retain each endpoint's existing validation response.
     * They cannot pass its path validation or reach a durable write. */
    if ( Context->ParamCount == 0u || Context->Params[0].Size == 0u ||
         Context->Params[0].Size >= sizeof(ProjectId) )
        return Route->Proc(Context);
    memcpy(ProjectId, Context->Params[0].Data, Context->Params[0].Size);
    ProjectId[Context->Params[0].Size] = '\0';
    Lease = MdoProjectLeaseAcquire(ProjectId, MDO_PROJECT_LEASE_SHARED, &Error);
    if ( Lease == NULL ) {
        if ( Error.eCode == XWORK_ERROR_INVALID_ARGUMENT )
            return Route->Proc(Context);
        if ( Error.eCode == XWORK_ERROR_CONTEXT &&
             strcmp(Error.sMessage, "project lifecycle is busy") == 0 )
            return MdoApiReplyError(Context, 409u, "project_busy",
                "Project data is being changed; try again later", NULL);
        return MdoApiReplyError(Context, 503u, "project_unavailable",
            "Project data is unavailable", NULL);
    }
    Ok = Route->Proc(Context);
    MdoProjectLeaseRelease(Lease);
    return Ok;
}

static bool MdoApiRouteInvoke(MdoApiContext* Context, const MdoApiRoute* Route)
{
    xhttpmethod Method = Context->Request->head->MethodCode;
    bool Read = (Method & (XHTTP_METHOD_GET | XHTTP_METHOD_HEAD)) != 0u;
    bool Recovery = Route->Proc == MdoApiProjectPurgeCancelRoute ||
        Route->Proc == MdoApiProjectPurgeIntentRoute;
    bool Exclusive = Route->Proc == MdoApiProjectPurgeRoute || Route->Proc == MdoApiUpdateInstallRoute;
    bool Stop = (Route->Proc == MdoApiRunRoute || Route->Proc == MdoApiTaskRoute) && Method == XHTTP_METHOD_DELETE;
    bool Update = Route->Proc == MdoApiUpdateRoute || Route->Proc == MdoApiUpdateDownloadRoute ||
        Route->Proc == MdoApiUpdateInstallRoute || Route->Proc == MdoApiUpdateExitRoute;
    bool Ok;
    if (!Read && MdoUpdateInstalling())
        return MdoApiReplyError(Context,409,"update_installing","Native update confirmation or installation is in progress",NULL);
    if (!Read && !Update && !Stop && MdoUpdateBlocked())
        return MdoApiReplyError(Context,409,"update_required","Install the required update before continuing",NULL);
    if ( !Read && !Recovery && !MdoApiWriteEnter(Context, Exclusive, Stop) ) return true;
    if (!Read && MdoUpdateInstalling()) {
        MdoApiWriteLeave(Context);
        return MdoApiReplyError(Context,409,"update_installing","Installation is in progress",NULL);
    }
    if (!Read && !Update && !Stop && MdoUpdateBlocked()) {
        MdoApiWriteLeave(Context);
        return MdoApiReplyError(Context,409,"update_required","Install the required update before continuing",NULL);
    }
    Ok = MdoApiRouteInvokeData(Context, Route);
    MdoApiWriteLeave(Context);
    if ( (Context->Request->head->MethodCode & (XHTTP_METHOD_GET | XHTTP_METHOD_HEAD)) == 0u )
        MdoApiLiveChanged(NULL);
    return Ok;
}

XS_RequestResult MdoApiRequest(XS_HttpReq* pRequest)
{
    MdoApiContext Context;
    size_t Index;

    if ( pRequest == NULL || pRequest->head == NULL ||
         !g_MdoApiInitialized ) return XS_FALLBACK;
    memset(&Context, 0, sizeof(Context));
    Context.Request = pRequest;
    if ( !xrtHttpTargetParse(pRequest->head->Method, pRequest->head->Target,
            &Context.Target) || !MdoApiPath(Context.Target.Path) ) {
        return XS_FALLBACK;
    }
    MdoApiRequestId(Context.RequestId);
    for ( Index = 0u;
          Index < sizeof(g_MdoApiRoutes) / sizeof(g_MdoApiRoutes[0]);
          Index++ ) {
        const MdoApiRoute* Route = &g_MdoApiRoutes[Index];
        if ( strchr(Route->Path, '{') == NULL ) {
            Context.ParamCount = 0u;
            if ( !MdoApiViewEqualText(Context.Target.Path, Route->Path) )
                continue;
        } else if ( !MdoApiRouteMatch(&Context, Route->Path) ) continue;
        if ( pRequest->head->MethodCode == XHTTP_METHOD_OPTIONS ) {
            (void)MdoApiReplyOptions(&Context, Route->Allow);
        } else if ( (pRequest->head->MethodCode & Route->Methods) != 0u ) {
            (void)MdoApiRouteInvoke(&Context, Route);
        } else {
            (void)MdoApiReplyError(&Context, 405u, "method_not_allowed",
                "The request method is not allowed for this resource",
                Route->Allow);
        }
        return Context.Takeover ? XS_TAKEOVER : XS_OK;
    }
    (void)MdoApiReplyError(&Context, 404u, "route_not_found",
        "The requested API resource does not exist", NULL);
    return XS_OK;
}
