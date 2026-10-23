"""Real frontend/native context, history and inline edit regression; local model only."""
import argparse
from http.server import ThreadingHTTPServer
import json
import os
from pathlib import Path
import subprocess
import tempfile
import threading
import time
from test_api_runtime import ROOT, free_port, request, wait_ready, write_site
from test_packed_queue_start_recovery import configure_model
from test_queue_actions_runtime import Model
from test_packed_home_lease import site as packed_site


def check(host, browser, width, packed=None):
    with tempfile.TemporaryDirectory(prefix="composer-context-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        if packed:
            runtime, port = packed_site(base, "packed", packed); config = None
        else:
            runtime = base; port = free_port(); config = write_site(base, port)
        home = base / "home"; workspace = base / "workspace"
        (workspace / "src").mkdir(parents=True); (workspace / "src/readme.md").write_text("Fixture", encoding="utf-8")
        commands = workspace / ".mdo/commands"; commands.mkdir(parents=True)
        (commands / "context-qa.c").write_text('''/* mdo-command
{"id":"context-qa","description":"Expand an inline test draft","effects":["read"]}
*/
#include "mdo/command.h"
MDO_EXPORT mdo_result mdoCommandRun(const mdo_command_context_v1* C,
    const xvalue* A, xvalue** R) {
    (void)A; *R = C->Host->Parse("{\\"draft\\":\\"Expanded inline command\\"}");
    return *R ? MDO_RESULT_OK : MDO_RESULT_ERROR;
}
''', encoding="utf-8")
        server = ThreadingHTTPServer(("127.0.0.1", 0), Model)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        log = (base / "runtime.log").open("wb"); process = None
        env = {**os.environ, "USERPROFILE": str(base), "HOME": str(base), "USE_WEBVIEW": "0", "MDO_QUEUE_FIXTURE_KEY": "fixture-only"}
        def start():
            nonlocal process
            command = [str(runtime / packed.name)] if packed else [str(host), str(config)]
            process = subprocess.Popen([*command, "--", "--home", str(home)], cwd=runtime, env=env,
                stdout=log, stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
            wait_ready(port, process)
        def stop():
            nonlocal process
            if process: process.terminate(); process.wait(timeout=12); process = None
        def call(method, path, body=None, status=200):
            code, _, raw = request(port, method, "/api/v1" + path,
                body=json.dumps(body).encode() if body is not None else None,
                headers={"Content-Type": "application/json"} if body is not None else None)
            assert code == status, (code, raw)
            return json.loads(raw).get("data")
        try:
            start(); configure_model(port, f"http://127.0.0.1:{server.server_port}/v1")
            call("POST", "/projects", {"id": "context-qa", "name": "Context QA", "workspace_root": str(workspace)}, 201)
            sessions = [call("POST", "/sessions", {"project_id": "context-qa", "title": title, "agent_id": "mdo.default",
                "model_id": "queue-fixture", "protocol": "openai-chat-completions", "reasoning_effort": "none"}, 201)
                for title in ("Source conversation", "Context conversation")]
            for item in sessions:
                run = call("POST", f'/projects/context-qa/sessions/{item["id"]}/runs', {"prompt": item["title"] + " seed question"}, 202)
                deadline = time.monotonic() + 8
                while not call("GET", "/runs/" + run["id"])["terminal"]:
                    assert time.monotonic() < deadline; time.sleep(.05)
            history = "/projects/context-qa/input-history"
            assert call("GET", history)["items"] == []
            for index in range(33): call("POST", history, {"id": f"{index:032x}", "text": f"history-{index}"})
            saved = call("GET", history); assert len(saved["items"]) == 30 and saved["items"][0]["text"] == "history-3"
            assert call("POST", history, saved["items"][-1]) == saved
            call("POST", history, {**saved["items"][-1], "text": "conflicting"}, 409)
            call("POST", history, {"id": "invalid", "text": "bad"}, 422)
            call("POST", history, {"id": "f" * 32, "text": "x" * 65536}, 422)
            paths = call("GET", "/projects/context-qa/workspace/files?q=&include_directories=1")["items"]
            assert "src/" in paths and "src/readme.md" in paths, paths
            stop(); start(); assert call("GET", history) == saved
            shot = ROOT / f".build/context-ui/{'packed-' if packed else ''}{width}.png"; shot.parent.mkdir(parents=True, exist_ok=True)
            with tempfile.TemporaryDirectory(prefix="context-browser-") as profile:
                source, session = [item["id"] for item in sessions]
                url = f"http://127.0.0.1:{port}/?source={source}&session={session}#/projects/context-qa/sessions/{session}"
                result = subprocess.run(["node", str(ROOT / "tests/queue_browser_driver.mjs"), str(browser), profile, url,
                    str(width), str(shot), str(ROOT / "tests/fixtures/composer-context-runtime.js")],
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=52,
                    creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
                assert result.returncode == 0, result.stderr.decode(errors="replace")[-1500:]
                report = json.loads(result.stdout.decode("utf-8", errors="replace")); print(json.dumps(report, ensure_ascii=False))
                assert report["passed"], (report, result.stderr.decode(errors="replace")[-1800:])
            latest = Model.calls[-1]
            assert "Revised review" in json.dumps(latest) and "pasted evidence" in json.dumps(latest), latest
            preview = call("GET", "/projects/context-qa/purge-preview")
            assert "data/prompt-history/context-qa.json" in [item["path"] for item in preview["targets"]], preview
        except BaseException:
            log.flush(); print((base / "runtime.log").read_text(encoding="utf-8", errors="replace")[-6000:]); raise
        finally:
            stop(); server.shutdown(); server.server_close(); log.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, default=ROOT / ".build/host/xs.exe")
    parser.add_argument("--browser", type=Path, default=Path("C:/Program Files/Google/Chrome/Application/chrome.exe"))
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--packed", type=Path)
    args = parser.parse_args(); check(args.host.resolve(), args.browser.resolve(), args.width, args.packed.resolve() if args.packed else None)
