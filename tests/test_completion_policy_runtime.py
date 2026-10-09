"""Three bounded, offline write/finish cases through real mdo and xs/TCC."""
from __future__ import annotations

import argparse
from collections import Counter
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile
import threading
import time

from test_interrupt_runtime import free_port, request, start_host, stop_host

ROOT = Path(__file__).resolve().parents[1]


class Model(BaseHTTPRequestHandler):
    calls = Counter()
    gates = set()
    workspace: Path

    def log_message(self, *_args):
        pass

    def do_POST(self):
        assert self.path == "/v1/responses", self.path
        payload = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        inputs = payload.get("input", [])
        content = next(item["content"] for item in inputs if item.get("role") == "user")
        prompt = content if isinstance(content, str) else content[0]["text"]
        assert prompt in ("general", "coding", "coding-c"), prompt
        self.calls[prompt] += 1
        count = self.calls[prompt]
        gated = "Completion verification gate:" in json.dumps(inputs)
        if gated:
            self.gates.add(prompt)
        if count == 1:
            output = [{"type": "function_call", "name": "write", "call_id": "save-report",
                       "arguments": json.dumps({"path": prompt + ".md", "content": "Research report"})}]
        elif count == 3 and gated:
            target = str(self.workspace / (prompt + ".md"))
            output = [{"type": "function_call", "name": "exec", "call_id": "verify-report",
                       "arguments": json.dumps({"argv": [sys.executable, "-c",
                           "from pathlib import Path; "
                           f"assert Path({target!r}).read_text(encoding='utf-8') == 'Research report'"],
                           "timeout_ms": 5000})}]
        else:
            output = [{"type": "message", "content": [{"type": "output_text",
                       "text": "Complete report" if count == 2 else "Verification complete"}]}]
        body = json.dumps({"id": "completion-policy", "status": "completed",
                           "model": "ornith-1.5-35b", "output": output,
                           "usage": {"input_tokens": 7, "output_tokens": 3, "total_tokens": 10}}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def probe(host: Path):
    with tempfile.TemporaryDirectory(prefix="completion-policy-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        site = base / "site"
        shutil.copytree(ROOT / "app", site)
        agents = site / "default-home/agents"
        agents.joinpath("coding.md").write_text(
            "---\nname: Coding\ndescription: Explicit coding completion policy.\n"
            "require_verification_after_write: true\n---\n", encoding="utf-8")
        agents.joinpath("coding-c.md").write_text(
            "---\nname: Coding C\ndescription: C completion policy.\ncode: true\n---\n", encoding="utf-8")
        module = site / "default-home/modules/agents/builtin_default.c"
        source = module.read_text(encoding="utf-8").replace('"mdo.default"', '"agent.coding-c"')
        source = source.replace('"mdo.default-agent"', '"completion.coding-c"')
        source = source.replace("MDO_AGENT_MAIN |", "MDO_AGENT_REQUIRE_VERIFICATION_AFTER_WRITE | MDO_AGENT_MAIN |")
        module.with_name("completion_policy.c").write_text(source, encoding="utf-8")
        port = free_port()
        config = site / "xs.json"
        config.write_text(json.dumps({"services": [{"enabled": True, "class": "http", "name": "policy",
            "ip": "127.0.0.1", "port": port, "host_default": {"enabled": True, "name": "mdo",
            "path": "web", "devlang": "c", "devfile": "generated/mdo_unity.c"}}]}), encoding="utf-8")
        model = ThreadingHTTPServer(("127.0.0.1", 0), Model)
        model.daemon_threads = True
        Model.calls.clear()
        Model.gates.clear()
        Model.workspace = base
        threading.Thread(target=model.serve_forever, daemon=True).start()
        environment = dict(os.environ, USERPROFILE=str(base), HOME=str(base),
            MDO_ORNITH_RESPONSES_URL=f"http://127.0.0.1:{model.server_port}/v1",
            MDO_ORNITH_API_KEY="offline-completion-fixture")
        process = None
        try:
            process = start_host(host, config, base / "home", environment, base / "xs.log", port)
            for prompt, agent_id, expected_calls in (("general", "mdo.default", 2),
                    ("coding", "agent.coding", 4), ("coding-c", "agent.coding-c", 4)):
                status, body = request(port, "POST", "/api/v1/sessions", {"project_id": "default",
                    "title": prompt, "agent_id": agent_id, "model_id": "ornith-1.5-35b",
                    "protocol": "openai-responses", "permission_profile": "full-access",
                    "workspace_root": str(base), "max_output_tokens": 1024})
                assert status == 201, (status, body)
                sid = body["data"]["id"]
                status, body = request(port, "POST", f"/api/v1/projects/default/sessions/{sid}/runs", {"prompt": prompt})
                assert status == 202, (status, body)
                run_id = body["data"]["id"]
                deadline = time.monotonic() + 12
                while time.monotonic() < deadline:
                    status, body = request(port, "GET", "/api/v1/runs/" + run_id)
                    assert status == 200, body
                    if body["data"]["terminal"]:
                        break
                    time.sleep(0.02)
                else:
                    raise AssertionError("completion-policy bounded run timeout")
                assert body["data"]["state"] == "succeeded", body
                assert Model.calls[prompt] == expected_calls, Model.calls
                assert (prompt in Model.gates) == (prompt != "general"), Model.gates
                assert (base / (prompt + ".md")).read_text(encoding="utf-8") == "Research report"
                print(f"PASS {prompt}: {expected_calls} model calls, explicit verification={prompt != 'general'}")
        except BaseException:
            print((base / "xs.log").read_text(encoding="utf-8", errors="replace")[-6000:])
            raise
        finally:
            stop_host(process)
            model.shutdown()
            model.server_close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, required=True)
    probe(parser.parse_args().host.resolve())
