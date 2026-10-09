"""Change a missing model without losing or replaying interrupted calls.

Serial, disposable native callbacks seed one read. Normal HTTP profile,
settings and recovery routes do the work; no provider requests are expected.
"""
from __future__ import annotations

import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import tempfile
import threading

from test_api_runtime import request
from test_interrupt_runtime import ROOT, start_host, stop_host, until
from test_packed_queue_start_recovery import configure_model
from test_recovery_history_runtime import prepare_fixture


class NoGeneration(BaseHTTPRequestHandler):
    calls = 0

    def log_message(self, *_):
        pass

    def do_POST(self):
        type(self).calls += 1
        self.send_error(503)


def check(host: Path, tool: str) -> dict:
    with tempfile.TemporaryDirectory(prefix="interrupted-profile-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        site, home, port, config = prepare_fixture(base)
        if tool == "write":
            fixture = site / "recovery-history.c"
            source = fixture.read_text(encoding="utf-8")
            name = 'RecoveryHistoryCopy("read")'
            arguments = 'RecoveryHistoryCopy("{\\\"path\\\":\\\"input.txt\\\"}")'
            assert source.count(name) == source.count(arguments) == 1
            source = source.replace(name, 'RecoveryHistoryCopy("write")').replace(arguments,
                'RecoveryHistoryCopy("{\\\"path\\\":\\\"output.txt\\\",\\\"content\\\":\\\"Recovered fixture\\\"}")')
            fixture.write_text(source, encoding="utf-8")
        process = None
        log = base / "xs.log"
        model = ThreadingHTTPServer(("127.0.0.1", 0), NoGeneration)
        NoGeneration.calls = 0
        worker = threading.Thread(target=model.serve_forever, daemon=True)
        worker.start()
        path = "/api/v1/projects/default/sessions/history-recovery"

        def call(method, target, body=None, expected=200, tag=None):
            status, headers, raw = request(port, method, target,
                body=json.dumps(body).encode() if body is not None else None,
                headers={"Content-Type": "application/json", **({"If-Match": tag} if tag else {})})
            document = json.loads(raw)
            assert status == expected, (status, document)
            return document.get("data", document), headers

        def switch(model_id, expected=200):
            meta, headers = call("GET", path)
            return call("PUT", path + "/profile", {"model_id": model_id,
                "reasoning_effort": "medium", "permission_profile": meta["permission_profile"]},
                expected, headers["etag"])[0]

        try:
            process = start_host(host, config, home,
                dict(os.environ, MDO_QUEUE_FIXTURE_KEY="fixture-only"), log, port)
            initial, _ = call("GET", path + "/recovery")
            assert initial["resume_required"] and len(initial["items"]) == 1
            assert initial["items"][0]["tool"] == tool
            assert not (base / "workspace/output.txt").exists()
            configure_model(port, f"http://127.0.0.1:{model.server_port}/v1")
            models, headers = call("GET", "/api/v1/models/config")
            models.pop("runtime_override", None)
            alternative = dict(next(item for item in models["items"] if item["id"] == "queue-fixture"))
            alternative.update(id="replacement-fixture", name="Replacement fixture")
            models["items"].append(alternative)
            call("PUT", "/api/v1/settings/models", {"schema_version": 1, "patch": models}, tag=headers["etag"])

            first = switch("queue-fixture")
            pending, _ = call("GET", path + "/recovery")
            assert pending["resume_required"] and pending["last_sequence"] == initial["last_sequence"]
            assert pending["items"][0]["tool_call_id"] == initial["items"][0]["tool_call_id"]
            assert call("GET", "/__probe/history")[0]["reads"] == 0
            assert not (base / "workspace/output.txt").exists()

            models, headers = call("GET", "/api/v1/models/config")
            models.pop("runtime_override", None)
            models["items"] = [item for item in models["items"] if item["id"] != "queue-fixture"]
            call("PUT", "/api/v1/settings/models", {"schema_version": 1, "patch": models}, tag=headers["etag"])
            error, _ = call("GET", path + "/recovery", expected=422)
            assert error["error"]["code"] == "model_profile_model_missing", error
            rejected = switch("missing-fixture", expected=422)
            assert rejected["error"]["code"] == "model_profile_model_missing", rejected
            unchanged, _ = call("GET", path)
            assert unchanged["model_id"] == first["model_id"] and unchanged["revision"] == first["revision"]

            changed = switch("replacement-fixture")
            restored, _ = call("GET", path + "/recovery")
            assert restored["resume_required"] and restored["last_sequence"] == initial["last_sequence"]
            assert restored["items"][0]["tool_call_id"] == initial["items"][0]["tool_call_id"]
            assert changed["revision"] == first["revision"] + 1
            assert call("GET", "/__probe/history")[0]["reads"] == 0
            assert not (base / "workspace/output.txt").exists()
            if tool == "write":
                assert restored["automatic_resume"] is False
            resumed, _ = call("POST", path + "/resume", {
                "recovery_token": restored["recovery_token"], "client_resume_id": "d" * 32,
                "decisions": [{"tool_call_id": restored["items"][0]["tool_call_id"], "action": "retry"}]}, 202)
            result = until(lambda: value if (value := call("GET", "/api/v1/runs/" + resumed["id"])[0])["terminal"] else None)
            assert result["state"] == "succeeded", result
            assert not call("GET", path + "/recovery")[0]["resume_required"]
            assert call("GET", "/__probe/history")[0]["reads"] == 1
            assert NoGeneration.calls == 0
            if tool == "write":
                assert (base / "workspace/output.txt").read_text(encoding="utf-8") == "Recovered fixture"
            evidence = {"result": "PASS", "tool": tool, "initial_pending_calls": 1, "model_changes": 2,
                "missing_model_recovery_status": 422, "missing_model_recovery_code": error["error"]["code"],
                "invalid_model_rejected_without_metadata_change": True,
                "pending_sequence_unchanged_by_model_changes": True, "tools_executed_by_model_changes": 0,
                "explicit_resume_result": result["state"], "completed_tools": 1,
                "provider_requests": NoGeneration.calls, "fixture": "native callbacks and real HTTP routes"}
            return evidence
        except BaseException:
            print(log.read_text(encoding="utf-8", errors="replace") if log.exists() else "no host log")
            raise
        finally:
            stop_host(process)
            model.shutdown(); model.server_close(); worker.join(timeout=3)


def run(host: Path, output: Path | None = None) -> None:
    evidence = {"checks": [check(host, tool) for tool in ("read", "write")]}
    if output:
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
    print("PASS missing model replaced with pending read/write intact; explicit resume executes each once")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    run(args.host.resolve(), args.output)
