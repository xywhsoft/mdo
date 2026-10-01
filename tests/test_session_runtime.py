"""Bounded create, recover, catalog, archive, and trash session probe."""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import re
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
#include "src/sessions/backup.c"
#include "src/sessions/backup_snapshot.c"
#include "session-capture.c"
#include "session-backup.c"

xwork_runtime *MdoBootstrapRuntime(void) { return NULL; }

typedef struct Probe {
    unsigned Calls;
    bool SawPriorPrompt;
    bool SawPriorAnswer;
    bool CheckTruncated;
    bool SawTruncatedPrompt;
    bool CheckCleared;
    bool SawOldAfterClear;
    bool SawSystemAfterClear;
    bool CheckClearRecovery;
    bool SawClearPrompt;
    bool SawClearAnswer;
} Probe;

static bool LeaseBlocked(const char *id, MdoProjectLeaseMode mode) {
    xwork_error error;
    MdoProjectLease *lease = MdoProjectLeaseAcquire(id, mode, &error);
    bool blocked = lease == NULL && error.eCode == XWORK_ERROR_CONTEXT;
    MdoProjectLeaseRelease(lease);
    return blocked;
}

static int32 LeaseReaderThread(ptr data) {
    bool *blocked = (bool*)data;
    *blocked = LeaseBlocked("lifecycle-probe", MDO_PROJECT_LEASE_SHARED);
    return 0;
}

static bool LifecycleProbe(void) {
    xwork_error error;
    MdoProjectLease *first = NULL, *pin = NULL, *exclusive = NULL;
    MdoProjectLease *other = NULL, *invalid = NULL;
    xthread *thread = NULL;
    bool reader_blocked = false, ok = false;
    first = MdoProjectLeaseAcquire("LifeCycle-Probe.",
        MDO_PROJECT_LEASE_SHARED, &error);
    pin = MdoProjectLeaseRef(first);
    if (pin == NULL || !MdoProjectLeaseProtects(pin, "LIFECYCLE-PROBE.",
            MDO_PROJECT_LEASE_SHARED) ||
        MdoProjectLeaseProtects(pin, "lifecycle-probe", MDO_PROJECT_LEASE_EXCLUSIVE) ||
        MdoProjectLeaseProtects(pin, "another-project", MDO_PROJECT_LEASE_SHARED) ||
        !LeaseBlocked("lifecycle-probe",
            MDO_PROJECT_LEASE_EXCLUSIVE)) goto done;
    MdoProjectLeaseRelease(first); first = NULL;
    if (!LeaseBlocked("lifecycle-probe", MDO_PROJECT_LEASE_EXCLUSIVE)) goto done;
    other = MdoProjectLeaseAcquire("another-project",
        MDO_PROJECT_LEASE_EXCLUSIVE, &error);
    if (other == NULL) goto done;
    MdoProjectLeaseRelease(other); other = NULL;
    invalid = MdoProjectLeaseAcquire("../escape", MDO_PROJECT_LEASE_SHARED, &error);
    if (invalid != NULL || error.eCode != XWORK_ERROR_INVALID_ARGUMENT) goto done;
    /* Unit closes the old registry while pins survive. A fresh Init owns a
     * separate registry; releasing the old pin cannot unlock the new one. */
    MdoProjectLifecycleUnit();
    if (MdoProjectLeaseProtects(pin, "lifecycle-probe", MDO_PROJECT_LEASE_SHARED)) goto done;
    if (!LeaseBlocked("lifecycle-probe", MDO_PROJECT_LEASE_SHARED) ||
        !MdoProjectLifecycleInit()) goto done;
    exclusive = MdoProjectLeaseAcquire("lifecycle-probe",
        MDO_PROJECT_LEASE_EXCLUSIVE, &error);
    if (exclusive == NULL || !MdoProjectLeaseProtects(exclusive, "lifecycle-probe.",
            MDO_PROJECT_LEASE_EXCLUSIVE) ||
        MdoProjectLeaseProtects(pin, "lifecycle-probe", MDO_PROJECT_LEASE_SHARED)) goto done;
    MdoProjectLeaseRelease(pin); pin = NULL;
    thread = xrtThreadCreate(LeaseReaderThread, &reader_blocked, 0u);
    if (thread == NULL || xrtThreadWait(thread) != XWAIT_OK) goto done;
    xrtThreadDestroy(thread); thread = NULL;
    if (!reader_blocked || !LeaseBlocked("LIFECYCLE-PROBE.",
            MDO_PROJECT_LEASE_EXCLUSIVE)) goto done;
    MdoProjectLeaseRelease(exclusive); exclusive = NULL;
    first = MdoProjectLeaseAcquire("lifecycle-probe",
        MDO_PROJECT_LEASE_SHARED, &error);
    ok = first != NULL;
done:
    if (thread != NULL) { (void)xrtThreadWait(thread); xrtThreadDestroy(thread); }
    MdoProjectLeaseRelease(first);
    MdoProjectLeaseRelease(pin);
    MdoProjectLeaseRelease(exclusive);
    MdoProjectLeaseRelease(other);
    MdoProjectLeaseRelease(invalid);
    printf("lease_primitives=%d\n", ok ? 1 : 0);
    return ok;
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
    response->sRequestId = Copy("session-probe");
    response->sFinishReason = Copy("stop");
    response->eFinish = XLLM_FINISH_STOP;
    response->uHttpStatus = 200u;
    response->tUsage.uInputTokens = 40u;
    response->tUsage.uOutputTokens = 8u;
    response->tUsage.uTotalTokens = 48u;
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
    const char *answer = "durable-answer-extra";
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
        if (probe->CheckTruncated &&
            strstr(text, "third transient prompt") != NULL)
            probe->SawTruncatedPrompt = true;
        if (probe->CheckCleared) {
            if (request->pMessages[i].eRole == XLLM_ROLE_SYSTEM)
                probe->SawSystemAfterClear = true;
            if (strstr(text, "first durable prompt") != NULL ||
                strstr(text, "durable-answer-one") != NULL ||
                strstr(text, "second prompt") != NULL ||
                strstr(text, "durable-answer-two") != NULL ||
                strstr(text, "third transient prompt") != NULL ||
                strstr(text, "durable-answer-three") != NULL ||
                strstr(text, "after truncation prompt") != NULL ||
                strstr(text, "durable-answer-four") != NULL)
                probe->SawOldAfterClear = true;
        }
        if (probe->CheckClearRecovery) {
            if (strstr(text, "after clear prompt") != NULL)
                probe->SawClearPrompt = true;
            if (strstr(text, "durable-answer-five") != NULL)
                probe->SawClearAnswer = true;
        }
    }
    if (probe->Calls == 1u) answer = "durable-answer-one";
    else if (probe->Calls == 2u) answer = "durable-answer-two";
    else if (probe->Calls == 3u) answer = "durable-answer-three";
    else if (probe->Calls == 4u) answer = "durable-answer-four";
    else if (probe->Calls == 5u) answer = "durable-answer-five";
    else if (probe->Calls == 6u) answer = "durable-answer-six";
    *response = Response(answer);
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
            printf("catalog_item=id:%s project:%s title:%s status:%d pinned:%d open:%d revision:%llu\n",
                info.Id, info.ProjectId, info.Title, (int)info.Status,
                info.Pinned ? 1 : 0, info.RuntimeOpen ? 1 : 0,
                (unsigned long long)info.Revision);
    }
    MdoSessionCatalogRelease(catalog);
}

static void Search(const char *label, const char *text, uint32 statuses,
    bool pinned) {
    xwork_error error;
    MdoSessionQuery query;
    MdoSessionCatalog *catalog;
    MdoSessionQueryInit(&query);
    query.ProjectId = "project-alpha";
    query.Text = text;
    query.StatusFlags = statuses;
    query.PinnedOnly = pinned;
    catalog = MdoSessionCatalogSearch(&query, &error);
    printf("%s=count:%zu diagnostics:%zu code:%d\n", label,
        MdoSessionCatalogCount(catalog),
        MdoSessionCatalogDiagnosticCount(catalog), (int)error.eCode);
    MdoSessionCatalogRelease(catalog);
}

static uint64 Events(const char *label, const char *project,
    const char *session, uint64 after, size_t limit) {
    xwork_error error;
    MdoSessionEventSnapshot *snapshot = MdoSessionEventReplay(project,
        session, after, limit, &error);
    MdoSessionEventInfo info;
    MdoSessionEventInfo legacy;
    size_t legacy_size = offsetof(MdoSessionEventInfo, ModelId);
    size_t byte;
    bool legacy_intact = true;
    uint64 cursor = MdoSessionEventSnapshotNextCursor(snapshot);
    printf("%s=count:%zu next:%llu latest:%llu lost:%d\n", label,
        MdoSessionEventSnapshotCount(snapshot),
        (unsigned long long)cursor,
        (unsigned long long)MdoSessionEventSnapshotLatestId(snapshot),
        MdoSessionEventSnapshotHistoryLost(snapshot) ? 1 : 0);
    memset(&info, 0, sizeof(info)); info.Size = sizeof(info);
    if (MdoSessionEventSnapshotAt(snapshot, 0u, &info))
        printf("event_item=id:%llu source:%llu kind:%d run:%llu text:%s\n",
            (unsigned long long)info.EventId,
            (unsigned long long)info.SourceEventId, (int)info.Kind,
            (unsigned long long)info.RunId,
            info.Text != NULL ? info.Text : "");
    memset(&legacy, 0xa5, sizeof(legacy));
    legacy.Size = (uint32)legacy_size;
    if (MdoSessionEventSnapshotCount(snapshot) != 0u) {
        legacy_intact = MdoSessionEventSnapshotAt(snapshot, 0u, &legacy);
        for (byte = legacy_size; byte < sizeof(legacy); ++byte)
            legacy_intact = legacy_intact &&
                ((unsigned char*)&legacy)[byte] == 0xa5u;
        printf("legacy_event_size=%d\n", legacy_intact ? 1 : 0);
    }
    MdoSessionEventSnapshotRelease(snapshot);
    return cursor;
}

static uint64 UserIdentity(const char *project, const char *session,
    const char *text, bool event_id) {
    xwork_error error;
    MdoSessionEventSnapshot *snapshot = MdoSessionEventReplay(project,
        session, 0u, 1000u, &error);
    uint64 found = 0u;
    size_t index;
    if (snapshot == NULL) return 0u;
    for (index = 0u; index < MdoSessionEventSnapshotCount(snapshot);
         ++index) {
        MdoSessionEventInfo info;
        memset(&info, 0, sizeof(info)); info.Size = sizeof(info);
        if (MdoSessionEventSnapshotAt(snapshot, index, &info) &&
            info.Kind == XWORK_EVENT_AGENT_START &&
            info.AgentDepth == 0u && info.Text != NULL &&
            strcmp(info.Text, text) == 0) {
            found = event_id ? info.EventId : info.UserMessageSequence;
            break;
        }
    }
    MdoSessionEventSnapshotRelease(snapshot);
    return found;
}

static uint64 UserSequence(const char *project, const char *session, const char *text) {
    return UserIdentity(project, session, text, false);
}

static bool CorruptEventTail(const char *project, const char *session) {
    char path[MDO_SESSION_PATH_CAPACITY];
    xfile file;
    bool ok;
    if (snprintf(path, sizeof(path), "sessions/%s/%s/ui-events.jsonl",
            project, session) <= 0) return false;
    file = MdoHomeOpenWrite(path, XFILE_CREATE | XFILE_APPEND | XFILE_SYNC);
    if (file == NULL) return false;
    ok = xrtWriteFull(file, "{", 1u, NULL) && xrtFlush(file);
    if (!xrtClose(file)) ok = false;
    return ok;
}

static bool AppendLegacySchema2Event(const char *project, const char *session) {
    char path[MDO_SESSION_PATH_CAPACITY];
    char json[2048];
    xwork_error error;
    MdoSessionEventSnapshot *snapshot = MdoSessionEventReplay(project,
        session, 0u, 1u, &error);
    xfile file;
    uint64 latest;
    int written;
    bool ok;
    if (snapshot == NULL) return false;
    latest = MdoSessionEventSnapshotLatestId(snapshot);
    MdoSessionEventSnapshotRelease(snapshot);
    written = snprintf(json, sizeof(json),
        "{\"schema_version\":2,\"event_id\":%llu,\"source_event_id\":0,"
        "\"occurred_at_us\":1,\"project_id\":\"%s\",\"session_id\":\"%s\","
        "\"kind\":%u,\"agent_turn\":1,\"agent_depth\":0,\"agent_id\":0,"
        "\"run_id\":0,\"task_id\":0,\"artifact_id\":0,\"parent_run_id\":0,"
        "\"effects\":0,\"task_state\":0,\"task_revision\":0,"
        "\"input_tokens\":0,\"output_tokens\":0,\"total_tokens\":0,"
        "\"success\":true,\"effect_applied\":false,\"text_truncated\":false,"
        "\"text\":\"legacy message\",\"tool_name\":\"\",\"tool_call_id\":\"\","
        "\"artifact_path\":\"\",\"model\":\"\"}\n",
        (unsigned long long)(latest + 1u), project, session,
        (unsigned)XWORK_EVENT_AGENT_START);
    if (written <= 0 || (size_t)written >= sizeof(json) ||
        snprintf(path, sizeof(path), "sessions/%s/%s/ui-events.jsonl",
            project, session) <= 0) return false;
    file = MdoHomeOpenWrite(path, XFILE_CREATE | XFILE_APPEND | XFILE_SYNC);
    if (file == NULL) return false;
    ok = xrtWriteFull(file, json, (size_t)written, NULL);
    written = snprintf(json, sizeof(json),
        "{\"schema_version\":3,\"event_id\":%llu,\"source_event_id\":0,"
        "\"occurred_at_us\":1,\"project_id\":\"%s\",\"session_id\":\"%s\","
        "\"kind\":%u,\"agent_turn\":1,\"user_message_sequence\":0,"
        "\"agent_depth\":0,\"agent_id\":0,\"run_id\":0,\"task_id\":0,"
        "\"artifact_id\":0,\"parent_run_id\":0,\"effects\":0,"
        "\"task_state\":0,\"task_revision\":0,\"input_tokens\":0,"
        "\"output_tokens\":0,\"total_tokens\":0,\"success\":true,"
        "\"effect_applied\":false,\"text_truncated\":false,"
        "\"text\":\"legacy v3 message\",\"tool_name\":\"\","
        "\"tool_call_id\":\"\",\"artifact_path\":\"\",\"model\":\"\"}\n",
        (unsigned long long)(latest + 2u), project, session,
        (unsigned)XWORK_EVENT_AGENT_START);
    ok = ok && written > 0 && (size_t)written < sizeof(json) &&
        xrtWriteFull(file, json, (size_t)written, NULL) && xrtFlush(file);
    if (!xrtClose(file)) ok = false;
    return ok;
}

static bool LegacyMeta(void) {
    static const char json[] =
        "{\"schema_version\":1,\"revision\":4,\"id\":\"legacy-session\","
        "\"project_id\":\"legacy-project\",\"title\":\"Legacy\","
        "\"agent_id\":\"mdo.default\",\"model_id\":\"ling.tiny\","
        "\"protocol\":\"openai-chat-completions\","
        "\"reasoning_effort\":\"medium\",\"max_output_tokens\":1024,"
        "\"workspace_root\":\".\",\"created_at_us\":1,"
        "\"updated_at_us\":2,\"status\":\"active\","
        "\"previous_status\":\"active\",\"pinned\":false,"
        "\"config_revision\":1,\"model_generation\":1,"
        "\"module_generation\":1,\"skill_generation\":1}";
    MdoSessionInfo info;
    bool ok = MdoSessionsInternalMetaParse("legacy-project", "legacy-session",
        xrtStrView(json), &info);
    printf("legacy_meta=ok:%d parent:%s through:%llu\n", ok ? 1 : 0,
        ok ? info.ParentSessionId : "invalid",
        (unsigned long long)(ok ? info.ForkedThroughSequence : UINT64_MAX));
    return ok;
}

void ServiceInit(XS_HostInfo *host) {
    xwork_runtime_config runtime_config;
    xwork_runtime *runtime = NULL;
    xwork_error error;
    MdoSessionCreateOptions create;
    MdoSessionRuntimeOptions open;
    MdoSessionForkOptions fork_options;
    MdoSessionInfo info;
    MdoSession *session = NULL;
    MdoSession *forked = NULL;
    MdoSession *blocked = NULL;
    MdoSession *stale = NULL;
    MdoProjectLease *exclusive = NULL;
    MdoAgentSession *retained_session = NULL;
    bool blocked_directory = true;
    bool stale_update;
    uint64 event_cursor = 0u;
    uint64 rewind_to = 0u;
    uint64 transient_tail = 0u;
    uint64 transient_source = 0u;
    uint64 transient_sequence = 0u;
    uint64 cleared_tail = 0u;
    xwork_event todo_event;
    xvalue *todo_value = NULL;
    char *todo_json = NULL;
    size_t todo_size = 0u;
    char *export_json = NULL;
    size_t export_size = 0u;
    Probe probe;
    Probe fork_probe;
    char session_id[MDO_SESSION_ID_CAPACITY] = {0};
    static const char invalid[] = "{}";
    (void)host;
    memset(&probe, 0, sizeof(probe));
    memset(&fork_probe, 0, sizeof(fork_probe));
    if (!MdoHomeInit() || !MdoProjectLifecycleInit() ||
        !LifecycleProbe() || !MdoConfigInit() || !MdoModelManagerInit() ||
        !MdoSkillManagerInit()) { printf("init_error=pre-runtime\n"); goto done; }
    if (!LegacyMeta()) goto done;
    xworkRuntimeConfigInit(&runtime_config);
    runtime = xworkRuntimeCreate(&runtime_config, &error);
    if (runtime == NULL || !MdoMemoryManagerInit(runtime) ||
        !MdoModuleManagerInit(runtime) ||
        !MdoSessionManagerInit(runtime)) {
        printf("init_error=runtime message:%s\n", error.sMessage); goto done;
    }
    Catalog("catalog_empty");
    if (!CaptureDataGateProbe()) goto done;
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
    memset(&todo_event, 0, sizeof(todo_event));
    todo_event.eKind = XWORK_EVENT_TOOL_DONE;
    todo_event.bSuccess = true;
    todo_event.sToolName = "mdo.todo";
    todo_event.sText = "{\"items\":[{\"text\":\"Inspect code\",\"done\":false}]}";
    todo_event.iTextLength = strlen(todo_event.sText);
    if (!MdoSessionTodoProject("project-alpha", session_id, 777u,
            &todo_event) ||
        !MdoSessionTodoLoad("project-alpha", session_id, &todo_value))
        goto done;
    todo_json = xrtJsonStringify(todo_value, false, &todo_size);
    printf("todo_snapshot=%s\n", todo_json != NULL ? todo_json : "missing");
    xrtFree(todo_json); todo_json = NULL;
    xrtValueRelease(todo_value); todo_value = NULL;
    todo_event.sText = "{\"items\":[{\"text\":\"\",\"done\":false}]}";
    todo_event.iTextLength = strlen(todo_event.sText);
    printf("todo_invalid=%d\n", MdoSessionTodoProject(
        "project-alpha", session_id, 778u, &todo_event) ? 1 : 0);
    MdoSessionRuntimeOptionsInit(&open);
    open.OnModelComplete = Complete;
    open.ModelUserData = &probe;
    blocked = MdoSessionOpen("project-alpha", session_id, &open, &error);
    printf("duplicate_open=%d code:%d\n", blocked != NULL ? 1 : 0,
        (int)error.eCode);
    MdoSessionRelease(blocked); blocked = NULL;
    Catalog("catalog_active");
    if (!Run(session, "first durable prompt")) goto done;
    stale_update = MdoSessionSetProfile(session, NULL, "low",
        "read-only", &open, &error);
    printf("profile_busy=%d code:%d\n", stale_update ? 1 : 0,
        (int)error.eCode);
    if (stale_update) goto done;
    event_cursor = Events("events_first", "project-alpha", session_id,
        0u, 2u);
    retained_session = MdoSessionAgentRef(session);
    if (retained_session == NULL) goto done;
    MdoSessionRelease(session); session = NULL;
    printf("lease_callback_owner=%d\n", LeaseBlocked("project-alpha",
        MDO_PROJECT_LEASE_EXCLUSIVE) ? 1 : 0);
    MdoAgentSessionRelease(retained_session); retained_session = NULL;
    exclusive = MdoProjectLeaseAcquire("project-alpha",
        MDO_PROJECT_LEASE_EXCLUSIVE, &error);
    if (exclusive == NULL) goto done;
    create.RequestedId = "blocked-by-project-lease";
    blocked = MdoSessionCreate(&create, &error);
    printf("lease_create_blocked=%d\n", blocked == NULL &&
        error.eCode == XWORK_ERROR_CONTEXT ? 1 : 0);
    MdoSessionRelease(blocked); blocked = NULL;
    blocked = MdoSessionOpen("project-alpha", session_id, &open, &error);
    printf("lease_open_blocked=%d\n", blocked == NULL &&
        error.eCode == XWORK_ERROR_CONTEXT ? 1 : 0);
    MdoSessionRelease(blocked); blocked = NULL;
    blocked = MdoSessionLoad("project-alpha", session_id, &error);
    printf("lease_load_blocked=%d\n", blocked == NULL &&
        error.eCode == XWORK_ERROR_CONTEXT ? 1 : 0);
    MdoSessionRelease(blocked); blocked = NULL;
    if (!MdoHomeExternalStat("sessions/project-alpha/blocked-by-project-lease",
            &blocked_directory, NULL) || blocked_directory) goto done;
    MdoProjectLeaseRelease(exclusive); exclusive = NULL;
    create.RequestedId = NULL;
    session = MdoSessionLoad("project-alpha", session_id, &error);
    printf("lease_loaded_handle=%d\n", LeaseBlocked("project-alpha",
        MDO_PROJECT_LEASE_EXCLUSIVE) ? 1 : 0);
    export_size = 123u;
    export_json = MdoSessionExportJson(session, &export_size, &error);
    printf("export_unopened=failed:%d size:%zu context:%d\n",
        export_json == NULL, export_size, error.eCode == XWORK_ERROR_CONTEXT);
    xrtFree(export_json); export_json = NULL;
    if (session == NULL || !MdoSessionSetProfile(session, NULL, "low",
            "read-only", &open, &error)) {
        printf("profile_error=%s\n", error.sMessage); goto done;
    }
    memset(&info, 0, sizeof(info)); info.Size = sizeof(info);
    if (!MdoSessionGetInfo(session, &info)) goto done;
    printf("profile=model:%s reasoning:%s permission:%s revision:%llu\n",
        info.ModelId, info.ReasoningEffort, info.PermissionProfile,
        (unsigned long long)info.Revision);
    MdoSessionRelease(session); session = NULL;
    Catalog("catalog_after_create");
    if (!AppendLegacySchema2Event("project-alpha", session_id)) goto done;
    if (!CorruptEventTail("project-alpha", session_id)) goto done;
    (void)Events("events_during_append", "project-alpha", session_id,
        event_cursor, 100u);

    session = MdoSessionOpen("project-alpha", session_id, &open, &error);
    if (session == NULL) { printf("recover_error=%s\n", error.sMessage); goto done; }
    if (!Run(session, "second prompt")) goto done;
    (void)Events("events_after_reopen", "project-alpha", session_id,
        event_cursor, 100u);
    if (!MdoSessionLastSequence(session, &rewind_to, &error)) goto done;
    export_json = MdoSessionExportJson(session, &export_size, &error);
    printf("export=ok:%d size:%zu schema:%d meta:%d snapshot:%d\n",
        export_json != NULL ? 1 : 0, export_size,
        export_json != NULL && strstr(export_json, "\"export_schema\":1") != NULL,
        export_json != NULL && strstr(export_json, "\"meta\":{") != NULL,
        export_json != NULL && strstr(export_json, "\"snapshot\":{") != NULL);
    xrtFree(export_json); export_json = NULL;
    if (!CaptureSessionBoundaryProbe(session)) goto done;
    if (!SessionBackupProbe(session)) goto done;
    {
        char meta_path[MDO_SESSION_PATH_CAPACITY];
        bool failed, released;
        memcpy(meta_path, session->MetaPath, sizeof(meta_path));
        snprintf(session->MetaPath, sizeof(session->MetaPath), "%s",
            "sessions/project-alpha/missing-export-meta.json");
        export_size = 123u;
        export_json = MdoSessionExportJson(session, &export_size, &error);
        failed = export_json == NULL && export_size == 0u &&
            error.eCode == XWORK_ERROR_IO;
        xrtFree(export_json); export_json = NULL;
        memcpy(session->MetaPath, meta_path, sizeof(meta_path));
        released = MdoSessionLastSequence(session, &rewind_to, &error);
        export_json = MdoSessionExportJson(session, &export_size, &error);
        printf("export_read_failure=failed:%d released:%d retry:%d\n",
            failed, released, export_json != NULL && export_size > 0u);
        xrtFree(export_json); export_json = NULL;
    }
    stale = MdoSessionLoad("project-alpha", session_id, &error);
    if (stale == NULL || !MdoSessionRename(stale, "Export revision probe", &error))
        goto done;
    export_size = 123u;
    export_json = MdoSessionExportJson(session, &export_size, &error);
    printf("export_stale=failed:%d size:%zu context:%d\n",
        export_json == NULL, export_size, error.eCode == XWORK_ERROR_CONTEXT);
    xrtFree(export_json); export_json = NULL;
    MdoSessionRelease(stale); stale = NULL;
    MdoSessionRelease(session); session = NULL;
    session = MdoSessionOpen("project-alpha", session_id, &open, &error);
    if (session == NULL) goto done;
    MdoSessionForkOptionsInit(&fork_options);
    fork_options.Title = "Forked durable session";
    fork_options.ThroughSequence = rewind_to;
    fork_options.Runtime.OnModelComplete = Complete;
    fork_options.Runtime.ModelUserData = &fork_probe;
    forked = MdoSessionFork(session, &fork_options, &error);
    if (forked == NULL) {
        printf("fork_error=%s\n", error.sMessage); goto done;
    }
    memset(&info, 0, sizeof(info)); info.Size = sizeof(info);
    if (!MdoSessionGetInfo(forked, &info) || !Run(forked, "fork prompt"))
        goto done;
    printf("fork=id:%s parent:%s through:%llu title:%s calls:%u history:%d\n",
        info.Id, info.ParentSessionId,
        (unsigned long long)info.ForkedThroughSequence, info.Title,
        fork_probe.Calls,
        fork_probe.SawPriorPrompt && fork_probe.SawPriorAnswer ? 1 : 0);
    MdoSessionRelease(forked); forked = NULL;
    {
        uint64 second_sequence = UserSequence("project-alpha", session_id,
            "second prompt");
        if (second_sequence < 2u) goto done;
        fork_options.Title = "Before second prompt";
        fork_options.ThroughSequence = second_sequence - 1u;
        forked = MdoSessionFork(session, &fork_options, &error);
        if (forked == NULL) {
            printf("prefix_fork_error=%s\n", error.sMessage); goto done;
        }
        memset(&info, 0, sizeof(info)); info.Size = sizeof(info);
        if (!MdoSessionGetInfo(forked, &info) ||
            !Run(forked, "edited second prompt")) goto done;
        printf("prefix_fork=id:%s through:%llu\n", info.Id,
            (unsigned long long)info.ForkedThroughSequence);
        MdoSessionRelease(forked); forked = NULL;
    }
    fork_options.ThroughSequence = rewind_to + UINT64_C(1000000);
    blocked = MdoSessionFork(session, &fork_options, &error);
    printf("invalid_fork=%d code:%d\n", blocked != NULL ? 1 : 0,
        (int)error.eCode);
    MdoSessionRelease(blocked); blocked = NULL;
    if (!Run(session, "third transient prompt") ||
        !MdoSessionLastSequence(session, &transient_tail, &error) ||
        transient_tail <= rewind_to) goto done;
    transient_source = UserIdentity("project-alpha", session_id, "third transient prompt", true);
    transient_sequence = UserSequence("project-alpha", session_id, "third transient prompt");
    if (transient_source == 0u || MdoSessionTruncateMessage(session, rewind_to,
            transient_source, &error) != MDO_SESSION_MESSAGE_OK) goto done;
    printf("visible_truncate=removed:%d\n",
        UserSequence("project-alpha", session_id,
            "third transient prompt") == 0u ? 1 : 0);
    MdoSessionRelease(session); session = NULL;
    session = MdoSessionOpen("project-alpha", session_id, &open, &error);
    if (session == NULL) goto done;
    probe.CheckTruncated = true;
    if (!Run(session, "after truncation prompt")) goto done;
    probe.CheckTruncated = false;
    printf("truncate=boundary:%llu tail:%llu removed:%d\n",
        (unsigned long long)rewind_to,
        (unsigned long long)transient_tail,
        probe.SawTruncatedPrompt ? 0 : 1);
    {
        uint64 before, after;
        MdoSessionMessageMutationResult result;
        if (!MdoSessionLastSequence(session, &before, &error)) goto done;
        result = MdoSessionTruncateMessage(session, transient_sequence - 1u,
            transient_source, &error);
        printf("message_guard=reused:%d changed:%d\n",
            UserSequence("project-alpha", session_id, "after truncation prompt") == transient_sequence,
            result == MDO_SESSION_MESSAGE_CHANGED && error.eCode == XWORK_ERROR_CONTEXT);
        if (!MdoSessionLastSequence(session, &after, &error)) goto done;
        printf("message_guard_retained=%d\n", before == after &&
            UserSequence("project-alpha", session_id, "after truncation prompt") == transient_sequence);
    }
    if (!MdoSessionClear(session, &error) ||
        !MdoSessionLastSequence(session, &cleared_tail, &error)) goto done;
    printf("visible_clear=removed:%d\n",
        UserSequence("project-alpha", session_id,
            "after truncation prompt") == 0u ? 1 : 0);
    probe.CheckCleared = true;
    if (!Run(session, "after clear prompt")) goto done;
    probe.CheckCleared = false;
    printf("clear=tail:%llu old:%d system:%d\n",
        (unsigned long long)cleared_tail,
        probe.SawOldAfterClear ? 1 : 0,
        probe.SawSystemAfterClear ? 1 : 0);
    MdoSessionRelease(session); session = NULL;
    session = MdoSessionOpen("project-alpha", session_id, &open, &error);
    if (session == NULL) goto done;
    probe.CheckClearRecovery = true;
    if (!Run(session, "after clear recovery prompt")) goto done;
    probe.CheckClearRecovery = false;
    printf("clear_recovery=prompt:%d answer:%d\n",
        probe.SawClearPrompt ? 1 : 0, probe.SawClearAnswer ? 1 : 0);
    stale = MdoSessionLoad("project-alpha", session_id, &error);
    if (stale == NULL) goto done;
    if (!MdoSessionRename(session, "Renamed durable session", &error) ||
        !MdoSessionSetPinned(session, true, &error)) goto done;
    Search("search_active", "RENAMED durable",
        MDO_SESSION_STATUS_ACTIVE_FLAG, true);
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
    Search("search_trash", "renamed",
        MDO_SESSION_STATUS_TRASH_FLAG, false);
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
    exclusive = MdoProjectLeaseAcquire("project-alpha",
        MDO_PROJECT_LEASE_EXCLUSIVE, &error);
    printf("lease_failed_create_cleanup=%d\n", exclusive != NULL ? 1 : 0);
    MdoProjectLeaseRelease(exclusive); exclusive = NULL;
    Catalog("catalog_after_failed_create");

    if (!MdoHomeAtomicWrite("sessions/project-alpha/bad/meta.json",
            invalid, sizeof(invalid) - 1u, false)) goto done;
    Catalog("catalog_diagnostic");
    printf("recovery=calls:%u prior_prompt:%d prior_answer:%d\n",
        probe.Calls, probe.SawPriorPrompt ? 1 : 0,
        probe.SawPriorAnswer ? 1 : 0);
    printf("probe_done=1\n");
done:
    MdoAgentSessionRelease(retained_session);
    MdoProjectLeaseRelease(exclusive);
    MdoSessionRelease(stale);
    MdoSessionRelease(blocked);
    MdoSessionRelease(forked);
    MdoSessionRelease(session);
    xrtFree(export_json);
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
        "web",
        "default-home/config",
        "default-home/modules/tools",
        "default-home/modules/agents",
        "default-home/modules/subagents",
        "default-home/skills/project-explorer/templates",
        "generated/module-sdk/mdo",
        "src/storage",
        "src/projects",
        "src/config",
        "src/security",
        "src/models",
        "src/skills",
        "src/memory",
        "src/modules",
        "src/agents",
        "src/asks",
        "src/sessions", "src/sessions/sidecars",
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
        "src/storage/home.c", "src/storage/home_import.inc.c", "src/storage/home_purge.inc.c",
        "src/projects/lifecycle.c",
        "src/config/config.c",
        "src/security/secrets.c",
        "src/models/catalog.c",
        "src/skills/manager.c",
        "src/memory/manager.c",
        "src/modules/manager.c",
        "src/agents/runtime.c",
        "src/asks/manager.c",
        "src/sessions/data_gate.c", "src/sessions/data_gate.h",
        "src/sessions/events.c",
        "src/sessions/todo.c", "src/sessions/attachments.c",
        "src/sessions/sidecars/binding.c", "src/sessions/sidecars/binding.h",
        "src/sessions/internal.h",
        "src/sessions/manager.c",
        "src/sessions/backup.c",
        "src/sessions/backup_snapshot.c",
        "src/sessions/backup_internal.h",
    ):
        shutil.copy2(ROOT / "app" / relative, site / relative)
    shutil.copy2(ROOT / "tests/fixtures/session-capture.c", site / "session-capture.c")
    shutil.copy2(ROOT / "tests/fixtures/session-backup.c", site / "session-backup.c")
    for header in (ROOT / "app/include/mdo").glob("*.h"):
        shutil.copy2(header, site / "include/mdo" / header.name)
    shutil.copy2(
        ROOT / "app/src/memory/internal.h", site / "src/memory/internal.h"
    )
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
        for label in ("lease_primitives", "lease_callback_owner",
                      "lease_create_blocked", "lease_open_blocked",
                      "lease_load_blocked", "lease_loaded_handle",
                      "lease_failed_create_cleanup"):
            assert f"{label}=1" in output, output
        assert "recover_error=" not in output, output
        assert "fork_error=" not in output, output
        assert "catalog_empty=count:0 diagnostics:0 generation:1" in output, output
        assert "legacy_meta=ok:1 parent: through:0" in output, output
        assert "created=id:" in output and "project:project-alpha" in output, output
        assert "duplicate_open=0 code:7" in output, output
        assert "profile_busy=0 code:7" in output, output
        assert re.search(r"profile=model:ling-3\.0-tiny reasoning:low "
            r"permission:read-only revision:[2-9]\d*", output), output
        assert "run=0 text:durable-answer-one" in output, output
        assert "run=0 text:durable-answer-two" in output, output
        assert "run=0 text:durable-answer-three" in output, output
        assert "run=0 text:durable-answer-four" in output, output
        assert "run=0 text:durable-answer-five" in output, output
        assert "run=0 text:durable-answer-six" in output, output
        assert re.search(r"export=ok:1 size:[1-9]\d* schema:1 meta:1 snapshot:1", output), output
        assert "export_unopened=failed:1 size:0 context:1" in output, output
        assert "export_read_failure=failed:1 released:1 retry:1" in output, output
        assert "capture_data_gate=1" in output, output
        assert "capture_boundary=writers:1 other:1 locks:1 calls:1" in output, output
        assert "capture_failure=preserved:1 released:1 retry:1" in output, output
        assert "backup_format=immutable:1 bounded:1 missing:1 unknown:1 deadline:1 partial:1 retry:1" in output, output
        assert "backup_format_error=" not in output, output
        if os.name != "nt":
            assert "backup_link=rejected" in output, output
        backup = json.loads((home / "data/session-backup-probe.json").read_text(encoding="utf-8"))
        assert (home / "data/session-backup-probe.json").read_bytes() == (
            home / "data/session-backup-after-write.json").read_bytes()
        assert backup["format"] == "mdo-session-backup" and backup["export_schema"] == 2
        assert backup["restore_ready"] is False
        assert backup["validation"] == "json-syntax-and-resource-references"
        assert backup["queue_restore_policy"] == "require-user-confirmation"
        assert backup["history_retention"] == "earlier-content-may-have-been-pruned"
        assert backup["project_id"] == backup["session_id"] == "backup-fixture"
        restored_files = {}
        ordered_paths = []
        for item in backup["files"]:
            path = item["path"]
            assert path not in restored_files and not path.startswith(("/", "\\"))
            assert ".." not in path.split("/") and "\\" not in path and ":" not in path
            assert item["encoding"] == "base64"
            data = base64.b64decode(item["data"], validate=True)
            assert item["bytes"] == len(data)
            assert item["sha256"] == hashlib.sha256(data).hexdigest()
            restored_files[path] = data
            ordered_paths.append(path)
        assert ordered_paths == sorted(ordered_paths)
        assert backup["file_count"] == len(restored_files)
        assert backup["total_bytes"] == sum(map(len, restored_files.values()))
        assert all(not path.endswith((".bak", ".tmp")) and path != ".runtime.lock"
                   for path in restored_files)
        assert not set(backup["absent_files"]) & set(restored_files)
        assert "journal.jsonl" in backup["absent_files"] or "journal.jsonl" in restored_files
        image_id = "a" * 32
        assert restored_files[f"attachments/{image_id}.bin"] == b"\x89PNG\x00\xff\x01\x02"
        image_meta = json.loads(restored_files[f"attachments/{image_id}.json"])
        assert image_meta["file_name"] == "original 图片.png" and image_meta["size"] == 8
        assert json.loads(restored_files["draft.json"])["text"] == "draft"
        assert json.loads(restored_files["queue.json"])["items"][0]["state"] == "pending"
        assert json.loads(restored_files["todo.json"])["items"][0]["text"] == "Saved task"
        assert json.loads(restored_files["feedback.json"])["items"][0]["vote"] == 1
        assert "queue-receipts/" + "b" * 32 + ".json" in restored_files
        assert json.loads(restored_files["attachments/events/1.json"])["attachments"] == [image_id]
        assert json.loads(restored_files["attachments/runs/1.json"])["attachments"] == [image_id]
        artifact = "artifacts/run-00000000000000000001/00000000000000000001-probe.txt"
        assert restored_files[artifact] == b"full artifact\n"
        events = [json.loads(line) for line in restored_files["ui-events.jsonl"].splitlines()]
        assert backup["ui_first_event_id"] == events[0]["event_id"]
        assert backup["ui_last_event_id"] == events[-1]["event_id"]
        assert backup["ui_records"] == len(events)
        assert events[-1]["artifact_path"].replace("\\", "/").endswith(artifact)
        assert isinstance(json.loads(restored_files["snapshot.json"]), dict)
        assert json.loads(restored_files["meta.json"])["revision"] == backup["revision"]
        assert "export_stale=failed:1 size:0 context:1" in output, output
        assert "message_guard=reused:1 changed:1" in output, output
        assert "message_guard_retained=1" in output, output
        created = re.search(r"created=id:([^ ]+) project:project-alpha", output)
        fork = re.search(
            r"fork=id:([^ ]+) parent:([^ ]+) through:([1-9]\d*) "
            r"title:Forked durable session calls:1 history:1", output)
        assert created and fork and fork.group(1) != created.group(1), output
        assert fork.group(2) == created.group(1), output
        prefix_fork = re.search(r"prefix_fork=id:([^ ]+) through:([1-9]\d*)", output)
        assert prefix_fork, output
        assert re.search(r"invalid_fork=0 code:[1-9]\d*", output), output
        assert re.search(r"truncate=boundary:[1-9]\d* tail:[1-9]\d* removed:1", output), output
        assert "visible_truncate=removed:1" in output, output
        assert re.search(r"clear=tail:[1-9]\d* old:0 system:1", output), output
        assert "visible_clear=removed:1" in output, output
        assert "clear_recovery=prompt:1 answer:1" in output, output
        assert "catalog_active=count:1 diagnostics:0" in output, output
        assert "open:1" in output, output
        assert "catalog_after_create=count:1 diagnostics:0" in output, output
        assert "open:0" in output, output
        assert "archived_open=0" in output, output
        assert "stale_update=0 code:7" in output, output
        assert "search_active=count:1 diagnostics:0 code:0" in output, output
        assert "catalog_trash=count:3 diagnostics:0" in output, output
        assert "search_trash=count:1 diagnostics:0 code:0" in output, output
        assert "status:3 pinned:0" in output, output
        assert "failed_create=0 code:1" in output, output
        assert "catalog_after_failed_create=count:3 diagnostics:0" in output, output
        assert "catalog_diagnostic=count:3 diagnostics:1" in output, output
        assert "recovery=calls:6 prior_prompt:1 prior_answer:1" in output, output
        assert 'todo_snapshot={"items":[{"text":"Inspect code","done":false}],"schema_version":1,"event_id":777}' in output, output
        assert "todo_invalid=0" in output, output
        first = re.search(r"events_first=count:(\d+) next:(\d+) latest:(\d+) lost:0", output)
        during_append = re.search(r"events_during_append=count:(\d+) next:(\d+) latest:(\d+) lost:0", output)
        reopened = re.search(r"events_after_reopen=count:(\d+) next:(\d+) latest:(\d+) lost:1", output)
        assert first and int(first.group(1)) == 2 and int(first.group(3)) >= 2, output
        assert during_append and int(during_append.group(1)) > 0, output
        assert reopened and int(reopened.group(1)) > 0, output
        assert int(reopened.group(3)) > int(first.group(3)), output
        assert "probe_done=1" in output, output
        assert ("legacy_event_size=1" in output and
                "legacy_event_size=0" not in output), output
        meta_files = list(home.glob("sessions/project-alpha/*/meta.json"))
        todo_files = list(home.glob("sessions/project-alpha/*/todo.json"))
        assert len(todo_files) == 1, todo_files
        assert json.loads(todo_files[0].read_text(encoding="utf-8")) == {
            "schema_version": 1, "event_id": 0, "items": [],
        }, todo_files[0]
        valid_meta = [path for path in meta_files if path.parent.name != "bad"]
        assert len(valid_meta) == 3, meta_files
        documents = {
            path.parent.name: json.loads(path.read_text(encoding="utf-8"))
            for path in valid_meta
        }
        source_path = next(path for path in valid_meta
            if documents[path.parent.name]["parent_session_id"] == "")
        child_path = next(path for path in valid_meta
            if path.parent.name == fork.group(1))
        document = documents[source_path.parent.name]
        child = documents[child_path.parent.name]
        assert document["schema_version"] == 3
        assert document["permission_profile"] == "read-only"
        assert document["status"] == "active" and document["title"] == "Renamed durable session"
        assert child["schema_version"] == 3
        assert child["parent_session_id"] == source_path.parent.name
        assert child["forked_through_sequence"] > 0
        assert child["title"] == "Forked durable session"
        assert source_path.parent.name == created.group(1)
        assert child_path.parent.name == fork.group(1)
        assert all((path.parent / "snapshot.json").is_file() for path in valid_meta)
        event_path = source_path.parent / "ui-events.jsonl"
        full_fork_path = home / "sessions/project-alpha" / fork.group(1) / "ui-events.jsonl"
        prefix_fork_path = home / "sessions/project-alpha" / prefix_fork.group(1) / "ui-events.jsonl"
        assert full_fork_path.is_file() and prefix_fork_path.is_file()
        full_fork_events = [json.loads(line) for line in full_fork_path.read_text(
            encoding="utf-8").splitlines()]
        prefix_fork_events = [json.loads(line) for line in prefix_fork_path.read_text(
            encoding="utf-8").splitlines()]
        assert all(event["session_id"] == fork.group(1)
                   for event in full_fork_events)
        assert all(event["session_id"] == prefix_fork.group(1)
                   for event in prefix_fork_events)
        assert any(event["text"] == "first durable prompt"
                   for event in full_fork_events)
        assert any(event["text"] == "second prompt"
                   for event in full_fork_events)
        assert any(event["text"] == "fork prompt" for event in full_fork_events)
        assert any(event["text"] == "first durable prompt"
                   for event in prefix_fork_events)
        assert not any(event["text"] == "second prompt"
                       for event in prefix_fork_events)
        assert any(event["text"] == "edited second prompt"
                   for event in prefix_fork_events)
        full_fork_calls = {event["source_event_id"]: event
                           for event in full_fork_events if event["kind"] == 4}
        prefix_fork_calls = {event["source_event_id"]: event
                             for event in prefix_fork_events if event["kind"] == 4}
        assert full_fork_calls and prefix_fork_calls
        assert all(event["schema_version"] == 5 and event["model_id"] and
                   event["context_window_tokens"] > 0
                   for event in full_fork_calls.values())
        assert all((event["model_id"], event["context_window_tokens"]) ==
                   (full_fork_calls[source_id]["model_id"],
                    full_fork_calls[source_id]["context_window_tokens"])
                   for source_id, event in prefix_fork_calls.items()
                   if source_id in full_fork_calls)
        events = []
        invalid_events = 0
        for line in event_path.read_text(encoding="utf-8").splitlines():
            try:
                events.append(json.loads(line))
            except json.JSONDecodeError:
                invalid_events += 1
        event_ids = [event["event_id"] for event in events]
        assert events and event_ids == sorted(set(event_ids)), event_ids
        assert invalid_events == 0
        assert all(event["session_id"] == source_path.parent.name for event in events)
        model_events = [event for event in events if event.get("input_tokens")]
        assert model_events and all(event["schema_version"] == 5 and
                                    event["model_id"] and
                                    event["context_window_tokens"] > 0
                                    for event in model_events)
        assert any(event["user_message_sequence"] > 0 for event in events)
        assert not any(event["text"] in ("legacy message", "first durable prompt",
                                          "second prompt", "third transient prompt")
                       for event in events)
        assert any(event["kind"] == 16 for event in events)
        assert all((event["input_tokens"], event["output_tokens"],
                    event["total_tokens"]) == (40, 8, 48)
                   for event in model_events)
    print("session runtime probe: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
