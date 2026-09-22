"""Bounded create, recover, catalog, archive, and trash session probe."""

from __future__ import annotations

import argparse
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
#include "src/config/config.c"
#include "src/security/secrets.c"
#include "src/models/catalog.c"
#include "src/skills/manager.c"
#include "src/modules/manager.c"
#include "src/agents/runtime.c"
#include "src/sessions/events.c"
#include "src/sessions/manager.c"

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
    MdoSessionEventSnapshotRelease(snapshot);
    return cursor;
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
    uint64 event_cursor = 0u;
    uint64 rewind_to = 0u;
    uint64 transient_tail = 0u;
    uint64 cleared_tail = 0u;
    char *export_json = NULL;
    size_t export_size = 0u;
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
    MdoSessionRuntimeOptionsInit(&open);
    open.OnModelComplete = Complete;
    open.ModelUserData = &probe;
    blocked = MdoSessionOpen("project-alpha", session_id, &open, &error);
    printf("duplicate_open=%d code:%d\n", blocked != NULL ? 1 : 0,
        (int)error.eCode);
    MdoSessionRelease(blocked); blocked = NULL;
    Catalog("catalog_active");
    if (!Run(session, "first durable prompt")) goto done;
    event_cursor = Events("events_first", "project-alpha", session_id,
        0u, 2u);
    MdoSessionRelease(session); session = NULL;
    Catalog("catalog_after_create");
    if (!CorruptEventTail("project-alpha", session_id)) goto done;

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
    if (!Run(session, "third transient prompt") ||
        !MdoSessionLastSequence(session, &transient_tail, &error) ||
        transient_tail <= rewind_to ||
        !MdoSessionTruncateAfter(session, rewind_to, &error)) goto done;
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
    if (!MdoSessionClear(session, &error) ||
        !MdoSessionLastSequence(session, &cleared_tail, &error)) goto done;
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
    xrtFree(export_json);
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
        "src/sessions/events.c",
        "src/sessions/internal.h",
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
        assert "duplicate_open=0 code:7" in output, output
        assert "run=0 text:durable-answer-one" in output, output
        assert "run=0 text:durable-answer-two" in output, output
        assert "run=0 text:durable-answer-three" in output, output
        assert "run=0 text:durable-answer-four" in output, output
        assert "run=0 text:durable-answer-five" in output, output
        assert "run=0 text:durable-answer-six" in output, output
        assert re.search(r"export=ok:1 size:[1-9]\d* schema:1 meta:1 snapshot:1", output), output
        assert re.search(r"truncate=boundary:[1-9]\d* tail:[1-9]\d* removed:1", output), output
        assert re.search(r"clear=tail:[1-9]\d* old:0 system:1", output), output
        assert "clear_recovery=prompt:1 answer:1" in output, output
        assert "catalog_active=count:1 diagnostics:0" in output, output
        assert "open:1" in output, output
        assert "catalog_after_create=count:1 diagnostics:0" in output, output
        assert "open:0" in output, output
        assert "archived_open=0" in output, output
        assert "stale_update=0 code:7" in output, output
        assert "search_active=count:1 diagnostics:0 code:0" in output, output
        assert "catalog_trash=count:1 diagnostics:0" in output, output
        assert "search_trash=count:1 diagnostics:0 code:0" in output, output
        assert "status:3 pinned:0" in output, output
        assert "failed_create=0 code:1" in output, output
        assert "catalog_after_failed_create=count:1 diagnostics:0" in output, output
        assert "catalog_diagnostic=count:1 diagnostics:1" in output, output
        assert "recovery=calls:6 prior_prompt:1 prior_answer:1" in output, output
        first = re.search(r"events_first=count:(\d+) next:(\d+) latest:(\d+) lost:0", output)
        reopened = re.search(r"events_after_reopen=count:(\d+) next:(\d+) latest:(\d+) lost:1", output)
        assert first and int(first.group(1)) == 2 and int(first.group(3)) >= 2, output
        assert reopened and int(reopened.group(1)) > 0, output
        assert int(reopened.group(3)) > int(first.group(3)), output
        assert "probe_done=1" in output, output
        meta_files = list(home.glob("sessions/project-alpha/*/meta.json"))
        valid_meta = [path for path in meta_files if path.parent.name != "bad"]
        assert len(valid_meta) == 1, meta_files
        document = json.loads(valid_meta[0].read_text(encoding="utf-8"))
        assert document["status"] == "active" and document["title"] == "Renamed durable session"
        assert (valid_meta[0].parent / "snapshot.json").is_file()
        event_path = valid_meta[0].parent / "ui-events.jsonl"
        events = []
        invalid_events = 0
        for line in event_path.read_text(encoding="utf-8").splitlines():
            try:
                events.append(json.loads(line))
            except json.JSONDecodeError:
                invalid_events += 1
        assert events and [event["event_id"] for event in events] == list(
            range(1, len(events) + 1))
        assert invalid_events == 1
        assert all(event["session_id"] == valid_meta[0].parent.name for event in events)
    print("session runtime probe: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
