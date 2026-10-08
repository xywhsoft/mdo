"""Bounded summary failure/cancellation, restart and continuation on real packs.

Three serial cases use only a synthetic loopback model and disposable Home.
No production credentials, paid models, tools, stress or high-load tests.
"""
import argparse
from collections import Counter
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import threading
import time

from test_api_runtime import ROOT, free_port, request, session_events, wait_ready
from test_packed_compaction_recovery import CODING_SUMMARY


class Model(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    mode = "valid"
    calls = Counter()
    requests = []

    def log_message(self, *_):
        pass

    def handle(self):
        try:
            super().handle()
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        summary = body["messages"][0]["content"].startswith("Create a precise continuation summary")
        kind = "summary" if summary else "normal"
        type(self).calls[kind] += 1
        type(self).requests.append({"kind": kind, "has_marker": "PIXEL_KEEP" in json.dumps(body["messages"]),
            "user_messages": sum(m["role"] == "user" for m in body["messages"]),
            "tools": len(body.get("tools", []))})
        failure = summary and self.mode in ("quota", "wait")
        if failure:
            value = {"error": {"code": "daily_token_limit" if self.mode == "quota" else "rate_limit_exceeded",
                "message": "Synthetic summary service response"}}
        else:
            text = ("invalid summary" if summary and self.mode == "quality" else CODING_SUMMARY
                if summary else "Reply complete; PIXEL_KEEP: dimensions32 palette8 output pixels.png.")
            value = {"id": "compaction-failure-fixture", "model": body["model"], "choices": [{"index": 0,
                "message": {"role": "assistant", "content": text}, "finish_reason": "stop"}],
                "usage": {"prompt_tokens": max(1, len(json.dumps(body["messages"])) // 4),
                    "completion_tokens": max(1, len(text) // 4)}}
        raw = json.dumps(value).encode()
        self.send_response(429 if failure else 200)
        if summary and self.mode == "wait":
            self.send_header("Retry-After", "10")
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(raw)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--packed", type=Path, required=True)
    parser.add_argument("--record", type=Path)
    parser.add_argument("--serve-ui", action="store_true", help="Keep the passing fixture open until its stop file is created")
    args = parser.parse_args()
    server = ThreadingHTTPServer(("127.0.0.1", 0), Model)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    records = []
    try:
        with tempfile.TemporaryDirectory(prefix="packed-context-failure-", dir=ROOT / ".build") as raw:
            base = Path(raw)
            exe = base / "mdo.exe"
            shutil.copy2(args.packed.resolve(), exe)
            port = free_port()
            (base / "xs.json").write_text(json.dumps({"services": [{"class": "http", "enabled": True,
                "name": "summary-failure-qa", "ip": "127.0.0.1", "port": port,
                "host_default": {"enabled": True, "name": "mdo", "path": "web", "devlang": "c",
                    "devfile": "generated/mdo_unity.c"}}]}))
            log = (base / "native.log").open("wb")
            env = dict(os.environ, USE_WEBVIEW="0", MDO_HOME=str(base / "home"), MDO_CONTEXT_FIXTURE_KEY="fixture-only")
            def start():
                child = subprocess.Popen([str(exe)], cwd=base, env=env, stdout=log, stderr=log,
                    creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
                wait_ready(port, child)
                return child
            process = start()
            try:
                def call(method, path, body=None, headers=None, status=200):
                    actual, fields, value = request(port, method, "/api/v1" + path,
                        body=None if body is None else json.dumps(body).encode(),
                        headers={"Content-Type": "application/json", **(headers or {})})
                    assert actual == status, (path, actual, value[:500])
                    return json.loads(value)["data"], fields
                def wait(run):
                    deadline = time.monotonic() + 20
                    while time.monotonic() < deadline:
                        value, _ = call("GET", "/runs/" + run["id"])
                        if value["terminal"]:
                            return value
                        time.sleep(.02)
                    raise AssertionError("bounded run did not finish")
                settings, fields = call("GET", "/models/config")
                provider = json.loads(json.dumps(settings["providers"][0]))
                provider.update(id="context-fixture", name="Context fixture", builtin=False, editable=True, removable=True,
                    endpoints={"chat_completions": f"http://127.0.0.1:{server.server_port}/v1"},
                    credential={"secret_ref": "env:MDO_CONTEXT_FIXTURE_KEY"})
                model = json.loads(json.dumps(settings["items"][0]))
                model.update(id="context-fixture", name="Context fixture", provider="context-fixture", builtin=False,
                    free=False, editable=True, removable=True, protocols=["openai-chat-completions"],
                    default_protocol="openai-chat-completions")
                model["window"].update(context_tokens=16384, max_input_tokens=16384, max_output_tokens=1024,
                    output_reserve_tokens=1024, summary_tokens=1024)
                settings["providers"].append(provider)
                settings["items"].append(model)
                settings.pop("runtime_override", None)
                call("PUT", "/settings/models", {"schema_version": 1, "patch": settings}, {"If-Match": fields["etag"]})
                for mode in ("quality", "quota", "wait"):
                    Model.mode = "valid"
                    Model.calls = Counter()
                    Model.requests = []
                    session, _ = call("POST", "/sessions", {"project_id": "default", "title": f"Summary {mode}",
                        "model_id": "context-fixture", "reasoning_effort": "none", "permission_profile": "read-only",
                        "max_output_tokens": 1024}, status=201)
                    path = "/projects/default/sessions/" + session["id"]
                    if mode == "quality":
                        quality_path = path
                    prompt = "PIXEL_KEEP: dimensions32 palette8 output pixels.png. " + "history-note " * 1000
                    run, _ = call("POST", path + "/runs", {"prompt": prompt}, status=202)
                    assert wait(run)["state"] == "succeeded"
                    Model.mode = mode
                    run, _ = call("POST", path + "/runs", {"prompt": "Continue the original task. " + prompt}, status=202)
                    if mode == "wait":
                        deadline = time.monotonic() + 5
                        while Model.calls["summary"] == 0:
                            assert time.monotonic() < deadline
                            time.sleep(.02)
                        cancelled_at = time.monotonic()
                        call("DELETE", "/runs/" + run["id"])
                    failed = wait(run)
                    cancel_ms = int((time.monotonic() - cancelled_at) * 1000) if mode == "wait" else None
                    assert failed["state"] == ("cancelled" if mode == "wait" else "failed"), failed
                    events = session_events(port, "/api/v1" + path)
                    errors = [e for e in events if e["kind"] == "error"]
                    assert len(errors) == (0 if mode == "wait" else 1), errors
                    assert Model.calls["normal"] == 1, Model.calls
                    assert Model.calls["summary"] == (2 if mode == "quality" else 1), Model.calls
                    expected = "context_compaction" if mode == "quality" else "daily_token_limit" if mode == "quota" else ""
                    before_kind = errors[0]["model_error_kind"] if errors else ""
                    if mode == "quality":
                        process.terminate()
                        process.wait(timeout=10)
                        process = start()
                        replayed = session_events(port, "/api/v1" + path)
                        assert [e for e in replayed if e["kind"] == "error"][0]["model_error_kind"] == before_kind
                    recovery, _ = call("GET", path + "/recovery")
                    assert recovery["resume_required"] and recovery["items"] == [], recovery
                    Model.mode = "valid"
                    resumed, _ = call("POST", path + "/resume", {"recovery_token": recovery["recovery_token"],
                        "decisions": [], "client_resume_id": "a" * 32}, status=202)
                    resumed = wait(resumed)
                    assert resumed["state"] == "succeeded" and resumed["resume"], resumed
                    recovery, _ = call("GET", path + "/recovery")
                    assert not recovery["resume_required"], recovery
                    followup, _ = call("POST", path + "/runs", {"prompt": "Continue; recall the original dimensions and filename."}, status=202)
                    assert wait(followup)["state"] == "succeeded"
                    events = session_events(port, "/api/v1" + path)
                    assert sum(e["kind"] == "agent_start" and e["user_message_sequence"] > 0 for e in events) == 3, events
                    assert sum(e["kind"] == "error" for e in events) == len(errors), events
                    assert Model.calls["normal"] == 3 and all(r["has_marker"] for r in Model.requests), Model.requests
                    assert all(r["tools"] == 0 for r in Model.requests if r["kind"] == "summary"), Model.requests
                    record = {"mode": mode, "kind": before_kind, "expected_kind": expected,
                        "calls": dict(Model.calls), "final_errors": len(errors), "resume_and_followup": True,
                        "restarted_before_resume": mode == "quality"}
                    if mode == "wait":
                        record["cancel_ms"] = cancel_ms
                        assert record["cancel_ms"] < 2000, record
                    records.append(record)
                    assert before_kind == expected, record
                result = {"passed": True, "cases": records}
                if args.record:
                    args.record.parent.mkdir(parents=True, exist_ok=True)
                    args.record.write_text(json.dumps(result, indent=2) + "\n")
                print(json.dumps(result, indent=2))
                if args.serve_ui:
                    stop = base / "stop"
                    print(json.dumps({"url": f"http://127.0.0.1:{port}/#{quality_path}",
                        "stop_file": str(stop)}), flush=True)
                    while not stop.exists():
                        assert process.poll() is None, "Packed summary failure fixture exited"
                        time.sleep(.2)
            except Exception:
                print((base / "native.log").read_text(errors="replace"))
                raise
            finally:
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=10)
                log.close()
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=3)


if __name__ == "__main__":
    main()
