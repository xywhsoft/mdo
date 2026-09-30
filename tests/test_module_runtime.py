"""Bounded MDO-3 module reload probe through the real xs/TCC runtime."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent

EXTERNAL_MODULE = r'''
#include "mdo/module.h"
static const mdo_host_services_v1 *g_Host;
static mdo_result Execute(void *data, const mdo_tool_context_v1 *context,
    const char *arguments, mdo_result_writer_v1 *writer,
    char *error, size_t error_capacity) {
    (void)data; (void)context; (void)arguments; (void)error;
    (void)error_capacity;
    if (!writer->WriteText(writer->Context, "external-ok") ||
        !writer->SetSuccess(writer->Context, true)) return MDO_RESULT_LIMIT;
    return MDO_RESULT_OK;
}
static const mdo_tool_v1 Tool = {
    MDO_V1_HEADER(mdo_tool_v1), "probe.external", "External", "External probe",
    "{\"type\":\"object\",\"additionalProperties\":false}",
    MDO_TOOL_EFFECT_READ, MDO_TOOL_STRICT | MDO_TOOL_PARALLEL_SAFE,
    "probe.group", 0, 1024, 0, 0, Execute, 0
};
static mdo_result Register(const mdo_host_services_v1 *host,
    const mdo_registrar_v1 *registrar, void **data,
    char *error, size_t error_capacity) {
    (void)data; g_Host = host;
    return registrar->AddTool(registrar->Context, &Tool, error, error_capacity);
}
static void Unregister(void *data) {
    (void)data;
    if (g_Host && g_Host->Core && g_Host->Core->Log)
        g_Host->Core->Log(g_Host->Core->Context, MDO_LOG_INFO,
            "external-unregistered");
}
static const mdo_module_v1 Module = {
    MDO_V1_HEADER(mdo_module_v1), "probe.external.module", "External module",
    "External module reload fixture", "1.0.0", MDO_CAPABILITY_LOG,
    0, 0, Register, Unregister
};
MDO_EXPORT const mdo_module_v1 *mdoModuleEntry(void) { return &Module; }
'''.strip()

INVALID_MODULE = '#include "mdo/module.h"\nthis is not valid C;\n'
BAD_ABI_MODULE = EXTERNAL_MODULE.replace(
    "MDO_V1_HEADER(mdo_module_v1),",
    "(uint32_t)sizeof(mdo_module_v1), 99u,",
    1,
)
MISSING_DEPENDENCY_MODULE = EXTERNAL_MODULE.replace(
    "static const mdo_module_v1 Module = {",
    'static const char *Dependencies[] = {"probe.missing.module"};\n'
    "static const mdo_module_v1 Module = {",
    1,
).replace(
    "    0, 0, Register, Unregister",
    "    Dependencies, 1u, Register, Unregister",
    1,
)


def c_literal(value: str) -> str:
    return json.dumps(value)


PROBE_SOURCE = rf'''
#include <stdio.h>
#include <string.h>
#include <xsbase.h>

#include "src/storage/home.c"
#include "src/modules/manager.c"

static const char sExternal[] = {c_literal(EXTERNAL_MODULE)};
static const char sBadAbi[] = {c_literal(BAD_ABI_MODULE)};
static const char sMissingDependency[] = {c_literal(MISSING_DEPENDENCY_MODULE)};
static const char sInvalid[] = {c_literal(INVALID_MODULE)};

static void PrintError(const char *label) {{
    const xerror *error = xrtGetError();
    printf("%s=%s\n", label,
        error != NULL ? xrtErrorMessage(error) : "missing-error");
    xrtClearError();
}}

static bool ExecuteOne(xwork_runtime *runtime, const char *name,
    const char *arguments, xwork_agent **agent_out,
    xllm_session **session_out, xwork_agent_definition **definition_out) {{
    xwork_agent_definition_config definition_config;
    xwork_agent_options options;
    xllm_session_config session_config;
    xwork_error error;
    xllm_executor executor;
    xllm_executor_ctx context;
    xllm_executor_result result;
    xllm_tool_call call;
    xwork_agent_definition *definition = *definition_out;
    xllm_session *session = *session_out;
    xwork_agent *agent = *agent_out;
    bool ok;
    if (agent == NULL) {{
        xworkAgentDefinitionConfigInit(&definition_config);
        definition_config.sId = "module.probe.agent";
        definition_config.bRegisterBuiltinTools = false;
        definition_config.bAutoSaveSession = false;
        definition_config.bRequireVerificationAfterWrite = false;
        definition = xworkAgentDefinitionCreate(&definition_config, &error);
        xllmSessionConfigInit(&session_config);
        session = xllmSessionCreate(&session_config, NULL);
        xworkAgentOptionsInit(&options);
        options.pSession = session;
        options.sWorkspaceRoot = ".";
        agent = xworkAgentCreateWithRuntime(runtime, definition, &options, &error);
        if (definition == NULL || session == NULL || agent == NULL) return false;
        *definition_out = definition; *session_out = session; *agent_out = agent;
    }}
    memset(&executor, 0, sizeof(executor));
    if (!xworkExecutorBind(&executor, agent, &error)) return false;
    memset(&context, 0, sizeof(context));
    context.uRound = 1u;
    context.uDeadline = XRT_DEADLINE_NEVER;
    memset(&call, 0, sizeof(call));
    call.sId = (char*)"module-probe-call";
    call.sName = (char*)name;
    call.sArgumentsJson = (char*)arguments;
    memset(&result, 0, sizeof(result));
    ok = executor.pExecute != NULL &&
        executor.pExecute(executor.pUserData, &call, &context, &result);
    printf("execute_%s=infra:%d success:%d text:%s\n", name, ok ? 1 : 0,
        result.bSuccess ? 1 : 0,
        result.sContent != NULL ? result.sContent : "null");
    xworkExecutorUnbind(&executor);
    return ok && result.bSuccess;
}}

void ServiceInit(XS_HostInfo *host) {{
    xwork_runtime_config runtime_config;
    xwork_error work_error;
    xwork_runtime *runtime = NULL;
    MdoModuleCatalog *old_catalog = NULL;
    MdoModuleCatalog *catalog = NULL;
    MdoModuleDiagnostics *diagnostics = NULL;
    MdoModuleInfo module_info;
    MdoModuleToolInfo tool_info;
    MdoModuleAgentInfo agent_info;
    MdoModuleDiagnosticInfo diagnostic_info;
    xwork_tool_catalog *work_catalog = NULL;
    xwork_tool_info work_info;
    xwork_agent_definition *definition = NULL;
    xllm_session *session = NULL;
    xwork_agent *agent = NULL;
    uint64 generation;
    size_t i;
    (void)host;
    if (!MdoHomeInit()) {{ PrintError("home_error"); goto done; }}
    xworkRuntimeConfigInit(&runtime_config);
    runtime = xworkRuntimeCreate(&runtime_config, &work_error);
    if (runtime == NULL || !MdoModuleManagerInit(runtime)) {{
        PrintError("module_init_error"); goto done;
    }}
    catalog = MdoModuleCatalogSnapshot();
    printf("initial_generation=%llu modules=%zu tools=%zu agents=%zu\n",
        (unsigned long long)MdoModuleManagerGeneration(),
        MdoModuleCatalogModuleCount(catalog), MdoModuleCatalogToolCount(catalog),
        MdoModuleCatalogAgentCount(catalog));
    for (i = 0u; i < MdoModuleCatalogModuleCount(catalog); ++i) {{
        memset(&module_info, 0, sizeof(module_info)); module_info.Size = sizeof(module_info);
        if (MdoModuleCatalogModuleAt(catalog, i, &module_info) &&
            strcmp(module_info.Id, "mdo.core.echo") == 0)
            printf("initial_module=%s external=%d hash=%s\n",
                module_info.Id, module_info.External ? 1 : 0,
                module_info.SourceHash);
    }}
    memset(&tool_info, 0, sizeof(tool_info)); tool_info.Size = sizeof(tool_info);
    memset(&agent_info, 0, sizeof(agent_info)); agent_info.Size = sizeof(agent_info);
    if (MdoModuleCatalogToolAt(catalog, 0u, &tool_info) &&
        MdoModuleCatalogAgentFind(catalog, "mdo.default", &agent_info))
        printf("initial_tool=%s agent=%s permission=%s\n", tool_info.Id,
            agent_info.Id, agent_info.PermissionProfile);
    if (!ExecuteOne(runtime, "mdo.echo", "{{\"text\":\"hello\"}}",
            &agent, &session, &definition)) goto done;
    if (!ExecuteOne(runtime, "mdo.todo",
            "{{\"items\":[{{\"text\":\"inspect\",\"done\":false}}]}}",
            &agent, &session, &definition)) goto done;
    old_catalog = catalog; catalog = NULL;
    if (!MdoHomeAtomicWrite("modules/tools/external.c", sExternal,
            strlen(sExternal), false) || !MdoModuleManagerReload()) {{
        PrintError("valid_reload_error"); goto done;
    }}
    catalog = MdoModuleCatalogSnapshot();
    generation = MdoModuleManagerGeneration();
    printf("reloaded_generation=%llu modules=%zu tools=%zu old_modules=%zu\n",
        (unsigned long long)generation, MdoModuleCatalogModuleCount(catalog),
        MdoModuleCatalogToolCount(catalog), MdoModuleCatalogModuleCount(old_catalog));
    for (i = 0u; i < MdoModuleCatalogModuleCount(catalog); ++i) {{
        memset(&module_info, 0, sizeof(module_info)); module_info.Size = sizeof(module_info);
        if (MdoModuleCatalogModuleAt(catalog, i, &module_info) &&
            strcmp(module_info.Id, "probe.external.module") == 0)
            printf("external_module=1 external=%d hash=%s\n",
                module_info.External ? 1 : 0, module_info.SourceHash);
    }}
    work_catalog = xworkRuntimeToolCatalogSnapshot(runtime);
    for (i = 0u; i < xworkToolCatalogCount(work_catalog); ++i) {{
        memset(&work_info, 0, sizeof(work_info));
        if (xworkToolCatalogToolAt(work_catalog, i, &work_info) &&
            strcmp(work_info.sName, "probe.external") == 0)
            printf("schedule_parallel=%d group=%s source=%s\n",
                work_info.bParallelSafe ? 1 : 0,
                work_info.sSerialGroup ? work_info.sSerialGroup : "null",
                work_info.sSource ? work_info.sSource : "null");
    }}
    xworkToolCatalogRelease(work_catalog); work_catalog = NULL;
    if (!xworkAgentAdoptRuntimeToolCatalog(agent, &work_error) ||
        !ExecuteOne(runtime, "probe.external", "{{}}", &agent, &session,
            &definition)) goto done;
    if (!MdoHomeAtomicWrite("modules/tools/external.c", sBadAbi,
            strlen(sBadAbi), false)) goto done;
    printf("bad_abi_reload=%d\n", MdoModuleManagerReload() ? 1 : 0);
    if (xrtGetError() != NULL) PrintError("bad_abi_reload_error");
    printf("bad_abi_preserved_generation=%llu\n",
        (unsigned long long)MdoModuleManagerGeneration());
    diagnostics = MdoModuleDiagnosticsSnapshot();
    memset(&diagnostic_info, 0, sizeof(diagnostic_info));
    diagnostic_info.Size = sizeof(diagnostic_info);
    if (MdoModuleDiagnosticsAt(diagnostics, 0u, &diagnostic_info))
        printf("bad_abi_diagnostic_stage=%d\n", (int)diagnostic_info.Stage);
    MdoModuleDiagnosticsRelease(diagnostics); diagnostics = NULL;
    if (!MdoHomeAtomicWrite("modules/tools/external.c", sMissingDependency,
            strlen(sMissingDependency), false)) goto done;
    printf("missing_dependency_reload=%d\n", MdoModuleManagerReload() ? 1 : 0);
    if (xrtGetError() != NULL) PrintError("missing_dependency_reload_error");
    printf("missing_dependency_preserved_generation=%llu\n",
        (unsigned long long)MdoModuleManagerGeneration());
    diagnostics = MdoModuleDiagnosticsSnapshot();
    memset(&diagnostic_info, 0, sizeof(diagnostic_info));
    diagnostic_info.Size = sizeof(diagnostic_info);
    if (MdoModuleDiagnosticsAt(diagnostics, 0u, &diagnostic_info))
        printf("missing_dependency_diagnostic_stage=%d\n",
            (int)diagnostic_info.Stage);
    MdoModuleDiagnosticsRelease(diagnostics); diagnostics = NULL;
    if (!MdoHomeAtomicWrite("modules/tools/external.c", sInvalid,
            strlen(sInvalid), false)) goto done;
    printf("invalid_reload=%d\n", MdoModuleManagerReload() ? 1 : 0);
    if (xrtGetError() != NULL) PrintError("invalid_reload_error");
    printf("preserved_generation=%llu\n",
        (unsigned long long)MdoModuleManagerGeneration());
    diagnostics = MdoModuleDiagnosticsSnapshot();
    printf("diagnostics=%zu\n", MdoModuleDiagnosticsCount(diagnostics));
    memset(&diagnostic_info, 0, sizeof(diagnostic_info));
    diagnostic_info.Size = sizeof(diagnostic_info);
    if (MdoModuleDiagnosticsAt(diagnostics, 0u, &diagnostic_info))
        printf("diagnostic_stage=%d path=%s message=%s\n",
            (int)diagnostic_info.Stage,
            diagnostic_info.SourcePath ? diagnostic_info.SourcePath : "null",
            diagnostic_info.Message ? diagnostic_info.Message : "null");
done:
    xworkToolCatalogRelease(work_catalog);
    MdoModuleDiagnosticsRelease(diagnostics);
    MdoModuleCatalogRelease(catalog);
    MdoModuleCatalogRelease(old_catalog);
    xworkAgentDestroy(agent);
    xllmSessionDestroy(session);
    xworkAgentDefinitionRelease(definition);
    MdoModuleManagerUnit();
    xworkRuntimeRelease(runtime);
    MdoHomeUnit();
    printf("probe_done=1\n");
}}

void ServiceUnit(XS_HostInfo *host) {{ (void)host; }}
'''


def write_site(site: Path) -> None:
    for relative in (
        "web", "default-home/config", "default-home/modules/tools",
        "default-home/modules/agents", "default-home/modules/subagents",
        "generated/module-sdk/mdo", "src/storage", "src/modules",
        "include/mdo",
    ):
        (site / relative).mkdir(parents=True, exist_ok=True)
    (site / "web/index.html").write_text("probe", encoding="utf-8")
    shutil.copy2(ROOT / "app/default-home/config/defaults.json",
                 site / "default-home/config/defaults.json")
    shutil.copy2(ROOT / "app/default-home/modules/tools/builtin_echo.c",
                 site / "default-home/modules/tools/builtin_echo.c")
    shutil.copy2(ROOT / "app/default-home/modules/tools/builtin_todo.c",
                 site / "default-home/modules/tools/builtin_todo.c")
    shutil.copy2(ROOT / "app/default-home/modules/agents/builtin_default.c",
                 site / "default-home/modules/agents/builtin_default.c")
    for relative in (
        "src/storage/home.c", "src/storage/home_import.inc.c", "src/modules/manager.c",
        "include/mdo/home.h", "include/mdo/home_import.h", "include/mdo/modules.h",
    ):
        shutil.copy2(ROOT / "app" / relative, site / relative)
    shutil.copy2(ROOT / "include/mdo/module.h",
                 site / "generated/module-sdk/mdo/module.h")
    (site / "probe.c").write_text(PROBE_SOURCE, encoding="utf-8")
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        port = listener.getsockname()[1]
    (site / "xs.json").write_text(json.dumps({
        "services": [{
            "enabled": True, "class": "http", "name": "module-probe",
            "ip": "127.0.0.1", "port": port,
            "host_default": {
                "enabled": True, "name": "probe", "path": "web",
                "devlang": "c", "devfile": "probe.c",
            },
        }],
    }), encoding="utf-8")


def run_probe(host: Path, site: Path, home: Path) -> str:
    command = [str(host), "xs.json", "--", "--home", str(home)]
    creationflags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
    process = subprocess.Popen(
        command, cwd=site, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
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
    default = ROOT / ".build/host" / ("xs.exe" if os.name == "nt" else "xs")
    parser.add_argument("--host", type=Path, default=default)
    args = parser.parse_args()
    host = args.host.resolve()
    if not host.is_file():
        print(f"missing xs host: {host}", file=sys.stderr)
        return 2
    (ROOT / ".build").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="module-runtime-",
                                     dir=ROOT / ".build") as raw:
        base = Path(raw)
        site = base / "site"
        write_site(site)
        output = run_probe(host, site, base / "home")
        assert "module_init_error=" not in output, output
        assert "initial_generation=1 modules=3 tools=2 agents=1" in output, output
        assert "initial_module=mdo.core.echo external=0" in output, output
        assert "initial_tool=mdo.echo agent=mdo.default permission=balanced" in output, output
        assert "execute_mdo.echo=infra:1 success:1" in output, output
        assert "execute_mdo.todo=infra:1 success:1" in output, output
        assert "reloaded_generation=2 modules=4 tools=3 old_modules=3" in output, output
        assert "external_module=1 external=1" in output, output
        assert "schedule_parallel=1 group=probe.group source=mdo.modules" in output, output
        assert "execute_probe.external=infra:1 success:1" in output, output
        assert "external-ok" in output, output
        assert "bad_abi_reload=0" in output, output
        assert "bad_abi_preserved_generation=2" in output, output
        assert "bad_abi_diagnostic_stage=6" in output, output
        assert "missing_dependency_reload=0" in output, output
        assert "missing_dependency_preserved_generation=2" in output, output
        assert "missing_dependency_diagnostic_stage=6" in output, output
        assert "invalid_reload=0" in output, output
        assert "preserved_generation=2" in output, output
        assert "diagnostics=1" in output, output
        assert "diagnostic_stage=3" in output, output
        assert "external-unregistered" in output, output
        assert "probe_done=1" in output, output
    print("module runtime probe: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
