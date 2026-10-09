"""Bounded settings snapshot/preview revision-boundary and credential probes.

Two one-shot mutations are injected into disposable API sources exactly between
their reads. This deterministically exercises a race without concurrent load.
The following writes/previews use unmodified native configuration transactions.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

from test_api_runtime import ROOT, free_port, request, wait_ready, write_site
from test_interrupt_runtime import stop_host


def inject(site: Path, relative: str, marker: str, name: str, theme: str) -> None:
    path = site / relative
    text = path.read_text(encoding="utf-8")
    helper = f'''
static void {name}(void)
{{
    static bool Done = false;
    MdoSettingsResult Result;
    if ( Done ) return;
    Done = true;
    memset(&Result, 0, sizeof(Result)); Result.Size = sizeof(Result);
    (void)MdoSettingsApply(MDO_CONFIG_SETTINGS,
        XRT_STR_LITERAL("{{\\"schema_version\\":1,\\"patch\\":{{\\"appearance\\":{{\\"theme\\":\\"{theme}\\"}}}}}}"),
        UINT64_MAX, &Result);
}}
'''
    include = '#include "../../include/mdo/settings.h"'
    assert text.count(include) == 1 and text.count(marker) == 1
    text = text.replace(include, include + "\n" + helper, 1)
    text = text.replace(marker, "    " + name + "();\n" + marker, 1)
    path.write_text(text, encoding="utf-8")


def run(host: Path) -> dict:
    with tempfile.TemporaryDirectory(prefix="settings-confirmation-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        port = free_port()
        config = write_site(base, port)
        inject(base, "src/api/state.c", "    Effective = EffectiveJson != NULL ?",
               "SettingsSnapshotProbe", "dark")
        inject(base, "src/api/mutations.c", "    xrtFree(Document);\n    MdoApiJsonBodyUnit(&Body);\n    memset(&Snapshot",
               "SettingsPreviewProbe", "light")
        environment = dict(os.environ, USERPROFILE=str(base), HOME=str(base), MDO_HOME=str(base / "home"))
        log_path = base / "host.log"
        with log_path.open("wb") as log:
            process = subprocess.Popen([str(host), str(config)], cwd=base, env=environment,
                stdout=log, stderr=subprocess.STDOUT,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
            try:
                wait_ready(port, process)
                status, _, body = request(port, "GET", "/api/v1/settings")
                assert status == 425 and json.loads(body)["error"]["code"] == "settings_snapshot_pending", body
                status, headers, body = request(port, "GET", "/api/v1/settings")
                settings = json.loads(body)["data"]
                assert status == 200 and settings["appearance"]["theme"] == "dark", body
                assert headers["etag"] == f'"mdo-config-{settings["revision"]}"'

                def preview(patch: dict) -> dict:
                    status, _, body = request(port, "PATCH", "/api/v1/settings/settings/preview",
                        body=json.dumps({"schema_version": 1, "patch": patch}).encode(),
                        headers={"Content-Type": "application/json"})
                    return {"status": status, **json.loads(body)}

                crossed = preview({"appearance": {"theme": "dark"}})
                assert crossed["status"] == 425 and crossed["error"]["code"] == "configuration_preview_pending", crossed
                stable = preview({"appearance": {"theme": "dark"}})
                assert stable["status"] == 200 and stable["data"]["changes"], stable
                status, headers, body = request(port, "GET", "/api/v1/settings")
                assert status == 200 and json.loads(body)["data"]["appearance"]["theme"] == "light", body
                proxy = {"kind": "none", "host": "127.0.0.1", "port": 18080,
                         "user": "probe", "bypass": "localhost",
                         "credential": {"secret_ref": "env:MDO_QA_PROXY_FIRST"}}
                patch = {"appearance": {"theme": "dark"}, "transport": {"proxy": proxy}}
                status, _, body = request(port, "PATCH", "/api/v1/settings/settings",
                    body=json.dumps({"schema_version": 1, "patch": patch}).encode(),
                    headers={"Content-Type": "application/json", "If-Match": headers["etag"]})
                assert status == 200, body
                mutation = json.loads(body)["data"]
                matching = preview(patch)
                assert matching["status"] == 200 and not matching["data"]["changes"], matching
                assert matching["data"]["current_revision"] == mutation["revision"]
                different = preview({"transport": {"proxy": {**proxy, "credential": {
                    "secret_ref": "env:MDO_QA_PROXY_SECOND"}}}})
                cleared = preview({"transport": {"proxy": {"credential": None}}})
                assert different["status"] == cleared["status"] == 200, (different, cleared)
                assert different["data"]["changes"] and cleared["data"]["changes"]
                status, headers, body = request(port, "GET", "/api/v1/settings")
                data = json.loads(body)["data"]
                assert status == 200 and data["transport"]["proxy"]["credential_configured"], body
                assert b"MDO_QA_PROXY_FIRST" not in body and b"MDO_QA_PROXY_SECOND" not in body
                status, _, body = request(port, "DELETE", "/api/v1/settings/settings", headers={"If-Match": headers["etag"]})
                assert status == 200 and json.loads(body)["data"]["restored"], body
                status, _, body = request(port, "GET", "/api/v1/settings")
                assert status == 200 and not json.loads(body)["data"]["user_patches"]["settings"], body
                return {"snapshot_boundary": 425, "preview_boundary": 425,
                    "matching_patch_changes": False, "replacement_credential_changes": True,
                    "cleared_credential_changes": True, "credential_reference_exposed": False,
                    "reset_user_patch": False}
            except BaseException as error:
                log.flush()
                raise RuntimeError(f"{error}\n{log_path.read_text(encoding='utf-8', errors='replace')[-3500:]}") from error
            finally:
                stop_host(process)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, required=True)
    parser.add_argument("--evidence", type=Path)
    args = parser.parse_args()
    result = run(args.host.resolve())
    if args.evidence:
        args.evidence.parent.mkdir(parents=True, exist_ok=True)
        args.evidence.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print("Settings confirmation native runtime: PASS (two deterministic read boundaries, credentials, reset)")


if __name__ == "__main__":
    main()
