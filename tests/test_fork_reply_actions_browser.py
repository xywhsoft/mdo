"""Real packed fork + cold reply buttons, with an isolated loopback model."""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import subprocess
import tempfile
import threading

from test_interrupt_runtime import until
from test_api_runtime import request
from test_packed_home_lease import ROOT, site, start, stop, wait_bootstrap, release_packed_copies
from test_packed_queue_start_recovery import configure_model
from test_session_purge_browser import Proxy as PackedProxy
from test_sidebar_focus_browser import ResultParser


class Model(BaseHTTPRequestHandler):
    calls = 0

    def log_message(self, *_):
        pass

    def do_POST(self):
        json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        Model.calls += 1
        n = Model.calls
        delta = {"role": "assistant", "content": f"Reply {n}: bounded action fixture."}
        if n < 3:
            delta["tool_calls"] = [{"id": f"read-{n}", "type": "function",
                "function": {"name": "read", "arguments": json.dumps({"path": "probe.txt"})}}]
        response = {"id": f"reply-{n}", "model": "queue-fixture", "choices": [
            {"index": 0, "delta": delta, "finish_reason": "tool_calls" if n < 3 else "stop"}],
            "usage": {"prompt_tokens": 7, "completion_tokens": 3, "total_tokens": 10}}
        body = ("data: " + json.dumps(response) + "\n\ndata: [DONE]\n\n").encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


class Proxy(PackedProxy):
    def proxy(self):
        if self.path.startswith("/__qa/browser"):
            raw = (ROOT / "tests/fixtures/fork-reply-actions-browser.html").read_bytes()
            return self.reply(200, raw, "text/html; charset=utf-8")
        return super().proxy()

    do_GET = do_POST = do_PUT = do_PATCH = do_DELETE = do_HEAD = proxy


def run(packed, browser):
    with tempfile.TemporaryDirectory(prefix="fork-reply-browser-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        native, port = site(base, "native", packed)
        home = base / "home"
        (base / "probe.txt").write_text("Bounded read fixture", encoding="utf-8")
        model = ThreadingHTTPServer(("127.0.0.1", 0), Model)
        Model.calls = 0
        threading.Thread(target=model.serve_forever, daemon=True).start()
        process = None
        proxy = None
        try:
            process = start(native, packed, home, dict(os.environ, USERPROFILE=str(base),
                MDO_QUEUE_FIXTURE_KEY="bounded-model-fixture", USE_WEBVIEW="0"))
            wait_bootstrap(process, port, native / "packed.log")
            configure_model(port, f"http://127.0.0.1:{model.server_port}/v1")

            def api(method, path, body=None, expected=200):
                status, _, response = request(port, method, "/api/v1" + path,
                    body=json.dumps(body).encode() if body is not None else None,
                    headers={"Content-Type": "application/json"})
                doc = json.loads(response)
                assert status == expected, (status, doc)
                return doc["data"]

            source = api("POST", "/sessions", {"project_id": "default", "title": "Fork reply source",
                "model_id": "queue-fixture", "permission_profile": "full-access",
                "reasoning_effort": "none", "workspace_root": str(base)}, 201)
            run_info = api("POST", f'/projects/default/sessions/{source["id"]}/runs',
                {"prompt": "Fork reply action input"}, 202)
            terminal = until(lambda: (state if (state := api("GET", "/runs/" + run_info["id"]))["terminal"] else None), 15)
            assert terminal["state"] == "succeeded" and Model.calls == 3, terminal
            journal = home / "sessions/default" / source["id"] / "ui-events.jsonl"
            original = journal.read_bytes()
            proxy = ThreadingHTTPServer(("127.0.0.1", 0), Proxy)
            proxy.native = port
            proxy.session_path = "/api/v1/projects/default/sessions/" + source["id"]
            proxy.purges = proxy.trashes = 0
            proxy.lock = threading.Lock()
            proxy.websockets = []
            proxy.tunnels = {}
            proxy.stopped = threading.Event()
            threading.Thread(target=proxy.serve_forever, daemon=True).start()
            command = [str(browser), "--headless=new", "--disable-gpu", "--no-first-run",
                "--enable-logging=stderr",
                "--run-all-compositor-stages-before-draw", "--disable-frame-rate-limit",
                "--disable-extensions", "--disable-background-networking",
                "--user-data-dir=" + str(base / "browser"), "--dump-dom", "--virtual-time-budget=16000",
                f"http://127.0.0.1:{proxy.server_port}/__qa/browser?session=" + source["id"]]
            output = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=40,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
            parser = ResultParser()
            parser.feed(output.stdout.decode("utf-8", errors="replace"))
            if not parser.result.startswith("{"):
                evidence = ROOT / ".build/fork-reply-actions"
                evidence.mkdir(exist_ok=True)
                (evidence / "browser-failure.html").write_bytes(output.stdout)
                (evidence / "browser-failure-stderr.txt").write_bytes(output.stderr)
                raise AssertionError(("browser did not finish", parser.result, output.returncode))
            proof = json.loads(parser.result)
            assert proof["passed"], proof
            assert journal.read_bytes() == original, "fork/display changed the source journal"
            assert Model.calls == 3, "button checks unexpectedly executed another model call"
            proof.update(source_journal_unchanged=True, external_model_traffic=False)
            return proof
        finally:
            if proxy:
                proxy.stopped.set()
                proxy.shutdown()
                proxy.server_close()
            stop(process)
            model.shutdown()
            model.server_close()
            release_packed_copies(native / packed.name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--packed", type=Path, required=True)
    parser.add_argument("--browser", type=Path, default=Path("C:/Program Files/Google/Chrome/Application/chrome.exe"))
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    proof = run(args.packed.resolve(), args.browser.resolve())
    if args.report:
        args.report.write_text(json.dumps(proof, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(proof))


if __name__ == "__main__":
    main()
