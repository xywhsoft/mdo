"""One bounded request per provider through a disposable real xadmin instance.

Creates a fixture member, signs in for a real JWT, and keeps upstream keys/test
transport in xadmin only. Does not touch its running service or production DB.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import socket
import sqlite3
import subprocess
import sys
import time

from test_search_api_runtime import invoke, web


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, default=web.ROOT / ".build/host" / ("xs.exe" if os.name == "nt" else "xs"))
    parser.add_argument("--xadmin-root", type=Path, required=True)
    parser.add_argument("--xadmin-host", type=Path)
    args = parser.parse_args()
    root = args.xadmin_root.resolve()
    sys.path.insert(0, str(root / "tests"))
    import smoke
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        port = listener.getsockname()[1]
    origin = f"http://127.0.0.1:{port}"
    fixture = smoke.fixture(port)
    config = json.loads((fixture / "xs.json").read_text())
    config["services"][0]["host_default"]["devfile"] = str(root / "tests/search_host.c")
    (fixture / "xs.json").write_text(json.dumps(config))
    (fixture / "db/identity.json").write_text(json.dumps({"public_origin": origin}))
    env = os.environ.copy()
    env.pop("BOCHA_API_KEY", None)
    env.pop("ZAI_API_KEY", None)
    host = (args.xadmin_host or root / ("xs.exe" if os.name == "nt" else "xs")).resolve()
    with (fixture / "server.log").open("wb") as output:
        process = subprocess.Popen([str(host), str(fixture / "xs.json")], cwd=root,
                                   env=env, stdout=output, stderr=subprocess.STDOUT,
                                   creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
    def call(method, path, payload=None, cookie=None, headers=None, expected=200):
        status, response_headers, raw = smoke.request(port, method, path, payload, cookie, headers)
        assert status == expected, (path, status, raw)
        return json.loads(raw), response_headers
    try:
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            assert process.poll() is None, "xadmin fixture exited"
            try:
                if smoke.request(port, "GET", "/admin/login")[0] == 200:
                    break
            except OSError:
                pass
            time.sleep(.1)
        else:
            raise AssertionError("xadmin fixture startup timed out")
        admin, headers = call("POST", "/admin/login", {
            "username": smoke.USER, "password": smoke.client_hash(smoke.USER, smoke.PASSWORD)})
        assert admin["result"]
        cookie = headers["Set-Cookie"].split(";")[0]
        reply, _ = call("POST", "/admin/plugin/enable", {"name": "web-search"}, cookie)
        assert reply["result"]
        call("POST", "/api/v1/register", {"username": "mdo_search_member", "password": smoke.PASSWORD}, expected=201)
        login, _ = call("POST", "/api/v1/login", {"identifier": "mdo_search_member", "password": smoke.PASSWORD})
        token = login["data"]["access_token"]
        endpoint = origin + "/api/v1/search"
        denied = invoke(args.host, endpoint, [('{"query":"hello"}', False)], token, external=True)
        assert "phone/email verification" in denied
        # Fixture-only verified contact; production verification is not bypassed.
        with sqlite3.connect(fixture / "db/main.db") as db:
            db.execute("UPDATE member SET phone='+8613800138000',phone_key='+8613800138000',phone_verified_at=1 WHERE username='mdo_search_member'")
        missing = invoke(args.host, endpoint, [('{"query":"hello"}', False)], token, external=True)
        assert "configure its API key" in missing
        state, _ = call("GET", "/admin/web-search/credentials", cookie=cookie)
        call("POST", "/admin/web-search/credentials", {"bocha": "test-bocha-secret", "zai": "test-zai-secret"}, cookie,
             {"Origin": origin, "X-CSRF-Token": state["data"]["csrf_token"]})
        policy = json.loads((root / "plugin/web-search/config.defaults.json").read_text())
        for provider in ("bocha", "zai"):
            policy["default_provider"] = provider
            reply, _ = call("POST", "/admin/plugin/settings", {"name": "web-search", "config": policy}, cookie)
            assert reply["result"]
            output = invoke(args.host, endpoint, [('{"query":"hello","count":2}', True)], token, external=True)
            assert f'"source":"{provider}"' in output
            assert '"title":"测试标题"' in output
        print("PASS mdo -> xadmin JWT/contact/provider-key policy -> Bocha/z.ai adapters -> normalized search results")
    finally:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)


if __name__ == "__main__":
    main()
