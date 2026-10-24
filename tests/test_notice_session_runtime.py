"""A public notification lifetime stays stable within one native run only."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from test_api_runtime import free_port, request, wait_ready
from test_cli_tools_runtime import stage_forwarder

ROOT = Path(__file__).resolve().parents[1]

def run(host):
    base = Path(tempfile.mkdtemp(prefix="notice-session-", dir=ROOT / ".build"))
    site, home = base / "site", base / "home"
    shutil.copytree(ROOT / "app", site)
    stage_forwarder(site)
    (site / "probe.c").write_text('#define MDO_ACCOUNT_SERVICE_ORIGIN "http://127.0.0.1:1"\n#define MDO_DISTRIBUTION_ORIGIN "http://127.0.0.1:1"\n#define MDO_UPDATE_ORIGIN "http://127.0.0.1:1"\n#include "generated/mdo_unity.c"\n', encoding="utf-8")
    ids = []
    for attempt in range(2):
        port = free_port()
        config = dict(services=[dict(enabled=True, name="notice-session", ip="127.0.0.1", port=port,
                      host_default=dict(enabled=True, name="notice-session", path=str(site / "web"), devlang="c", devfile=str(site / "probe.c")))])
        config["services"][0]["class"] = "http"
        (site / "xs.json").write_text(json.dumps(config), encoding="utf-8")
        with (base / f"native-{attempt}.log").open("wb") as log:
            proc = subprocess.Popen([str(host), str(site / "xs.json")], cwd=ROOT,
                env={**os.environ, "MDO_HOME":str(home)}, stdout=log, stderr=log,
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            try:
                wait_ready(port, proc)
                def read():
                    status, _, body = request(port, "GET", "/api/v1/distribution")
                    assert status == 200, (status, body)
                    return json.loads(body)["data"]["notification_session"]
                value = read()
                assert re.fullmatch("[0-9a-f]{32}", value), value
                assert read() == value
                ids.append(value)
            finally:
                proc.terminate(); proc.wait(15)
    assert ids[0] != ids[1], "A restart must restore postponed notification prompts"
    print("PASS native notification lifetime stable within run, distinct after restart; private Home retained")

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", type=Path, default=ROOT / ".build/host/xs.exe")
    run(parser.parse_args().host.resolve())
