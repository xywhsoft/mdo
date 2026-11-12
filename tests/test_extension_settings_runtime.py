"""Real extension settings API, credentials and upgrades; no app/SDK copies."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

from current_product_host import APP, product_http_config
from test_api_runtime import ROOT, free_port, request, wait_ready
from test_interrupt_runtime import stop_host

KINDS = dict(agents="agent", subagents="subagent", tools="tool", skills="skill",
             mcp="mcp", commands="command", **{"external-tools": "external-tool"},
             knowledge="knowledge", plugins="plugin")
FORM = {"schema": "mdo.extension-settings.v1", "config_version": 1,
        "default_locale": "en-US", "groups": [{"id": "main", "title": "Configuration", "fields": [
    {"name": "endpoint", "type": "url", "label": "Endpoint", "required": True},
    {"name": "api_key", "type": "secret", "label": "Key", "required": True},
    {"name": "character_name", "type": "text", "label": "Name", "default": "Mira", "expose_to_prompt": True},
    {"name": "count", "type": "integer", "label": "Count", "min": 0, "max": 100, "default": 5},
    {"name": "enabled", "type": "switch", "label": "Enabled", "default": False},
    {"name": "extra", "type": "text", "label": "Extra", "required": True,
     "visible_if": {"field": "enabled", "equals": True}}
]}]}


def run(host, browser=None):
    port = free_port()
    with tempfile.TemporaryDirectory(prefix="extension-settings-", dir=ROOT / ".build") as raw, product_http_config(port) as config:
        base = Path(raw); home = base / "home"; state = {}
        for kind, prefix in KINDS.items():
            root = home / kind / "qa-settings"; root.mkdir(parents=True)
            (root / "entry.txt").write_text("Fixture entry; disabled, never executed.")
            (root / "role.md").write_text("Name: {{$settings.character_name}}")
            (root / "settings.form.json").write_text(json.dumps(FORM))
            manifest = dict(kind=kind, id=prefix+":qa.settings", name="Settings fixture", alias="qa-settings",
                            entry="entry.txt", platforms=["any"], settings={"form": "settings.form.json", "templates": ["role.md"]})
            (root / "extension.json").write_text(json.dumps(manifest))
            state[kind+":qa-settings"] = dict(kind=kind, id="qa-settings", logical_id=manifest["id"], source="manual", enabled=False)
        (home / "data/extensions").mkdir(parents=True)
        (home / "data/extensions/state.json").write_text(json.dumps(state))
        (home / "config").mkdir(exist_ok=True)
        (home / "config/settings.json").write_text(json.dumps({"schema_version": 1, "patch": {"locale": "en-US"}}))
        environment = dict(os.environ, MDO_HOME=str(home), USERPROFILE=str(base), USE_WEBVIEW="0")
        process = None; log = base / "host.log"; checks = 0

        def check(condition, detail):
            nonlocal checks
            assert condition, detail
            checks += 1

        def call(method, kind="skills", body=None, preview=False, status=200, tag=None):
            path = f"/api/v1/extensions/{kind}/qa-settings/settings" + ("/preview" if preview else "")
            if body is not None and tag is None:
                tag = '"' + body["revision"] + '"'
            actual, headers, data = request(port, method, path,
                body=json.dumps(body).encode() if body is not None else None,
                headers={"Content-Type": "application/json", **({"If-Match": tag} if tag else {})})
            document = json.loads(data)
            check(actual == status, (method, kind, actual, document))
            return document.get("data"), headers

        def mutation(current, **changes):
            return dict(revision=current["revision"], schema_digest=current["schema_digest"], **changes)

        def start():
            nonlocal process
            with log.open("ab") as output:
                process = subprocess.Popen([str(host), str(config), "--", "--home", str(home)],
                    cwd=APP, env=environment, stdout=output, stderr=subprocess.STDOUT,
                    creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            wait_ready(port, process)

        try:
            start()
            for kind in KINDS:
                current, headers = call("GET", kind)
                check(current["form"] == FORM and current["values"]["enabled"] is False, current)
                check(current["valid"] is False and headers["etag"] == '"'+current["revision"]+'"', current)
                check({i["field"] for i in current["issues"]} == {"endpoint", "api_key"}, current)
            current, _ = call("GET")
            initial = current["revision"]
            preview, _ = call("POST", body=mutation(current, values={"endpoint": "https://example.invalid/v1"}, secrets={"api_key": "fixture-key-never-export"}), preview=True)
            check(preview["valid"] and preview["secrets"]["api_key"] is True, preview)
            check(preview["templates"] == [{"file": "role.md", "text": "Name: Mira"}], preview)
            check("fixture-key-never-export" not in json.dumps(preview) and "vault:" not in json.dumps(preview), preview)
            check(not list((home / "config/extensions").glob("*.json")), "Preview wrote configuration")
            current, _ = call("GET"); check(current["revision"] == initial, current)
            saved, _ = call("PUT", body=mutation(current, values={"endpoint": "https://example.invalid/v1", "count": 0, "enabled": False}, secrets={"api_key": "fixture-key-never-export"}))
            check(saved["valid"] and saved["values"]["count"] == 0 and saved["values"]["enabled"] is False, saved)
            check("fixture-key-never-export" not in json.dumps(saved), saved)
            files = list((home / "config/extensions").glob("*.json")); check(len(files) == 1, files)
            private = files[0].read_text(); check("vault:" in private and "fixture-key-never-export" not in private, private)
            call("PUT", body=mutation(current, values={"count": 42}), status=412)
            current, _ = call("GET"); check(current["values"]["count"] == 0, current)
            for values in ({"count": -1}, {"count": True}, {"count": 101}, {"enabled": True}, {"api_key": "not-a-values-field"}, {"unknown": "rejected"}):
                call("PUT", body=mutation(current, values=values), status=422)
                after, _ = call("GET"); check(after["revision"] == current["revision"], after)
            current, _ = call("PUT", body=mutation(current, values={"character_name": "Ada"}))
            check(current["values"]["count"] == 0 and current["secrets"]["api_key"] is True, current)
            stop_host(process); process = None; start()
            current, _ = call("GET"); check(current["values"]["character_name"] == "Ada" and current["values"]["count"] == 0, current)
            form_path = home / "skills/qa-settings/settings.form.json"
            upgraded = json.loads(json.dumps(FORM)); upgraded["config_version"] = 2
            upgraded["groups"][0]["fields"].append(dict(name="new_field", type="text", label="New", required=True))
            form_path.write_text(json.dumps(upgraded))
            next_form, _ = call("GET"); check(not next_form["valid"] and next_form["values"]["character_name"] == "Ada", next_form)
            call("PUT", body=mutation(current, values={"character_name": "stale edit"}), status=412)
            repaired, _ = call("PUT", body=mutation(next_form, values={"new_field": "complete"}))
            check(repaired["valid"] and repaired["secrets"]["api_key"] is True, repaired)
            form_path.write_text('{"schema":"broken"}')
            call("GET", status=422)
            call("GET", kind="agents")
            form_path.write_text(json.dumps(upgraded))
            current, _ = call("GET")
            reset, _ = call("PUT", body=mutation(current, reset=True))
            check(reset["values"]["count"] == 5 and reset["values"]["character_name"] == "Mira" and reset["secrets"]["api_key"] is True, reset)
            cleared, _ = call("PUT", body=mutation(reset, reset=True, clear_secrets=True))
            check(not cleared["valid"] and not cleared["secrets"].get("api_key", False), cleared)
            if browser:
                proof = ROOT / ".build/extension-settings-browser"; proof.mkdir(exist_ok=True)
                for locale, width in (("en-US", 1400), ("ru-RU", 390), ("zh-CN", 1400)):
                    with tempfile.TemporaryDirectory(prefix="settings-browser-", dir=base) as profile:
                        result = subprocess.run(["node", str(ROOT / "tests/queue_browser_driver.mjs"), str(browser), profile,
                            f"http://127.0.0.1:{port}/?locale={locale}", str(width), str(proof / f"{locale}-{width}.png"),
                            str(ROOT / "tests/extension_settings_browser_driver.js"), "desktop"],
                            capture_output=True, timeout=50, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
                        report = json.loads(result.stdout.decode("utf-8")); (proof / f"{locale}-{width}.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
                        check(result.returncode == 0 and report.get("passed"), (report, result.stderr.decode("utf-8", errors="replace")[-1000:]))
        except BaseException as error:
            raise AssertionError(f"{error}\n{log.read_text(errors='replace')[-3000:]}") from error
        finally:
            if process: stop_host(process)
    print(f"PASS {checks} extension settings API checks: nine kinds, validation, credentials, conflict, upgrades, restart, reset")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, default=ROOT / ".build/host-model-policy/xs.exe")
    parser.add_argument("--browser", type=Path)
    args = parser.parse_args(); run(args.host.resolve(), args.browser)
