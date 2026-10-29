"""Native retry -> Agent sink -> journal -> API contract, in the project itself.

Uses the existing xs host and a bounded fake model attempt. No app copy,
external model, account, SDK change, or mutation of the normal app config.
"""
import argparse
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = Path(__file__).resolve().parents[1]
PROBE = r'''
#define MDO_MODEL_RETRY_BASE_MS 20u
#define MDO_MODEL_RETRY_MAX_MS 80u
#define ServiceInit MdoProductServiceInit
#include "generated/mdo_unity.c"
#undef ServiceInit
typedef struct Seen {
    unsigned Waiting, Connecting, Replayed, Api, Attempts, Hooks;
    bool Bad, CancelOnRetry;
    uint64 Agent, Run;
    xcancel* Cancel;
    xllm_response Response;
} Seen;
static bool Collect(void* Data,const xwork_event* Event) {
    Seen* S=Data;
    if(Event->eKind!=MDO_AGENT_EVENT_MODEL_RETRY)return true;
    S->Bad|=Event->uAgentId!=S->Agent||Event->uRunId!=S->Run||Event->uAgentTurn!=7;
    if(Event->tDiagnostics.uRetryAfterMs)++S->Waiting;else ++S->Connecting;
    MdoSessionEventBridge Bridge={0};
    strcpy(Bridge.ProjectId,"default");strcpy(Bridge.SessionId,"0123456789abcdef0123456789abcdef");
    size_t Size=0;char* Json=MdoEventsRecord(&Bridge,1,Event,"fixture",1000,NULL,&Size);
    MdoSessionEventOwned Parsed={0};
    bool Good=Json&&MdoEventsParse(Bridge.ProjectId,Bridge.SessionId,xrtStrViewN(Json,Size),&Parsed);
    S->Bad|=!Good||strstr(Json?Json:"","private-provider-secret")!=NULL;
    if(Good){
        ++S->Replayed;
        S->Bad|=Parsed.Info.ModelHttpStatus!=429||strcmp(Parsed.Info.ModelErrorKind,"rate_limited")||
            Parsed.Info.ModelRetryAfterMs!=Event->tDiagnostics.uRetryAfterMs||
            Parsed.Info.ModelAttempts!=Event->tDiagnostics.uAttemptCount;
        xvalue* Api=NULL;
        if(MdoApiSessionEventValueLimit(&Parsed.Info,Bridge.ProjectId,Bridge.SessionId,4096,&Api)){
            xstrview Kind={0};xrtValueGetString(xrtValueObjectGet(Api,XRT_STR_LITERAL("kind")),&Kind);
            if(Kind.Size==11&&!memcmp(Kind.Data,"model_retry",11))++S->Api;
        }
        xrtValueRelease(Api);
    }
    MdoEventsOwnedUnit(&Parsed);
    if(Json){
        xvalue* Root=xrtJsonParse(xrtStrViewN(Json,Size));xrtFree(Json);
        xrtValueObjectRemove(Root,XRT_STR_LITERAL("model_retry_after_ms"));
        Json=xrtJsonStringify(Root,false,&Size);
        S->Bad|=MdoEventsParse(Bridge.ProjectId,Bridge.SessionId,xrtStrViewN(Json,Size),&Parsed);
        MdoEventsOwnedUnit(&Parsed);xrtFree(Json);xrtValueRelease(Root);
    }
    if(S->CancelOnRetry&&S->Cancel)xrtCancelRequest(S->Cancel);
    return true;
}
static bool RetryHook(xllm_client* C,const xllm_diagnostics* D,uint32 Next,void* Data){
    (void)C;Seen* S=Data;++S->Hooks;return Next==D->uAttemptCount+1;
}
static xllm_result Attempt(void* Data,xllm_client** Client,const xllm_request* Request,
    const xllm_stream_callbacks* Callbacks,xllm_response** Response,xllm_error* Error){
    (void)Client;(void)Request;(void)Callbacks;Seen* S=Data;++S->Attempts;xllmErrorInit(Error);
    if(S->Attempts<3){Error->eCode=XLLM_ERROR_RATE_LIMIT;Error->iHttpStatus=429;
        strcpy(Error->sMessage,"private-provider-secret");return XLLM_RESULT_ERROR;}
    S->Response.sContent="OK";*Response=&S->Response;return XLLM_RESULT_OK;
}
static bool Access(xcancel* Cancel,uint64 Deadline,MdoModelOnlineAccess* Out){
    (void)Deadline;Out->Token="fixture-only-key";Out->Cancel=Cancel;return true;
}
static void ReleaseAccess(MdoModelOnlineAccess* Out,int Status){(void)Out;(void)Status;}
void ServiceInit(XS_HostInfo* Host){
    (void)Host;MdoAgentOwner Owner={0};Owner.RouteLock=xrtMutexCreate();
    Seen S={0};S.Agent=11;S.Run=21;Owner.ExternalEvent=Collect;Owner.ExternalEventData=&S;
    xwork_event Start={0};Start.eKind=XWORK_EVENT_MODEL_START;Start.uAgentId=11;
    Start.uRunId=21;Start.uAgentTurn=7;
    MdoAgentsProgressOwnerEvent(&Owner,&Start);
    MdoAgentModelProgress Progress={0};MdoAgentsProgressSnapshot(&Owner,&Progress);
    MdoModelRetryObserver Observer={MdoAgentsRetryProgress,&Progress};
    xllm_request Request;xllmRequestInit(&Request);xllm_error Error;xllm_response* Response=NULL;
    xllm_hooks Hooks={0};Hooks.pOnRetry=RetryHook;Hooks.pUserData=&S;Request.pHooks=&Hooks;
    xllm_result Result=MdoModelsComplete(NULL,&Request,NULL,&Response,&Error,true,Attempt,&S,&Observer);
    printf("recovery=%u attempts=%u waiting=%u connecting=%u replay=%u api=%u hooks=%u bad=%u\n",
        Result==XLLM_RESULT_OK,S.Attempts,S.Waiting,S.Connecting,S.Replayed,S.Api,S.Hooks,S.Bad);
    /* A second run's sink must never receive the first run's status. */
    MdoAgentSession Session={0};Session.Owner=&Owner;
    Seen Other={0};Other.Agent=12;Other.Run=22;
    MdoAgentRun Run={0};Run.Session=&Session;Run.ExternalEvent=Collect;Run.ExternalEventData=&Other;
    Start.uAgentId=12;Start.uRunId=22;MdoAgentsProgressRunEvent(&Run,&Start);
    MdoAgentModelProgress OtherProgress={0};MdoAgentsProgressSnapshot(&Owner,&OtherProgress);
    xllmErrorInit(&Error);Error.eCode=XLLM_ERROR_RATE_LIMIT;Error.iHttpStatus=429;
    MdoAgentsRetryProgress(&OtherProgress,&Error,2,20);
    printf("isolation=%u\n",Other.Waiting==1&&!Other.Bad&&S.Waiting==2);
    Start.eKind=XWORK_EVENT_MODEL_DONE;MdoAgentsProgressRunEvent(&Run,&Start);
    MdoAgentsProgressSnapshot(&Owner,&OtherProgress);
    printf("nested_restore=%u\n",OtherProgress.Identity.uAgentId==11);
    /* Stop during the waiting notice must not announce or run a next attempt. */
    S.Attempts=S.Waiting=S.Connecting=S.Replayed=S.Api=S.Hooks=0;
    S.Cancel=xrtCancelCreate();S.CancelOnRetry=true;Request.pCancel=S.Cancel;
    Result=MdoModelsComplete(NULL,&Request,NULL,&Response,&Error,true,Attempt,&S,&Observer);
    printf("cancel=%u attempts=%u waiting=%u connecting=%u\n",
        Result==XLLM_RESULT_CANCELLED,S.Attempts,S.Waiting,S.Connecting);
    xrtCancelDestroy(S.Cancel);Request.pCancel=NULL;
    /* Ordinary schema-8 journals are still readable after adding progress. */
    MdoSessionEventBridge Bridge={0};strcpy(Bridge.ProjectId,"default");strcpy(Bridge.SessionId,"0123456789abcdef0123456789abcdef");
    xwork_event Plain={0};Plain.eKind=XWORK_EVENT_MODEL_START;size_t Size=0;
    char* Json=MdoEventsRecord(&Bridge,1,&Plain,"fixture",1000,NULL,&Size);
    xvalue* Root=xrtJsonParse(xrtStrViewN(Json,Size));xrtFree(Json);
    xrtValueObjectSetNew(Root,XRT_STR_LITERAL("schema_version"),xrtValueUInt(8));
    Json=xrtJsonStringify(Root,false,&Size);MdoSessionEventOwned Parsed={0};
    printf("schema8=%u\n",MdoEventsParse(Bridge.ProjectId,Bridge.SessionId,xrtStrViewN(Json,Size),&Parsed));
    MdoEventsOwnedUnit(&Parsed);xrtFree(Json);xrtValueRelease(Root);
    /* Exercise the actual configured and online Agent route adapters over HTTP. */
    bool Ready=MdoHomeInit()&&MdoConfigInit()&&MdoModelManagerInit();
    Owner.Models=Ready?MdoModelCatalogSnapshot():NULL;
    MdoAgentRoute Route={0};Route.ModelId=MDO_BUILTIN_MODEL_ID;
    Route.Protocol=MDO_MODEL_PROTOCOL_OPENAI_CHAT_COMPLETIONS;Route.MaxOutputTokens=128;
    for(unsigned Online=0;Online<2&&Owner.Models;Online++){
        S.Attempts=S.Waiting=S.Connecting=S.Replayed=S.Api=S.Hooks=0;S.CancelOnRetry=false;
        xllmRequestUnit(&Request);xllmRequestInit(&Request);
        xllmRequestAddTextMessage(&Request,XLLM_ROLE_USER,Online?"online":"configured");
        if(Online){
            char* Origin=xrtEnvGet("MDO_RETRY_TEST_ORIGIN");
            MdoModelOnlineAuthority Authority={0};Authority.Origin=Origin;
            Authority.Acquire=Access;Authority.Release=ReleaseAccess;
            MdoModelManagerSetOnlineAuthority(&Authority);
        }
        Response=NULL;
        Result=MdoAgentsRouteComplete(&Owner,&Route,&Request,NULL,&Response,&Error);
        printf("http_%s=%u waiting=%u connecting=%u replay=%u api=%u bad=%u\n",
            Online?"online":"configured",Result==XLLM_RESULT_OK,S.Waiting,S.Connecting,S.Replayed,S.Api,S.Bad);
        if(Result!=XLLM_RESULT_OK)printf("http_error=%s kind=%s\n",Error.sMessage,MdoModelErrorKind(&Error));
        xllmResponseDestroy(Response);
    }
    MdoModelManagerSetOnlineAuthority(NULL);xllmClientDestroy(Route.Client);
    MdoModelCatalogRelease(Owner.Models);if(Ready){MdoModelManagerUnit();MdoConfigUnit();MdoHomeUnit();}
    MdoAgentsProgressUnit(&Owner);xrtMutexDestroy(Owner.RouteLock);xllmRequestUnit(&Request);
    printf("probe_done=1\n");fflush(stdout);
}
'''


class Model(BaseHTTPRequestHandler):
    calls = {}
    def log_message(self, *_): pass
    def do_POST(self):
        body=json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        name=body['messages'][-1]['content']
        self.calls[name]=self.calls.get(name,0)+1
        if self.calls[name]<3:
            data=json.dumps({'error':{'code':'rate_limit_exceeded','message':'fixture rate limit'}}).encode()
            status,media=429,'application/json'
        else:
            chunks=[{'id':'progress-fixture','model':'ornith-1.5-35b','choices':[{'index':0,'delta':{'content':'OK'},'finish_reason':None}]},
                {'id':'progress-fixture','choices':[{'index':0,'delta':{},'finish_reason':'stop'}],'usage':{'prompt_tokens':3,'completion_tokens':1,'total_tokens':4}}]
            data=(''.join('data: '+json.dumps(x)+'\n\n' for x in chunks)+'data: [DONE]\n\n').encode()
            status,media=200,'text/event-stream'
        self.send_response(status);self.send_header('Content-Type',media)
        self.send_header('Content-Length',str(len(data)));self.send_header('Connection','close')
        self.end_headers();self.wfile.write(data);self.close_connection=True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=Path, default=ROOT / '.build/host/xs.exe')
    args = parser.parse_args()
    from tools import build_mdo
    build_mdo.write_if_changed(build_mdo.UNITY_PATH, build_mdo.generated_unity(
        build_mdo.load_object(build_mdo.LOCK_PATH), build_mdo.source_list()).encode())
    probe = ROOT / 'app/generated/model_retry_progress_test.c'
    config = ROOT / 'app/xs.model-retry-progress-test.json'
    assert not probe.exists() and not config.exists(), 'An owned fixture is already running'
    with tempfile.TemporaryDirectory(prefix='model-retry-progress-', dir=ROOT / '.build') as raw:
        base = Path(raw)
        with socket.socket() as sock:
            sock.bind(('127.0.0.1', 0)); port = sock.getsockname()[1]
        probe.write_text(PROBE, encoding='utf-8')
        config.write_text(json.dumps({'services': [{'enabled': True, 'class': 'http',
            'ip': '127.0.0.1', 'port': port, 'name': 'retry-progress',
            'host_default': {'enabled': True, 'name': 'mdo', 'path': 'web',
                'devlang': 'c', 'devfile': 'generated/model_retry_progress_test.c'}}]}))
        proc = None
        Model.calls={};server=ThreadingHTTPServer(('127.0.0.1',0),Model)
        worker=threading.Thread(target=server.serve_forever,daemon=True);worker.start()
        origin=f'http://127.0.0.1:{server.server_port}'
        try:
            log = base / 'runtime.log'
            with log.open('wb') as output:
                proc = subprocess.Popen([str(args.host.resolve()), str(config)], cwd=ROOT / 'app',
                    stdout=output, stderr=output, env={**os.environ, 'MDO_HOME': str(base / 'home'),
                        'MDO_ORNITH_API_KEY':'fixture-only-key','MDO_RETRY_TEST_ORIGIN':origin,
                        'MDO_ORNITH_CHAT_COMPLETIONS_URL':origin+'/v1/chat/completions'},
                    creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
                import time
                until = time.monotonic() + 35
                while time.monotonic() < until and proc.poll() is None:
                    if 'probe_done=1' in log.read_text(errors='replace'): break
                    time.sleep(.1)
            text = log.read_text(errors='replace')
            for expected in ['recovery=1 attempts=3 waiting=2 connecting=2 replay=4 api=4 hooks=2 bad=0',
                             'isolation=1', 'nested_restore=1', 'cancel=1 attempts=1 waiting=1 connecting=0', 'schema8=1',
                             'http_configured=1 waiting=2 connecting=2 replay=4 api=4 bad=0',
                             'http_online=1 waiting=2 connecting=2 replay=4 api=4 bad=0', 'probe_done=1']:
                assert expected in text, text[-16000:]
            assert Model.calls=={'configured':3,'online':3},Model.calls
            print('Native retry progress, sink isolation, journal/API replay, cancellation and legacy compatibility: PASS')
        finally:
            if proc and proc.poll() is None:
                if os.name == 'nt': subprocess.run(['taskkill', '/PID', str(proc.pid), '/T', '/F'], capture_output=True)
                else: proc.terminate()
                proc.wait(timeout=8)
            server.shutdown();server.server_close();worker.join(timeout=3)
            probe.unlink(missing_ok=True); config.unlink(missing_ok=True)


if __name__ == '__main__':
    import sys
    sys.path.insert(0, str(ROOT))
    main()
