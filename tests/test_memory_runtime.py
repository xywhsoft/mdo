"""Bounded global/project memory persistence and audit probe."""

from __future__ import annotations

import argparse
import hashlib
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

PROBE_SOURCE = r'''
#include <stdio.h>
#include <string.h>
#include <xsbase.h>

#include "src/storage/home.c"
#include "src/projects/lifecycle.c"
#include "src/config/config.c"
static bool MemoryProbePublish(const char *path);
#include "src/memory/manager.c"
#include "src/memory/transfer.c"

static bool probe_publication;
static bool probe_all_reserved;
static unsigned probe_publication_checks;

static bool MemoryLeaseFree(const char *project) {
    xwork_error error;
    MdoProjectLease *lease = MdoProjectLeaseAcquire(project,
        MDO_PROJECT_LEASE_EXCLUSIVE, &error);
    bool free = lease != NULL;
    MdoProjectLeaseRelease(lease);
    return free;
}

/* Runs immediately before each actual store publication in the copied source.
 * The last store fails deliberately, after the first two have been published. */
static bool MemoryProbePublish(const char *path) {
    static const char *projects[] = {"lease-first", "lease-last"};
    size_t i;
    if (!probe_publication) return true;
    ++probe_publication_checks;
    for (i = 0u; i < 2u; ++i) {
        xwork_error error;
        MdoProjectLease *lease = MdoProjectLeaseAcquire(projects[i],
            MDO_PROJECT_LEASE_EXCLUSIVE, &error);
        if (lease != NULL || error.eCode != XWORK_ERROR_CONTEXT)
            probe_all_reserved = false;
        MdoProjectLeaseRelease(lease);
    }
    xrtClearError();
    return strcmp(path, "memory/projects/lease-last.json") != 0;
}

static bool MemoryLeaseProbe(void) {
    static const char empty[] =
        "{\"schema_version\":1,\"revision\":1,\"updated_at_us\":0,\"entries\":[]}";
    MdoMemoryImportCandidate stores[3] = {
        {MDO_MEMORY_GLOBAL, NULL, "memory/global.json", NULL},
        {MDO_MEMORY_PROJECT, "lease-first", "memory/projects/lease-first.json", NULL},
        {MDO_MEMORY_PROJECT, "lease-last", "memory/projects/lease-last.json", NULL}
    };
    MdoMemoryWriteOptions write;
    MdoMemoryRemoveOptions remove;
    MdoProjectLease *held = NULL;
    MdoHomeSnapshot home = {0};
    xfileinfo stat;
    xwork_error error;
    uint64 generation = MdoMemoryManagerGeneration();
    bool exists, blocked, unchanged, rolled_back, ok = false;
    size_t i;
    for (i = 0u; i < 3u; ++i) {
        stores[i].Snapshot = MdoMemoryInternalParse(stores[i].Scope,
            stores[i].ProjectId, xrtStrView(empty));
        if (stores[i].Snapshot == NULL) goto done;
    }
    held = MdoProjectLeaseAcquire("LEASE-LAST.", MDO_PROJECT_LEASE_EXCLUSIVE, &error);
    if (held == NULL) goto done;
    MdoMemoryWriteOptionsInit(&write);
    write.Scope = MDO_MEMORY_PROJECT; write.ProjectId = "lease-last";
    write.Id = "blocked"; write.Title = "Blocked"; write.Content = "No commit";
    MdoMemoryRemoveOptionsInit(&remove);
    remove.Scope = MDO_MEMORY_PROJECT; remove.ProjectId = "lease-last";
    remove.Id = "blocked";
    blocked = !MdoMemoryUpsert(&write, &error) && error.eCode == XWORK_ERROR_CONTEXT &&
        !MdoMemoryRemove(&remove, &error) && error.eCode == XWORK_ERROR_CONTEXT &&
        !MdoMemoryUpsert(&write, NULL) && !MdoMemoryRemove(&remove, NULL);
    printf("memory_lease_mutations=%d\n", blocked ? 1 : 0);
    if (!blocked) goto done;
    blocked = !MdoMemoryInternalImportEmpty(stores, 3u, UINT64_MAX,
        "lease-probe", NULL, NULL, &error) && error.eCode == XWORK_ERROR_CONTEXT;
    home.Size = sizeof(home);
    unchanged = MdoMemoryManagerGeneration() == generation &&
        MdoHomeGetSnapshot(&home) && !xrtPathStat(home.Path, false, &stat);
    xrtClearError();
    printf("memory_lease_import_blocked=%d\n", blocked && unchanged ? 1 : 0);
    if (!blocked || !unchanged || !MemoryLeaseFree("lease-first")) goto done;
    printf("memory_lease_partial_cleanup=1\n");
    MdoProjectLeaseRelease(held); held = NULL;
    probe_publication = true; probe_all_reserved = true;
    blocked = !MdoMemoryInternalImportEmpty(stores, 3u, UINT64_MAX,
        "lease-probe", NULL, NULL, &error) && error.eCode == XWORK_ERROR_IO;
    probe_publication = false;
    rolled_back = blocked && probe_all_reserved && probe_publication_checks == 3u &&
        MdoMemoryManagerGeneration() == generation;
    for (i = 0u; i < 3u; ++i)
        if (!MdoHomeExternalStat(stores[i].Path, &exists, &stat) || exists)
            rolled_back = false;
    rolled_back = rolled_back && MemoryLeaseFree("lease-first") &&
        MemoryLeaseFree("lease-last");
    printf("memory_lease_import_rollback=%d\n", rolled_back ? 1 : 0);
    /* Restore this isolated fixture's empty audit before the existing roundtrip. */
    ok = rolled_back && MdoHomeRemove("memory/audit.jsonl", false);
done:
    probe_publication = false;
    MdoProjectLeaseRelease(held);
    for (i = 0u; i < 3u; ++i)
        MdoMemorySnapshotRelease((MdoMemorySnapshot*)stores[i].Snapshot);
    return ok;
}

static void PrintSnapshot(const char *label, MdoMemoryScope scope,
    const char *project) {
    xwork_error error;
    MdoMemorySnapshot *snapshot = MdoMemorySnapshotCreate(scope, project, &error);
    MdoMemoryEntryInfo info;
    printf("%s=ok:%d revision:%llu count:%zu generation:%llu code:%d\n",
        label, snapshot != NULL ? 1 : 0,
        (unsigned long long)MdoMemorySnapshotRevision(snapshot),
        MdoMemorySnapshotCount(snapshot),
        (unsigned long long)MdoMemorySnapshotGeneration(snapshot),
        (int)error.eCode);
    memset(&info, 0, sizeof(info)); info.Size = sizeof(info);
    if (MdoMemorySnapshotAt(snapshot, 0u, &info))
        printf("memory_item=id:%s title:%s content:%s tags:%zu pinned:%d entry_revision:%llu\n",
            info.Id, info.Title, info.Content, info.TagCount,
            info.Pinned ? 1 : 0, (unsigned long long)info.Revision);
    MdoMemorySnapshotRelease(snapshot);
}

typedef struct ToolProbe {
    unsigned Permissions;
    unsigned Resources;
} ToolProbe;

static xwork_permission_decision AllowMemory(void *data,
    const xwork_permission_request *request) {
    ToolProbe *probe = (ToolProbe*)data;
    ++probe->Permissions;
    probe->Resources += (unsigned)request->iResourceCount;
    return XWORK_PERMISSION_ALLOW;
}

static bool ExecuteMemory(xwork_agent *agent, const char *name,
    const char *arguments, bool expect_success) {
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
    call.sId = (char*)"memory-probe-call";
    call.sName = (char*)name;
    call.sArgumentsJson = (char*)arguments;
    memset(&result, 0, sizeof(result));
    infrastructure = executor.pExecute != NULL &&
        executor.pExecute(executor.pUserData, &call, &context, &result);
    printf("tool_%s=infra:%d success:%d text:%s\n", name,
        infrastructure ? 1 : 0, result.bSuccess ? 1 : 0,
        result.sContent != NULL ? result.sContent : "null");
    xworkExecutorUnbind(&executor);
    return infrastructure && result.bSuccess == expect_success;
}

void ServiceInit(XS_HostInfo *host) {
    static const char *tags[] = {"preference", "editor"};
    static const char invalid[] = "{}";
    xwork_runtime_config runtime_config;
    xwork_runtime *runtime = NULL;
    xwork_agent_definition_config definition_config;
    xwork_agent_definition *definition = NULL;
    xllm_session_config session_config;
    xllm_session *llm_session = NULL;
    xllm_session *isolated_session = NULL;
    xwork_agent_options agent_options;
    xwork_agent *agent = NULL;
    xwork_agent *isolated = NULL;
    xwork_tool_catalog *held_catalog = NULL;
    MdoProjectLease *held_project = NULL;
    MdoMemoryWriteOptions write;
    MdoMemoryRemoveOptions remove;
    MdoMemoryImportOptions import_options;
    MdoMemoryTransferSummary export_summary;
    MdoMemoryTransferSummary preview_summary;
    MdoMemoryTransferSummary import_summary;
    xwork_error error;
    ToolProbe tool_probe;
    char *prompt = NULL;
    size_t prompt_bytes = 0u;
    uint64 prompt_generation = 0u;
    bool result;
    (void)host;
    memset(&tool_probe, 0, sizeof(tool_probe));

    if (!MdoHomeInit() || !MdoProjectLifecycleInit() || !MdoConfigInit()) {
        printf("init_error=pre-runtime\n"); goto done;
    }
    xworkRuntimeConfigInit(&runtime_config);
    runtime = xworkRuntimeCreate(&runtime_config, &error);
    if (runtime == NULL || !MdoMemoryManagerInit(runtime)) {
        printf("init_error=runtime\n"); goto done;
    }
    PrintSnapshot("global_empty", MDO_MEMORY_GLOBAL, NULL);
    if (!MemoryLeaseProbe()) goto done;

    MdoMemoryWriteOptionsInit(&write);
    write.Scope = MDO_MEMORY_GLOBAL;
    write.Id = "editor-style";
    write.Title = "Editor preference";
    write.Content = "Prefer compact diffs and explicit validation.";
    write.Tags = tags;
    write.TagCount = 2u;
    write.Pinned = true;
    write.Actor = "runtime-probe";
    write.SessionId = "session-a";
    write.Reason = "user preference";
    if (!MdoMemoryUpsert(&write, &error)) {
        printf("write_error=%s\n", error.sMessage); goto done;
    }
    PrintSnapshot("global_written", MDO_MEMORY_GLOBAL, NULL);

    write.ExpectedRevision = 0u;
    result = MdoMemoryUpsert(&write, &error);
    printf("stale_write=%d code:%d\n", result ? 1 : 0, (int)error.eCode);
    write.ExpectedRevision = UINT64_MAX;
    write.Id = "credential";
    write.Title = "Unsafe";
    write.Content = "password: should-not-persist";
    result = MdoMemoryUpsert(&write, &error);
    printf("sensitive_write=%d code:%d\n", result ? 1 : 0, (int)error.eCode);

    MdoMemoryWriteOptionsInit(&write);
    write.Scope = MDO_MEMORY_PROJECT;
    write.ProjectId = "project-alpha";
    write.Id = "build-command";
    write.Title = "Build";
    write.Content = "Use the locked xserver checkout for builds.";
    write.Actor = "runtime-probe";
    if (!MdoMemoryUpsert(&write, &error)) goto done;
    PrintSnapshot("project_written", MDO_MEMORY_PROJECT, "project-alpha");

    prompt = MdoMemoryBuildPrompt("project-alpha", &prompt_bytes,
        &prompt_generation, &error);
    printf("prompt=ok:%d bytes:%zu generation:%llu global:%d project:%d untrusted:%d\n",
        prompt != NULL ? 1 : 0, prompt_bytes,
        (unsigned long long)prompt_generation,
        prompt != NULL && strstr(prompt, "editor-style") != NULL,
        prompt != NULL && strstr(prompt, "build-command") != NULL,
        prompt != NULL && strstr(prompt, "untrusted reference data") != NULL);
    xrtFree(prompt); prompt = NULL;

    MdoMemoryManagerUnit();
    if (!MdoMemoryManagerInit(runtime)) goto done;
    PrintSnapshot("project_recovered", MDO_MEMORY_PROJECT, "project-alpha");

    xworkAgentDefinitionConfigInit(&definition_config);
    definition_config.sId = "memory.probe.agent";
    definition_config.eApprovalMode = XWORK_APPROVAL_CALLBACK;
    definition_config.bRegisterBuiltinTools = false;
    definition = xworkAgentDefinitionCreate(&definition_config, &error);
    xllmSessionConfigInit(&session_config);
    llm_session = xllmSessionCreate(&session_config, NULL);
    isolated_session = xllmSessionCreate(&session_config, NULL);
    xworkAgentOptionsInit(&agent_options);
    agent_options.pSession = llm_session;
    agent_options.sWorkspaceRoot = ".";
    agent_options.OnPermission = AllowMemory;
    agent_options.pPermissionUserData = &tool_probe;
    agent = xworkAgentCreateWithRuntime(runtime, definition, &agent_options,
        &error);
    agent_options.pSession = isolated_session;
    isolated = xworkAgentCreateWithRuntime(runtime, definition, &agent_options,
        &error);
    if (definition == NULL || llm_session == NULL || isolated_session == NULL ||
        agent == NULL ||
        isolated == NULL ||
        !MdoMemoryAgentBind(agent, "project-alpha", "session-a", &error) ||
        !MdoMemoryAgentBind(isolated, "project-beta", "session-b", &error))
        goto done;
    if (!ExecuteMemory(agent, "memory_search",
            "{\"query\":\"build\",\"scope\":\"project\",\"limit\":4}", true) ||
        !ExecuteMemory(isolated, "memory_search",
            "{\"query\":\"build\",\"scope\":\"project\",\"limit\":4}", true) ||
        !ExecuteMemory(agent, "memory_write",
            "{\"scope\":\"project\",\"id\":\"tool-note\",\"title\":\"Tool\",\"content\":\"Written through the memory tool.\",\"expected_revision\":1,\"reason\":\"probe\"}", true) ||
        !ExecuteMemory(agent, "memory_write",
            "{\"scope\":\"project\",\"id\":\"stale-note\",\"title\":\"Stale\",\"content\":\"Must not commit.\",\"expected_revision\":1}", false) ||
        !ExecuteMemory(agent, "memory_delete",
            "{\"scope\":\"project\",\"id\":\"tool-note\",\"expected_revision\":2,\"reason\":\"probe cleanup\"}", true))
        goto done;
    printf("tool_permissions=%u resources:%u\n", tool_probe.Permissions,
        tool_probe.Resources);

    held_catalog = xworkAgentToolCatalogSnapshot(agent);
    if (held_catalog == NULL || MemoryLeaseFree("project-alpha")) goto done;
    MdoMemoryAgentUnbind(agent);
    if (MemoryLeaseFree("project-alpha")) goto done;
    printf("memory_lease_binding_retained=1\n");
    xworkToolCatalogRelease(held_catalog); held_catalog = NULL;
    held_project = MdoProjectLeaseAcquire("project-alpha",
        MDO_PROJECT_LEASE_EXCLUSIVE, &error);
    if (held_project == NULL) goto done;
    printf("memory_lease_binding_released=1\n");
    if (MdoMemoryAgentBind(agent, "project-alpha", "session-a", &error) ||
        error.eCode != XWORK_ERROR_CONTEXT) goto done;
    printf("memory_lease_binding_rejected=1\n");

    MdoMemoryRemoveOptionsInit(&remove);
    remove.Scope = MDO_MEMORY_GLOBAL;
    remove.Id = "editor-style";
    remove.ExpectedRevision = 1u;
    remove.Actor = "runtime-probe";
    remove.Reason = "preference removed";
    if (!MdoMemoryRemove(&remove, &error)) goto done;
    printf("memory_lease_global_independent=1\n");
    MdoProjectLeaseRelease(held_project); held_project = NULL;
    PrintSnapshot("global_removed", MDO_MEMORY_GLOBAL, NULL);

    memset(&export_summary, 0, sizeof(export_summary));
    export_summary.Size = sizeof(export_summary);
    if (!MdoMemoryExportDirectory("memory-export", &export_summary, &error)) {
        printf("export_error=%s\n", error.sMessage); goto done;
    }
    printf("directory_export=stores:%zu projects:%zu entries:%zu generation:%llu bytes:%llu\n",
        export_summary.StoreCount, export_summary.ProjectCount,
        export_summary.EntryCount,
        (unsigned long long)export_summary.Generation,
        (unsigned long long)export_summary.TotalBytes);
    result = MdoMemoryExportDirectory("memory-export", &export_summary, &error);
    printf("directory_existing=%d code:%d\n", result ? 1 : 0,
        (int)error.eCode);
    memset(&preview_summary, 0, sizeof(preview_summary));
    preview_summary.Size = sizeof(preview_summary);
    if (!MdoMemoryPreviewImportDirectory("memory-export", &preview_summary,
            &error)) {
        printf("preview_error=%s\n", error.sMessage); goto done;
    }
    printf("directory_preview=stores:%zu projects:%zu entries:%zu generation:%llu\n",
        preview_summary.StoreCount, preview_summary.ProjectCount,
        preview_summary.EntryCount,
        (unsigned long long)preview_summary.Generation);
    {
        xfile unknown = xrtOpen("memory-export/unknown.txt",
            XFILE_WRITE | XFILE_CREATE | XFILE_EXCLUSIVE | XFILE_NOFOLLOW);
        bool invalid_preview;
        if (unknown == NULL || !xrtWriteFull(unknown, "x", 1u, NULL) ||
            !xrtClose(unknown)) goto done;
        unknown = NULL;
        memset(&import_summary, 0, sizeof(import_summary));
        import_summary.Size = sizeof(import_summary);
        invalid_preview = MdoMemoryPreviewImportDirectory("memory-export",
            &import_summary, &error);
        printf("directory_unknown=%d code:%d\n", invalid_preview ? 1 : 0,
            (int)error.eCode);
        if (!xrtFileDelete("memory-export/unknown.txt")) goto done;
    }
    {
        size_t saved_size = 0u;
        bytes saved = xrtFileReadAllLimit(
            "memory-export/projects/project-alpha.json", 5u * 1024u * 1024u,
            &saved_size);
        bytes tampered = saved != NULL ? (bytes)xrtMalloc(saved_size + 1u) : NULL;
        bool invalid_hash;
        if (saved == NULL || tampered == NULL) {
            xrtFree(tampered); xrtFree(saved); goto done;
        }
        memcpy(tampered, saved, saved_size);
        tampered[saved_size] = '\n';
        if (!xrtFileWriteAll(
                "memory-export/projects/project-alpha.json",
                (xbytesview){tampered, saved_size + 1u})) {
            xrtFree(tampered); xrtFree(saved); goto done;
        }
        xrtFree(tampered);
        memset(&import_summary, 0, sizeof(import_summary));
        import_summary.Size = sizeof(import_summary);
        invalid_hash = MdoMemoryPreviewImportDirectory("memory-export",
            &import_summary, &error);
        printf("directory_hash=%d code:%d\n", invalid_hash ? 1 : 0,
            (int)error.eCode);
        if (!xrtFileWriteAll("memory-export/projects/project-alpha.json",
                (xbytesview){saved, saved_size})) {
            xrtFree(saved); goto done;
        }
        xrtFree(saved);
    }
    MdoMemoryImportOptionsInit(&import_options);
    import_options.Directory = "memory-export";
    import_options.ExpectedGeneration = preview_summary.Generation - 1u;
    import_options.Actor = "runtime-probe";
    import_options.Reason = "portable memory restore";
    memset(&import_summary, 0, sizeof(import_summary));
    import_summary.Size = sizeof(import_summary);
    held_project = MdoProjectLeaseAcquire("project-alpha",
        MDO_PROJECT_LEASE_EXCLUSIVE, &error);
    if (held_project == NULL) goto done;
    result = MdoMemoryImportDirectory(&import_options, &import_summary, &error);
    if (result || strcmp(error.sMessage, "project lifecycle is busy") != 0 ||
        MdoMemoryManagerGeneration() != preview_summary.Generation) goto done;
    printf("memory_lease_directory_import=1\n");
    MdoProjectLeaseRelease(held_project); held_project = NULL;
    if (!MemoryLeaseFree("project-alpha")) goto done;
    result = MdoMemoryImportDirectory(&import_options, &import_summary, &error);
    printf("directory_stale=%d code:%d\n", result ? 1 : 0,
        (int)error.eCode);
    if (!MdoHomeRemove("memory/global.json", false) ||
        !MdoHomeRemove("memory/global.json.bak", false) ||
        !MdoHomeRemove("memory/projects/project-alpha.json", false) ||
        !MdoHomeRemove("memory/projects/project-alpha.json.bak", false))
        goto done;
    import_options.ExpectedGeneration = preview_summary.Generation;
    memset(&import_summary, 0, sizeof(import_summary));
    import_summary.Size = sizeof(import_summary);
    if (!MdoMemoryImportDirectory(&import_options, &import_summary, &error)) {
        printf("import_error=%s\n", error.sMessage); goto done;
    }
    printf("directory_import=stores:%zu projects:%zu entries:%zu generation:%llu\n",
        import_summary.StoreCount, import_summary.ProjectCount,
        import_summary.EntryCount,
        (unsigned long long)import_summary.Generation);
    PrintSnapshot("global_imported", MDO_MEMORY_GLOBAL, NULL);
    PrintSnapshot("project_imported", MDO_MEMORY_PROJECT, "project-alpha");
    result = MdoMemoryImportDirectory(&import_options, &import_summary, &error);
    printf("directory_nonempty=%d code:%d\n", result ? 1 : 0,
        (int)error.eCode);

    if (!MdoHomeAtomicWrite("memory/projects/broken.json", invalid,
            sizeof(invalid) - 1u, false)) goto done;
    PrintSnapshot("broken_project", MDO_MEMORY_PROJECT, "broken");
    PrintSnapshot("project_intact", MDO_MEMORY_PROJECT, "project-alpha");
    printf("probe_done=1\n");
done:
    xrtFree(prompt);
    xworkToolCatalogRelease(held_catalog);
    MdoProjectLeaseRelease(held_project);
    MdoMemoryAgentUnbind(isolated);
    MdoMemoryAgentUnbind(agent);
    xworkAgentDestroy(isolated);
    xworkAgentDestroy(agent);
    xllmSessionDestroy(isolated_session);
    xllmSessionDestroy(llm_session);
    xworkAgentDefinitionRelease(definition);
    MdoMemoryManagerUnit();
    xworkRuntimeRelease(runtime);
    MdoConfigUnit();
    MdoProjectLifecycleUnit();
    MdoHomeUnit();
}

void ServiceUnit(XS_HostInfo *host) { (void)host; }
'''


def write_site(site: Path) -> None:
    for relative in (
        "web",
        "default-home/config",
        "src/storage",
        "src/projects",
        "src/config",
        "src/memory",
        "include/mdo",
    ):
        (site / relative).mkdir(parents=True, exist_ok=True)
    (site / "web/index.html").write_text("probe", encoding="utf-8")
    for relative in (
        "default-home/config/defaults.json",
        "src/storage/home.c", "src/storage/home_import.inc.c", "src/storage/home_purge.inc.c",
        "src/projects/lifecycle.c",
        "src/config/config.c",
        "src/memory/manager.c",
        "src/memory/transfer.c",
    ):
        shutil.copy2(ROOT / "app" / relative, site / relative)
    shutil.copy2(
        ROOT / "app/src/memory/internal.h", site / "src/memory/internal.h"
    )
    for name in ("home.h", "home_import.h", "home_purge.h", "config.h", "memory.h", "project_lifecycle.h"):
        shutil.copy2(ROOT / "app/include/mdo" / name, site / "include/mdo" / name)
    # Inject a bounded publication fault only into this temporary application's copy.
    manager = site / "src/memory/manager.c"
    text = manager.read_text(encoding="utf-8")
    publication = "    Ok = MdoHomeAtomicWrite(Path, Json, Size, true);"
    assert text.count(publication) == 1
    manager.write_text(text.replace(publication,
        "    Ok = MemoryProbePublish(Path) &&\n"
        "        MdoHomeAtomicWrite(Path, Json, Size, true);", 1),
        encoding="utf-8", newline="\n")
    (site / "probe.c").write_text(PROBE_SOURCE, encoding="utf-8")
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        port = listener.getsockname()[1]
    (site / "xs.json").write_text(json.dumps({
        "services": [{
            "enabled": True,
            "class": "http",
            "name": "memory-probe",
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


def run_probe(host: Path, site: Path, home: Path) -> str:
    process = subprocess.Popen(
        [str(host), "xs.json", "--", "--home", str(home)],
        cwd=site,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
        creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0,
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
    done.wait(timeout=20.0)
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
    parser.add_argument("--host", type=Path,
        default=ROOT / ".build/host" / ("xs.exe" if os.name == "nt" else "xs"))
    args = parser.parse_args()
    host = args.host.resolve()
    if not host.is_file():
        print(f"missing xs host: {host}", file=sys.stderr)
        return 2
    (ROOT / ".build").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="memory-runtime-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        site = base / "site"
        home = base / "home"
        write_site(site)
        output = run_probe(host, site, home)
        assert "init_error=" not in output, output
        assert "write_error=" not in output, output
        for label in ("mutations", "import_blocked", "partial_cleanup", "import_rollback",
                      "binding_retained", "binding_released", "binding_rejected",
                      "global_independent", "directory_import"):
            assert f"memory_lease_{label}=1" in output, output
        assert "global_empty=ok:1 revision:0 count:0 generation:1 code:0" in output, output
        assert "global_written=ok:1 revision:1 count:1 generation:2 code:0" in output, output
        assert "memory_item=id:editor-style title:Editor preference" in output, output
        assert "tags:2 pinned:1 entry_revision:1" in output, output
        assert "stale_write=0 code:7" in output, output
        assert "sensitive_write=0 code:1" in output, output
        assert "project_written=ok:1 revision:1 count:1 generation:3 code:0" in output, output
        assert "prompt=ok:1" in output and "global:1 project:1 untrusted:1" in output, output
        assert "project_recovered=ok:1 revision:1 count:1 generation:1 code:0" in output, output
        assert "tool_memory_search=infra:1 success:1" in output, output
        assert '{"success":true,"untrusted":true' in output, output
        assert '"count":1,"results":[{"scope":"project"' in output, output
        assert '"count":0,"results":[]' in output, output
        assert 'tool_memory_write=infra:1 success:1' in output, output
        assert 'tool_memory_write=infra:1 success:0' in output, output
        assert 'tool_memory_delete=infra:1 success:1' in output, output
        assert "tool_permissions=5 resources:3" in output, output
        assert "global_removed=ok:1 revision:2 count:0 generation:4 code:0" in output, output
        assert "directory_export=stores:2 projects:1 entries:1 generation:4" in output, output
        assert "directory_existing=0 code:7" in output, output
        assert "directory_preview=stores:2 projects:1 entries:1 generation:4" in output, output
        assert "directory_unknown=0 code:1" in output, output
        assert "directory_hash=0 code:1" in output, output
        assert "directory_stale=0 code:7" in output, output
        assert "directory_import=stores:2 projects:1 entries:1 generation:5" in output, output
        assert "global_imported=ok:1 revision:2 count:0 generation:5 code:0" in output, output
        assert "project_imported=ok:1 revision:3 count:1 generation:5 code:0" in output, output
        assert "directory_nonempty=0 code:7" in output, output
        assert "broken_project=ok:0 revision:0 count:0 generation:0" in output, output
        assert "project_intact=ok:1 revision:3 count:1 generation:5 code:0" in output, output
        assert "probe_done=1" in output, output

        global_store = json.loads((home / "memory/global.json").read_text(encoding="utf-8"))
        project_store = json.loads((home / "memory/projects/project-alpha.json").read_text(encoding="utf-8"))
        assert global_store["schema_version"] == 1 and global_store["revision"] == 2
        assert global_store["entries"] == []
        assert project_store["revision"] == 3
        assert project_store["entries"][0]["id"] == "build-command"
        audit_lines = (home / "memory/audit.jsonl").read_text(encoding="utf-8").splitlines()
        audit = [json.loads(line) for line in audit_lines]
        assert len(audit) == 7
        assert [item["operation"] for item in audit] == [
            "create", "create", "create", "remove", "remove",
            "import", "import"
        ]
        assert all(item["phase"] == "prepared" for item in audit)
        assert all("content" not in item for item in audit)
        assert audit[0]["content_sha256"] == hashlib.sha256(
            b"Prefer compact diffs and explicit validation.").hexdigest()
        assert audit[3]["content_sha256"] == ""
        assert audit[4]["content_sha256"] == ""
        assert audit[5]["content_sha256"] and audit[6]["content_sha256"]
        serialized = "\n".join(audit_lines).lower()
        assert "password" not in serialized and "should-not-persist" not in serialized
        export_root = site / "memory-export"
        export_manifest = json.loads((export_root / "manifest.json").read_text(
            encoding="utf-8"
        ))
        assert export_manifest["kind"] == "mdo-memory-directory"
        assert export_manifest["store_count"] == 2
        assert [item["path"] for item in export_manifest["files"]] == [
            "global.json", "projects/project-alpha.json"
        ]
        for item in export_manifest["files"]:
            data = (export_root / item["path"]).read_bytes()
            assert len(data) == item["bytes"]
            assert hashlib.sha256(data).hexdigest() == item["sha256"]
    print("memory runtime probe: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
