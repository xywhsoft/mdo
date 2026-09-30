#!/usr/bin/env python3
"""Bounded configuration/runtime transaction and rollback probe."""

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

#include "include/mdo/models.h"
#include "include/mdo/schedules.h"
#include "include/mdo/web.h"
#include "src/storage/home.c"
#include "src/config/config.c"

static unsigned ModelGeneration = 1u;
static unsigned WebGeneration = 1u;
static unsigned ScheduleGeneration = 1u;
static unsigned ModelCalls;
static unsigned WebCalls;
static unsigned ScheduleCalls;
static unsigned FailModels;
static unsigned FailWeb;
static unsigned FailSchedules;

bool MdoModelManagerReload(void) {
    ++ModelCalls;
    if (FailModels != 0u) { --FailModels; return false; }
    ++ModelGeneration;
    return true;
}

uint64 MdoModelManagerGeneration(void) { return ModelGeneration; }

bool MdoWebManagerReload(void) {
    ++WebCalls;
    if (FailWeb != 0u) { --FailWeb; return false; }
    ++WebGeneration;
    return true;
}

bool MdoWebManagerGetSnapshot(MdoWebSnapshot *snapshot) {
    uint32 size;
    if (snapshot == NULL || snapshot->Size < sizeof(*snapshot)) return false;
    size = snapshot->Size;
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->Size = size;
    snapshot->Generation = WebGeneration;
    snapshot->Enabled = true;
    return true;
}

bool MdoScheduleManagerReloadSettings(xwork_error *error) {
    xworkErrorInit(error);
    ++ScheduleCalls;
    if (FailSchedules != 0u) { --FailSchedules; return false; }
    ++ScheduleGeneration;
    return true;
}

uint64 MdoScheduleManagerGeneration(void) { return ScheduleGeneration; }

#include "src/config/service.c"

static void PrintResult(const char *label, bool ok,
    const MdoSettingsResult *result) {
    printf("%s=ok:%d status:%d changed:%d restored:%d previous:%llu revision:%llu model:%llu web:%llu schedule:%llu message:%s\n",
        label, ok ? 1 : 0, (int)result->Status,
        result->Changed ? 1 : 0, result->Restored ? 1 : 0,
        (unsigned long long)result->PreviousRevision,
        (unsigned long long)result->Revision,
        (unsigned long long)result->ModelGeneration,
        (unsigned long long)result->WebGeneration,
        (unsigned long long)result->ScheduleGeneration,
        result->Message);
}

static uint64 Revision(void) {
    MdoConfigSnapshot snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.Size = sizeof(snapshot);
    return MdoConfigGetSnapshot(&snapshot) ? snapshot.Revision : 0u;
}

void ServiceInit(XS_HostInfo *host) {
    static const char dark[] =
        "{\"schema_version\":1,\"patch\":{\"appearance\":{\"theme\":\"dark\"}}}";
    static const char light[] =
        "{\"schema_version\":1,\"patch\":{\"appearance\":{\"theme\":\"light\"}}}";
    static const char compact[] =
        "{\"schema_version\":1,\"patch\":{\"appearance\":{\"density\":\"compact\"}}}";
    static const char permissions[] =
        "{\"schema_version\":1,\"patch\":{\"default_profile\":\"read-only\"}}";
    MdoSettingsResult result;
    MdoSettingsServiceSnapshot service;
    char *exported;
    size_t exported_size = 0u;
    uint64 revision;
    bool ok;
    (void)host;

    if (!MdoHomeInit() || !MdoConfigInit() || !MdoSettingsServiceInit()) {
        printf("init_error=1\n"); goto done;
    }
    revision = Revision();
    printf("initial_revision=%llu\n", (unsigned long long)revision);

    memset(&result, 0, sizeof(result)); result.Size = sizeof(result);
    ok = MdoSettingsApply(MDO_CONFIG_SETTINGS, xrtStrView(dark), revision,
        &result);
    PrintResult("apply_dark", ok, &result);
    revision = result.Revision;

    memset(&result, 0, sizeof(result)); result.Size = sizeof(result);
    ok = MdoSettingsApply(MDO_CONFIG_SETTINGS, xrtStrView(light), 1u,
        &result);
    PrintResult("stale", ok, &result);

    memset(&result, 0, sizeof(result)); result.Size = sizeof(result);
    ok = MdoSettingsApply(MDO_CONFIG_SETTINGS, xrtStrView(dark), revision,
        &result);
    PrintResult("no_change", ok, &result);

    FailWeb = 1u;
    memset(&result, 0, sizeof(result)); result.Size = sizeof(result);
    ok = MdoSettingsApply(MDO_CONFIG_SETTINGS, xrtStrView(light), revision,
        &result);
    PrintResult("web_rejected", ok, &result);
    revision = result.Revision;
    exported = MdoConfigExport(MDO_CONFIG_SETTINGS, false, &exported_size);
    printf("after_web_reject=%s\n", exported != NULL ? exported : "null");
    xrtFree(exported);

    memset(&result, 0, sizeof(result)); result.Size = sizeof(result);
    ok = MdoSettingsApply(MDO_CONFIG_PERMISSIONS, xrtStrView(permissions),
        revision, &result);
    PrintResult("permissions", ok, &result);
    revision = result.Revision;

    memset(&result, 0, sizeof(result)); result.Size = sizeof(result);
    ok = MdoSettingsRestore(MDO_CONFIG_SETTINGS, revision, &result);
    PrintResult("restore", ok, &result);
    revision = result.Revision;

    FailSchedules = 1u;
    memset(&result, 0, sizeof(result)); result.Size = sizeof(result);
    ok = MdoSettingsApply(MDO_CONFIG_SETTINGS, xrtStrView(dark), revision,
        &result);
    PrintResult("schedule_rejected", ok, &result);
    revision = result.Revision;

    FailSchedules = 2u;
    memset(&result, 0, sizeof(result)); result.Size = sizeof(result);
    ok = MdoSettingsApply(MDO_CONFIG_SETTINGS, xrtStrView(compact), revision,
        &result);
    PrintResult("rollback_failed", ok, &result);
    revision = result.Revision;

    memset(&service, 0, sizeof(service)); service.Size = sizeof(service);
    if (MdoSettingsServiceGetSnapshot(&service))
        printf("service=initialized:%d degraded:%d transactions:%llu rollbacks:%llu last:%s\n",
            service.Initialized ? 1 : 0, service.Degraded ? 1 : 0,
            (unsigned long long)service.Transactions,
            (unsigned long long)service.Rollbacks, service.LastError);

    memset(&result, 0, sizeof(result)); result.Size = sizeof(result);
    ok = MdoSettingsRestore(MDO_CONFIG_PERMISSIONS, revision, &result);
    PrintResult("write_after_degraded", ok, &result);
    printf("calls=models:%u web:%u schedules:%u\n",
        ModelCalls, WebCalls, ScheduleCalls);
    printf("probe_done=1\n");
done:
    MdoSettingsServiceUnit();
    MdoConfigUnit();
    MdoHomeUnit();
}

void ServiceUnit(XS_HostInfo *host) { (void)host; }
'''


def write_site(site: Path) -> None:
    for relative in ("web", "default-home/config", "src/storage", "src/config",
                     "include/mdo"):
        (site / relative).mkdir(parents=True, exist_ok=True)
    (site / "web/index.html").write_text("probe", encoding="utf-8")
    for relative in (
        "default-home/config/defaults.json", "src/storage/home.c", "src/storage/home_import.inc.c", "src/storage/home_purge.inc.c",
        "src/config/config.c", "src/config/service.c",
    ):
        shutil.copy2(ROOT / "app" / relative, site / relative)
    for name in ("home.h", "home_import.h", "home_purge.h", "config.h", "models.h", "schedules.h",
                 "settings.h", "web.h"):
        shutil.copy2(ROOT / "app/include/mdo" / name,
                     site / "include/mdo" / name)
    (site / "probe.c").write_text(PROBE_SOURCE, encoding="utf-8")
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        port = listener.getsockname()[1]
    (site / "xs.json").write_text(json.dumps({"services": [{
        "enabled": True, "class": "http", "name": "settings-probe",
        "ip": "127.0.0.1", "port": port,
        "host_default": {"enabled": True, "name": "probe", "path": "web",
                         "devlang": "c", "devfile": "probe.c"},
    }]}), encoding="utf-8")


def run_probe(host: Path, site: Path, home: Path) -> str:
    process = subprocess.Popen(
        [str(host), "xs.json", "--", "--home", str(home)], cwd=site,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
        encoding="utf-8", errors="replace",
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
    with tempfile.TemporaryDirectory(prefix="settings-runtime-",
                                     dir=ROOT / ".build") as raw:
        base = Path(raw)
        site = base / "site"
        home = base / "home"
        write_site(site)
        output = run_probe(host, site, home)
        assert "init_error=" not in output, output
        assert "initial_revision=1" in output, output
        assert "apply_dark=ok:1 status:0 changed:1 restored:0 previous:1 revision:2" in output, output
        assert "stale=ok:0 status:2" in output and "revision:2" in output, output
        assert "no_change=ok:1 status:0 changed:0" in output, output
        assert "web_rejected=ok:0 status:5" in output and "revision:4" in output, output
        assert 'after_web_reject={\n  "schema_version": 1,' in output, output
        assert '"theme": "dark"' in output, output
        assert "permissions=ok:1 status:0 changed:1 restored:0 previous:4 revision:5" in output, output
        assert "restore=ok:1 status:0 changed:1 restored:1 previous:5 revision:6" in output, output
        assert "schedule_rejected=ok:0 status:5" in output and "revision:8" in output, output
        assert "rollback_failed=ok:0 status:6" in output and "revision:10" in output, output
        assert "service=initialized:1 degraded:1 transactions:3 rollbacks:3" in output, output
        assert "write_after_degraded=ok:0 status:7" in output, output
        assert "calls=models:0 web:7 schedules:6" in output, output
        assert "probe_done=1" in output, output
        permissions_file = home / "config/permissions.json"
        assert permissions_file.is_file(), output
        settings_file = home / "config/settings.json"
        assert not settings_file.exists(), list((home / "config").iterdir())
    print("settings transaction runtime probe: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
