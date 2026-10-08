"""Full packed page, real WebSocket and tasks, with bounded read failures.

The failure seam only returns 503 from task-list reads after an armed stop.
The ordinary model and spawn tool create the task; nothing fabricates state.
Create the printed stop file to clean up, or restart file to reboot once.
"""
import argparse
from collections import Counter
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import threading
import time

from test_api_runtime import request, session_events
from test_packed_home_lease import site, start, stop, wait_bootstrap
from test_packed_queue_start_recovery import configure_model

ROOT = Path(__file__).resolve().parents[1]
SEAM = r'''
static xmutex* g_TaskReadProbeLock;
static unsigned g_TaskReadProbeArm, g_TaskReadProbeLeft, g_TaskReadProbeFailures, g_TaskReadProbeStops;
static bool TaskReadProbe(XS_HttpReq* Request)
{
    if (!Request || !Request->head) return false;
    xstrview Target = Request->head->Target;
    const char* Arm = "/api/v1/qa-task-arm";
    const char* Proof = "/api/v1/qa-task-proof";
    bool IsGet = Request->head->MethodCode == XHTTP_METHOD_GET;
    bool IsArm = IsGet && Target.Size == strlen(Arm) && memcmp(Target.Data, Arm, Target.Size) == 0;
    bool IsProof = IsGet && Target.Size == strlen(Proof) && memcmp(Target.Data, Proof, Target.Size) == 0;
    bool IsList = IsGet && Target.Size == 13u && memcmp(Target.Data, "/api/v1/tasks", 13u) == 0;
    bool IsStop = Request->head->MethodCode == XHTTP_METHOD_DELETE && Target.Size > 14u &&
        memcmp(Target.Data, "/api/v1/tasks/", 14u) == 0;
    bool Fail = false;
    xrtMutexLock(g_TaskReadProbeLock);
    if (IsArm) g_TaskReadProbeArm = 1u;
    if (IsStop) ++g_TaskReadProbeStops;
    if (IsStop && g_TaskReadProbeArm) {
        g_TaskReadProbeArm = 0u; g_TaskReadProbeLeft = QA_READ_FAILURES;
    }
    if (IsList && g_TaskReadProbeLeft) {
        --g_TaskReadProbeLeft; ++g_TaskReadProbeFailures; Fail = true;
    }
    unsigned Left = g_TaskReadProbeLeft, Failures = g_TaskReadProbeFailures, Stops = g_TaskReadProbeStops;
    xrtMutexUnlock(g_TaskReadProbeLock);
    if (!IsArm && !IsProof && !Fail) return false;
    MdoApiContext Context = {0}; Context.Request = Request;
    MdoApiRequestId(Context.RequestId);
    if (Fail) MdoApiReplyError(&Context, 503u, "tasks_unavailable",
        "Task list refresh temporarily unavailable", NULL);
    else {
        xvalue* Data = xrtValueObject();
        MdoApiValueSetUInt(Data, "remaining", Left);
        MdoApiValueSetUInt(Data, "failed_reads", Failures);
        MdoApiValueSetUInt(Data, "delete_requests", Stops);
        MdoApiReplySuccessTake(&Context, 200u, Data, NULL);
    }
    return true;
}
'''


class Model(BaseHTTPRequestHandler):
    calls = Counter()

    def log_message(self, *_):
        pass

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        prompt = next(m["content"] for m in reversed(body["messages"]) if m["role"] == "user")
        type(self).calls[prompt] += 1
        message = {"role": "assistant", "content": "The background task is ready."}
        reason = "stop"
        if prompt == "background" and self.calls[prompt] == 1:
            message["tool_calls"] = [{"id": "read-recovery-spawn", "type": "function", "function": {
                "name": "spawn", "arguments": json.dumps({"argv": [sys.executable, "-u", "-c",
                    "import sys; print('fixture ready', flush=True); sys.stdin.readline()"],
                    "notify": "task-read-qa"})}}]
            reason = "tool_calls"
        elif prompt != "background":
            message["content"] = "The conversation can continue."
        raw = json.dumps({"id": "read-recovery", "model": body["model"], "choices": [{
            "index": 0, "message": message, "finish_reason": reason}],
            "usage": {"prompt_tokens": 50, "completion_tokens": 20}}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)


def fixture(base: Path, host: Path, failures: int) -> Path:
    app = base / "app"
    shutil.copytree(ROOT / "app", app)
    service = app / "src/bootstrap/service.c"
    text = service.read_text(encoding="utf-8")
    declaration = "void ServiceInit(XS_HostInfo* pHost)\n"
    init = "    bool ready = MdoBootstrapInit(pHost);\n"
    unit = "    MdoRemoteUnit();\n"
    route = "    return MdoApiRequest(pRequest);\n"
    assert all(text.count(marker) == 1 for marker in (declaration, init, unit, route))
    text = text.replace(declaration, SEAM.replace("QA_READ_FAILURES", str(failures) + "u") + declaration)
    text = text.replace(init, init + "    g_TaskReadProbeLock = xrtMutexCreate();\n")
    text = text.replace(unit, unit + "    xrtMutexDestroy(g_TaskReadProbeLock);\n")
    text = text.replace(route, "    if (TaskReadProbe(pRequest)) return XS_OK;\n" + route)
    service.write_text(text, encoding="utf-8", newline="\n")
    packed = base / "task-read.exe"
    subprocess.run([str(host), "pack", str(app), "-o", str(packed)], check=True,
        creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
    return packed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, required=True)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--failures", type=int, choices=(2, 6), default=2)
    args = parser.parse_args()
    base = args.directory.resolve()
    assert not base.exists(), "Choose a fresh isolated fixture directory"
    base.mkdir(parents=True)
    packed = fixture(base, args.host.resolve(), args.failures)
    native, port = site(base, "native", packed)
    model = ThreadingHTTPServer(("127.0.0.1", 0), Model)
    worker = threading.Thread(target=model.serve_forever, daemon=True)
    worker.start()
    env = dict(os.environ, USE_WEBVIEW="0", MDO_QUEUE_FIXTURE_KEY="fixture-only")
    process = start(native, packed, base / "home", env)
    def call(method, path, body=None, expected=200):
        status, _, raw = request(port, method, path, body=None if body is None else json.dumps(body).encode(),
            headers={"Content-Type": "application/json"})
        assert status == expected, raw
        return json.loads(raw)["data"]
    sessions = []
    try:
        status, bootstrap = wait_bootstrap(process, port, native / "packed.log")
        assert status == 200 and bootstrap["data"]["ready"], bootstrap
        configure_model(port, f"http://127.0.0.1:{model.server_port}/v1")
        for title in ("Background read recovery", "Focus check"):
            session = call("POST", "/api/v1/sessions", {"project_id": "default", "title": title,
                "model_id": "queue-fixture", "reasoning_effort": "none", "permission_profile": "full-access"}, 201)
            sessions.append("/api/v1/projects/default/sessions/" + session["id"])
        run = call("POST", sessions[0] + "/runs", {"prompt": "background"}, 202)
        end = time.monotonic() + 10
        while not call("GET", "/api/v1/runs/" + run["id"])["terminal"]:
            assert time.monotonic() < end
            time.sleep(.03)
        task, = call("GET", "/api/v1/tasks")["items"]
        assert not task["terminal"] and task["notify"] == "task-read-qa", task
        print(json.dumps({"url": f"http://127.0.0.1:{port}/#/projects/default/sessions/" + sessions[0].split("/")[-1],
            "native_port": port, "task_id": task["id"], "stop_file": str(base / "stop"),
            "restart_file": str(base / "restart"), "sessions": sessions}), flush=True)
        end = time.monotonic() + 360
        restarted = False
        while not (base / "stop").exists() and time.monotonic() < end:
            assert process.poll() is None, "Native fixture exited"
            if (base / "restart").exists() and not restarted:
                stop(process)
                process = start(native, packed, base / "home", env)
                status, boot = wait_bootstrap(process, port, native / "packed.log")
                assert status == 200 and boot["data"]["ready"], boot
                restarted = True
                print("restarted=1", flush=True)
            time.sleep(.1)
        proof = {"calls": dict(Model.calls), "restarted": restarted,
            "final_errors": sum(e["kind"] == "error" for path in sessions for e in session_events(port, path)),
            "tasks": call("GET", "/api/v1/tasks")["items"]}
        (base / "proof.json").write_text(json.dumps(proof, indent=2), encoding="utf-8")
    finally:
        try:
            if process.poll() is None:
                for task in call("GET", "/api/v1/tasks")["items"]:
                    call("DELETE", "/api/v1/tasks/" + str(task["id"]))
        finally:
            stop(process)
            model.shutdown()
            model.server_close()
            worker.join(timeout=3)


if __name__ == "__main__":
    main()
