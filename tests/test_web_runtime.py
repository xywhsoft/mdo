"""Bounded MDO-6E Web tool probe through the real xs/TCC runtime."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import socket
import subprocess
import tempfile
import threading
from pathlib import Path


from runtime_sources import copy_app_source


ROOT = Path(__file__).resolve().parent.parent

PROBE_SOURCE = r'''
#include <stdio.h>
#include <string.h>
#include <xsbase.h>

#include "src/storage/home.c"
#include "src/config/config.c"
#include "src/security/secrets.c"
#include "src/web/manager.c"

typedef struct Probe {
    unsigned Fetches;
    unsigned PublicOnly;
    unsigned SawSecret;
    unsigned Permissions;
    unsigned PermissionResources;
} Probe;

static unsigned char *Copy(const char *text, size_t *size) {
    unsigned char *copy;
    *size = strlen(text);
    copy = (unsigned char*)xrtMalloc(*size + 1u);
    if (copy != NULL) memcpy(copy, text, *size + 1u);
    return copy;
}

static bool Fetch(void *data, const XS_FetchRequest *request,
    XS_FetchResponse *response) {
    static const char search[] =
        "{\"code\":0,\"message\":\"\",\"data\":{\"provider\":\"bocha\","
        "\"request_id\":\"0123456789abcdef0123456789abcdef\",\"count\":1,"
        "\"truncated\":false,\"results\":["
        "{\"title\":\"Result One\",\"url\":\"https://example.com/page\","
        "\"snippet\":\"needle snippet\",\"site\":\"example.com\",\"published_at\":\"2026-10-04\"}]}}";
    static const char page[] =
        "<!doctype html><html><head><title>Probe &amp; Page</title>"
        "<style>hidden style</style></head><body><h1>Alpha</h1>"
        "needle <b>Omega</b> &amp; more<script>hidden script</script>"
        "</body></html>";
    Probe *probe = (Probe*)data;
    size_t i;
    const char *body;
    const char *content_type;
    const char *final_url;
    ++probe->Fetches;
    if ((request->Flags & XS_FETCH_PUBLIC_ADDRESSES_ONLY) != 0u)
        ++probe->PublicOnly;
    for (i = 0u; i < request->HeaderCount; ++i)
        if (strcmp(request->Headers[i].Name, "Authorization") == 0 &&
            strcmp(request->Headers[i].Value, "Bearer probe-secret") == 0)
            ++probe->SawSecret;
    if (strcmp(request->Url, "https://ai.xywhsoft.com/api/v1/search") == 0) {
        if (strcmp(request->Method, "POST") != 0 || request->Body == NULL ||
            request->BodySize != strlen("{\"query\":\"alpha beta\",\"count\":2}") ||
            memcmp(request->Body, "{\"query\":\"alpha beta\",\"count\":2}", request->BodySize) != 0 ||
            (request->Flags & XS_FETCH_FOLLOW_REDIRECTS) != 0u || request->MaxRedirects != 0u)
            printf("request_contract_failed=1\n");
        body = search; content_type = "application/json";
        final_url = request->Url;
    } else if (strcmp(request->Url, "https://example.com/page") == 0) {
        body = page; content_type = "Text/HTML; charset=utf-8";
        final_url = "https://example.com/final";
    } else {
        xerror *error = xrtErrorCreate(XERR_IO, "probe.web", 1,
            "deterministic transport failure");
        if (error != NULL) xrtSetErrorTake(error);
        return false;
    }
    memset(response, 0, sizeof(*response));
    response->Size = sizeof(*response);
    response->Version = XS_FETCH_RESPONSE_VERSION;
    response->Status = 200u;
    response->FinalUrl = xrtStrDup(final_url);
    response->ContentType = xrtStrDup(content_type);
    response->Body = Copy(body, &response->BodySize);
    response->FetchedAt = 1700000000000000LL + probe->Fetches;
    return response->FinalUrl != NULL && response->ContentType != NULL &&
        response->Body != NULL;
}

static void ResponseUnit(void *data, XS_FetchResponse *response) {
    (void)data;
    xrtFree(response->FinalUrl);
    xrtFree(response->ContentType);
    xrtFree(response->Body);
    memset(response, 0, sizeof(*response));
}

static xwork_permission_decision Permission(void *data,
    const xwork_permission_request *request) {
    Probe *probe = (Probe*)data;
    ++probe->Permissions;
    probe->PermissionResources += (unsigned)request->iResourceCount;
    return XWORK_PERMISSION_ALLOW;
}

static bool Execute(xwork_agent *agent, const char *name,
    const char *arguments, char **output) {
    xllm_executor executor;
    xllm_executor_ctx context;
    xllm_executor_result result;
    xllm_tool_call call;
    xwork_error error;
    bool infrastructure;
    memset(&executor, 0, sizeof(executor));
    if (!xworkExecutorBind(&executor, agent, &error)) return false;
    memset(&context, 0, sizeof(context));
    context.uRound = 1u;
    context.uDeadline = xrtDeadlineAfter(5000000u);
    memset(&call, 0, sizeof(call));
    call.sId = (char*)"web-probe-call";
    call.sName = (char*)name;
    call.sArgumentsJson = (char*)arguments;
    memset(&result, 0, sizeof(result));
    infrastructure = executor.pExecute != NULL &&
        executor.pExecute(executor.pUserData, &call, &context, &result);
    *output = result.sContent != NULL ? xrtStrDup(result.sContent) : NULL;
    printf("execute_%s=infra:%d success:%d text:%s\n", name,
        infrastructure ? 1 : 0, result.bSuccess ? 1 : 0,
        result.sContent != NULL ? result.sContent : "null");
    xworkExecutorUnbind(&executor);
    return infrastructure && result.bSuccess;
}

void ServiceInit(XS_HostInfo *host) {
    xwork_runtime_config runtime_config;
    xwork_runtime *runtime = NULL;
    xwork_error error;
    MdoWebTransport transport;
    MdoWebSnapshot snapshot;
    xwork_tool_catalog *catalog = NULL;
    xwork_tool_info info;
    xwork_agent_definition_config definition_config;
    xwork_agent_definition *definition = NULL;
    xllm_session_config session_config;
    xllm_session *session = NULL;
    xwork_agent_options options;
    xwork_agent *agent = NULL;
    Probe probe;
    char *search = NULL;
    char *open = NULL;
    char *find = NULL;
    char *find_after = NULL;
    char *failed = NULL;
    char *invalid = NULL;
    size_t i;
    (void)host;
    memset(&probe, 0, sizeof(probe));
    if (!MdoHomeInit() || !MdoConfigInit()) {
        printf("init_error=config\n"); goto done;
    }
    xworkRuntimeConfigInit(&runtime_config);
    runtime = xworkRuntimeCreate(&runtime_config, &error);
    memset(&transport, 0, sizeof(transport));
    transport.Size = sizeof(transport);
    transport.Version = MDO_WEB_TRANSPORT_VERSION;
    transport.Context = &probe;
    transport.Fetch = Fetch;
    transport.ResponseUnit = ResponseUnit;
    if (runtime == NULL || !MdoWebManagerInitWithTransport(runtime, &transport)) {
        printf("init_error=web\n"); goto done;
    }
    catalog = xworkRuntimeToolCatalogSnapshot(runtime);
    printf("catalog_count=%zu\n", xworkToolCatalogCount(catalog));
    for (i = 0u; i < xworkToolCatalogCount(catalog); ++i) {
        memset(&info, 0, sizeof(info));
        if (xworkToolCatalogToolAt(catalog, i, &info) &&
            strncmp(info.sName, "web_", 4u) == 0)
            printf("tool=%s effects:%llu source:%s permissions:%d\n",
                info.sName, (unsigned long long)info.uEffects, info.sSource,
                info.bDescribesPermissions ? 1 : 0);
    }
    xworkAgentDefinitionConfigInit(&definition_config);
    definition_config.sId = "web.probe.agent";
    definition_config.bRegisterBuiltinTools = false;
    definition_config.bAutoSaveSession = false;
    definition_config.bRequireVerificationAfterWrite = false;
    definition = xworkAgentDefinitionCreate(&definition_config, &error);
    xllmSessionConfigInit(&session_config);
    session = xllmSessionCreate(&session_config, NULL);
    xworkAgentOptionsInit(&options);
    options.pSession = session;
    options.sWorkspaceRoot = ".";
    options.OnPermission = Permission;
    options.pPermissionUserData = &probe;
    agent = xworkAgentCreateWithRuntime(runtime, definition, &options, &error);
    if (definition == NULL || session == NULL || agent == NULL) {
        printf("init_error=agent\n"); goto done;
    }
    if (!Execute(agent, "web_search", "{\"query\":\"alpha beta\",\"count\":2}",
            &search)) goto done;
    if (!Execute(agent, "web_open", "{\"url\":\"https://example.com/page\",\"max_characters\":4096}",
            &open)) goto done;
    if (!Execute(agent, "web_find", "{\"document_id\":\"doc-0000000000000001\",\"query\":\"needle\",\"max_results\":3,\"context_characters\":32}",
            &find)) goto done;
    (void)Execute(agent, "web_open", "{\"url\":\"https://example.com/fail\"}",
        &failed);
    (void)Execute(agent, "web_open", "{\"url\":\"http://127.0.0.1/private\"}",
        &invalid);
    printf("reload=%d\n", MdoWebManagerReload() ? 1 : 0);
    if (!Execute(agent, "web_find", "{\"document_id\":\"doc-0000000000000001\",\"query\":\"Omega\"}",
            &find_after)) goto done;
    memset(&snapshot, 0, sizeof(snapshot)); snapshot.Size = sizeof(snapshot);
    if (MdoWebManagerGetSnapshot(&snapshot))
        printf("snapshot=generation:%llu enabled:%d docs:%zu/%zu completed:%llu failed:%llu\n",
            (unsigned long long)snapshot.Generation,
            snapshot.Enabled ? 1 : 0, snapshot.DocumentCount,
            snapshot.MaxDocuments,
            (unsigned long long)snapshot.RequestsCompleted,
            (unsigned long long)snapshot.RequestsFailed);
    printf("probe=fetches:%u public:%u secret:%u permissions:%u resources:%u\n",
        probe.Fetches, probe.PublicOnly, probe.SawSecret,
        probe.Permissions, probe.PermissionResources);
    printf("probe_done=1\n");
done:
    xrtFree(search); xrtFree(open); xrtFree(find); xrtFree(find_after);
    xrtFree(failed);
    xrtFree(invalid);
    xworkAgentDestroy(agent);
    xllmSessionDestroy(session);
    xworkAgentDefinitionRelease(definition);
    xworkToolCatalogRelease(catalog);
    MdoWebManagerUnit();
    xworkRuntimeRelease(runtime);
    MdoConfigUnit();
    MdoHomeUnit();
}

void ServiceUnit(XS_HostInfo *host) { (void)host; }
'''


def write_site(site: Path) -> None:
    (site / "web").mkdir(parents=True)
    (site / "default-home" / "config").mkdir(parents=True)
    for directory in ("storage", "config", "security", "web"):
        (site / "src" / directory).mkdir(parents=True)
    (site / "include" / "mdo").mkdir(parents=True)
    (site / "web" / "index.html").write_text("probe", encoding="utf-8")
    shutil.copy2(ROOT / "app/default-home/config/defaults.json",
                 site / "default-home/config/defaults.json")
    for relative in (
        "src/storage/home.c", "src/storage/home_import.inc.c", "src/storage/home_purge.inc.c", "src/storage/home_restore.inc.c", "src/config/config.c", "src/security/secrets.c",
        "src/web/manager.c", "include/mdo/home.h", "include/mdo/home_import.h", "include/mdo/home_purge.h", "include/mdo/home_restore.h", "include/mdo/session_file_policy.h", "include/mdo/config.h",
        "include/mdo/secrets.h", "include/mdo/web.h",
    ):
        copy_app_source(relative, site)
    (site / "probe.c").write_text(PROBE_SOURCE, encoding="utf-8")
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        port = listener.getsockname()[1]
    (site / "xs.json").write_text(json.dumps({
        "services": [{
            "enabled": True,
            "class": "http",
            "name": "mdo-web-probe",
            "ip": "127.0.0.1",
            "port": port,
            "host_default": {
                "enabled": True,
                "name": "probe",
                "path": "web",
                "devlang": "c",
                "devfile": "probe.c",
            },
        }],
    }), encoding="utf-8")


def run_probe(host: Path, site: Path, home: Path, token: str | None = "probe-secret") -> str:
    environment = os.environ.copy()
    environment.pop("MDO_SEARCH_ACCESS_TOKEN", None)
    if token is not None:
        environment["MDO_SEARCH_ACCESS_TOKEN"] = token
    command = [str(host), "xs.json", "--", "--home", str(home)]
    creationflags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
    process = subprocess.Popen(
        command, cwd=site, env=environment,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, encoding="utf-8", errors="replace",
        creationflags=creationflags,
    )
    lines: list[str] = []
    done = threading.Event()
    assert process.stdout is not None

    def read_output() -> None:
        assert process.stdout is not None
        for line in process.stdout:
            lines.append(line)
            if "probe_done=1" in line:
                done.set()

    reader = threading.Thread(target=read_output, daemon=True)
    reader.start()
    done.wait(timeout=15.0)
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=3.0)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=3.0)
    reader.join(timeout=3.0)
    return "".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    default_host = ROOT / ".build" / "host" / ("xs.exe" if os.name == "nt" else "xs")
    parser.add_argument("--host", type=Path, default=default_host)
    args = parser.parse_args()
    host = args.host.resolve()
    if not host.is_file():
        print(f"missing xs host: {host}")
        return 2
    (ROOT / ".build").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="web-runtime-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        site = base / "site"
        write_site(site)
        output = run_probe(host, site, base / "state")
        assert "init_error=" not in output, output
        assert "catalog_count=3" in output, output
        assert "tool=web_search effects:57 source:mdo.web permissions:1" in output, output
        assert "tool=web_open effects:9 source:mdo.web permissions:1" in output, output
        assert "tool=web_find effects:1 source:mdo.web permissions:0" in output, output
        assert '"type":"web_search_results"' in output, output
        assert '"title":"Result One"' in output and "Rejected" not in output, output
        assert '"published_at":"2026-10-04"' in output, output
        assert "request_contract_failed" not in output, output
        assert '"document_id":"doc-0000000000000001"' in output, output
        assert '"title":"Probe & Page"' in output, output
        assert "hidden script" not in output and "hidden style" not in output, output
        assert '"type":"web_find_results"' in output, output
        assert '"matches":[{"offset":' in output, output
        assert "execute_web_open=infra:1 success:0" in output, output
        assert "deterministic transport failure" in output, output
        assert "reload=1" in output, output
        assert "snapshot=generation:2 enabled:1 docs:1/16 completed:2 failed:1" in output, output
        assert "probe=fetches:3 public:2 secret:1 permissions:5 resources:5" in output, output
        assert "probe_done=1" in output, output
    print("PASS bounded Web search/open/find runtime")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
