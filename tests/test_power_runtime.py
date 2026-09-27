#!/usr/bin/env python3
"""Bounded sleep-inhibitor lifecycle probe with controlled run counts."""

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

#include "src/storage/home.c"
#include "src/config/config.c"
#include "include/mdo/runs.h"
#include "include/mdo/schedules.h"

static xatomic32 Interactive;
static xatomic32 Scheduled;

bool MdoRunManagerGetStatus(MdoRunManagerStatus* status) {
    uint32 size;
    if (status == NULL || status->Size < sizeof(*status)) return false;
    size = status->Size;
    memset(status, 0, sizeof(*status));
    status->Size = size;
    status->ActiveRuns = xrtAtomic32Load(&Interactive, XMEMORY_ACQUIRE);
    return true;
}

bool MdoScheduleExecutorGetSnapshot(MdoScheduleExecutorSnapshot* status) {
    uint32 size;
    if (status == NULL || status->Size < sizeof(*status)) return false;
    size = status->Size;
    memset(status, 0, sizeof(*status));
    status->Size = size;
    status->ActiveRuns = xrtAtomic32Load(&Scheduled, XMEMORY_ACQUIRE);
    return true;
}

#include "src/power/inhibitor.c"
#include "src/power/manager.c"

static bool WaitFor(bool configured, bool running, bool active) {
    unsigned i;
    for (i = 0u; i < 100u; ++i) {
        MdoPowerManagerStatus status;
        memset(&status, 0, sizeof(status));
        status.Size = sizeof(status);
        if (MdoPowerManagerGetStatus(&status) &&
            status.Configured == configured && status.Running == running &&
            status.Active == active) return true;
        xrtSleep(25u);
    }
    return false;
}

void ServiceInit(XS_HostInfo* host) {
    MdoPowerManagerStatus status;
    bool available;
    const char* on = "{\"schema_version\":1,\"patch\":{\"power\":{\"prevent_sleep\":true}}}";
    const char* off = "{\"schema_version\":1,\"patch\":{\"power\":{\"prevent_sleep\":false}}}";
    (void)host;
    xrtAtomic32Init(&Interactive, 0u);
    xrtAtomic32Init(&Scheduled, 0u);
    if (!MdoHomeInit() || !MdoConfigInit() || !MdoPowerManagerInit()) {
        printf("probe_error=init\n"); fflush(stdout); return;
    }
    memset(&status, 0, sizeof(status)); status.Size = sizeof(status);
    if (!MdoPowerManagerGetStatus(&status) || !status.Checked) {
        printf("probe_error=probe\n"); fflush(stdout); return;
    }
    available = status.Available;
    if (!WaitFor(false, false, false) ||
        !MdoConfigImport(MDO_CONFIG_SETTINGS, xrtStrView(on))) {
        printf("probe_error=enable\n"); fflush(stdout); return;
    }
    xrtAtomic32Store(&Interactive, 1u, XMEMORY_RELEASE);
    if (!WaitFor(true, true, available)) {
        printf("probe_error=interactive\n"); fflush(stdout); return;
    }
    xrtAtomic32Store(&Scheduled, 1u, XMEMORY_RELEASE);
    xrtAtomic32Store(&Interactive, 0u, XMEMORY_RELEASE);
    if (!WaitFor(true, true, available)) {
        printf("probe_error=scheduled\n"); fflush(stdout); return;
    }
    if (!MdoConfigImport(MDO_CONFIG_SETTINGS, xrtStrView(off)) ||
        !WaitFor(false, true, false)) {
        printf("probe_error=disable\n"); fflush(stdout); return;
    }
    xrtAtomic32Store(&Scheduled, 0u, XMEMORY_RELEASE);
    if (!WaitFor(false, false, false)) {
        printf("probe_error=finish\n"); fflush(stdout); return;
    }
    if (!MdoConfigImport(MDO_CONFIG_SETTINGS, xrtStrView(on))) {
        printf("probe_error=reenable\n"); fflush(stdout); return;
    }
    xrtAtomic32Store(&Scheduled, 1u, XMEMORY_RELEASE);
    if (!WaitFor(true, true, available)) {
        printf("probe_error=shutdown_active\n"); fflush(stdout); return;
    }
    MdoPowerManagerUnit();
    printf("power_available=%d probe_done=1\n", available ? 1 : 0);
    fflush(stdout);
}

void ServiceUnit(XS_HostInfo* host) {
    (void)host;
    MdoPowerManagerUnit();
    MdoConfigUnit();
    MdoHomeUnit();
}
'''


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
    with tempfile.TemporaryDirectory(prefix="power-runtime-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        site = base / "site"
        for relative in ("web", "default-home/config", "src/storage", "src/config",
                         "src/power", "include/mdo"):
            (site / relative).mkdir(parents=True, exist_ok=True)
        (site / "web/index.html").write_text("probe", encoding="utf-8")
        for relative in ("default-home/config/defaults.json", "src/storage/home.c",
                         "src/config/config.c", "src/power/inhibitor.c",
                         "src/power/manager.c"):
            shutil.copy2(ROOT / "app" / relative, site / relative)
        for header in (ROOT / "app/include/mdo").glob("*.h"):
            shutil.copy2(header, site / "include/mdo" / header.name)
        (site / "probe.c").write_text(PROBE_SOURCE, encoding="utf-8")
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            port = listener.getsockname()[1]
        (site / "xs.json").write_text(json.dumps({"services": [{
            "enabled": True, "class": "http", "name": "power-probe",
            "ip": "127.0.0.1", "port": port,
            "host_default": {"enabled": True, "name": "probe", "path": "web",
                             "devlang": "c", "devfile": "probe.c"},
        }]}), encoding="utf-8")
        process = subprocess.Popen(
            [str(host), "xs.json", "--", "--home", str(base / "home")],
            cwd=site, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, encoding="utf-8", errors="replace",
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0,
        )
        lines: list[str] = []
        done = threading.Event()
        assert process.stdout is not None

        def read_output() -> None:
            assert process.stdout is not None
            for line in process.stdout:
                lines.append(line)
                if "probe_done=1" in line or "probe_error=" in line:
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
        output = "".join(lines)
        assert "probe_done=1" in output and "probe_error=" not in output, output
        if os.name == "nt":
            assert "power_available=1" in output, output
    print("power runtime lifecycle probe: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
