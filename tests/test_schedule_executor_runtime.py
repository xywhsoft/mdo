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


from runtime_sources import copy_app_source, copy_echo_module


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
#include "src/schedules/manager.c"
static void ShutdownLeaseCheckpoint(void);
#include "src/schedules/executor.c"

xwork_runtime *MdoBootstrapRuntime(void) { return NULL; }
static bool UpdatePending;
bool MdoUpdateInstalling(void) { return UpdatePending; }
static bool UpdateRequired;
bool MdoUpdateBlocked(void) { return UpdateRequired; }

typedef struct Owner {
    unsigned Refs;
    unsigned Retains;
    unsigned Releases;
    unsigned Calls;
    bool SawPrompt;
    bool CheckLease;
    bool SawLease;
} Owner;

static Owner *ShutdownOwner;
static bool ShutdownProtected;
static bool ShutdownOwnerReleased;

static bool ExecutorLeaseAvailable(const char *project) {
    MdoProjectLease *lease = MdoProjectLeaseAcquire(project,
        MDO_PROJECT_LEASE_EXCLUSIVE, NULL);
    bool available = lease != NULL;
    MdoProjectLeaseRelease(lease);
    return available;
}

static void ShutdownLeaseCheckpoint(void) {
    if (ShutdownOwner == NULL) return;
    ShutdownProtected = !ExecutorLeaseAvailable("project-alpha");
    ShutdownOwnerReleased = ShutdownOwner->Refs == 1u;
}

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
    if (owner->CheckLease)
        owner->SawLease = !ExecutorLeaseAvailable("project-alpha");
    for (i = 0u; i < request->iMessageCount; ++i) {
        const char *text = request->pMessages[i].sContent;
        if (text != NULL && strstr(text, "execute scheduled review") != NULL)
            owner->SawPrompt = true;
    }
    *response = Response("scheduled-agent-result");
    return *response != NULL ? XLLM_RESULT_OK : XLLM_RESULT_ERROR;
}

typedef struct CancelProbe {
    xatomic32 Entered;
    xatomic32 SawCancel;
    xatomic32 Release;
    bool Immediate;
} CancelProbe;

static xllm_result CancelComplete(void *data, const xllm_request *request,
    const xllm_stream_callbacks *callbacks, xllm_response **response,
    xllm_error *error) {
    CancelProbe *probe = (CancelProbe*)data;
    unsigned i;
    (void)callbacks; (void)error;
    *response = NULL;
    xrtAtomic32Store(&probe->Entered, 1u, XMEMORY_RELEASE);
    if (probe->Immediate) {
        *response = Response("completed before stop");
        return *response != NULL ? XLLM_RESULT_OK : XLLM_RESULT_ERROR;
    }
    for (i = 0u; i < 1000u; ++i) {
        if (xrtCancelRequested(request->pCancel)) {
            xrtAtomic32Store(&probe->SawCancel, 1u, XMEMORY_RELEASE);
            if (xrtAtomic32Load(&probe->Release, XMEMORY_ACQUIRE) != 0u)
                return XLLM_RESULT_CANCELLED;
        }
        xrtSleep(2u);
    }
    return XLLM_RESULT_ERROR;
}

static bool WaitAtomic(xatomic32 *value) {
    unsigned i;
    for (i = 0u; i < 500u; ++i) {
        if (xrtAtomic32Load(value, XMEMORY_ACQUIRE) != 0u) return true;
        xrtSleep(2u);
    }
    return false;
}

static bool ExecutorCancellationProbe(xwork_runtime *runtime) {
    const int64 now = 1700000000000000LL;
    MdoScheduleExecutorOptions executor;
    MdoScheduleCreateOptions create;
    CancelProbe probe;
    xwork_error error;
    xwork_task_snapshot *tasks = NULL;
    xwork_task_info task;
    uint64 task_id = 0u, run_id = 0u;
    size_t started = 0u, completed = 0u;
    unsigned mode, i;
    bool handled = true, ok = false;
    memset(&probe, 0, sizeof(probe));
    xrtAtomic32Init(&probe.Entered, 0u);
    xrtAtomic32Init(&probe.SawCancel, 0u);
    xrtAtomic32Init(&probe.Release, 0u);
    MdoScheduleExecutorOptionsInit(&executor);
    executor.Automatic = false;
    executor.OnModelComplete = CancelComplete;
    executor.ModelUserData = &probe;
    if (!MdoScheduleExecutorInit(runtime, &executor, &error) ||
        !MdoScheduleExecutorCancelTask(UINT64_MAX, &handled, &error) || handled ||
        MdoScheduleExecutorCancelTask(0u, &handled, &error) ||
        error.eCode != XWORK_ERROR_INVALID_ARGUMENT) goto done;
    /* Model cancellation, generic task_cancel forwarding, and completion
     * before the user stops all run through the real Agent worker. */
    for (mode = 0u; mode < 4u; ++mode) {
        const char *id = mode == 0u ? "cancel-model" :
            (mode == 1u ? "cancel-generic" : mode == 2u ? "cancel-completed" : "cancel-update");
        xrtAtomic32Store(&probe.Entered, 0u, XMEMORY_RELEASE);
        xrtAtomic32Store(&probe.SawCancel, 0u, XMEMORY_RELEASE);
        xrtAtomic32Store(&probe.Release, 0u, XMEMORY_RELEASE);
        probe.Immediate = mode == 2u;
        MdoScheduleCreateOptionsInit(&create);
        create.Id = id; create.Label = id;
        create.ProjectId = "project-alpha";
        create.Input = "bounded cancellation";
        create.AgentId = "mdo.default";
        create.StartAt = now + 60000000LL;
        create.Enabled = false;
        if (!MdoScheduleCreate(&create, NULL, &error) ||
            !MdoScheduleExecutorRunNow(id, 1u, now, &task_id, &run_id, &error) ||
            !WaitAtomic(&probe.Entered)) goto done;
        if (mode == 2u) {
            MdoAgentRunInfo info;
            for (i = 0u; i < 500u; ++i) {
                memset(&info, 0, sizeof(info)); info.Size = sizeof(info);
                if (!MdoAgentRunGetInfo(g_MdoScheduleExecutor.Active[0].Run,
                        &info)) goto done;
                if (info.Run.eState == XWORK_RUN_SUCCEEDED) break;
                xrtSleep(2u);
            }
            if (i == 500u || !MdoScheduleExecutorCancelTask(task_id,
                    &handled, &error) || !handled ||
                g_MdoScheduleExecutor.ActiveCount != 0u ||
                MdoScheduleExecutorTaskCancellationRequested(task_id)) goto done;
        } else {
            if (mode == 0u) {
                if (!MdoScheduleExecutorCancelTask(task_id, &handled, &error) ||
                    !handled || !MdoScheduleExecutorCancelTask(task_id,
                        &handled, &error) || !handled) goto done;
                tasks = xworkRuntimeTaskSnapshot(runtime, 0u, &error);
                xworkTaskInfoInit(&task);
                if (tasks == NULL || !xworkTaskSnapshotFind(tasks, task_id, &task) ||
                    task.eState != XWORK_TASK_RUNNING ||
                    !MdoScheduleExecutorTaskCancellationRequested(task_id) ||
                    ExecutorLeaseAvailable("project-alpha")) goto done;
                xworkTaskSnapshotRelease(tasks); tasks = NULL;
            } else if (mode == 1u) {
                if (!xworkRuntimeCancelTask(runtime, task_id, &error) ||
                    !MdoScheduleExecutorPump(now, &started, &completed, &error) ||
                    !MdoScheduleExecutorTaskCancellationRequested(task_id)) goto done;
            } else {
                UpdateRequired = true;
                if (MdoScheduleExecutorRunNow(id, 1u, now, NULL, NULL, &error) ||
                    !MdoScheduleExecutorPump(now, &started, &completed, &error) || started != 0u ||
                    !MdoScheduleExecutorTaskCancellationRequested(task_id)) goto done;
            }
            if (!WaitAtomic(&probe.SawCancel)) goto done;
            printf("schedule_cancel_%s_requested=1\n", mode == 0u ? "model" : "generic");
            xrtAtomic32Store(&probe.Release, 1u, XMEMORY_RELEASE);
            completed = 0u;
            for (i = 0u; i < 500u && completed == 0u; ++i) {
                xrtSleep(2u);
                if (!MdoScheduleExecutorPump(now, &started, &completed, &error)) goto done;
            }
            if (completed != 1u || g_MdoScheduleExecutor.ActiveCount != 0u ||
                !ExecutorLeaseAvailable("project-alpha")) goto done;
            UpdateRequired = false;
        }
        tasks = xworkRuntimeTaskSnapshot(runtime, 0u, &error);
        xworkTaskInfoInit(&task);
        if (tasks == NULL || !xworkTaskSnapshotFind(tasks, task_id, &task) ||
            task.eState != (mode == 2u ? XWORK_TASK_SUCCEEDED : XWORK_TASK_CANCELLED) ||
            !MdoScheduleExecutorCancelTask(task_id, &handled, &error) || handled)
            goto done;
        xworkTaskSnapshotRelease(tasks); tasks = NULL;
        printf("schedule_cancel_%s_finished=1\n", mode == 2u ? "completed" :
            (mode == 0u ? "model" : "generic"));
    }
    ok = true;
done:
    UpdateRequired = false;
    xrtAtomic32Store(&probe.Release, 1u, XMEMORY_RELEASE);
    xworkTaskSnapshotRelease(tasks);
    MdoScheduleExecutorUnit();
    if (!ok) printf("cancel_probe_error=%s\n", error.sMessage);
    return ok;
}

static bool ExecutorOwnerLeaseProbe(xwork_runtime *runtime) {
    const int64 start = 1700000000000000LL;
    MdoAgentSessionOptions options;
    MdoAgentSession *session = NULL;
    MdoScheduleCreateOptions create;
    MdoScheduleExecutorOptions executor;
    MdoProjectLease *exclusive = NULL;
    xwork_run *retained = NULL;
    xwork_agent *agent = NULL;
    xwork_run_config retained_config;
    MdoHomeSnapshot home;
    xfileinfo stat;
    xwork_error error;
    Owner owner;
    size_t started = 0u, completed = 0u;
    uint64 task_id, run_id;
    unsigned i;
    bool ok = false;
    memset(&owner, 0, sizeof(owner)); owner.Refs = 1u; owner.CheckLease = true;
    MdoAgentSessionOptionsInit(&options);
    options.ProjectId = "project-alpha";
    options.WorkspaceRoot = ".";
    options.OnModelComplete = Complete;
    options.ModelUserData = &owner;
    options.OwnerUserData = &owner;
    options.OnOwnerRetain = OwnerRetain;
    options.OnOwnerRelease = OwnerRelease;
    exclusive = MdoProjectLeaseAcquire("PROJECT-ALPHA.",
        MDO_PROJECT_LEASE_EXCLUSIVE, &error);
    session = MdoAgentSessionCreateWithRuntime(runtime, &options, &error);
    memset(&home, 0, sizeof(home)); home.Size = sizeof(home);
    if (exclusive == NULL || session != NULL || error.eCode != XWORK_ERROR_CONTEXT ||
        owner.Refs != 1u || !MdoHomeGetSnapshot(&home) ||
        xrtPathStat(home.Path, false, &stat)) goto done;
    MdoProjectLeaseRelease(exclusive); exclusive = NULL;
    options.AgentId = "missing-agent";
    session = MdoAgentSessionCreateWithRuntime(runtime, &options, &error);
    if (session != NULL || owner.Refs != 1u ||
        !ExecutorLeaseAvailable("project-alpha")) goto done;
    printf("schedule_owner_creation=1\n");

    MdoScheduleExecutorOptionsInit(&executor);
    executor.Automatic = false;
    executor.OnModelComplete = Complete;
    executor.ModelUserData = &owner;
    executor.OwnerUserData = &owner;
    executor.OnOwnerRetain = OwnerRetain;
    executor.OnOwnerRelease = OwnerRelease;
    if (!MdoScheduleExecutorInit(runtime, &executor, &error)) goto done;
    MdoScheduleCreateOptionsInit(&create);
    create.Id = "lease-failed-start";
    create.Label = "Failed startup";
    create.ProjectId = "project-alpha";
    create.AgentId = "missing-agent";
    create.Input = "bounded failed startup";
    create.StartAt = start;
    if (!MdoScheduleCreate(&create, NULL, &error) ||
        MdoScheduleExecutorPump(start, &started, &completed, &error) ||
        !ExecutorLeaseAvailable("project-alpha") || owner.Refs != 1u) goto done;
    printf("schedule_owner_failed_start=1\n");
    create.Id = "lease-agent";
    create.Label = "Retained owner";
    create.AgentId = "mdo.default";
    create.Input = "execute scheduled review";
    if (!MdoScheduleCreate(&create, NULL, &error) ||
        !MdoScheduleExecutorPump(start, &started, &completed, &error) ||
        started != 1u || g_MdoScheduleExecutor.ActiveCount != 1u) goto done;
    agent = g_MdoScheduleExecutor.Active[0].Run->Session->Agent;
    xworkRunConfigInit(&retained_config);
    retained_config.sPrompt = "retained runtime reference, never started";
    retained = xworkRunCreate(agent, &retained_config, &error);
    if (retained == NULL || ExecutorLeaseAvailable("project-alpha") ||
        !ExecutorLeaseAvailable("project-beta")) goto done;
    {
        xwork_tool_catalog *tools = xworkAgentToolCatalogSnapshot(agent);
        xwork_tool_info info;
        bool has_memory = false;
        if (tools == NULL) goto done;
        for (i = 0u; i < xworkToolCatalogCount(tools); ++i) {
            memset(&info, 0, sizeof(info));
            if (xworkToolCatalogToolAt(tools, i, &info) && info.sName != NULL &&
                strncmp(info.sName, "memory_", 7u) == 0) has_memory = true;
        }
        xworkToolCatalogRelease(tools);
        if (has_memory) goto done;
    }
    completed = 0u;
    for (i = 0u; i < 100u && completed == 0u; ++i) {
        xrtSleep(5u);
        if (!MdoScheduleExecutorPump(start, &started, &completed, &error)) goto done;
    }
    if (completed != 1u || g_MdoScheduleExecutor.ActiveCount != 0u ||
        !owner.SawLease || ExecutorLeaseAvailable("project-alpha")) goto done;
    printf("schedule_owner_retained=1\n");
    xworkRunDestroy(retained); retained = NULL;
    if (!ExecutorLeaseAvailable("project-alpha") || owner.Refs != 1u) goto done;
    printf("schedule_owner_released=1\n");
    if (!MdoScheduleExecutorRunNow("lease-agent", 2u, start,
            &task_id, &run_id, &error)) goto done;
    ShutdownOwner = &owner;
    MdoScheduleExecutorUnit();
    ShutdownOwner = NULL;
    if (!ShutdownProtected || !ShutdownOwnerReleased ||
        !ExecutorLeaseAvailable("project-alpha")) goto done;
    printf("schedule_owner_shutdown=1\n");
    ok = true;
done:
    MdoProjectLeaseRelease(exclusive);
    MdoAgentSessionRelease(session);
    MdoScheduleExecutorUnit();
    xworkRunDestroy(retained);
    ShutdownOwner = NULL;
    return ok;
}

static bool ExecutorDefaultWorkspaceProbe(xwork_runtime *runtime) {
    const int64 start = 1700000000000000LL;
    MdoScheduleCreateOptions create;
    MdoScheduleExecutorOptions executor;
    xwork_error error;
    Owner owner = {0};
    char *expected = NULL, *real = NULL, *actual = NULL;
    bool exists, ok = false;
    size_t started = 0u, completed = 0u;
    unsigned i;
    expected = MdoHomeDefaultWorkspacePath(false);
    if (expected == NULL || !MdoHomeExternalStat(MDO_DEFAULT_WORKSPACE_PATH,
            &exists, NULL) || exists) goto done;
    MdoScheduleCreateOptionsInit(&create);
    create.Id = "default-workspace";
    create.Label = "Portable default workspace";
    create.ProjectId = "default";
    create.AgentId = "mdo.default";
    create.ModelId = "ornith-1.5-35b";
    create.Input = "execute scheduled review";
    create.StartAt = start;
    MdoScheduleExecutorOptionsInit(&executor);
    executor.Automatic = false;
    executor.OnModelComplete = Complete;
    executor.ModelUserData = &owner;
    if (!MdoScheduleCreate(&create, NULL, &error) ||
        !MdoScheduleExecutorInit(runtime, &executor, &error) ||
        !MdoScheduleExecutorPump(start, &started, &completed, &error) ||
        started != 1u || g_MdoScheduleExecutor.ActiveCount != 1u) goto done;
    real = xrtPathReal(expected);
    actual = xrtPathReal(xworkAgentWorkspaceRoot(
        g_MdoScheduleExecutor.Active[0].Run->Session->Agent));
    if (real == NULL || actual == NULL || strcmp(real, actual) != 0) goto done;
    for (i = 0u; i < 200u && completed == 0u; ++i) {
        xrtSleep(5000u);
        if (!MdoScheduleExecutorPump(start, &started, &completed, &error)) goto done;
    }
    ok = completed == 1u && owner.Calls == 1u && owner.SawPrompt;
done:
    MdoScheduleExecutorUnit();
    xrtFree(actual); xrtFree(real); xrtFree(expected);
    printf("schedule_default_workspace=%d\n", ok ? 1 : 0);
    return ok;
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
    if (!MdoHomeInit() || !MdoProjectLifecycleInit() ||
        !MdoConfigInit() || !MdoModelManagerInit()) {
        printf("init_error=product\n"); goto done;
    }
    xworkRuntimeConfigInit(&runtime_config);
    runtime = xworkRuntimeCreate(&runtime_config, &error);
    if (runtime == NULL || !MdoSkillManagerInit() ||
        !MdoMemoryManagerInit(runtime) || !MdoModuleManagerInit(runtime) ||
        !MdoScheduleManagerInit(runtime)) {
        printf("init_error=runtime\n"); goto done;
    }
    if (getenv("MDO_SCHEDULE_WORKSPACE_ONLY") != NULL) {
        (void)ExecutorDefaultWorkspaceProbe(runtime);
        printf("probe_done=1\n"); goto done;
    }
    if (getenv("MDO_SCHEDULE_OWNER_ONLY") != NULL) {
        if (!ExecutorOwnerLeaseProbe(runtime)) printf("owner_lease_error=1\n");
        printf("probe_done=1\n"); goto done;
    }
    if (getenv("MDO_SCHEDULE_CANCEL_ONLY") != NULL) {
        if (!ExecutorCancellationProbe(runtime)) printf("cancel_error=1\n");
        printf("probe_done=1\n"); goto done;
    }
    MdoScheduleCreateOptionsInit(&create);
    create.Id = "agent-review";
    create.Label = "Agent review";
    create.ProjectId = "project-alpha";
    create.AgentId = "mdo.default";
    create.ModelId = "ornith-1.5-35b";
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
    UpdatePending = true;
    if (!MdoScheduleExecutorPump(start, &started, &completed, &error) || started != 0u) {
        printf("update_gate_error=1\n"); goto done;
    }
    UpdatePending = false;
    printf("update_gate=paused\n");
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
    create.ModelId = "ornith-1.5-35b";
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
    MdoProjectLifecycleUnit();
    MdoHomeUnit();
}

void ServiceUnit(XS_HostInfo *host) { (void)host; }
'''


def write_site(site: Path, memory_enabled: bool = True) -> None:
    for relative in (
        "web", "default-home/config", "default-home/modules/tools",
        "default-home/modules/agents", "default-home/skills/project-explorer/templates",
        "generated/module-sdk/mdo", "src/storage", "src/projects", "src/config", "src/security",
        "src/models", "src/skills", "src/memory", "src/modules", "src/agents", "src/asks",
        "src/schedules", "include/mdo",
    ):
        (site / relative).mkdir(parents=True, exist_ok=True)
    (site / "web/index.html").write_text("probe", encoding="utf-8")
    for relative in (
        "default-home/config/defaults.json",
        "default-home/modules/agents/builtin_default.c",
        "default-home/skills/project-explorer/SKILL.md",
        "default-home/skills/project-explorer/templates/report.md",
        "src/storage/home.c", "src/storage/home_import.inc.c", "src/storage/home_purge.inc.c", "src/storage/home_restore.inc.c", "src/projects/lifecycle.c",
        "src/config/config.c", "src/security/secrets.c",
        "src/models/catalog.c", "src/skills/manager.c", "src/memory/manager.c",
        "src/modules/manager.c", "src/agents/runtime.c",
        "src/asks/manager.c",
        "src/schedules/manager.c", "src/schedules/executor.c",
    ):
        copy_app_source(relative, site)
    copy_echo_module(site)
    for header in (ROOT / "app/include/mdo").glob("*.h"):
        shutil.copy2(header, site / "include/mdo" / header.name)
    shutil.copy2(ROOT / "app/src/memory/internal.h", site / "src/memory/internal.h")
    shutil.copy2(ROOT / "app/src/schedules/internal.h",
                 site / "src/schedules/internal.h")
    shutil.copy2(ROOT / "include/mdo/module.h",
                 site / "generated/module-sdk/mdo/module.h")
    if not memory_enabled:
        defaults = site / "default-home/config/defaults.json"
        config = json.loads(defaults.read_text(encoding="utf-8"))
        config["settings"]["agent"]["memory"] = False
        defaults.write_text(json.dumps(config), encoding="utf-8")
    executor = site / "src/schedules/executor.c"
    source = executor.read_text(encoding="utf-8")
    old = '        MdoAgentRunDestroy(Active[i].Run);'
    assert source.count(old) == 1
    source = source.replace(old, old + '\n        ShutdownLeaseCheckpoint();')
    executor.write_text(source, encoding="utf-8")
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


def run_probe(host: Path, site: Path, home: Path, owner_only: bool = False,
              cancel_only: bool = False, workspace_only: bool = False) -> str:
    env = os.environ.copy()
    if owner_only:
        env["MDO_SCHEDULE_OWNER_ONLY"] = "1"
    if cancel_only:
        env["MDO_SCHEDULE_CANCEL_ONLY"] = "1"
    if workspace_only:
        env["MDO_SCHEDULE_WORKSPACE_ONLY"] = "1"
    process = subprocess.Popen(
        [str(host), "xs.json", "--", "--home", str(home)], cwd=site,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
        encoding="utf-8", errors="replace", env=env,
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
        owner_site = base / "owner-site"
        owner_home = base / "owner-home"
        write_site(owner_site, memory_enabled=False)
        workspace_home = base / "workspace-home"
        workspace_output = run_probe(host, site, workspace_home, workspace_only=True)
        assert "schedule_default_workspace=1" in workspace_output, workspace_output
        assert (workspace_home / "workspace").is_dir()
        cancel_home = base / "cancel-home"
        cancel_output = run_probe(host, site, cancel_home, cancel_only=True)
        assert "cancel_error=" not in cancel_output, cancel_output
        for label in ("model_requested", "generic_requested", "model_finished",
                      "generic_finished", "completed_finished"):
            assert f"schedule_cancel_{label}=1" in cancel_output, cancel_output
        for schedule, result in (("cancel-model", -2), ("cancel-generic", -2),
                                 ("cancel-completed", 0)):
            records = [json.loads(line) for line in
                (cancel_home / f"schedules/history/{schedule}.jsonl").read_text(
                    encoding="utf-8").splitlines()]
            assert len(records) == 1 and records[0]["result"] == result, records
            assert records[0]["agent_run_id"] > 0, records
        owner_output = run_probe(host, owner_site, owner_home, owner_only=True)
        assert "owner_lease_error=" not in owner_output, owner_output
        for label in ("creation", "failed_start", "retained", "released", "shutdown"):
            assert f"schedule_owner_{label}=1" in owner_output, owner_output
        failed_history = [json.loads(line) for line in
            (owner_home / "schedules/history/lease-failed-start.jsonl").read_text(
                encoding="utf-8").splitlines()]
        assert len(failed_history) == 1 and failed_history[0]["result"] == -1
        assert failed_history[0]["agent_run_id"] == 0
        owner_history = [json.loads(line) for line in
            (owner_home / "schedules/history/lease-agent.jsonl").read_text(
                encoding="utf-8").splitlines()]
        assert [item["result"] for item in owner_history] == [0, -2]
        output = run_probe(host, site, home)
        assert "init_error=" not in output, output
        assert "create_error=" not in output, output
        assert "executor_error=" not in output, output
        assert "update_gate=paused" in output and "update_gate_error=" not in output, output
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
