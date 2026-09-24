#!/usr/bin/env python3
"""Bounded image-only and text-plus-image runs through xs/TCC and xwork."""

from __future__ import annotations

import argparse
import base64
import json
import os
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

from test_api_runtime import (ModelHandler, ModelServer, free_port, request,
                              wait_ready, write_site)


ROOT = Path(__file__).resolve().parent.parent


def wait_run(port: int, run_id: str) -> dict:
    deadline = time.monotonic() + 15.0
    run: dict = {}
    while time.monotonic() < deadline:
        status, _, body = request(port, "GET", f"/api/v1/runs/{run_id}")
        document = json.loads(body)
        assert status == 200, (status, document)
        run = document["data"]
        if run["terminal"]:
            return run
        time.sleep(0.02)
    raise AssertionError(
        f"image run {run_id} did not terminate: {run}; "
        f"model calls={ModelHandler.calls}; "
        f"payload={str(ModelHandler.last_payload)[:500]}")


def probe(host: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="mdo-image-probe-") as temp:
        base = Path(temp)
        site = base / "site"
        home = base / "mdo-home"
        port = free_port()
        config = write_site(site, port)
        defaults_path = site / "default-home/config/defaults.json"
        defaults = json.loads(defaults_path.read_text(encoding="utf-8"))
        model = defaults["models"]["items"][0]
        model["capabilities"].append("media-input")
        model["attachments"] = ["image"]
        defaults_path.write_text(json.dumps(defaults), encoding="utf-8")

        model_server = ModelServer(("127.0.0.1", 0), ModelHandler)
        model_port = int(model_server.server_address[1])
        model_thread = threading.Thread(target=model_server.serve_forever,
                                        daemon=True)
        ModelHandler.calls = 0
        ModelHandler.last_payload = None
        model_thread.start()
        environment = os.environ.copy()
        environment["USERPROFILE"] = str(base)
        environment["HOME"] = str(base)
        environment["MDO_LING_RESPONSES_URL"] = (
            f"http://127.0.0.1:{model_port}/v1")
        environment["MDO_LING_API_KEY"] = "bounded-image-test-key"
        log_path = base / "xs.log"
        try:
            with log_path.open("wb") as log:
                process = subprocess.Popen(
                    [str(host), str(config), "--", "--home", str(home)],
                    cwd=base, env=environment, stdout=log,
                    stderr=subprocess.STDOUT,
                    creationflags=(subprocess.CREATE_NO_WINDOW
                                   if os.name == "nt" else 0),
                )
                try:
                    wait_ready(port, process)
                    payload = json.dumps({
                        "project_id": "image-probe",
                        "title": "Image probe",
                        "model_id": "ling-3.0-tiny",
                        "protocol": "openai-responses",
                        "workspace_root": str(base),
                    }).encode()
                    status, _, body = request(port, "POST", "/api/v1/sessions",
                        body=payload, headers={"Content-Type": "application/json"})
                    document = json.loads(body)
                    assert status == 201, (status, document)
                    session_id = document["data"]["id"]
                    route = ("/api/v1/projects/image-probe/sessions/" +
                             session_id)
                    image_bytes = b"\x89PNG\r\n\x1a\n" + b"image-run-bytes"
                    status, _, body = request(port, "POST",
                        route + "/attachments", body=image_bytes,
                        headers={"Content-Type": "image/png"})
                    document = json.loads(body)
                    assert status == 201, (status, document)
                    image_id = document["data"]["id"]
                    completed: list[tuple[int, int]] = []
                    for refs, expected in (
                        (["0" * 32], "attachment_invalid"),
                        ([image_id, image_id], "run_start_invalid"),
                        ([image_id] * 5, "run_start_invalid"),
                    ):
                        status, _, body = request(port, "POST", route + "/runs",
                            body=json.dumps({"prompt": "", "attachments": refs}).encode(),
                            headers={"Content-Type": "application/json"})
                        rejected = json.loads(body)
                        assert status == 422 and rejected["error"]["code"] == (
                            expected), (status, rejected)
                    encoded = base64.b64encode(image_bytes).decode()
                    for prompt in ("", "Describe this image"):
                        ModelHandler.calls = 0
                        ModelHandler.last_payload = None
                        status, _, body = request(port, "POST", route + "/runs",
                            body=json.dumps({"prompt": prompt,
                                             "attachments": [image_id]}).encode(),
                            headers={"Content-Type": "application/json"})
                        document = json.loads(body)
                        assert status == 202, (status, document)
                        run = wait_run(port, document["data"]["id"])
                        assert run["state"] == "succeeded", run
                        status, _, body = request(port, "GET",
                            route + "/events?after=0&limit=32")
                        assert status == 200, (status, body)
                        starts = [item for item in json.loads(body)["data"]["items"]
                                  if item["kind"] == "agent_start" and
                                  item["run_id"] == run["agent_run_id"]]
                        assert len(starts) == 1 and starts[0]["attachments"] == (
                            [image_id]), starts
                        completed.append((starts[0]["event_id"],
                                          run["agent_run_id"]))
                        record = (home / "sessions/image-probe" / session_id /
                                  "attachments/events" /
                                  f"{starts[0]['event_id']}.json")
                        assert record.is_file(), record
                        wire = json.dumps(ModelHandler.last_payload)
                        assert "image/png" in wire and encoded in wire, wire[:1000]
                        if prompt:
                            assert prompt in wire, wire[:1000]
                    status, _, body = request(port, "DELETE",
                        route + "/attachments/" + image_id)
                    assert status == 409 and json.loads(body)["error"][
                        "code"] == "attachment_in_use", (status, body)
                finally:
                    if process.poll() is None:
                        process.terminate()
                        try:
                            process.wait(timeout=5)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.wait(timeout=3)
            # Simulate the two legacy run-ID records left by an older build.
            # New text and image runs reuse these IDs after host restart.
            attachment_root = home / "sessions/image-probe" / session_id / "attachments"
            (attachment_root / "runs").mkdir(exist_ok=True)
            for event_id, agent_run_id in completed:
                (attachment_root / "events" / f"{event_id}.json").replace(
                    attachment_root / "runs" / f"{agent_run_id}.json")
            with log_path.open("ab") as log:
                process = subprocess.Popen(
                    [str(host), str(config), "--", "--home", str(home)],
                    cwd=base, env=environment, stdout=log,
                    stderr=subprocess.STDOUT,
                    creationflags=(subprocess.CREATE_NO_WINDOW
                                   if os.name == "nt" else 0),
                )
                try:
                    wait_ready(port, process)
                    status, _, body = request(port, "GET",
                        route + "/events?after=0&limit=32")
                    assert status == 200, (status, body)
                    restored = [item for item in json.loads(body)["data"]["items"]
                                if item["kind"] == "agent_start"]
                    assert len(restored) == 2 and all(
                        item["attachments"] == [image_id] for item in restored), (
                        restored)
                    status, _, body = request(port, "DELETE",
                        route + "/attachments/" + image_id)
                    assert status == 409 and json.loads(body)["error"][
                        "code"] == "attachment_in_use", (status, body)
                    status, _, body = request(port, "POST",
                        route + "/attachments", body=image_bytes,
                        headers={"Content-Type": "image/png"})
                    trimmed_id = json.loads(body)["data"]["id"]
                    assert status == 201 and trimmed_id != image_id, body
                    for prompt, refs in (("text after restart", []),
                                         ("image after restart", [trimmed_id])):
                        ModelHandler.calls = 0
                        ModelHandler.last_payload = None
                        run_input = ({"prompt": prompt, "attachments": refs}
                                     if refs else {"prompt": prompt})
                        status, _, body = request(port, "POST", route + "/runs",
                            body=json.dumps(run_input).encode(),
                            headers={"Content-Type": "application/json"})
                        document = json.loads(body)
                        assert status == 202, (status, document)
                        run = wait_run(port, document["data"]["id"])
                        assert run["state"] == "succeeded", run
                        status, _, body = request(port, "GET",
                            route + "/events?after=0&limit=32")
                        assert status == 200, (status, body)
                        starts = [item for item in json.loads(body)["data"]["items"]
                                  if item["kind"] == "agent_start"]
                        assert len(starts) == 2 + (1 if not refs else 2), starts
                        assert [item["attachments"] for item in starts[:2]] == (
                            [[image_id], [image_id]]), starts
                        assert starts[-1]["attachments"] == refs, starts
                        if run["agent_run_id"] in [old[1] for old in completed]:
                            marker = (attachment_root / "events" /
                                      f"{starts[-1]['event_id']}.json")
                            assert marker.is_file(), marker
                    status, headers, body = request(port, "GET", route)
                    assert status == 200, (status, body)
                    first_sequence = starts[0]["user_message_sequence"]
                    source_file = attachment_root / f"{image_id}.bin"
                    hidden_file = attachment_root / f"{image_id}.held"
                    session_directories = home / "sessions/image-probe"
                    before = {path.name for path in session_directories.iterdir()}
                    source_file.rename(hidden_file)
                    try:
                        status, _, body = request(port, "POST", route + "/fork",
                            body=json.dumps({"title": "Broken image fork",
                                             "through_sequence": first_sequence}).encode(),
                            headers={"Content-Type": "application/json",
                                     "If-Match": headers["etag"]})
                        assert status >= 500, (status, body)
                        assert {path.name for path in session_directories.iterdir()} == (
                            before), list(session_directories.iterdir())
                    finally:
                        hidden_file.rename(source_file)
                    status, _, body = request(port, "POST", route + "/fork",
                        body=json.dumps({"title": "Image fork",
                                         "through_sequence": first_sequence}).encode(),
                        headers={"Content-Type": "application/json",
                                 "If-Match": headers["etag"]})
                    document = json.loads(body)
                    assert status == 201, (status, document)
                    child_id = document["data"]["id"]
                    child_route = ("/api/v1/projects/image-probe/sessions/" +
                                   child_id)
                    status, _, body = request(port, "GET",
                        child_route + "/events?after=0&limit=32")
                    assert status == 200, (status, body)
                    child_starts = [item for item in json.loads(body)["data"]["items"]
                                    if item["kind"] == "agent_start"]
                    assert len(child_starts) == 1 and (
                        child_starts[0]["attachments"] == [image_id]), child_starts
                    child_root = home / "sessions/image-probe" / child_id
                    assert (child_root / "attachments" / f"{image_id}.bin").read_bytes() == (
                        image_bytes)
                    trimmed_start = starts[-1]
                    assert trimmed_start["attachments"] == [trimmed_id], starts
                    trimmed_record = (attachment_root / "events" /
                                      f"{trimmed_start['event_id']}.json")
                    record_bytes = trimmed_record.read_bytes()
                    status, headers, body = request(port, "GET", route)
                    assert status == 200, (status, body)
                    status, _, body = request(port, "POST", route + "/truncate",
                        body=json.dumps({"through_sequence":
                            trimmed_start["user_message_sequence"] - 1}).encode(),
                        headers={"Content-Type": "application/json",
                                 "If-Match": headers["etag"]})
                    assert status == 200 and not trimmed_record.exists(), (
                        status, body)
                    # A stale reference left by an interrupted cleanup is
                    # pruned again before deciding whether the image is used.
                    trimmed_record.write_bytes(record_bytes)
                    status, _, body = request(port, "DELETE",
                        route + "/attachments/" + trimmed_id)
                    assert status == 200 and not trimmed_record.exists(), (
                        status, body)
                    (attachment_root / f"{image_id}.bin").unlink()
                    status, _, copied = request(port, "GET",
                        child_route + "/attachments/" + image_id)
                    assert status == 200 and copied == image_bytes, (
                        status, copied[:100])
                finally:
                    if process.poll() is None:
                        process.terminate()
                        try:
                            process.wait(timeout=5)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.wait(timeout=3)
        except BaseException as error:
            output = log_path.read_text(encoding="utf-8", errors="replace")
            raise RuntimeError(f"{error}\n--- xs log ---\n{output[-6000:]}") from error
        finally:
            model_server.shutdown()
            model_server.server_close()
            model_thread.join(timeout=3)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path,
        default=ROOT / ".build/host" / ("xs.exe" if os.name == "nt" else "xs"))
    args = parser.parse_args()
    probe(args.host.resolve())
    print("image runtime probe: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
