#!/usr/bin/env python3
"""Bounded asynchronous operation lifecycle probe through xs/TCC."""

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
#include <string.h>
#include <xsbase.h>

#include "include/mdo/modules.h"
#include "include/mdo/mcp.h"
#include "include/mdo/operations.h"

struct MdoModuleCatalog { int Value; };
struct MdoModuleDiagnostics { int Value; };
struct MdoMcpCatalog { int Value; };

static struct MdoModuleCatalog Catalog;
static struct MdoModuleDiagnostics Diagnostics;
static struct MdoMcpCatalog McpCatalog;
static xatomic32 BlockReload;
static xatomic32 ReloadEntered;
static xatomic32 FailReload;
static xatomic32 ReloadCalls;
static xatomic32 FailMcp;
static xatomic32 McpCalls;
static uint64 Generation = 1u;
static uint64 McpSchema;
static uint64 McpRequests;

bool MdoModuleManagerReload(void) {
    xrtAtomic32FetchAdd(&ReloadCalls, 1u, XMEMORY_RELAXED);
    xrtAtomic32Store(&ReloadEntered, 1u, XMEMORY_RELEASE);
    while (xrtAtomic32Load(&BlockReload, XMEMORY_ACQUIRE) != 0u)
        xrtSleepUs(1000u);
    if (xrtAtomic32Load(&FailReload, XMEMORY_ACQUIRE) != 0u) {
        xrtAtomic32Store(&FailReload, 0u, XMEMORY_RELEASE);
        return false;
    }
    ++Generation;
    return true;
}

uint64 MdoModuleManagerGeneration(void) { return Generation; }
MdoModuleCatalog* MdoModuleCatalogSnapshot(void) { return &Catalog; }
void MdoModuleCatalogRelease(MdoModuleCatalog* value) { (void)value; }
size_t MdoModuleCatalogModuleCount(const MdoModuleCatalog* value) {
    (void)value; return 2u;
}
size_t MdoModuleCatalogToolCount(const MdoModuleCatalog* value) {
    (void)value; return 3u;
}
size_t MdoModuleCatalogAgentCount(const MdoModuleCatalog* value) {
    (void)value; return 1u;
}
MdoModuleDiagnostics* MdoModuleDiagnosticsSnapshot(void) {
    return &Diagnostics;
}
void MdoModuleDiagnosticsRelease(MdoModuleDiagnostics* value) { (void)value; }
size_t MdoModuleDiagnosticsCount(const MdoModuleDiagnostics* value) {
    (void)value; return 0u;
}

MdoMcpCatalog* MdoMcpCatalogSnapshot(void) { return &McpCatalog; }
void MdoMcpCatalogRelease(MdoMcpCatalog* value) { (void)value; }
bool MdoMcpCatalogFind(const MdoMcpCatalog* catalog, const char* id,
    MdoMcpServerInfo* info) {
    (void)catalog;
    if (strcmp(id, "mock") != 0 || info == NULL ||
        info->Size < sizeof(*info)) return false;
    info->Id = "mock";
    info->RequestTimeoutMilliseconds = 1000u;
    return true;
}
bool MdoMcpManagerGetStatus(const char* id, MdoMcpServerStatus* status) {
    if (strcmp(id, "mock") != 0 || status == NULL ||
        status->Size < sizeof(*status)) return false;
    status->CatalogGeneration = 7u;
    status->State = McpSchema != 0u ? XWORK_MCP_SERVER_READY :
        XWORK_MCP_SERVER_DISCONNECTED;
    status->SchemaGeneration = McpSchema;
    status->DiscoveredToolCount = McpSchema != 0u ? 4u : 0u;
    status->RequestsCompleted = McpRequests;
    status->Enabled = true;
    status->Connected = McpSchema != 0u;
    status->ToolsDiscovered = McpSchema != 0u;
    return true;
}
bool MdoMcpManagerRefresh(const char* id, xcancel* cancel, uint64 deadline,
    xwork_error* error) {
    (void)deadline; (void)error;
    if (strcmp(id, "mock") != 0 || xrtCancelRequested(cancel)) return false;
    xrtAtomic32FetchAdd(&McpCalls, 1u, XMEMORY_RELAXED);
    if (xrtAtomic32Load(&FailMcp, XMEMORY_ACQUIRE) != 0u) {
        xrtAtomic32Store(&FailMcp, 0u, XMEMORY_RELEASE);
        return false;
    }
    ++McpSchema;
    McpRequests += 2u;
    return true;
}

#include "src/operations/manager.c"

static bool WaitTerminal(const char* id, MdoOperationInfo* info) {
    unsigned i;
    for (i = 0u; i < 5000u; ++i) {
        memset(info, 0, sizeof(*info)); info->Size = sizeof(*info);
        if (!MdoOperationGet(id, info)) return false;
        if (info->State == MDO_OPERATION_SUCCEEDED ||
            info->State == MDO_OPERATION_FAILED ||
            info->State == MDO_OPERATION_CANCELLED) return true;
        xrtSleepUs(1000u);
    }
    return false;
}

void ServiceInit(XS_HostInfo* host) {
    MdoOperationInfo success;
    MdoOperationInfo failure;
    MdoOperationInfo blocker;
    MdoOperationInfo queued;
    MdoOperationInfo mcp_success;
    MdoOperationInfo mcp_failure;
    MdoOperationInfo current;
    MdoOperationInfo items[8];
    char success_id[MDO_OPERATION_ID_CAPACITY];
    char failure_id[MDO_OPERATION_ID_CAPACITY];
    char blocker_id[MDO_OPERATION_ID_CAPACITY];
    char queued_id[MDO_OPERATION_ID_CAPACITY];
    char mcp_id[MDO_OPERATION_ID_CAPACITY];
    size_t count;
    unsigned i;
    (void)host;

    xrtAtomic32Init(&BlockReload, 0u);
    xrtAtomic32Init(&ReloadEntered, 0u);
    xrtAtomic32Init(&FailReload, 0u);
    xrtAtomic32Init(&ReloadCalls, 0u);
    xrtAtomic32Init(&FailMcp, 0u);
    xrtAtomic32Init(&McpCalls, 0u);
    if (!MdoOperationManagerInit()) { printf("init_error=1\n"); goto done; }

    memset(&success, 0, sizeof(success)); success.Size = sizeof(success);
    if (!MdoOperationStartModuleReload(&success)) {
        printf("start_error=success\n"); goto done;
    }
    snprintf(success_id, sizeof(success_id), "%s", success.Id);
    if (!WaitTerminal(success_id, &current)) {
        printf("wait_error=success\n"); goto done;
    }
    printf("success=state:%d generation:%llu modules:%llu tools:%llu agents:%llu\n",
        (int)current.State, (unsigned long long)current.Generation,
        (unsigned long long)current.ItemCount,
        (unsigned long long)current.SecondaryCount,
        (unsigned long long)current.TertiaryCount);

    xrtAtomic32Store(&FailReload, 1u, XMEMORY_RELEASE);
    memset(&failure, 0, sizeof(failure)); failure.Size = sizeof(failure);
    if (!MdoOperationStartModuleReload(&failure)) {
        printf("start_error=failure\n"); goto done;
    }
    snprintf(failure_id, sizeof(failure_id), "%s", failure.Id);
    if (!WaitTerminal(failure_id, &current)) {
        printf("wait_error=failure\n"); goto done;
    }
    printf("failure=state:%d message:%s\n", (int)current.State,
        current.Message);

    memset(&mcp_success, 0, sizeof(mcp_success));
    mcp_success.Size = sizeof(mcp_success);
    if (!MdoOperationStartMcpRefresh("mock", &mcp_success)) {
        printf("start_error=mcp_success\n"); goto done;
    }
    snprintf(mcp_id, sizeof(mcp_id), "%s", mcp_success.Id);
    if (!WaitTerminal(mcp_id, &current)) {
        printf("wait_error=mcp_success\n"); goto done;
    }
    printf("mcp_success=state:%d target:%s catalog:%llu schema:%llu tools:%llu connected:%d\n",
        (int)current.State, current.Target,
        (unsigned long long)current.Generation,
        (unsigned long long)current.AuxiliaryGeneration,
        (unsigned long long)current.ItemCount,
        current.Connected ? 1 : 0);

    xrtAtomic32Store(&FailMcp, 1u, XMEMORY_RELEASE);
    memset(&mcp_failure, 0, sizeof(mcp_failure));
    mcp_failure.Size = sizeof(mcp_failure);
    if (!MdoOperationStartMcpRefresh("mock", &mcp_failure)) {
        printf("start_error=mcp_failure\n"); goto done;
    }
    snprintf(mcp_id, sizeof(mcp_id), "%s", mcp_failure.Id);
    if (!WaitTerminal(mcp_id, &current)) {
        printf("wait_error=mcp_failure\n"); goto done;
    }
    printf("mcp_failure=state:%d message:%s\n", (int)current.State,
        current.Message);

    xrtAtomic32Store(&ReloadEntered, 0u, XMEMORY_RELEASE);
    xrtAtomic32Store(&BlockReload, 1u, XMEMORY_RELEASE);
    memset(&blocker, 0, sizeof(blocker)); blocker.Size = sizeof(blocker);
    if (!MdoOperationStartModuleReload(&blocker)) {
        printf("start_error=blocker\n"); goto done;
    }
    snprintf(blocker_id, sizeof(blocker_id), "%s", blocker.Id);
    for (i = 0u; i < 5000u &&
         xrtAtomic32Load(&ReloadEntered, XMEMORY_ACQUIRE) == 0u; ++i)
        xrtSleepUs(1000u);
    if (xrtAtomic32Load(&ReloadEntered, XMEMORY_ACQUIRE) == 0u) {
        printf("enter_error=1\n"); goto done;
    }

    memset(&queued, 0, sizeof(queued)); queued.Size = sizeof(queued);
    if (!MdoOperationStartModuleReload(&queued)) {
        printf("start_error=queued\n"); goto done;
    }
    snprintf(queued_id, sizeof(queued_id), "%s", queued.Id);
    memset(&current, 0, sizeof(current)); current.Size = sizeof(current);
    if (!MdoOperationCancel(queued_id, &current)) {
        printf("cancel_error=1\n"); goto done;
    }
    printf("cancel=state:%d requested:%d\n", (int)current.State,
        current.CancelRequested ? 1 : 0);
    xrtAtomic32Store(&BlockReload, 0u, XMEMORY_RELEASE);
    if (!WaitTerminal(blocker_id, &current)) {
        printf("wait_error=blocker\n"); goto done;
    }

    memset(items, 0, sizeof(items));
    for (i = 0u; i < 8u; ++i) items[i].Size = sizeof(items[i]);
    count = MdoOperationList(items, 8u);
    printf("list=count:%llu first_sequence:%llu last_sequence:%llu\n",
        (unsigned long long)count,
        (unsigned long long)(count != 0u ? items[0].Sequence : 0u),
        (unsigned long long)(count != 0u ? items[count - 1u].Sequence : 0u));
    printf("calls=module:%u mcp:%u\n",
        xrtAtomic32Load(&ReloadCalls, XMEMORY_ACQUIRE),
        xrtAtomic32Load(&McpCalls, XMEMORY_ACQUIRE));
done:
    xrtAtomic32Store(&BlockReload, 0u, XMEMORY_RELEASE);
    MdoOperationManagerUnit();
    printf("probe_done=1\n");
}

void ServiceUnit(XS_HostInfo* host) { (void)host; }
'''


def write_site(site: Path) -> None:
    for relative in ("web", "src/operations", "include/mdo",
                     "generated/module-sdk/mdo"):
        (site / relative).mkdir(parents=True, exist_ok=True)
    (site / "web/index.html").write_text("probe", encoding="utf-8")
    shutil.copy2(ROOT / "app/src/operations/manager.c",
                 site / "src/operations/manager.c")
    for name in ("mcp.h", "modules.h", "operations.h"):
        shutil.copy2(ROOT / "app/include/mdo" / name,
                     site / "include/mdo" / name)
    shutil.copy2(ROOT / "app/generated/module-sdk/mdo/module.h",
                 site / "generated/module-sdk/mdo/module.h")
    (site / "probe.c").write_text(PROBE_SOURCE, encoding="utf-8")
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        port = listener.getsockname()[1]
    (site / "xs.json").write_text(json.dumps({"services": [{
        "enabled": True, "class": "http", "name": "operation-probe",
        "ip": "127.0.0.1", "port": port,
        "host_default": {"enabled": True, "name": "probe", "path": "web",
                         "devlang": "c", "devfile": "probe.c"},
    }]}), encoding="utf-8")


def run_probe(host: Path, site: Path) -> str:
    process = subprocess.Popen(
        [str(host), "xs.json"], cwd=site, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace",
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
    done.wait(timeout=15.0)
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
    with tempfile.TemporaryDirectory(prefix="operation-runtime-",
                                     dir=ROOT / ".build") as raw:
        site = Path(raw) / "site"
        write_site(site)
        output = run_probe(host, site)
        assert "init_error=" not in output, output
        assert "start_error=" not in output, output
        assert "wait_error=" not in output, output
        assert "enter_error=" not in output, output
        assert "cancel_error=" not in output, output
        assert "success=state:2 generation:2 modules:2 tools:3 agents:1" in output, output
        assert "failure=state:3 message:Module reload failed" in output, output
        assert "cancel=state:4 requested:1" in output, output
        assert "mcp_success=state:2 target:mock catalog:7 schema:1 tools:4 connected:1" in output, output
        assert "mcp_failure=state:3 message:MCP refresh failed" in output, output
        assert "list=count:6 first_sequence:6 last_sequence:1" in output, output
        assert "calls=module:3 mcp:2" in output, output
        assert "probe_done=1" in output, output
    print("operation runtime probe: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
