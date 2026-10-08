"""Real account-worker/model composition with a controlled loopback identity.

Verify renewal success, a short caller deadline, logout and account switching
while renewal is in flight. The identity is synthetic; xadmin login/JWT and
gateway billing have separate integration tests. No public credentials/load.
"""
from __future__ import annotations
import argparse
from collections import Counter
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import tempfile
import threading
import time
from test_model_runtime import ROOT, run_probe, write_site
from runtime_sources import copy_app_source

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
#include "src/account/client.c"
#include "src/account/credential.c"
#include "src/account/authorization.c"
#include "src/account/session.c"
static bool Idle(uint64 Member) {
    uint64 Until=xrtDeadlineAfter(2000000u);
    while(!xrtDeadlineExpired(Until)) {
        xrtMutexLock(g_MdoAccount.Lock);
        bool Ready=g_MdoAccount.Tokens.MemberId==Member&&!g_MdoAccount.Busy&&!g_MdoAccount.Work.Kind;
        xrtMutexUnlock(g_MdoAccount.Lock);
        if(Ready)return true;xrtSleep(2u);
    }
    return false;
}
static int32 Transition(void* Data) {
    xrtSleep(70u);
    if(!strcmp((const char*)Data,"account-switch"))
        MdoAccountLoginPassword("switched_member","Fixture-only-2026",false);
    else MdoAccountLogout();
    return 0;
}
void ServiceInit(XS_HostInfo* Host) {
    (void)Host;
    if(!MdoHomeInit()||!MdoConfigInit()||!MdoModelManagerInit()||!MdoAccountInit()) {
        printf("init_failed=1\nprobe_done=1\n");return;
    }
    const char* Names[]={"account-renew","account-deadline","account-logout","account-switch","account-business"};
    for(unsigned i=0;i<sizeof(Names)/sizeof(Names[0]);++i) {
        const char* Name=Names[i];
        if(!MdoAccountLoginPassword(Name,"Fixture-only-2026",false)||!Idle(1u)) {
            printf("login_failed=1\nprobe_done=1\n");return;
        }
        MdoModelCatalog* Catalog=MdoModelCatalogSnapshot();
        MdoModelClientOptions Options;MdoModelClientOptionsInit(&Options);
        Options.Protocol=MDO_MODEL_PROTOCOL_OPENAI_CHAT_COMPLETIONS;Options.MaxOutputTokens=64u;
        xllm_request Request;xllmRequestInit(&Request);
        xllmRequestAddTextMessage(&Request,XLLM_ROLE_USER,Name);Request.uMaxOutputTokens=64u;
        Request.bStream=false;
        xthread* Thread=NULL;
        if(!strcmp(Name,"account-logout")||!strcmp(Name,"account-switch"))Thread=xrtThreadCreate(Transition,(void*)Name,0);
        if(!strcmp(Name,"account-deadline"))Request.uDeadline=xrtDeadlineAfter(100000u);
        xllm_response* Response=NULL;xllm_error Error;xllmErrorInit(&Error);
        uint64 Started=xrtClock();
        xllm_result Result=MdoModelOnlineComplete(Catalog,&Options,&Request,NULL,&Response,&Error);
        uint64 Elapsed=(xrtClock()-Started)/1000u;
        if(Thread){xrtThreadWait(Thread);xrtThreadDestroy(Thread);}
        if(!strcmp(Name,"account-switch")&&!Idle(2u))printf("switch_failed=1\n");
        printf("case=%s result=%d kind=%s attempts=%u acquirers=%zu ms=%llu\n",Name,Result,
            MdoModelErrorKind(&Error),Error.tDiagnostics.uAttemptCount,g_MdoAccount.Acquirers,(unsigned long long)Elapsed);
        xllmResponseDestroy(Response);xllmRequestUnit(&Request);MdoModelCatalogRelease(Catalog);
        MdoAccountLogout();if(!Idle(0u))printf("logout_failed=1\n");
    }
    MdoAccountUnit();MdoModelManagerUnit();MdoConfigUnit();MdoHomeUnit();
    printf("probe_done=1\n");
}
'''

class Identity(BaseHTTPRequestHandler):
    protocol_version="HTTP/1.1"
    calls=Counter()
    renewals=Counter()
    case=""
    def log_message(self,*_):pass
    def handle(self):
        try:super().handle()
        except (ConnectionResetError,BrokenPipeError):pass
    def reply(self,status,value):
        data=json.dumps(value).encode()
        self.send_response(status);self.send_header("Content-Type","application/json")
        self.send_header("Content-Length",str(len(data)));self.send_header("Connection","close")
        self.end_headers();self.wfile.write(data)
    def do_GET(self):
        if self.path=="/api/v1/profile":
            member=2 if "switched-member" in self.headers.get("Authorization","") else 1
            return self.reply(200,{"code":0,"data":{"id":member,"username":"fixture_member","nickname":"Fixture"}})
        self.reply(404,{"code":404,"message":"Optional fixture endpoint unavailable"})
    def do_POST(self):
        body=json.loads(self.rfile.read(int(self.headers.get("Content-Length","0"))) or b"{}")
        if self.path=="/api/v1/login":
            switched=body["identifier"]=="switched_member"
            if not switched:type(self).case=body["identifier"]
            return self.reply(200,{"code":0,"data":{"access_token":"switched-member-token" if switched else "expired-fixture-token",
                "refresh_token":"a"*64,"id":2 if switched else 1,"token_type":"Bearer","expires_in":900}})
        if self.path=="/api/v1/token/refresh":
            name=type(self).case;self.renewals[name]+=1
            assert body["refresh_token"]=="a"*64
            if name!="account-renew":time.sleep(.25)
            return self.reply(200,{"code":0,"data":{"access_token":"fresh-fixture-token","refresh_token":"b"*64,
                "id":1,"token_type":"Bearer","expires_in":900}})
        if self.path=="/api/v1/logout":return self.reply(200,{"code":0,"data":None})
        if self.path=="/api/v1/ai/chat/completions":
            name=body["messages"][0]["content"];self.calls[name]+=1
            if name=="account-business":
                return self.reply(401,{"error":{"code":"upstream_configuration_error","message":"Fixture provider unavailable"}})
            token=self.headers.get("Authorization","")
            assert token!="Bearer switched-member-token","A cancelled request must not use the new account"
            if token=="Bearer expired-fixture-token":return self.reply(401,{"error":{"code":"invalid_token","message":"Fixture access expired"}})
            assert token=="Bearer fresh-fixture-token"
            return self.reply(200,{"id":"fixture","choices":[{"message":{"role":"assistant","content":"renewal completed"},"finish_reason":"stop"}],
                "usage":{"prompt_tokens":1,"completion_tokens":1,"total_tokens":2}})
        self.reply(404,{"code":404})

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host",type=Path,default=ROOT/".build/host/xs.exe")
    parser.add_argument("--record",type=Path)
    args=parser.parse_args()
    Identity.calls=Counter();Identity.renewals=Counter()
    server=ThreadingHTTPServer(("127.0.0.1",0),Identity)
    worker=threading.Thread(target=server.serve_forever,daemon=True);worker.start()
    try:
        with tempfile.TemporaryDirectory(prefix="online-auth-account-",dir=ROOT/".build") as raw:
            base=Path(raw);site=base/"site";write_site(site)
            for relative in ("src/account/client.c","src/account/credential.c","src/account/authorization.c","src/account/session.c"):
                copy_app_source(relative,site)
            origin=f"http://127.0.0.1:{server.server_port}"
            (site/"probe.c").write_text("#define MDO_ACCOUNT_SERVICE_ORIGIN "+json.dumps(origin)+"\n"+PROBE,encoding="utf-8")
            output=run_probe(args.host.resolve(),site,base/"home")
        print(output[output.find("case="):output.find("probe_done=")])
        assert "probe_done=1" in output and not any(key in output for key in ("init_failed","login_failed","switch_failed","logout_failed")),output
        expected={"account-renew":(0,"unknown",2),"account-deadline":(-2,"timeout",1),
            "account-logout":(-3,"cancelled",1),"account-switch":(-3,"cancelled",1),
            "account-business":(-1,"service_configuration",1)}
        for name,(result,kind,count) in expected.items():
            line=next(line for line in output.splitlines() if line.startswith(f"case={name} "))
            fields=dict(part.split("=",1) for part in line.split())
            assert (int(fields["result"]),fields["kind"],Identity.calls[name])==(result,kind,count),(line,Identity.calls)
            assert int(fields["attempts"])==count and fields["acquirers"]=="0",line
            if name=="account-deadline":assert 80<=int(fields["ms"])<200,line
            assert Identity.renewals[name]==(0 if name=="account-business" else 1),(name,Identity.renewals)
        record={"passed":True,"model_requests":dict(Identity.calls),"token_refreshes":dict(Identity.renewals),
            "output":output[output.find("case="):output.find("probe_done=")],"scope":"real account worker, controlled identity, no xadmin or public data"}
        if args.record:
            args.record.parent.mkdir(parents=True,exist_ok=True);args.record.write_text(json.dumps(record,indent=2)+"\n")
        print("Account renewal, caller deadline, logout and account-switch fences: PASS")
    finally:
        server.shutdown();server.server_close();worker.join(timeout=3)

if __name__=="__main__":main()
