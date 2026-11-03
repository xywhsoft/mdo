"""Real Agent loops and delegated children, with no network/model requests.

Compiles current product source in place using the existing host. Only this
test's tiny Home, workspace, generated probe and config are written.
"""
import argparse
import json
import os
from pathlib import Path
import socket
import shutil
import subprocess
import tempfile
import time
from test_conversation_groups_runtime import prepare_history_home

ROOT = Path(__file__).resolve().parents[1]
PROBE = r'''
#define ServiceInit MdoProductServiceInit
#include "generated/mdo_unity.c"
#undef ServiceInit
static unsigned Failed;
#define CHECK(X) do{if(!(X)){++Failed;printf("FAIL line %u: %s\n",__LINE__,#X);fflush(stdout);}}while(0)
typedef struct TurnTest {unsigned Root,Child,Depth,Target;bool Delegate,ChildDone,ChildSuccess;} TurnTest;
static bool Observe(void* Data,const xwork_event* Event){
    if(Event->eKind==XWORK_EVENT_MODEL_START)((TurnTest*)Data)->Depth=Event->uAgentDepth;
    if(Event->eKind==XWORK_EVENT_AGENT_DONE&&Event->uAgentDepth){
        ((TurnTest*)Data)->ChildDone=true;((TurnTest*)Data)->ChildSuccess=Event->bSuccess;
    }
    return true;
}
static xllm_result Model(void* Data,const xllm_request* Request,const xllm_stream_callbacks* Callbacks,xllm_response** Out,xllm_error* Error){
    (void)Request;(void)Callbacks;TurnTest* S=Data;unsigned Call=S->Depth?++S->Child:++S->Root;
    bool Delegate=S->Delegate&&!S->Depth&&Call==1;
    bool Read=(S->Depth||!S->Delegate)&&Call<S->Target;
    xllm_response* R=calloc(1,sizeof(*R));if(!R)return XLLM_RESULT_ERROR;
    R->sContent=strdup(Delegate||Read?"":"finished");R->sModel=strdup("fixture");R->uHttpStatus=200;
    R->sFinishReason=strdup(Delegate||Read?"tool_calls":"stop");
    R->eFinish=Delegate||Read?XLLM_FINISH_TOOL_CALLS:XLLM_FINISH_STOP;
    if(Delegate||Read){
        R->iToolCallCount=1;R->pToolCalls=calloc(1,sizeof(*R->pToolCalls));
        char Id[32],Arguments[256];snprintf(Id,sizeof(Id),"call-%u-%u",S->Depth,Call);
        snprintf(Arguments,sizeof(Arguments),"{\"path\":\"marker.txt\",\"max_bytes\":%u}",512+Call);
        R->pToolCalls[0].sId=strdup(Id);R->pToolCalls[0].sName=strdup(Delegate?"agent":"read");
        R->pToolCalls[0].sArgumentsJson=strdup(Delegate?"{\"name\":\"subagent.general\",\"prompt\":\"Complete the offline child fixture\",\"background\":false}":Arguments);
    }
    *Out=R;xllmErrorInit(Error);return XLLM_RESULT_OK;
}
static void Run(unsigned Global,unsigned Target,bool Delegate,const char* Role,unsigned ExpectedRoot,unsigned ExpectedChild,xwork_result Expected){
    char Document[256];snprintf(Document,sizeof(Document),"{\"schema_version\":1,\"patch\":{\"agent\":{\"max_turns\":%u,\"memory\":false,\"web_search\":false}}}",Global);
    CHECK(MdoConfigImport(MDO_CONFIG_SETTINGS,xrtStrView(Document)));
    MdoConfigAgentSettings Settings={0};Settings.Size=sizeof(Settings);
    CHECK(MdoConfigGetAgentSettings(&Settings)&&Settings.MaxTurns==Global);
    MdoAgentSessionOptions Options;MdoAgentSessionOptionsInit(&Options);
    char* Workspace=xrtEnvGet("MDO_TURN_TEST_WORKSPACE");
    Options.WorkspaceRoot=Workspace;Options.AgentId=Role;Options.ModelId="history-fixture";
    Options.ProjectId="default";Options.PermissionProfile="full-access";
    TurnTest S={0};S.Target=Target;S.Delegate=Delegate;
    Options.OnModelComplete=Model;Options.ModelUserData=&S;Options.OnEvent=Observe;Options.EventUserData=&S;
    xwork_error E;MdoAgentSession* Session=MdoAgentSessionCreateWithRuntime(MdoBootstrapRuntime(),&Options,&E);
    CHECK(Session!=NULL);if(!Session){printf("session_error=%s\n",E.sMessage);xrtFree(Workspace);return;}
    if(Delegate){
        xwork_subagent_catalog* Catalog=xworkAgentSubagentCatalogSnapshot(Session->Agent);
        bool Found=false;
        for(size_t I=0;I<xworkSubagentCatalogCount(Catalog);I++){
            xwork_subagent_definition_info Info;xworkSubagentDefinitionInfoInit(&Info);
            if(xworkSubagentCatalogDefinitionAt(Catalog,I,&Info)&&!strcmp(Info.sName,"subagent.general")){
                Found=true;CHECK(Info.uMaxTurns==Global);
            }
        }
        CHECK(Found);xworkSubagentCatalogRelease(Catalog);
    }
    MdoAgentRunOptions RunOptions;MdoAgentRunOptionsInit(&RunOptions);RunOptions.Prompt="Complete the offline turn fixture";
    MdoAgentRun* Active=MdoAgentRunCreate(Session,&RunOptions,&E);
    CHECK(Active&&MdoAgentRunStart(Active,&E));
    xwork_run_result Result={0};xwork_result Status=MdoAgentRunWait(Active,MdoDeadlineAfterUs(60000000u),&Result,&E);
    printf("CASE global=%u target=%u delegate=%u role=%s status=%u root=%u child=%u error=%s\n",Global,Target,Delegate,Role,(unsigned)Status,S.Root,S.Child,E.sMessage);fflush(stdout);
    CHECK(Status==Expected);CHECK(S.Root==ExpectedRoot);CHECK(S.Child==ExpectedChild);
    if(Delegate){
        bool Completes=!Global||Target<=Global;
        if(Completes)CHECK(S.ChildDone&&S.ChildSuccess);
        else CHECK(!S.ChildSuccess); /* A limited child can end with ERROR rather than AGENT_DONE. */
    }
    if(Expected==XWORK_RESULT_LIMIT)CHECK(E.eCode==XWORK_ERROR_LOOP_GUARD&&strstr(E.sMessage,"agent-turn limit"));
    xworkRunResultUnit(&Result);MdoAgentRunDestroy(Active);MdoAgentSessionRelease(Session);xrtFree(Workspace);
}
void ServiceInit(XS_HostInfo* Host){
    CHECK(MdoBootstrapInit(Host));if(!MdoBootstrapRuntime()){printf("turn_failed=%u\nprobe_done=1\n",Failed);fflush(stdout);return;}
    MdoConfigAgentSettings Settings={0};Settings.Size=sizeof(Settings);
    CHECK(MdoConfigGetAgentSettings(&Settings)&&Settings.MaxTurns==0);
    CHECK(MdoAgentEffectiveTurnLimit(0,0)==0&&MdoAgentEffectiveTurnLimit(0,3)==3&&MdoAgentEffectiveTurnLimit(2,0)==2&&MdoAgentEffectiveTurnLimit(7,3)==3);
    MdoAgentFile File;char Error[1024]={0};
    CHECK(MdoAgentFileParse("unlimited","{\"schema\":\"mdo.agent.v1\",\"name\":\"Unlimited\",\"prompt\":\"Do the task\",\"runtime\":{\"max_turns\":0}}",true,&File,Error,sizeof(Error)));
    CHECK(File.Agent.MaxTurns==0);MdoAgentFileUnit(&File);
    Run(0,130,false,"mdo.default",130,0,XWORK_RESULT_OK);
    Run(2,4,false,"mdo.default",2,0,XWORK_RESULT_LIMIT);
    Run(0,130,true,"mdo.default",2,130,XWORK_RESULT_OK);
    Run(2,4,true,"mdo.default",2,2,XWORK_RESULT_OK);
    Run(0,5,false,"agent.limited",3,0,XWORK_RESULT_LIMIT);
    printf("turn_failed=%u\nprobe_done=1\n",Failed);fflush(stdout);
}
'''


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',type=Path,default=ROOT/'.build/host/xs.exe')
    args=parser.parse_args()
    evidence=ROOT/'.build/agent-turn-limit-20261102';evidence.mkdir(parents=True,exist_ok=True)
    process=None
    with tempfile.TemporaryDirectory(prefix='agent-turn-limit-',dir=ROOT/'.build') as raw:
        base=Path(raw);home=base/'home';prepare_history_home(home)
        site=base/'site';shutil.copytree(ROOT/'app',site)
        probe=site/'generated/agent_turn_limit_test.c';config=site/'xs.agent-turn-limit-test.json'
        # Simulate an older Home whose saved settings predate max_turns.
        (home/'config/settings.json').write_text(json.dumps({'schema_version':1,'patch':{'agent':{'memory':False}}}),encoding='utf-8')
        role=home/'agents/limited';role.mkdir(parents=True)
        (role/'extension.json').write_text(json.dumps(dict(kind='agents',id='agent:limited',alias='limited',name='Limited',entry='agent.md',platforms=['any'])),encoding='utf-8')
        (role/'agent.md').write_text('---\nname: Limited\nmode: primary\ntools: [read]\nruntime: {"max_turns":3}\n---\nComplete the offline task.',encoding='utf-8')
        workspace=base/'workspace';workspace.mkdir();(workspace/'marker.txt').write_text('offline read fixture',encoding='utf-8')
        with socket.socket() as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
        probe.write_text(PROBE,encoding='utf-8')
        config.write_text(json.dumps({'engine':{'workers':1},'services':[{'enabled':True,'class':'http','name':'turn-limit','ip':'127.0.0.1','port':port,
            'host_default':{'enabled':True,'path':'web','devlang':'c','devfile':'generated/agent_turn_limit_test.c'}}]}),encoding='utf-8')
        try:
            log=evidence/'runtime.log'
            with log.open('wb') as out:
                process=subprocess.Popen([str(args.host.resolve()),str(config)],cwd=site,stdout=out,stderr=out,
                    env={**os.environ,'MDO_HOME':str(home),'USERPROFILE':str(base),'MDO_TURN_TEST_WORKSPACE':str(workspace)},creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            deadline=time.monotonic()+120
            while process.poll() is None and time.monotonic()<deadline:
                if 'probe_done=1' in log.read_text(encoding='utf-8',errors='replace'):break
                time.sleep(.1)
            output=log.read_text(encoding='utf-8',errors='replace')
            assert 'turn_failed=0' in output and 'probe_done=1' in output,output[-9000:]
            print('\n'.join(line for line in output.splitlines() if line.startswith(('CASE ','turn_failed='))))
        finally:
            if process and process.poll() is None:process.terminate();process.wait(timeout=8)
            probe.unlink(missing_ok=True);config.unlink(missing_ok=True)
    print('PASS legacy settings default, 130-turn main and child, finite global/role ceilings, and SDK zero preservation')


if __name__=='__main__':main()
