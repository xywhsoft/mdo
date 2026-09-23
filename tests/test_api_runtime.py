#!/usr/bin/env python3
"""Bounded MDO-8 HTTP probe through the real xs/TCC request path."""

from __future__ import annotations

import argparse
import http.client
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time
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


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.bind(("127.0.0.1", 0))
        return int(probe.getsockname()[1])


def write_site(base: Path, port: int) -> Path:
    # xs mounts its configured application root at /app.  Mirror the packed
    # application layout directly so /app/default-home has identical meaning
    # in the development and single-file paths.
    shutil.copytree(ROOT / "app", base, dirs_exist_ok=True)
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
        log_path = base / "xs.log"
        environment = os.environ.copy()
        environment["MDO_HOME"] = str(base / "wrong-environment-home")
        environment["MDO_LING_CHAT_COMPLETIONS_URL"] = (
            "https://example.invalid/v1")
        environment["MDO_LING_RESPONSES_URL"] = (
            "https://example.invalid/v1")
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

                resources = (
                    "settings", "models", "agents", "modules", "skills",
                    "mcp", "projects", "sessions", "runs", "schedules",
                    "tasks", "artifacts", "permissions", "diagnostics",
                    "storage", "operations",
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
                assert status == 200 and headers["allow"] == "POST, OPTIONS"
                assert document["data"]["allow"] == "POST, OPTIONS"

                status, headers, body = request(
                    port, "GET", "/api/v1/settings/settings/preview")
                document = json.loads(body)
                assert status == 405 and headers["allow"] == "POST, OPTIONS"
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
                assert mutation["revision"] == initial_revision + 2, mutation
                assert headers["etag"] == (
                    f'"mdo-config-{mutation["revision"]}"'), headers
                assert not (home / "config/settings.json").exists(), list(
                    (home / "config").iterdir())

                status, headers, body = request(port, "GET", "/api/v1/settings")
                settings_document = json.loads(body)
                assert status == 200, (status, body)
                assert settings_document["data"]["revision"] == (
                    initial_revision + 2), settings_document
                assert settings_document["data"]["transaction_service"][
                    "transactions"] == 2, settings_document
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
                    "GET, HEAD, OPTIONS"), (status, headers, body)

                status, headers, body = request(
                    port, "OPTIONS", "/api/v1/settings/settings")
                document = json.loads(body)
                assert status == 200, (status, body)
                assert headers["allow"] == "PUT, DELETE, OPTIONS", headers
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
