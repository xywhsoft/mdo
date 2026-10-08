"""Bounded session list/detail faults in an isolated real packed page.

Open the printed URL. The QA-only GET arm endpoint accepts list or detail,
with two transient faults or six exhaustion faults. Profile writes stay real.
Create the printed stop file when done. Nothing is added to the release app.
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
INPUT_TRACE = r'''
const prompt = document.querySelector("#prompt");
const value = Object.getOwnPropertyDescriptor(HTMLTextAreaElement.prototype, "value");
Object.defineProperty(prompt, "value", { configurable: true,
    get() { return value.get.call(this); },
    set(text) {
        console.info("QA_INPUT", JSON.stringify({ type: "restore", text, hash: location.hash,
            stack: new Error().stack.split("\n").slice(1, 5) }));
        value.set.call(this, text);
    }
});
for (const type of ["beforeinput", "input", "compositionstart", "compositionend", "blur"])
    prompt.addEventListener(type, event => console.info("QA_INPUT", JSON.stringify({ type,
        inputType: event.inputType, composing: event.isComposing, data: event.data,
        text: prompt.value, hash: location.hash })), true);
'''
SEAM = r'''
static xmutex* g_SessionReadProbeLock;
static unsigned g_ListLeft, g_ListFailed, g_DetailLeft, g_DetailFailed, g_ProfileWrites;
static bool SessionProbeTarget(xstrview Target, const char* Text)
{ return Target.Size == strlen(Text) && memcmp(Target.Data, Text, Target.Size) == 0; }
static bool SessionReadProbe(XS_HttpReq* Request)
{
    if (!Request || !Request->head) return false;
    xstrview Target = Request->head->Target;
    bool Get = Request->head->MethodCode == XHTTP_METHOD_GET;
    bool List = Get && SessionProbeTarget(Target, "/api/v1/sessions");
    const char* Prefix = "/api/v1/projects/default/sessions/";
    size_t PrefixSize = strlen(Prefix);
    bool InSession = Target.Size >= PrefixSize && memcmp(Target.Data, Prefix, PrefixSize) == 0;
    bool Detail = Get && InSession && Target.Size == PrefixSize + 32u;
    bool Profile = Request->head->MethodCode == XHTTP_METHOD_PUT && InSession &&
        Target.Size == PrefixSize + 40u && memcmp(Target.Data + Target.Size - 8u, "/profile", 8u) == 0;
    bool ArmList2 = Get && SessionProbeTarget(Target, "/api/v1/qa-session-arm/list/2");
    bool ArmList6 = Get && SessionProbeTarget(Target, "/api/v1/qa-session-arm/list/6");
    bool ArmDetail2 = Get && SessionProbeTarget(Target, "/api/v1/qa-session-arm/detail/2");
    bool ArmDetail6 = Get && SessionProbeTarget(Target, "/api/v1/qa-session-arm/detail/6");
    bool Arm = ArmList2 || ArmList6 || ArmDetail2 || ArmDetail6;
    bool Proof = Get && SessionProbeTarget(Target, "/api/v1/qa-session-proof");
    bool Fail = false;
    xrtMutexLock(g_SessionReadProbeLock);
    if (ArmList2 || ArmList6) { g_ListLeft = ArmList2 ? 2u : 6u; g_ListFailed = 0u; }
    if (ArmDetail2 || ArmDetail6) { g_DetailLeft = ArmDetail2 ? 2u : 6u; g_DetailFailed = 0u; }
    if (List && g_ListLeft) { --g_ListLeft; ++g_ListFailed; Fail = true; }
    if (Detail && g_DetailLeft) { --g_DetailLeft; ++g_DetailFailed; Fail = true; }
    if (Profile) ++g_ProfileWrites;
    unsigned ListLeft = g_ListLeft, ListFailed = g_ListFailed;
    unsigned DetailLeft = g_DetailLeft, DetailFailed = g_DetailFailed, Writes = g_ProfileWrites;
    xrtMutexUnlock(g_SessionReadProbeLock);
    if (!Arm && !Proof && !Fail) return false;
    MdoApiContext Context = {0}; Context.Request = Request; MdoApiRequestId(Context.RequestId);
    if (Fail) MdoApiReplyError(&Context, 503u, "fixture_session_read", "Fixture session read unavailable", NULL);
    else {
        xvalue* Data = xrtValueObject();
        MdoApiValueSetUInt(Data, "list_remaining", ListLeft); MdoApiValueSetUInt(Data, "list_failed", ListFailed);
        MdoApiValueSetUInt(Data, "detail_remaining", DetailLeft); MdoApiValueSetUInt(Data, "detail_failed", DetailFailed);
        MdoApiValueSetUInt(Data, "profile_writes", Writes);
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
        self.calls[body["model"]] += 1
        raw = json.dumps({"id": "session-read", "model": body["model"], "choices": [{"index": 0,
            "message": {"role": "assistant", "content": "SESSION_READY — 对话可以继续。"}, "finish_reason": "stop"}],
            "usage": {"prompt_tokens": 50, "completion_tokens": 20}}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)


def fixture(base, host, trace_input=False):
    app = base / "app"
    shutil.copytree(ROOT / "app", app)
    if trace_input:
        index = app / "web/index.html"
        text = index.read_text(encoding="utf-8")
        assert text.count("</body>") == 1
        index.write_text(text.replace("</body>", '<script src="/js/qa-input-trace.js"></script>\n</body>'),
            encoding="utf-8", newline="\n")
        (app / "web/js/qa-input-trace.js").write_text(INPUT_TRACE, encoding="utf-8", newline="\n")
    service = app / "src/bootstrap/service.c"
    text = service.read_text(encoding="utf-8")
    markers = ("void ServiceInit(XS_HostInfo* pHost)\n", "    bool ready = MdoBootstrapInit(pHost);\n",
        "    MdoRemoteUnit();\n", "    return MdoApiRequest(pRequest);\n")
    assert all(text.count(marker) == 1 for marker in markers)
    text = text.replace(markers[0], SEAM + markers[0])
    text = text.replace(markers[1], markers[1] + "    g_SessionReadProbeLock = xrtMutexCreate();\n")
    text = text.replace(markers[2], markers[2] + "    xrtMutexDestroy(g_SessionReadProbeLock);\n")
    text = text.replace(markers[3], "    if (SessionReadProbe(pRequest)) return XS_OK;\n" + markers[3])
    service.write_text(text, encoding="utf-8", newline="\n")
    packed = base / "session-read.exe"
    subprocess.run([str(host), "pack", str(app), "-o", str(packed)], check=True,
        creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
    return packed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, required=True)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--trace-input", action="store_true", help="Log synthetic input events only in this isolated fixture")
    args = parser.parse_args()
    base = args.directory.resolve()
    assert not base.exists(), "Choose a fresh isolated directory"
    base.mkdir(parents=True)
    packed = fixture(base, args.host.resolve(), args.trace_input)
    native, port = site(base, "native", packed)
    model = ThreadingHTTPServer(("127.0.0.1", 0), Model)
    worker = threading.Thread(target=model.serve_forever, daemon=True)
    worker.start()
    process = start(native, packed, base / "home", dict(os.environ, USE_WEBVIEW="0", MDO_QUEUE_FIXTURE_KEY="fixture-only"))
    def call(method, path, body=None, expected=200):
        status, headers, raw = request(port, method, path, body=None if body is None else json.dumps(body).encode(),
            headers={"Content-Type": "application/json"})
        assert status == expected, raw
        return json.loads(raw)["data"], headers
    try:
        status, bootstrap = wait_bootstrap(process, port, native / "packed.log")
        assert status == 200 and bootstrap["data"]["ready"], bootstrap
        configure_model(port, f"http://127.0.0.1:{model.server_port}/v1")
        config, headers = call("GET", "/api/v1/models/config")
        second = json.loads(json.dumps(next(m for m in config["items"] if m["id"] == "queue-fixture")))
        second.update(id="queue-fixture-two", name="Second fixture", wire_model="session-read-alternate")
        config["items"].append(second)
        config.pop("runtime_override", None)
        status, _, raw = request(port, "PUT", "/api/v1/settings/models",
            body=json.dumps({"schema_version": 1, "patch": config}).encode(),
            headers={"Content-Type": "application/json", "If-Match": headers["etag"]})
        assert status == 200, raw
        sessions = [call("POST", "/api/v1/sessions", {"project_id": "default", "title": title,
            "model_id": "queue-fixture", "reasoning_effort": "none", "permission_profile": "balanced"}, 201)[0]
            for title in ("Session read recovery", "Second saved task")]
        print(json.dumps({"url": f"http://127.0.0.1:{port}/#/projects/default/sessions/" + sessions[0]["id"],
            "native_port": port, "sessions": [s["id"] for s in sessions],
            "stop_file": str(base / "stop")}), flush=True)
        end = time.monotonic() + 600
        while not (base / "stop").exists() and time.monotonic() < end:
            assert process.poll() is None, "Native fixture exited"
            time.sleep(.1)
        proof = {"model_calls": dict(Model.calls), "faults": call("GET", "/api/v1/qa-session-proof")[0], "sessions": []}
        for session in sessions:
            path = "/api/v1/projects/default/sessions/" + session["id"]
            events = session_events(port, path)
            proof["sessions"].append({"id": session["id"], "detail": call("GET", path)[0],
                "user_inputs": [event.get("text", "") for event in events if event["kind"] == "agent_start"],
                "draft": call("GET", path + "/draft")[0],
                "final_errors": sum(event["kind"] == "error" for event in events),
                "event_kinds": dict(Counter(event["kind"] for event in events))})
        (base / "proof.json").write_text(json.dumps(proof, indent=2), encoding="utf-8")
    finally:
        stop(process)
        model.shutdown(); model.server_close(); worker.join(timeout=3)


if __name__ == "__main__":
    main()
