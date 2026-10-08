"""Real packed chat/ask/approval with bounded GET failures after accepted PUTs.

Uses an ordinary editable loopback model. The tools ask a question and run one
Python print command through normal approval. Create the printed stop file to
clean up; no fault endpoint is included in the product candidate.
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
static xmutex* g_DecisionReadProbeLock;
static unsigned g_DecisionReadLeft[3], g_DecisionReadFailed[3], g_DecisionReadWrites[2];
static bool DecisionReadProbe(XS_HttpReq* Request)
{
    if (!Request || !Request->head) return false;
    xstrview Target = Request->head->Target;
    const char* Proof = "/api/v1/qa-decision-proof";
    bool IsGet = Request->head->MethodCode == XHTTP_METHOD_GET;
    bool IsPut = Request->head->MethodCode == XHTTP_METHOD_PUT;
    bool IsProof = IsGet && Target.Size == strlen(Proof) && memcmp(Target.Data, Proof, Target.Size) == 0;
    bool IsSession = Target.Size > 17u && memcmp(Target.Data, "/api/v1/projects/", 17u) == 0;
    bool IsApprovals = Target.Size >= 17u && memcmp(Target.Data, "/api/v1/approvals", 17u) == 0;
    bool IsRuns = Target.Size == 12u && memcmp(Target.Data, "/api/v1/runs", 12u) == 0;
    bool IsAskRead = IsSession && Target.Size >= 5u && memcmp(Target.Data + Target.Size - 5u, "/asks", 5u) == 0;
    bool IsAskPut = false;
    if (IsPut && IsSession) {
        for (size_t I = 17u; I + 6u <= Target.Size; ++I)
            if (memcmp(Target.Data + I, "/asks/", 6u) == 0) { IsAskPut = true; break; }
    }
    int Kind = IsAskRead ? 0 : IsApprovals ? 1 : IsRuns ? 2 : -1;
    bool Fail = false;
    xrtMutexLock(g_DecisionReadProbeLock);
    if (IsAskPut) {
        ++g_DecisionReadWrites[0];
        g_DecisionReadLeft[0] = g_DecisionReadLeft[1] = g_DecisionReadLeft[2] = 2u;
    }
    if (IsPut && IsApprovals) { ++g_DecisionReadWrites[1]; g_DecisionReadLeft[1] = 6u; }
    if (IsGet && Kind >= 0 && g_DecisionReadLeft[Kind]) {
        --g_DecisionReadLeft[Kind]; ++g_DecisionReadFailed[Kind]; Fail = true;
    }
    unsigned Failed[3], Writes[2];
    memcpy(Failed, g_DecisionReadFailed, sizeof(Failed)); memcpy(Writes, g_DecisionReadWrites, sizeof(Writes));
    xrtMutexUnlock(g_DecisionReadProbeLock);
    if (!IsProof && !Fail) return false;
    MdoApiContext Context = {0}; Context.Request = Request; MdoApiRequestId(Context.RequestId);
    if (Fail) {
        const char* Codes[3] = { "asks_unavailable", "approvals_unavailable", "runs_unavailable" };
        MdoApiReplyError(&Context, 503u, Codes[Kind], "Fixture read unavailable after acknowledgement", NULL);
    } else {
        xvalue* Data = xrtValueObject();
        MdoApiValueSetUInt(Data, "asks_failed_reads", Failed[0]);
        MdoApiValueSetUInt(Data, "approvals_failed_reads", Failed[1]);
        MdoApiValueSetUInt(Data, "runs_failed_reads", Failed[2]);
        MdoApiValueSetUInt(Data, "answer_puts", Writes[0]);
        MdoApiValueSetUInt(Data, "approval_puts", Writes[1]);
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
        message = {"role": "assistant", "content": "The conversation can continue."}
        finish = "stop"
        if prompt == "decisions" and self.calls[prompt] <= 2:
            name, arguments = (
                ("ask_user", {"question": "Which fixture route should be checked?", "options": ["Continue carefully", "Inspect locally"]}),
                ("exec", {"argv": [sys.executable, "-c", "print('DECISION_VERIFIED')"], "timeout_ms": 5000}),
            )[self.calls[prompt] - 1]
            message["tool_calls"] = [{"id": f"decision-read-{self.calls[prompt]}", "type": "function",
                "function": {"name": name, "arguments": json.dumps(arguments)}}]
            finish = "tool_calls"
        raw = json.dumps({"id": "decision-read", "model": body["model"], "choices": [{
            "index": 0, "message": message, "finish_reason": finish}],
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
    text = text.replace(init, init + "    g_DecisionReadProbeLock = xrtMutexCreate();\n")
    text = text.replace(unit, unit + "    xrtMutexDestroy(g_DecisionReadProbeLock);\n")
    text = text.replace(route, "    if (DecisionReadProbe(pRequest)) return XS_OK;\n" + route)
    service.write_text(text, encoding="utf-8", newline="\n")
    packed = base / "decision-read.exe"
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
        session = call("POST", "/api/v1/sessions", {"project_id": "default", "title": "Decision read recovery",
            "model_id": "queue-fixture", "reasoning_effort": "none", "permission_profile": "balanced"}, 201)
        path = "/api/v1/projects/default/sessions/" + session["id"]
        print(json.dumps({"url": f"http://127.0.0.1:{port}/#/projects/default/sessions/" + session["id"],
            "native_port": port, "stop_file": str(base / "stop")}), flush=True)
        end = time.monotonic() + 360
        while not (base / "stop").exists() and time.monotonic() < end:
            assert process.poll() is None, "Native fixture exited"
            time.sleep(.1)
        events = session_events(port, path)
        proof = {"model_calls": dict(Model.calls), "faults": call("GET", "/api/v1/qa-decision-proof"),
            "final_errors": sum(event["kind"] == "error" for event in events),
            "event_kinds": dict(Counter(event["kind"] for event in events)),
            "tool_results": [event for event in events if event["kind"] == "tool_done"]}
        assert len(proof["tool_results"]) == 2, proof
        assert all(event["success"] for event in proof["tool_results"]), proof
        assert "DECISION_VERIFIED" in proof["tool_results"][1]["text"], proof
        (base / "proof.json").write_text(json.dumps(proof, indent=2), encoding="utf-8")
    finally:
        stop(process)
        model.shutdown(); model.server_close(); worker.join(timeout=3)


if __name__ == "__main__":
    main()
