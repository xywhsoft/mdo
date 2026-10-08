"""Production task panel against real packed tasks and bounded HTTP faults.

Three idle child processes are created by the normal Agent spawn tool. Faults
affect only task cancellation acknowledgements/reads, never model execution.
Use the printed local URL, then create the printed stop file for cleanup.
"""
import argparse
from collections import Counter
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import http.client
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import threading
import time

from test_api_runtime import request, session_events
from test_packed_home_lease import site, start, stop, wait_bootstrap
from test_packed_queue_start_recovery import configure_model


class Model(BaseHTTPRequestHandler):
    calls = Counter()

    def log_message(self, *_):
        pass

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        prompt = next(m["content"] for m in reversed(body["messages"]) if m["role"] == "user")
        type(self).calls[prompt] += 1
        message = {"role": "assistant", "content": "Background task ready."}
        reason = "stop"
        if self.calls[prompt] == 1:
            message["tool_calls"] = [{"id": "spawn-" + prompt, "type": "function", "function": {
                "name": "spawn", "arguments": json.dumps({"argv": [sys.executable, "-u", "-c",
                    "import sys; print('fixture ready', flush=True); sys.stdin.readline()"], "notify": prompt})}}]
            reason = "tool_calls"
        raw = json.dumps({"id": "task-stop-fixture", "model": body["model"],
            "choices": [{"index": 0, "message": message, "finish_reason": reason}],
            "usage": {"prompt_tokens": 50, "completion_tokens": 20}}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)


PAGE = '''<!doctype html><html lang="zh-CN"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>任务停止恢复验收</title>
<link rel="stylesheet" href="/css/app.css">
<style>body{height:auto;overflow:auto;padding:16px}main{max-width:1000px;margin:auto}
pre{white-space:pre-wrap;overflow-wrap:anywhere}.tasks-dialog-body{height:auto;overflow:auto}
</style><main><h1>任务停止恢复验收</h1><p>真实打包程序与生产任务面板，故障只注入回环测试请求。</p>
<button id="proof">查看验证记录</button><pre id="result">尚未核对</pre>
<button id="duplicate">停止确认耗尽（重复点击）</button><pre id="notices">[]</pre>
<button id="refresh">刷新后台任务快照</button><button id="release">释放附加读取</button>
<section class="tasks-dialog-body"><div id="tasks"></div><div id="detail"></div></section></main>
<div id="toast-region" class="toast-region" role="status" aria-live="polite"></div>
<script type="module">
import {api} from '/js/api/client.js';
import {loadLocale} from '/js/i18n.js';
import {createTaskPanel} from '/js/features/tasks/task-panel.js';
import {tasksStore,taskDetailStore,artifactPreviewStore,loadTasks} from '/js/state/tasks.js';
await loadLocale('zh-CN'); await api.get('/project-purge-intent');
const notices=[];
new MutationObserver(records=>{for(const record of records)for(const node of record.addedNodes)
if(node.nodeType===1&&node.classList.contains('toast'))notices.push({tone:node.dataset.tone,text:node.textContent});
document.querySelector('#notices').textContent=JSON.stringify(notices,null,2);
}).observe(document.querySelector('#toast-region'),{childList:true});
createTaskPanel({container:document.querySelector('#tasks'),detailContainer:document.querySelector('#detail'),
store:tasksStore,detailStore:taskDetailStore,previewStore:artifactPreviewStore});
await loadTasks();
document.querySelector('#duplicate').addEventListener('click',()=>{
const button=document.querySelector('[data-task-cancel="3"]');button?.focus();button?.click();button?.click();});
document.querySelector('#proof').addEventListener('click',async()=>{
document.querySelector('#result').textContent=JSON.stringify((await api.get('/qa-stop-proof')).data,null,2);});
document.querySelector('#refresh').addEventListener('click',()=>void loadTasks());
document.querySelector('#release').addEventListener('click',()=>void api.post('/qa-stop-release',{}));
</script></html>'''


class Proxy(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *_):
        pass

    def reply(self, status, raw, content_type="application/json"):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def handle(self):
        try:
            super().handle()
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass

    def proxy(self):
        if self.path == "/":
            self.reply(200, PAGE.encode(), "text/html; charset=utf-8")
            return
        if self.path == "/api/v1/qa-stop-proof":
            tasks = json.loads(request(self.server.native, "GET", "/api/v1/tasks")[2])["data"]["items"]
            errors = sum(e["kind"] == "error" for path in self.server.sessions
                for e in session_events(self.server.native, path))
            proof = {"model_calls": dict(Model.calls), "faults": self.server.counts,
                "native_tasks": [{"id": t["id"], "state": t["state"], "terminal": t["terminal"]} for t in tasks],
                "final_model_errors": errors, "secondary_read_pending": self.server.secondary_pending}
            self.reply(200, json.dumps({"ok": True, "data": proof}).encode())
            return
        if self.path == "/api/v1/qa-stop-release" and self.command == "POST":
            self.rfile.read(int(self.headers.get("Content-Length", "0")))
            self.server.secondary_release.set()
            self.reply(200, b'{"ok":true,"data":{}}')
            return
        if (self.path == "/api/v1/tasks" and "secondary" in self.server.counts and
            self.server.counts["secondary"]["http_deletes"] and not self.server.secondary_release.is_set()):
            self.server.counts["secondary"]["held_list_reads"] += 1
            self.server.secondary_pending = True
            try:
                self.server.secondary_release.wait(timeout=45)
            finally:
                self.server.secondary_pending = False
        mode = self.server.modes.get(self.path)
        counts = self.server.counts.get(mode)
        if mode and self.command == "DELETE":
            counts["http_deletes"] += 1
            if mode == "before" and counts["http_deletes"] == 1:
                self.reply(503, b'{"ok":false,"error":{"code":"runtime_unavailable","message":"fixture before dispatch"}}')
                return
        if mode and self.command == "GET" and counts["http_deletes"]:
            counts["reads_after_delete"] += 1
            if mode in ("unconfirmed", "observed"):
                self.reply(503, b'{"ok":false,"error":{"code":"task_unavailable","message":"fixture read unavailable"}}')
                return
        body = self.rfile.read(int(self.headers.get("Content-Length", "0")))
        headers = {k: v for k, v in self.headers.items() if k.lower() not in ("host", "connection")}
        if headers.get("Origin") == f"http://127.0.0.1:{self.server.server_port}":
            headers["Origin"] = f"http://127.0.0.1:{self.server.native}"
        upstream = http.client.HTTPConnection("127.0.0.1", self.server.native, timeout=5)
        try:
            upstream.request(self.command, self.path, body=body, headers=headers)
            response = upstream.getresponse()
            raw = response.read()
            if mode and self.command == "DELETE":
                counts["native_deletes"] += 1
                assert response.status == 200, raw
                if mode in ("lost", "unconfirmed", "observed"):
                    # Chromium may transparently replay an idempotent DELETE
                    # if no response headers arrive. Lose the body after the
                    # headers to isolate the application's confirmation path.
                    self.send_response(200)
                    self.send_header("Content-Type", "application/json")
                    self.send_header("Content-Length", str(len(raw)))
                    self.end_headers()
                    self.wfile.write(raw[:4])
                    self.wfile.flush()
                    self.close_connection = True
                    self.connection.shutdown(socket.SHUT_RDWR)
                    return
            self.send_response(response.status)
            for key, value in response.getheaders():
                if key.lower() not in ("content-length", "connection", "transfer-encoding"):
                    self.send_header(key, value)
            self.send_header("Content-Length", str(len(raw)))
            self.end_headers()
            self.wfile.write(raw)
        finally:
            upstream.close()

    do_GET = do_DELETE = do_POST = do_PUT = proxy


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--packed", type=Path, required=True)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--observations-only", action="store_true",
        help="Prepare two tasks for background confirmation and blocked secondary reads")
    args = parser.parse_args()
    base = args.directory.resolve()
    assert not base.exists(), "Choose a fresh fixture directory"
    base.mkdir(parents=True)
    native_site, port = site(base, "native", args.packed.resolve())
    model = ThreadingHTTPServer(("127.0.0.1", 0), Model)
    proxy = ThreadingHTTPServer(("127.0.0.1", 0), Proxy)
    proxy.native, proxy.modes, proxy.counts, proxy.sessions = port, {}, {}, []
    proxy.secondary_release = threading.Event()
    proxy.secondary_pending = False
    model_thread = threading.Thread(target=model.serve_forever, daemon=True)
    proxy_thread = threading.Thread(target=proxy.serve_forever, daemon=True)
    model_thread.start()
    process = start(native_site, args.packed.resolve(), base / "home",
        dict(os.environ, USE_WEBVIEW="0", MDO_QUEUE_FIXTURE_KEY="fixture-only"))
    try:
        status, document = wait_bootstrap(process, port, native_site / "packed.log")
        assert status == 200 and document["data"]["ready"], document
        configure_model(port, f"http://127.0.0.1:{model.server_port}/v1")
        modes = ("observed", "secondary") if args.observations_only else ("lost", "before", "unconfirmed")
        for mode in modes:
            def call(method, path, body=None, expected=200):
                status, _, raw = request(port, method, path, body=None if body is None else json.dumps(body).encode(),
                    headers={"Content-Type": "application/json"})
                assert status == expected, raw
                return json.loads(raw)["data"]
            session = call("POST", "/api/v1/sessions", {"project_id": "default", "title": "Stop " + mode,
                "model_id": "queue-fixture", "reasoning_effort": "none", "permission_profile": "full-access"}, 201)
            path = "/api/v1/projects/default/sessions/" + session["id"]
            proxy.sessions.append(path)
            run = call("POST", path + "/runs", {"prompt": mode}, 202)
            deadline = time.monotonic() + 10
            while not call("GET", "/api/v1/runs/" + run["id"])["terminal"]:
                assert time.monotonic() < deadline, "Task preparation timed out"
                time.sleep(.05)
            assert call("GET", "/api/v1/runs/" + run["id"])["state"] == "succeeded"
            tasks = call("GET", "/api/v1/tasks")["items"]
            task = next(t for t in tasks if t["notify"] == mode)
            assert not task["terminal"], task
            proxy.modes["/api/v1/tasks/" + str(task["id"])] = mode
            proxy.counts[mode] = {"http_deletes": 0, "native_deletes": 0, "reads_after_delete": 0}
            if mode == "secondary": proxy.counts[mode]["held_list_reads"] = 0
        proxy_thread.start()
        print(json.dumps({"url": f"http://127.0.0.1:{proxy.server_port}/", "stop_file": str(base / "stop")}), flush=True)
        end = time.monotonic() + 1200
        while not (base / "stop").exists() and time.monotonic() < end:
            assert process.poll() is None, "Task fixture exited"
            time.sleep(.2)
    finally:
        proxy.secondary_release.set()
        for path in proxy.modes:
            if process.poll() is None:
                request(port, "DELETE", path)
        stop(process)
        if proxy_thread.is_alive():
            proxy.shutdown()
        proxy.server_close()
        model.shutdown()
        model.server_close()
        model_thread.join(timeout=3)
        proxy_thread.join(timeout=3) if proxy_thread.is_alive() else None
        (base / "calls.json").write_text(json.dumps(Model.calls, indent=2), encoding="utf-8")
        (base / "faults.json").write_text(json.dumps(proxy.counts, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
