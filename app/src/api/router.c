#include <stdio.h>
#include <string.h>

#include "internal.h"

typedef struct MdoApiRoute {
    cstr Path;
    xhttpmethod Methods;
    cstr Allow;
    MdoApiRouteProc Proc;
} MdoApiRoute;

static xatomic64 g_MdoApiFallbackId;
static bool g_MdoApiInitialized;

static const MdoApiRoute g_MdoApiRoutes[] = {
    { "/api/v1/bootstrap", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiBootstrapRoute },
    { "/api/v1/settings", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiSettingsRoute },
    { "/api/v1/models", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiModelsRoute },
    { "/api/v1/agents", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiAgentsRoute },
    { "/api/v1/modules", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiModulesRoute },
    { "/api/v1/skills", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiSkillsRoute },
    { "/api/v1/mcp", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiMcpRoute },
    { "/api/v1/projects", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiProjectsRoute },
    { "/api/v1/sessions",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD | XHTTP_METHOD_POST,
      "GET, HEAD, POST, OPTIONS", MdoApiSessionsRoute },
    { "/api/v1/draft",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD | XHTTP_METHOD_PUT,
      "GET, HEAD, PUT, OPTIONS", MdoApiDraftRoute },
    { "/api/v1/runs", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiRunsRoute },
    { "/api/v1/schedules",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD | XHTTP_METHOD_POST,
      "GET, HEAD, POST, OPTIONS", MdoApiSchedulesRoute },
    { "/api/v1/tasks", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiTasksRoute },
    { "/api/v1/approvals", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiApprovalsRoute },
    { "/api/v1/projects/{project}/sessions/{session}/asks",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiAsksRoute },
    { "/api/v1/artifacts", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiArtifactsRoute },
    { "/api/v1/permissions", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiPermissionsRoute },
    { "/api/v1/diagnostics", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiDiagnosticsRoute },
    { "/api/v1/storage", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiStorageRoute },
    { "/api/v1/migrations/legacy",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD | XHTTP_METHOD_POST,
      "GET, HEAD, POST, OPTIONS", MdoApiLegacyMigrationsRoute },
    { "/api/v1/events", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiEventsRoute },
    { "/api/v1/operations", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiOperationsRoute },
    { "/api/v1/settings/{domain}/preview",
      XHTTP_METHOD_POST | XHTTP_METHOD_PATCH,
      "POST, PATCH, OPTIONS", MdoApiSettingsPreviewRoute },
    { "/api/v1/settings/{domain}",
      XHTTP_METHOD_PUT | XHTTP_METHOD_PATCH | XHTTP_METHOD_DELETE,
      "PUT, PATCH, DELETE, OPTIONS", MdoApiSettingsMutationRoute },
    { "/api/v1/models/reload", XHTTP_METHOD_POST,
      "POST, OPTIONS", MdoApiModelsReloadRoute },
    { "/api/v1/skills/reload", XHTTP_METHOD_POST,
      "POST, OPTIONS", MdoApiSkillsReloadRoute },
    { "/api/v1/modules/reload", XHTTP_METHOD_POST,
      "POST, OPTIONS", MdoApiModulesReloadRoute },
    { "/api/v1/mcp/reload", XHTTP_METHOD_POST,
      "POST, OPTIONS", MdoApiMcpReloadRoute },
    { "/api/v1/mcp/{server}/enabled", XHTTP_METHOD_PUT,
      "PUT, OPTIONS", MdoApiMcpEnabledRoute },
    { "/api/v1/mcp/{server}/disconnect", XHTTP_METHOD_POST,
      "POST, OPTIONS", MdoApiMcpDisconnectRoute },
    { "/api/v1/mcp/{server}/refresh", XHTTP_METHOD_POST,
      "POST, OPTIONS", MdoApiMcpRefreshRoute },
    { "/api/v1/operations/{operation}",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD | XHTTP_METHOD_DELETE,
      "GET, HEAD, DELETE, OPTIONS", MdoApiOperationRoute },
    { "/api/v1/projects/{project}/sessions/{session}",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD | XHTTP_METHOD_PATCH |
        XHTTP_METHOD_DELETE,
      "GET, HEAD, PATCH, DELETE, OPTIONS",
      MdoApiSessionRoute },
    { "/api/v1/projects/{project}/sessions/{session}/restore",
      XHTTP_METHOD_POST, "POST, OPTIONS", MdoApiSessionRestoreRoute },
    { "/api/v1/projects/{project}/sessions/{session}/profile",
      XHTTP_METHOD_PUT, "PUT, OPTIONS", MdoApiSessionProfileRoute },
    { "/api/v1/projects/{project}/sessions/{session}/history",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD, "GET, HEAD, OPTIONS",
      MdoApiSessionHistoryRoute },
    { "/api/v1/projects/{project}/sessions/{session}/recovery",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD, "GET, HEAD, OPTIONS",
      MdoApiSessionRecoveryRoute },
    { "/api/v1/projects/{project}/sessions/{session}/resume",
      XHTTP_METHOD_POST, "POST, OPTIONS", MdoApiSessionResumeRoute },
    { "/api/v1/projects/{project}/sessions/{session}/fork",
      XHTTP_METHOD_POST, "POST, OPTIONS", MdoApiSessionForkRoute },
    { "/api/v1/projects/{project}/sessions/{session}/truncate",
      XHTTP_METHOD_POST, "POST, OPTIONS", MdoApiSessionTruncateRoute },
    { "/api/v1/projects/{project}/sessions/{session}/clear",
      XHTTP_METHOD_POST, "POST, OPTIONS", MdoApiSessionClearRoute },
    { "/api/v1/projects/{project}/sessions/{session}/export",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD, "GET, HEAD, OPTIONS",
      MdoApiSessionExportRoute },
    { "/api/v1/projects/{project}/sessions/{session}/runs",
      XHTTP_METHOD_POST, "POST, OPTIONS", MdoApiRunStartRoute },
    { "/api/v1/runs/{run}",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD | XHTTP_METHOD_DELETE,
      "GET, HEAD, DELETE, OPTIONS", MdoApiRunRoute },
    { "/api/v1/schedules/{schedule}",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD | XHTTP_METHOD_PUT |
        XHTTP_METHOD_DELETE,
      "GET, HEAD, PUT, DELETE, OPTIONS", MdoApiScheduleRoute },
    { "/api/v1/schedules/{schedule}/enabled", XHTTP_METHOD_PUT,
      "PUT, OPTIONS", MdoApiScheduleEnabledRoute },
    { "/api/v1/tasks/{task}/output", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiTaskOutputRoute },
    { "/api/v1/approvals/{approval}", XHTTP_METHOD_PUT,
      "PUT, OPTIONS", MdoApiApprovalRoute },
    { "/api/v1/projects/{project}/sessions/{session}/asks/{ask}",
      XHTTP_METHOD_PUT, "PUT, OPTIONS", MdoApiAskRoute },
    { "/api/v1/tasks/{task}/events", XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiTaskEventsRoute },
    { "/api/v1/artifacts/{artifact}",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiArtifactRoute },
    { "/api/v1/tasks/{task}",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD | XHTTP_METHOD_DELETE,
      "GET, HEAD, DELETE, OPTIONS", MdoApiTaskRoute },
    { "/api/v1/projects/{project}/sessions/{session}/events",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD, "GET, HEAD, OPTIONS",
      MdoApiSessionEventsRoute },
    { "/api/v1/projects/{project}/sessions/{session}/feedback",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD | XHTTP_METHOD_PUT,
      "GET, HEAD, PUT, OPTIONS", MdoApiFeedbackRoute },
    { "/api/v1/projects/{project}/sessions/{session}/draft",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD | XHTTP_METHOD_PUT,
      "GET, HEAD, PUT, OPTIONS", MdoApiDraftRoute },
    { "/api/v1/projects/{project}/sessions/{session}/queue",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD | XHTTP_METHOD_POST,
      "GET, HEAD, POST, OPTIONS", MdoApiQueueRoute },
    { "/api/v1/projects/{project}/sessions/{session}/queue/{item}",
      XHTTP_METHOD_PUT | XHTTP_METHOD_DELETE,
      "PUT, DELETE, OPTIONS", MdoApiQueueItemRoute },
    { "/api/v1/projects/{project}/sessions/{session}/todo",
      XHTTP_METHOD_GET | XHTTP_METHOD_HEAD,
      "GET, HEAD, OPTIONS", MdoApiTodoRoute },
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
    if ( !MdoApiFeedbackInit() ) return false;
    if ( !MdoApiDraftInit() ) {
        MdoApiFeedbackUnit();
        return false;
    }
    if ( !MdoApiQueueInit() ) {
        MdoApiDraftUnit();
        MdoApiFeedbackUnit();
        return false;
    }
    xrtAtomic64Init(&g_MdoApiFallbackId, 0u);
    g_MdoApiInitialized = true;
    return true;
}

void MdoApiUnit(void)
{
    g_MdoApiInitialized = false;
    MdoApiFeedbackUnit();
    MdoApiDraftUnit();
    MdoApiQueueUnit();
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
            (void)Route->Proc(&Context);
        } else {
            (void)MdoApiReplyError(&Context, 405u, "method_not_allowed",
                "The request method is not allowed for this resource",
                Route->Allow);
        }
        return XS_OK;
    }
    (void)MdoApiReplyError(&Context, 404u, "route_not_found",
        "The requested API resource does not exist", NULL);
    return XS_OK;
}
