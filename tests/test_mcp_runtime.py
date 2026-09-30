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


def c_literal(value: str) -> str:
    return json.dumps(value)


def probe_source(valid: str, changed: str, missing: str, invalid_http: str,
                 http: str, other: str, marker: str, program: str,
                 script: str) -> str:
    return rf'''
#include <stdio.h>
#include <string.h>
#include <xsbase.h>
#include <xwork.h>

#include "src/storage/home.c"
#include "src/security/secrets.c"
#include "src/mcp/manager.c"

static const char sValid[] = {c_literal(valid)};
static const char sChanged[] = {c_literal(changed)};
static const char sMissing[] = {c_literal(missing)};
static const char sInvalidHttp[] = {c_literal(invalid_http)};
static const char sHttp[] = {c_literal(http)};
static const char sOther[] = {c_literal(other)};
static const char sInvalid[] = "{{\"schema_version\":1,\"id\":\"mock\",\"unknown\":true}}";
static const char sMarker[] = {c_literal(marker)};
static const char sProgram[] = {c_literal(program)};
static const char sScript[] = {c_literal(script)};

static int MarkerExists(void) {{
    xfileinfo info;
    if (xrtPathStat(sMarker, false, &info)) return 1;
    xrtClearError();
    return 0;
}}

static void PrintDiagnostics(const char *label) {{
    MdoMcpDiagnostics *diagnostics = MdoMcpDiagnosticsSnapshot();
    MdoMcpDiagnosticInfo info;
    memset(&info, 0, sizeof(info));
    info.Size = sizeof(info);
    printf("%s_count=%zu\n", label, MdoMcpDiagnosticsCount(diagnostics));
    if (MdoMcpDiagnosticsAt(diagnostics, 0u, &info))
        printf("%s=stage:%d id:%s message:%s\n", label, (int)info.Stage,
            info.ServerId != NULL ? info.ServerId : "null",
            info.Message != NULL ? info.Message : "null");
    MdoMcpDiagnosticsRelease(diagnostics);
}}

static void PrintCatalog(const char *label) {{
    MdoMcpCatalog *catalog = MdoMcpCatalogSnapshot();
    MdoMcpServerInfo info;
    memset(&info, 0, sizeof(info));
    info.Size = sizeof(info);
    printf("%s=generation:%llu count:%zu", label,
        (unsigned long long)MdoMcpManagerGeneration(),
        MdoMcpCatalogCount(catalog));
    if (MdoMcpCatalogAt(catalog, 0u, &info))
        printf(" id:%s external:%d args:%zu env:%zu hash:%zu headers:%zu transport:%d",
            info.Id, info.External ? 1 : 0, info.ArgumentCount,
            info.EnvironmentCount, strlen(info.SourceHash),
            info.HttpHeaderCount, (int)info.Transport);
    printf("\n");
    MdoMcpCatalogRelease(catalog);
}}

static void PrintStatus(const char *label) {{
    MdoMcpServerStatus status;
    memset(&status, 0, sizeof(status));
    status.Size = sizeof(status);
    if (MdoMcpManagerGetStatus("mock", &status))
        printf("%s=state:%d enabled:%d connected:%d discovered:%d tools:%zu requests:%llu schema:%llu\n",
            label, (int)status.State, status.Enabled ? 1 : 0,
            status.Connected ? 1 : 0, status.ToolsDiscovered ? 1 : 0,
            status.DiscoveredToolCount,
            (unsigned long long)status.RequestsCompleted,
            (unsigned long long)status.SchemaGeneration);
    else printf("%s=missing\n", label);
}}

static int Write(const char *path, const char *text) {{
    return MdoHomeAtomicWrite(path, text, strlen(text), false) ? 1 : 0;
}}

void ServiceInit(XS_HostInfo *host) {{
    xwork_runtime_config runtime_config;
    xwork_runtime *runtime = NULL;
    xwork_error error;
    MdoMcpCatalog *catalog = NULL;
    xwork_mcp_server_config manual;
    const char *manual_args[2];
    uint64 generation;
    (void)host;
    printf("endpoint_validation=valid:%d http:%d userinfo:%d fragment:%d port0:%d overflow:%d\n",
        MdoMcpHttpEndpointValid("HTTPS://example.com:443/mcp?mode=1") ? 1 : 0,
        MdoMcpHttpEndpointValid("http://example.com/mcp") ? 1 : 0,
        MdoMcpHttpEndpointValid("https://user@example.com/mcp") ? 1 : 0,
        MdoMcpHttpEndpointValid("https://example.com/mcp#fragment") ? 1 : 0,
        MdoMcpHttpEndpointValid("https://example.com:0/mcp") ? 1 : 0,
        MdoMcpHttpEndpointValid("https://example.com:65536/mcp") ? 1 : 0);
    printf("header_validation=authorization:%d accept:%d mcp:%d safe:%d newline:%d\n",
        MdoMcpHeaderNameReserved((xstrview){{"Authorization", 13u}}) ? 1 : 0,
        MdoMcpHeaderNameReserved((xstrview){{"Accept", 6u}}) ? 1 : 0,
        MdoMcpHeaderNameReserved((xstrview){{"mCp-Name", 8u}}) ? 1 : 0,
        MdoMcpHeaderValueValid("Bearer secret-v1") ? 1 : 0,
        MdoMcpHeaderValueValid("unsafe\nvalue") ? 1 : 0);
    xworkRuntimeConfigInit(&runtime_config);
    memset(&error, 0, sizeof(error));
    if (!MdoHomeInit() || !Write("mcp/mock.json", sValid)) {{
        printf("setup_failed=1\n"); goto done;
    }}
    runtime = xworkRuntimeCreate(&runtime_config, &error);
    if (runtime == NULL || !MdoMcpManagerInit(runtime)) {{
        printf("manager_init_failed=%s\n", error.sMessage); goto done;
    }}
    PrintCatalog("initial_catalog");
    PrintStatus("initial_status");
    printf("cold_marker=%d runtime_count=%zu\n", MarkerExists(),
        xworkRuntimeMcpServerCount(runtime));
    PrintDiagnostics("initial_diagnostic");

    memset(&error, 0, sizeof(error));
    printf("refresh=%d\n", MdoMcpManagerRefresh("mock", NULL,
        XRT_DEADLINE_NEVER, &error) ? 1 : 0);
    PrintStatus("ready_status");
    printf("ready_marker=%d\n", MarkerExists());

    printf("same_reload=%d\n", MdoMcpManagerReload() ? 1 : 0);
    PrintCatalog("same_catalog");
    PrintStatus("same_status");

    memset(&error, 0, sizeof(error));
    printf("disconnect=%d\n", MdoMcpManagerDisconnect("mock", &error) ? 1 : 0);
    PrintStatus("disconnected_status");
    printf("reconnect=%d\n", MdoMcpManagerRefresh("mock", NULL,
        XRT_DEADLINE_NEVER, &error) ? 1 : 0);
    PrintStatus("reconnected_status");

    if (!Write("mcp/mock.json", sChanged)) goto done;
    printf("changed_reload=%d\n", MdoMcpManagerReload() ? 1 : 0);
    PrintCatalog("changed_catalog");
    PrintStatus("changed_status");

    if (!Write("mcp/mock.json", sInvalid)) goto done;
    printf("invalid_reload=%d\n", MdoMcpManagerReload() ? 1 : 0);
    PrintCatalog("invalid_catalog");
    PrintDiagnostics("invalid_diagnostic");

    if (!Write("mcp/mock.json", sMissing)) goto done;
    printf("missing_reload=%d\n", MdoMcpManagerReload() ? 1 : 0);
    PrintCatalog("missing_catalog");
    PrintDiagnostics("missing_diagnostic");

    if (!Write("mcp/mock.json", sInvalidHttp)) goto done;
    printf("invalid_http_reload=%d\n", MdoMcpManagerReload() ? 1 : 0);
    PrintCatalog("invalid_http_catalog");
    PrintDiagnostics("invalid_http_diagnostic");

    if (!Write("mcp/mock.json", sHttp)) goto done;
    printf("http_reload=%d\n", MdoMcpManagerReload() ? 1 : 0);
    PrintCatalog("http_catalog");
    PrintDiagnostics("http_diagnostic");

    if (!Write("mcp/mock.json", sValid) || !MdoMcpManagerReload()) goto done;
    generation = MdoMcpManagerGeneration();
    xworkMcpServerConfigInit(&manual);
    manual_args[0] = sScript;
    manual_args[1] = sMarker;
    manual.sServerId = "other";
    manual.sSummary = "Independent owner.";
    manual.sProgram = sProgram;
    manual.psArguments = manual_args;
    manual.iArgumentCount = 2u;
    manual.uDefaultToolEffects = XWORK_TOOL_EFFECT_EXTERNAL_SERVICE;
    manual.bEnabled = false;
    printf("manual_other=%d\n",
        xworkRuntimeRegisterMcpServer(runtime, &manual, &error) ? 1 : 0);
    if (!Write("mcp/other.json", sOther)) goto done;
    xrtClearError();
    printf("collision_reload=%d\n", MdoMcpManagerReload() ? 1 : 0);
    printf("collision_generation=%llu expected:%llu\n",
        (unsigned long long)MdoMcpManagerGeneration(),
        (unsigned long long)generation);
    PrintCatalog("collision_catalog");
    PrintDiagnostics("collision_diagnostic");
    (void)MdoHomeRemove("mcp/other.json", false);
    (void)xworkRuntimeUnregisterMcpServer(runtime, "other", &error);

done:
    MdoMcpCatalogRelease(catalog);
    MdoMcpManagerUnit();
    if (runtime != NULL) xworkRuntimeRelease(runtime);
    MdoHomeUnit();
    printf("probe_done=1\n");
}}

void ServiceUnit(XS_HostInfo *host) {{ (void)host; }}
'''


def write_site(site: Path, base: Path) -> tuple[Path, Path]:
    script = base / "mock_mcp.py"
    marker = base / "server_started.txt"
    script.write_text(MOCK_SERVER, encoding="utf-8")
    valid = server_document(sys.executable, str(script), str(marker))
    changed = server_document(sys.executable, str(script), str(marker),
                              description="Changed mock tools.")
    missing = server_document(sys.executable, str(script), str(marker),
                              secret_ref="env:MDO_MCP_MISSING_SECRET")
    other = server_document(sys.executable, str(script), str(marker),
                            server_id="other")
    http = json.dumps({
        "schema_version": 1,
        "id": "mock",
        "name": "HTTP mock",
        "description": "HTTP mock tools.",
        "enabled": True,
        "transport": {
            "type": "streamable-http",
            "endpoint": "https://example.invalid/mcp",
            "headers": [{
                "name": "Authorization",
                "secret_ref": "env:MDO_MCP_TEST_SECRET",
            }],
        },
        "protocol_version": "2026-07-28",
        "startup_timeout_ms": 5000,
        "request_timeout_ms": 5000,
        "limits": {"message_bytes": 1048576, "tools": 16},
        "tools": {"allow": [], "deny": []},
        "security": {
            "default_effects": ["network", "external-service"],
            "permission_profile": "balanced",
            "trust_read_only_annotations": False,
        },
        "auto_reconnect": True,
    }, separators=(",", ":"))
    invalid_http_document = json.loads(http)
    invalid_http_document["transport"]["headers"] = [{
        "name": "Authorization",
        "secret_ref": "env:MDO_MCP_TEST_SECRET",
        "value": "must-not-be-accepted",
    }]
    invalid_http = json.dumps(invalid_http_document, separators=(",", ":"))

    for relative in (
        "web", "default-home/config", "default-home/mcp",
        "src/storage", "src/security", "src/mcp", "include/mdo",
    ):
        (site / relative).mkdir(parents=True, exist_ok=True)
    (site / "web/index.html").write_text("probe", encoding="utf-8")
    shutil.copy2(ROOT / "app/default-home/config/defaults.json",
                 site / "default-home/config/defaults.json")
    for relative in (
        "src/storage/home.c", "src/storage/home_import.inc.c", "src/security/secrets.c", "src/mcp/manager.c",
        "include/mdo/home.h", "include/mdo/home_import.h", "include/mdo/secrets.h", "include/mdo/mcp.h",
    ):
        shutil.copy2(ROOT / "app" / relative, site / relative)
    (site / "probe.c").write_text(probe_source(
        valid, changed, missing, invalid_http, http, other, str(marker),
        sys.executable, str(script)), encoding="utf-8")
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        port = listener.getsockname()[1]
    (site / "xs.json").write_text(json.dumps({
        "services": [{
            "enabled": True, "class": "http", "name": "mcp-probe",
            "ip": "127.0.0.1", "port": port,
            "host_default": {
                "enabled": True, "name": "probe", "path": "web",
                "devlang": "c", "devfile": "probe.c",
            },
        }],
    }), encoding="utf-8")
    return script, marker


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
    parser = argparse.ArgumentParser(description=__doc__)
    default = ROOT / ".build/host" / ("xs.exe" if os.name == "nt" else "xs")
    parser.add_argument("--host", type=Path, default=default)
    args = parser.parse_args()
    host = args.host.resolve()
    if not host.is_file():
        print(f"missing xs host: {host}", file=sys.stderr)
        return 2
    (ROOT / ".build").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="mcp-runtime-",
                                     dir=ROOT / ".build") as raw:
        base = Path(raw)
        site = base / "site"
        write_site(site, base)
        output = run_probe(host, site, base / "home")
        assert "manager_init_failed=" not in output, output
        assert "endpoint_validation=valid:1 http:0 userinfo:0 fragment:0 port0:0 overflow:0" in output, output
        assert "header_validation=authorization:0 accept:1 mcp:1 safe:1 newline:0" in output, output
        assert "initial_catalog=generation:1 count:1 id:mock external:1 args:2 env:1 hash:64" in output, output
        assert "initial_status=state:0 enabled:1 connected:0 discovered:0 tools:0 requests:0" in output, output
        assert "cold_marker=0 runtime_count=1" in output, output
        assert "initial_diagnostic_count=0" in output, output
        assert "refresh=1" in output, output
        assert "ready_status=state:1 enabled:1 connected:1 discovered:1 tools:1 requests:2" in output, output
        assert "ready_marker=1" in output, output
        assert "same_reload=1" in output, output
        assert "same_catalog=generation:2 count:1" in output, output
        assert "same_status=state:1 enabled:1 connected:1 discovered:1 tools:1 requests:2" in output, output
        assert "disconnect=1" in output, output
        assert "disconnected_status=state:0 enabled:1 connected:0" in output, output
        assert "reconnect=1" in output, output
        assert "reconnected_status=state:1 enabled:1 connected:1" in output, output
        assert "changed_reload=1" in output, output
        assert "changed_catalog=generation:3 count:1" in output, output
        assert "changed_status=state:0 enabled:1 connected:0 discovered:0" in output, output
        assert "invalid_reload=1" in output, output
        assert "invalid_catalog=generation:4 count:0" in output, output
        assert "invalid_diagnostic=stage:3 id:mock" in output, output
        assert "missing_reload=1" in output, output
        assert "missing_catalog=generation:5 count:0" in output, output
        assert "missing_diagnostic=stage:4 id:mock" in output, output
        assert "invalid_http_reload=1" in output, output
        assert "invalid_http_catalog=generation:6 count:0" in output, output
        assert "invalid_http_diagnostic=stage:3 id:mock" in output, output
        assert "http_reload=1" in output, output
        assert "http_catalog=generation:7 count:1 id:mock external:1 args:0 env:0 hash:64 headers:1 transport:2" in output, output
        assert "http_diagnostic_count=0" in output, output
        assert "manual_other=1" in output, output
        assert "collision_reload=0" in output, output
        assert "collision_generation=8 expected:8" in output, output
        assert "collision_catalog=generation:8 count:1" in output, output
        assert "collision_diagnostic=stage:6 id:null" in output, output
        assert "probe_done=1" in output, output
    print("MCP runtime probe: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
