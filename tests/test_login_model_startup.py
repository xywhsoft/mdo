"""Restore a remembered account and online-model session with delayed HTTP.

All accounts, tokens, models and Homes are disposable loopback fixtures. Check
the real account worker, catalog, recovery and run admission across restarts.
"""
from __future__ import annotations

import argparse
import copy
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import threading
import time

from test_api_runtime import free_port, request, wait_ready

ROOT = Path(__file__).resolve().parents[1]
ONLINE = "mdo-online.startup-fixture"


class Service(BaseHTTPRequestHandler):
    login = threading.Event()
    catalog = threading.Event()
    login_started = threading.Event()
    catalog_started = threading.Event()
    removed = False
    catalog_status = 200
    model_calls = 0

    def log_message(self, *_):
        pass

    def reply(self, data, status=200):
        payload = json.dumps({"code": 0, "data": data}).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers.get("Content-Length", 0))) or b"{}")
        if self.path in ("/api/v1/login", "/api/v1/token/refresh"):
            self.login_started.set()
            assert self.login.wait(15), "fixture login was not released"
            self.reply({"id": 1, "access_token": "startup-fixture-token", "refresh_token": "a" * 64,
                        "token_type": "Bearer", "expires_in": 900})
        elif self.path == "/api/v1/ai/chat/completions":
            type(self).model_calls += 1
            response = {"id": "startup-reply", "object": "chat.completion", "model": "startup-fixture",
                        "choices": [{"index": 0, "message": {"role": "assistant", "content": "Ready."},
                                     "finish_reason": "stop"}],
                        "usage": {"prompt_tokens": 10, "completion_tokens": 2, "total_tokens": 12}}
            if body.get("stream"):
                delta = {"id": "startup-reply", "model": "startup-fixture", "choices": [
                    {"index": 0, "delta": {"role": "assistant", "content": "Ready."}, "finish_reason": "stop"}]}
                payload = ("data: " + json.dumps(delta) + "\n\ndata: [DONE]\n\n").encode()
                self.send_response(200)
                self.send_header("Content-Type", "text/event-stream")
                self.send_header("Content-Length", str(len(payload)))
                self.end_headers()
                self.wfile.write(payload)
            else:
                payload = json.dumps(response).encode()
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(payload)))
                self.end_headers()
                self.wfile.write(payload)
        else:
            self.reply({}, 200 if self.path == "/api/v1/logout" else 404)

    def do_GET(self):
        if self.path == "/api/v1/profile":
            self.reply({"id": 1, "username": "startup-fixture"})
        elif self.path == "/api/v1/search/usage":
            self.reply(dict.fromkeys(("minute_used", "minute_limit", "daily_used", "daily_limit", "daily_reset_at"), 0))
        elif self.path == "/api/v1/ai/allowance":
            self.reply({"member_id": 1, "plan_id": "fixture", "group_id": "fixture", "is_vip": False,
                        "plan_expires_at": 0, "updated_at": 1, "daily_quotas": []})
        elif self.path == "/api/v1/ai/catalog":
            self.catalog_started.set()
            assert self.catalog.wait(15), "fixture catalog was not released"
            model = {"id": "startup-fixture", "title": "Startup fixture", "available": True,
                     "member_only": True, "free": False, "tool_calling": True, "vision": False,
                     "context_window": 240128, "max_output": 16384, "reasoning_efforts": "none",
                     "output_limit_field": "max_tokens", "default_protocol": "chat", "protocols": ["chat"]}
            self.reply({"version": ("b" if self.removed else "a") * 64,
                        "models": [] if self.removed else [model]}, self.catalog_status)
        else:
            self.reply({}, 503)


def until(probe):
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        result = probe()
        if result:
            return result
        time.sleep(.03)
    raise AssertionError("bounded account/model transition did not complete")


def check(host: Path, base: Path):
    server = ThreadingHTTPServer(("127.0.0.1", 0), Service)
    server.daemon_threads = True
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    site, home = base / "site", base / "home"
    shutil.copytree(ROOT / "app", site)
    unity = site / "generated/mdo_unity.c"
    unity.write_text(f'#define MDO_ACCOUNT_SERVICE_ORIGIN "http://127.0.0.1:{server.server_port}"\n'
        '#define MDO_DISTRIBUTION_ORIGIN "http://127.0.0.1:9"\n'
        '#define MDO_DISTRIBUTION_HOST "127.0.0.1"\n#define MDO_DISTRIBUTION_PORT 9\n'
        '#define MDO_DISTRIBUTION_SECURE false\n' + unity.read_text(encoding="utf-8"), encoding="utf-8")
    defaults_path = site / "default-home/config/defaults.json"
    defaults = json.loads(defaults_path.read_text(encoding="utf-8"))
    personal = copy.deepcopy(defaults["models"]["items"][0])
    personal.update(id="mdo-online.personal", name="Personal fixture", builtin=False, editable=True, removable=True)
    defaults["models"]["items"].append(personal)
    defaults_path.write_text(json.dumps(defaults), encoding="utf-8")
    port = free_port()
    config = site / "xs.json"
    config.write_text(json.dumps({"services": [{"enabled": True, "class": "http", "name": "mdo",
        "ip": "127.0.0.1", "port": port, "host_default": {"enabled": True, "name": "mdo",
        "path": "web", "devlang": "c", "devfile": "generated/mdo_unity.c"}}]}), encoding="utf-8")
    process = None
    starts = 0

    def stop():
        nonlocal process
        if process and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
        process = None

    def start():
        nonlocal process, starts
        starts += 1
        log_path = base / f"native-{starts}.log"
        with log_path.open("wb") as log:
            process = subprocess.Popen([str(host), str(config), "--", "--home", str(home)], cwd=site,
                env={**os.environ, "USE_WEBVIEW": "0", "MDO_PLUGINS_DISABLED": "1"}, stdout=log, stderr=subprocess.STDOUT,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        try:
            wait_ready(port, process)
        except Exception:
            raise AssertionError(log_path.read_text(encoding="utf-8", errors="replace"))

    def call(method, path, body=None, expected=200):
        status, _, raw = request(port, method, "/api/v1" + path,
            body=json.dumps(body).encode() if body is not None else None,
            headers={"Content-Type": "application/json"})
        document = json.loads(raw)
        assert status == expected, (method, path, status, document)
        return document.get("data", document)

    def reject(path, kind, status=503, method="GET", body=None):
        result = call(method, path, body, status)
        assert result["error"]["code"] == "model_profile_" + kind, result

    def ready():
        value = call("GET", "/models")
        return value if value["login_state"] == "ready" else None

    try:
        start()
        assert call("GET", "/models")["login_state"] == "signed_out"
        # A personal identity in the online namespace is still a local model.
        local = call("POST", "/sessions", {"model_id": personal["id"], "title": "Personal", "project_id": "default"}, 201)
        assert local["model_id"] == personal["id"]
        call("POST", "/account/login", {"identifier": "fixture", "password": "fixture-only", "remember": True})
        assert Service.login_started.wait(3)
        reject("/sessions", "login_pending", method="POST", body={"model_id": ONLINE, "project_id": "default"})
        Service.login.set()
        assert Service.catalog_started.wait(3)
        assert call("GET", "/account")["state"] == "signed_in"
        assert call("GET", "/models")["login_state"] == "pending"
        Service.catalog.set()
        catalog = until(ready)
        assert next(m for m in catalog["models"] if m["id"] == ONLINE)["requires_login"] is True
        assert next(m for m in catalog["models"] if m["id"] == personal["id"])["requires_login"] is False
        assert call("GET", "/account")["remembered"] is True
        selected = call("POST", "/sessions", {"model_id": ONLINE, "reasoning_effort": "none",
            "project_id": "default", "title": "Restore online"}, 201)
        path = "/projects/default/sessions/" + selected["id"]
        call("GET", path + "/recovery")
        stop()

        Service.login.clear(); Service.catalog.clear()
        Service.login_started.clear(); Service.catalog_started.clear()
        start()
        assert Service.login_started.wait(3), "remembered account did not restore"
        reject(path + "/recovery", "login_pending")
        reject(path + "/runs", "login_pending", method="POST", body={"prompt": "Must wait"})
        assert Service.model_calls == 0
        Service.login.set()
        assert Service.catalog_started.wait(3)
        reject(path + "/recovery", "login_pending")
        Service.catalog.set()
        until(ready)
        call("GET", path + "/recovery")
        assert call("GET", path)["model_id"] == ONLINE
        run = call("POST", path + "/runs", {"prompt": "Ready after restoration"}, 202)
        finished = until(lambda: (r if (r := call("GET", "/runs/" + run["id"]))["terminal"] else None))
        assert finished["state"] == "succeeded", finished
        assert Service.model_calls == 1
        stop()

        Service.catalog_status = 503
        start()
        until(lambda: call("GET", "/account")["model_catalog_state"] == "unavailable")
        reject(path + "/recovery", "login_unavailable")
        stop()

        Service.catalog_status = 200; Service.removed = True
        start(); until(ready)
        reject(path + "/recovery", "model_missing", 422)
        assert call("GET", path)["model_id"] == ONLINE
        call("POST", "/account/logout", {})
        reject(path + "/recovery", "login_required", 422)
        return {"result": "PASS", "restarts": starts - 1, "model_calls": Service.model_calls}
    finally:
        Service.login.set(); Service.catalog.set()
        stop(); server.shutdown(); server.server_close(); worker.join(timeout=3)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, default=ROOT / ".build/host/xs.exe")
    parser.add_argument("--packed", type=Path, help="also check the final executable in an isolated Home")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="login-model-startup-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        proof = check(args.host.resolve(), base)
        if args.packed:
            from test_packed_home_lease import site, start, stop, wait_bootstrap
            packed = args.packed.resolve()
            native, port = site(base, "packed", packed)
            process = start(native, packed, base / "packed-home", {**os.environ, "USE_WEBVIEW": "0"})
            try:
                wait_bootstrap(process, port, native / "packed.log")
                status, _, raw = request(port, "GET", "/api/v1/models")
                catalog = json.loads(raw)["data"]
                assert status == 200 and catalog["login_state"] == "signed_out", catalog
                assert any(model["requires_login"] for model in catalog["models"]), catalog
                status, _, raw = request(port, "POST", "/api/v1/sessions",
                    body=json.dumps({"project_id": "default", "model_id": ONLINE}).encode(),
                    headers={"Content-Type": "application/json"})
                assert status == 422 and json.loads(raw)["error"]["code"] == "model_profile_login_required", raw
                status, _, raw = request(port, "GET", "/js/app.js")
                assert status == 200 and b"composerProfile.loginBlock()" in raw
                proof["packed"] = "PASS"
            finally:
                stop(process)
        print(json.dumps(proof), flush=True)


if __name__ == "__main__":
    main()
