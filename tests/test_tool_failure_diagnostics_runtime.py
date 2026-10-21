"""Offline model fixture drives real process/edit tools through HTTP and TCC."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from test_interrupt_runtime import free_port, request, start_host, stop_host

ROOT = Path(__file__).resolve().parents[1]
SECRET = "argument-and-env-must-not-appear-7291"


def call(name, arguments, label):
    return {"type": "function_call", "call_id": label, "name": name,
            "arguments": json.dumps(arguments)}


class Model(BaseHTTPRequestHandler):
    cases = {}
    counts = {}

    def log_message(self, *_):
        pass

    def do_POST(self):
        assert self.path == "/v1/responses"
        payload = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        inputs = payload["input"]
        content = next(v["content"] for v in inputs if v.get("role") == "user")
        label = content if isinstance(content, str) else content[0]["text"]
        index = Model.counts.get(label, 0)
        Model.counts[label] = index + 1
        steps = Model.cases[label]["steps"]
        output = [{"type": "message", "content": [{"type": "output_text", "text": "Probe complete"}]}]
        if index < len(steps):
            name, arguments = steps[index]
            arguments = dict(arguments)
            if arguments.get("task_id") == -1 or arguments.get("task_ids") == [-1]:
                prior = "\n".join(str(v.get("output", "")) for v in inputs
                                  if v.get("type") == "function_call_output")
                match = re.search(r"task_id: (\d+)", prior)
                assert match, (label, prior)
                if "task_id" in arguments:
                    arguments["task_id"] = int(match[1])
                else:
                    arguments["task_ids"] = [int(match[1])]
            output = [call(name, arguments, label + "-" + str(index))]
        body = json.dumps({"id": "diagnostic-response", "status": "completed",
            "model": "ornith-1.5-35b", "output": output,
            "usage": {"input_tokens": 7, "output_tokens": 3, "total_tokens": 10}}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def cases(workspace):
    result = {}

    def add(label, steps, contains=(), success=False, **options):
        result[label] = dict(steps=steps, contains=contains, success=success, **options)

    for tool in ("exec", "spawn"):
        add(tool + "-missing-program", [(tool, {"argv": [str(workspace / "missing.exe"), SECRET],
            "env": ["MDO_DIAGNOSTIC_SECRET=" + SECRET]})], contains=("process_failure:", "reason=not_found", "system_code=", "operation=spawn.command" if os.name == "nt" else "operation=spawn.exec", "program:", "missing.exe"))
        add(tool + "-missing-cwd", [(tool, {"argv": [sys.executable, "-c", "print(42)"],
            "cwd": str(workspace / "missing-cwd")})], contains=("reason=not_found", "missing-cwd", "working directory does not exist"))
    if os.name == "nt":
        denied = workspace / "denied.exe"
        shutil.copy2(sys.executable, denied)
        acl = subprocess.run([str(Path(os.environ["SystemRoot"]) / "System32/icacls.exe"),
            str(denied), "/deny", os.environ["USERNAME"] + ":(X)"], capture_output=True)
        assert acl.returncode == 0, "Could not set execution-only ACL on disposable fixture"
        add("exec-access-denied", [("exec", {"argv": [str(denied)]})],
            contains=("reason=permission_denied", "system_code=5"))
        invalid = workspace / "invalid.exe"
        invalid.write_text("This is not an executable", encoding="utf-8")
        add("exec-invalid-image", [("exec", {"argv": [str(invalid)]})],
            contains=("process_failure:", "operation=spawn.exec", "system_message:"),
            one_of=("system_code=193", "system_code=216"))
    add("completed-task-stop", [("spawn", {"argv": [sys.executable, "-c", "print('done')"]}),
        ("wait", {"task_ids": [-1], "timeout_ms": 5000, "mode": "all"}),
        ("stop", {"task_id": -1, "release": False})], success=True, multi=True)
    add("live-task-stop", [("spawn", {"argv": [sys.executable, "-c", "import time;time.sleep(20)"]}),
        ("stop", {"task_id": -1, "mode": "interrupt", "wait_ms": 0, "release": False}),
        ("stop", {"task_id": -1, "mode": "kill_tree", "wait_ms": 5000, "release": True})], multi=True, stop=True)

    def edit(label, source, edits, *, contains=(), success=False, expected=None, **options):
        path = workspace / (label + ".txt")
        before = source.encode("utf-8")
        path.write_bytes(before)
        add(label, [("edit", {"path": str(path), "edits": edits})], contains,
            success, path=path, before=before, expected=expected, **options)

    header = "".join(f"// unrelated header {i}\n" for i in range(60))
    edit("edit-typo-late", header + "timeout_seconds = 300;\n",
        [{"old_text": "timeout_seconds = 310;", "new_text": "timeout_seconds = 600;"}],
        contains=("0 matches", "similar anchor at line 61", "timeout_seconds = 300;"))
    edit("edit-whitespace", header + "\treturn amount + 1;\n",
        [{"old_text": "    return amount + 1;\n", "new_text": "    return amount + 2;\n"}],
        contains=("similar anchor at line 61", "\\treturn amount + 1;", "similarity 1000/1000"))
    block = "marker_alpha();\nmarker_beta();\n"
    edit("edit-multiline-ambiguous", header + block + "\n" + block,
        [{"old_text": block, "new_text": "unique();\n"}],
        contains=("occurs 2 times", "exact match at line 61", "exact match at line 64", "marker_beta();"))
    edit("edit-atomic-batch", header + "original_first();\noriginal_second();\n",
        [{"old_text": "original_first();", "new_text": "changed_first();"},
         {"old_text": "original_seconx();", "new_text": "changed_second();"}],
        contains=("edit 1 failed", "original_second();"))
    edit("edit-no-candidate", header, [{"old_text": "完全不存在的待替换字符串", "new_text": "new"}],
        contains=("no sufficiently similar anchor found",), no_header=True)
    edit("edit-unicode-bounded", header + 'const char* title = "中文标题";\n' + "界" * 4000 + "\n",
        [{"old_text": 'const char* title = "中文标题!";', "new_text": "new"}],
        contains=("similar anchor at line 61", "中文标题", "line truncated"), bounded=True)
    edit("edit-crlf", "alpha();\r\nbeta();\r\n", [{"old_text": "alpha();\nbeta();\n", "new_text": "gamma();\n"}],
        success=True, expected=b"gamma();\r\n")
    edit("edit-replace-all", "old();\nold();\n", [{"old_text": "old();", "new_text": "new();", "replace_all": True}],
        success=True, expected=b"new();\nnew();\n")
    return result


def read(port, path):
    status, body = request(port, "GET", "/api/v1/" + path)
    assert status == 200, (path, status, body)
    return body["data"]


def probe(host):
    proof = []
    with tempfile.TemporaryDirectory(prefix="tool-diagnostics-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        site = base / "site"
        shutil.copytree(ROOT / "app", site)
        workspace = base / "workspace 中文"
        workspace.mkdir()
        Model.cases, Model.counts = cases(workspace), {}
        port = free_port()
        config = site / "xs.json"
        config.write_text(json.dumps({"services": [{"enabled": True, "class": "http", "name": "probe",
            "ip": "127.0.0.1", "port": port, "host_default": {"enabled": True, "name": "mdo",
                "path": "web", "devlang": "c", "devfile": "generated/mdo_unity.c"}}]}), encoding="utf-8")
        model = ThreadingHTTPServer(("127.0.0.1", 0), Model)
        model.daemon_threads = True
        threading.Thread(target=model.serve_forever, daemon=True).start()
        environment = dict(os.environ, USERPROFILE=str(base), HOME=str(base),
            MDO_ORNITH_RESPONSES_URL=f"http://127.0.0.1:{model.server_port}/v1",
            MDO_ORNITH_API_KEY="offline-diagnostic-fixture")
        process = None
        try:
            process = start_host(host, config, base / "home", environment, base / "xs.log", port)
            for label, case in Model.cases.items():
                status, body = request(port, "POST", "/api/v1/sessions", {
                    "project_id": "default", "title": label, "agent_id": "mdo.default",
                    "model_id": "ornith-1.5-35b", "permission_profile": "full-access",
                    "workspace_root": str(workspace), "max_output_tokens": 1024})
                assert status == 201, (label, status, body)
                sid = body["data"]["id"]
                status, body = request(port, "POST", f"/api/v1/projects/default/sessions/{sid}/runs", {"prompt": label})
                assert status == 202, (label, status, body)
                run_id = body["data"]["id"]
                deadline = time.monotonic() + 20
                while time.monotonic() < deadline:
                    run = read(port, "runs/" + run_id)
                    if run["terminal"]:
                        break
                    time.sleep(0.02)
                else:
                    raise AssertionError((label, "bounded probe timeout"))
                assert run["state"] == "succeeded", (label, run)
                events = read(port, f"projects/default/sessions/{sid}/events?limit=32")["items"]
                outputs = [v for v in events if v.get("kind") == "tool_done"]
                assert len(outputs) == len(case["steps"]), (label, outputs)
                if case.get("stop"):
                    assert outputs[0]["success"] and outputs[-1]["success"], (label, outputs)
                    text = outputs[1]["text"]
                    if "process_failure:" in text:
                        assert "process_failure: stage=interrupt" in text and "cause:" in text and "operation=" in text, text
                    else:
                        # Successful signal delivery with wait_ms=0 can leave a
                        # running task; tool success means a terminal snapshot.
                        assert "task_id:" in text and "state:" in text, text
                else:
                    assert all(v["success"] == case["success"] for v in outputs), (label, outputs)
                text = "\n".join(v.get("text", "") for v in outputs)
                assert SECRET not in text and "tool execution denied by approval policy" not in text, (label, text)
                for value in case["contains"]:
                    assert value in text, (label, value, text)
                if case.get("one_of"):
                    assert any(v in text for v in case["one_of"]), (label, text)
                if "path" in case:
                    assert case["path"].read_bytes() == (case["expected"] if case["success"] else case["before"]), label
                if case.get("no_header"):
                    assert "unrelated header" not in text, text
                if case.get("bounded"):
                    assert len(text.encode("utf-8")) < 8192 and "\ufffd" not in text, text
                proof.append({"case": label, "passed": True, "tools": len(outputs),
                              "failure_diagnostic": text[:3000] if not case["success"] else None})
                print("Tool diagnostic PASS: " + label, flush=True)
            assert process.poll() is None
        except BaseException:
            if (base / "xs.log").exists():
                print((base / "xs.log").read_text(encoding="utf-8", errors="replace")[-5000:])
            raise
        finally:
            stop_host(process)
            model.shutdown()
            model.server_close()
    return {"passed": True, "external_traffic": False, "cases": proof}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, required=True)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    result = probe(args.host.resolve())
    if args.report:
        args.report.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Tool diagnostics PASS: {len(result['cases'])} bounded cases; no external traffic")
