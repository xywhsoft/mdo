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


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.bind(("127.0.0.1", 0))
        return int(probe.getsockname()[1])


def write_site(base: Path, port: int) -> Path:
    # xs mounts its configured application root at /app.  Mirror the packed
    # application layout directly so /app/default-home has identical meaning
    # in the development and single-file paths.
    shutil.copytree(ROOT / "app", base, dirs_exist_ok=True)
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
                    "storage",
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

                assert not home.exists(), list(base.iterdir())
                assert not (base / "wrong-environment-home").exists(), list(base.iterdir())
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
