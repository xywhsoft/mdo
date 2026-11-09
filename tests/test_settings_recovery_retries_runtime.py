"""Recovery retry edits survive settings readback, later saves and restart.

Uses current product sources and the existing host in place. Only a tiny
owned Home, log and HTTP configuration are created; no model requests.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

from current_product_host import APP, product_http_config
from test_api_runtime import ROOT, free_port, request, wait_ready
from test_interrupt_runtime import stop_host


def run(host):
    port = free_port()
    with tempfile.TemporaryDirectory(prefix="settings-retries-", dir=ROOT / ".build") as raw, \
            product_http_config(port) as config:
        base = Path(raw)
        home = base / "home"
        (home / "config").mkdir(parents=True)
        (home / "config/settings.json").write_text(json.dumps({
            "schema_version": 1, "patch": {"locale": "en-US", "agent": {"memory": False}}
        }), encoding="utf-8")
        environment = dict(os.environ, MDO_HOME=str(home), USERPROFILE=str(base),
                           USE_WEBVIEW="0")
        process = None
        log = base / "host.log"

        def call(method, path="/settings", patch=None, etag=None, expected=200):
            status, headers, body = request(port, method, "/api/v1" + path,
                body=json.dumps({"schema_version": 1, "patch": patch}).encode()
                    if patch is not None else None,
                headers={"Content-Type": "application/json",
                         **({"If-Match": etag} if etag else {})})
            document = json.loads(body)
            assert status == expected, (status, document)
            return document.get("data"), headers.get("etag")

        def check(expected):
            snapshot, etag = call("GET")
            assert snapshot["agent"].get("recovery_retries") == expected, snapshot["agent"]
            return snapshot, etag

        def save(patch):
            _, etag = call("GET")
            return call("PATCH", "/settings/settings", patch, etag)

        def start():
            nonlocal process
            with log.open("ab") as output:
                process = subprocess.Popen([str(host), str(config), "--", "--home", str(home)],
                    cwd=APP, env=environment, stdout=output, stderr=subprocess.STDOUT,
                    creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            wait_ready(port, process)

        def restart(expected):
            nonlocal process
            stop_host(process)
            process = None
            stored = json.loads((home / "config/settings.json").read_text(encoding="utf-8"))
            assert stored["patch"]["agent"]["recovery_retries"] == expected, stored
            start()
            check(expected)

        try:
            start()
            check(10)
            for value in (0, 3, 100):
                save({"agent": {"recovery_retries": value}})
                snapshot, _ = check(value)
                # The settings form resubmits Agent fields after readback when
                # another setting changes. That must retain the user's count.
                save({"appearance": {"theme": "dark" if value != 3 else "light"},
                      "agent": snapshot["agent"]})
                check(value)
            for invalid in (-1, 101, 1.5, True, "5", None):
                _, etag = call("GET")
                call("PATCH", "/settings/settings", {"agent": {"recovery_retries": invalid}},
                     etag, expected=422)
                check(100)
            restart(100)
            save({"agent": {"recovery_retries": 0}})
            restart(0)
            _, etag = check(0)
            call("DELETE", "/settings/settings", etag=etag)
            check(10)
        except BaseException as error:
            raise AssertionError(f"{error}\n{log.read_text(errors='replace')[-2200:]}") from error
        finally:
            if process:
                stop_host(process)
    print("PASS recovery retries: readback, later full-Agent saves, 0/3/100, invalid values, restart and reset")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, default=ROOT / ".build/host/xs.exe")
    run(parser.parse_args().host.resolve())
