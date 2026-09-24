#!/usr/bin/env python3
"""Bounded MDO-8 HTTP probe through the real xs/TCC request path."""

from __future__ import annotations

import argparse
import base64
import http.client
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent

MCP_MOCK_SERVER = r'''import json
import sys

for line in sys.stdin:
    request = json.loads(line)
    method = request.get("method")
    if method == "server/discover":
        result = {
            "resultType": "complete",
            "supportedVersions": ["2026-07-28"],
            "capabilities": {"tools": {}},
            "ttlMs": 60000,
            "cacheScope": "private",
        }
    elif method == "tools/list":
        result = {
            "resultType": "complete",
            "tools": [{
                "name": "echo",
                "description": "Echo fixture text.",
                "inputSchema": {
                    "type": "object",
                    "properties": {"text": {"type": "string"}},
                    "required": ["text"],
                    "additionalProperties": False,
                },
            }],
            "ttlMs": 60000,
            "cacheScope": "private",
        }
    else:
        continue
    print(json.dumps({"jsonrpc": "2.0", "id": request.get("id"),
                      "result": result}, separators=(",", ":")), flush=True)
'''


class ModelHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    calls = 0
    saw_prompt = False
    last_payload: dict | None = None
    todo_sent = False
    ask_sent = False
    ask_cancel_sent = False
    ask_verify_sent = False

    def log_message(self, format: str, *args: object) -> None:
        del format, args

    def do_POST(self) -> None:  # noqa: N802 - stdlib callback name
        if self.path != "/v1/responses":
            self.send_error(404)
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if length <= 0 or length > 1024 * 1024:
                raise ValueError("invalid request length")
            payload = json.loads(self.rfile.read(length))
            ModelHandler.last_payload = payload
            ModelHandler.calls += 1
            ModelHandler.saw_prompt = "API interactive prompt" in json.dumps(payload)
            output = [{
                "type": "message",
                "content": [{
                    "type": "output_text",
                    "text": "API interactive result",
                }],
            }]
            if ModelHandler.calls == 2:
                output = [{
                    "type": "function_call",
                    "call_id": "recovery-verify-call",
                    "name": "exec",
                    "arguments": json.dumps({
                        "argv": [sys.executable, "-c",
                                 "print('recovery verified')"],
                        "timeout_ms": 5000,
                    }, separators=(",", ":")),
                }]
            if "TODO probe" in json.dumps(payload) and not ModelHandler.todo_sent:
                ModelHandler.todo_sent = True
                output = [{
                    "type": "function_call",
                    "call_id": "todo-probe-call",
                    "name": "mdo.todo",
                    "arguments": json.dumps({"items": [
                        {"text": "Inspect repository", "done": True},
                        {"text": "Verify result", "done": False},
                    ]}, separators=(",", ":")),
                }]
            if "ASK probe" in json.dumps(payload) and not ModelHandler.ask_sent:
                ModelHandler.ask_sent = True
                output = [{
                    "type": "function_call",
                    "call_id": "ask-probe-call",
                    "name": "ask_user",
                    "arguments": json.dumps({
                        "question": "Which route should I take?",
                        "options": ["Fast", "Careful"],
                    }, separators=(",", ":")),
                }]
            if "ASK cancel probe" in json.dumps(payload) and not ModelHandler.ask_cancel_sent:
                ModelHandler.ask_cancel_sent = True
                output = [{
                    "type": "function_call",
                    "call_id": "ask-cancel-call",
                    "name": "ask_user",
                    "arguments": '{"question":"Cancel this question?"}',
                }]
            if "Answer the pending question." in json.dumps(payload) and not ModelHandler.ask_verify_sent:
                ModelHandler.ask_verify_sent = True
                output = [{
                    "type": "function_call",
                    "call_id": "ask-recovery-verify-call",
                    "name": "exec",
                    "arguments": json.dumps({
                        "argv": [sys.executable, "-c",
                                 "print('ask recovery verified')"],
                        "timeout_ms": 5000,
                    }, separators=(",", ":")),
                }]
            response = json.dumps({
                "id": "resp_api_probe",
                "model": "ling-3.0-tiny",
                "status": "completed",
                "output": output,
                "usage": {
                    "input_tokens": 7,
                    "output_tokens": 3,
                    "total_tokens": 10,
                },
            }, separators=(",", ":")).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(response)))
            self.end_headers()
            self.wfile.write(response)
        except (ValueError, json.JSONDecodeError):
            self.send_error(400)


class ModelServer(ThreadingHTTPServer):
    def handle_error(self, request: object, client_address: object) -> None:
        del request, client_address


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.bind(("127.0.0.1", 0))
        return int(probe.getsockname()[1])


def write_site(base: Path, port: int) -> Path:
    # xs mounts its configured application root at /app.  Mirror the packed
    # application layout directly so /app/default-home has identical meaning
    # in the development and single-file paths.
    shutil.copytree(ROOT / "app", base, dirs_exist_ok=True)
    service_path = base / "src/bootstrap/service.c"
    service_text = service_path.read_text(encoding="utf-8")
    fixture = r'''
#include "../../include/mdo/approvals.h"
#include "../../include/mdo/home.h"
#include "../../include/mdo/sessions.h"
#include <xllm-session.h>

static xthread* g_MdoApiProbeApprovalThread;

static xwork_permission_decision MdoApiProbeRequestApproval(uint64 RequestId,
    const char* CallId, const char* ResourceText)
{
    xwork_permission_resource Resource;
    xwork_permission_request Request;
    memset(&Resource, 0, sizeof(Resource));
    Resource.eKind = XWORK_RESOURCE_PATH;
    Resource.uAccess = XWORK_RESOURCE_ACCESS_WRITE;
    Resource.sResource = ResourceText;
    memset(&Request, 0, sizeof(Request));
    Request.uRequestId = RequestId;
    Request.uAgentId = 41u;
    Request.uRunId = 73u;
    Request.uCatalogGeneration = 5u;
    Request.sToolName = "edit";
    Request.sToolCallId = CallId;
    Request.uEffects = XWORK_TOOL_EFFECT_WORKSPACE_WRITE;
    Request.eRisk = XWORK_RISK_MEDIUM;
    Request.pResources = &Resource;
    Request.iResourceCount = 1u;
    Request.sArgumentsJson = "{\"path\":\"notes.txt\"}";
    Request.sWorkspaceRoot = "api-probe-workspace";
    Request.uAgentTurn = 9u;
    Request.uDeadline = xrtDeadlineAfter(UINT64_C(120) * 1000000u);
    return MdoApprovalOnPermission(NULL, &Request);
}

static int32 MdoApiProbeApprovals(ptr Data)
{
    (void)Data;
    (void)MdoApiProbeRequestApproval(7001u, "call-allow", "notes.txt");
    (void)MdoApiProbeRequestApproval(7002u, "call-deny", "blocked.txt");
    return 0;
}

static void MdoApiProbeCreateTasks(void)
{
    xwork_runtime* Runtime = MdoBootstrapRuntime();
    xwork_scheduled_task_config Config;
    xwork_error Error;
    uint64 TaskId;
    if ( Runtime == NULL ) return;
    xworkScheduledTaskConfigInit(&Config);
    Config.sOwnerSession = "api-probe-session";
    Config.sLabel = "api-probe-finished";
    Config.sNotify = "none";
    Config.iScheduledAtUs = xrtNow();
    if ( xworkRuntimeCreateScheduledTask(Runtime, &Config, &TaskId, &Error) ) {
        (void)xworkRuntimeStartScheduledTask(Runtime, TaskId, &Error);
        (void)xworkRuntimeFinishScheduledTask(Runtime, TaskId, XWORK_RESULT_OK,
            "task-result", &Error);
    }
    Config.sLabel = "api-probe-cancellable";
    Config.iScheduledAtUs = xrtNow() + 60000000;
    (void)xworkRuntimeCreateScheduledTask(Runtime, &Config, &TaskId, &Error);
}

static void MdoApiProbeCreateRecoverySession(bool* Created,
    const char* ProjectId, const char* Title, const char* Prompt,
    char* ToolCallId, char* ToolName,
    char* ArgumentsJson)
{
    MdoSessionCreateOptions Options;
    MdoSessionInfo Info;
    MdoSession* Session;
    xllm_session* Ledger = NULL;
    xllm_response Response;
    xllm_tool_call ToolCall;
    xllm_session_config SessionConfig;
    xllm_error ModelError;
    xwork_error Error;
    uint64 Turn;
    char Relative[MDO_SESSION_PATH_CAPACITY];
    char* SnapshotPath = NULL;
    char* JournalPath = NULL;

    if ( *Created ) return;
    *Created = true;
    MdoSessionCreateOptionsInit(&Options);
    Options.ProjectId = ProjectId;
    Options.Title = Title;
    Options.Agent.AgentId = "mdo.default";
    Options.Agent.ModelId = "ling-3.0-tiny";
    Options.Agent.Protocol = MDO_MODEL_PROTOCOL_OPENAI_RESPONSES;
    Session = MdoSessionCreate(&Options, &Error);
    if ( Session == NULL ) return;
    memset(&Info, 0, sizeof(Info));
    Info.Size = sizeof(Info);
    if ( !MdoSessionGetInfo(Session, &Info) ) {
        MdoSessionRelease(Session);
        return;
    }
    MdoSessionRelease(Session);
    Session = NULL;
    if ( snprintf(Relative, sizeof(Relative), "sessions/%s/%s/snapshot.json",
            Info.ProjectId, Info.Id) <= 0 ) return;
    SnapshotPath = MdoHomeExternalPath(Relative);
    if ( snprintf(Relative, sizeof(Relative), "sessions/%s/%s/journal.jsonl",
            Info.ProjectId, Info.Id) <= 0 ) goto done;
    JournalPath = MdoHomeExternalPath(Relative);
    if ( SnapshotPath == NULL || JournalPath == NULL ) goto done;
    xllmSessionConfigInit(&SessionConfig);
    SessionConfig.eWindowMode = XLLM_WINDOW_SHARED_CONTEXT;
    SessionConfig.uContextWindowTokens = 131072u;
    SessionConfig.uMaxInputTokens = 131071u;
    SessionConfig.uMaxOutputTokens = 16384u;
    SessionConfig.uOutputReserveTokens = 8192u;
    SessionConfig.uSummaryMaxTokens = 4096u;
    Ledger = xllmSessionRecover(SnapshotPath, JournalPath, &SessionConfig,
        &ModelError);
    if ( Ledger == NULL ) goto done;
    Turn = xllmSessionBeginTurn(Ledger);
    if ( Turn == 0u || !xllmSessionAddText(Ledger, Turn, XLLM_ROLE_USER,
            Prompt, 0u) ||
         !xllmSessionBeginModelCall(Ledger, &ModelError) ) goto done;
    memset(&ToolCall, 0, sizeof(ToolCall));
    ToolCall.sId = ToolCallId;
    ToolCall.sName = ToolName;
    ToolCall.sArgumentsJson = ArgumentsJson;
    memset(&Response, 0, sizeof(Response));
    Response.eFinish = XLLM_FINISH_TOOL_CALLS;
    Response.pToolCalls = &ToolCall;
    Response.iToolCallCount = 1u;
    if ( !xllmSessionAddAssistantResponse(Ledger, Turn, &Response) ) goto done;
    (void)xllmSessionCheckpoint(Ledger, SnapshotPath, &ModelError);
done:
    xllmSessionDestroy(Ledger);
    xrtFree(SnapshotPath);
    xrtFree(JournalPath);
}

'''
    needle = "void ServiceInit(XS_HostInfo* pHost)\n{\n    (void)MdoBootstrapInit(pHost);\n"
    replacement = (
        fixture + "void ServiceInit(XS_HostInfo* pHost)\n{\n"
        "    if ( MdoBootstrapInit(pHost) ) {\n"
        "        MdoApiProbeCreateTasks();\n"
        "        g_MdoApiProbeApprovalThread = xrtThreadCreate(\n"
        "            MdoApiProbeApprovals, NULL, 0u);\n"
        "    }\n")
    if needle not in service_text:
        raise RuntimeError("API task fixture could not patch ServiceInit")
    service_text = service_text.replace(needle, replacement, 1)
    unit_needle = (
        "    MdoApiUnit();\n"
        "    MdoBootstrapUnit();\n"
        "}\n\nXS_RequestResult RequestProc")
    unit_replacement = (
        "    MdoApiUnit();\n"
        "    MdoBootstrapUnit();\n"
        "    if ( g_MdoApiProbeApprovalThread != NULL ) {\n"
        "        (void)xrtThreadWait(g_MdoApiProbeApprovalThread);\n"
        "        xrtThreadDestroy(g_MdoApiProbeApprovalThread);\n"
        "        g_MdoApiProbeApprovalThread = NULL;\n"
        "    }\n"
        "}\n\nXS_RequestResult RequestProc")
    if unit_needle not in service_text:
        raise RuntimeError("API approval fixture could not patch ServiceUnit")
    service_text = service_text.replace(unit_needle, unit_replacement, 1)
    request_needle = (
        "XS_RequestResult RequestProc(XS_HttpReq* pRequest)\n"
        "{\n"
        "    return MdoApiRequest(pRequest);\n"
        "}")
    request_replacement = (
        "XS_RequestResult RequestProc(XS_HttpReq* pRequest)\n"
        "{\n"
        "    static const char Marker[] = \"fixture=recovery\";\n"
        "    static bool CreatedEdit, CreatedAsk;\n"
        "    size_t Index;\n"
        "    if ( pRequest != NULL && pRequest->head != NULL ) {\n"
        "        xstrview Target = pRequest->head->Target;\n"
        "        for ( Index = 0u; Index + sizeof(Marker) - 1u <= Target.Size; ++Index ) {\n"
        "            if ( memcmp(Target.Data + Index, Marker, sizeof(Marker) - 1u) == 0 ) {\n"
        "                MdoApiProbeCreateRecoverySession(&CreatedEdit,\n"
        "                    \"recovery-probe\", \"Recovery probe\",\n"
        "                    \"Continue after checking the uncertain edit.\",\n"
        "                    \"recovery-edit-call\", \"edit\",\n"
        "                    \"{\\\"path\\\":\\\"recovery-probe.txt\\\",\\\"edits\\\":[{\\\"old_text\\\":\\\"before\\\",\\\"new_text\\\":\\\"after\\\"}]}\");\n"
        "                MdoApiProbeCreateRecoverySession(&CreatedAsk,\n"
        "                    \"ask-recovery-probe\", \"Ask recovery probe\",\n"
        "                    \"Answer the pending question.\",\n"
        "                    \"recovery-ask-call\", \"ask_user\",\n"
        "                    \"{\\\"question\\\":\\\"Should I continue?\\\",\\\"options\\\":[\\\"Yes\\\",\\\"No\\\"]}\");\n"
        "                break;\n"
        "            }\n"
        "        }\n"
        "    }\n"
        "    return MdoApiRequest(pRequest);\n"
        "}")
    if request_needle not in service_text:
        raise RuntimeError("API recovery fixture could not patch RequestProc")
    service_path.write_text(service_text.replace(
        request_needle, request_replacement, 1), encoding="utf-8", newline="\n")
    mock_server = base / "mcp_mock_server.py"
    mock_server.write_text(MCP_MOCK_SERVER, encoding="utf-8")
    mcp_document = {
        "schema_version": 1,
        "id": "api-mock",
        "name": "API mock tools",
        "description": "Local bounded API fixture.",
        "enabled": True,
        "transport": {
            "type": "stdio",
            "program": sys.executable,
            "arguments": [str(mock_server)],
            "working_directory": None,
            "inherit_environment": True,
            "environment": [],
        },
        "protocol_version": "2026-07-28",
        "startup_timeout_ms": 5000,
        "request_timeout_ms": 5000,
        "limits": {"message_bytes": 1048576, "tools": 16},
        "tools": {"allow": ["echo"], "deny": []},
        "security": {
            "default_effects": ["external-service"],
            "permission_profile": "balanced",
            "trust_read_only_annotations": False,
        },
        "auto_reconnect": True,
    }
    (base / "default-home/mcp/api-mock.json").write_text(
        json.dumps(mcp_document), encoding="utf-8")
    config = {
        "engine": {"workers": 1},
        "services": [{
            "enabled": True,
            "class": "http",
            "name": "mdo-api-probe",
            "ip": "127.0.0.1",
            "port": port,
            "host_default": {
                "enabled": True,
                "name": "mdo",
                "path": "web",
                "devlang": "c",
                "devfile": "generated/mdo_unity.c",
            },
        }],
    }
    config_path = base / "xs.json"
    config_path.write_text(json.dumps(config), encoding="utf-8")
    return config_path


def request(port: int, method: str, target: str, *,
            body: bytes | list[bytes] | None = None,
            headers: dict[str, str] | None = None,
            encode_chunked: bool = False) -> tuple[int, dict[str, str], bytes]:
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=4)
    try:
        connection.request(method, target, body=body, headers=headers or {},
                           encode_chunked=encode_chunked)
        response = connection.getresponse()
        headers = {name.lower(): value for name, value in response.getheaders()}
        return response.status, headers, response.read()
    finally:
        connection.close()


def session_events(port: int, session_path: str) -> list[dict]:
    items: list[dict] = []
    cursor = 0
    for _ in range(64):
        status, _, body = request(port, "GET",
            f"{session_path}/events?after={cursor}&limit=32")
        document = json.loads(body)
        assert status == 200, (status, document)
        data = document["data"]
        items.extend(data["items"])
        cursor = data["next_cursor"]
        if cursor >= data["latest_event_id"]:
            return items
    raise AssertionError("session event probe exceeded its bounded pages")


def wait_ready(port: int, process: subprocess.Popen[bytes]) -> None:
    deadline = time.monotonic() + 20.0
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"xs exited during API startup: {process.returncode}")
        try:
            status, _, body = request(port, "GET", "/api/v1/bootstrap")
            if status == 200 and json.loads(body)["data"]["ready"] is True:
                return
        except (OSError, json.JSONDecodeError, KeyError):
            pass
        time.sleep(0.05)
    raise RuntimeError("MDO API did not become ready")


def assert_common(headers: dict[str, str], document: dict) -> None:
    assert headers["content-type"] == "application/json; charset=utf-8", headers
    assert headers["cache-control"] == "no-store", headers
    assert headers["x-content-type-options"] == "nosniff", headers
    assert headers["referrer-policy"] == "no-referrer", headers
    request_id = headers["x-request-id"]
    assert re.fullmatch(r"mdo-[0-9a-f]{24}|mdo-[0-9a-f]{16}", request_id), request_id
    assert document["schema_version"] == 1, document
    assert document["request_id"] == request_id, document


def run_probe(host: Path) -> None:
    if not (ROOT / "app/generated/mdo_unity.c").is_file():
        raise RuntimeError("run tools/build_mdo.py --prepare-only before the API probe")
    (ROOT / ".build").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="api-runtime-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        port = free_port()
        config_path = write_site(base, port)
        home = base / "home"
        legacy = base / ".mdo"
        (legacy / "projects/api-legacy").mkdir(parents=True)
        (legacy / "projects/api-legacy/sessions").mkdir()
        (legacy / "memory").mkdir()
        (legacy / "schedules").mkdir()
        (legacy / "config.json").write_text(json.dumps({
            "models": [{
                "id": "legacy-model",
                "name": "Legacy model",
                "baseUrl": "https://example.invalid/v1",
                "model": "legacy-wire-model",
                "dialect": "responses",
                "reasoning": "medium",
                "apiKey": "legacy-migration-secret",
            }],
            "defaultModel": "legacy-model",
            "activeProject": "api-legacy",
            "settings": {"theme": "auto", "fontSize": "md",
                         "interactMode": "guide", "sound": True},
        }), encoding="utf-8")
        (legacy / "projects/api-legacy/project.json").write_text(json.dumps({
            "name": "API legacy project",
            "path": str(base / "workspace"),
            "defaultModel": "legacy-model",
        }), encoding="utf-8")
        (legacy / "projects/api-legacy/sessions/s123456789abc.meta.json").write_text(
            json.dumps({
                "id": "s123456789abc",
                "title": "Legacy API session",
                "model": "legacy-model",
                "project": "api-legacy",
                "contextWindow": 131072,
                "createdAt": 1700000000000,
                "updatedAt": 1700000001000,
                "turns": 0,
                "pinned": True,
                "userPrompt": "Preserve this legacy session prompt.",
            }), encoding="utf-8")
        (legacy / "memory/preference.md").write_text(
            "# Preference\n\nKeep migration explicit.\n", encoding="utf-8")
        legacy_schedule = legacy / "schedules/once.json"
        legacy_schedule.write_text(json.dumps({
            "id": "once",
            "title": "L" * 300,
            "prompt": "Review migration",
            "kind": "once",
        }), encoding="utf-8")
        log_path = base / "xs.log"
        model_server = ModelServer(("127.0.0.1", 0), ModelHandler)
        model_port = int(model_server.server_address[1])
        model_thread = threading.Thread(target=model_server.serve_forever,
                                        daemon=True)
        ModelHandler.calls = 0
        ModelHandler.saw_prompt = False
        ModelHandler.todo_sent = False
        ModelHandler.ask_sent = False
        ModelHandler.ask_cancel_sent = False
        ModelHandler.ask_verify_sent = False
        model_thread.start()
        environment = os.environ.copy()
        environment["USERPROFILE"] = str(base)
        environment["HOME"] = str(base)
        environment["MDO_HOME"] = str(base / "wrong-environment-home")
        environment["MDO_LING_CHAT_COMPLETIONS_URL"] = (
            "https://example.invalid/v1")
        environment["MDO_LING_RESPONSES_URL"] = (
            f"http://127.0.0.1:{model_port}/v1")
        environment["MDO_LING_ANTHROPIC_URL"] = "https://example.invalid"
        environment["MDO_LING_API_KEY"] = "bounded-api-test-key"
        with log_path.open("wb") as log:
            process = subprocess.Popen(
                [str(host), str(config_path), "--", "--home", str(home)],
                cwd=base, env=environment, stdout=log, stderr=subprocess.STDOUT,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0,
            )
            failure: BaseException | None = None
            try:
                wait_ready(port, process)

                status, headers, body = request(port, "GET", "/api/v1/bootstrap?probe=1")
                document = json.loads(body)
                assert status == 200, (status, body)
                assert_common(headers, document)
                assert document["ok"] is True, document
                data = document["data"]
                assert data["version"] == "0.1.0-dev", data
                assert data["ready"] is True and data["stage"] == "ready", data
                assert data["config"]["schema_version"] == 1, data
                assert data["resources"]["models"]["models"] >= 1, data
                assert not home.exists(), home
                status, _, body = request(port, "GET", "/api/v1/memory/global")
                assert status == 200 and json.loads(body)["data"]["items"] == [], body
                status, _, body = request(port, "GET", "/api/v1/pane-layout")
                assert status == 200, (status, body)
                layout_defaults = json.loads(body)["data"]
                assert layout_defaults == {
                    "sidebar_width": 272, "inspector_width": 336,
                    "sidebar_open": True, "inspector_open": True,
                }, layout_defaults
                status, headers, body = request(port, "GET", "/api/v1/models/config")
                assert status == 200, (status, body)
                model_config = json.loads(body)["data"]
                assert model_config["default_model"] == "ling-3.0-tiny", model_config
                assert model_config["items"][0]["editable"] is False, model_config
                assert headers["etag"].startswith('"mdo-config-'), headers
                assert not home.exists(), home

                status, headers, body = request(
                    port, "GET", "/api/v1/migrations/legacy")
                document = json.loads(body)
                assert status == 200, (status, body)
                assert_common(headers, document)
                migrations = document["data"]
                assert migrations["count"] == 2, migrations
                assert migrations["requires_confirmation"] is True, migrations
                sources = {item["source_id"]: item
                           for item in migrations["items"]}
                portable = sources["portable-data"]
                assert portable["found"] is False, portable
                user_home = sources["user-home"]
                assert user_home["found"] is True, user_home
                assert user_home["valid"] is True, user_home
                assert user_home["importable"] is True, user_home
                assert user_home["target_available"] is True, user_home
                assert user_home["file_count"] == 5, user_home
                assert user_home["project_count"] == 1, user_home
                assert user_home["session_count"] == 1, user_home
                assert user_home["model_count"] == 1, user_home
                assert user_home["schedule_count"] == 1, user_home
                assert user_home["memory_file_count"] == 1, user_home
                assert user_home["unsupported_count"] == 0, user_home
                assert re.fullmatch(r"[0-9a-f]{64}",
                                    user_home["preview_token"]), user_home
                assert not home.exists(), home

                stale_token = ("0" if user_home["preview_token"][0] != "0"
                               else "1") + user_home["preview_token"][1:]
                status, _, body = request(
                    port, "POST", "/api/v1/migrations/legacy",
                    body=json.dumps({
                        "source_id": "user-home",
                        "preview_token": stale_token,
                    }).encode(), headers={"Content-Type": "application/json"})
                assert status == 409, (status, body)
                assert not home.exists(), home

                status, _, body = request(
                    port, "POST", "/api/v1/migrations/legacy",
                    body=json.dumps({
                        "source_id": "user-home",
                        "preview_token": user_home["preview_token"],
                    }).encode(), headers={"Content-Type": "application/json"})
                assert status == 422, (status, body)
                assert json.loads(body)["error"]["code"] == "migration_invalid", body
                assert not home.exists(), home
                assert not list(home.parent.glob(f"{home.name}.migrate-*")), list(
                    home.parent.iterdir())
                assert legacy.is_dir(), legacy

                legacy_schedule.write_text(json.dumps({
                    "id": "once",
                    "title": "Legacy reminder",
                    "prompt": "Review migration",
                    "kind": "once",
                }), encoding="utf-8")
                status, _, body = request(
                    port, "GET", "/api/v1/migrations/legacy")
                assert status == 200, (status, body)
                refreshed = json.loads(body)["data"]
                user_home = {item["source_id"]: item
                             for item in refreshed["items"]}["user-home"]
                assert user_home["importable"] is True, user_home

                status, headers, body = request(
                    port, "POST", "/api/v1/migrations/legacy",
                    body=json.dumps({
                        "source_id": "user-home",
                        "preview_token": user_home["preview_token"],
                    }).encode(), headers={"Content-Type": "application/json"})
                document = json.loads(body)
                assert status == 201, (status, body)
                assert_common(headers, document)
                applied = document["data"]
                assert applied["restart_required"] is True, applied
                assert applied["imported_models"] == 1, applied
                assert applied["imported_projects"] == 2, applied
                assert applied["imported_sessions"] == 1, applied
                assert applied["imported_schedules"] == 1, applied
                assert applied["imported_memory_entries"] == 1, applied
                assert home.is_dir(), home
                report_path = Path(applied["report_path"])
                assert report_path.is_file(), report_path
                report = json.loads(report_path.read_text(encoding="utf-8"))
                assert report["source_preserved"] is True, report
                assert report["restart_required"] is True, report
                assert report["preview_token"] == user_home["preview_token"], report
                assert "legacy-migration-secret" not in report_path.read_text(
                    encoding="utf-8"), report
                assert (home / "config/settings.json").is_file(), home
                migrated_settings = json.loads((home / "config/settings.json")
                                               .read_text(encoding="utf-8"))
                assert migrated_settings["patch"]["composer"]["submit_mode"] == "guide", migrated_settings
                assert migrated_settings["patch"]["notifications"]["sound"] is True, migrated_settings
                assert migrated_settings["patch"]["agent"]["interaction_mode"] == "agent", migrated_settings
                assert (home / "config/models.json").is_file(), home
                model_config = (home / "config/models.json").read_text(
                    encoding="utf-8")
                assert "legacy-migration-secret" not in model_config, model_config
                secret_path = next((home / "secrets").glob("legacy-*.key"))
                assert secret_path.read_text(encoding="utf-8") == (
                    "legacy-migration-secret"), secret_path
                assert (home / "memory/global.json").is_file(), home
                assert (home / "schedules/once.json").is_file(), home
                migrated_session = home / "sessions/api-legacy/s123456789abc"
                assert (migrated_session / "meta.json").is_file(), migrated_session
                assert (migrated_session / "snapshot.json").is_file(), migrated_session
                assert (home / "migration/session-prompts/api-legacy/"
                        "s123456789abc.txt").is_file(), home
                assert legacy.is_dir(), legacy

                # The UI reads bounded summaries and edits through store-wide
                # ETags, so concurrent agent writes cannot be overwritten.
                memory_path = "/api/v1/memory/projects/api-project"
                status, headers, body = request(port, "GET", memory_path)
                assert status == 200 and json.loads(body)["data"]["items"] == [], body
                initial_tag = headers["etag"]
                payload = json.dumps({
                    "id": "style", "title": "Style", "content": "Keep code readable.",
                    "tags": ["code"], "pinned": True,
                }).encode()
                status, _, body = request(port, "PUT", memory_path,
                    body=payload, headers={"Content-Type": "application/json"})
                assert status == 428, (status, body)
                status, headers, body = request(port, "PUT", memory_path,
                    body=payload, headers={"Content-Type": "application/json",
                                           "If-Match": initial_tag})
                assert status == 200, (status, body)
                saved_tag = headers["etag"]
                status, _, body = request(port, "GET", memory_path)
                listing = json.loads(body)["data"]
                assert status == 200 and len(listing["items"]) == 1, listing
                assert "content" not in listing["items"][0], listing
                status, _, body = request(port, "GET", memory_path + "/style")
                entry = json.loads(body)["data"]
                assert status == 200 and entry["content"] == "Keep code readable.", entry
                assert entry["tags"] == ["code"] and entry["pinned"], entry
                status, _, body = request(port, "PUT", memory_path,
                    body=payload, headers={"Content-Type": "application/json",
                                           "If-Match": initial_tag})
                assert status == 412, (status, body)
                status, _, body = request(port, "DELETE", memory_path + "/style",
                    headers={"If-Match": saved_tag})
                assert status == 200, (status, body)
                status, _, body = request(port, "GET", memory_path + "/style")
                assert status == 404, (status, body)

                layout_path = "/api/v1/pane-layout"
                layout = {
                    "sidebar_width": 354, "inspector_width": 412,
                    "sidebar_open": False, "inspector_open": True,
                }
                status, _, body = request(port, "PUT", layout_path,
                    body=json.dumps({**layout, "sidebar_width": 600}).encode(),
                    headers={"Content-Type": "application/json"})
                assert status == 422, (status, body)
                status, _, body = request(port, "PUT", layout_path,
                    body=json.dumps(layout).encode(),
                    headers={"Content-Type": "application/json"})
                assert status == 200 and json.loads(body)["data"] == layout, body
                status, _, body = request(port, "GET", layout_path)
                assert status == 200 and json.loads(body)["data"] == layout, body
                saved_layout = json.loads((home / "data/pane-layout.json")
                    .read_text(encoding="utf-8"))
                assert saved_layout["schema_version"] == 1, saved_layout
                assert all(saved_layout[key] == value for key, value in layout.items()), saved_layout

                resources = (
                    "settings", "models", "agents", "modules", "skills",
                    "mcp", "projects", "sessions", "runs", "schedules",
                    "tasks", "artifacts", "approvals", "permissions", "diagnostics",
                    "storage", "operations",
                    "migrations/legacy",
                )
                for resource in resources:
                    status, resource_headers, resource_body = request(
                        port, "GET", f"/api/v1/{resource}")
                    resource_document = json.loads(resource_body)
                    assert status == 200, (resource, status, resource_body)
                    assert_common(resource_headers, resource_document)
                    assert resource_document["ok"] is True, (
                        resource, resource_document)
                    assert isinstance(resource_document["data"], dict), (
                        resource, resource_document)
                    lowered = resource_body.lower()
                    assert b'"secret_ref"' not in lowered, (resource, lowered)
                    assert b'"authorization"' not in lowered, (resource, lowered)
                assert json.loads(request(port, "GET", "/api/v1/models")[2])[
                    "data"]["models"][0]["id"] == "ling-3.0-tiny"

                project_workspace = base / "project-workspace"
                project_workspace.mkdir()
                project_relative = os.path.relpath(project_workspace, host.parent)
                project_input = {
                    "id": "ui-workspace", "name": "UI Workspace",
                    "workspace_root": project_relative,
                    "default_model_id": "ling-3.0-tiny",
                }
                status, headers, body = request(
                    port, "POST", "/api/v1/projects",
                    body=json.dumps(project_input).encode(),
                    headers={"Content-Type": "application/json"})
                created_project = json.loads(body)
                assert status == 201, (status, body)
                assert_common(headers, created_project)
                assert created_project["data"]["managed"] is True, created_project
                assert created_project["data"]["revision"] == 1, created_project
                assert headers["etag"] == '"mdo-project-ui-workspace-1"', headers
                stored_project = json.loads((home / "projects/ui-workspace.json")
                                            .read_text(encoding="utf-8"))
                assert stored_project["schema_version"] == 1, stored_project
                assert stored_project["workspace_root"] == project_relative
                listed_projects = json.loads(request(
                    port, "GET", "/api/v1/projects")[2])["data"]["items"]
                assert any(item["id"] == "ui-workspace" and item["managed"] and
                           item["session_count"] == 0 for item in listed_projects), (
                               listed_projects)
                status, _, body = request(
                    port, "POST", "/api/v1/projects",
                    body=json.dumps(project_input).encode(),
                    headers={"Content-Type": "application/json"})
                assert status == 409 and json.loads(body)["error"]["code"] == (
                    "project_exists"), (status, body)
                for invalid in (
                    {**project_input, "id": "../escape"},
                    {**project_input, "extra": True},
                    {**project_input, "workspace_root": ""},
                ):
                    status, _, body = request(
                        port, "POST", "/api/v1/projects",
                        body=json.dumps(invalid).encode(),
                        headers={"Content-Type": "application/json"})
                    assert status == 422 and json.loads(body)["error"]["code"] == (
                        "project_invalid"), (status, body)
                status, _, body = request(
                    port, "POST", "/api/v1/sessions",
                    body=json.dumps({"project_id": "ui-workspace",
                                     "title": "Project default probe"}).encode(),
                    headers={"Content-Type": "application/json"})
                assert status == 201, (status, body)
                project_session = json.loads(body)["data"]
                assert project_session["project_id"] == "ui-workspace"
                assert project_session["model_id"] == "ling-3.0-tiny"
                assert (Path(project_session["workspace_root"]).resolve() ==
                        project_workspace.resolve()), project_session
                project_path = "/api/v1/projects/ui-workspace"
                status, headers, body = request(port, "GET", project_path)
                assert status == 200 and json.loads(body)["data"]["revision"] == 1
                assert headers["etag"] == '"mdo-project-ui-workspace-1"'
                status, _, body = request(port, "GET", "/api/v1/projects/.bad")
                assert status == 400 and json.loads(body)["error"]["code"] == (
                    "invalid_project_path"), (status, body)
                second_workspace = base / "second-project-workspace"
                second_workspace.mkdir()
                second_relative = os.path.relpath(second_workspace, host.parent)
                replacement = {
                    "name": "Renamed Workspace",
                    "workspace_root": second_relative,
                    "default_model_id": "",
                }
                status, _, body = request(
                    port, "PUT", project_path,
                    body=json.dumps(replacement).encode(),
                    headers={"Content-Type": "application/json"})
                assert status == 428, (status, body)
                status, headers, body = request(
                    port, "PUT", project_path,
                    body=json.dumps(replacement).encode(),
                    headers={"Content-Type": "application/json",
                             "If-Match": '"mdo-project-ui-workspace-1"'})
                updated_project = json.loads(body)["data"]
                assert status == 200 and updated_project["revision"] == 2, (
                    status, body)
                assert updated_project["name"] == "Renamed Workspace"
                assert headers["etag"] == '"mdo-project-ui-workspace-2"'
                backup_project = json.loads((home / "projects/ui-workspace.json.bak")
                                            .read_text(encoding="utf-8"))
                assert backup_project["revision"] == 1, backup_project
                status, _, body = request(
                    port, "PUT", project_path,
                    body=json.dumps(replacement).encode(),
                    headers={"Content-Type": "application/json",
                             "If-Match": '"mdo-project-ui-workspace-1"'})
                assert status == 412, (status, body)
                status, _, body = request(
                    port, "POST", "/api/v1/sessions",
                    body=json.dumps({"project_id": "ui-workspace",
                                     "title": "Updated workspace probe"}).encode(),
                    headers={"Content-Type": "application/json"})
                assert status == 201, (status, body)
                assert (Path(json.loads(body)["data"]["workspace_root"]).resolve()
                        == second_workspace.resolve()), body
                status, _, body = request(
                    port, "DELETE", project_path,
                    headers={"If-Match": '"mdo-project-ui-workspace-1"'})
                assert status == 412, (status, body)
                status, _, body = request(
                    port, "DELETE", project_path,
                    headers={"If-Match": '"mdo-project-ui-workspace-2"'})
                assert status == 200 and json.loads(body)["data"]["removed"], (
                    status, body)
                assert not (home / "projects/ui-workspace.json").exists()
                status, _, body = request(port, "GET", project_path)
                assert status == 404, (status, body)
                listed_projects = json.loads(request(
                    port, "GET", "/api/v1/projects")[2])["data"]["items"]
                assert any(item["id"] == "ui-workspace" and not item["managed"]
                           and item["session_count"] == 2 for item in
                           listed_projects), listed_projects

                approval_deadline = time.monotonic() + 3.0
                approvals = {"items": []}
                while time.monotonic() < approval_deadline:
                    status, headers, body = request(port, "GET", "/api/v1/approvals")
                    approval_document = json.loads(body)
                    assert status == 200, (status, body)
                    assert_common(headers, approval_document)
                    approvals = approval_document["data"]
                    if approvals["items"]:
                        break
                    time.sleep(0.01)
                assert approvals["total"] == 1, approvals
                assert approvals["limit"] == 4, approvals
                assert approvals["truncated"] is False, approvals
                approval = approvals["items"][0]
                assert approval["id"] == 7001, approval
                assert approval["agent_id"] == 41 and approval["run_id"] == 73, approval
                assert approval["catalog_generation"] == 5, approval
                assert approval["agent_turn"] == 9, approval
                assert approval["tool"] == "edit", approval
                assert approval["tool_call_id"] == "call-allow", approval
                assert approval["risk"] == "medium", approval
                assert approval["effects"] == ["workspace_write"], approval
                assert approval["resources"] == [{
                    "kind": "path", "resource": "notes.txt",
                    "access_code": 2, "access": ["write"],
                }], approval
                assert approval["expires_in_ms"] > 0, approval
                status, headers, body = request(port, "HEAD", "/api/v1/approvals")
                assert status == 200 and body == b"", (status, body)
                status, headers, body = request(port, "OPTIONS", "/api/v1/approvals")
                assert status == 200 and headers["allow"] == "GET, HEAD, OPTIONS", (
                    status, headers, body)
                decision_headers = {"Content-Type": "application/json"}
                status, headers, body = request(
                    port, "PUT", "/api/v1/approvals/7001",
                    body=b'{"decision":"allow"}', headers=decision_headers)
                decision_document = json.loads(body)
                assert status == 200, (status, body)
                assert_common(headers, decision_document)
                assert decision_document["data"] == {
                    "id": 7001, "decision": "allow"}, decision_document
                status, _, body = request(
                    port, "PUT", "/api/v1/approvals/7001",
                    body=b'{"decision":"allow"}', headers=decision_headers)
                assert status == 404, (status, body)
                assert json.loads(body)["error"]["code"] == "approval_not_found"

                approval_deadline = time.monotonic() + 3.0
                approval = None
                while time.monotonic() < approval_deadline:
                    approvals = json.loads(request(
                        port, "GET", "/api/v1/approvals")[2])["data"]
                    if approvals["items"] and approvals["items"][0]["id"] == 7002:
                        approval = approvals["items"][0]
                        break
                    time.sleep(0.01)
                assert approval is not None and approval["tool_call_id"] == "call-deny", (
                    approvals)
                status, _, body = request(
                    port, "PUT", "/api/v1/approvals/7002",
                    body=b'{"decision":"later"}', headers=decision_headers)
                assert status == 422, (status, body)
                assert json.loads(body)["error"]["code"] == (
                    "approval_decision_invalid")
                status, _, body = request(
                    port, "PUT", "/api/v1/approvals/7002",
                    body=b'{"decision":"deny"}', headers=decision_headers)
                assert status == 200, (status, body)
                assert json.loads(body)["data"]["decision"] == "deny"
                for approval_id in ("0", "bad", "18446744073709551616"):
                    status, _, body = request(
                        port, "PUT", f"/api/v1/approvals/{approval_id}",
                        body=b'{"decision":"deny"}', headers=decision_headers)
                    assert status == 400, (approval_id, status, body)
                    assert json.loads(body)["error"]["code"] == (
                        "invalid_approval_id")
                status, headers, body = request(
                    port, "OPTIONS", "/api/v1/approvals/7002")
                assert status == 200 and headers["allow"] == "PUT, OPTIONS", (
                    status, headers, body)

                tasks_document = json.loads(request(
                    port, "GET", "/api/v1/tasks")[2])
                tasks = tasks_document["data"]["items"]
                finished_task = next(
                    item for item in tasks
                    if item["label"] == "api-probe-finished")
                cancellable_task = next(
                    item for item in tasks
                    if item["label"] == "api-probe-cancellable")
                assert finished_task["state"] == "succeeded", finished_task
                assert finished_task["terminal"] is True, finished_task
                assert cancellable_task["state"] == "pending", cancellable_task
                finished_id = finished_task["id"]
                cancellable_id = cancellable_task["id"]
                task_path = f"/api/v1/tasks/{finished_id}"

                status, headers, body = request(port, "GET", task_path)
                document = json.loads(body)
                assert status == 200, (status, body)
                assert_common(headers, document)
                assert document["data"] == finished_task, document
                assert headers["etag"] == (
                    f'"mdo-task-{finished_id}-{finished_task["revision"]}"')

                output_path = task_path + "/output"
                status, headers, body = request(
                    port, "GET", output_path + "?result=0&limit=4")
                output_document = json.loads(body)
                assert status == 200, (status, body)
                assert_common(headers, output_document)
                output = output_document["data"]
                assert output["encoding"] == "base64", output
                assert output["complete"] is True, output
                assert output["stdout"]["data"] == "", output
                assert output["stderr"]["data"] == "", output
                assert base64.b64decode(output["result"]["data"]) == b"task", output
                assert output["result"]["start"] == 0, output
                assert output["result"]["next"] == 4, output
                status, _, body = request(
                    port, "GET", output_path + "?result=4&limit=64")
                output = json.loads(body)["data"]
                assert status == 200, (status, body)
                assert base64.b64decode(output["result"]["data"]) == b"-result", output
                assert output["result"]["next"] == len(b"task-result"), output
                for query in (
                    "limit=0", "limit=65537", "stdout=x", "result=0&result=1",
                    "unknown=1", "limit=4&",
                ):
                    status, _, body = request(
                        port, "GET", output_path + "?" + query)
                    assert status == 400, (query, status, body)
                    assert json.loads(body)["error"]["code"] == "invalid_query"

                events_path = task_path + "/events"
                status, headers, body = request(
                    port, "GET", events_path + "?after=0&limit=1")
                events_document = json.loads(body)
                assert status == 200, (status, body)
                assert_common(headers, events_document)
                events = events_document["data"]
                assert len(events["items"]) == 1, events
                assert events["items"][0]["kind"] == "created", events
                assert events["next_revision"] == events["items"][0]["revision"]
                status, _, body = request(
                    port, "GET",
                    events_path + f'?after={events["next_revision"]}&limit=64')
                later_events = json.loads(body)["data"]
                assert status == 200, (status, body)
                assert later_events["items"][-1]["state"] == "succeeded", (
                    later_events)
                assert later_events["items"][-1]["terminal"] is True, later_events

                cancel_path = f"/api/v1/tasks/{cancellable_id}"
                status, _, body = request(
                    port, "DELETE", cancel_path, body=b"{}",
                    headers={"Content-Type": "application/json"})
                assert status == 400, (status, body)
                assert json.loads(body)["error"]["code"] == "body_not_allowed"
                status, headers, body = request(port, "DELETE", cancel_path)
                cancelled = json.loads(body)
                assert status == 200, (status, body)
                assert_common(headers, cancelled)
                assert cancelled["data"]["state"] == "cancelled", cancelled
                assert cancelled["data"]["terminal"] is True, cancelled
                status, _, body = request(port, "DELETE", cancel_path)
                assert status == 200, (status, body)
                assert json.loads(body)["data"]["state"] == "cancelled"
                status, _, body = request(
                    port, "GET", cancel_path + "/events?after=0&limit=64")
                cancel_events = json.loads(body)["data"]["items"]
                assert status == 200, (status, body)
                assert any(item["kind"] == "cancel_requested"
                           for item in cancel_events), cancel_events
                assert cancel_events[-1]["state"] == "cancelled", cancel_events

                for path in (task_path, output_path, events_path):
                    status, headers, body = request(port, "HEAD", path)
                    assert status == 200 and body == b"", (path, status, body)
                    status, headers, body = request(port, "OPTIONS", path)
                    expected = ("GET, HEAD, DELETE, OPTIONS"
                                if path == task_path else "GET, HEAD, OPTIONS")
                    assert status == 200 and headers["allow"] == expected, (
                        path, status, headers, body)
                for path in (
                    "/api/v1/tasks/0", "/api/v1/tasks/not-a-number",
                    "/api/v1/tasks/18446744073709551616",
                ):
                    status, _, body = request(port, "GET", path)
                    assert status == 400, (path, status, body)
                    assert json.loads(body)["error"]["code"] == "invalid_path"
                for path in (
                    "/api/v1/tasks/999999999",
                    "/api/v1/tasks/999999999/output",
                    "/api/v1/tasks/999999999/events",
                ):
                    status, _, body = request(port, "GET", path)
                    assert status == 404, (path, status, body)
                    assert json.loads(body)["error"]["code"] == "task_not_found"

                for path in (
                    "/api/v1/artifacts/0", "/api/v1/artifacts/not-a-number",
                    "/api/v1/artifacts/18446744073709551616",
                ):
                    status, _, body = request(port, "GET", path)
                    assert status == 400, (path, status, body)
                    assert json.loads(body)["error"]["code"] == "invalid_path"
                for query in (
                    "limit=0", "limit=65537", "offset=x",
                    "offset=0&offset=1", "unknown=1", "offset=0&",
                ):
                    status, _, body = request(
                        port, "GET", f"/api/v1/artifacts/999999999?{query}")
                    assert status == 400, (query, status, body)
                    assert json.loads(body)["error"]["code"] == "invalid_query"
                artifact_path = "/api/v1/artifacts/999999999"
                status, _, body = request(port, "GET", artifact_path)
                assert status == 404, (status, body)
                assert json.loads(body)["error"]["code"] == "artifact_not_found"
                status, _, body = request(port, "HEAD", artifact_path)
                assert status == 404 and body == b"", (status, body)
                status, headers, body = request(port, "OPTIONS", artifact_path)
                assert status == 200 and headers["allow"] == (
                    "GET, HEAD, OPTIONS"), (status, headers, body)

                status, headers, body = request(
                    port, "GET", "/api/v1/events?after=0&limit=1")
                document = json.loads(body)
                assert status == 200, (status, body)
                assert_common(headers, document)
                assert document["data"]["after"] == 0, document
                assert isinstance(document["data"]["items"], list), document
                for query in (
                    "limit=0", "limit=33", "after=x", "after=0&after=1",
                    "unknown=1", "after=0&",
                ):
                    status, headers, body = request(
                        port, "GET", f"/api/v1/events?{query}")
                    document = json.loads(body)
                    assert status == 400, (query, status, body)
                    assert_common(headers, document)
                    assert document["error"]["code"] == "invalid_query", (
                        query, document)

                status, headers, body = request(
                    port, "GET",
                    "/api/v1/projects/project-1/sessions/session-1/events")
                document = json.loads(body)
                assert status == 404, (status, body)
                assert_common(headers, document)
                assert document["error"]["code"] == "session_not_found", document
                status, _, body = request(
                    port, "GET",
                    "/api/v1/projects/bad%20id/sessions/session-1/events")
                assert status == 400 and json.loads(body)["error"]["code"] == "invalid_path"

                status, headers, body = request(port, "HEAD", "/api/v1/bootstrap")
                assert status == 200 and body == b"", (status, body)
                assert int(headers["content-length"]) > 0, headers
                assert headers["content-type"] == "application/json; charset=utf-8", headers

                status, headers, body = request(port, "OPTIONS", "/api/v1/bootstrap")
                document = json.loads(body)
                assert status == 200, (status, body)
                assert_common(headers, document)
                assert headers["allow"] == "GET, HEAD, OPTIONS", headers
                assert document["data"]["allow"] == headers["allow"], document

                status, headers, body = request(port, "POST", "/api/v1/bootstrap")
                document = json.loads(body)
                assert status == 405, (status, body)
                assert_common(headers, document)
                assert headers["allow"] == "GET, HEAD, OPTIONS", headers
                assert document["ok"] is False, document
                assert document["error"]["code"] == "method_not_allowed", document

                status, headers, body = request(port, "GET", "/api/v1/unknown")
                document = json.loads(body)
                assert status == 404, (status, body)
                assert_common(headers, document)
                assert document["error"]["code"] == "route_not_found", document

                status, headers, body = request(port, "GET", "/")
                assert status == 200 and b"<!doctype html" in body.lower(), (status, body[:120])
                assert "x-request-id" not in headers, headers
                for asset, marker in (
                    ("/css/app.css", b".app-shell"),
                    ("/js/main.js", b'import { boot }'),
                    ("/js/app.js", b"export async function boot"),
                    ("/js/state/store.js", b"createResourceStore"),
                    ("/js/state/recovery.js", b"resumeRecovery"),
                ):
                    status, asset_headers, asset_body = request(port, "GET", asset)
                    assert status == 200 and marker in asset_body, (
                        asset, status, asset_body[:120])
                    assert "x-request-id" not in asset_headers, asset_headers

                preview_document = json.dumps({
                    "schema_version": 1,
                    "patch": {"appearance": {"theme": "dark"},
                              "composer": {"submit_mode": "guide"},
                              "notifications": {"sound": True}},
                }).encode()
                status, headers, body = request(
                    port, "POST", "/api/v1/settings/settings/preview",
                    body=preview_document,
                    headers={"Content-Type": "application/json; charset=UTF-8"})
                document = json.loads(body)
                assert status == 200, (status, body)
                assert_common(headers, document)
                assert document["data"]["domain"] == "settings", document
                assert document["data"]["valid"] is True, document
                assert document["data"]["changes"] is True, document
                assert document["data"]["patch_bytes"] > 0, document

                status, headers, body = request(
                    port, "POST", "/api/v1/settings/settings/preview",
                    body=[preview_document[:11], preview_document[11:]],
                    headers={"Content-Type": "application/json"},
                    encode_chunked=True)
                document = json.loads(body)
                assert status == 200, (status, body)
                assert_common(headers, document)

                body_errors = (
                    (None, {}, 415, "unsupported_media_type"),
                    (b"", {"Content-Type": "application/json"}, 400,
                     "body_required"),
                    (b"{", {"Content-Type": "application/json"}, 400,
                     "invalid_json"),
                    (b"{}", {"Content-Type": "text/plain"}, 415,
                     "unsupported_media_type"),
                    (b"{}", {"Content-Type": "application/json"}, 422,
                     "configuration_invalid"),
                    (b" " * (256 * 1024 + 1),
                     {"Content-Type": "application/json"}, 413,
                     "body_too_large"),
                )
                for request_body, request_headers, expected_status, code in body_errors:
                    status, headers, body = request(
                        port, "POST", "/api/v1/settings/settings/preview",
                        body=request_body, headers=request_headers)
                    document = json.loads(body)
                    assert status == expected_status, (code, status, body[:200])
                    assert_common(headers, document)
                    assert document["error"]["code"] == code, document

                status, headers, body = request(
                    port, "POST", "/api/v1/settings/unknown/preview",
                    body=preview_document,
                    headers={"Content-Type": "application/json"})
                document = json.loads(body)
                assert status == 404, (status, body)
                assert document["error"]["code"] == "config_domain_not_found"

                status, headers, body = request(
                    port, "OPTIONS", "/api/v1/settings/settings/preview")
                document = json.loads(body)
                assert status == 200 and headers["allow"] == (
                    "POST, PATCH, OPTIONS")
                assert document["data"]["allow"] == (
                    "POST, PATCH, OPTIONS")

                status, headers, body = request(
                    port, "GET", "/api/v1/settings/settings/preview")
                document = json.loads(body)
                assert status == 405 and headers["allow"] == (
                    "POST, PATCH, OPTIONS")
                assert document["error"]["code"] == "method_not_allowed"

                for resource in ("models", "skills", "mcp"):
                    before = json.loads(request(
                        port, "GET", f"/api/v1/{resource}")[2])["data"][
                            "generation"]
                    status, headers, body = request(
                        port, "POST", f"/api/v1/{resource}/reload")
                    document = json.loads(body)
                    assert status == 200, (resource, status, body)
                    assert_common(headers, document)
                    assert document["data"]["resource"] == resource, document
                    assert document["data"]["generation"] > before, document
                    status, headers, body = request(
                        port, "OPTIONS", f"/api/v1/{resource}/reload")
                    assert status == 200 and headers["allow"] == "POST, OPTIONS"

                mcp_data = json.loads(request(port, "GET", "/api/v1/mcp")[2])[
                    "data"]
                assert len(mcp_data["items"]) == 1, mcp_data
                assert mcp_data["items"][0]["id"] == "api-mock", mcp_data
                assert mcp_data["items"][0]["enabled"] is True, mcp_data
                assert mcp_data["items"][0]["connected"] is False, mcp_data

                status, headers, body = request(
                    port, "PUT", "/api/v1/mcp/api-mock/enabled",
                    body=json.dumps({"enabled": False, "extra": True}).encode(),
                    headers={"Content-Type": "application/json"})
                document = json.loads(body)
                assert status == 422, (status, body)
                assert document["error"]["code"] == "mcp_state_invalid", document

                status, headers, body = request(
                    port, "PUT", "/api/v1/mcp/api-mock/enabled",
                    body=b'{"enabled":false}',
                    headers={"Content-Type": "application/json"})
                document = json.loads(body)
                assert status == 200, (status, body)
                assert_common(headers, document)
                assert document["data"]["enabled"] is False, document
                assert document["data"]["state"] == "disabled", document

                status, headers, body = request(
                    port, "POST", "/api/v1/mcp/api-mock/refresh")
                document = json.loads(body)
                assert status == 409, (status, body)
                assert document["error"]["code"] == "mcp_server_disabled", document

                status, headers, body = request(
                    port, "PUT", "/api/v1/mcp/api-mock/enabled",
                    body=b'{"enabled":true}',
                    headers={"Content-Type": "application/json"})
                document = json.loads(body)
                assert status == 200 and document["data"]["enabled"] is True, (
                    status, body)

                status, headers, body = request(
                    port, "POST", "/api/v1/mcp/api-mock/refresh",
                    body=b"{}", headers={"Content-Type": "application/json"})
                document = json.loads(body)
                assert status == 400, (status, body)
                assert document["error"]["code"] == "body_not_allowed", document

                status, headers, body = request(
                    port, "POST", "/api/v1/mcp/api-mock/refresh")
                mcp_operation_document = json.loads(body)
                assert status == 202, (status, body)
                assert_common(headers, mcp_operation_document)
                mcp_operation = mcp_operation_document["data"]
                assert mcp_operation["kind"] == "mcp_refresh", mcp_operation
                assert mcp_operation["target"] == "api-mock", mcp_operation
                mcp_operation_id = mcp_operation["id"]
                deadline = time.monotonic() + 5.0
                while (not mcp_operation["terminal"] and
                       time.monotonic() < deadline):
                    time.sleep(0.01)
                    status, headers, body = request(
                        port, "GET", f"/api/v1/operations/{mcp_operation_id}")
                    mcp_operation = json.loads(body)["data"]
                    assert status == 200, (status, body)
                assert mcp_operation["state"] == "succeeded", mcp_operation
                assert mcp_operation["result"]["connected"] is True, mcp_operation
                assert mcp_operation["result"]["tools_discovered"] is True, (
                    mcp_operation)
                assert mcp_operation["result"]["discovered_tools"] == 1, (
                    mcp_operation)

                mcp_data = json.loads(request(port, "GET", "/api/v1/mcp")[2])[
                    "data"]
                assert mcp_data["items"][0]["connected"] is True, mcp_data
                assert mcp_data["items"][0]["discovered_tool_count"] == 1, (
                    mcp_data)

                status, headers, body = request(
                    port, "POST", "/api/v1/mcp/api-mock/disconnect",
                    body=b"{}", headers={"Content-Type": "application/json"})
                document = json.loads(body)
                assert status == 400 and document["error"]["code"] == (
                    "body_not_allowed"), (status, body)
                status, headers, body = request(
                    port, "POST", "/api/v1/mcp/api-mock/disconnect")
                document = json.loads(body)
                assert status == 200, (status, body)
                assert document["data"]["connected"] is False, document
                assert document["data"]["enabled"] is True, document

                status, headers, body = request(
                    port, "PUT", "/api/v1/mcp/missing/enabled",
                    body=b'{"enabled":true}',
                    headers={"Content-Type": "application/json"})
                document = json.loads(body)
                assert status == 404 and document["error"]["code"] == (
                    "mcp_server_not_found"), (status, body)
                for path, allow in (
                    ("enabled", "PUT, OPTIONS"),
                    ("disconnect", "POST, OPTIONS"),
                    ("refresh", "POST, OPTIONS"),
                ):
                    status, headers, body = request(
                        port, "OPTIONS", f"/api/v1/mcp/api-mock/{path}")
                    assert status == 200 and headers["allow"] == allow, (
                        path, status, headers, body)

                module_generation = json.loads(request(
                    port, "GET", "/api/v1/modules")[2])["data"]["generation"]
                status, headers, body = request(
                    port, "POST", "/api/v1/modules/reload")
                operation_document = json.loads(body)
                assert status == 202, (status, body)
                assert_common(headers, operation_document)
                operation = operation_document["data"]
                operation_id = operation["id"]
                assert re.fullmatch(r"op-[0-9a-f]{24}|op-[0-9a-f]{16}",
                                    operation_id), operation
                assert operation["kind"] == "module_reload", operation
                assert operation["state"] in (
                    "pending", "running", "succeeded"), operation

                terminal_operation = operation
                deadline = time.monotonic() + 5.0
                while (not terminal_operation["terminal"] and
                       time.monotonic() < deadline):
                    time.sleep(0.01)
                    status, headers, body = request(
                        port, "GET", f"/api/v1/operations/{operation_id}")
                    operation_document = json.loads(body)
                    assert status == 200, (status, body)
                    assert_common(headers, operation_document)
                    terminal_operation = operation_document["data"]
                assert terminal_operation["state"] == "succeeded", (
                    terminal_operation)
                assert terminal_operation["terminal"] is True, terminal_operation
                assert terminal_operation["result"]["catalog_generation"] > (
                    module_generation), terminal_operation
                assert terminal_operation["result"]["modules"] >= 1, (
                    terminal_operation)

                status, headers, body = request(
                    port, "GET", "/api/v1/operations")
                operations_document = json.loads(body)
                assert status == 200, (status, body)
                assert_common(headers, operations_document)
                assert operations_document["data"]["items"][0]["id"] == (
                    operation_id), operations_document

                status, headers, body = request(
                    port, "DELETE", f"/api/v1/operations/{operation_id}")
                document = json.loads(body)
                assert status == 200, (status, body)
                assert document["data"]["state"] == "succeeded", document
                status, headers, body = request(
                    port, "GET", "/api/v1/operations/op-does-not-exist")
                document = json.loads(body)
                assert status == 404, (status, body)
                assert document["error"]["code"] == "operation_not_found", document
                status, headers, body = request(
                    port, "OPTIONS", "/api/v1/modules/reload")
                assert status == 200 and headers["allow"] == "POST, OPTIONS"
                status, headers, body = request(
                    port, "OPTIONS", f"/api/v1/operations/{operation_id}")
                assert status == 200 and headers["allow"] == (
                    "GET, HEAD, DELETE, OPTIONS")

                assert home.is_dir(), list(base.iterdir())
                assert not (base / "wrong-environment-home").exists(), list(base.iterdir())

                status, headers, body = request(port, "GET", "/api/v1/settings")
                settings_document = json.loads(body)
                assert status == 200, (status, body)
                assert_common(headers, settings_document)
                initial_revision = settings_document["data"]["revision"]
                initial_etag = headers["etag"]
                assert initial_etag == f'"mdo-config-{initial_revision}"', headers
                assert settings_document["data"]["appearance"] == {
                    "theme": "system",
                    "font_size": "normal",
                    "density": "comfortable",
                }, settings_document
                assert settings_document["data"]["composer"] == {
                    "submit_mode": "queue",
                }, settings_document
                assert settings_document["data"]["notifications"] == {
                    "sound": False,
                }, settings_document
                assert settings_document["data"]["agent"][
                    "interaction_mode"] == "agent", settings_document
                assert settings_document["data"]["agent"][
                    "web_search"] is True, settings_document
                assert settings_document["data"]["workspace"] == {
                    "open_mode": "last",
                    "confirm_external_write": True,
                }, settings_document
                status, _, body = request(port, "GET", "/api/v1/workspace-state")
                assert status == 200 and json.loads(body)["data"] == {
                    "project_id": "", "session_id": "",
                }, body
                assert not (home / "data/workspace-state.json").exists()
                assert settings_document["data"]["transaction_service"] == {
                    "runtime_consistent": True,
                    "transactions": 0,
                    "rollbacks": 0,
                    "last_error": "",
                }, settings_document

                for supplied_headers, expected_status, code in (
                    ({"Content-Type": "application/json"}, 428,
                     "precondition_required"),
                    ({"Content-Type": "application/json",
                      "If-Match": str(initial_revision)}, 400,
                     "invalid_precondition"),
                    ({"Content-Type": "application/json",
                      "If-Match": 'W/"mdo-config-1"'}, 400,
                     "invalid_precondition"),
                ):
                    status, headers, body = request(
                        port, "PUT", "/api/v1/settings/settings",
                        body=preview_document, headers=supplied_headers)
                    document = json.loads(body)
                    assert status == expected_status, (status, body)
                    assert_common(headers, document)
                    assert document["error"]["code"] == code, document

                status, headers, body = request(
                    port, "PUT", "/api/v1/settings/settings",
                    body=preview_document,
                    headers={"Content-Type": "application/json",
                             "If-Match": initial_etag})
                document = json.loads(body)
                assert status == 200, (status, body)
                assert_common(headers, document)
                mutation = document["data"]
                assert mutation["domain"] == "settings", mutation
                assert mutation["changed"] is True, mutation
                assert mutation["restored"] is False, mutation
                assert mutation["previous_revision"] == initial_revision, mutation
                assert mutation["revision"] == initial_revision + 1, mutation
                current_etag = headers["etag"]
                assert current_etag == (
                    f'"mdo-config-{mutation["revision"]}"'), headers
                stored = json.loads((home / "config/settings.json").read_text(
                    encoding="utf-8"))
                assert stored["patch"]["appearance"]["theme"] == "dark", stored
                assert stored["patch"]["composer"]["submit_mode"] == "guide", stored
                assert stored["patch"]["notifications"]["sound"] is True, stored
                status, _, body = request(port, "GET", "/api/v1/settings")
                assert status == 200 and json.loads(body)["data"]["notifications"] == {
                    "sound": True,
                }, body

                merge_document = json.dumps({
                    "schema_version": 1,
                    "patch": {"agent": {"memory": False}},
                }).encode()
                status, headers, body = request(
                    port, "PATCH", "/api/v1/settings/settings/preview",
                    body=merge_document,
                    headers={"Content-Type": "application/json"})
                document = json.loads(body)
                assert status == 200 and document["data"]["changes"] is True, (
                    status, body)
                assert document["data"]["current_revision"] == (
                    initial_revision + 1), document
                status, headers, body = request(
                    port, "PATCH", "/api/v1/settings/settings",
                    body=merge_document,
                    headers={"Content-Type": "application/json",
                             "If-Match": current_etag})
                document = json.loads(body)
                assert status == 200, (status, body)
                mutation = document["data"]
                assert mutation["revision"] == initial_revision + 2, mutation
                current_etag = headers["etag"]
                stored = json.loads((home / "config/settings.json").read_text(
                    encoding="utf-8"))
                assert stored["patch"]["appearance"]["theme"] == "dark", stored
                assert stored["patch"]["agent"]["memory"] is False, stored

                stale_document = json.dumps({
                    "schema_version": 1,
                    "patch": {"appearance": {"theme": "light"}},
                }).encode()
                status, headers, body = request(
                    port, "PUT", "/api/v1/settings/settings",
                    body=stale_document,
                    headers={"Content-Type": "application/json",
                             "If-Match": initial_etag})
                document = json.loads(body)
                assert status == 412, (status, body)
                assert_common(headers, document)
                assert document["error"]["code"] == "revision_conflict", document

                status, headers, body = request(
                    port, "DELETE", "/api/v1/settings/settings",
                    body=b"{}", headers={"Content-Type": "application/json",
                                          "If-Match": current_etag})
                document = json.loads(body)
                assert status == 400, (status, body)
                assert document["error"]["code"] == "body_not_allowed", document

                status, headers, body = request(
                    port, "DELETE", "/api/v1/settings/settings",
                    headers={"If-Match": current_etag})
                document = json.loads(body)
                assert status == 200, (status, body)
                assert_common(headers, document)
                mutation = document["data"]
                assert mutation["changed"] is True, mutation
                assert mutation["restored"] is True, mutation
                assert mutation["revision"] == initial_revision + 3, mutation
                assert headers["etag"] == (
                    f'"mdo-config-{mutation["revision"]}"'), headers
                assert not (home / "config/settings.json").exists(), list(
                    (home / "config").iterdir())

                status, headers, body = request(port, "GET", "/api/v1/settings")
                settings_document = json.loads(body)
                assert status == 200, (status, body)
                assert settings_document["data"]["revision"] == (
                    initial_revision + 3), settings_document
                assert settings_document["data"]["transaction_service"][
                    "transactions"] == 3, settings_document
                assert settings_document["data"]["transaction_service"][
                    "runtime_consistent"] is True, settings_document

                invalid_session = json.dumps({
                    "project_id": "api-project",
                    "unknown": True,
                }).encode()
                status, headers, body = request(
                    port, "POST", "/api/v1/sessions", body=invalid_session,
                    headers={"Content-Type": "application/json"})
                document = json.loads(body)
                assert status == 422, (status, body)
                assert document["error"]["code"] == "session_create_invalid", (
                    document)

                create_session = json.dumps({
                    "project_id": "api-project",
                    "title": "API session",
                    "agent_id": "mdo.default",
                    "model_id": "ling-3.0-tiny",
                    "protocol": "openai-responses",
                    "reasoning_effort": "medium",
                    "max_output_tokens": 1024,
                    "workspace_root": str(base),
                }).encode()
                status, headers, body = request(
                    port, "POST", "/api/v1/sessions", body=create_session,
                    headers={"Content-Type": "application/json"})
                session_document = json.loads(body)
                assert status == 201, (status, body)
                assert_common(headers, session_document)
                session = session_document["data"]
                session_id = session["id"]
                assert session["project_id"] == "api-project", session
                assert session["title"] == "API session", session
                assert session["agent_id"] == "mdo.default", session
                assert session["model_id"] == "ling-3.0-tiny", session
                assert session["protocol"] == "openai-responses", session
                assert session["revision"] == 1, session
                assert session["runtime_open"] is False, session
                session_etag = headers["etag"]
                assert session_etag == (
                    f'"mdo-session-{session_id}-1"'), headers

                meta_path = home / f"sessions/api-project/{session_id}/meta.json"
                assert meta_path.is_file(), list(home.rglob("*"))
                meta_text = meta_path.read_text(encoding="utf-8")
                assert "bounded-api-test-key" not in meta_text, meta_text
                meta = json.loads(meta_text)
                assert meta["id"] == session_id, meta
                assert meta["project_id"] == "api-project", meta

                selected_workspace = json.dumps({
                    "project_id": "api-project", "session_id": session_id,
                }).encode()
                status, _, body = request(
                    port, "PUT", "/api/v1/workspace-state",
                    body=selected_workspace,
                    headers={"Content-Type": "application/json"})
                assert status == 200 and json.loads(body)["data"] == {
                    "project_id": "api-project", "session_id": session_id,
                }, body
                stored_workspace = json.loads((home / "data/workspace-state.json")
                                              .read_text(encoding="utf-8"))
                assert stored_workspace == {
                    "schema_version": 1, "project_id": "api-project",
                    "session_id": session_id,
                }, stored_workspace
                status, _, body = request(port, "GET", "/api/v1/workspace-state")
                assert status == 200 and json.loads(body)["data"] == {
                    "project_id": "api-project", "session_id": session_id,
                }, body
                for invalid in (
                    {"project_id": "../outside", "session_id": session_id},
                    {"project_id": "api-project", "session_id": ""},
                    {"project_id": "api-project", "session_id": session_id,
                     "extra": True},
                ):
                    status, _, body = request(
                        port, "PUT", "/api/v1/workspace-state",
                        body=json.dumps(invalid).encode(),
                        headers={"Content-Type": "application/json"})
                    assert status == 422 and json.loads(body)["error"]["code"] == (
                        "workspace_state_invalid"), (status, body)
                status, _, body = request(
                    port, "PUT", "/api/v1/workspace-state",
                    body=json.dumps({"project_id": "api-project",
                                     "session_id": "missing"}).encode(),
                    headers={"Content-Type": "application/json"})
                assert status == 404 and json.loads(body)["error"]["code"] == (
                    "session_not_found"), (status, body)
                assert json.loads((home / "data/workspace-state.json")
                                  .read_text(encoding="utf-8")) == stored_workspace

                status, headers, body = request(
                    port, "GET",
                    f"/api/v1/projects/api-project/sessions/{session_id}")
                detail_document = json.loads(body)
                assert status == 200, (status, body)
                assert_common(headers, detail_document)
                assert detail_document["data"] == session, (
                    detail_document, session_document)
                assert headers["etag"] == session_etag, headers

                sessions_document = json.loads(request(
                    port, "GET", "/api/v1/sessions")[2])
                assert any(item["id"] == session_id for item in
                           sessions_document["data"]["items"]), sessions_document
                status, headers, body = request(
                    port, "GET",
                    "/api/v1/projects/api-project/sessions/missing")
                document = json.loads(body)
                assert status == 404, (status, body)
                assert document["error"]["code"] == "session_not_found", document

                session_path = (
                    f"/api/v1/projects/api-project/sessions/{session_id}")
                attachments_path = session_path + "/attachments"
                png_bytes = b"\x89PNG\r\n\x1a\n" + b"bounded-image" * 28000
                status, headers, response = request(
                    port, "POST", attachments_path, body=png_bytes,
                    headers={"Content-Type": "image/png"})
                uploaded = json.loads(response)
                assert status == 201, (status, uploaded)
                assert_common(headers, uploaded)
                image = uploaded["data"]
                assert re.fullmatch(r"[0-9a-f]{32}", image["id"]), image
                assert image["size"] == len(png_bytes), image
                assert image["mime_type"] == "image/png", image
                assert image["url"] == attachments_path + "/" + image["id"]
                assert (home / f"sessions/api-project/{session_id}/attachments/"
                        f"{image['id']}.bin").read_bytes() == png_bytes
                status, image_headers, downloaded = request(
                    port, "GET", image["url"])
                assert status == 200 and downloaded == png_bytes, (
                    status, len(downloaded))
                assert image_headers["content-type"] == "image/png", image_headers
                assert image_headers["cache-control"] == "no-store", image_headers
                status, _, downloaded = request(port, "HEAD", image["url"])
                assert status == 200 and downloaded == b"", (status, downloaded)
                status, _, body = request(port, "DELETE", image["url"],
                    body=b"{}", headers={"Content-Type": "application/json"})
                assert status == 400 and json.loads(body)["error"][
                    "code"] == "body_not_allowed", (status, body)
                status, _, body = request(port, "POST", attachments_path,
                    body=png_bytes, headers={"Content-Type": "image/png"})
                orphan = json.loads(body)["data"]
                assert status == 201 and orphan["id"] != image["id"], body
                status, _, body = request(port, "DELETE", orphan["url"])
                assert status == 200, (status, body)
                assert request(port, "GET", orphan["url"])[0] == 404
                assert request(port, "DELETE", orphan["url"])[0] == 404
                status, _, body = request(port, "POST", attachments_path,
                    body=png_bytes, headers={"Content-Type": "image/png"})
                half_orphan = json.loads(body)["data"]
                assert status == 201, (status, body)
                (home / f"sessions/api-project/{session_id}/attachments/"
                 f"{half_orphan['id']}.json").unlink()
                assert request(port, "DELETE", half_orphan["url"])[0] == 200
                assert not (home / f"sessions/api-project/{session_id}/"
                            f"attachments/{half_orphan['id']}.bin").exists()
                status, headers, body = request(port, "OPTIONS", image["url"])
                assert status == 200 and headers["allow"] == (
                    "GET, HEAD, DELETE, OPTIONS"), (status, headers, body)
                status, _, response = request(
                    port, "POST", attachments_path, body=b"not an image",
                    headers={"Content-Type": "image/png"})
                assert status == 415, (status, response)
                status, _, response = request(
                    port, "POST", attachments_path, body=png_bytes[:16],
                    headers={"Content-Type": "image/jpeg"})
                assert status == 415, (status, response)
                status, _, response = request(
                    port, "GET", image["url"].replace(
                        f"sessions/{session_id}/", "sessions/missing/"))
                assert status == 404, (status, response)
                status, _, response = request(
                    port, "POST", session_path + "/runs",
                    body=json.dumps({"prompt": "", "attachments": [image["id"]]}).encode(),
                    headers={"Content-Type": "application/json"})
                rejected = json.loads(response)
                assert status == 422, (status, rejected)
                assert rejected["error"]["code"] == (
                    "image_model_unsupported"), rejected
                (base / "mention-ref.c").write_text("// fixture", encoding="utf-8")
                (base / "mention-fixture").mkdir()
                (base / "mention-fixture/mention-ref nested.c").write_text(
                    "// fixture", encoding="utf-8")
                (base / ".hidden").mkdir()
                (base / ".hidden/mention-ref-secret.c").write_text(
                    "// fixture", encoding="utf-8")
                files_path = session_path + "/workspace/files"
                status, headers, body = request(
                    port, "GET", files_path + "?q=mention-ref")
                files = json.loads(body)
                assert status == 200, (status, body)
                assert_common(headers, files)
                assert files["data"]["items"] == [
                    "mention-ref.c", "mention-fixture/mention-ref nested.c"], files
                assert files["data"]["query"] == "mention-ref", files
                assert isinstance(files["data"]["truncated"], bool), files
                status, _, body = request(
                    port, "GET", files_path + "?q=MENTION-REF")
                assert status == 200 and json.loads(body)["data"]["items"] == [
                    "mention-ref.c", "mention-fixture/mention-ref nested.c"], body
                status, _, _ = request(port, "HEAD", files_path + "?q=mention-ref")
                assert status == 200, status
                status, _, body = request(port, "GET", files_path + "?q=%00")
                assert status == 400 and json.loads(body)["error"][
                    "code"] == "invalid_query", (status, body)
                status, _, body = request(
                    port, "GET", session_path.replace(session_id, "missing") +
                    "/workspace/files?q=mention-ref")
                assert status == 404 and json.loads(body)["error"][
                    "code"] == "session_not_found", (status, body)
                status, _, body = request(port, "GET", "/api/v1/draft")
                assert status == 200 and json.loads(body)["data"] == {
                    "revision": 0, "text": "", "attachments": []}, (status, body)
                assert not (home / "data/draft.json").exists()
                status, _, body = request(
                    port, "PUT", "/api/v1/draft",
                    body=json.dumps({"revision": 0, "text": "未发送的草稿"}).encode(),
                    headers={"Content-Type": "application/json"})
                assert status == 200 and json.loads(body)["data"] == {
                    "revision": 1, "text": "未发送的草稿", "attachments": []}, (status, body)
                status, _, body = request(
                    port, "PUT", "/api/v1/draft",
                    body=b'{"revision":0,"text":"stale"}',
                    headers={"Content-Type": "application/json"})
                assert status == 409 and json.loads(body)["error"][
                    "code"] == "draft_conflict", (status, body)
                assert json.loads((home / "data/draft.json").read_text(
                    encoding="utf-8"))["text"] == "未发送的草稿"
                draft_path = session_path + "/draft"
                status, _, body = request(port, "GET", draft_path)
                assert status == 200 and json.loads(body)["data"] == {
                    "revision": 0, "text": "", "attachments": []}, (status, body)
                status, _, body = request(
                    port, "PUT", draft_path,
                    body=b'{"revision":0,"text":"session draft"}',
                    headers={"Content-Type": "application/json"})
                assert status == 200 and json.loads(body)["data"] == {
                    "revision": 1, "text": "session draft", "attachments": []}, (status, body)
                assert json.loads(request(port, "GET", draft_path)[2])[
                    "data"]["text"] == "session draft"
                status, _, body = request(port, "PUT", draft_path,
                    body=json.dumps({"revision": 1, "text": "",
                                     "attachments": [image["id"]]}).encode(),
                    headers={"Content-Type": "application/json"})
                assert status == 200 and json.loads(body)["data"] == {
                    "revision": 2, "text": "", "attachments": [image["id"]]}, (
                    status, body)
                assert json.loads(request(port, "GET", draft_path)[2])[
                    "data"]["attachments"] == [image["id"]]
                status, _, body = request(port, "DELETE", image["url"])
                assert status == 409 and json.loads(body)["error"][
                    "code"] == "attachment_in_use", (status, body)
                attachment_dir = (home / "sessions/api-project" / session_id /
                                  "attachments")
                old_time = int((time.time() - 2 * 86400) * 1_000_000)
                image_meta_path = attachment_dir / f"{image['id']}.json"
                meta = json.loads(image_meta_path.read_text(encoding="utf-8"))
                meta["created_at"] = old_time
                image_meta_path.write_text(json.dumps(meta),
                                           encoding="utf-8")
                status, _, body = request(port, "POST", attachments_path,
                    body=png_bytes[:32],
                    headers={"Content-Type": "image/png"})
                aged = json.loads(body)["data"]
                assert status == 201, (status, body)
                aged_meta_path = attachment_dir / f"{aged['id']}.json"
                aged_meta = json.loads(aged_meta_path.read_text(
                    encoding="utf-8"))
                aged_meta["created_at"] = old_time
                aged_meta_path.write_text(json.dumps(aged_meta),
                                          encoding="utf-8")
                filler_ids = [f"{index:032x}" for index in range(1, 15)]
                assert image["id"] not in filler_ids and aged["id"] not in (
                    filler_ids)
                for filler_id in filler_ids:
                    (attachment_dir / f"{filler_id}.bin").write_bytes(
                        png_bytes[:32])
                    (attachment_dir / f"{filler_id}.json").write_text(
                        json.dumps({"schema_version": 1, "id": filler_id,
                            "mime_type": "image/png", "size": 32,
                            "created_at": int(time.time() * 1_000_000)}),
                        encoding="utf-8")
                status, _, body = request(port, "POST", attachments_path,
                    body=png_bytes[:32],
                    headers={"Content-Type": "image/png"})
                reclaimed_upload = json.loads(body)
                assert status == 201, (status, reclaimed_upload)
                assert not (attachment_dir / f"{aged['id']}.bin").exists()
                assert (attachment_dir / f"{image['id']}.bin").exists()
                assert request(port, "DELETE",
                    reclaimed_upload["data"]["url"])[0] == 200
                for filler_id in filler_ids:
                    (attachment_dir / f"{filler_id}.bin").unlink()
                    (attachment_dir / f"{filler_id}.json").unlink()
                status, _, body = request(port, "PUT", draft_path,
                    body=b'{"revision":2,"text":"","attachments":[]}',
                    headers={"Content-Type": "application/json"})
                assert status == 200 and json.loads(body)["data"][
                    "attachments"] == [], (status, body)
                assert json.loads((home / f"sessions/api-project/{session_id}/"
                                   "draft.json").read_text(encoding="utf-8"))[
                    "schema_version"] == 2
                status, _, body = request(port, "PUT", "/api/v1/draft",
                    body=json.dumps({"revision": 1, "text": "global",
                                     "attachments": [image["id"]]}).encode(),
                    headers={"Content-Type": "application/json"})
                assert status == 422, (status, body)
                queue_path = session_path + "/queue"
                queue_file = home / "sessions/api-project" / session_id / "queue.json"
                status, _, body = request(port, "GET", queue_path)
                assert status == 200 and json.loads(body)["data"] == {
                    "items": []}, (status, body)
                assert not queue_file.exists(), queue_file
                first_id = "a" * 32
                second_id = "b" * 32
                priority_id = "c" * 32

                def queue_request(method, target, payload):
                    return request(port, method, target,
                                   body=json.dumps(payload).encode("utf-8"),
                                   headers={"Content-Type": "application/json"})

                first = {"id": first_id, "text": "first prompt", "first": False}
                status, _, body = queue_request("POST", queue_path, first)
                assert status == 201 and json.loads(body)["data"]["items"] == [
                    {"id": first_id, "text": "first prompt", "state": "pending",
                     "attachments": [], "priority": False}], (
                    status, body)
                status, _, body = queue_request("POST", queue_path, first)
                assert status == 200 and len(json.loads(body)["data"]["items"]) == 1, (
                    status, body)
                status, _, body = queue_request("POST", queue_path,
                    {"id": first_id, "text": "different", "first": False})
                assert status == 409 and json.loads(body)["error"][
                    "code"] == "queue_id_conflict", (status, body)
                status, _, body = queue_request("POST", queue_path,
                    {"id": second_id, "text": "second prompt", "first": False})
                assert status == 201, (status, body)
                first_path = queue_path + "/" + first_id
                status, _, body = queue_request("PUT", first_path,
                    {"state": "sending"})
                assert status == 200 and json.loads(body)["data"]["items"][0][
                    "state"] == "sending", (status, body)
                status, _, body = queue_request("PUT", first_path,
                    {"state": "sending"})
                assert status == 409 and json.loads(body)["error"][
                    "code"] == "queue_state_conflict", (status, body)
                status, _, body = queue_request("POST", queue_path,
                    {"id": priority_id, "text": "interrupt prompt",
                     "first": True, "priority": True})
                assert status == 201 and [item["id"] for item in json.loads(body)[
                    "data"]["items"]] == [first_id, priority_id, second_id], (
                    status, body)
                assert json.loads(body)["data"]["items"][1]["priority"] is True
                status, _, body = queue_request("POST", queue_path,
                    {"id": priority_id, "text": "interrupt prompt", "first": True})
                assert status == 409 and json.loads(body)["error"][
                    "code"] == "queue_id_conflict", (status, body)
                assert json.loads(queue_file.read_text(encoding="utf-8"))[
                    "items"][1]["priority"] is True
                status, _, body = queue_request("PUT", first_path,
                    {"state": "pending"})
                assert status == 200 and json.loads(body)["data"]["items"][0][
                    "state"] == "pending", (status, body)
                status, _, body = request(port, "DELETE", first_path)
                assert status == 200 and [item["id"] for item in json.loads(body)[
                    "data"]["items"]] == [priority_id, second_id], (status, body)
                status, _, body = request(port, "DELETE", first_path)
                assert status == 200 and len(json.loads(body)["data"]["items"]) == 2, (
                    status, body)
                status, _, body = queue_request("POST", queue_path,
                    {"id": "invalid", "text": "bad", "first": False})
                assert status == 422 and json.loads(body)["error"][
                    "code"] == "queue_item_invalid", (status, body)
                for item_id in (priority_id, second_id):
                    status, _, body = request(port, "DELETE",
                                              queue_path + "/" + item_id)
                    assert status == 200, (status, body)
                assert json.loads(request(port, "GET", queue_path)[2])[
                    "data"]["items"] == []
                image_item_id = "d" * 32
                status, _, body = queue_request("POST", queue_path, {
                    "id": image_item_id, "text": "", "first": False,
                    "attachments": [image["id"]],
                })
                image_queue = json.loads(body)["data"]["items"]
                assert status == 201 and image_queue == [{
                    "id": image_item_id, "text": "", "state": "pending",
                    "attachments": [image["id"]], "priority": False,
                }], (status, body)
                assert json.loads(queue_file.read_text(encoding="utf-8"))[
                    "schema_version"] == 3
                status, _, body = request(port, "DELETE", image["url"])
                assert status == 409 and json.loads(body)["error"][
                    "code"] == "attachment_in_use", (status, body)
                status, _, body = queue_request("PUT",
                    queue_path + "/" + image_item_id, {"state": "sending"})
                assert status == 200 and json.loads(body)["data"]["items"][0][
                    "attachments"] == [image["id"]], (status, body)
                status, _, body = queue_request("POST", queue_path, {
                    "id": "e" * 32, "text": "", "first": False,
                    "attachments": ["0" * 32],
                })
                assert status == 422, (status, body)
                status, _, body = request(port, "DELETE",
                    queue_path + "/" + image_item_id)
                assert status == 200 and json.loads(body)["data"]["items"] == [], (
                    status, body)
                status, _, body = request(port, "DELETE", image["url"])
                assert status == 200 and request(port, "GET", image["url"])[0] == (
                    404), (status, body)
                legacy_id = "f" * 32
                queue_file.write_text(json.dumps({"schema_version": 2,
                    "items": [{"id": legacy_id, "text": "legacy prompt",
                               "state": "pending", "attachments": []}]}),
                    encoding="utf-8")
                status, _, body = request(port, "GET", queue_path)
                assert status == 200 and json.loads(body)["data"]["items"][0][
                    "priority"] is False, (status, body)
                status, _, body = queue_request("POST", queue_path,
                    {"id": "1" * 32, "text": "new prompt", "first": False})
                assert status == 201 and json.loads(queue_file.read_text(
                    encoding="utf-8"))["schema_version"] == 3, (status, body)
                for item_id in (legacy_id, "1" * 32):
                    assert request(port, "DELETE", queue_path + "/" + item_id)[0] == 200
                todo_path = session_path + "/todo"
                todo_file = home / "sessions/api-project" / session_id / "todo.json"
                status, _, body = request(port, "GET", todo_path)
                assert status == 200 and json.loads(body)["data"] == {
                    "schema_version": 1, "event_id": 0, "items": []}, (status, body)
                assert not todo_file.exists(), todo_file
                todo_file.write_text(json.dumps({
                    "schema_version": 1, "event_id": 8,
                    "items": [{"text": "Inspect code", "done": False},
                              {"text": "Verify change", "done": True}],
                }), encoding="utf-8")
                status, _, body = request(port, "GET", todo_path)
                assert status == 200 and json.loads(body)["data"]["items"] == [
                    {"text": "Inspect code", "done": False},
                    {"text": "Verify change", "done": True}], (status, body)
                status, _, body = request(port, "GET",
                    "/api/v1/projects/api-project/sessions/missing/todo")
                assert status == 404 and json.loads(body)["error"][
                    "code"] == "session_not_found", (status, body)
                run_path = session_path + "/runs"
                status, headers, body = request(
                    port, "POST", run_path,
                    body=b'{"prompt":"","unknown":true}',
                    headers={"Content-Type": "application/json"})
                document = json.loads(body)
                assert status == 422, (status, body)
                assert document["error"]["code"] == "run_start_invalid", (
                    document)
                status, headers, body = request(
                    port, "POST",
                    "/api/v1/projects/api-project/sessions/missing/runs",
                    body=b'{"prompt":"missing session"}',
                    headers={"Content-Type": "application/json"})
                document = json.loads(body)
                assert status == 404, (status, body)
                assert document["error"]["code"] == "session_not_found", (
                    document)

                status, headers, body = request(
                    port, "POST", run_path,
                    body=json.dumps({
                        "prompt": "API interactive prompt",
                        "timeout_ms": 5000,
                    }).encode(),
                    headers={"Content-Type": "application/json"})
                run_document = json.loads(body)
                assert status == 202, (status, body)
                assert_common(headers, run_document)
                run = run_document["data"]
                run_id = run["id"]
                assert run_id.startswith("run-"), run
                assert run["project_id"] == "api-project", run
                assert run["session_id"] == session_id, run
                assert run["state"] in ("running", "succeeded"), run
                assert run["terminal"] is False, run
                assert run["created_at"] > 0, run
                assert run["started_at"] >= run["created_at"], run
                assert run["ended_at"] == 0, run
                detail_path = f"/api/v1/runs/{run_id}"
                deadline = time.monotonic() + 5.0
                while not run["terminal"] and time.monotonic() < deadline:
                    time.sleep(0.01)
                    status, headers, body = request(port, "GET", detail_path)
                    run_document = json.loads(body)
                    assert status == 200, (status, body)
                    assert_common(headers, run_document)
                    run = run_document["data"]
                assert run["terminal"] is True, run
                assert run["state"] == "succeeded", run
                assert run["result"] == "ok", run
                assert run["ended_at"] >= run["started_at"], run
                assert run["final_text"] == "API interactive result", run
                assert run["final_text_bytes"] == 22, run
                assert run["model_calls"] == 1, run
                assert ModelHandler.calls == 1 and ModelHandler.saw_prompt, (
                    ModelHandler.calls, ModelHandler.saw_prompt)

                runs_document = json.loads(request(
                    port, "GET", "/api/v1/runs")[2])
                assert runs_document["data"]["active_runs"] == 0, runs_document
                assert runs_document["data"]["runs_completed"] == 1, (
                    runs_document)
                listed = runs_document["data"]["items"][0]
                assert listed["id"] == run_id and listed["terminal"] is True, (
                    listed)
                assert "final_text" not in listed, listed

                status, headers, body = request(
                    port, "DELETE", detail_path, body=b"{}",
                    headers={"Content-Type": "application/json"})
                document = json.loads(body)
                assert status == 400, (status, body)
                assert document["error"]["code"] == "body_not_allowed", document
                status, headers, body = request(port, "DELETE", detail_path)
                document = json.loads(body)
                assert status == 200, (status, body)
                assert document["data"]["terminal"] is True, document
                assert document["data"]["state"] == "succeeded", document

                status, headers, body = request(port, "HEAD", detail_path)
                assert status == 200 and body == b"", (status, body)
                status, headers, body = request(port, "OPTIONS", run_path)
                assert status == 200 and headers["allow"] == "POST, OPTIONS", (
                    status, headers, body)
                status, headers, body = request(port, "OPTIONS", detail_path)
                assert status == 200 and headers["allow"] == (
                    "GET, HEAD, DELETE, OPTIONS"), (status, headers, body)
                event_document = json.loads(request(
                    port, "GET", session_path + "/events?after=0&limit=32")[2])
                assert any(item["terminal"] for item in
                           event_document["data"]["items"]), event_document

                # Feedback belongs to a completed model reply and survives a
                # fresh read from the portable session sidecar.
                feedback_path = session_path + "/feedback"
                done_event = next(item for item in
                                  event_document["data"]["items"]
                                  if item["kind"] == "model_done" and
                                  item["success"])
                done_id = done_event["event_id"]
                status, _, body = request(port, "GET", feedback_path)
                assert status == 200 and json.loads(body)["data"]["items"] == [], (
                    status, body)
                feedback_body = json.dumps({"event_id": done_id,
                                            "value": "good"}).encode()
                status, _, body = request(
                    port, "PUT", feedback_path, body=feedback_body,
                    headers={"Content-Type": "application/json"})
                assert status == 200, (status, body)
                assert json.loads(body)["data"]["items"] == [
                    {"event_id": done_id, "value": "good"}], body
                sidecar = home / f"sessions/api-project/{session_id}/feedback.json"
                assert json.loads(sidecar.read_text(encoding="utf-8")) == {
                    "schema_version": 1,
                    "items": [{"event_id": done_id, "value": "good"}],
                }
                assert json.loads(request(port, "GET", feedback_path)[2])[
                    "data"]["items"][0]["value"] == "good"
                status, _, body = request(
                    port, "PUT", feedback_path,
                    body=json.dumps({"event_id": done_id,
                                     "value": "none"}).encode(),
                    headers={"Content-Type": "application/json"})
                assert status == 200 and json.loads(body)["data"]["items"] == [], (
                    status, body)
                status, _, body = request(
                    port, "PUT", feedback_path, body=feedback_body,
                    headers={"Content-Type": "application/json"})
                assert status == 200, (status, body)
                status, _, body = request(
                    port, "PUT", feedback_path,
                    body=b'{"event_id":999999999,"value":"bad"}',
                    headers={"Content-Type": "application/json"})
                assert status == 422 and json.loads(body)["error"][
                    "code"] == "feedback_event_invalid", (status, body)

                status, _, body = request(
                    port, "GET", "/api/v1/diagnostics?fixture=recovery")
                assert status == 200, (status, body)
                all_sessions = json.loads(request(
                    port, "GET", "/api/v1/sessions")[2])["data"]["items"]
                recovery_session = next(item for item in all_sessions
                                        if item["project_id"] == "recovery-probe")
                recovery_path = (
                    "/api/v1/projects/recovery-probe/sessions/"
                    f"{recovery_session['id']}/recovery")
                resume_path = recovery_path.removesuffix("/recovery") + "/resume"
                status, headers, body = request(port, "GET", recovery_path)
                recovery_document = json.loads(body)
                assert status == 200, (status, body)
                assert_common(headers, recovery_document)
                recovery = recovery_document["data"]
                assert recovery["project_id"] == "recovery-probe", recovery
                assert recovery["session_id"] == recovery_session["id"], recovery
                assert recovery["resume_required"] is True, recovery
                assert recovery["total"] == 1, recovery
                assert recovery["last_sequence"] > 0, recovery
                assert recovery["catalog_generation"] > 0, recovery
                assert re.fullmatch(r"[0-9a-f]{64}",
                                    recovery["recovery_token"]), recovery
                pending = recovery["items"][0]
                assert pending["tool_call_id"] == "recovery-edit-call", pending
                assert pending["tool"] == "edit", pending
                assert pending["effects"] == ["workspace_write"], pending
                assert pending["tool_available"] is True, pending
                assert pending["automatic_retry_safe"] is False, pending
                assert json.loads(pending["arguments_json"])["path"] == (
                    "recovery-probe.txt"), pending
                status, _, body = request(port, "HEAD", recovery_path)
                assert status == 200 and body == b"", (status, body)
                status, headers, body = request(port, "OPTIONS", recovery_path)
                assert status == 200 and headers["allow"] == (
                    "GET, HEAD, OPTIONS"), (status, headers, body)
                status, headers, body = request(port, "OPTIONS", resume_path)
                assert status == 200 and headers["allow"] == "POST, OPTIONS", (
                    status, headers, body)
                abandon_path = recovery_path.removesuffix("/recovery") + "/abandon"
                status, headers, body = request(port, "OPTIONS", abandon_path)
                assert status == 200 and headers["allow"] == "POST, OPTIONS", (
                    status, headers, body)
                status, _, body = request(
                    port, "POST", abandon_path,
                    body=json.dumps({"revision": recovery["revision"],
                                     "last_sequence": recovery["last_sequence"]}).encode(),
                    headers={"Content-Type": "application/json"})
                assert status == 409, (status, body)
                assert json.loads(body)["error"]["code"] == (
                    "recovery_state_conflict")

                resume_headers = {"Content-Type": "application/json"}
                invalid_resume = json.dumps({
                    "recovery_token": recovery["recovery_token"],
                    "decisions": [
                        {"tool_call_id": "recovery-edit-call",
                         "action": "retry"},
                        {"tool_call_id": "recovery-edit-call",
                         "action": "record_uncertain"},
                    ],
                }).encode()
                status, _, body = request(port, "POST", resume_path,
                                          body=invalid_resume,
                                          headers=resume_headers)
                assert status == 422, (status, body)
                assert json.loads(body)["error"]["code"] == (
                    "recovery_resume_invalid")
                stale_resume = json.dumps({
                    "recovery_token": "0" * 64,
                    "decisions": [{
                        "tool_call_id": "recovery-edit-call",
                        "action": "record_uncertain",
                    }],
                }).encode()
                status, _, body = request(port, "POST", resume_path,
                                          body=stale_resume,
                                          headers=resume_headers)
                assert status == 409, (status, body)
                assert json.loads(body)["error"]["code"] == (
                    "recovery_state_conflict")

                status, _, body = request(port, "GET", recovery_path)
                assert status == 200, (status, body)
                recovery = json.loads(body)["data"]
                assert recovery["total"] == 1, recovery

                resume_document = json.dumps({
                    "recovery_token": recovery["recovery_token"],
                    "decisions": [{
                        "tool_call_id": "recovery-edit-call",
                        "action": "record_uncertain",
                    }],
                }).encode()
                status, headers, body = request(port, "POST", resume_path,
                                                body=resume_document,
                                                headers=resume_headers)
                resumed_document = json.loads(body)
                assert status == 202, (status, body)
                assert_common(headers, resumed_document)
                resumed = resumed_document["data"]
                assert resumed["resume"] is True, resumed
                resumed_path = f"/api/v1/runs/{resumed['id']}"
                approval_deadline = time.monotonic() + 3.0
                verification_approval = None
                while time.monotonic() < approval_deadline:
                    approval_data = json.loads(request(
                        port, "GET", "/api/v1/approvals")[2])["data"]
                    verification_approval = next((item for item in
                        approval_data["items"] if item["tool"] == "exec" and
                        item["tool_call_id"] == "recovery-verify-call"), None)
                    if verification_approval is not None:
                        break
                    time.sleep(0.01)
                assert verification_approval is not None, approval_data
                status, _, body = request(
                    port, "PUT",
                    f"/api/v1/approvals/{verification_approval['id']}",
                    body=b'{"decision":"allow"}', headers=resume_headers)
                assert status == 200, (status, body)
                deadline = time.monotonic() + 5.0
                while not resumed["terminal"] and time.monotonic() < deadline:
                    time.sleep(0.01)
                    status, headers, body = request(port, "GET", resumed_path)
                    resumed = json.loads(body)["data"]
                    assert status == 200, (status, body)
                assert resumed["terminal"] is True, resumed
                assert resumed["state"] == "succeeded", resumed
                assert resumed["resume"] is True, resumed
                assert resumed["tool_calls"] == 1, resumed
                assert not (base / "recovery-probe.txt").exists(), list(
                    base.iterdir())
                status, _, body = request(port, "GET", recovery_path)
                assert status == 200, (status, body)
                resolved_recovery = json.loads(body)["data"]
                assert resolved_recovery["total"] == 0, resolved_recovery
                assert resolved_recovery["resume_required"] is False, (
                    resolved_recovery)

                ask_recovery_session = next(item for item in
                    json.loads(request(port, "GET", "/api/v1/sessions")[2])[
                        "data"]["items"] if item["project_id"] ==
                            "ask-recovery-probe")
                ask_recovery_base = (
                    "/api/v1/projects/ask-recovery-probe/sessions/"
                    f"{ask_recovery_session['id']}")
                status, _, body = request(port, "GET",
                    ask_recovery_base + "/recovery")
                assert status == 200, (status, body)
                ask_recovery = json.loads(body)["data"]
                assert ask_recovery["resume_required"] is True, ask_recovery
                assert ask_recovery["total"] == 1, ask_recovery
                pending_ask = ask_recovery["items"][0]
                assert pending_ask["tool"] == "ask_user" and pending_ask[
                    "automatic_retry_safe"] is True, pending_ask
                status, _, body = request(port, "POST",
                    ask_recovery_base + "/resume",
                    body=json.dumps({
                        "recovery_token": ask_recovery["recovery_token"],
                        "decisions": [],
                    }).encode(), headers=resume_headers)
                assert status == 202, (status, body)
                ask_recovered_run = json.loads(body)["data"]
                ask_recovery_asks = ask_recovery_base + "/asks"
                deadline = time.monotonic() + 5.0
                asks = []
                while not asks and time.monotonic() < deadline:
                    asks = json.loads(request(port, "GET",
                        ask_recovery_asks)[2])["data"]["items"]
                    if not asks: time.sleep(0.02)
                assert asks and asks[0]["question"] == "Should I continue?", asks
                status, _, body = request(port, "PUT",
                    ask_recovery_asks + "/" + str(asks[0]["id"]),
                    body=b'{"answer":"Yes"}', headers=resume_headers)
                assert status == 200, (status, body)
                deadline = time.monotonic() + 3.0
                ask_verification = None
                while time.monotonic() < deadline:
                    approval_data = json.loads(request(port, "GET",
                        "/api/v1/approvals")[2])["data"]
                    ask_verification = next((item for item in
                        approval_data["items"] if item["tool_call_id"] ==
                        "ask-recovery-verify-call"), None)
                    if ask_verification is not None: break
                    time.sleep(0.02)
                assert ask_verification is not None, approval_data
                status, _, body = request(port, "PUT",
                    f'/api/v1/approvals/{ask_verification["id"]}',
                    body=b'{"decision":"allow"}', headers=resume_headers)
                assert status == 200, (status, body)
                ask_recovered_path = f'/api/v1/runs/{ask_recovered_run["id"]}'
                deadline = time.monotonic() + 5.0
                while not ask_recovered_run["terminal"] and time.monotonic() < deadline:
                    time.sleep(0.02)
                    ask_recovered_run = json.loads(request(port, "GET",
                        ask_recovered_path)[2])["data"]
                assert ask_recovered_run["state"] == "succeeded", (
                    ask_recovered_run)
                assert ask_recovered_run["resume"] is True, ask_recovered_run

                status, _, body = request(port, "POST", run_path,
                    body=b'{"prompt":"TODO probe","timeout_ms":10000}',
                    headers={"Content-Type": "application/json"})
                assert status == 202, (status, body)
                todo_run = json.loads(body)["data"]
                todo_run_path = f'/api/v1/runs/{todo_run["id"]}'
                deadline = time.monotonic() + 12.0
                while not todo_run["terminal"] and time.monotonic() < deadline:
                    time.sleep(0.01)
                    todo_run = json.loads(request(
                        port, "GET", todo_run_path)[2])["data"]
                assert todo_run["state"] == "succeeded", (
                    todo_run, ModelHandler.calls, ModelHandler.todo_sent)
                assert ModelHandler.todo_sent, ModelHandler.calls
                assert json.loads(request(port, "GET", "/api/v1/approvals")[2])[
                    "data"]["total"] == 0
                projected = json.loads(request(port, "GET", todo_path)[2])[
                    "data"]
                assert projected["event_id"] > 8 and projected["items"] == [
                    {"text": "Inspect repository", "done": True},
                    {"text": "Verify result", "done": False}], projected
                todo_events = json.loads(request(port, "GET",
                    session_path + "/events?after=0&limit=32")[2])[
                        "data"]["items"]
                assert any(event["kind"] == "agent_start" and
                           event["user_message_sequence"] > 0
                           for event in todo_events), todo_events
                assert any(event["kind"] == "tool_done" and
                           event["tool_name"] == "mdo.todo" and
                           event["success"] for event in todo_events), todo_events

                asks_path = session_path + "/asks"
                assert json.loads(request(port, "GET", asks_path)[2])[
                    "data"]["items"] == []
                status, _, body = request(port, "POST", run_path,
                    body=b'{"prompt":"ASK probe","timeout_ms":10000}',
                    headers={"Content-Type": "application/json"})
                assert status == 202, (status, body)
                ask_run = json.loads(body)["data"]
                ask_run_path = f'/api/v1/runs/{ask_run["id"]}'
                deadline = time.monotonic() + 5.0
                asks = []
                while not asks and time.monotonic() < deadline:
                    time.sleep(0.02)
                    asks = json.loads(request(port, "GET", asks_path)[2])[
                        "data"]["items"]
                assert len(asks) == 1, (asks, ModelHandler.calls)
                ask = asks[0]
                assert ask["question"] == "Which route should I take?", ask
                assert ask["options"] == ["Fast", "Careful"], ask
                assert json.loads(request(port, "GET", asks_path)[2])[
                    "data"]["items"][0]["id"] == ask["id"]
                ask_item_path = asks_path + "/" + str(ask["id"])
                status, _, body = request(port, "PUT", ask_item_path,
                    body=b'{"answer":""}',
                    headers={"Content-Type": "application/json"})
                assert status == 422, (status, body)
                status, _, body = request(port, "PUT", asks_path + "/999999",
                    body=b'{"answer":"Fast"}',
                    headers={"Content-Type": "application/json"})
                assert status == 404, (status, body)
                status, _, body = request(port, "PUT",
                    ask_recovery_asks + "/" + str(ask["id"]),
                    body=b'{"answer":"Wrong session"}',
                    headers={"Content-Type": "application/json"})
                assert status == 404, (status, body)
                status, _, body = request(port, "PUT", ask_item_path,
                    body=b'{"answer":"My own route"}',
                    headers={"Content-Type": "application/json"})
                assert status == 200, (status, body)
                assert json.loads(request(port, "GET", asks_path)[2])[
                    "data"]["items"] == []
                deadline = time.monotonic() + 6.0
                while not ask_run["terminal"] and time.monotonic() < deadline:
                    time.sleep(0.02)
                    ask_run = json.loads(request(port, "GET",
                        ask_run_path)[2])["data"]
                assert ask_run["state"] == "succeeded", ask_run
                ask_events = json.loads(request(port, "GET",
                    session_path + f'/events?after={projected["event_id"]}&limit=32')[2])[
                        "data"]["items"]
                assert any(event["kind"] == "tool_done" and
                           event["tool_name"] == "ask_user" and
                           event["success"] and "My own route" in event["text"]
                           for event in ask_events), ask_events

                status, _, body = request(port, "POST", run_path,
                    body=b'{"prompt":"ASK cancel probe","timeout_ms":10000}',
                    headers={"Content-Type": "application/json"})
                assert status == 202, (status, body)
                cancel_ask_run = json.loads(body)["data"]
                deadline = time.monotonic() + 5.0
                while time.monotonic() < deadline:
                    asks = json.loads(request(port, "GET", asks_path)[2])[
                        "data"]["items"]
                    if asks: break
                    time.sleep(0.02)
                assert asks and asks[0]["question"] == "Cancel this question?", asks
                status, _, body = request(port, "DELETE",
                    f'/api/v1/runs/{cancel_ask_run["id"]}')
                assert status == 200, (status, body)
                deadline = time.monotonic() + 5.0
                while time.monotonic() < deadline:
                    asks = json.loads(request(port, "GET", asks_path)[2])[
                        "data"]["items"]
                    if not asks: break
                    time.sleep(0.02)
                assert asks == [], asks

                schedule_path = "/api/v1/schedules/api-schedule"
                status, headers, body = request(
                    port, "POST", "/api/v1/schedules",
                    body=b'{"label":"missing fields","unknown":true}',
                    headers={"Content-Type": "application/json"})
                document = json.loads(body)
                assert status == 422, (status, body)
                assert document["error"]["code"] == "schedule_invalid", (
                    document)

                schedule_input = "Scheduled API prompt"
                schedule_start = 4102444800000000
                schedule_definition = {
                    "id": "api-schedule",
                    "label": "API schedule",
                    "notify": "desktop",
                    "project_id": "api-project",
                    "agent_id": "mdo.default",
                    "model_id": "ling-3.0-tiny",
                    "protocol": "openai-responses",
                    "reasoning_effort": "medium",
                    "max_output_tokens": 1024,
                    "workspace_root": str(base),
                    "input": schedule_input,
                    "frequency": "daily",
                    "interval": 1,
                    "start_at": schedule_start,
                    "weekday_mask": 0,
                    "timezone": "utc",
                    "utc_offset_seconds": 0,
                    "fold_policy": "earlier",
                    "misfire_policy": "run_once",
                    "misfire_grace_seconds": 60,
                    "max_catch_up": 1,
                    "overlap_policy": "skip",
                    "max_concurrent_runs": 1,
                    "enabled": True,
                }
                status, headers, body = request(
                    port, "POST", "/api/v1/schedules",
                    body=json.dumps(schedule_definition).encode(),
                    headers={"Content-Type": "application/json"})
                document = json.loads(body)
                assert status == 201, (status, body)
                assert_common(headers, document)
                schedule = document["data"]
                assert schedule["id"] == "api-schedule", schedule
                assert schedule["input"] == schedule_input, schedule
                assert schedule["input_bytes"] == len(schedule_input), schedule
                assert schedule["next_occurrence_at"] == schedule_start, schedule
                assert schedule["enabled"] is True, schedule
                assert schedule["runnable"] is True, schedule
                assert schedule["revision"] == 1, schedule
                schedule_etag = headers["etag"]
                assert schedule_etag == '"mdo-schedule-api-schedule-1"', (
                    headers)
                definition_path = home / "schedules/api-schedule.json"
                assert definition_path.is_file(), list(home.rglob("*"))
                definition_text = definition_path.read_text(encoding="utf-8")
                assert "bounded-api-test-key" not in definition_text, (
                    definition_text)

                status, headers, body = request(port, "GET", schedule_path)
                detail_document = json.loads(body)
                assert status == 200, (status, body)
                assert_common(headers, detail_document)
                assert detail_document["data"] == schedule, detail_document
                assert headers["etag"] == schedule_etag, headers
                history_path = home / "schedules/history/api-schedule.jsonl"
                assert not history_path.exists(), history_path
                status, headers, body = request(
                    port, "GET", schedule_path + "/history")
                empty_history = json.loads(body)["data"]
                assert status == 200 and empty_history["items"] == [], (
                    status, body)
                assert not history_path.exists(), history_path
                history_path.parent.mkdir(parents=True, exist_ok=True)
                history_records = [{
                    "schema_version": 1, "task_id": number,
                    "agent_run_id": number + 100,
                    "schedule_id": "api-schedule",
                    "scheduled_at_us": schedule_start + number * 1000000,
                    "finished_at_us": schedule_start + number * 1000000 + 1,
                    "result": 0 if number % 2 else -1,
                    "text": "中文" * 400 if number == 35 else f"result {number}",
                } for number in range(1, 36)]
                history_path.write_text("".join(
                    json.dumps(row, ensure_ascii=False) + "\n"
                    for row in history_records), encoding="utf-8")
                status, headers, body = request(
                    port, "GET", schedule_path + "/history")
                history_data = json.loads(body)["data"]
                assert status == 200, (status, body)
                assert history_data["count"] == 32, history_data
                assert history_data["has_more"] is True, history_data
                assert history_data["items"][0]["task_id"] == 35, history_data
                assert history_data["items"][-1]["task_id"] == 4, history_data
                assert history_data["items"][0]["result"] == "succeeded", (
                    history_data)
                assert history_data["items"][0]["text_truncated"] is True, (
                    history_data)
                assert len(history_data["items"][0]["text"].encode()) <= 1024, (
                    history_data)
                schedule_list = json.loads(request(
                    port, "GET", "/api/v1/schedules")[2])["data"]
                listed_schedule = next(
                    item for item in schedule_list["items"]
                    if item["id"] == "api-schedule")
                assert listed_schedule["input_bytes"] == len(schedule_input), (
                    listed_schedule)
                assert "input" not in listed_schedule, listed_schedule

                replacement = dict(schedule_definition)
                replacement["label"] = "Updated API schedule"
                replacement["input"] = "Updated scheduled API prompt"
                mismatched_replacement = dict(replacement)
                mismatched_replacement["id"] = "other-schedule"
                status, headers, body = request(
                    port, "PUT", schedule_path,
                    body=json.dumps(mismatched_replacement).encode(),
                    headers={"Content-Type": "application/json",
                             "If-Match": schedule_etag})
                document = json.loads(body)
                assert status == 422, (status, body)
                assert document["error"]["code"] == "schedule_invalid", (
                    document)
                status, headers, body = request(
                    port, "PUT", schedule_path,
                    body=json.dumps(replacement).encode(),
                    headers={"Content-Type": "application/json",
                             "If-Match": schedule_etag})
                document = json.loads(body)
                assert status == 200, (status, body)
                schedule = document["data"]
                assert schedule["label"] == "Updated API schedule", schedule
                assert schedule["input"] == replacement["input"], schedule
                assert schedule["revision"] == 2, schedule
                assert schedule["runtime_generation"] > 0, schedule
                schedule_etag = headers["etag"]
                assert schedule_etag == '"mdo-schedule-api-schedule-2"', (
                    headers)
                persisted_replacement = json.loads(
                    definition_path.read_text(encoding="utf-8"))
                assert persisted_replacement["revision"] == 2, (
                    persisted_replacement)
                assert persisted_replacement["input"] == replacement["input"], (
                    persisted_replacement)

                status, headers, body = request(
                    port, "PUT", schedule_path + "/enabled",
                    body=b'{"enabled":false}',
                    headers={"Content-Type": "application/json"})
                document = json.loads(body)
                assert status == 428, (status, body)
                assert document["error"]["code"] == "precondition_required", (
                    document)
                status, headers, body = request(
                    port, "PUT", schedule_path + "/enabled",
                    body=b'{"enabled":false}',
                    headers={"Content-Type": "application/json",
                             "If-Match": '"mdo-schedule-api-schedule-99"'})
                document = json.loads(body)
                assert status == 412, (status, body)
                assert document["error"]["code"] == "revision_conflict", (
                    document)
                status, headers, body = request(
                    port, "PUT", schedule_path + "/enabled",
                    body=b'{"enabled":false}',
                    headers={"Content-Type": "application/json",
                             "If-Match": schedule_etag})
                document = json.loads(body)
                assert status == 200, (status, body)
                schedule = document["data"]
                assert schedule["enabled"] is False, schedule
                assert schedule["runnable"] is False, schedule
                assert schedule["revision"] == 3, schedule
                disabled_etag = headers["etag"]
                assert disabled_etag == '"mdo-schedule-api-schedule-3"', (
                    headers)

                status, headers, body = request(
                    port, "POST", schedule_path + "/run")
                document = json.loads(body)
                assert status == 428, (status, body)
                assert document["error"]["code"] == "precondition_required", (
                    document)
                status, headers, body = request(
                    port, "POST", schedule_path + "/run", body=b"{}",
                    headers={"Content-Type": "application/json",
                             "If-Match": disabled_etag})
                document = json.loads(body)
                assert status == 400, (status, body)
                assert document["error"]["code"] == "body_not_allowed", (
                    document)
                status, headers, body = request(
                    port, "POST", schedule_path + "/run",
                    headers={"If-Match": schedule_etag})
                document = json.loads(body)
                assert status == 412, (status, body)
                assert document["error"]["code"] == "revision_conflict", (
                    document)

                status, headers, body = request(
                    port, "DELETE", schedule_path, body=b"{}",
                    headers={"Content-Type": "application/json",
                             "If-Match": disabled_etag})
                document = json.loads(body)
                assert status == 400, (status, body)
                assert document["error"]["code"] == "body_not_allowed", (
                    document)
                status, headers, body = request(
                    port, "DELETE", schedule_path,
                    headers={"If-Match": schedule_etag})
                document = json.loads(body)
                assert status == 412, (status, body)
                assert document["error"]["code"] == "revision_conflict", (
                    document)
                status, headers, body = request(
                    port, "DELETE", schedule_path,
                    headers={"If-Match": disabled_etag})
                document = json.loads(body)
                assert status == 200, (status, body)
                assert document["data"]["removed"] is True, document
                assert document["data"]["revision"] == 3, document
                assert not definition_path.exists(), definition_path
                status, headers, body = request(port, "GET", schedule_path)
                document = json.loads(body)
                assert status == 404, (status, body)
                assert document["error"]["code"] == "schedule_not_found", (
                    document)
                status, headers, body = request(
                    port, "OPTIONS", "/api/v1/schedules")
                assert status == 200 and headers["allow"] == (
                    "GET, HEAD, POST, OPTIONS"), (status, headers, body)
                status, headers, body = request(
                    port, "OPTIONS", schedule_path)
                assert status == 200 and headers["allow"] == (
                    "GET, HEAD, PUT, DELETE, OPTIONS"), (
                    status, headers, body)
                status, headers, body = request(
                    port, "OPTIONS", schedule_path + "/enabled")
                assert status == 200 and headers["allow"] == (
                    "PUT, OPTIONS"), (status, headers, body)
                status, headers, body = request(
                    port, "OPTIONS", schedule_path + "/history")
                assert status == 200 and headers["allow"] == (
                    "GET, HEAD, OPTIONS"), (status, headers, body)
                status, headers, body = request(
                    port, "OPTIONS", schedule_path + "/run")
                assert status == 200 and headers["allow"] == (
                    "POST, OPTIONS"), (status, headers, body)

                manual_definition = dict(schedule_definition)
                manual_definition.update({
                    "id": "manual-api", "label": "Manual API schedule",
                    "enabled": False, "frequency": "once",
                })
                manual_path = "/api/v1/schedules/manual-api"
                status, headers, body = request(
                    port, "POST", "/api/v1/schedules",
                    body=json.dumps(manual_definition).encode(),
                    headers={"Content-Type": "application/json"})
                assert status == 201, (status, body)
                manual_etag = headers["etag"]
                status, headers, body = request(
                    port, "POST", manual_path + "/run",
                    headers={"If-Match": manual_etag})
                assert status == 202, (status, body)
                manual_run = json.loads(body)["data"]
                assert manual_run["state"] == "running", manual_run
                assert manual_run["task_id"] > 0 and manual_run["agent_run_id"] > 0, (
                    manual_run)
                status, headers, body = request(port, "GET", manual_path)
                manual_after_etag = headers["etag"]
                manual_after = json.loads(body)["data"]
                assert status == 200 and manual_after["revision"] == 2, (
                    status, body)
                assert manual_after["enabled"] is False, manual_after
                assert manual_after["next_occurrence_at"] == schedule_start, (
                    manual_after)
                status, _, body = request(
                    port, "POST", manual_path + "/run",
                    headers={"If-Match": manual_etag})
                assert status == 412, (status, body)
                manual_history = []
                deadline = time.monotonic() + 5.0
                while time.monotonic() < deadline:
                    status, _, body = request(
                        port, "GET", manual_path + "/history")
                    manual_history = json.loads(body)["data"]["items"]
                    if manual_history: break
                    time.sleep(0.02)
                assert status == 200 and len(manual_history) == 1, (
                    status, body)
                assert manual_history[0]["task_id"] == manual_run["task_id"], (
                    manual_history)
                assert manual_history[0]["agent_run_id"] == manual_run["agent_run_id"], (
                    manual_history)
                assert manual_history[0]["result"] == "succeeded", (
                    manual_history)
                status, _, body = request(
                    port, "DELETE", manual_path,
                    headers={"If-Match": manual_after_etag})
                assert status == 200, (status, body)

                status, headers, body = request(
                    port, "PATCH", session_path,
                    body=b'{"title":"Renamed session"}',
                    headers={"Content-Type": "application/json"})
                document = json.loads(body)
                assert status == 428, (status, body)
                assert document["error"]["code"] == "precondition_required", (
                    document)
                status, headers, body = request(
                    port, "PATCH", session_path,
                    body=b'{"title":"Renamed session"}',
                    headers={"Content-Type": "application/json",
                             "If-Match": 'W/"mdo-session-invalid-1"'})
                document = json.loads(body)
                assert status == 400, (status, body)
                assert document["error"]["code"] == "invalid_precondition", (
                    document)
                status, headers, body = request(
                    port, "PATCH", session_path,
                    body=b'{"title":"Renamed session"}',
                    headers={"Content-Type": "application/json",
                             "If-Match": '"mdo-session-other-1"'})
                document = json.loads(body)
                assert status == 412, (status, body)
                assert document["error"]["code"] == "revision_conflict", (
                    document)
                status, headers, body = request(
                    port, "PATCH", session_path,
                    body=b'{"title":"Renamed session","pinned":true}',
                    headers={"Content-Type": "application/json",
                             "If-Match": session_etag})
                document = json.loads(body)
                assert status == 422, (status, body)
                assert document["error"]["code"] == "session_patch_invalid", (
                    document)

                status, headers, body = request(
                    port, "PATCH", session_path,
                    body=b'{"title":"Renamed session"}',
                    headers={"Content-Type": "application/json",
                             "If-Match": session_etag})
                document = json.loads(body)
                assert status == 200, (status, body)
                session = document["data"]
                assert session["title"] == "Renamed session", session
                assert session["revision"] == 2, session
                renamed_etag = headers["etag"]
                assert renamed_etag == (
                    f'"mdo-session-{session_id}-2"'), headers

                status, headers, body = request(
                    port, "PATCH", session_path, body=b'{"pinned":true}',
                    headers={"Content-Type": "application/json",
                             "If-Match": session_etag})
                document = json.loads(body)
                assert status == 412, (status, body)
                assert document["error"]["code"] == "revision_conflict", (
                    document)

                status, headers, body = request(
                    port, "PATCH", session_path, body=b'{"pinned":true}',
                    headers={"Content-Type": "application/json",
                             "If-Match": renamed_etag})
                document = json.loads(body)
                assert status == 200, (status, body)
                assert document["data"]["pinned"] is True, document
                assert document["data"]["revision"] == 3, document
                current_etag = headers["etag"]

                status, headers, body = request(
                    port, "PATCH", session_path, body=b'{"archived":true}',
                    headers={"Content-Type": "application/json",
                             "If-Match": current_etag})
                document = json.loads(body)
                assert status == 200, (status, body)
                assert document["data"]["status"] == "archived", document
                assert document["data"]["revision"] == 4, document
                current_etag = headers["etag"]

                status, headers, body = request(
                    port, "DELETE", session_path, body=b"{}",
                    headers={"Content-Type": "application/json",
                             "If-Match": current_etag})
                document = json.loads(body)
                assert status == 400, (status, body)
                assert document["error"]["code"] == "body_not_allowed", document
                status, headers, body = request(
                    port, "DELETE", session_path,
                    headers={"If-Match": current_etag})
                document = json.loads(body)
                assert status == 200, (status, body)
                assert document["data"]["status"] == "trash", document
                assert document["data"]["pinned"] is False, document
                assert document["data"]["revision"] == 5, document
                current_etag = headers["etag"]

                status, headers, body = request(
                    port, "PATCH", session_path, body=b'{"archived":false}',
                    headers={"Content-Type": "application/json",
                             "If-Match": current_etag})
                document = json.loads(body)
                assert status == 409, (status, body)
                assert document["error"]["code"] == "session_state_conflict", (
                    document)

                restore_path = session_path + "/restore"
                status, headers, body = request(
                    port, "POST", restore_path, body=b"{}",
                    headers={"Content-Type": "application/json",
                             "If-Match": current_etag})
                document = json.loads(body)
                assert status == 400, (status, body)
                assert document["error"]["code"] == "body_not_allowed", document
                status, headers, body = request(
                    port, "POST", restore_path,
                    headers={"If-Match": current_etag})
                document = json.loads(body)
                assert status == 200, (status, body)
                assert document["data"]["status"] == "archived", document
                assert document["data"]["revision"] == 6, document
                current_etag = headers["etag"]

                status, headers, body = request(
                    port, "PATCH", session_path, body=b'{"archived":false}',
                    headers={"Content-Type": "application/json",
                             "If-Match": current_etag})
                document = json.loads(body)
                assert status == 200, (status, body)
                assert document["data"]["status"] == "active", document
                assert document["data"]["revision"] == 7, document
                current_etag = headers["etag"]

                status, headers, body = request(port, "GET", session_path)
                document = json.loads(body)
                assert status == 200, (status, body)
                assert document["data"]["revision"] == 7, document
                assert headers["etag"] == current_etag, headers

                history_path = session_path + "/history"
                status, headers, body = request(port, "GET", history_path)
                document = json.loads(body)
                assert status == 200, (status, body)
                history = document["data"]
                assert history["session_id"] == session_id, history
                assert history["revision"] == 7, history
                assert isinstance(history["last_sequence"], int), history
                assert headers["etag"] == current_etag, headers

                export_path = session_path + "/export"
                status, headers, body = request(port, "GET", export_path)
                assert status == 200, (status, body)
                exported = json.loads(body)
                assert exported["export_schema"] == 1, exported
                assert exported["meta"]["id"] == session_id, exported
                assert isinstance(exported["snapshot"], dict), exported
                assert headers["etag"] == current_etag, headers
                assert headers["content-type"] == (
                    "application/octet-stream"), headers
                assert headers["content-disposition"] == (
                    f'attachment; filename="mdo-session-{session_id}.json"'), (
                    headers)
                assert headers["cache-control"] == "no-store", headers

                fork_path = session_path + "/fork"
                status, headers, body = request(
                    port, "POST", fork_path,
                    body=b'{"title":"Forked API session"}',
                    headers={"Content-Type": "application/json",
                             "If-Match": current_etag})
                document = json.loads(body)
                assert status == 201, (status, body)
                forked = document["data"]
                assert forked["id"] != session_id, forked
                assert forked["parent_session_id"] == session_id, forked
                assert forked["title"] == "Forked API session", forked
                assert forked["revision"] == 1, forked
                assert headers["etag"] == (
                    f'"mdo-session-{forked["id"]}-1"'), headers
                fork_events = json.loads(request(port, "GET",
                    f'/api/v1/projects/api-project/sessions/{forked["id"]}'
                    '/events?after=0&limit=32')[2])["data"]["items"]
                assert any(event["kind"] == "agent_start" and
                           event["user_message_sequence"] > 0
                           for event in fork_events), fork_events
                fork_todo = json.loads(request(port, "GET",
                    f'/api/v1/projects/api-project/sessions/{forked["id"]}'
                    '/todo')[2])["data"]
                assert fork_todo["items"] == [
                    {"text": "Inspect repository", "done": True},
                    {"text": "Verify result", "done": False},
                ], fork_todo

                journal_before_trim = session_events(port, session_path)
                cutoff_start = next(event for event in journal_before_trim
                    if event["kind"] == "agent_start" and
                    event["agent_depth"] == 0 and
                    event["text"] == "ASK probe")
                assert cutoff_start["user_message_sequence"] > 1
                removed_done_id = next(event["event_id"] for event in
                    journal_before_trim if event["kind"] == "model_done" and
                    event["success"] and
                    event["event_id"] > cutoff_start["event_id"])
                status, _, body = request(port, "PUT", feedback_path,
                    body=json.dumps({"event_id": removed_done_id,
                                     "value": "bad"}).encode(),
                    headers={"Content-Type": "application/json"})
                assert status == 200, (status, body)
                # Simulate a newer projection belonging to a turn that the
                # next truncate removes, while retaining the earlier TODO.
                todo_file.write_text(json.dumps({
                    "schema_version": 1,
                    "event_id": cutoff_start["event_id"],
                    "items": [{"text": "Removed plan", "done": False}],
                }), encoding="utf-8")

                truncate_path = session_path + "/truncate"
                status, headers, body = request(port, "POST", truncate_path,
                    body=json.dumps({"through_sequence":
                        cutoff_start["user_message_sequence"] - 1}).encode(),
                    headers={"Content-Type": "application/json",
                             "If-Match": current_etag})
                document = json.loads(body)
                assert status == 200 and document["data"]["revision"] == 8, (
                    status, body)
                current_etag = headers["etag"]
                restored_todo = json.loads(request(port, "GET", todo_path)[2])[
                    "data"]
                assert restored_todo["items"] == [
                    {"text": "Inspect repository", "done": True},
                    {"text": "Verify result", "done": False},
                ], restored_todo
                retained_events = session_events(port, session_path)
                assert not any(event["kind"] == "agent_start" and
                    event["text"] == "ASK probe" for event in retained_events)
                assert retained_events[-1]["kind"] == "history_truncated" and (
                    retained_events[-1]["source_event_id"] ==
                    cutoff_start["event_id"]), retained_events[-1]
                expected_feedback = [{"event_id": done_id, "value": "good"}]
                assert json.loads(request(port, "GET", feedback_path)[2])[
                    "data"]["items"] == expected_feedback
                assert json.loads(sidecar.read_text(encoding="utf-8"))[
                    "items"] == expected_feedback
                # A GET repairs a stale sidecar left by an interrupted write.
                sidecar.write_text(json.dumps({"schema_version": 1,
                    "items": expected_feedback + [{"event_id": removed_done_id,
                                                   "value": "bad"}]}),
                    encoding="utf-8")
                assert json.loads(request(port, "GET", feedback_path)[2])[
                    "data"]["items"] == expected_feedback
                assert json.loads(sidecar.read_text(encoding="utf-8"))[
                    "items"] == expected_feedback
                todo_file.write_text(json.dumps({
                    "schema_version": 1,
                    "event_id": cutoff_start["event_id"],
                    "items": [{"text": "Interrupted repair", "done": False}],
                }), encoding="utf-8")
                status, headers, body = request(port, "POST", truncate_path,
                    body=json.dumps({"through_sequence":
                        cutoff_start["user_message_sequence"] - 1}).encode(),
                    headers={"Content-Type": "application/json",
                             "If-Match": current_etag})
                document = json.loads(body)
                assert status == 200 and document["data"]["revision"] == 9, (
                    status, body)
                current_etag = headers["etag"]
                repaired_todo = json.loads(request(port, "GET", todo_path)[2])[
                    "data"]
                assert repaired_todo["items"] == [
                    {"text": "Inspect repository", "done": True},
                    {"text": "Verify result", "done": False},
                ], repaired_todo
                status, headers, body = request(
                    port, "POST", truncate_path, body=b"{}",
                    headers={"Content-Type": "application/json",
                             "If-Match": current_etag})
                document = json.loads(body)
                assert status == 422, (status, body)
                assert document["error"]["code"] == (
                    "session_truncate_invalid"), document

                clear_path = session_path + "/clear"
                status, headers, body = request(port, "POST", clear_path)
                document = json.loads(body)
                assert status == 428, (status, body)
                assert document["error"]["code"] == (
                    "precondition_required"), document
                status, headers, body = request(
                    port, "POST", clear_path,
                    headers={"If-Match": current_etag})
                document = json.loads(body)
                assert status == 200, (status, body)
                assert document["data"]["revision"] == 10, document
                current_etag = headers["etag"]
                status, _, body = request(port, "GET", todo_path)
                assert status == 200 and json.loads(body)["data"] == {
                    "schema_version": 1, "event_id": 0, "items": [],
                }, (status, body)
                assert json.loads(request(port, "GET", feedback_path)[2])[
                    "data"]["items"] == []
                assert json.loads(sidecar.read_text(encoding="utf-8"))[
                    "items"] == []
                cleared_events = json.loads(request(port, "GET",
                    session_path + "/events?after=0&limit=32")[2])[
                        "data"]["items"]
                assert len(cleared_events) == 1 and (
                    cleared_events[0]["kind"] == "history_truncated" and
                    cleared_events[0]["source_event_id"] == 1), cleared_events

                status, headers, body = request(port, "GET", history_path)
                document = json.loads(body)
                assert status == 200, (status, body)
                last_sequence = document["data"]["last_sequence"]
                assert last_sequence > 0, document
                assert headers["etag"] == current_etag, headers

                status, headers, body = request(
                    port, "POST", truncate_path,
                    body=json.dumps({
                        "through_sequence": last_sequence,
                    }).encode(),
                    headers={"Content-Type": "application/json",
                             "If-Match": current_etag})
                document = json.loads(body)
                assert status == 200, (status, body)
                assert document["data"]["revision"] == 11, document
                current_etag = headers["etag"]

                for suffix, allow in (
                    ("history", "GET, HEAD, OPTIONS"),
                    ("export", "GET, HEAD, OPTIONS"),
                    ("fork", "POST, OPTIONS"),
                    ("truncate", "POST, OPTIONS"),
                    ("clear", "POST, OPTIONS"),
                ):
                    status, option_headers, body = request(
                        port, "OPTIONS", session_path + "/" + suffix)
                    assert status == 200 and option_headers["allow"] == allow, (
                        suffix, status, option_headers, body)

                meta_path.write_text("{broken", encoding="utf-8")
                status, headers, body = request(
                    port, "GET",
                    f"/api/v1/projects/api-project/sessions/{session_id}")
                document = json.loads(body)
                assert status == 500, (status, body)
                assert document["error"]["code"] == "session_read_failed", (
                    document)

                status, headers, body = request(
                    port, "OPTIONS", "/api/v1/sessions")
                document = json.loads(body)
                assert status == 200 and headers["allow"] == (
                    "GET, HEAD, POST, OPTIONS"), (status, headers, body)
                status, headers, body = request(
                    port, "OPTIONS",
                    f"/api/v1/projects/api-project/sessions/{session_id}")
                assert status == 200 and headers["allow"] == (
                    "GET, HEAD, PATCH, DELETE, OPTIONS"), (
                    status, headers, body)
                status, headers, body = request(
                    port, "OPTIONS",
                    f"/api/v1/projects/api-project/sessions/{session_id}/restore")
                assert status == 200 and headers["allow"] == (
                    "POST, OPTIONS"), (status, headers, body)

                status, headers, body = request(
                    port, "OPTIONS", "/api/v1/settings/settings")
                document = json.loads(body)
                assert status == 200, (status, body)
                assert headers["allow"] == (
                    "PUT, PATCH, DELETE, OPTIONS"), headers
                assert document["data"]["allow"] == headers["allow"], document
            except BaseException as error:
                failure = error
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=5.0)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=3.0)
                model_server.shutdown()
                model_server.server_close()
                model_thread.join(timeout=3.0)
            if failure is not None:
                output = log_path.read_text(encoding="utf-8", errors="replace")
                raise RuntimeError(f"{failure}\n--- xs log ---\n{output[-6000:]}") from failure


def run_unconfigured_model_probe(host: Path) -> None:
    """A saved task must not require a live model endpoint at creation."""
    with tempfile.TemporaryDirectory(prefix="api-unconfigured-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        site = base / "site"
        shutil.copytree(ROOT / "app", site)
        config_path = site / "xs.json"
        config = json.loads(config_path.read_text(encoding="utf-8"))
        port = free_port()
        config["services"][0]["port"] = port
        config_path.write_text(json.dumps(config), encoding="utf-8")
        environment = os.environ.copy()
        for name in (
            "MDO_LING_CHAT_COMPLETIONS_URL", "MDO_LING_RESPONSES_URL",
            "MDO_LING_ANTHROPIC_URL", "MDO_LING_API_KEY",
        ):
            environment.pop(name, None)
        log_path = base / "xs.log"
        with log_path.open("wb") as log:
            process = subprocess.Popen(
                [str(host), str(config_path), "--", "--home", str(base / "home")],
                cwd=site, env=environment, stdout=log, stderr=subprocess.STDOUT,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0,
            )
            failure: BaseException | None = None
            try:
                wait_ready(port, process)
                for body in (
                    {"project_id": "default", "title": "Unconfigured default model"},
                    {"project_id": "default", "model_id": "ling-3.0-tiny",
                     "title": "Unconfigured selected model"},
                ):
                    status, _, response = request(
                        port, "POST", "/api/v1/sessions",
                        body=json.dumps(body).encode(),
                        headers={"Content-Type": "application/json"})
                    document = json.loads(response)
                    assert status == 201, (status, document)
                    assert document["data"]["model_id"] == "ling-3.0-tiny", document
                    assert (base / "home/sessions/default" /
                            document["data"]["id"] / "meta.json").is_file()
                profile_path = ("/api/v1/projects/default/sessions/" +
                    document["data"]["id"] + "/profile")
                profile_body = json.dumps({
                    "model_id": "ling-3.0-tiny",
                    "reasoning_effort": "low",
                    "permission_profile": "read-only",
                }).encode()
                status, _, response = request(port, "PUT", profile_path,
                    body=profile_body,
                    headers={"Content-Type": "application/json"})
                assert status == 428, (status, response)
                status, headers, response = request(port, "PUT", profile_path,
                    body=profile_body,
                    headers={"Content-Type": "application/json",
                             "If-Match": '"mdo-session-' +
                                document["data"]["id"] + '-1"'})
                profile = json.loads(response)
                assert status == 200, (status, profile)
                assert profile["data"]["reasoning_effort"] == "low", profile
                assert profile["data"]["permission_profile"] == "read-only", profile
                assert profile["data"]["revision"] == 2, profile
                assert headers["etag"].endswith('-2"'), headers
                status, _, response = request(port, "PUT", profile_path,
                    body=profile_body,
                    headers={"Content-Type": "application/json",
                             "If-Match": '"mdo-session-' +
                                document["data"]["id"] + '-1"'})
                assert status == 412, (status, response)
                invalid_profile = json.dumps({
                    "model_id": "missing-model",
                    "reasoning_effort": "low",
                    "permission_profile": "read-only",
                }).encode()
                status, _, response = request(port, "PUT", profile_path,
                    body=invalid_profile,
                    headers={"Content-Type": "application/json",
                             "If-Match": headers["etag"]})
                assert status == 422, (status, response)
                assert json.loads(response)["error"]["code"] == (
                    "session_profile_invalid")
                status, _, response = request(
                    port, "POST", "/api/v1/sessions",
                    body=b'{"project_id":"default","model_id":"unknown"}',
                    headers={"Content-Type": "application/json"})
                document = json.loads(response)
                assert status == 422, (status, document)
                assert document["error"]["code"] == "session_profile_invalid", document

                # The model editor uses a complete, versioned configuration
                # document; built-in Ling stays protected by the transaction.
                model_path = "/api/v1/models/config"
                status, headers, response = request(port, "GET", model_path)
                assert status == 200, (status, response)
                original_models = json.loads(response)["data"]
                original_tag = headers["etag"]
                changed = {key: value for key, value in original_models.items()
                    if key != "runtime_override"}
                changed = json.loads(json.dumps(changed))
                changed["providers"].append({
                    "id": "test-provider", "name": "Test Provider",
                    "builtin": False, "editable": True, "removable": True,
                    "verify_peer": True, "timeout_ms": 30000,
                    "endpoints": {"responses": "https://example.com/v1/responses"},
                    "credential": {"secret_ref": "env:MDO_TEST_MODEL_KEY"},
                })
                changed["items"].append({
                    "id": "test-model", "name": "Test Model",
                    "provider": "test-provider", "wire_model": "test-model",
                    "builtin": False, "free": False,
                    "editable": True, "removable": True,
                    "protocols": ["openai-responses"],
                    "default_protocol": "openai-responses",
                    "capabilities": ["text-input", "text-output", "streaming"],
                    "window": {"mode": "shared-context", "context_tokens": 32000,
                        "max_input_tokens": 30000, "max_output_tokens": 2000,
                        "output_reserve_tokens": 1000, "summary_tokens": 500},
                    "reasoning_efforts": ["none", "medium"],
                    "default_reasoning_effort": "medium", "attachments": [],
                })
                model_document = json.dumps({"schema_version": 1,
                    "patch": changed}).encode()
                status, _, response = request(port, "POST",
                    "/api/v1/settings/models/preview", body=model_document,
                    headers={"Content-Type": "application/json"})
                assert status == 200 and json.loads(response)["data"]["changes"], response
                status, _, response = request(port, "PUT", "/api/v1/settings/models",
                    body=model_document, headers={"Content-Type": "application/json",
                        "If-Match": original_tag})
                assert status == 200, (status, response)
                status, _, response = request(port, "PUT", "/api/v1/settings/models",
                    body=model_document, headers={"Content-Type": "application/json",
                        "If-Match": original_tag})
                assert status == 412, (status, response)
                status, _, response = request(port, "GET", model_path)
                assert status == 200, (status, response)
                saved_models = json.loads(response)["data"]
                assert any(item["id"] == "test-model" for item in saved_models["items"])
                assert any(item["id"] == "test-provider" for item in saved_models["providers"])
                assert any(item["id"] == "test-model" for item in
                    json.loads(request(port, "GET", "/api/v1/models")[2])["data"]["models"])
                protected = json.loads(json.dumps(changed))
                protected["items"][0]["name"] = "Altered Ling"
                status, _, response = request(port, "POST",
                    "/api/v1/settings/models/preview",
                    body=json.dumps({"schema_version": 1, "patch": protected}).encode(),
                    headers={"Content-Type": "application/json"})
                assert status == 422, (status, response)
                status, headers, response = request(port, "GET", model_path)
                assert status == 200, (status, response)
                changed["default_model"] = "test-model"
                status, _, response = request(port, "PUT", "/api/v1/settings/models",
                    body=json.dumps({"schema_version": 1, "patch": changed}).encode(),
                    headers={"Content-Type": "application/json",
                        "If-Match": headers["etag"]})
                assert status == 200, (status, response)
                status, headers, response = request(port, "GET", model_path)
                assert status == 200 and json.loads(response)["data"][
                    "default_model"] == "test-model", response
                changed["items"] = [item for item in changed["items"]
                    if item["id"] != "test-model"]
                status, _, response = request(port, "POST",
                    "/api/v1/settings/models/preview",
                    body=json.dumps({"schema_version": 1, "patch": changed}).encode(),
                    headers={"Content-Type": "application/json"})
                assert status == 422, (status, response)
                changed["default_model"] = "ling-3.0-tiny"
                changed["providers"] = [provider for provider in changed["providers"]
                    if provider["id"] != "test-provider"]
                status, _, response = request(port, "PUT", "/api/v1/settings/models",
                    body=json.dumps({"schema_version": 1, "patch": changed}).encode(),
                    headers={"Content-Type": "application/json",
                        "If-Match": headers["etag"]})
                assert status == 200, (status, response)
                status, _, response = request(port, "GET", model_path)
                assert status == 200, (status, response)
                remaining = json.loads(response)["data"]
                assert remaining["default_model"] == "ling-3.0-tiny", remaining
                assert [item["id"] for item in remaining["items"]] == [
                    "ling-3.0-tiny"], remaining
            except BaseException as error:
                failure = error
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=5.0)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=3.0)
            if failure is not None:
                output = log_path.read_text(encoding="utf-8", errors="replace")
                raise RuntimeError(f"{failure}\n--- xs log ---\n{output[-6000:]}") from failure


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path,
        default=ROOT / ".build/host" / ("xs.exe" if os.name == "nt" else "xs"))
    args = parser.parse_args()
    host = args.host.resolve()
    if not host.is_file():
        print(f"missing xs host: {host}", file=sys.stderr)
        return 2
    run_unconfigured_model_probe(host)
    run_probe(host)
    print("API runtime probe: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
