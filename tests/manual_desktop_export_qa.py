"""An isolated native Windows window for real clipboard and export checks.

Select the printed test session, send ``copy and export``, copy its response,
paste into the composer, and export Markdown/full backup normally. Files and
clipboard must be checked through the native window; no download/clipboard API
is mocked. Create the printed stop file to end the bounded fixture.
"""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import threading
import time

from test_api_runtime import request, session_events
from test_packed_home_lease import site, start, stop, wait_bootstrap
from test_packed_queue_start_recovery import configure_model

REPLY = "mdo desktop copy/export acceptance.\n\n中文：像素画笔与橡皮。\nEnglish: keep the original text.\nРусский: сохранить исходный текст.\n\n- Color: #34a1ff\n- File: pixel.png\n"


class Model(BaseHTTPRequestHandler):
    calls = 0

    def log_message(self, *_):
        pass

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        type(self).calls += 1
        raw = json.dumps({"id": "desktop-export", "model": body["model"], "choices": [{"index": 0,
            "message": {"role": "assistant", "content": REPLY}, "finish_reason": "stop"}],
            "usage": {"prompt_tokens": 50, "completion_tokens": 40}}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--packed", type=Path, required=True)
    parser.add_argument("--directory", type=Path, required=True)
    args = parser.parse_args()
    base = args.directory.resolve()
    assert not base.exists(), "Choose a fresh isolated directory"
    base.mkdir(parents=True)
    native, port = site(base, "native", args.packed.resolve())
    model = ThreadingHTTPServer(("127.0.0.1", 0), Model)
    worker = threading.Thread(target=model.serve_forever, daemon=True)
    worker.start()
    env = dict(os.environ, MDO_QUEUE_FIXTURE_KEY="fixture-only")
    process = start(native, args.packed.resolve(), base / "home", env)
    def call(method, path, body=None, expected=200):
        status, _, raw = request(port, method, path, body=None if body is None else json.dumps(body).encode(),
            headers={"Content-Type": "application/json"})
        assert status == expected, raw
        return json.loads(raw)["data"]
    try:
        status, bootstrap = wait_bootstrap(process, port, native / "packed.log")
        assert status == 200 and bootstrap["data"]["ready"], bootstrap
        configure_model(port, f"http://127.0.0.1:{model.server_port}/v1")
        session = call("POST", "/api/v1/sessions", {"project_id": "default", "title": "mdo-desktop-export-20261008",
            "model_id": "queue-fixture", "reasoning_effort": "none", "permission_profile": "balanced"}, 201)
        path = "/api/v1/projects/default/sessions/" + session["id"]
        # Configure using real APIs before opening the UI, so the first model
        # catalog already contains the fixture. An app service owns the native
        # window; USE_WEBVIEW does not turn an HTTP service into an app service.
        stop(process)
        config_path = native / "xs.json"
        config = json.loads(config_path.read_text(encoding="utf-8"))
        config["services"][0].update({"class": "app", "window": {
            "title": "mdo · 桌面导出验收", "width": 1280, "height": 900,
            "min_width": 360, "min_height": 320, "url": "/#/projects/default/sessions/" + session["id"],
            "devtools": False, "close": "stop", "profile_dir": {
                "argument": "--home", "environment": "MDO_HOME", "default": "mdo-home",
                "subdir": "data/cache/webview2"}, "install_fallback": "ask"}})
        config_path.write_text(json.dumps(config, ensure_ascii=False), encoding="utf-8")
        process = start(native, args.packed.resolve(), base / "home", env)
        status, bootstrap = wait_bootstrap(process, port, native / "packed.log")
        assert status == 200 and bootstrap["data"]["ready"], bootstrap
        print(json.dumps({"url": f"http://127.0.0.1:{port}/#/projects/default/sessions/" + session["id"],
            "native_port": port, "window_executable": str(native / args.packed.name), "session_id": session["id"],
            "stop_file": str(base / "stop")}), flush=True)
        end = time.monotonic() + 480
        while not (base / "stop").exists() and time.monotonic() < end:
            assert process.poll() is None, "Native fixture exited"
            time.sleep(.1)
        events = session_events(port, path)
        proof = {"model_calls": Model.calls, "final_errors": sum(event["kind"] == "error" for event in events),
            "expected_reply": REPLY, "native_window_executable": str(native / args.packed.name),
            "events": events}
        (base / "proof.json").write_text(json.dumps(proof, ensure_ascii=False, indent=2), encoding="utf-8")
    finally:
        stop(process)
        model.shutdown(); model.server_close(); worker.join(timeout=3)


if __name__ == "__main__":
    main()
