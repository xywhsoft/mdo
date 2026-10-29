"""Execute configurable sources, history projection and model-call limits offline."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import tempfile
import test_agent_runtime as fixture

ROLE = {
    "schema": "mdo.agent.v1", "name": "Context fixture", "prompt": "CONFIG-ROLE",
    "tools": ["read"], "runtime": {"max_model_calls": 4},
    "model_options": {"temperature": 0.4, "top_p": 0.8, "parallel_tool_calls": False},
    "history": {"mode": "current", "include_summary": False, "tool_result_max_bytes": 8},
    "context": {"memory.global": False, "memory.project": False, "knowledge": False,
                "skills.discovery": False, "external_tools": False, "user_instructions": False,
                "custom.once": {"file": "once.txt", "refresh": "run"},
                "custom.live": {"file": "live.txt", "refresh": "request", "role": "user"},
                "custom.disabled": {"enabled": False, "required": True, "file": "missing.txt", "template": "{{missing}}"}},
}
PROBE = r'''
static unsigned BudgetCalls;
static xllm_result BudgetComplete(void* Data,const xllm_request* Request,const xllm_stream_callbacks* Callbacks,xllm_response** Out,xllm_error* Error){
    (void)Data;(void)Request;(void)Callbacks;(void)Error;++BudgetCalls;
    if(BudgetCalls>1){*Out=Response("budget-next-run-complete");return XLLM_RESULT_OK;}
    xllm_response* R=Response("");R->pToolCalls=calloc(1,sizeof(*R->pToolCalls));R->iToolCallCount=1;
    R->pToolCalls[0].sId=strdup("budget-read");R->pToolCalls[0].sName=strdup("read");R->pToolCalls[0].sArgumentsJson=strdup("{\"path\":\"context-data.txt\"}");R->eFinish=XLLM_FINISH_TOOL_CALLS;*Out=R;return XLLM_RESULT_OK;
}
static unsigned ConfigCalls;static bool ConfigProjection=true,ConfigRefresh=true;
static xllm_result ConfigComplete(void* Data,const xllm_request* Request,const xllm_stream_callbacks* Callbacks,xllm_response** Out,xllm_error* Error){
    (void)Data;(void)Callbacks;(void)Error;++ConfigCalls;
    bool Once1=false,Once2=false,Live1=false,Live2=false,Old=false,Shortened=false;
    for(size_t I=0;I<Request->iMessageCount;I++){
        cstr Text=Request->pMessages[I].sContent;if(!Text)continue;
        Once1|=strstr(Text,"ONCE-V1")!=NULL;Once2|=strstr(Text,"ONCE-V2")!=NULL;
        Live1|=strstr(Text,"LIVE-V1")!=NULL;Live2|=strstr(Text,"LIVE-V2")!=NULL;
        Old|=strstr(Text,"FIRST-CONFIG-TASK")!=NULL;
        Shortened|=strstr(Text,"shortened by this role")!=NULL;
    }
    ConfigProjection &= Request->iToolCount==1 && !strcmp(Request->pTools[0].sName,"read") &&
        Request->bHasTemperature && Request->fTemperature==0.4 && Request->bHasTopP && !Request->bParallelToolCalls;
    if(ConfigCalls==1){
        ConfigRefresh &= Once1&&!Once2&&Live1&&!Live2;
        MdoHomeAtomicWrite("agents/once.txt","ONCE-V2",7,false);MdoHomeAtomicWrite("agents/live.txt","LIVE-V2",7,false);
        xllm_response* R=Response("");R->pToolCalls=calloc(1,sizeof(*R->pToolCalls));R->iToolCallCount=1;
        R->pToolCalls[0].sId=strdup("context-read");R->pToolCalls[0].sName=strdup("read");R->pToolCalls[0].sArgumentsJson=strdup("{\"path\":\"context-data.txt\"}");R->eFinish=XLLM_FINISH_TOOL_CALLS;*Out=R;
    }else{
        ConfigRefresh &= ConfigCalls==2 ? Once1&&!Once2&&!Live1&&Live2&&Shortened : !Once1&&Once2&&!Live1&&Live2&&!Old;
        *Out=Response("context-complete");
    }
    return *Out?XLLM_RESULT_OK:XLLM_RESULT_ERROR;
}
static void ConfigProbe(xwork_runtime* Runtime){
    char* Metadata=xrtStrDup("Libraries:\n[{\"name\":\"Display name\",\"path\":\"@knowledge/selected\"},{\"name\":\"Other display\",\"path\":\"@knowledge/other\"}]\n");
    xvalue* Filter=xrtJsonParse(xrtStrView("{\"include\":[\"selected\"]}"));
    printf("selection_checks=%d\n",MdoAgentsFilterMetadata(&Metadata,Filter)&&strstr(Metadata,"Display name")&&!strstr(Metadata,"Other display"));
    xrtFree(Metadata);xrtValueRelease(Filter);
    MdoAgentSessionOptions Options;MdoAgentSessionOptionsInit(&Options);xwork_error Error;
    Options.AgentId="agent.context-fixture";Options.ProjectId="default";Options.ProductSessionId="context-probe";
    Options.WorkspaceRoot=".";Options.PermissionProfile="full-access";Options.OnModelComplete=ConfigComplete;
    MdoAgentSession* Session=MdoAgentSessionCreateWithRuntime(Runtime,&Options,&Error);
    if(!Session){printf("context_error=create:%s\n",Error.sMessage);return;}
    for(unsigned I=0;I<2;I++){
        MdoAgentRunOptions RunOptions;MdoAgentRunOptionsInit(&RunOptions);RunOptions.Prompt=I?"SECOND-CONFIG-TASK":"FIRST-CONFIG-TASK";
        MdoAgentRun* Run=MdoAgentRunCreate(Session,&RunOptions,&Error);xwork_run_result Result={0};
        xwork_result Status=Run&&MdoAgentRunStart(Run,&Error)?MdoAgentRunWait(Run,MdoDeadlineAfterUs(5000000),&Result,&Error):XWORK_RESULT_ERROR;
        if(Status!=XWORK_RESULT_OK)printf("context_error=run:%s\n",Error.sMessage);
        xworkRunResultUnit(&Result);MdoAgentRunDestroy(Run);
    }
    printf("context_checks=calls:%u projection:%d refresh:%d\n",ConfigCalls,ConfigProjection,ConfigRefresh);
    MdoAgentSessionRelease(Session);
    Options.AgentId="agent.budget-fixture";Options.OnModelComplete=BudgetComplete;
    Session=MdoAgentSessionCreateWithRuntime(Runtime,&Options,&Error);
    if(!Session){printf("context_error=budget-create:%s\n",Error.sMessage);return;}
    bool BudgetOk=true;
    for(unsigned I=0;I<2;I++){
        MdoAgentRunOptions RunOptions;MdoAgentRunOptionsInit(&RunOptions);RunOptions.Prompt="Bounded request";RunOptions.Resume=I!=0;
        MdoAgentRun* Run=MdoAgentRunCreate(Session,&RunOptions,&Error);xwork_run_result Result={0};
        xwork_result Status=Run&&MdoAgentRunStart(Run,&Error)?MdoAgentRunWait(Run,MdoDeadlineAfterUs(5000000),&Result,&Error):XWORK_RESULT_ERROR;
        BudgetOk &= I?Status==XWORK_RESULT_OK:Status==XWORK_RESULT_LIMIT&&strstr(Error.sMessage,"model-call budget")!=NULL;
        xworkRunResultUnit(&Result);MdoAgentRunDestroy(Run);
    }
    printf("budget_checks=%d calls:%u\n",BudgetOk,BudgetCalls);MdoAgentSessionRelease(Session);
    Options.AgentId="agent.oversize-fixture";
    Session=MdoAgentSessionCreateWithRuntime(Runtime,&Options,&Error);
    if(!Session){printf("context_error=oversize-create:%s\n",Error.sMessage);return;}
    MdoAgentRunOptions BigOptions;MdoAgentRunOptionsInit(&BigOptions);BigOptions.Prompt="Bounded context";
    MdoAgentRun* Big=MdoAgentRunCreate(Session,&BigOptions,&Error);xwork_run_result BigResult={0};
    xwork_result BigStatus=Big&&MdoAgentRunStart(Big,&Error)?MdoAgentRunWait(Big,MdoDeadlineAfterUs(5000000),&BigResult,&Error):XWORK_RESULT_ERROR;
    printf("oversize_checks=%d calls:%u\n",BigStatus!=XWORK_RESULT_OK&&BudgetCalls==2,BudgetCalls);
    xworkRunResultUnit(&BigResult);MdoAgentRunDestroy(Big);MdoAgentSessionRelease(Session);
    Options.AgentId="agent.permission-fixture";Options.PermissionProfile="full-access";
    Session=MdoAgentSessionCreateWithRuntime(Runtime,&Options,&Error);MdoAgentSessionInfo Info={0};Info.Size=sizeof(Info);
    bool Restricted=Session && MdoAgentSessionGetInfo(Session,&Info) && !strcmp(Info.PermissionProfile,"read-only") && Info.ToolCount==1;
    MdoAgentSessionRelease(Session);Options.AgentId="agent.global-permission-fixture";Options.PermissionProfile="read-only";
    Session=MdoAgentSessionCreateWithRuntime(Runtime,&Options,&Error);Info.Size=sizeof(Info);
    Restricted &= Session && MdoAgentSessionGetInfo(Session,&Info) && !strcmp(Info.PermissionProfile,"read-only") && Info.ToolCount==1;
    printf("permission_checks=%d\n",Restricted);MdoAgentSessionRelease(Session);
}
'''

def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',type=Path,default=fixture.ROOT/'.build/host/xs.exe')
    host=parser.parse_args().host.resolve()
    with tempfile.TemporaryDirectory(prefix="agent-context-", dir=fixture.ROOT / ".build") as raw:
        site = Path(raw) / "site"
        fixture.write_site(site)
        (site / "context-data.txt").write_text("A meaningful tool result that should be projected and retained in full on disk.", encoding="utf-8")
        source = (site / "probe.c").read_text(encoding="utf-8")
        source = source.replace("void ServiceInit(", PROBE + "\nvoid ServiceInit(")
        budget = {**ROLE, "runtime": {"max_model_calls": 1}}
        big = {**ROLE, "tools": [], "runtime": {"context_tokens": 8192, "output_tokens": 1024},
               "context": {**ROLE["context"], "custom.large": {"role": "user", "text": "large role reference " * 10000}}}
        # The configured reference lives in a role-local file, within the same
        # limits as real resources, so its body is not an oversized definition.
        large = big['context']['custom.large'].pop('text')
        big['context']['custom.large']['file'] = 'large.txt'
        large = large[:100000]
        files = [("agents/context-fixture.md", json.dumps(ROLE)), ("agents/budget-fixture.md", json.dumps(budget)),
                 ("agents/oversize-fixture.md", json.dumps(big)), ("agents/large.txt", large),
                 ("agents/permission-fixture.md", json.dumps({**ROLE,"permission_profile":"read-only","tools":["read","write"]})),
                 ("agents/global-permission-fixture.md", json.dumps({**ROLE,"permission_profile":"full-access","tools":["read","write"]})),
                 ("agents/once.txt", "ONCE-V1"), ("agents/live.txt", "LIVE-V1")]
        setup = "\n".join(f"    MdoHomeAtomicWrite({json.dumps(path)}, {json.dumps(text)}, {len(text.encode())}u, false);" for path, text in files)
        source = source.replace("xworkRuntimeConfigInit(&runtime_config);", setup + "\n    xworkRuntimeConfigInit(&runtime_config);")
        source = source.replace("MdoMemoryWriteOptionsInit(&memory_write);", "ConfigProbe(runtime);\n    MdoMemoryWriteOptionsInit(&memory_write);")
        (site / "probe.c").write_text(source, encoding="utf-8")
        output = fixture.run_probe(host, site, Path(raw) / "home")
        assert "context_checks=calls:3 projection:1 refresh:1" in output and "context_error=" not in output, output
        assert "budget_checks=1 calls:2" in output, output
        assert "oversize_checks=1 calls:2" in output, output
        assert "selection_checks=1" in output, output
        assert "permission_checks=1" in output, output
    print("Agent context: per-source refresh, disabled files, model options, history/tool projection and repeated runs PASS")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
