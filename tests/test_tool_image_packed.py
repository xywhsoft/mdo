"""Real packed read(PNG), model interruption and restart/resume on loopback."""
from __future__ import annotations

import argparse
import base64
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import tempfile
import threading
import time

from test_api_runtime import request as raw_request
from test_packed_home_lease import ROOT, request, site, start, stop, wait_bootstrap, release_packed_copies
from test_packed_queue_start_recovery import configure_model


class Model(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    requests: list[dict] = []

    def log_message(self, *_):
        pass

    def do_POST(self):
        payload = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        self.requests.append(payload)
        index = len(self.requests)
        status = 200
        if index == 1:
            message = {"role": "assistant", "content": "", "tool_calls": [{
                "id": "packed-image-call", "type": "function", "function": {
                    "name": "read", "arguments": json.dumps({"path": "image.png"})}}]}
            finish = "tool_calls"
        elif index == 2:
            status = 400
        else:
            message = {"role": "assistant", "content": "Image tool task continued."}
            finish = "stop"
        document = {"error": {"code": "fixture_interruption", "message": "offline interruption"}} if status != 200 else {
            "id": "media-fixture", "model": payload["model"], "choices": [{
                "index": 0, "message": message, "finish_reason": finish}],
            "usage": {"prompt_tokens": 10, "completion_tokens": 5}}
        raw = json.dumps(document).encode()
        self.send_response(status);self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)));self.send_header("Connection", "close")
        self.end_headers();self.wfile.write(raw)


def wait_run(port: int, run_id: str) -> dict:
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        status, document = request(port, "GET", "/api/v1/runs/" + run_id)
        assert status == 200, document
        if document["data"]["terminal"]:
            return document["data"]
        time.sleep(.05)
    raise AssertionError("packed image run did not terminate")


def check(packed: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="tool-image-packed-", dir=ROOT / ".build") as raw:
        base = Path(raw);home = base / "home";workspace = base / "workspace";workspace.mkdir()
        image = base64.b64decode("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+/l9sAAAAASUVORK5CYII=")
        (workspace / "image.png").write_bytes(image)
        first_site, first_port = site(base, "first", packed)
        second_site, second_port = site(base, "second", packed)
        env = dict(os.environ, USERPROFILE=str(base), USE_WEBVIEW="0", MDO_QUEUE_FIXTURE_KEY="fixture-only")
        model = ThreadingHTTPServer(("127.0.0.1", 0), Model)
        Model.requests = [];worker = threading.Thread(target=model.serve_forever, daemon=True);worker.start()
        process = start(first_site, packed, home, env)
        try:
            status, bootstrap = wait_bootstrap(process, first_port, first_site / "packed.log")
            assert status == 200 and bootstrap["data"]["ready"], bootstrap
            configure_model(first_port, f"http://127.0.0.1:{model.server_port}/v1")
            status, headers, raw_config = raw_request(first_port, "GET", "/api/v1/models/config")
            assert status == 200
            config = json.loads(raw_config)["data"];config.pop("runtime_override", None)
            text = next(item for item in config["items"] if item["id"] == "queue-fixture")
            text["capabilities"].remove("media-input");text["attachments"] = []
            status, _, result = raw_request(first_port, "PUT", "/api/v1/settings/models",
                body=json.dumps({"schema_version": 1, "patch": config}).encode(),
                headers={"Content-Type": "application/json", "If-Match": headers["etag"]})
            assert status == 200, result
            status, document = request(first_port, "POST", "/api/v1/sessions", {
                "project_id": "default", "title": "Tool PNG continuation", "model_id": "queue-fixture",
                "protocol": "openai-chat-completions", "reasoning_effort": "none",
                "max_output_tokens": 1024, "workspace_root": str(workspace)})
            assert status == 201, document
            sid = document["data"]["id"];path = "/api/v1/projects/default/sessions/" + sid
            status, document = request(first_port, "POST", path + "/runs", {"prompt": "Read image.png."})
            assert status == 202, document
            interrupted = wait_run(first_port, document["data"]["id"])
            assert interrupted["state"] == "failed" and len(Model.requests) == 2, (interrupted, len(Model.requests))
            snapshot = home / "sessions/default" / sid / "snapshot.json"
            before = json.loads(snapshot.read_text(encoding="utf-8"))
            images = [entry for entry in before["entries"] if any(part["kind"] == 2 for part in entry.get("parts", []))]
            assert len(images) == 1 and any(part["kind"] == 0 for part in images[0]["parts"]), images
            stop(process)
            process = start(second_site, packed, home, env)
            status, bootstrap = wait_bootstrap(process, second_port, second_site / "packed.log")
            assert status == 200 and bootstrap["data"]["ready"], bootstrap
            status, recovery = request(second_port, "GET", path + "/recovery")
            assert status == 200 and recovery["data"]["resume_required"] and recovery["data"]["items"] == [], recovery
            status, document = request(second_port, "POST", path + "/resume", {
                "recovery_token": recovery["data"]["recovery_token"], "decisions": [],
                "client_resume_id": "d" * 32})
            assert status == 202, document
            final = wait_run(second_port, document["data"]["id"])
            assert final["state"] == "succeeded" and len(Model.requests) == 3, (final, len(Model.requests))
            for payload in Model.requests[1:]:
                tools = [message for message in payload["messages"] if message["role"] == "tool"]
                assert len(tools) == 1 and tools[0]["tool_call_id"] == "packed-image-call", tools
                text = tools[0]["content"]
                assert isinstance(text, str) and "This model cannot view images" in text and "image.png" in text, tools
                assert "data:image/" not in json.dumps(payload), "tool image leaked into text-model wire request"
            after = json.loads(snapshot.read_text(encoding="utf-8"))
            retained = [entry for entry in after["entries"] if any(part["kind"] == 2 for part in entry.get("parts", []))]
            assert retained == images, "resume changed original image result or replayed read"
        finally:
            stop(process);model.shutdown();model.server_close();worker.join(timeout=3)
            release_packed_copies(first_site / packed.name, second_site / packed.name)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--packed", type=Path, default=ROOT / "mdo.exe")
    check(parser.parse_args().packed.resolve())
    print("Packed PNG read, text-model wire request, restart/resume without tool replay: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
