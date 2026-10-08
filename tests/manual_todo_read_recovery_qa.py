"""Bounded packed TODO read faults, using real plan tools and ordinary chat.

Open the printed URL. Send ``baseline``, arm two or six GET failures at the
printed fixture endpoint, then send ``update`` and continue normally. Create
the printed stop file to finish. No fault endpoint is shipped in the candidate.
"""
import argparse
from collections import Counter
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import shutil
import subprocess
import threading
import time

from test_api_runtime import request, session_events
from test_packed_home_lease import site, start, stop, wait_bootstrap
from test_packed_queue_start_recovery import configure_model

ROOT = Path(__file__).resolve().parents[1]
SEAM = r'''
static xmutex* g_TodoReadProbeLock;
static unsigned g_TodoReadProbeLeft, g_TodoReadProbeFailed;
static bool TodoReadProbe(XS_HttpReq* Request)
{
    if (!Request || !Request->head || Request->head->MethodCode != XHTTP_METHOD_GET) return false;
    xstrview Target = Request->head->Target;
    const char* Arm = "/api/v1/qa-todo-arm", *Proof = "/api/v1/qa-todo-proof";
    bool IsArm = Target.Size == strlen(Arm) + 2u && memcmp(Target.Data, Arm, strlen(Arm)) == 0 &&
        Target.Data[strlen(Arm)] == '/' && (Target.Data[Target.Size - 1u] == '2' || Target.Data[Target.Size - 1u] == '6');
    bool IsProof = Target.Size == strlen(Proof) && memcmp(Target.Data, Proof, Target.Size) == 0;
    bool IsTodo = Target.Size > 17u && memcmp(Target.Data, "/api/v1/projects/", 17u) == 0 &&
        memcmp(Target.Data + Target.Size - 5u, "/todo", 5u) == 0;
    bool Fail = false;
    xrtMutexLock(g_TodoReadProbeLock);
    if (IsArm) { g_TodoReadProbeLeft = (unsigned)(Target.Data[Target.Size - 1u] - '0'); g_TodoReadProbeFailed = 0u; }
    if (IsTodo && g_TodoReadProbeLeft) { --g_TodoReadProbeLeft; ++g_TodoReadProbeFailed; Fail = true; }
    unsigned Left = g_TodoReadProbeLeft, Failed = g_TodoReadProbeFailed;
    xrtMutexUnlock(g_TodoReadProbeLock);
    if (!IsArm && !IsProof && !Fail) return false;
    MdoApiContext Context = {0}; Context.Request = Request; MdoApiRequestId(Context.RequestId);
    if (Fail) MdoApiReplyError(&Context, 503u, "fixture_todo_read", "Fixture todo read unavailable", NULL);
    else {
        xvalue* Data = xrtValueObject();
        MdoApiValueSetUInt(Data, "remaining", Left); MdoApiValueSetUInt(Data, "failed_reads", Failed);
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
        self.calls[prompt] += 1
        message, finish = {"role": "assistant", "content": "The plan can continue."}, "stop"
        if prompt in ("baseline", "update", "update-final") and self.calls[prompt] == 1:
            arguments = {"items": [{"text": "Inspect pixel tools", "done": prompt != "baseline"},
                {"text": "Verify the result", "done": prompt == "update-final"}]}
            message["tool_calls"] = [{"id": f"todo-{prompt}", "type": "function",
                "function": {"name": "mdo.todo", "arguments": json.dumps(arguments)}}]
            finish = "tool_calls"
        raw = json.dumps({"id": "todo-read", "model": body["model"], "choices": [{"index": 0,
            "message": message, "finish_reason": finish}],
            "usage": {"prompt_tokens": 50, "completion_tokens": 20}}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)


def fixture(base, host):
    app = base / "app"
    shutil.copytree(ROOT / "app", app)
    service = app / "src/bootstrap/service.c"
    text = service.read_text(encoding="utf-8")
    declaration, init, unit, route = ("void ServiceInit(XS_HostInfo* pHost)\n",
        "    bool ready = MdoBootstrapInit(pHost);\n", "    MdoRemoteUnit();\n", "    return MdoApiRequest(pRequest);\n")
    assert all(text.count(marker) == 1 for marker in (declaration, init, unit, route))
    text = text.replace(declaration, SEAM + declaration)
    text = text.replace(init, init + "    g_TodoReadProbeLock = xrtMutexCreate();\n")
    text = text.replace(unit, unit + "    xrtMutexDestroy(g_TodoReadProbeLock);\n")
    text = text.replace(route, "    if (TodoReadProbe(pRequest)) return XS_OK;\n" + route)
    service.write_text(text, encoding="utf-8", newline="\n")
    packed = base / "todo-read.exe"
    subprocess.run([str(host), "pack", str(app), "-o", str(packed)], check=True,
        creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
    return packed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, required=True)
    parser.add_argument("--directory", type=Path, required=True)
    args = parser.parse_args()
    base = args.directory.resolve()
    assert not base.exists(), "Choose a fresh isolated directory"
    base.mkdir(parents=True)
    packed = fixture(base, args.host.resolve())
    native, port = site(base, "native", packed)
    model = ThreadingHTTPServer(("127.0.0.1", 0), Model)
    worker = threading.Thread(target=model.serve_forever, daemon=True)
    worker.start()
    process = start(native, packed, base / "home", dict(os.environ, USE_WEBVIEW="0", MDO_QUEUE_FIXTURE_KEY="fixture-only"))
    def call(method, path, body=None, expected=200):
        status, _, raw = request(port, method, path, body=None if body is None else json.dumps(body).encode(),
            headers={"Content-Type": "application/json"})
        assert status == expected, raw
        return json.loads(raw)["data"]
    try:
        status, bootstrap = wait_bootstrap(process, port, native / "packed.log")
        assert status == 200 and bootstrap["data"]["ready"], bootstrap
        configure_model(port, f"http://127.0.0.1:{model.server_port}/v1")
        session = call("POST", "/api/v1/sessions", {"project_id": "default", "title": "Todo read recovery",
            "model_id": "queue-fixture", "reasoning_effort": "none", "permission_profile": "balanced"}, 201)
        path = "/api/v1/projects/default/sessions/" + session["id"]
        print(json.dumps({"url": f"http://127.0.0.1:{port}/#/projects/default/sessions/" + session["id"],
            "native_port": port, "arm_path": "/api/v1/qa-todo-arm/{2|6}", "stop_file": str(base / "stop")}), flush=True)
        end = time.monotonic() + 360
        while not (base / "stop").exists() and time.monotonic() < end:
            assert process.poll() is None, "Native fixture exited"
            time.sleep(.1)
        events = session_events(port, path)
        proof = {"model_calls": dict(Model.calls), "faults": call("GET", "/api/v1/qa-todo-proof"),
            "final_errors": sum(event["kind"] == "error" for event in events),
            "event_kinds": dict(Counter(event["kind"] for event in events)),
            "tool_results": [event for event in events if event["kind"] == "tool_done"],
            "final_plan": call("GET", path + "/todo")}
        assert proof["tool_results"] and all(event["success"] for event in proof["tool_results"]), proof
        (base / "proof.json").write_text(json.dumps(proof, indent=2), encoding="utf-8")
    finally:
        stop(process)
        model.shutdown(); model.server_close(); worker.join(timeout=3)


if __name__ == "__main__":
    main()
