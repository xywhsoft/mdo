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
#include "../../include/mdo/http_url.h"
#include "../transfers/internal.h"

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
    { "/api/v1/application/services", XHTTP_METHOD_GET, "GET, OPTIONS", MdoApiApplicationRoute, false },
    { "/api/v1/application/call", XHTTP_METHOD_POST, "POST, OPTIONS", MdoApiApplicationRoute, false },
    { "/api/v1/application/events", XHTTP_METHOD_GET, "GET, OPTIONS", MdoApiApplicationRoute, false },
    { "/api/v1/commands", XHTTP_METHOD_GET, "GET, OPTIONS", MdoApiCommandsRoute, false },
    { "/api/v1/commands/project", XHTTP_METHOD_GET, "GET, OPTIONS", MdoApiCommandProjectRoute, false },
    { "/api/v1/commands/project/{command}", XHTTP_METHOD_GET|XHTTP_METHOD_DELETE, "GET, DELETE, OPTIONS", MdoApiCommandProjectRoute, false },
    { "/api/v1/commands/project/{command}/enabled", XHTTP_METHOD_POST, "POST, OPTIONS", MdoApiCommandProjectRoute, false },
    { "/api/v1/command-invocations", XHTTP_METHOD_POST, "POST, OPTIONS", MdoApiCommandInvocationRoute, false },
    { "/api/v1/command-invocations/{invocation}", XHTTP_METHOD_GET|XHTTP_METHOD_DELETE, "GET, DELETE, OPTIONS", MdoApiCommandInvocationRoute, false },
    { "/api/v1/device-transfers/capabilities", XHTTP_METHOD_GET, "GET, OPTIONS", MdoApiDeviceTransfersRoute, false },
    { "/api/v1/device-transfers/policy", XHTTP_METHOD_GET|XHTTP_METHOD_PUT, "GET, PUT, OPTIONS", MdoApiDeviceTransfersRoute, false },
    { "/api/v1/device-transfers/devices", XHTTP_METHOD_GET|XHTTP_METHOD_POST, "GET, POST, OPTIONS", MdoApiDeviceTransfersRoute, false },
    { "/api/v1/device-transfers/incoming/{transfer}", XHTTP_METHOD_GET|XHTTP_METHOD_POST, "GET, POST, OPTIONS", MdoApiDeviceTransfersRoute, false },
    { "/api/v1/device-transfers/incoming/{transfer}/{action}", XHTTP_METHOD_POST, "POST, OPTIONS", MdoApiDeviceTransfersRoute, false },
    { "/api/v1/device-transfers/incoming/{transfer}/{action}/{offset}", XHTTP_METHOD_PUT, "PUT, OPTIONS", MdoApiDeviceTransfersRoute, false },
    { "/api/v1/device-transfers/incoming/{transfer}/{action}/{index}/{offset}", XHTTP_METHOD_PUT, "PUT, OPTIONS", MdoApiDeviceTransfersRoute, false },
    { "/api/v1/device-transfers", XHTTP_METHOD_GET|XHTTP_METHOD_POST, "GET, POST, OPTIONS", MdoApiDeviceTransfersRoute, false },
    { "/api/v1/device-transfers/{transfer}", XHTTP_METHOD_GET|XHTTP_METHOD_POST, "GET, POST, OPTIONS", MdoApiDeviceTransfersRoute, false },
    { "/api/v1/ecosystem", XHTTP_METHOD_GET | XHTTP_METHOD_POST, "GET, POST, OPTIONS", MdoApiEcosystemRoute, false },
    { "/api/v1/extension-tasks", XHTTP_METHOD_GET|XHTTP_METHOD_POST, "GET, POST, OPTIONS", MdoApiExtensionTasksRoute, false },
    { "/api/v1/extension-tasks/{task}", XHTTP_METHOD_GET|XHTTP_METHOD_DELETE, "GET, DELETE, OPTIONS", MdoApiExtensionTasksRoute, false },
    { "/api/v1/extension-tasks/{task}/chunks/{offset}", XHTTP_METHOD_PUT, "PUT, OPTIONS", MdoApiExtensionTasksRoute, false },
    { "/api/v1/extension-tasks/{task}/file/{offset}", XHTTP_METHOD_GET, "GET, OPTIONS", MdoApiExtensionTasksRoute, false },
    { "/api/v1/extension-tasks/{task}/seal", XHTTP_METHOD_POST, "POST, OPTIONS", MdoApiExtensionTasksRoute, false },
    { "/api/v1/extension-tasks/{task}/install", XHTTP_METHOD_POST, "POST, OPTIONS", MdoApiExtensionTasksRoute, false },
    { "/api/v1/extension-tasks/{task}/forget", XHTTP_METHOD_DELETE, "DELETE, OPTIONS", MdoApiExtensionTasksRoute, false },
    { "/api/v1/extensions/{kind}", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD, "GET, HEAD, OPTIONS", MdoApiExtensionsRoute, false },
    { "/api/v1/extensions/{kind}/{id}/plan", XHTTP_METHOD_GET | XHTTP_METHOD_POST,
      "GET, POST, OPTIONS", MdoApiAgentPlanRoute, false },
    { "/api/v1/extensions/{kind}/{id}", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD | XHTTP_METHOD_PUT | XHTTP_METHOD_DELETE,
      "GET, HEAD, PUT, DELETE, OPTIONS", MdoApiExtensionsRoute, false },
    { "/api/v1/extensions/{kind}/{id}/files", XHTTP_METHOD_GET, "GET, OPTIONS", MdoApiExtensionsRoute, false },
    { "/api/v1/extensions/{kind}/{id}/file", XHTTP_METHOD_GET, "GET, OPTIONS", MdoApiExtensionsRoute, false },
    { "/api/v1/extensions/{kind}/{id}/pack", XHTTP_METHOD_POST, "POST, OPTIONS", MdoApiExtensionsRoute, false },
    { "/api/v1/extensions/{kind}/{id}/actions", XHTTP_METHOD_POST, "POST, OPTIONS", MdoApiExtensionsRoute, false },
    { "/api/v1/extensions/{kind}/{id}/enabled", XHTTP_METHOD_POST,
      "POST, OPTIONS", MdoApiExtensionsRoute, false },
    { "/api/v1/distribution", XHTTP_METHOD_GET | XHTTP_METHOD_POST, "GET, POST, OPTIONS", MdoApiDistributionRoute, false },
    { "/api/v1/cli-tools", XHTTP_METHOD_GET | XHTTP_METHOD_POST, "GET, POST, OPTIONS", MdoApiCliToolsRoute, false },
    { "/api/v1/remote", XHTTP_METHOD_GET | XHTTP_METHOD_POST,
      "GET, POST, OPTIONS", MdoApiRemoteRoute, false },
    { "/api/v1/connector/state", XHTTP_METHOD_GET, "GET, OPTIONS", MdoApiRemoteRoute, false },
    { "/api/v1/connector/devices", XHTTP_METHOD_GET | XHTTP_METHOD_POST,
      "GET, POST, OPTIONS", MdoApiRemoteRoute, false },
    { "/api/v1/connector/ticket", XHTTP_METHOD_POST, "POST, OPTIONS", MdoApiRemoteRoute, false },
    { "/api/v1/live", XHTTP_METHOD_GET, "GET, OPTIONS", MdoApiLiveRoute, false },
    { "/api/v1/connector/direct", XHTTP_METHOD_GET, "GET, OPTIONS", MdoApiDirectRoute, false },
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
    { "/api/v1/sidebar-layout", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD | XHTTP_METHOD_PUT,
      "GET, HEAD, PUT, OPTIONS", MdoApiSidebarLayoutRoute, false },
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
    { "/api/v1/tools", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiToolsRoute, false },
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
    { "/api/v1/projects/{project}/input-history",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD | XHTTP_METHOD_POST,
      "GET, HEAD, POST, OPTIONS", MdoApiPromptHistoryRoute, true },
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
    { "/api/v1/skills/sources", XHTTP_METHOD_GET, "GET, OPTIONS", MdoApiSkillSourcesRoute, false },
    { "/api/v1/skills/migrate", XHTTP_METHOD_POST, "POST, OPTIONS", MdoApiSkillMigrateRoute, false },
    { "/api/v1/skills/project", XHTTP_METHOD_GET, "GET, OPTIONS", MdoApiSkillProjectRoute, false },
    { "/api/v1/skills/project/{id}", XHTTP_METHOD_GET|XHTTP_METHOD_DELETE, "GET, DELETE, OPTIONS", MdoApiSkillProjectRoute, false },
    { "/api/v1/skills/project/{id}/enabled", XHTTP_METHOD_POST, "POST, OPTIONS", MdoApiSkillProjectRoute, false },
    { "/api/v1/skills/invoke", XHTTP_METHOD_POST,
      "POST, OPTIONS", MdoApiSkillInvokeRoute, false },
    { "/api/v1/skills/validate", XHTTP_METHOD_POST,
      "POST, OPTIONS", MdoApiSkillValidateRoute, false },
    { "/api/v1/modules/reload", XHTTP_METHOD_POST,
      "POST, OPTIONS", MdoApiModulesReloadRoute, false },
    { "/api/v1/mcp/oauth/callback", XHTTP_METHOD_GET, "GET, OPTIONS", MdoApiMcpAuthRoute, false },
    { "/api/v1/mcp/{server}/oauth", XHTTP_METHOD_GET|XHTTP_METHOD_DELETE, "GET, DELETE, OPTIONS", MdoApiMcpAuthRoute, false },
    { "/api/v1/mcp/{server}/oauth/start", XHTTP_METHOD_POST, "POST, OPTIONS", MdoApiMcpAuthRoute, false },
    { "/api/v1/mcp/{server}/oauth/open", XHTTP_METHOD_POST, "POST, OPTIONS", MdoApiMcpAuthRoute, false },
    { "/api/v1/mcp/{server}/context", XHTTP_METHOD_POST, "POST, OPTIONS", MdoApiMcpContextRoute, false },
    { "/api/v1/mcp/{server}/diagnostics", XHTTP_METHOD_GET, "GET, OPTIONS", MdoApiMcpContextRoute, false },
    { "/api/v1/mcp/reload", XHTTP_METHOD_POST,
      "POST, OPTIONS", MdoApiMcpReloadRoute, false },
    { "/api/v1/mcp/templates", XHTTP_METHOD_GET, "GET, OPTIONS", MdoApiMcpTemplatesRoute, false },
    { "/api/v1/mcp/connections", XHTTP_METHOD_GET|XHTTP_METHOD_POST, "GET, POST, OPTIONS", MdoApiMcpConnectionsRoute, false },
    { "/api/v1/mcp/connections/{connection}", XHTTP_METHOD_GET, "GET, OPTIONS", MdoApiMcpConnectionsRoute, false },
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
    /* The session purge handler owns its exclusive project lease. */
    { "/api/v1/projects/{project}/sessions/{session}/purge",
      XHTTP_METHOD_POST, "POST, OPTIONS", MdoApiSessionPurgeRoute, false },
    { "/api/v1/projects/{project}/sessions/{session}/permission",
      XHTTP_METHOD_PUT, "PUT, OPTIONS", MdoApiSessionPermissionRoute, true },
    { "/api/v1/projects/{project}/sessions/{session}/shared-files",
      XHTTP_METHOD_POST, "POST, OPTIONS", MdoApiSharedFilesRoute, true },
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
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD | XHTTP_METHOD_PUT | XHTTP_METHOD_DELETE,
      "GET, HEAD, PUT, DELETE, OPTIONS",
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
    { "/api/v1/projects/{project}/sessions/{session}/conversation",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD, "GET, HEAD, OPTIONS",
      MdoApiSessionConversationRoute, true },
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
    { "/api/v1/projects/{project}/sessions/{session}/queue/actions",
      XHTTP_METHOD_POST, "POST, OPTIONS", MdoApiQueueActionsRoute, true },
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

/* This control API is served on loopback. A Host chosen by a remote domain
 * must never expose Home, account state or the write token through DNS
 * rebinding. Native bridges forward a loopback authority too. Origin may be
 * absent for CLI clients and top-level PKCE navigation; when present it must
 * match this exact local authority, even on reads and OPTIONS. */
bool MdoApiLocalRequest(const XS_HttpReq* Request)
{
    const xhttpfield *Host = NULL, *Origin = NULL;
    if (!Request || !Request->head || !Request->server ||
        xrtHttpFieldGetUnique(Request->head->Fields, Request->head->FieldCount,
            XRT_STR_LITERAL("Host"), &Host) != XHTTP_NEXT_ITEM || !Host) return false;
    uint16 Port = Request->tls ? Request->server->PortTLS :
        Request->server->PortBound ? Request->server->PortBound : Request->server->Port;
    if (!Port) return false;
    const char* Names[] = { "127.0.0.1", "localhost", "[::1]" };
    char Authority[64]; bool Local = false;
    for (size_t i = 0u; i < sizeof(Names) / sizeof(Names[0]); ++i) {
        int Length = snprintf(Authority, sizeof(Authority), "%s:%u", Names[i], (unsigned)Port);
        if (Length > 0 && Host->Value.Size == (size_t)Length &&
            MdoHttpUrlAsciiEqual(Host->Value.Data, Authority, (size_t)Length)) Local = true;
        if (Port == (Request->tls ? 443u : 80u) &&
            Host->Value.Size == strlen(Names[i]) &&
            MdoHttpUrlAsciiEqual(Host->Value.Data, Names[i], Host->Value.Size)) Local = true;
    }
    if (!Local) return false;
    xhttpnext Next = xrtHttpFieldGetUnique(Request->head->Fields, Request->head->FieldCount,
        XRT_STR_LITERAL("Origin"), &Origin);
    if (Next == XHTTP_NEXT_END) return true;
    if (Next != XHTTP_NEXT_ITEM || !Origin) return false;
    const char* Scheme = Request->tls ? "https://" : "http://";
    size_t Size = strlen(Scheme);
    return Origin->Value.Size == Size + Host->Value.Size &&
        !memcmp(Origin->Value.Data, Scheme, Size) &&
        MdoHttpUrlAsciiEqual(Origin->Value.Data + Size, Host->Value.Data, Host->Value.Size);
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
    if ( !MdoApiDecisionsInit() || !MdoApiBackupUploadsInit() || !MdoApiBackupPreviewsInit() || !MdoApiBackupRestoresInit() || !MdoApiDownloadsInit() ||
         !MdoApiImageDownloadsInit() || !MdoApiLiveInit() || !MdoApiEcosystemInit() || !MdoApiExtensionsInit() || !MdoApiModelProbesInit() ) {
        MdoApiModelProbesUnit();
        MdoApiExtensionsUnit();
        MdoApiLiveUnit(); MdoApiLiveRelease();
        MdoApiImageDownloadsUnit(); MdoApiDownloadsUnit();
        MdoApiBackupRestoresUnit();
        MdoApiBackupPreviewsUnit();
        MdoApiBackupUploadsUnit();
        MdoApiDecisionsUnit();
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
    MdoApiQueueStop();
    MdoCliStop();
    g_MdoApiInitialized = false;
    MdoApiModelProbesUnit();
    MdoApiEcosystemUnit();
    MdoApiExtensionsUnit();
    MdoApiLiveUnit();
    MdoApiDownloadsUnit();
    MdoApiImageDownloadsUnit();
    MdoApiBackupRestoresUnit();
    MdoApiBackupPreviewsUnit();
    MdoApiBackupUploadsUnit();
    MdoApiDecisionsUnit();
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
    /* The dispatch envelope authenticates its caller but owns no product write
     * lease: the selected service acquires the correct shared/exclusive gate. */
    if(Route->Proc==MdoApiApplicationRoute){
        if(!Read&&!MdoApiWriteEnter(Context,false,false))return true;
        MdoApiWriteLeave(Context);return Route->Proc(Context);
    }
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

bool MdoApiApplicationInvoke(const mdo_sdk_context_v1* Caller,cstr Method,cstr Target,
    const xvalue* Body,const xvalue* Headers,xbytesview Binary,MdoApplicationResponse* Response,mdo_error_v1* Error)
{
    if(!g_MdoApiInitialized)return MdoApplicationError(Error,"sdk_unavailable","Product services are not initialized","dispatch",503,false);
    xhttp1head Head={0};XS_HttpReq Request={0};MdoApiContext C={0};xhttpfield Fields[20];size_t Count=0;
    Head.Method=xrtStrView(Method);
    const struct {cstr Name;xhttpmethod Code;} Methods[]={{"GET",XHTTP_METHOD_GET},{"POST",XHTTP_METHOD_POST},{"PUT",XHTTP_METHOD_PUT},{"PATCH",XHTTP_METHOD_PATCH},{"DELETE",XHTTP_METHOD_DELETE}};
    for(size_t i=0;i<sizeof(Methods)/sizeof(*Methods);i++)if(!strcmp(Method,Methods[i].Name))Head.MethodCode=Methods[i].Code;
    Head.Target=xrtStrView(Target);Request.head=&Head;Request.server=MdoApplicationServer();C.Request=&Request;
    C.ApplicationContext=Caller;C.ApplicationBody=Body;C.ApplicationBinary=Binary;C.ApplicationResponse=Response;
    if(!Head.MethodCode||!xrtHttpTargetParse(Head.Method,Head.Target,&C.Target))return MdoApplicationError(Error,"sdk_arguments_invalid","Invalid product operation","resolve",400,false);
    for(size_t i=0;i<xrtValueCount(Headers);i++){
        if(Count>=18)return MdoApplicationError(Error,"sdk_arguments_invalid","Too many service headers","resolve",400,false);
        cstr Name=SdkKey(Headers,i),Value=SdkField(Headers,Name);if(!Value)return MdoApplicationError(Error,"sdk_arguments_invalid","Service headers must contain strings","resolve",400,false);
        Fields[Count++]=(xhttpfield){xrtStrView(Name),xrtStrView(Value)};
    }
    char Token[MDO_API_WRITE_TOKEN_CAPACITY];
    MdoApplicationInvocation* Invocation=Caller->Opaque;
    if(strcmp(Method,"GET")){
        if(Invocation&&Invocation->WriteToken[0])snprintf(Token,sizeof(Token),"%s",Invocation->WriteToken);
        else if(!MdoApiWriteToken(Token))return MdoApplicationError(Error,"sdk_unavailable","Write admission unavailable","dispatch",503,false);
        Fields[Count++]=(xhttpfield){XRT_STR_LITERAL("X-Mdo-Write-Token"),xrtStrView(Token)};
    }
    if(Body&&!Binary.Data)Fields[Count++]=(xhttpfield){XRT_STR_LITERAL("Content-Type"),XRT_STR_LITERAL("application/json")};
    char Authority[64];
    if(Request.server){uint16 Port=Request.server->PortBound?Request.server->PortBound:Request.server->Port;
        snprintf(Authority,sizeof(Authority),"127.0.0.1:%u",(unsigned)Port);Fields[Count++]=(xhttpfield){XRT_STR_LITERAL("Host"),xrtStrView(Authority)};}
    Head.Fields=Fields;Head.FieldCount=Count;MdoApiRequestId(C.RequestId);
    for(size_t i=0;i<sizeof(g_MdoApiRoutes)/sizeof(*g_MdoApiRoutes);i++){
        const MdoApiRoute* R=&g_MdoApiRoutes[i];if(!MdoApiRouteMatch(&C,R->Path))continue;
        /* These handlers explicitly retain a physical HTTP connection. The SDK
         * transport chooses loopback before invoking them, never replays work. */
        if(R->Proc==MdoApiEcosystemRoute||R->Proc==MdoApiModelDiscoverRoute||R->Proc==MdoApiModelTestRoute||
            R->Proc==MdoApiSessionBackupRoute||R->Proc==MdoApiLiveRoute||R->Proc==MdoApiDirectRoute||
            strstr(Target,"/attachments/"))return false;
        if(!(R->Methods&Head.MethodCode)){MdoApplicationError(Error,"method_not_allowed","Unsupported operation method","dispatch",405,false);return false;}
        bool Ok=MdoApiRouteInvoke(&C,R);
        if(!Ok&&!Response->Status)return MdoApplicationError(Error,"sdk_service_failed","Product handler did not provide a result","service",500,strcmp(Method,"GET")!=0);
        return true;
    }
    return MdoApplicationError(Error,"sdk_service_missing","Product resource does not exist","dispatch",404,false);
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
    if (!MdoApiLocalRequest(pRequest)) {
        Context.UntrustedOrigin = true;
        (void)MdoApiReplyError(&Context, 403u, "local_origin_required",
            "Use the local application address and its own origin", NULL);
        return XS_OK;
    }
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
