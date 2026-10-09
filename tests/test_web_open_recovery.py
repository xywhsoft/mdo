"""Finite native web_open recovery cases; local HTTP only, no provider charges."""
from __future__ import annotations

import argparse
import json
import socket
import tempfile
import threading
import time
from email.utils import formatdate
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

import test_web_runtime as web


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    calls: dict[str, list[float]] = {}

    def log_message(self, *_args: object) -> None:
        pass

    def do_GET(self) -> None:
        calls = self.calls.setdefault(self.path, [])
        calls.append(time.monotonic())
        attempt = len(calls)
        if self.path in ("/disconnect", "/short-network") and attempt <= 2:
            self.connection.shutdown(socket.SHUT_RDWR)
            self.connection.close()
            self.close_connection = True
            return
        if self.path == "/partial" and attempt <= 2:
            self.send_response(200)
            self.send_header("Content-Length", "100")
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(b"partial")
            self.close_connection = True
            return
        if self.path == "/malformed":
            self.connection.sendall(b"HTTP/1.1 invalid\r\n\r\n")
            self.close_connection = True
            return
        if self.path == "/deadline-late" and attempt == 2:
            time.sleep(0.5)
        status = 200
        if self.path in ("/recover", "/deadline", "/deadline-late", "/cancel", "/cancel-wait") and attempt <= 2:
            status = 503
        elif self.path == "/exhausted":
            status = 502
        elif self.path.startswith("/limited") and attempt == 1:
            status = 429
        elif self.path == "/date" and attempt == 1:
            status = 503
        elif self.path in ("/denied", "/missing"):
            status = 403 if self.path == "/denied" else 404
        body = b"recovered page" if status == 200 else b"HOSTILE_UPSTREAM_BODY_DO_NOT_SHOW"
        if self.path == "/large":
            body = b"x" * 32768
        self.send_response(status)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        if self.path.startswith("/limited"):
            value = {"/limited-long": "120", "/limited-overflow": "9999999999999999999999999",
                     "/limited-invalid": "not a date"}.get(self.path, "2")
            self.send_header("Retry-After", value)
            if self.path == "/limited-duplicate":
                self.send_header("Retry-After", "1")
        if self.path == "/date":
            self.send_header("Retry-After", formatdate(time.time() + 3, usegmt=True))
        self.end_headers()
        self.wfile.write(body)


class QuietServer(ThreadingHTTPServer):
    def handle_error(self, _request: object, _address: object) -> None:
        # Only the deliberately disconnected fixture may reset its socket.
        import sys
        if isinstance(sys.exception(), (BrokenPipeError, ConnectionResetError)):
            return
        super().handle_error(_request, _address)


def source(port: int, baseline: bool) -> str:
    text = web.PROBE_SOURCE
    start = text.index("static bool Fetch(")
    end = text.index("static void ResponseUnit(", start)
    text = text[:start] + r'''
static xcancel* ProbeCancel;
static uint64 ProbeDeadline;
static xthread* ProbeCancelThread;
static int32 CancelLater(void* cancel) {
    xrtSleep(120u); xrtCancelRequest((xcancel*)cancel); return 0;
}
static bool Fetch(void* data, const XS_FetchRequest* request,
    XS_FetchResponse* response) {
    Probe* probe = (Probe*)data;
    ++probe->Fetches;
    if (strstr(request->Url, "/policy")) {
        xerror* cause = xrtErrorCreate(XERR_PERMISSION, "probe.policy", 1, "Denied by URL policy");
        xerrordesc desc = {0}; desc.Kind = XERR_IO; desc.Domain = "xs.fetch";
        desc.Message = "wrapped policy failure"; desc.Cause = cause;
        xrtSetErrorTake(xrtErrorBuild(&desc)); xrtErrorFree(cause); return false;
    }
    bool ok = xsFetch(request, response);
    if (strstr(request->Url, "/cancel-wait") && ProbeCancel && probe->Fetches == 1u)
        ProbeCancelThread = xrtThreadCreate(CancelLater, ProbeCancel, 0u);
    else if (strstr(request->Url, "/cancel") && ProbeCancel && probe->Fetches == 1u)
        xrtCancelRequest(ProbeCancel);
    return ok;
}
''' + text[end:]
    text = text.replace("context.uDeadline = xrtDeadlineAfter(5000000u);",
                        "context.uDeadline = ProbeDeadline; context.pCancel = ProbeCancel;")
    start = text.index('    if (!Execute(agent, "web_search"')
    end = text.index('    printf("probe_done=1', start)
    cases = ["recover", "disconnect", "denied", "missing", "deadline", "cancel"]
    if not baseline:
        cases += ["partial", "malformed", "policy", "large", "limited", "date", "limited-long",
                  "limited-overflow", "limited-invalid", "limited-duplicate", "deadline-late", "short-network", "cancel-wait", "exhausted"]
    calls = []
    for name in cases:
        arguments = json.dumps({"url": f"http://127.0.0.1:{port}/{name}"})
        calls.append(f'''
    probe.Fetches = 0u;
    ProbeDeadline = xrtDeadlineAfter({200000 if name in ('deadline', 'short-network') else 800000 if name == 'deadline-late' else 25000000}u);
    ProbeCancel = {'xrtCancelCreate()' if name.startswith('cancel') else 'NULL'};
    uint64 started_{name.replace('-', '_')} = xrtClock();
    bool ok_{name.replace('-', '_')} = Execute(agent, "web_open", {json.dumps(arguments)}, &open);
    printf("case={name} success:%d fetches:%u elapsed_ms:%llu\\n", ok_{name.replace('-', '_')},
        probe.Fetches, (unsigned long long)((xrtClock() - started_{name.replace('-', '_')}) / 1000u));
    xrtFree(open); open = NULL;
    if (ProbeCancelThread) {{ xrtThreadWait(ProbeCancelThread); xrtThreadDestroy(ProbeCancelThread); ProbeCancelThread = NULL; }}
    xrtCancelDestroy(ProbeCancel); ProbeCancel = NULL;
''')
    summary = r'''
    memset(&snapshot, 0, sizeof(snapshot)); snapshot.Size = sizeof(snapshot);
    if (MdoWebManagerGetSnapshot(&snapshot))
        printf("summary=permissions:%u docs:%zu completed:%llu failed:%llu\n", probe.Permissions,
            snapshot.DocumentCount, (unsigned long long)snapshot.RequestsCompleted,
            (unsigned long long)snapshot.RequestsFailed);
'''
    preflight = "" if baseline else r'''
    {
        xwork_tool_context expired = {0}; XS_FetchResponse unused = {0};
        expired.uDeadline = xrtDeadlineAfter(1u); xrtSleep(2u);
        xrtSetErrorKind(XERR_PERMISSION);
        if (MdoWebFetchRequest(g_MdoWeb.Current, &expired, "https://example.com/", NULL, 0u,
                false, NULL, 0u, &unused, NULL) || xrtGetError() == NULL ||
            xrtErrorKind(xrtGetError()) != XERR_TIMEOUT || probe.Fetches != 0u)
            printf("expired_preflight_failed=1\n");
        xrtClearError();
    }
'''
    text = text[:start] + preflight + "".join(calls) + summary + text[end:]
    # ServiceInit fixtures terminate after their finite assertions; no server
    # remains running and the runner never interprets a timeout as completion.
    return text.replace('    MdoHomeUnit();', '    MdoHomeUnit(); fflush(stdout); exit(0);')


def run(host: Path, baseline: bool, output_file: Path | None) -> None:
    server = QuietServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    Handler.calls = {}
    try:
        with tempfile.TemporaryDirectory(prefix="web-open-recovery-", dir=web.ROOT / ".build") as raw:
            base = Path(raw)
            web.write_site(base / "site")
            defaults = base / "site/default-home/config/defaults.json"
            settings = json.loads(defaults.read_text(encoding="utf-8"))
            settings["settings"]["web"]["allow_http"] = True
            settings["settings"]["web"]["allow_private_networks"] = True
            if not baseline:
                settings["settings"]["web"]["max_response_bytes"] = 16384
                settings["settings"]["web"]["max_text_bytes"] = 4096
            defaults.write_text(json.dumps(settings), encoding="utf-8")
            (base / "site/probe.c").write_text(source(server.server_port, baseline), encoding="utf-8")
            import os
            import subprocess
            if baseline:
                for relative in ("src/web/manager.c", "src/web/search_api.inc.c"):
                    original = subprocess.check_output(["git", "show", "01894d08:app/" + relative], cwd=web.ROOT)
                    (base / "site" / relative).write_bytes(original)
            env = os.environ.copy()
            env["MDO_TEST_SEARCH_ACCESS_TOKEN"] = "probe-secret"
            proc = subprocess.run([str(host.resolve()), "xs.json", "--", "--home", str(base / "home")],
                                  cwd=base / "site", env=env, capture_output=True, text=True,
                                  encoding="utf-8", errors="replace", timeout=65,
                                  creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
            output = proc.stdout + proc.stderr
            assert proc.returncode == 0 and "probe_done=1" in output, output
            assert "init_error=" not in output, output
            assert "expired_preflight_failed=" not in output, output
            assert "HOSTILE_UPSTREAM_BODY_DO_NOT_SHOW" not in output, output
            for name in ("recover", "disconnect"):
                expected = 0 if baseline else 1
                count = 1 if baseline else 3
                assert f"case={name} success:{expected} fetches:{count}" in output, output
            for name in ("denied", "missing", "deadline", "cancel"):
                assert f"case={name} success:0 fetches:1" in output, output
            if not baseline:
                for name in ("limited", "date"):
                    assert f"case={name} success:1 fetches:2" in output, output
                assert Handler.calls["/limited"][1] - Handler.calls["/limited"][0] >= 1.95
                assert Handler.calls["/date"][1] - Handler.calls["/date"][0] >= 1.9
                assert "case=limited-long success:0 fetches:1" in output, output
                assert "case=exhausted success:0 fetches:6" in output, output
                assert "case=partial success:1 fetches:3" in output, output
                for name in ("malformed", "policy", "large", "limited-overflow", "short-network", "cancel-wait"):
                    assert f"case={name} success:0 fetches:1" in output, output
                assert "case=deadline-late success:0 fetches:2" in output, output
                for name in ("limited-invalid", "limited-duplicate"):
                    assert f"case={name} success:1 fetches:2" in output, output
                assert Handler.calls["/limited-duplicate"][1] - Handler.calls["/limited-duplicate"][0] >= 1.95
                for marker in ("HTTP 403", "HTTP 404", "HTTP 429", "HTTP 502"):
                    assert marker in output, output
                for marker in ("protocol validation", "policy denied", "response size limit"):
                    assert marker in output, output
                assert "Web page could not be reached (1 attempt). The next retry would exceed" in output, output
                assert output.count("execute_web_open=") == 20, output
                assert "summary=permissions:20 docs:7 completed:7 failed:13" in output, output
            record = {"baseline": baseline, "native_output": output,
                      "requests": {name: [round(t - times[0], 3) for t in times]
                                   for name, times in Handler.calls.items()}}
            if output_file:
                output_file.parent.mkdir(parents=True, exist_ok=True)
                output_file.write_text(json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=3)
    print("PASS baseline first-failure reproduction" if baseline else "PASS native web_open quiet recovery/HTTP reasons/Retry-After/cancel/deadline")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, required=True)
    parser.add_argument("--expect-baseline", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    run(args.host, args.expect_baseline, args.output)
