"""Bounded MDO-2 configuration probe through the real xs/TCC runtime."""

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

static void PrintError(const char* sLabel)
{
    const xerror* pError = xrtGetError();
    printf("%s=%s\n", sLabel,
        pError != NULL ? xrtErrorMessage(pError) : "missing-error");
    xrtClearError();
}

void ServiceInit(XS_HostInfo* pHost)
{
    static const char sSettingsOne[] =
        "{\"schema_version\":1,\"patch\":{\"appearance\":{\"theme\":\"dark\"},"
        "\"future_option\":{\"enabled\":true}}}";
    static const char sSettingsTwo[] =
        "{\"schema_version\":1,\"patch\":{\"appearance\":{\"theme\":\"light\"}}}";
    static const char sPlainSecret[] =
        "{\"schema_version\":1,\"patch\":{\"api_key\":\"plaintext\"}}";
    static const char sEscapingCa[] =
        "{\"schema_version\":1,\"patch\":{\"transport\":{\"ca_pem_path\":\"../outside.pem\"}}}";
    static const char sIncompleteProxy[] =
        "{\"schema_version\":1,\"patch\":{\"transport\":{\"proxy\":{\"kind\":\"socks5\"}}}}";
    static const char sPlainProxyPassword[] =
        "{\"schema_version\":1,\"patch\":{\"transport\":{\"proxy\":{\"password\":\"plaintext\"}}}}";
    static const char sRemoveLing[] =
        "{\"schema_version\":1,\"patch\":{\"items\":[]}}";
    MdoConfigSnapshot Snapshot;
    MdoConfigPreview Preview;
    str sJson = NULL;
    size_t iSize = 0u;
    (void)pHost;

    if ( !MdoHomeInit() || !MdoConfigInit() ) {
        PrintError("probe_init_error");
        printf("probe_done=1\n");
        return;
    }
    memset(&Snapshot, 0, sizeof(Snapshot));
    Snapshot.Size = sizeof(Snapshot);
    if ( !MdoConfigGetSnapshot(&Snapshot) ) {
        PrintError("probe_snapshot_error");
        printf("probe_done=1\n");
        return;
    }
    sJson = MdoConfigEffectiveJson(&iSize);
    printf("initial_revision=%llu runtime=%d json=%s\n",
        (unsigned long long)Snapshot.Revision,
        Snapshot.RuntimeOverride ? 1 : 0, sJson != NULL ? sJson : "null");
    xrtFree(sJson);

    {
        uint32 i;
        for ( i = 0u; i < xsAppArgumentCount(); i++ ) {
            if ( strcmp(xsAppArgument(i), "--fault-write") == 0 ) {
                printf("fault_import=%d\n", MdoConfigImport(
                    MDO_CONFIG_SETTINGS, xrtStrView(sSettingsOne)) ? 1 : 0);
                if ( xrtGetError() != NULL ) PrintError("fault_error");
                sJson = MdoConfigEffectiveJson(&iSize);
                printf("fault_effective=%s\n", sJson != NULL ? sJson : "null");
                xrtFree(sJson);
                printf("probe_done=1\n");
                return;
            }
        }
    }

    memset(&Preview, 0, sizeof(Preview));
    Preview.Size = sizeof(Preview);
    printf("preview_one=%d changes=%d\n",
        MdoConfigPreviewImport(MDO_CONFIG_SETTINGS,
            xrtStrView(sSettingsOne), &Preview) ? 1 : 0,
        Preview.Changes ? 1 : 0);
    printf("import_one=%d\n", MdoConfigImport(MDO_CONFIG_SETTINGS,
        xrtStrView(sSettingsOne)) ? 1 : 0);
    sJson = MdoConfigExport(MDO_CONFIG_SETTINGS, false, &iSize);
    printf("export_one=%s\n", sJson != NULL ? sJson : "null");
    xrtFree(sJson);

    printf("import_two=%d\n", MdoConfigImport(MDO_CONFIG_SETTINGS,
        xrtStrView(sSettingsTwo)) ? 1 : 0);
    memset(&Preview, 0, sizeof(Preview));
    Preview.Size = sizeof(Preview);
    if ( MdoConfigPreviewImport(MDO_CONFIG_SETTINGS,
            xrtStrView(sPlainSecret), &Preview) )
        printf("plain_secret=accepted\n");
    else PrintError("plain_secret");
    memset(&Preview, 0, sizeof(Preview));
    Preview.Size = sizeof(Preview);
    printf("escaping_ca=%d\n", MdoConfigPreviewImport(MDO_CONFIG_SETTINGS,
        xrtStrView(sEscapingCa), &Preview) ? 1 : 0);
    printf("incomplete_proxy=%d\n", MdoConfigPreviewImport(MDO_CONFIG_SETTINGS,
        xrtStrView(sIncompleteProxy), &Preview) ? 1 : 0);
    printf("plain_proxy_password=%d\n", MdoConfigPreviewImport(MDO_CONFIG_SETTINGS,
        xrtStrView(sPlainProxyPassword), &Preview) ? 1 : 0);
    memset(&Preview, 0, sizeof(Preview));
    Preview.Size = sizeof(Preview);
    if ( MdoConfigPreviewImport(MDO_CONFIG_MODELS,
            xrtStrView(sRemoveLing), &Preview) )
        printf("remove_ling=accepted\n");
    else PrintError("remove_ling");

    memset(&Preview, 0, sizeof(Preview));
    Preview.Size = sizeof(Preview);
    printf("restore_preview=%d changes=%d\n",
        MdoConfigPreviewRestore(MDO_CONFIG_SETTINGS, &Preview) ? 1 : 0,
        Preview.Changes ? 1 : 0);
    printf("restore=%d\n", MdoConfigRestore(MDO_CONFIG_SETTINGS) ? 1 : 0);
    printf("probe_done=1\n");
}

void ServiceUnit(XS_HostInfo* pHost)
{
    (void)pHost;
    MdoConfigUnit();
    MdoHomeUnit();
}
'''


def write_site(site: Path) -> None:
    (site / "web").mkdir(parents=True)
    (site / "default-home" / "config").mkdir(parents=True)
    (site / "src" / "storage").mkdir(parents=True)
    (site / "src" / "config").mkdir(parents=True)
    (site / "include" / "mdo").mkdir(parents=True)
    (site / "web" / "index.html").write_text("probe", encoding="utf-8")
    shutil.copy2(ROOT / "app/default-home/config/defaults.json",
                 site / "default-home/config/defaults.json")
    for relative in ("src/storage/home.c", "src/storage/home_import.inc.c", "src/config/config.c",
                     "include/mdo/home.h", "include/mdo/home_import.h", "include/mdo/config.h"):
        shutil.copy2(ROOT / "app" / relative, site / relative)
    (site / "probe.c").write_text(PROBE_SOURCE, encoding="utf-8")
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        port = listener.getsockname()[1]
    (site / "xs.json").write_text(json.dumps({
        "services": [{
            "enabled": True,
            "class": "http",
            "name": "mdo-config-probe",
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
    }), encoding="utf-8")


def run_probe(host: Path, site: Path, home: Path, *,
              override: str | None = None,
              cli_override: str | None = None,
              extra_args: tuple[str, ...] = ()) -> str:
    environment = os.environ.copy()
    environment.pop("MDO_CONFIG_OVERRIDE", None)
    if override is not None:
        environment["MDO_CONFIG_OVERRIDE"] = override
    command = [str(host), "xs.json", "--", "--home", str(home)]
    if cli_override is not None:
        command.extend(("--config-override", cli_override))
    command.extend(extra_args)
    creationflags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
    process = subprocess.Popen(
        command, cwd=site, env=environment,
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
            if "probe_done=1" in line:
                done.set()

    reader = threading.Thread(target=read_output, daemon=True)
    reader.start()
    done.wait(timeout=10.0)
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
    default_host = ROOT / ".build" / "host" / ("xs.exe" if os.name == "nt" else "xs")
    parser.add_argument("--host", type=Path, default=default_host)
    args = parser.parse_args()
    host = args.host.resolve()
    if not host.is_file():
        print(f"missing xs host: {host}", file=sys.stderr)
        return 2

    (ROOT / ".build").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="config-runtime-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        site = base / "site"
        write_site(site)

        home = base / "state"
        output = run_probe(host, site, home)
        assert "probe_init_error=" not in output, output
        assert "runtime=0" in output, output
        assert "preview_one=1 changes=1" in output, output
        assert "import_one=1" in output and "import_two=1" in output, output
        assert '"future_option"' in output, output
        assert "sensitive values must be stored as secret_ref" in output, output
        assert "escaping_ca=0" in output, output
        assert "incomplete_proxy=0" in output, output
        assert "plain_proxy_password=0" in output, output
        assert "Ling 3.0 Tiny is built-in" in output, output
        assert "restore_preview=1 changes=1" in output, output
        assert "restore=1" in output, output
        assert not (home / "config/settings.json").exists()
        backup = json.loads((home / "config/settings.json.bak").read_text(
            encoding="utf-8"))
        assert backup["patch"]["appearance"]["theme"] == "light"
        for path in (home / "config").glob("*.json*"):
            if path.is_file():
                assert "plaintext" not in path.read_text(encoding="utf-8")

        layered = base / "layered"
        (layered / "config").mkdir(parents=True)
        (layered / "config/defaults.json").write_text(
            '{"schema_version":999,"models":{"items":[]}}', encoding="utf-8")
        output = run_probe(
            host, site, layered,
            override='{"settings":{"appearance":{"theme":"dark"}}}',
            cli_override='{"settings":{"appearance":{"theme":"light"}}}',
        )
        assert "probe_init_error=" not in output, output
        assert "runtime=1" in output, output
        initial = output.split(" json=", 1)[1].split("\npreview_one=", 1)[0]
        effective = json.loads(initial)
        assert effective["schema_version"] == 1
        assert effective["settings"]["appearance"]["theme"] == "light"
        assert effective["models"]["items"][0]["id"] == "ling-3.0-tiny"

        damaged = base / "damaged"
        (damaged / "config").mkdir(parents=True)
        (damaged / "config/settings.json").write_text(
            '{"schema_version":1,"patch":', encoding="utf-8")
        output = run_probe(host, site, damaged)
        assert "probe_init_error=" in output, output

        faulted = base / "faulted"
        (faulted / "config/settings.json.tmp/occupied").mkdir(parents=True)
        original = {
            "schema_version": 1,
            "patch": {"appearance": {"theme": "light"}},
        }
        (faulted / "config/settings.json").write_text(
            json.dumps(original), encoding="utf-8")
        output = run_probe(host, site, faulted, extra_args=("--fault-write",))
        assert "fault_import=0" in output, output
        fault_json = output.split("fault_effective=", 1)[1].split(
            "\nprobe_done=", 1)[0]
        fault_effective = json.loads(fault_json)
        assert fault_effective["settings"]["appearance"]["theme"] == "light"
        assert "future_option" not in fault_effective["settings"]
        assert json.loads((faulted / "config/settings.json").read_text(
            encoding="utf-8")) == original

    print("config runtime probe: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
