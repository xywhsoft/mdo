"""Bounded online-token recovery through real HTTP/xs/TCC, without a website.

The authority models token rotation and account cancellation; only fixed
fixture credentials reach the loopback model. These are functional cases,
not a load test. Actual website/account composition has separate coverage.
"""
from __future__ import annotations
import argparse
from collections import Counter
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import tempfile
import threading
from test_model_runtime import ROOT, run_probe, write_site

PROBE = r'''
#include <stdio.h>
#include <string.h>
#include <xsbase.h>
#define MDO_MODEL_RETRY_BASE_MS 10u
#define MDO_MODEL_RECOVERY_MS 2000u
#include "src/storage/home.c"
#include "src/config/config.c"
#include "src/security/secrets.c"
#include "src/models/catalog.c"

typedef struct Lease { xcancel* Cancel; xcancelwatch* Watch; } Lease;
static const char* Case;
static xcancel* AccountCancel;
static bool Fresh;
static unsigned Acquired, Released, Deltas, Retries;
static uint64 RequestDeadline;
static void CancelLease(void* Data) { xrtCancelRequest((xcancel*)Data); }
static bool Acquire(xcancel* Cancel,uint64 Deadline,MdoModelOnlineAccess* Access) {
    if (Fresh && !strcmp(Case,"deadline")) {
        while (!xrtDeadlineExpired(Deadline)) xrtSleep(1u);
        return false;
    }
    if ((Cancel && xrtCancelRequested(Cancel)) || xrtCancelRequested(AccountCancel)) return false;
    Lease* L=xrtCalloc(1u,sizeof(*L)); if (!L) return false;
    L->Cancel=xrtCancelChild(Cancel);
    L->Watch=xrtCancelWatch(AccountCancel,CancelLease,L->Cancel);
    if (!L->Cancel || !L->Watch) {
        xrtCancelUnwatch(L->Watch);xrtCancelDestroy(L->Cancel);xrtFree(L);return false;
    }
    Access->Token=Fresh?"fresh-fixture-token":"expired-fixture-token";
    Access->Cancel=L->Cancel; Access->Handle=L; ++Acquired; return true;
}
static void Release(MdoModelOnlineAccess* Access,int Status) {
    Lease* L=Access->Handle;
    if (Status==401 || (Status==503 && !strcmp(Case,"backoff-expiry"))) Fresh=true;
    if (Status==401 && !strcmp(Case,"cancel")) xrtCancelRequest(AccountCancel);
    xrtCancelUnwatch(L->Watch); xrtCancelDestroy(L->Cancel); xrtFree(L);
    memset(Access,0,sizeof(*Access)); ++Released;
}
static bool Event(void* Data,const xllm_event* E) {
    (void)Data;
    if (E->eKind==XLLM_EVENT_TEXT_DELTA) ++Deltas;
    return true;
}
static bool Retry(xllm_client* Client,const xllm_diagnostics* Diagnostics,uint32 Next,void* Data) {
    (void)Client;(void)Data;
    if (Next!=Diagnostics->uAttemptCount+1u || Diagnostics->uMaxAttempts!=6u) return false;
    ++Retries; return strcmp(Case,"hook-stop")!=0;
}
void ServiceInit(XS_HostInfo* Host) {
    (void)Host;
    if (!MdoHomeInit() || !MdoConfigInit() || !MdoModelManagerInit()) {
        printf("init_failed=1\nprobe_done=1\n"); return;
    }
    MdoModelOnlineAuthority Authority={getenv("MDO_AUTH_FIXTURE_ORIGIN"),Acquire,Release};
    MdoModelManagerSetOnlineAuthority(&Authority);
    MdoModelCatalog* Catalog=MdoModelCatalogSnapshot();
    MdoModelClientOptions Options;MdoModelClientOptionsInit(&Options);
    Options.Protocol=MDO_MODEL_PROTOCOL_OPENAI_CHAT_COMPLETIONS;Options.MaxOutputTokens=64u;
    const char* Names[]={"renew","expired-again","backoff-expiry","business","budget",
        "cancel","deadline","partial","hook-stop","preexpired","cancelled","nullable-error"};
    for (unsigned i=0;i<sizeof(Names)/sizeof(Names[0]);++i) {
        Case=Names[i];Fresh=false;Acquired=Released=Deltas=Retries=0u;
        AccountCancel=xrtCancelCreate();xcancel* UserCancel=xrtCancelCreate();
        xllm_request Request;xllmRequestInit(&Request);
        xllmRequestAddTextMessage(&Request,XLLM_ROLE_USER,Case);Request.uMaxOutputTokens=64u;
        Request.pCancel=UserCancel;RequestDeadline=XRT_DEADLINE_NEVER;
        if (!strcmp(Case,"deadline")) RequestDeadline=xrtDeadlineAfter(100000u);
        if (!strcmp(Case,"preexpired")) RequestDeadline=xrtClock()-1u;
        if (!strcmp(Case,"cancelled")) xrtCancelRequest(UserCancel);
        Request.uDeadline=RequestDeadline;
        xllm_stream_callbacks Callbacks={0};Callbacks.OnEvent=Event;
        xllm_hooks Hooks={0};Hooks.pOnRetry=Retry;Request.pHooks=&Hooks;
        Request.bStream=!strcmp(Case,"partial");
        xllm_response* Response=NULL;xllm_error Error;xllmErrorInit(&Error);
        uint64 Started=xrtClock();
        xllm_result Result=MdoModelOnlineComplete(Catalog,&Options,&Request,&Callbacks,&Response,
            !strcmp(Case,"nullable-error")?NULL:&Error);
        printf("case=%s result=%d kind=%s attempts=%u acquired=%u released=%u deltas=%u retries=%u ms=%llu\n",
            Case,Result,MdoModelErrorKind(&Error),Error.tDiagnostics.uAttemptCount,Acquired,Released,
            Deltas,Retries,(unsigned long long)((xrtClock()-Started)/1000u));
        if(Request.pCancel!=UserCancel || Request.uDeadline!=RequestDeadline || Request.iMessageCount!=1u || Request.uMaxOutputTokens!=64u)
            printf("request_mutated=1\n");
        xllmResponseDestroy(Response);xllmRequestUnit(&Request);
        xrtCancelDestroy(UserCancel);xrtCancelDestroy(AccountCancel);
    }
    MdoModelCatalogRelease(Catalog);MdoModelManagerSetOnlineAuthority(NULL);
    MdoModelManagerUnit();MdoConfigUnit();MdoHomeUnit();
    printf("probe_done=1\n");
}
'''

class Model(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    calls = Counter()
    tokens: dict[str, list[str]] = {}
    def log_message(self, *_): pass
    def handle(self):
        try: super().handle()
        except (ConnectionResetError, BrokenPipeError): pass
    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        name = body["messages"][0]["content"]
        self.calls[name] += 1
        attempt = self.calls[name]
        token = self.headers.get("Authorization", "")
        self.tokens.setdefault(name, []).append(token)
        if name == "partial":
            assert body.get("stream") is True,body
            self.send_response(200);self.send_header("Content-Type", "text/event-stream")
            self.send_header("Transfer-Encoding", "chunked");self.send_header("Connection","close");self.end_headers()
            chunk = {"id":"partial","model":"ornith-1.5-35b","choices":[{"index":0,"delta":{"content":"unfinished fixture reply"},"finish_reason":None}]}
            data = ("data: " + json.dumps(chunk) + "\n\n").encode()
            self.wfile.write(f"{len(data):x}\r\n".encode() + data + b"\r\n")
            self.wfile.flush();self.close_connection=True;return
        status, code = 200, ""
        if name == "business": status, code = 401, "upstream_configuration_error"
        elif name == "budget": status, code = (503,"unavailable") if attempt < 6 else (401,"invalid_token")
        elif name == "expired-again": status, code = 401, "invalid_token"
        elif name == "backoff-expiry":
            if attempt == 1: status, code = 503, "unavailable"
            elif token != "Bearer fresh-fixture-token": status, code = 401, "invalid_token"
        elif token != "Bearer fresh-fixture-token": status, code = 401, "invalid_token"
        value = {"error":{"code":code,"message":"fixture rejection"}} if status != 200 else {
            "id":"fixture-reply","choices":[{"message":{"role":"assistant","content":"fixture completed"},"finish_reason":"stop"}],
            "usage":{"prompt_tokens":1,"completion_tokens":1,"total_tokens":2}}
        data = json.dumps(value).encode()
        self.send_response(status);self.send_header("Content-Type","application/json")
        self.send_header("Content-Length",str(len(data)));self.send_header("Connection","close")
        self.end_headers();self.wfile.write(data)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, default=ROOT/".build/host/xs.exe")
    parser.add_argument("--record", type=Path)
    args = parser.parse_args()
    Model.calls=Counter();Model.tokens={}
    server = ThreadingHTTPServer(("127.0.0.1",0),Model)
    worker = threading.Thread(target=server.serve_forever,daemon=True);worker.start()
    try:
        with tempfile.TemporaryDirectory(prefix="online-auth-retry-",dir=ROOT/".build") as raw:
            base=Path(raw);site=base/"site";write_site(site)
            (site/"probe.c").write_text(PROBE,encoding="utf-8")
            output=run_probe(args.host.resolve(),site,base/"home",{
                "MDO_AUTH_FIXTURE_ORIGIN":f"http://127.0.0.1:{server.server_port}"})
        print(output[output.find("case="):output.find("probe_done=")])
        assert "init_failed" not in output and "probe_done=1" in output and "request_mutated=1" not in output,output
        expected={"renew":(0,"unknown",2),"expired-again":(-1,"login_required",2),
            "backoff-expiry":(0,"unknown",2),"business":(-1,"service_configuration",1),
            "budget":(-1,"login_required",6),"cancel":(-3,"cancelled",1),
            "deadline":(-2,"timeout",1),"partial":(-1,"network",1),
            "hook-stop":(-1,"login_required",1),"preexpired":(-2,"timeout",0),
            "cancelled":(-3,"cancelled",0),"nullable-error":(0,"unknown",2)}
        for name,(result,kind,count) in expected.items():
            line=next(line for line in output.splitlines() if line.startswith(f"case={name} "))
            fields=dict(part.split("=",1) for part in line.split())
            assert (int(fields["result"]),fields["kind"],Model.calls[name])==(result,kind,count),(line,Model.calls)
            assert fields["acquired"]==fields["released"],line
            if name != "nullable-error": assert int(fields["attempts"])==count,line
        for name in ("renew","expired-again","nullable-error","backoff-expiry"):
            assert Model.tokens[name]==["Bearer expired-fixture-token","Bearer fresh-fixture-token"],name
        record={"passed":True,"cases":expected,"http_requests":dict(Model.calls),"output":output[output.find("case="):output.find("probe_done=")],
            "scope":"loopback HTTP, real product completion, simulated authority; no website or private credentials"}
        if args.record:
            args.record.parent.mkdir(parents=True,exist_ok=True);args.record.write_text(json.dumps(record,indent=2)+"\n")
        print("Online token recovery, shared retry budget, cancellation and deadline: PASS")
    finally:
        server.shutdown();server.server_close();worker.join(timeout=3)

if __name__=="__main__":main()
