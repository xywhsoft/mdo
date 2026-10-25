"""Run configuration/C roles and real delegated file tools with an offline model."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import shutil
import tempfile
import test_agent_runtime as fixture

MODULE = r'''
#include "mdo/agent.h"
#include <stdio.h>
static const mdo_host_services_v1* Host;
static mdo_result Event(void* Data,mdo_agent_phase Phase,const mdo_agent_context_v1* Context,char* Error,size_t Capacity){
    (void)Data;(void)Error;(void)Capacity;
    if(Phase==MDO_AGENT_CONTEXT){
        if(!xrtValueObjectSetNew(mdoAgentState(Context),xrtStrView("instance_owner"),xrtValueString(xrtStrView(Context->AgentId))))return MDO_RESULT_ERROR;
        xvalue* Source=mdoAgentContextSource(Context,"system");xstrview Text;
        if(!Source || !xrtValueGetString(xrtValueObjectGet(Source,xrtStrView("text")),&Text))return MDO_RESULT_ERROR;
        char* Next=xrtFormat("%s BINDING-V1",Text.Data);bool Ok=Next&&mdoAgentSetContextText(Context,"system",Next);xrtFree(Next);
        if(!Ok)return MDO_RESULT_ERROR;
    }
    if(Phase==MDO_AGENT_BEFORE_REQUEST){
        xstrview Owner;
        if(!xrtValueGetString(xrtValueObjectGet(mdoAgentState(Context),xrtStrView("instance_owner")),&Owner) ||
           !xrtStrEqual(Owner,xrtStrView(Context->AgentId))){snprintf(Error,Capacity,"role instance state leaked across parent/child");return MDO_RESULT_ERROR;}
        xwork_agent_extension_context* Native=mdoAgentNative(Context);
        Native->pRequest->bHasTemperature=true;Native->pRequest->fTemperature=0.25;
    }
    if(Phase==MDO_AGENT_BEFORE_TOOL){
        xwork_agent_extension_context* Native=mdoAgentNative(Context);
        if(Native->pToolEvent && !strcmp(Native->pToolEvent->sToolName,"read")){
            snprintf(Error,Capacity,"role_fixture_read_denied");return MDO_RESULT_ERROR;
        }
    }
    if(Phase==MDO_AGENT_AFTER_TOOL){
        xwork_agent_extension_context* Native=mdoAgentNative(Context);
        if(Native->pToolEvent && !strcmp(Native->pToolEvent->sToolName,"agent") &&
           !xworkToolOutputSet(Native->pToolOutput,true,"replaced delegation result"))return MDO_RESULT_ERROR;
    }
    if(Phase==MDO_AGENT_AFTER_DELEGATE){
        xwork_agent_extension_context* Native=mdoAgentNative(Context);
        if(Native->pToolEvent && Native->pToolEvent->sOutput && !strcmp(Native->pToolEvent->sOutput,"replaced delegation result"))
            Host->Core->Log(Host->Core->Context,MDO_LOG_INFO,"program-after-delegate-current=1");
    }
    if(Phase==MDO_AGENT_INSTANCE_RELEASE && Host && Host->Core)
        Host->Core->Log(Host->Core->Context,MDO_LOG_INFO,"program-instance-released");
    return MDO_RESULT_OK;
}
static mdo_result Drive(void* Data,const mdo_agent_context_v1* Context,char* Error,size_t Capacity){
    (void)Data;xwork_error NativeError;xworkErrorInit(&NativeError);
    mdo_result Result=mdoAgentRunDefault(Context,&NativeError);
    if(Result!=MDO_RESULT_OK && Capacity)snprintf(Error,Capacity,"%s",NativeError.sMessage);
    return Result;
}
static const mdo_agent_program_v1 Program={MDO_V1_HEADER(mdo_agent_program_v1),Event,Drive};
static const char* MainTools[]={"agent"};
static const char* ChildTools[]={"read","write"};
static const mdo_agent_v1 Main={.Size=sizeof(mdo_agent_v1),.AbiVersion=MDO_MODULE_ABI_VERSION,
 .Id="agent.mod-main",.Name="Mod coordinator",.Description="Delegate a bounded write.",
 .SystemPrompt="C-MAIN",.PermissionProfile="read-only",.Tools=MainTools,.ToolCount=1,
 .AllowedEffects=MDO_TOOL_EFFECT_ALL,.Flags=MDO_AGENT_MAIN|MDO_AGENT_ALLOW_DELEGATION|MDO_AGENT_READ_ONLY,
 .MaxTurns=4,.MaxDepth=3,.MaxOutputTokens=1024,.Program=&Program};
static const mdo_agent_v1 Child={.Size=sizeof(mdo_agent_v1),.AbiVersion=MDO_MODULE_ABI_VERSION,
 .Id="subagent.mod-child",.Name="Mod worker",.Description="Write the delegated result.",
 .SystemPrompt="C-CHILD",.Tools=ChildTools,.ToolCount=2,.AllowedEffects=MDO_TOOL_EFFECT_ALL,
 .Flags=MDO_AGENT_SUBAGENT,.MaxTurns=4,.MaxDepth=2,.MaxOutputTokens=4096,.Program=&Program};
static mdo_result Register(const mdo_host_services_v1* H,const mdo_registrar_v1* R,void** Data,char* Error,size_t Capacity){
    (void)Data;Host=H;mdo_result Result=R->AddAgent(R->Context,&Main,Error,Capacity);
    return Result==MDO_RESULT_OK?R->AddAgent(R->Context,&Child,Error,Capacity):Result;
}
static const mdo_module_v1 Module={MDO_V1_HEADER(mdo_module_v1),"mod.fixture","Mod fixture","Owned C role programs","1.0.0",MDO_CAPABILITY_LOG,0,0,Register,0};
MDO_EXPORT const mdo_module_v1* mdoModuleEntry(void){return &Module;}
'''

MAIN = '''---
schema: mdo.agent.v1
name: Coordinator
code: true
read_only: true
allow_delegation: true
tools: [agent]
runtime: {"context_tokens":8192,"output_tokens":1024}
delegation: {"allow":["mod-child"]}
context: {"memory.global":false,"memory.project":false,"external_tools":false,"skills.discovery":false,"knowledge":false,"custom.fixture":{"role":"user","template":"role={{agent.id}}"}}
---
'''
CHILD = '''---
schema: mdo.agent.v1
name: Worker
code: true
tools: [read,write]
runtime: {"context_tokens":16384,"input_tokens":12000,"output_tokens":4096}
context: {"memory.global":false,"memory.project":false,"external_tools":false,"skills.discovery":false,"knowledge":false}
---
'''

PROBE = r'''
static unsigned ModMainCalls,ModChildCalls;static bool ModSawHook,ModSawIndependentTools,ModSawChildBudget,ModSawUserContext,ModSawHookReason;
static xllm_response* ModTool(const char* Id,const char* Name,const char* Args){
    xllm_response* R=Response("");if(!R)return NULL;
    R->pToolCalls=calloc(1,sizeof(*R->pToolCalls));R->iToolCallCount=1;
    R->pToolCalls[0].sId=strdup(Id);R->pToolCalls[0].sName=strdup(Name);R->pToolCalls[0].sArgumentsJson=strdup(Args);
    R->eFinish=XLLM_FINISH_TOOL_CALLS;return R;
}
static xllm_result ModComplete(void* Data,const xllm_request* Request,const xllm_stream_callbacks* Callbacks,xllm_response** ResponseOut,xllm_error* Error){
    (void)Data;(void)Callbacks;(void)Error;bool Child=false;
    for(size_t I=0;I<Request->iMessageCount;I++){
        const char* Text=Request->pMessages[I].sContent;
        if(Request->pMessages[I].eRole==XLLM_ROLE_TOOL && Text&&strstr(Text,"role_fixture_read_denied"))ModSawHookReason=true;
        if(Text&&strstr(Text,"C-CHILD"))Child=true;
        if(Text&&strstr(Text,"BINDING-V1"))ModSawHook=true;
        if(Text&&strstr(Text,"role=agent.mod-main"))ModSawUserContext=true;
    }
    if(Child){
        ++ModChildCalls;bool Write=false;for(size_t I=0;I<Request->iToolCount;I++)if(!strcmp(Request->pTools[I].sName,"write"))Write=true;
        ModSawIndependentTools=Write && Request->iToolCount==2;
        ModSawChildBudget=Request->uMaxOutputTokens==4096;
        if(ModChildCalls==1){
            char* Path=xrtPathAbs("../mod-result.txt");xvalue* Value=xrtValueString(xrtStrView(Path));char* Json=xrtJsonStringify(Value,false,NULL);
            char* Args=xrtFormat("{\"path\":%s,\"content\":\"delegated-write-ok\"}",Json);
            *ResponseOut=ModTool("write-1","write",Args);xrtFree(Args);xrtFree(Json);xrtValueRelease(Value);xrtFree(Path);
        }else *ResponseOut=ModChildCalls==2?ModTool("read-1","read","{\"path\":\"mod-result.txt\"}"):Response("child-complete");
    } else {
        ++ModMainCalls;
        *ResponseOut=ModMainCalls==1?ModTool("delegate-1","agent","{\"name\":\"subagent.mod-child\",\"prompt\":\"Write the result file\"}"):Response("mod-complete");
    }
    return *ResponseOut?XLLM_RESULT_OK:XLLM_RESULT_ERROR;
}
static void ModProbe(xwork_runtime* Runtime){
    MdoAgentSessionOptions Options;MdoAgentSessionOptionsInit(&Options);xwork_error Error;
    Options.AgentId="agent.mod-main";Options.ProjectId="default";Options.ProductSessionId="role-program-probe";
    Options.WorkspaceRoot=".";Options.PermissionProfile="full-access";Options.OnModelComplete=ModComplete;
    MdoAgentSession* Session=MdoAgentSessionCreateWithRuntime(Runtime,&Options,&Error);
    if(!Session){printf("mod_error=create:%s\n",Error.sMessage);return;}
    /* A reload must not replace the callback generation retained by Session. */
    char* Old=MdoExtensionRead("modules/agents/mod.c",true,MDO_EXTENSION_TEXT_LIMIT,NULL);
    char* At=Old?strstr(Old,"BINDING-V1"):NULL;if(At)At[9]='2';
    if(!At || !MdoHomeAtomicWrite("modules/agents/mod.c",Old,strlen(Old),false) || !MdoModuleManagerReload())printf("mod_error=reload\n");
    xrtFree(Old);
    MdoAgentRunOptions RunOptions;MdoAgentRunOptionsInit(&RunOptions);RunOptions.Prompt="Run the mod fixture";
    MdoAgentRun* Run=MdoAgentRunCreate(Session,&RunOptions,&Error);xwork_run_result Result={0};
    if(!Run || !MdoAgentRunStart(Run,&Error)){printf("mod_error=start:%s\n",Error.sMessage);goto done;}
    xwork_result Status=MdoAgentRunWait(Run,MdoDeadlineAfterUs(10000000),&Result,&Error);
    printf("mod_result=%d:%s error:%s\n",Status,Result.sFinalText?Result.sFinalText:"",Error.sMessage);
    printf("mod_checks=main:%u child:%u hook:%d tools:%d budget:%d user:%d\n",ModMainCalls,ModChildCalls,ModSawHook,ModSawIndependentTools,ModSawChildBudget,ModSawUserContext);
    printf("mod_hook_reason=%d\n",ModSawHookReason);
    xvalue* View=MdoAgentSessionInspect(Session,&Error);
    printf("mod_inspect=%d\n",View && MdoAgentConfigGet(View,"context") && MdoAgentConfigGet(View,"request"));xrtValueRelease(View);
    char* Text=MdoAgentsContextRead("..","mod-result.txt");printf("mod_file=%d\n",Text && !strcmp(Text,"delegated-write-ok"));xrtFree(Text);
done:
    xworkRunResultUnit(&Result);MdoAgentRunDestroy(Run);MdoAgentSessionRelease(Session);
}
'''


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, default=fixture.ROOT / ".build/host/xs.exe")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="agent-program-", dir=fixture.ROOT / ".build") as raw:
        site = Path(raw) / "site"
        fixture.write_site(site)
        for name in ("agent.h", "sdk.h"):
            shutil.copy2(fixture.ROOT / "include/mdo" / name, site / "generated/module-sdk/mdo" / name)
        source = (site / "probe.c").read_text(encoding="utf-8")
        source = source.replace("void ServiceInit(", PROBE + "\nvoid ServiceInit(")
        insert = "\n".join(f'    MdoHomeAtomicWrite({json.dumps(path)}, {json.dumps(text)}, {len(text.encode())}u, false);'
                           for path, text in [("modules/agents/mod.c", MODULE), ("agents/mod-main.md", MAIN), ("subagents/mod-child.md", CHILD),
                                              ("modules/agents/code-reviewer.c", (fixture.ROOT / "examples/agents/code-reviewer.c").read_text(encoding="utf-8"))])
        source = source.replace("xworkRuntimeConfigInit(&runtime_config);", insert + "\n    xworkRuntimeConfigInit(&runtime_config);")
        source = source.replace("MdoMemoryWriteOptionsInit(&memory_write);", "ModProbe(runtime);\n    MdoMemoryWriteOptionsInit(&memory_write);")
        source = source.replace('PrintRuntimeError("init_error", &error);', 'MdoModuleDiagnostics* D=MdoModuleDiagnosticsSnapshot();for(size_t I=0;I<MdoModuleDiagnosticsCount(D);I++){MdoModuleDiagnosticInfo Info={0};Info.Size=sizeof(Info);MdoModuleDiagnosticsAt(D,I,&Info);printf("module_diagnostic=%s:%s\\n",Info.SourcePath,Info.Message);}MdoModuleDiagnosticsRelease(D);PrintRuntimeError("init_error", &error);')
        source = source.replace('PrintRuntimeError("init_error", &error);', 'printf("module_global_error=%s\\n",xrtGetError()?xrtErrorMessage(xrtGetError()):"none");MdoModuleDiagnostics* Q=MdoModulesDiagnosticsCreate();MdoModuleCatalog* Candidate=MdoModulesBuildCandidate(99,Q);for(size_t I=0;I<MdoModuleDiagnosticsCount(Q);I++){MdoModuleDiagnosticInfo Info={0};Info.Size=sizeof(Info);MdoModuleDiagnosticsAt(Q,I,&Info);printf("candidate_diagnostic=%s:%s\\n",Info.SourcePath,Info.Message);}MdoModuleCatalogRelease(Candidate);MdoModuleDiagnosticsRelease(Q);PrintRuntimeError("init_error", &error);')
        (site / "probe.c").write_text(source, encoding="utf-8")
        output = fixture.run_probe(args.host.resolve(), site, Path(raw) / "home")
        assert "mod_result=0:mod-complete" in output, output
        assert "mod_checks=main:2 child:3 hook:1 tools:1 budget:1 user:1" in output, output
        assert "mod_hook_reason=1" in output, output
        assert "program-after-delegate-current=1" in output, output
        assert "mod_inspect=1" in output and "mod_file=1" in output, output
        assert "mod_error=" not in output and "program-instance-released" in output, output
    print("C/config role programs: delegated write, independent budgets, hooks, inspection and reload pinning PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
