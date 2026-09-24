"""Bounded scheduled Agent execution probe through the real xs/TCC runtime."""

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
#include <stdlib.h>
#include <string.h>
#include <xsbase.h>

#include "src/storage/home.c"
#include "src/config/config.c"
#include "src/security/secrets.c"
#include "src/models/catalog.c"
#include "src/skills/manager.c"
#include "src/memory/manager.c"
#include "src/modules/manager.c"
#include "src/agents/runtime.c"
#include "src/asks/manager.c"
#include "src/schedules/manager.c"
#include "src/schedules/executor.c"

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
    response->sModel = Copy("ling-3.0-tiny");
    response->sRequestId = Copy("schedule-executor-probe");
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
        if (text != NULL && strstr(text, "execute scheduled review") != NULL)
            owner->SawPrompt = true;
    }
    *response = Response("scheduled-agent-result");
    return *response != NULL ? XLLM_RESULT_OK : XLLM_RESULT_ERROR;
}

void ServiceInit(XS_HostInfo *host) {
    const int64 start = 1700000000000000LL;
    xwork_runtime_config runtime_config;
    xwork_runtime *runtime = NULL;
    MdoScheduleCreateOptions create;
    MdoScheduleExecutorOptions executor_options;
    MdoScheduleExecutorSnapshot snapshot;
    MdoScheduleCatalog *catalog = NULL;
    MdoScheduleInfo manual_info;
    xwork_error error;
    Owner owner;
    size_t started = 0u, completed = 0u;
    uint64 manual_task = 0u, manual_run = 0u;
    unsigned i;
    (void)host;
    memset(&owner, 0, sizeof(owner)); owner.Refs = 1u;
    if (!MdoHomeInit() || !MdoConfigInit() || !MdoModelManagerInit()) {
        printf("init_error=product\n"); goto done;
    }
    xworkRuntimeConfigInit(&runtime_config);
    runtime = xworkRuntimeCreate(&runtime_config, &error);
    if (runtime == NULL || !MdoSkillManagerInit() ||
        !MdoMemoryManagerInit(runtime) || !MdoModuleManagerInit(runtime) ||
        !MdoScheduleManagerInit(runtime)) {
        printf("init_error=runtime\n"); goto done;
    }
    MdoScheduleCreateOptionsInit(&create);
    create.Id = "agent-review";
    create.Label = "Agent review";
    create.ProjectId = "project-alpha";
    create.AgentId = "mdo.default";
    create.ModelId = "ling-3.0-tiny";
    create.Protocol = MDO_MODEL_PROTOCOL_OPENAI_RESPONSES;
    create.ReasoningEffort = "medium";
    create.WorkspaceRoot = ".";
    create.Input = "execute scheduled review";
    create.StartAt = start;
    if (!MdoScheduleCreate(&create, NULL, &error)) {
        printf("create_error=%s\n", error.sMessage); goto done;
    }
    MdoScheduleExecutorOptionsInit(&executor_options);
    executor_options.Automatic = false;
    executor_options.OnModelComplete = Complete;
    executor_options.ModelUserData = &owner;
    executor_options.OwnerUserData = &owner;
    executor_options.OnOwnerRetain = OwnerRetain;
    executor_options.OnOwnerRelease = OwnerRelease;
    if (!MdoScheduleExecutorInit(runtime, &executor_options, &error)) {
        printf("executor_error=%s\n", error.sMessage); goto done;
    }
    if (!MdoScheduleExecutorPump(start, &started, &completed, &error)) {
        printf("pump_error=%s\n", error.sMessage); goto done;
    }
    printf("first_pump=started:%zu completed:%zu refs:%u\n",
        started, completed, owner.Refs);
    for (i = 0u; i < 100u && completed == 0u; ++i) {
        xrtSleep(5u);
        started = 0u;
        if (!MdoScheduleExecutorPump(start, &started, &completed, &error)) {
            printf("harvest_error=%s\n", error.sMessage); goto done;
        }
    }
    MdoScheduleCreateOptionsInit(&create);
    create.Id = "manual-review";
    create.Label = "Manual review";
    create.ProjectId = "project-alpha";
    create.AgentId = "mdo.default";
    create.ModelId = "ling-3.0-tiny";
    create.Protocol = MDO_MODEL_PROTOCOL_OPENAI_RESPONSES;
    create.WorkspaceRoot = ".";
    create.Input = "execute scheduled review";
    create.StartAt = start + 60000000LL;
    create.Enabled = false;
    if (!MdoScheduleCreate(&create, NULL, &error) ||
        !MdoScheduleExecutorRunNow("manual-review", 1u, start,
            &manual_task, &manual_run, &error)) {
        printf("manual_error=%s\n", error.sMessage); goto done;
    }
    catalog = MdoScheduleCatalogSnapshot(&error);
    memset(&manual_info, 0, sizeof(manual_info));
    manual_info.Size = sizeof(manual_info);
    if (catalog == NULL || !MdoScheduleCatalogFind(catalog,
            "manual-review", &manual_info)) {
        printf("manual_catalog_error=1\n"); goto done;
    }
    MdoScheduleCatalogRelease(catalog); catalog = NULL;
    printf("manual=task:%llu run:%llu revision:%llu next:%lld enabled:%d\n",
        (unsigned long long)manual_task, (unsigned long long)manual_run,
        (unsigned long long)manual_info.Revision,
        (long long)manual_info.NextOccurrenceAt,
        manual_info.Enabled ? 1 : 0);
    completed = 0u;
    for (i = 0u; i < 100u && completed == 0u; ++i) {
        xrtSleep(5u);
        started = 0u;
        if (!MdoScheduleExecutorPump(start, &started, &completed, &error)) {
            printf("manual_harvest_error=%s\n", error.sMessage); goto done;
        }
    }
    memset(&snapshot, 0, sizeof(snapshot)); snapshot.Size = sizeof(snapshot);
    if (!MdoScheduleExecutorGetSnapshot(&snapshot)) {
        printf("snapshot_error=1\n"); goto done;
    }
    printf("executor=automatic:%d active:%zu claims:%llu completed:%llu failed:%llu\n",
        snapshot.Automatic ? 1 : 0, snapshot.ActiveRuns,
        (unsigned long long)snapshot.ClaimsStarted,
        (unsigned long long)snapshot.RunsCompleted,
        (unsigned long long)snapshot.RunsFailed);
    printf("callback=calls:%u prompt:%d refs:%u retains:%u releases:%u\n",
        owner.Calls, owner.SawPrompt ? 1 : 0, owner.Refs,
        owner.Retains, owner.Releases);
    printf("probe_done=1\n");
done:
    MdoScheduleCatalogRelease(catalog);
    MdoScheduleExecutorUnit();
    MdoScheduleManagerUnit();
    MdoModuleManagerUnit();
    MdoMemoryManagerUnit();
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
        "web", "default-home/config", "default-home/modules/tools",
        "default-home/modules/agents", "default-home/skills/project-explorer/templates",
        "generated/module-sdk/mdo", "src/storage", "src/config", "src/security",
        "src/models", "src/skills", "src/memory", "src/modules", "src/agents", "src/asks",
        "src/schedules", "include/mdo",
    ):
        (site / relative).mkdir(parents=True, exist_ok=True)
    (site / "web/index.html").write_text("probe", encoding="utf-8")
    for relative in (
        "default-home/config/defaults.json",
        "default-home/modules/tools/builtin_echo.c",
        "default-home/modules/agents/builtin_default.c",
        "default-home/skills/project-explorer/SKILL.md",
        "default-home/skills/project-explorer/templates/report.md",
        "src/storage/home.c", "src/config/config.c", "src/security/secrets.c",
        "src/models/catalog.c", "src/skills/manager.c", "src/memory/manager.c",
        "src/modules/manager.c", "src/agents/runtime.c",
        "src/asks/manager.c",
        "src/schedules/manager.c", "src/schedules/executor.c",
    ):
        shutil.copy2(ROOT / "app" / relative, site / relative)
    for header in (ROOT / "app/include/mdo").glob("*.h"):
        shutil.copy2(header, site / "include/mdo" / header.name)
    shutil.copy2(ROOT / "app/src/memory/internal.h", site / "src/memory/internal.h")
    shutil.copy2(ROOT / "app/src/schedules/internal.h",
                 site / "src/schedules/internal.h")
    shutil.copy2(ROOT / "include/mdo/module.h",
                 site / "generated/module-sdk/mdo/module.h")
    (site / "probe.c").write_text(PROBE_SOURCE, encoding="utf-8")
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        port = listener.getsockname()[1]
    (site / "xs.json").write_text(json.dumps({"services": [{
        "enabled": True, "class": "http", "name": "schedule-executor-probe",
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
    with tempfile.TemporaryDirectory(prefix="schedule-executor-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        site = base / "site"
        home = base / "home"
        write_site(site)
        output = run_probe(host, site, home)
        assert "init_error=" not in output, output
        assert "create_error=" not in output, output
        assert "executor_error=" not in output, output
        assert "pump_error=" not in output and "harvest_error=" not in output, output
        assert "manual_error=" not in output and "manual_catalog_error=" not in output, output
        assert "manual_harvest_error=" not in output, output
        assert "first_pump=started:1 completed:0 refs:2" in output, output
        assert "manual=task:" in output and " revision:2 next:1700000060000000 enabled:0" in output, output
        assert "executor=automatic:0 active:0 claims:2 completed:2 failed:0" in output, output
        assert "callback=calls:2 prompt:1 refs:1 retains:2 releases:2" in output, output
        assert "probe_done=1" in output, output
        history = [json.loads(line) for line in
                   (home / "schedules/history/agent-review.jsonl").read_text(
                       encoding="utf-8").splitlines()]
        assert len(history) == 1
        assert history[0]["agent_run_id"] > 0
        assert history[0]["result"] == 0
        assert history[0]["text"] == "scheduled-agent-result"
        manual_history = [json.loads(line) for line in
                          (home / "schedules/history/manual-review.jsonl").read_text(
                              encoding="utf-8").splitlines()]
        assert len(manual_history) == 1, manual_history
        assert manual_history[0]["task_id"] > 0, manual_history
        assert manual_history[0]["agent_run_id"] > 0, manual_history
        assert manual_history[0]["result"] == 0, manual_history
        audit = (home / "schedules/audit.jsonl").read_text(encoding="utf-8")
        assert '"operation":"run-now"' in audit, audit
    print("schedule executor runtime probe: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
