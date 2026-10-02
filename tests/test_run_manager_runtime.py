"""Deterministic interactive Agent run start, result, and cancel probe."""

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


from runtime_sources import copy_app_source


ROOT = Path(__file__).resolve().parent.parent

PROBE_SOURCE = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xsbase.h>

#include "src/storage/home.c"
#include "src/projects/lifecycle.c"
#include "src/config/config.c"
#include "src/security/secrets.c"
#include "src/models/catalog.c"
#include "src/skills/manager.c"
#include "src/memory/manager.c"
#include "src/modules/manager.c"
#include "src/agents/runtime.c"
#include "src/asks/manager.c"
#include "src/sessions/data_gate.c"
#include "src/sessions/events.c"
#include "src/sessions/todo.c"
#include "src/sessions/sidecars/binding.c"
#include "src/sessions/attachments.c"
#include "src/sessions/manager.c"
#include "src/runs/manager.c"

xwork_runtime *MdoBootstrapRuntime(void) { return NULL; }

typedef struct Owner {
    unsigned Refs;
    unsigned Retains;
    unsigned Releases;
    unsigned Calls;
    bool SawPrompt;
} Owner;

static bool OwnerRetain(void *data) {
    Owner *owner = (Owner*)data;
    if (owner == NULL || owner->Refs == 0u) return false;
    ++owner->Refs; ++owner->Retains; return true;
}

static void OwnerRelease(void *data) {
    Owner *owner = (Owner*)data;
    if (owner == NULL || owner->Refs == 0u) return;
    --owner->Refs; ++owner->Releases;
}

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
    response->sModel = Copy("ornith-1.5-35b");
    response->sRequestId = Copy("run-manager-probe");
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
    Owner *owner = (Owner*)data;
    size_t i;
    (void)callbacks; (void)error;
    ++owner->Calls;
    for (i = 0u; i < request->iMessageCount; ++i) {
        const char *text = request->pMessages[i].sContent;
        if (text != NULL && strstr(text, "managed interactive prompt") != NULL)
            owner->SawPrompt = true;
    }
    *response = Response("interactive-agent-result");
    return *response != NULL ? XLLM_RESULT_OK : XLLM_RESULT_ERROR;
}

static bool WaitForTerminal(const char *id, MdoRunInfo *found) {
    unsigned attempt;
    for (attempt = 0u; attempt < 200u; ++attempt) {
        MdoRunSnapshot *snapshot;
        xwork_error error;
        size_t completed = 0u;
        if (!MdoRunManagerPump(&completed, &error)) return false;
        snapshot = MdoRunSnapshotCreate(&error);
        if (snapshot == NULL) return false;
        memset(found, 0, sizeof(*found)); found->Size = sizeof(*found);
        if (MdoRunSnapshotFind(snapshot, id, found) && found->Terminal) {
            MdoRunSnapshotRelease(snapshot);
            return true;
        }
        MdoRunSnapshotRelease(snapshot);
        xrtSleep(5u);
    }
    return false;
}

void ServiceInit(XS_HostInfo *host) {
    xwork_runtime_config runtime_config;
    xwork_runtime *runtime = NULL;
    xwork_error error;
    MdoSessionCreateOptions create;
    MdoSessionInfo session_info;
    MdoSession *session = NULL;
    MdoRunManagerOptions manager;
    MdoRunStartOptions start;
    MdoRunInfo first;
    MdoRunInfo second;
    MdoRunInfo found;
    MdoRunManagerStatus status;
    bool may_have_executed = true;
    MdoRunSnapshot *snapshot = NULL;
    const char *text = NULL;
    size_t text_size = 0u;
    Owner owner;
    char session_id[MDO_SESSION_ID_CAPACITY] = {0};
    (void)host;
    memset(&owner, 0, sizeof(owner)); owner.Refs = 1u;
    if (!MdoHomeInit() || !MdoProjectLifecycleInit() ||
        !MdoConfigInit() || !MdoModelManagerInit() ||
        !MdoSkillManagerInit()) {
        printf("init_error=product\n"); goto done;
    }
    xworkRuntimeConfigInit(&runtime_config);
    runtime = xworkRuntimeCreate(&runtime_config, &error);
    if (runtime == NULL || !MdoMemoryManagerInit(runtime) ||
        !MdoModuleManagerInit(runtime) || !MdoSessionManagerInit(runtime)) {
        printf("init_error=runtime\n"); goto done;
    }
    MdoSessionCreateOptionsInit(&create);
    create.ProjectId = "project-alpha";
    create.Title = "Interactive run probe";
    create.Agent.OnModelComplete = Complete;
    create.Agent.ModelUserData = &owner;
    create.Agent.OwnerUserData = &owner;
    create.Agent.OnOwnerRetain = OwnerRetain;
    create.Agent.OnOwnerRelease = OwnerRelease;
    session = MdoSessionCreate(&create, &error);
    if (session == NULL) {
        printf("create_error=%s\n", error.sMessage); goto done;
    }
    memset(&session_info, 0, sizeof(session_info));
    session_info.Size = sizeof(session_info);
    if (!MdoSessionGetInfo(session, &session_info)) goto done;
    snprintf(session_id, sizeof(session_id), "%s", session_info.Id);
    MdoSessionRelease(session); session = NULL;

    MdoRunManagerOptionsInit(&manager);
    manager.Automatic = false;
    manager.MaxActive = 1u;
    manager.MaxRetained = 2u;
    manager.OnModelComplete = Complete;
    manager.ModelUserData = &owner;
    manager.OwnerUserData = &owner;
    manager.OnOwnerRetain = OwnerRetain;
    manager.OnOwnerRelease = OwnerRelease;
    if (!MdoRunManagerInit(runtime, &manager, &error)) {
        printf("manager_error=%s\n", error.sMessage); goto done;
    }
    printf("manager_owner=refs:%u retains:%u releases:%u\n",
        owner.Refs, owner.Retains, owner.Releases);

    MdoRunStartOptionsInit(&start);
    start.ProjectId = "project-alpha";
    start.SessionId = session_id;
    start.Prompt = "managed interactive prompt";
    start.TimeoutMilliseconds = 5000u;
    memset(&first, 0, sizeof(first)); first.Size = sizeof(first);
    if (!MdoRunStart(&start, &first, &error)) {
        printf("start_error=%s\n", error.sMessage); goto done;
    }
    printf("first_started=id:%s agent_run:%llu state:%d refs:%u\n",
        first.Id, (unsigned long long)first.AgentRunId, (int)first.State,
        owner.Refs);
    memset(&second, 0, sizeof(second)); second.Size = sizeof(second);
    if (MdoRunStartWithOutcome(&start, &second, &error,
            &may_have_executed) || may_have_executed) {
        printf("prestart_outcome_error=unexpected execution\n"); goto done;
    }
    printf("prestart_denied=may_execute:%d\n", may_have_executed ? 1 : 0);
    if (!WaitForTerminal(first.Id, &found)) {
        printf("wait_error=first\n"); goto done;
    }
    snapshot = MdoRunSnapshotCreate(&error);
    if (snapshot == NULL || !MdoRunSnapshotResult(snapshot, first.Id,
            &text, &text_size)) goto done;
    printf("first_done=state:%d result:%d terminal:%d bytes:%zu text:%s refs:%u\n",
        (int)found.State, (int)found.Result, found.Terminal ? 1 : 0,
        text_size, text != NULL ? text : "null", owner.Refs);
    MdoRunSnapshotRelease(snapshot); snapshot = NULL;

    start.Prompt = "cancel this managed prompt";
    memset(&second, 0, sizeof(second)); second.Size = sizeof(second);
    if (!MdoRunStart(&start, &second, &error) ||
        !MdoRunCancel(second.Id, NULL, &error)) {
        printf("cancel_error=%s\n", error.sMessage); goto done;
    }
    if (!WaitForTerminal(second.Id, &found)) {
        printf("wait_error=second\n"); goto done;
    }
    printf("second_done=state:%d result:%d terminal:%d cancel:%d refs:%u\n",
        (int)found.State, (int)found.Result, found.Terminal ? 1 : 0,
        found.CancelRequested ? 1 : 0, owner.Refs);
    memset(&status, 0, sizeof(status)); status.Size = sizeof(status);
    if (!MdoRunManagerGetStatus(&status)) goto done;
    printf("status=active:%zu starting:%zu retained:%zu started:%llu completed:%llu failed:%llu\n",
        status.ActiveRuns, status.StartingRuns, status.RetainedRuns,
        (unsigned long long)status.RunsStarted,
        (unsigned long long)status.RunsCompleted,
        (unsigned long long)status.RunsFailed);
    printf("callback=calls:%u prompt:%d refs:%u retains:%u releases:%u\n",
        owner.Calls, owner.SawPrompt ? 1 : 0, owner.Refs,
        owner.Retains, owner.Releases);
    MdoRunManagerUnit();
    printf("manager_unit=refs:%u balanced:%d\n", owner.Refs,
        owner.Retains == owner.Releases ? 1 : 0);
    printf("probe_done=1\n");
done:
    MdoRunSnapshotRelease(snapshot);
    MdoSessionRelease(session);
    MdoRunManagerUnit();
    MdoSessionManagerUnit();
    MdoModuleManagerUnit();
    MdoMemoryManagerUnit();
    MdoSkillManagerUnit();
    xworkRuntimeRelease(runtime);
    MdoModelManagerUnit();
    MdoConfigUnit();
    MdoProjectLifecycleUnit();
    MdoHomeUnit();
}

void ServiceUnit(XS_HostInfo *host) { (void)host; }
'''


def write_site(site: Path) -> None:
    for relative in (
        "web", "default-home/config", "default-home/modules/tools",
        "default-home/modules/agents", "default-home/skills/project-explorer/templates",
        "generated/module-sdk/mdo", "src/storage", "src/config", "src/security",
        "src/models", "src/skills", "src/memory", "src/modules", "src/agents", "src/asks",
        "src/projects", "src/sessions", "src/sessions/sidecars", "src/runs", "include/mdo",
    ):
        (site / relative).mkdir(parents=True, exist_ok=True)
    (site / "web/index.html").write_text("probe", encoding="utf-8")
    for relative in (
        "default-home/config/defaults.json",
        "default-home/modules/tools/builtin_echo.c",
        "default-home/modules/agents/builtin_default.c",
        "default-home/skills/project-explorer/SKILL.md",
        "default-home/skills/project-explorer/templates/report.md",
        "src/storage/home.c", "src/storage/home_import.inc.c", "src/storage/home_purge.inc.c", "src/storage/home_restore.inc.c", "src/projects/lifecycle.c",
        "src/config/config.c", "src/security/secrets.c",
        "src/models/catalog.c", "src/skills/manager.c", "src/memory/manager.c",
        "src/modules/manager.c", "src/agents/runtime.c",
        "src/sessions/data_gate.c", "src/sessions/data_gate.h", "src/sessions/events.c",
        "src/asks/manager.c",
        "src/sessions/todo.c", "src/sessions/attachments.c",
        "src/sessions/sidecars/binding.c", "src/sessions/sidecars/binding.h",
        "src/sessions/internal.h", "src/sessions/manager.c", "src/sessions/restore_reservation.inc.c", "src/runs/manager.c",
    ):
        copy_app_source(relative, site)
    for header in (ROOT / "app/include/mdo").glob("*.h"):
        shutil.copy2(header, site / "include/mdo" / header.name)
    shutil.copy2(ROOT / "app/src/memory/internal.h", site / "src/memory/internal.h")
    shutil.copy2(ROOT / "include/mdo/module.h",
                 site / "generated/module-sdk/mdo/module.h")
    (site / "probe.c").write_text(PROBE_SOURCE, encoding="utf-8")
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        port = listener.getsockname()[1]
    (site / "xs.json").write_text(json.dumps({"services": [{
        "enabled": True, "class": "http", "name": "run-manager-probe",
        "ip": "127.0.0.1", "port": port,
        "host_default": {"enabled": True, "name": "probe", "path": "web",
                         "devlang": "c", "devfile": "probe.c"},
    }]}), encoding="utf-8")


def run_probe(host: Path, site: Path, home: Path) -> str:
    process = subprocess.Popen(
        [str(host), "xs.json", "--", "--home", str(home)], cwd=site,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
        encoding="utf-8", errors="replace",
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
    with tempfile.TemporaryDirectory(prefix="run-manager-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        site = base / "site"
        home = base / "home"
        write_site(site)
        output = run_probe(host, site, home)
        assert "init_error=" not in output, output
        assert "create_error=" not in output, output
        assert "manager_error=" not in output, output
        assert "start_error=" not in output and "wait_error=" not in output, output
        assert "cancel_error=" not in output, output
        assert "manager_owner=refs:2" in output, output
        assert "first_started=id:run-" in output, output
        assert "prestart_denied=may_execute:0" in output, output
        assert "first_done=state:2 result:0 terminal:1 bytes:24 text:interactive-agent-result refs:2" in output, output
        assert "second_done=state:4 result:-2 terminal:1 cancel:1 refs:2" in output, output
        assert "status=active:0 starting:0 retained:2 started:2 completed:2 failed:1" in output, output
        assert "callback=calls:" in output and "prompt:1 refs:2" in output, output
        assert "manager_unit=refs:1 balanced:1" in output, output
        assert "probe_done=1" in output, output
    print("interactive run manager runtime probe: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
