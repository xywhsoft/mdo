"""Bounded create, recover, catalog, archive, and trash session probe."""

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

PROBE_SOURCE = r'''
#include <stdio.h>
#include <string.h>
#include <xsbase.h>

#include "src/storage/home.c"
#include "src/config/config.c"
#include "src/security/secrets.c"
#include "src/models/catalog.c"
#include "src/skills/manager.c"
#include "src/modules/manager.c"
#include "src/agents/runtime.c"
#include "src/sessions/manager.c"

xwork_runtime *MdoBootstrapRuntime(void) { return NULL; }

typedef struct Probe {
    unsigned Calls;
    bool SawPriorPrompt;
    bool SawPriorAnswer;
} Probe;

static char *Copy(const char *text) {
    size_t size = strlen(text) + 1u;
    char *copy = (char*)malloc(size);
    if (copy != NULL) memcpy(copy, text, size);
    return copy;
}

static xllm_response *Response(const char *text) {
    xllm_response *response = (xllm_response*)calloc(1u, sizeof(*response));
    if (response == NULL) return NULL;
    response->sContent = Copy(text);
    response->sModel = Copy("ling-3.0-tiny");
    response->sRequestId = Copy("session-probe");
    response->sFinishReason = Copy("stop");
    response->eFinish = XLLM_FINISH_STOP;
    response->uHttpStatus = 200u;
    if (response->sContent == NULL || response->sModel == NULL ||
        response->sRequestId == NULL || response->sFinishReason == NULL) {
        xllmResponseDestroy(response); return NULL;
    }
    return response;
}

static xllm_result Complete(void *data, const xllm_request *request,
    const xllm_stream_callbacks *callbacks, xllm_response **response,
    xllm_error *error) {
    Probe *probe = (Probe*)data;
    size_t i;
    (void)callbacks; (void)error;
    ++probe->Calls;
    for (i = 0u; i < request->iMessageCount; ++i) {
        const char *text = request->pMessages[i].sContent;
        if (text == NULL) continue;
        if (strstr(text, "first durable prompt") != NULL)
            probe->SawPriorPrompt = true;
        if (strstr(text, "durable-answer-one") != NULL)
            probe->SawPriorAnswer = true;
    }
    *response = Response(probe->Calls == 1u ?
        "durable-answer-one" : "durable-answer-two");
    return *response != NULL ? XLLM_RESULT_OK : XLLM_RESULT_ERROR;
}

static bool Run(MdoSession *session, const char *prompt) {
    MdoAgentSession *agent = MdoSessionAgentRef(session);
    MdoAgentRunOptions options;
    MdoAgentRun *run = NULL;
    xwork_run_result result;
    xwork_error error;
    xwork_result status;
    bool ok = false;
    if (agent == NULL) return false;
    MdoAgentRunOptionsInit(&options);
    options.Prompt = prompt;
    run = MdoAgentRunCreate(agent, &options, &error);
    MdoAgentSessionRelease(agent);
    if (run == NULL || !MdoAgentRunStart(run, &error)) goto done;
    memset(&result, 0, sizeof(result));
    status = MdoAgentRunWait(run, xrtDeadlineAfter(UINT64_C(5000000)),
        &result, &error);
    printf("run=%d text:%s\n", (int)status,
        result.sFinalText != NULL ? result.sFinalText : "null");
    ok = status == XWORK_RESULT_OK;
    xworkRunResultUnit(&result);
done:
    MdoAgentRunDestroy(run);
    return ok;
}

static void Catalog(const char *label) {
    xwork_error error;
    MdoSessionCatalog *catalog = MdoSessionCatalogSnapshot(&error);
    MdoSessionInfo info;
    size_t i;
    printf("%s=count:%zu diagnostics:%zu generation:%llu\n", label,
        MdoSessionCatalogCount(catalog),
        MdoSessionCatalogDiagnosticCount(catalog),
        (unsigned long long)MdoSessionCatalogGeneration(catalog));
    for (i = 0u; i < MdoSessionCatalogCount(catalog); ++i) {
        memset(&info, 0, sizeof(info)); info.Size = sizeof(info);
        if (MdoSessionCatalogAt(catalog, i, &info))
            printf("catalog_item=id:%s project:%s title:%s status:%d pinned:%d revision:%llu\n",
                info.Id, info.ProjectId, info.Title, (int)info.Status,
                info.Pinned ? 1 : 0, (unsigned long long)info.Revision);
    }
    MdoSessionCatalogRelease(catalog);
}

void ServiceInit(XS_HostInfo *host) {
    xwork_runtime_config runtime_config;
    xwork_runtime *runtime = NULL;
    xwork_error error;
    MdoSessionCreateOptions create;
    MdoSessionRuntimeOptions open;
    MdoSessionInfo info;
    MdoSession *session = NULL;
    MdoSession *blocked = NULL;
    MdoSession *stale = NULL;
    bool stale_update;
    Probe probe;
    char session_id[MDO_SESSION_ID_CAPACITY] = {0};
    static const char invalid[] = "{}";
    (void)host;
    memset(&probe, 0, sizeof(probe));
    if (!MdoHomeInit() || !MdoConfigInit() || !MdoModelManagerInit() ||
        !MdoSkillManagerInit()) { printf("init_error=pre-runtime\n"); goto done; }
    xworkRuntimeConfigInit(&runtime_config);
    runtime = xworkRuntimeCreate(&runtime_config, &error);
    if (runtime == NULL || !MdoModuleManagerInit(runtime) ||
        !MdoSessionManagerInit(runtime)) {
        printf("init_error=runtime message:%s\n", error.sMessage); goto done;
    }
    Catalog("catalog_empty");
    MdoSessionCreateOptionsInit(&create);
    create.ProjectId = "project-alpha";
    create.Title = "Initial title";
    create.Agent.WorkspaceRoot = ".";
    create.Agent.OnModelComplete = Complete;
    create.Agent.ModelUserData = &probe;
    session = MdoSessionCreate(&create, &error);
    if (session == NULL) { printf("create_error=%s\n", error.sMessage); goto done; }
    memset(&info, 0, sizeof(info)); info.Size = sizeof(info);
    if (!MdoSessionGetInfo(session, &info)) goto done;
    snprintf(session_id, sizeof(session_id), "%s", info.Id);
    printf("created=id:%s project:%s title:%s status:%d revision:%llu\n",
        info.Id, info.ProjectId, info.Title, (int)info.Status,
        (unsigned long long)info.Revision);
    if (!Run(session, "first durable prompt")) goto done;
    MdoSessionRelease(session); session = NULL;
    Catalog("catalog_after_create");

    MdoSessionRuntimeOptionsInit(&open);
    open.OnModelComplete = Complete;
    open.ModelUserData = &probe;
    session = MdoSessionOpen("project-alpha", session_id, &open, &error);
    if (session == NULL) { printf("recover_error=%s\n", error.sMessage); goto done; }
    if (!Run(session, "second prompt")) goto done;
    stale = MdoSessionLoad("project-alpha", session_id, &error);
    if (stale == NULL) goto done;
    if (!MdoSessionRename(session, "Renamed durable session", &error) ||
        !MdoSessionSetPinned(session, true, &error)) goto done;
    stale_update = MdoSessionRename(stale, "Lost update", &error);
    printf("stale_update=%d code:%d\n", stale_update ? 1 : 0,
        (int)error.eCode);
    MdoSessionRelease(stale); stale = NULL;
    MdoSessionRelease(session); session = NULL;

    session = MdoSessionLoad("project-alpha", session_id, &error);
    if (session == NULL || !MdoSessionSetArchived(session, true, &error)) goto done;
    MdoSessionRelease(session); session = NULL;
    blocked = MdoSessionOpen("project-alpha", session_id, &open, &error);
    printf("archived_open=%d code:%d\n", blocked != NULL ? 1 : 0,
        (int)error.eCode);
    MdoSessionRelease(blocked); blocked = NULL;
    session = MdoSessionLoad("project-alpha", session_id, &error);
    if (session == NULL || !MdoSessionSetArchived(session, false, &error) ||
        !MdoSessionMoveToTrash(session, &error)) goto done;
    MdoSessionRelease(session); session = NULL;
    Catalog("catalog_trash");
    session = MdoSessionLoad("project-alpha", session_id, &error);
    if (session == NULL || !MdoSessionRestore(session, &error)) goto done;
    MdoSessionRelease(session); session = NULL;

    MdoSessionCreateOptionsInit(&create);
    create.ProjectId = "project-alpha";
    create.Agent.AgentId = "missing.agent";
    blocked = MdoSessionCreate(&create, &error);
    printf("failed_create=%d code:%d\n", blocked != NULL ? 1 : 0,
        (int)error.eCode);
    MdoSessionRelease(blocked); blocked = NULL;
    Catalog("catalog_after_failed_create");

    if (!MdoHomeAtomicWrite("sessions/project-alpha/bad/meta.json",
            invalid, sizeof(invalid) - 1u, false)) goto done;
    Catalog("catalog_diagnostic");
    printf("recovery=calls:%u prior_prompt:%d prior_answer:%d\n",
        probe.Calls, probe.SawPriorPrompt ? 1 : 0,
        probe.SawPriorAnswer ? 1 : 0);
    printf("probe_done=1\n");
done:
    MdoSessionRelease(stale);
    MdoSessionRelease(blocked);
    MdoSessionRelease(session);
    MdoSessionManagerUnit();
    MdoModuleManagerUnit();
    MdoSkillManagerUnit();
    xworkRuntimeRelease(runtime);
    MdoModelManagerUnit();
    MdoConfigUnit();
    MdoHomeUnit();
}

void ServiceUnit(XS_HostInfo *host) { (void)host; }
'''


def write_site(site: Path) -> None:
    for relative in (
        "web",
        "default-home/config",
        "default-home/modules/tools",
        "default-home/modules/agents",
        "default-home/modules/subagents",
        "default-home/skills/project-explorer/templates",
        "generated/module-sdk/mdo",
        "src/storage",
        "src/config",
        "src/security",
        "src/models",
        "src/skills",
        "src/modules",
        "src/agents",
        "src/sessions",
        "include/mdo",
    ):
        (site / relative).mkdir(parents=True, exist_ok=True)
    (site / "web/index.html").write_text("probe", encoding="utf-8")
    for relative in (
        "default-home/config/defaults.json",
        "default-home/modules/tools/builtin_echo.c",
        "default-home/modules/agents/builtin_default.c",
        "default-home/skills/project-explorer/SKILL.md",
        "default-home/skills/project-explorer/templates/report.md",
        "src/storage/home.c",
        "src/config/config.c",
        "src/security/secrets.c",
        "src/models/catalog.c",
        "src/skills/manager.c",
        "src/modules/manager.c",
        "src/agents/runtime.c",
        "src/sessions/manager.c",
    ):
        shutil.copy2(ROOT / "app" / relative, site / relative)
    for header in (ROOT / "app/include/mdo").glob("*.h"):
        shutil.copy2(header, site / "include/mdo" / header.name)
    shutil.copy2(ROOT / "include/mdo/module.h", site / "generated/module-sdk/mdo/module.h")
    (site / "probe.c").write_text(PROBE_SOURCE, encoding="utf-8")
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        port = listener.getsockname()[1]
    (site / "xs.json").write_text(
        json.dumps({"services": [{"enabled": True, "class": "http",
            "name": "session-probe", "ip": "127.0.0.1", "port": port,
            "host_default": {"enabled": True, "name": "probe",
                "path": "web", "devlang": "c", "devfile": "probe.c"}}]}),
        encoding="utf-8",
    )


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
    done.wait(timeout=25.0)
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
    with tempfile.TemporaryDirectory(prefix="session-runtime-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        site = base / "site"
        home = base / "home"
        write_site(site)
        output = run_probe(host, site, home)
        assert "init_error=" not in output, output
        assert "create_error=" not in output, output
        assert "recover_error=" not in output, output
        assert "catalog_empty=count:0 diagnostics:0 generation:1" in output, output
        assert "created=id:" in output and "project:project-alpha" in output, output
        assert "run=0 text:durable-answer-one" in output, output
        assert "run=0 text:durable-answer-two" in output, output
        assert "catalog_after_create=count:1 diagnostics:0" in output, output
        assert "archived_open=0" in output, output
        assert "stale_update=0 code:7" in output, output
        assert "catalog_trash=count:1 diagnostics:0" in output, output
        assert "status:3 pinned:0" in output, output
        assert "failed_create=0 code:1" in output, output
        assert "catalog_after_failed_create=count:1 diagnostics:0" in output, output
        assert "catalog_diagnostic=count:1 diagnostics:1" in output, output
        assert "recovery=calls:2 prior_prompt:1 prior_answer:1" in output, output
        assert "probe_done=1" in output, output
        meta_files = list(home.glob("sessions/project-alpha/*/meta.json"))
        valid_meta = [path for path in meta_files if path.parent.name != "bad"]
        assert len(valid_meta) == 1, meta_files
        document = json.loads(valid_meta[0].read_text(encoding="utf-8"))
        assert document["status"] == "active" and document["title"] == "Renamed durable session"
        assert (valid_meta[0].parent / "snapshot.json").is_file()
    print("session runtime probe: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
