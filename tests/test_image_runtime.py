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
                        wire = json.dumps(ModelHandler.last_payload)
                        assert "image/png" in wire and encoded in wire, wire[:1000]
                        if prompt:
                            assert prompt in wire, wire[:1000]
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
