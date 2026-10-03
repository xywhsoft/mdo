#!/usr/bin/env python3
"""Bounded write attempts through real xs/TCC for the three composer profiles."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import sys
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from test_interrupt_runtime import free_port, request, start_host, stop_host


ROOT = Path(__file__).resolve().parent.parent


class Model(BaseHTTPRequestHandler):
    calls = {}
    lock = threading.Lock()
    workspace = None
    def log_message(self, *_args):
        pass

    def do_POST(self):
        if self.path != "/v1/responses":
            self.send_error(404)
            return
        payload = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        inputs = payload.get("input", [])
        content = next(item["content"] for item in inputs
                       if item.get("role") == "user")
        prompt = content if isinstance(content, str) else content[0]["text"]
        assert prompt.startswith("permission-probe-") and prompt.isascii()
        with Model.lock:
            count = Model.calls.get(prompt, 0) + 1
            Model.calls[prompt] = count
        output = [{"type": "message", "content": [
            {"type": "output_text", "text": "Bounded permission probe complete"}]}]
        if count == 1:
            # Even read-only attempts receive this malicious/out-of-catalog
            # call. The product must reject it before any filesystem write.
            output = [{"type": "function_call", "name": "write",
                       "call_id": "permission-write",
                       "arguments": json.dumps({"path": prompt + ".txt",
                                                 "content": prompt})}]
        elif count == 2:
            output = [{"type": "function_call", "name": "read",
                       "call_id": "permission-verify",
                       "arguments": json.dumps({"path": prompt + ".txt"})}]
        elif count == 3 and prompt.endswith(("-auto", "-allow")):
            target = str(Model.workspace / (prompt + ".txt"))
            output = [{"type": "function_call", "name": "exec",
                       "call_id": "permission-check",
                       "arguments": json.dumps({"argv": [sys.executable, "-c",
                           "from pathlib import Path; "
                           f"assert Path({target!r}).read_text(encoding='utf-8') == {prompt!r}"],
                           "timeout_ms": 5000})}]
        body = json.dumps({"id": "permission-response", "status": "completed",
                           "model": "ornith-1.5-35b", "output": output,
                           "usage": {"input_tokens": 7, "output_tokens": 3,
                                     "total_tokens": 10}}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def read(port, path):
    status, body = request(port, "GET", "/api/v1/" + path)
    assert status == 200, (path, status, body)
    return body["data"]


def probe(host):
    with tempfile.TemporaryDirectory(prefix="permission-profiles-",
                                     dir=ROOT / ".build") as raw:
        base = Path(raw)
        site = base / "site"
        shutil.copytree(ROOT / "app", site)
        # Declared read-only Agents must remain read-only even if the user
        # selects full-access. This also checks effective-profile precedence.
        agent = site / "default-home/modules/agents/builtin_default.c"
        source = agent.read_text(encoding="utf-8")
        readonly = source.replace('"mdo.default"', '"permission.readonly"')
        readonly = readonly.replace('"mdo.default-agent"', '"permission.readonly-module"')
        readonly = readonly.replace("MDO_AGENT_MAIN | MDO_AGENT_ALLOW_BACKGROUND",
                                    "MDO_AGENT_MAIN | MDO_AGENT_READ_ONLY | MDO_AGENT_ALLOW_BACKGROUND")
        assert readonly != source
        agent.with_name("permission_readonly.c").write_text(readonly, encoding="utf-8")
        port = free_port()
        config = site / "xs.json"
        config.write_text(json.dumps({"services": [{"enabled": True,
            "class": "http", "name": "permissions", "ip": "127.0.0.1",
            "port": port, "host_default": {"enabled": True, "name": "mdo",
                "path": "web", "devlang": "c", "devfile": "generated/mdo_unity.c"}}]}),
            encoding="utf-8")
        model = ThreadingHTTPServer(("127.0.0.1", 0), Model)
        Model.calls = {}
        Model.workspace = base
        model.daemon_threads = True
        threading.Thread(target=model.serve_forever, daemon=True).start()
        environment = dict(os.environ, USERPROFILE=str(base), HOME=str(base),
            MDO_ORNITH_RESPONSES_URL=f"http://127.0.0.1:{model.server_address[1]}/v1",
            MDO_ORNITH_API_KEY="bounded-permission-fixture-key")
        process = None
        try:
            process = start_host(host, config, base / "home", environment,
                                 base / "xs.log", port)
            cases = [("full-access", "mdo.default", "auto"),
                     ("balanced", "mdo.default", "allow"),
                     ("balanced", "mdo.default", "deny"),
                     ("read-only", "mdo.default", "readonly"),
                     ("full-access", "permission.readonly", "agent-readonly")]
            for profile, agent_id, label in cases:
                prompt = "permission-probe-" + label
                status, body = request(port, "POST", "/api/v1/sessions", {
                    "project_id": "default", "title": prompt,
                    "agent_id": agent_id, "model_id": "ornith-1.5-35b",
                    "protocol": "openai-responses", "permission_profile": profile,
                    "workspace_root": str(base), "max_output_tokens": 1024})
                assert status == 201, (status, body)
                sid = body["data"]["id"]
                status, body = request(port, "POST",
                    f"/api/v1/projects/default/sessions/{sid}/runs", {"prompt": prompt})
                assert status == 202, (status, body)
                run_id = body["data"]["id"]
                pending_seen = False
                resolved = False
                deadline = time.monotonic() + 12
                while time.monotonic() < deadline:
                    approvals = read(port, "approvals")["items"]
                    if approvals:
                        pending_seen = True
                        assert profile == "balanced" and not resolved, (label, approvals)
                        assert not (base / (prompt + ".txt")).exists()
                        item = approvals[0]
                        status, result = request(port, "PUT",
                            f'/api/v1/approvals/{item["id"]}',
                            {"decision": "allow_run" if label == "allow" else "deny"})
                        assert status == 200, (status, result)
                        resolved = True
                    run = read(port, "runs/" + run_id)
                    if run["terminal"]:
                        break
                    time.sleep(0.02)
                else:
                    raise AssertionError((label, "bounded run timeout"))
                assert run["state"] == "succeeded", (label, run)
                assert pending_seen == (profile == "balanced"), label
                target = base / (prompt + ".txt")
                assert target.exists() == (label in ("auto", "allow")), label
                if target.exists():
                    assert target.read_text(encoding="utf-8") == prompt
                assert not read(port, "approvals")["items"]
                print(f"Permission profile PASS: {profile} / {label}", flush=True)
            assert process.poll() is None
        except BaseException:
            if (base / "xs.log").exists():
                print((base / "xs.log").read_text(encoding="utf-8", errors="replace")[-6000:])
            raise
        finally:
            stop_host(process)
            model.shutdown()
            model.server_close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path,
                        default=ROOT / ".build/host" / ("xs.exe" if os.name == "nt" else "xs"))
    args = parser.parse_args()
    (ROOT / ".build").mkdir(exist_ok=True)
    probe(args.host.resolve())
    print("Permission profiles runtime PASS: 5 bounded cases; no external model traffic")


if __name__ == "__main__":
    main()
