"""Bounded raw file upload, tool read, fork and portable restore; local model only."""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from test_api_runtime import request as raw_request
from test_packed_home_lease import ROOT, request, site, start, stop, wait_bootstrap
from test_packed_queue_start_recovery import configure_model


def check(packed):
    leaf = "a" * 32 + ".json"
    payload = b'{"uploaded_evidence":"portable-file-fixture"}\n'
    calls = []

    class Model(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_POST(self):
            body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
            calls.append(body)
            message = {"role": "assistant", "content": "Uploaded file read successfully."}
            if len(calls) == 1:
                message = {"role": "assistant", "content": None, "tool_calls": [{"id": "read-upload",
                    "type": "function", "function": {"name": "read", "arguments": json.dumps({"path": "@uploads/" + leaf})}}]}
            raw = json.dumps({"id": "file-fixture", "choices": [{"index": 0, "message": message,
                "finish_reason": "tool_calls" if len(calls) == 1 else "stop"}],
                "usage": {"prompt_tokens": 30, "completion_tokens": 10}}).encode()
            self.send_response(200); self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(raw))); self.end_headers(); self.wfile.write(raw)

    peer = ThreadingHTTPServer(("127.0.0.1", 0), Model)
    peer.daemon_threads = True
    threading.Thread(target=peer.serve_forever, daemon=True).start()
    with tempfile.TemporaryDirectory(prefix="file-uploads-", dir=ROOT / ".build") as raw:
        base = Path(raw); native, port = site(base, "native", packed); home = base / "mdo-home"
        process = start(native, packed, home, dict(os.environ, MDO_QUEUE_FIXTURE_KEY="fixture-only"))

        def call(method, path, body=None, expected=200, headers=None):
            code, head, data = raw_request(port, method, "/api/v1" + path,
                body=None if body is None else json.dumps(body).encode(),
                headers={"Content-Type": "application/json", **(headers or {})})
            assert code == expected, (path, code, data[:1500])
            return json.loads(data).get("data"), head

        def wait(path, finished):
            deadline = time.monotonic() + 12
            while time.monotonic() < deadline:
                value, _ = call("GET", path)
                if finished(value):
                    return value
                time.sleep(.03)
            raise AssertionError((path, value))

        try:
            assert wait_bootstrap(process, port, native / "packed.log")[1]["data"]["ready"]
            configure_model(port, f"http://127.0.0.1:{peer.server_port}/v1/chat/completions")
            session, _ = call("POST", "/sessions", {"project_id": "default", "title": "File upload QA",
                "model_id": "queue-fixture", "reasoning_effort": "none", "permission_profile": "read-only"}, 201)
            prefix = f'/projects/default/sessions/{session["id"]}'
            uploads = prefix + "/uploads/"
            for name, content in [(leaf, payload), ("b" * 32 + ".json", b"not valid JSON\x00\xff"),
                                  ("c" * 32 + ".bin", b"\x00\xffraw binary")]:
                for status in (201, 200):
                    code, _, response = raw_request(port, "PUT", "/api/v1" + uploads + name,
                        body=content, headers={"Content-Type": "application/octet-stream"})
                    assert code == status, (code, response)
                    assert json.loads(response)["data"]["path"] == "@uploads/" + name
                code, _, _ = raw_request(port, "PUT", "/api/v1" + uploads + name, body=b"different",
                    headers={"Content-Type": "application/octet-stream"})
                assert code == 409
                assert (home / f'sessions/default/{session["id"]}/uploads' / name).read_bytes() == content
            code, _, _ = raw_request(port, "PUT", "/api/v1" + uploads + "invalid.json", body=b"{}",
                headers={"Content-Type": "application/octet-stream"})
            assert code == 422
            run, _ = call("POST", prefix + "/runs", {"prompt": "Read the user-uploaded JSON with the read tool."}, 202)
            completed = wait("/runs/" + run["id"], lambda row: row["terminal"])
            assert completed["state"] == "succeeded", completed
            tool_results = [message for message in calls[-1]["messages"] if message["role"] == "tool"]
            assert any("portable-file-fixture" in json.dumps(value) for value in tool_results), tool_results
            assert len(calls) == 2, calls
            _, headers = call("GET", prefix)
            child, _ = call("POST", prefix + "/fork", {"title": "Fork with files"}, 201,
                {"If-Match": headers["etag"]})
            original = home / f'sessions/default/{session["id"]}/uploads'
            forked = home / f'sessions/default/{child["id"]}/uploads'
            expected_files = {path.name: path.read_bytes() for path in original.iterdir()}
            assert {path.name: path.read_bytes() for path in forked.iterdir()} == expected_files
            code, _, archive = raw_request(port, "GET", "/api/v1" + prefix + "/backup")
            assert code == 200, archive[:1500]
            files = {row["path"]: base64.b64decode(row["data"]) for row in json.loads(archive)["files"]}
            assert all(files["uploads/" + name] == value for name, value in expected_files.items())
            upload_id = "e" * 32; upload = "/session-backups/uploads/" + upload_id
            call("POST", "/session-backups/uploads", {"id": upload_id, "bytes": len(archive),
                "sha256": hashlib.sha256(archive).hexdigest()}, 201)
            for offset in range(0, len(archive), 256 * 1024):
                code, _, response = raw_request(port, "PUT", "/api/v1" + upload + f"/chunks/{offset}",
                    body=archive[offset:offset + 256 * 1024], headers={"Content-Type": "application/octet-stream"})
                assert code == 200, response
            call("POST", upload + "/seal")
            preview, _ = call("POST", upload + "/preview", expected=202)
            preview_path = "/session-backups/previews/" + preview["id"]
            assert wait(preview_path, lambda row: row["terminal"])["state"] == "succeeded"
            target, _ = call("POST", preview_path + "/restore-review", {"project_id": "default"}, 201)
            restore_path = "/session-backups/restores/" + target["id"]
            call("POST", restore_path + "/apply", expected=202)
            restored = wait(restore_path, lambda row: row.get("worker_finished"))
            assert restored["committed"], restored
            destination = home / f'sessions/default/{target["id"]}/uploads'
            assert {path.name: path.read_bytes() for path in destination.iterdir()} == expected_files
            return {"raw_put_replay_and_conflict": True, "read_only_tool_alias": True,
                "fork_preserves_files": True, "backup_restores_opaque_json_and_binary": True,
                "real_model_calls": False, "local_model_calls": len(calls)}
        finally:
            stop(process); peer.shutdown(); peer.server_close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--packed", type=Path, required=True)
    parser.add_argument("--record", type=Path)
    args = parser.parse_args(); result = check(args.packed.resolve())
    if args.record: args.record.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))
