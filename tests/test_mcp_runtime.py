"""Bounded MDO-5 MCP manager probe through the real xs/TCC runtime."""

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


MOCK_SERVER = r'''import json
import os
import pathlib
import sys

marker = pathlib.Path(sys.argv[1])
if os.environ.get("MDO_MCP_TEST_SECRET") != "secret-v1":
    raise SystemExit(7)

for line in sys.stdin:
    marker.parent.mkdir(parents=True, exist_ok=True)
    with marker.open("a", encoding="utf-8") as stream:
        stream.write("request\n")
    request = json.loads(line)
    request_id = request.get("id")
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
    elif method == "tools/call":
        result = {
            "resultType": "complete",
            "content": [{"type": "text", "text": "echo: ok"}],
            "isError": False,
        }
    else:
        continue
    print(json.dumps({"jsonrpc": "2.0", "id": request_id,
                      "result": result}, separators=(",", ":")), flush=True)
'''


def server_document(program: str, script: str, marker: str, *,
                    server_id: str = "mock", description: str = "Mock tools.",
                    secret_ref: str = "env:MDO_MCP_TEST_SECRET") -> str:
    return json.dumps({
        "schema_version": 1,
        "id": server_id,
        "name": "Mock tools",
        "description": description,
        "enabled": True,
        "transport": {
            "type": "stdio",
            "program": program,
            "arguments": [script, marker],
            "working_directory": None,
            "inherit_environment": True,
            "environment": [{
                "name": "MDO_MCP_TEST_SECRET",
                "secret_ref": secret_ref,
            }],
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
    }, separators=(",", ":"))


def run_probe(host: Path, site: Path, home: Path) -> str:
    environment = os.environ.copy()
    environment["MDO_MCP_TEST_SECRET"] = "secret-v1"
    command = [str(host), "xs.json", "--", "--home", str(home)]
    creationflags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
    process = subprocess.Popen(
        command, cwd=site, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, encoding="utf-8", errors="replace", env=environment,
        creationflags=creationflags,
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
    from native_suite import run
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',type=Path,required=True)
    args=parser.parse_args()
    run(['test_mcp_directory_native','test_mcp_automatic_native','test_mcp_runtime_data_native'],args.host.resolve(),None)
    print('MCP approved directory/private connection/automatic protocol/owned runtime data: PASS')
    return 0

if __name__=='__main__':
    raise SystemExit(main())
