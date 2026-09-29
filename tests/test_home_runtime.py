"""Bounded MDO-1 Home integration probe through the real xs/TCC runtime."""

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
#include <stdlib.h>
#include <string.h>
#include <xsbase.h>

#include "src/storage/home.c"

static bool ProbeRead(char* sOutput, size_t iCapacity)
{
    xfile File = MdoResourceOpenRead("config/defaults.json");
    size_t iRead = 0u;
    if ( File == NULL ) return false;
    if ( !xrtRead(File, sOutput, iCapacity - 1u, &iRead) ) {
        (void)xrtClose(File);
        return false;
    }
    sOutput[iRead] = '\0';
    return xrtClose(File);
}

void ServiceInit(XS_HostInfo* pHost)
{
    static const char sSession[] = "durable-session";
    static const char* const sReservedPaths[] = {
        ".mdo.lock", ".MDO.LOCK", ".mdo.lock.", ".mdo.lock "
    };
    MdoHomeSnapshot Snapshot;
    char sResource[128];
    xfile File;
    size_t i;
    bool bRead;
    bool bWrite = false;
    bool bRename = false;
    bool bMaterialized = false;
    bool bReserved = true;
    (void)pHost;

    if ( !MdoHomeInit() ) {
        const xerror* Error = xrtGetError();
        printf("probe_init_error=1 message=%s\n",
            Error != NULL ? xrtErrorMessage(Error) : "unknown");
        return;
    }
    bRead = ProbeRead(sResource, sizeof(sResource));
    if ( !bRead ) {
        printf("probe_resource_error=1\n");
        return;
    }
    File = MdoHomeOpenWrite("sessions/probe.txt",
        XFILE_CREATE | XFILE_TRUNCATE | XFILE_SYNC);
    if ( File != NULL ) {
        bWrite = xrtWriteFull(File, sSession, sizeof(sSession) - 1u, NULL) &&
            xrtFlush(File) && xrtClose(File);
    }
    bMaterialized = MdoResourceMaterialize("config/defaults.json");
    if ( bWrite && MdoHomeRemove("sessions/moved.txt", false) ) {
        bRename = MdoHomeRenameNoReplace("sessions/probe.txt",
            "sessions/moved.txt") &&
            !MdoHomeRenameNoReplace("config/defaults.json",
                "sessions/moved.txt");
        xrtClearError();
    }
    for ( i = 0u; i < sizeof(sReservedPaths) / sizeof(sReservedPaths[0]); i++ ) {
        File = MdoHomeOpenWrite(sReservedPaths[i], XFILE_CREATE);
        bReserved = bReserved && File == NULL && xrtGetError() != NULL &&
            xrtErrorKind(xrtGetError()) == XERR_ARGUMENT;
        if (File != NULL) (void)xrtClose(File);
        xrtClearError();
    }
    memset(&Snapshot, 0, sizeof(Snapshot));
    Snapshot.Size = sizeof(Snapshot);
    printf("probe_resource=%s\n", sResource);
    printf("probe_reserved=%d\n", bReserved ? 1 : 0);
    printf("probe_rename=%d\n", bRename ? 1 : 0);
    printf("probe_ok=%d mode=%d overlay=%d materialized=%d\n",
        bWrite ? 1 : 0,
        MdoHomeGetSnapshot(&Snapshot) ? (int)Snapshot.Persistence : -1,
        Snapshot.ExternalOverlay ? 1 : 0,
        bMaterialized ? 1 : 0);
    if (getenv("MDO_PROBE_HOLD")) {
        fflush(stdout);
        xrtSleep(6000u);
    }
}

void ServiceUnit(XS_HostInfo* pHost)
{
    (void)pHost;
    MdoHomeUnit();
}
'''


def write_site(site: Path) -> None:
    (site / "web").mkdir(parents=True)
    (site / "default-home" / "config").mkdir(parents=True)
    (site / "src" / "storage").mkdir(parents=True)
    (site / "include" / "mdo").mkdir(parents=True)
    (site / "web" / "index.html").write_text("probe", encoding="utf-8")
    (site / "default-home" / "config" / "defaults.json").write_text(
        "builtin-default", encoding="utf-8")
    shutil.copy2(ROOT / "app" / "src" / "storage" / "home.c",
                 site / "src" / "storage" / "home.c")
    shutil.copy2(ROOT / "app" / "include" / "mdo" / "home.h",
                 site / "include" / "mdo" / "home.h")
    (site / "probe.c").write_text(PROBE_SOURCE, encoding="utf-8")
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        port = listener.getsockname()[1]
    config = {
        "services": [{
            "enabled": True,
            "class": "http",
            "name": "mdo-home-probe",
            "ip": "127.0.0.1",
            "port": port,
            "host_default": {
                "enabled": True,
                "name": "probe",
                "path": "web",
                "devlang": "c",
                "devfile": "probe.c",
            },
        }],
    }
    (site / "xs.json").write_text(json.dumps(config), encoding="utf-8")


def run_probe(host: Path, site: Path, home: Path) -> str:
    environment = os.environ.copy()
    wrong_home = home.parent / "wrong-environment-home"
    environment["MDO_HOME"] = str(wrong_home)
    creationflags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
    process = subprocess.Popen(
        [str(host), "xs.json", "--", "--home", str(home)],
        cwd=site, env=environment,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, encoding="utf-8", errors="replace",
        creationflags=creationflags,
    )
    lines: list[str] = []
    done = threading.Event()
    assert process.stdout is not None

    def read_output() -> None:
        assert process.stdout is not None
        for line in process.stdout:
            lines.append(line)
            if ("probe_ok=" in line or "probe_resource_error=" in line or
                    "probe_init_error=" in line):
                done.set()

    reader = threading.Thread(target=read_output, daemon=True)
    reader.start()
    done.wait(timeout=8.0)
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=3.0)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=3.0)
    reader.join(timeout=3.0)
    if wrong_home.exists():
        raise AssertionError("command-line Home did not override MDO_HOME")
    return "".join(lines)


def probe_exclusive_home(host: Path, base: Path) -> None:
    home = base / "shared-state"
    first_site = base / "first-site"
    second_site = base / "second-site"
    write_site(first_site)
    write_site(second_site)
    environment = os.environ.copy()
    environment["MDO_PROBE_HOLD"] = "1"
    creationflags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
    first = subprocess.Popen(
        [str(host), "xs.json", "--", "--home", str(home)],
        cwd=first_site, env=environment,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, encoding="utf-8", errors="replace",
        creationflags=creationflags,
    )
    lines: list[str] = []
    ready = threading.Event()
    assert first.stdout is not None

    def read_first() -> None:
        for line in first.stdout:
            lines.append(line)
            if "probe_ok=" in line or "probe_init_error=" in line:
                ready.set()

    reader = threading.Thread(target=read_first, daemon=True)
    reader.start()
    try:
        assert ready.wait(timeout=8.0), "first Home owner did not start"
        assert first.poll() is None, "first Home owner exited early"
        assert "probe_ok=1 mode=1" in "".join(lines), "".join(lines)
        rejected = run_probe(host, second_site, home)
        assert "probe_init_error=1" in rejected, rejected
        assert "external Home is already in use" in rejected, rejected
        assert first.poll() is None, "first Home owner exited during contention"
    finally:
        if first.poll() is None:
            first.terminate()
        first.wait(timeout=5.0)
        reader.join(timeout=3.0)
    restarted = run_probe(host, second_site, home)
    assert "probe_ok=1 mode=1" in restarted, restarted
    assert (home / ".mdo.lock").is_file()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    default_host = ROOT / ".build" / "host" / ("xs.exe" if os.name == "nt" else "xs")
    parser.add_argument("--host", type=Path, default=default_host)
    args = parser.parse_args()
    host = args.host.resolve()
    if not host.is_file():
        print(f"missing xs host: {host}", file=sys.stderr)
        return 2

    (ROOT / ".build").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="home-runtime-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        site = base / "site"
        home = base / "state"
        write_site(site)
        if home.exists():
            raise AssertionError("probe Home must begin absent")
        output = run_probe(host, site, home)
        assert "probe_resource=builtin-default" in output, output
        assert "probe_reserved=1" in output, output
        assert "probe_rename=1" in output, output
        assert "probe_ok=1 mode=1 overlay=1 materialized=1" in output, output
        assert (home / "sessions" / "moved.txt").read_text(
            encoding="utf-8") == "durable-session"
        assert (home / "config" / "defaults.json").read_text(
            encoding="utf-8") == "builtin-default"

        broken = base / "broken-state"
        (broken / "config" / "defaults.json").mkdir(parents=True)
        output = run_probe(host, site, broken)
        assert "probe_resource_error=1" in output, output
        assert "probe_resource=builtin-default" not in output, output

        blocker = base / "blocked-parent"
        blocker.write_text("not a directory", encoding="utf-8")
        ephemeral = blocker / "state"
        output = run_probe(host, site, ephemeral)
        assert "probe_resource=builtin-default" in output, output
        assert "probe_ok=0 mode=2 overlay=0 materialized=0" in output, output
        assert not ephemeral.exists()

        probe_exclusive_home(host, base)

    print("home runtime probe: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
