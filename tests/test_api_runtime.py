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

static void MdoApiProbeCreateRecoverySession(void)
{
    static bool Created;
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

    if ( Created ) return;
    Created = true;
    MdoSessionCreateOptionsInit(&Options);
    Options.ProjectId = "recovery-probe";
    Options.Title = "Recovery probe";
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
            "Continue after checking the uncertain edit.", 0u) ||
         !xllmSessionBeginModelCall(Ledger, &ModelError) ) goto done;
    memset(&ToolCall, 0, sizeof(ToolCall));
    ToolCall.sId = "recovery-edit-call";
    ToolCall.sName = "edit";
    ToolCall.sArgumentsJson =
        "{\"path\":\"recovery-probe.txt\",\"edits\":[{\"old_text\":\"before\",\"new_text\":\"after\"}]}";
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
        "    size_t Index;\n"
        "    if ( pRequest != NULL && pRequest->head != NULL ) {\n"
        "        xstrview Target = pRequest->head->Target;\n"
        "        for ( Index = 0u; Index + sizeof(Marker) - 1u <= Target.Size; ++Index ) {\n"
        "            if ( memcmp(Target.Data + Index, Marker, sizeof(Marker) - 1u) == 0 ) {\n"
        "                MdoApiProbeCreateRecoverySession();\n"
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
            }],
            "defaultModel": "legacy-model",
            "activeProject": "api-legacy",
            "settings": {"theme": "auto", "fontSize": "md"},
        }), encoding="utf-8")
        (legacy / "projects/api-legacy/project.json").write_text(json.dumps({
            "name": "API legacy project",
            "path": str(base / "workspace"),
            "defaultModel": "legacy-model",
        }), encoding="utf-8")
        (legacy / "memory/preference.md").write_text(
            "# Preference\n\nKeep migration explicit.\n", encoding="utf-8")
        (legacy / "schedules/once.json").write_text(json.dumps({
            "id": "once",
            "title": "Legacy reminder",
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
                assert user_home["file_count"] == 4, user_home
                assert user_home["project_count"] == 1, user_home
                assert user_home["model_count"] == 1, user_home
                assert user_home["schedule_count"] == 1, user_home
                assert user_home["memory_file_count"] == 1, user_home
                assert user_home["unsupported_count"] == 0, user_home
                assert re.fullmatch(r"[0-9a-f]{64}",
                                    user_home["preview_token"]), user_home
                assert not home.exists(), home

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
                    "patch": {"appearance": {"theme": "dark"}},
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

                assert not home.exists(), list(base.iterdir())
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
                assert settings_document["data"]["agent"][
                    "interaction_mode"] == "agent", settings_document
                assert settings_document["data"]["agent"][
                    "web_search"] is True, settings_document
                assert settings_document["data"]["workspace"] == {
                    "open_mode": "last",
                    "confirm_external_write": True,
                }, settings_document
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
                assert sessions_document["data"]["items"][0]["id"] == (
                    session_id), sessions_document
                status, headers, body = request(
                    port, "GET",
                    "/api/v1/projects/api-project/sessions/missing")
                document = json.loads(body)
                assert status == 404, (status, body)
                assert document["error"]["code"] == "session_not_found", document

                session_path = (
                    f"/api/v1/projects/api-project/sessions/{session_id}")
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

                truncate_path = session_path + "/truncate"
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
                assert document["data"]["revision"] == 8, document
                current_etag = headers["etag"]

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
                assert document["data"]["revision"] == 9, document
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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path,
        default=ROOT / ".build/host" / ("xs.exe" if os.name == "nt" else "xs"))
    args = parser.parse_args()
    host = args.host.resolve()
    if not host.is_file():
        print(f"missing xs host: {host}", file=sys.stderr)
        return 2
    run_probe(host)
    print("API runtime probe: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
