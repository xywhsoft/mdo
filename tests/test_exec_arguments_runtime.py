#!/usr/bin/env python3
"""Preserve empty argv and distinguish argument failures from approval denials.

Exercise the real xs/TCC executor and batch planner with a loopback model.
All processes, markers, and the optional SSH key pair belong to a disposable
workspace; no SSH connection or external model request is made.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import sys
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from test_interrupt_runtime import free_port, request, start_host, stop_host


ROOT = Path(__file__).resolve().parent.parent


def call(name, arguments, identifier):
    return {"type": "function_call", "name": name, "call_id": identifier,
            "arguments": json.dumps(arguments)}


class Model(BaseHTTPRequestHandler):
    calls = {}
    cases = {}
    lock = threading.Lock()

    def log_message(self, *_args):
        pass

    def do_POST(self):
        if self.path != "/v1/responses":
            self.send_error(404)
            return
        payload = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        inputs = payload.get("input", [])
        content = next(item["content"] for item in inputs if item.get("role") == "user")
        label = content if isinstance(content, str) else content[0]["text"]
        case = Model.cases[label]
        with Model.lock:
            count = Model.calls.get(label, 0) + 1
            Model.calls[label] = count
        output = [{"type": "message", "content": [
            {"type": "output_text", "text": "Bounded argument probe complete"}]}]
        if count == 1:
            output = case["calls"]
        elif count == 2 and case.get("spawn"):
            prior = "\n".join(str(item.get("output", "")) for item in inputs
                              if item.get("type") == "function_call_output")
            match = re.search(r'"?task_id"?\s*:\s*(\d+)', prior)
            assert match, (label, "spawn did not return a task ID", prior)
            output = [call("wait", {"task_ids": [int(match[1])], "mode": "all",
                                     "timeout_ms": 5000}, label + "-wait")]
        elif count == 3 and case.get("spawn"):
            prior = [str(item.get("output", "")) for item in inputs
                     if item.get("type") == "function_call_output"]
            state = next(json.loads(text[text.index('{'):]) for text in reversed(prior)
                         if '{"tasks"' in text)
            output = [call("read", {"path": state["tasks"][0]["stdout"]["path"],
                                     "max_bytes": 4096}, label + "-read")]
        body = json.dumps({"id": "argument-response", "status": "completed",
            "model": "ornith-1.5-35b", "output": output,
            "usage": {"input_tokens": 7, "output_tokens": 3, "total_tokens": 10}}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def read(port, path):
    status, body = request(port, "GET", "/api/v1/" + path)
    assert status == 200, (path, status, body)
    return body["data"]


def make_cases(workspace):
    cases = {}

    def arguments(label, values):
        marker = workspace / (label + ".executed")
        script = ("import json,sys;from pathlib import Path;"
                  f"Path({str(marker)!r}).write_text('executed');"
                  "print(json.dumps(sys.argv[1:]))")
        return {"argv": [sys.executable, "-c", script, *values], "timeout_ms": 5000}

    def add(label, args, **options):
        tool = "spawn" if options.get("spawn") else "exec"
        cases[label] = {"calls": [call(tool, args, label)], **options}

    for label, values in (
        ("middle-empty", ["left", "", "right"]),
        ("consecutive-trailing-empty", ["", "", "end", "", ""]),
        ("quoted-unicode-empty", ['a "quote"', "a b", "中文", "", "\\", ""]),
        ("spawn-empty", ["first", "", "last", ""]),
        ("balanced-allow-empty", ["", "allowed", ""]),
        ("balanced-deny-empty", ["", "denied", ""]),
    ):
        add(label, arguments(label, values), expected_args=values,
            spawn=label == "spawn-empty",
            profile="balanced" if label.startswith("balanced-") else "full-access",
            decision="deny" if label == "balanced-deny-empty" else "allow")

    invalid = {
        "missing-argv": {}, "empty-argv": {"argv": []},
        "empty-program": {"argv": ["", "argument"]},
        "non-string-argument": {"argv": arguments("non-string-argument", [])["argv"] + [7]},
        "nul-argument": arguments("nul-argument", ["left\0right"]),
        "nul-program": {"argv": [sys.executable + "\0hidden"]},
        "too-many-arguments": arguments("too-many-arguments", [""] * 256),
        "oversize-preview": arguments("oversize-preview", ["x" * 4096]),
        "balanced-invalid": {"argv": ["", ""]},
        "spawn-invalid": {"argv": [sys.executable, "\0"]},
    }
    for label, args in invalid.items():
        add(label, args, invalid=True, spawn=False,
            profile="balanced" if label == "balanced-invalid" else "full-access")
    cases["spawn-invalid"]["calls"][0]["name"] = "spawn"
    label = "mixed-batch"
    cases[label] = {"calls": [call("exec", {"argv": [""]}, label + "-invalid"),
                                call("exec", arguments(label, ["", "batch", ""]),
                                     label + "-valid")],
                    "expected_args": ["", "batch", ""], "mixed": True}

    keygen = ROOT / "tools/runtime/windows-x86_64/openssh/ssh-keygen.exe"
    keygen = str(keygen) if os.name == "nt" and keygen.is_file() else shutil.which("ssh-keygen")
    if keygen:
        key = workspace / ".ssh/id_probe"
        key.parent.mkdir()
        add("ssh-keygen-empty-passphrase", {"argv": [keygen, "-q", "-t", "ed25519",
            "-N", "", "-C", "mdo-empty-argument-fixture", "-f", str(key)],
            "timeout_ms": 5000}, key=key)
    else:
        print("SSH key generation SKIP: ssh-keygen is unavailable", flush=True)
    return cases


def probe(host):
    proof = []
    with tempfile.TemporaryDirectory(prefix="exec-arguments-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        site = base / "site"
        shutil.copytree(ROOT / "app", site)
        workspace = base / "workspace"
        workspace.mkdir()
        Model.calls = {}
        Model.cases = make_cases(workspace)
        port = free_port()
        config = site / "xs.json"
        config.write_text(json.dumps({"services": [{"enabled": True, "class": "http",
            "name": "arguments", "ip": "127.0.0.1", "port": port,
            "host_default": {"enabled": True, "name": "mdo", "path": "web",
                             "devlang": "c", "devfile": "generated/mdo_unity.c"}}]}),
            encoding="utf-8")
        model = ThreadingHTTPServer(("127.0.0.1", 0), Model)
        model.daemon_threads = True
        threading.Thread(target=model.serve_forever, daemon=True).start()
        environment = dict(os.environ, USERPROFILE=str(base), HOME=str(base),
            MDO_ORNITH_RESPONSES_URL=f"http://127.0.0.1:{model.server_port}/v1",
            MDO_ORNITH_API_KEY="bounded-exec-arguments-fixture-key")
        process = None
        try:
            process = start_host(host, config, base / "home", environment,
                                 base / "xs.log", port)
            for label, case in Model.cases.items():
                profile = case.get("profile", "full-access")
                status, body = request(port, "POST", "/api/v1/sessions", {
                    "project_id": "default", "title": label, "agent_id": "mdo.default",
                    "model_id": "ornith-1.5-35b", "permission_profile": profile,
                    "workspace_root": str(workspace), "max_output_tokens": 1024})
                assert status == 201, (label, status, body)
                sid = body["data"]["id"]
                status, body = request(port, "POST",
                    f"/api/v1/projects/default/sessions/{sid}/runs", {"prompt": label})
                assert status == 202, (label, status, body)
                run_id = body["data"]["id"]
                approved = 0
                deadline = time.monotonic() + 15
                while time.monotonic() < deadline:
                    for approval in read(port, "approvals")["items"]:
                        assert profile == "balanced" and not case.get("invalid"), (label, approval)
                        assert approved == 0, (label, "unexpected second approval")
                        commands = [r["resource"] for r in approval["resources"] if r["kind"] == "command"]
                        assert len(commands) == 1 and '""' in commands[0], (label, commands)
                        assert not (workspace / (label + ".executed")).exists(), label
                        status, body = request(port, "PUT", f'/api/v1/approvals/{approval["id"]}',
                                               {"decision": case["decision"]})
                        assert status == 200, (label, status, body)
                        approved += 1
                    run = read(port, "runs/" + run_id)
                    if run["terminal"]:
                        break
                    time.sleep(0.02)
                else:
                    raise AssertionError((label, "bounded run timeout"))
                assert run["state"] == "succeeded", (label, run)
                assert approved == int(profile == "balanced" and not case.get("invalid")), label
                events = read(port, f"projects/default/sessions/{sid}/events?limit=32")["items"]
                results = [e for e in events if e.get("kind") == "tool_done"]
                expected_count = 3 if case.get("spawn") else 2 if case.get("mixed") else 1
                assert len(results) == expected_count, (label, results)
                invalid = [e for e in results if "invalid tool arguments:" in e.get("text", "")]
                denied = [e for e in results if "tool execution denied by approval policy" in e.get("text", "")]
                if case.get("invalid"):
                    assert len(invalid) == 1 and not invalid[0]["success"] and not denied, (label, results)
                elif label == "balanced-deny-empty":
                    assert len(denied) == 1 and not denied[0]["success"] and not invalid, results
                else:
                    assert not denied, (label, results)
                    assert len(invalid) == int(bool(case.get("mixed"))), (label, results)
                    succeeded = [e for e in results if e["success"]]
                    assert len(succeeded) == expected_count - len(invalid), (label, results)
                    text = "\n".join(e.get("text", "") for e in succeeded)
                    for event in succeeded:
                        raw = event.get("text", "")
                        try:
                            value = json.loads(raw[raw.index('{'):])
                        except (ValueError, TypeError):
                            continue
                        text += "\n" + value.get("stdout", {}).get("text", "")
                    if "expected_args" in case:
                        assert json.dumps(case["expected_args"]) in text, (label, text)
                        assert '""' in text, (label, "empty arguments invisible in preview")
                        assert (workspace / (label + ".executed")).exists(), label
                    if "key" in case:
                        key = case["key"]
                        assert key.is_file() and key.stat().st_size > 0, (label, text)
                        assert key.with_suffix(".pub").read_text().startswith("ssh-ed25519 "), label
                if case.get("invalid") or label == "balanced-deny-empty":
                    assert not (workspace / (label + ".executed")).exists(), label
                assert not read(port, "approvals")["items"], label
                proof.append({"case": label, "passed": True, "approvals": approved})
                print("Exec arguments PASS: " + label, flush=True)
            assert process.poll() is None
        except BaseException:
            if (base / "xs.log").exists():
                print((base / "xs.log").read_text(encoding="utf-8", errors="replace")[-6000:])
            raise
        finally:
            stop_host(process)
            model.shutdown()
            model.server_close()
    return {"passed": True, "external_model_or_ssh_traffic": False, "cases": proof}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path,
        default=ROOT / ".build/host" / ("xs.exe" if os.name == "nt" else "xs"))
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    (ROOT / ".build").mkdir(exist_ok=True)
    proof = probe(args.host.resolve())
    if args.report:
        args.report.write_text(json.dumps(proof, indent=2) + "\n", encoding="utf-8")
    print(f"Exec arguments runtime PASS: {len(proof['cases'])} bounded cases; no external traffic")


if __name__ == "__main__":
    main()
