"""One real background child, held at a bounded model seam during cancellation.

The packed fixture uses the production task API and panel. No task DTO is
fabricated. Only the offline child model waits for an explicit release, so the
nonterminal stopping state can be inspected deterministically. No load loop.
"""
import argparse
from http.server import ThreadingHTTPServer
import json
import os
from pathlib import Path
import shutil
import subprocess
import threading
import time

from test_api_runtime import request
from test_packed_home_lease import site, start, stop, wait_bootstrap
from manual_task_stop_recovery_qa import PAGE, Proxy

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = r'''
static xatomic32 g_StopEntered, g_StopObserved, g_StopRelease;
static xwork_agent* g_StopParent;
static xllm_session* g_StopSession;
static xllm_executor g_StopExecutor;
static xllm_result StopChild(void* User, const xllm_request* Request,
    const xllm_stream_callbacks* Callbacks, xllm_response** Response,
    xllm_error* Error)
{
    (void)User; (void)Callbacks; *Response = NULL;
    xrtAtomic32Store(&g_StopEntered, 1u, XMEMORY_RELEASE);
    uint64 Deadline = xrtDeadlineAfter(UINT64_C(300000000));
    while (!xrtCancelRequested(Request->pCancel) && !xrtDeadlineExpired(Deadline))
        xrtSleep(10u);
    if (xrtCancelRequested(Request->pCancel))
        xrtAtomic32Store(&g_StopObserved, 1u, XMEMORY_RELEASE);
    while (!xrtAtomic32Load(&g_StopRelease, XMEMORY_ACQUIRE) && !xrtDeadlineExpired(Deadline))
        xrtSleep(10u);
    xllmErrorInit(Error); Error->eCode = XLLM_ERROR_CANCELLED;
    return XLLM_RESULT_CANCELLED;
}
static void StopFixtureInit(void)
{
    xwork_error Error;
    xllm_session_config Session;
    xwork_agent_definition_config Definition;
    xwork_agent_options Options;
    xwork_subagent_definition_config Child;
    const char* NoTools[] = { NULL };
    xrtAtomic32Init(&g_StopEntered, 0u);
    xrtAtomic32Init(&g_StopObserved, 0u);
    xrtAtomic32Init(&g_StopRelease, 0u);
    xllmSessionConfigInit(&Session);
    g_StopSession = xllmSessionCreateForTest(&Session, StopChild, NULL, NULL);
    xworkAgentDefinitionConfigInit(&Definition);
    Definition.sId = "fixture.stop";
    Definition.bAutoSaveSession = false;
    Definition.bRequireVerificationAfterWrite = false;
    Definition.eApprovalMode = XWORK_APPROVAL_AUTO;
    xwork_agent_definition* Def = xworkAgentDefinitionCreate(&Definition, &Error);
    xworkAgentOptionsInit(&Options);
    Options.pSession = g_StopSession;
    Options.sWorkspaceRoot = ".";
    g_StopParent = xworkAgentCreateWithRuntime(MdoBootstrapRuntime(), Def, &Options, &Error);
    xworkAgentDefinitionRelease(Def);
    xworkSubagentDefinitionConfigInit(&Child);
    Child.sName = "hold";
    Child.sDescription = "Offline stopping fixture";
    Child.sSystemPrompt = "Wait for cancellation.";
    Child.psTools = NoTools;
    Child.iToolCount = 0u;
    Child.bAllowBackground = true;
    Child.uTimeoutMs = 300000u;
    if (!g_StopParent || !xworkAgentReplaceSubagentDefinitions(g_StopParent, &Child, 1u, &Error) ||
        !xworkExecutorBind(&g_StopExecutor, g_StopParent, &Error)) {
        fprintf(stderr, "stop_fixture_init_failed=%s\n", Error.sMessage); return;
    }
    xllm_tool_call Call = {0};
    xllm_executor_ctx Context = {0};
    xllm_executor_result Result = {0};
    Call.sId = "stop-child"; Call.sName = "agent";
    Call.sArgumentsJson = "{\"name\":\"hold\",\"prompt\":\"hold\",\"background\":true}";
    if (!g_StopExecutor.pExecute(g_StopExecutor.pUserData, &Call, &Context, &Result) || !Result.bSuccess)
        fprintf(stderr, "stop_fixture_spawn_failed=%s\n", Result.sContent ? Result.sContent : "empty");
}
static bool StopFixtureControl(XS_HttpReq* Request)
{
    if (!Request || !Request->head) return false;
    xstrview Target = Request->head->Target;
    const char* ReleasePath = "/api/v1/qa-child-release";
    const char* StatusPath = "/api/v1/qa-child-status";
    bool Release = Target.Size == strlen(ReleasePath) &&
        memcmp(Target.Data, ReleasePath, Target.Size) == 0;
    if (!Release && !(Target.Size == strlen(StatusPath) &&
        memcmp(Target.Data, StatusPath, Target.Size) == 0)) return false;
    if (Release) xrtAtomic32Store(&g_StopRelease, 1u, XMEMORY_RELEASE);
    MdoApiContext Context = {0}; Context.Request = Request;
    MdoApiRequestId(Context.RequestId);
    xvalue* Data = xrtValueObject();
    MdoApiValueSetBool(Data, "entered", xrtAtomic32Load(&g_StopEntered, XMEMORY_ACQUIRE) != 0u);
    MdoApiValueSetBool(Data, "cancel_observed", xrtAtomic32Load(&g_StopObserved, XMEMORY_ACQUIRE) != 0u);
    MdoApiReplySuccessTake(&Context, 200u, Data, NULL);
    return true;
}
static void StopFixtureUnit(void)
{
    xrtAtomic32Store(&g_StopRelease, 1u, XMEMORY_RELEASE);
    xwork_task_snapshot* Tasks = xworkRuntimeTaskSnapshot(MdoBootstrapRuntime(), 0u, NULL);
    xwork_task_info Task;
    for (size_t i = 0; i < xworkTaskSnapshotCount(Tasks); ++i)
        if (xworkTaskSnapshotTaskAt(Tasks, i, &Task)) {
            xworkRuntimeCancelTask(MdoBootstrapRuntime(), Task.uTaskId, NULL);
            xworkRuntimeWaitTasks(MdoBootstrapRuntime(), &Task.uTaskId, 1u,
                XWORK_TASK_WAIT_ALL, xrtDeadlineAfter(UINT64_C(2000000)), NULL, NULL, NULL);
        }
    xworkTaskSnapshotRelease(Tasks);
    xworkExecutorUnbind(&g_StopExecutor);
    xworkAgentDestroy(g_StopParent);
    xllmSessionDestroy(g_StopSession);
}
'''


class StoppingProxy(Proxy):
    def proxy(self):
        if self.path == "/":
            page = PAGE.replace('<button id="proof">',
                '<button id="child-release">释放子智能体</button><button id="proof">')
            page = page.replace("document.querySelector('#release').addEventListener",
                "document.querySelector('#child-release').addEventListener('click',()=>void api.get('/qa-child-release'));\n"
                "document.querySelector('#release').addEventListener")
            self.reply(200, page.encode(), "text/html; charset=utf-8")
            return
        if self.path == "/api/v1/qa-stop-proof":
            tasks = json.loads(request(self.server.native, "GET", "/api/v1/tasks")[2])["data"]["items"]
            child = json.loads(request(self.server.native, "GET", "/api/v1/qa-child-status")[2])["data"]
            self.reply(200, json.dumps({"ok": True, "data": {"native_tasks": tasks, "child": child,
                "faults": self.server.counts, "secondary_read_pending": self.server.secondary_pending}}).encode())
            return
        super().proxy()

    do_GET = do_DELETE = do_POST = do_PUT = proxy


def fixture(base: Path, host: Path) -> Path:
    app = base / "app"
    shutil.copytree(ROOT / "app", app)
    service = app / "src/bootstrap/service.c"
    text = service.read_text(encoding="utf-8")
    init = "    bool ready = MdoBootstrapInit(pHost);\n"
    declaration = "void ServiceInit(XS_HostInfo* pHost)\n"
    unit = "    MdoRemoteUnit();\n"
    route = "    return MdoApiRequest(pRequest);\n"
    assert all(text.count(marker) == 1 for marker in (init, declaration, unit, route))
    text = text.replace(declaration, FIXTURE + declaration)
    text = text.replace(init, init + "    if (ready) StopFixtureInit();\n")
    text = text.replace(unit, "    StopFixtureUnit();\n" + unit)
    text = text.replace(route, "    if (StopFixtureControl(pRequest)) return XS_OK;\n" + route)
    service.write_text(text, encoding="utf-8", newline="\n")
    packed = base / "task-stopping.exe"
    subprocess.run([str(host), "pack", str(app), "-o", str(packed)], check=True,
        creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
    return packed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, required=True, help="Newly built xsw pack host")
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--manual", action="store_true")
    args = parser.parse_args()
    base = args.directory.resolve()
    assert not base.exists(), "Choose a fresh isolated fixture directory"
    base.mkdir(parents=True)
    packed = fixture(base, args.host.resolve())
    native, port = site(base, "native", packed)
    process = start(native, packed, base / "home", dict(os.environ, USE_WEBVIEW="0"))
    proxy = ThreadingHTTPServer(("127.0.0.1", 0), StoppingProxy)
    worker = None
    proxy.native, proxy.modes, proxy.counts, proxy.sessions = port, {}, {}, []
    proxy.secondary_release = threading.Event()
    proxy.secondary_pending = False
    def call(method, path):
        status, _, raw = request(port, method, path)
        assert status == 200, raw
        return json.loads(raw)["data"]
    def tasks():
        return call("GET", "/api/v1/tasks")["items"]
    try:
        status, bootstrap = wait_bootstrap(process, port, native / "packed.log")
        assert status == 200 and bootstrap["data"]["ready"], bootstrap
        end = time.monotonic() + 5
        while not call("GET", "/api/v1/qa-child-status")["entered"]:
            assert time.monotonic() < end
            time.sleep(.01)
        before, = tasks()
        assert before["kind"] == "agent" and before["state"] == "running" and not before["stop_requested"], before
        path = "/api/v1/tasks/" + str(before["id"])
        if args.manual:
            proxy.modes[path] = "secondary"
            proxy.counts["secondary"] = {"http_deletes": 0, "native_deletes": 0,
                "reads_after_delete": 0, "held_list_reads": 0}
            worker = threading.Thread(target=proxy.serve_forever, daemon=True)
            worker.start()
            print(json.dumps({"url": f"http://127.0.0.1:{proxy.server_port}/", "stop_file": str(base / "stop")}), flush=True)
            end = time.monotonic() + 240
            while not (base / "stop").exists() and time.monotonic() < end:
                assert process.poll() is None, "Native fixture exited"
                time.sleep(.1)
        else:
            stopped = call("DELETE", path)
            assert stopped["state"] == "running" and stopped["stop_requested"] and not stopped["terminal"], stopped
            repeat = call("DELETE", path)
            assert repeat["revision"] == stopped["revision"] and repeat["stop_requested"], repeat
            detail = call("GET", path)
            listed, = tasks()
            assert detail == listed and detail == repeat, (detail, listed)
            call("GET", "/api/v1/qa-child-release")
            end = time.monotonic() + 3
            while not (final := call("GET", path))["terminal"]:
                assert time.monotonic() < end
                time.sleep(.01)
            assert final["state"] == "cancelled" and final["stop_requested"], final
            proof = {"before": before, "accepted": stopped, "repeat": repeat,
                "list_matches_detail": True, "repeat_preserves_revision": True, "final": final}
            (base / "proof.json").write_text(json.dumps(proof, indent=2), encoding="utf-8")
            print("Real packed/TCC child stopping state, idempotent cancellation, list/detail and terminal transition: PASS")
    finally:
        proxy.secondary_release.set()
        try:
            if process.poll() is None:
                call("GET", "/api/v1/qa-child-release")
                for task in tasks():
                    call("DELETE", "/api/v1/tasks/" + str(task["id"]))
        finally:
            stop(process)
            if worker:
                proxy.shutdown()
                worker.join(timeout=3)
            proxy.server_close()


if __name__ == "__main__":
    main()
