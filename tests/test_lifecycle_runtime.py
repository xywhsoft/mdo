"""Generation-pinned global lifecycle observers/controllers on config/C roles."""
import argparse
import json
from pathlib import Path
import tempfile
import shutil
import test_agent_program_runtime as roles
import test_agent_runtime as fixture

LIFECYCLE=r'''
static mdo_result Lifecycle(void* Data,mdo_agent_phase Phase,const mdo_agent_context_v1* C,char* Error,size_t Capacity){
 (void)Data;char Line[512];
 if(Phase==MDO_AGENT_RUN_PREPARE&&!strcmp(C->AgentId,"agent.life-denied")){
  snprintf(Error,Capacity,"lifecycle_prepare_denied");return MDO_RESULT_ERROR;
 }
 if(Phase==MDO_AGENT_CONTEXT){
  xvalue* Source=mdoAgentContextSource(C,"system");xstrview Text;
  if(Source&&xrtValueGetString(xrtValueObjectGet(Source,xrtStrView("text")),&Text)){
   char* Next=xrtFormat("%s GLOBAL-LIFECYCLE",Text.Data);bool Ok=Next&&mdoAgentSetContextText(C,"system",Next);xrtFree(Next);if(!Ok)return MDO_RESULT_ERROR;
  }
 }
 if(Phase==MDO_AGENT_RUN_FINISHED){
  xwork_agent_extension_context* Native=mdoAgentNative(C);
  if(!C->OutcomeAvailable||C->Outcome!=MDO_RESULT_OK||!Native||!Native->pResult||!Native->pResult->sFinalText){snprintf(Error,Capacity,"finish_not_materialized");return MDO_RESULT_ERROR;}
  snprintf(Line,sizeof(Line),"LIFECYCLE-V1 finished:%s:%s",C->AgentId,Native->pResult->sFinalText);Host->Core->Log(Host->Core->Context,MDO_LOG_INFO,Line);
 }
 if(Phase==MDO_AGENT_DELEGATE_FINISHED){
  snprintf(Line,sizeof(Line),"lifecycle-child-actually-finished:%s:%d",C->AgentId,C->OutcomeAvailable);Host->Core->Log(Host->Core->Context,MDO_LOG_INFO,Line);
 }
 if(Phase==MDO_AGENT_RUN_FAILED){
  snprintf(Line,sizeof(Line),"lifecycle-failed:%s:%d",C->AgentId,C->OutcomeAvailable);Host->Core->Log(Host->Core->Context,MDO_LOG_INFO,Line);
 }
 return MDO_RESULT_OK;
}
'''

OBSERVER=r'''
#include "mdo/lifecycle.h"
#include <stdio.h>
static const mdo_host_services_v1* Host;
static mdo_result Observe(void* U,mdo_agent_phase P,const mdo_agent_context_v1* C,char* E,size_t N){
 (void)U;(void)P;(void)C;Host->Core->Log(Host->Core->Context,MDO_LOG_INFO,"lifecycle-observer-error-isolated");snprintf(E,N,"optional_observer_error");return MDO_RESULT_ERROR;
}
static mdo_result Register(const mdo_host_services_v1* H,const mdo_registrar_v1* R,void** D,char* E,size_t N){
 (void)D;Host=H;mdo_lifecycle_v1 L={sizeof(L),1,"observer",NULL,0,true,MDO_AGENT_PHASE_BIT(MDO_AGENT_BEFORE_REQUEST),NULL,Observe};
 return mdoRegisterLifecycle(R,&L,E,N);
}
MDO_EXPORT const mdo_module_v1* mdoModuleEntry(void){
 static const mdo_module_v1 M={sizeof(M),1,"life.observer","Observer","Lifecycle only module","1",MDO_CAPABILITY_LOG,0,0,Register,0};return &M;
}'''

EXTRA=r'''
static void LifeExtra(xwork_runtime* Runtime){
 MdoAgentSessionOptions Options;MdoAgentSessionOptionsInit(&Options);Options.AgentId="agent.life-config";Options.ProjectId="default";Options.ProductSessionId="lifecycle-config";Options.WorkspaceRoot=".";Options.OnModelComplete=ModComplete;
 xwork_error E;MdoAgentSession* S=MdoAgentSessionCreateWithRuntime(Runtime,&Options,&E);
 MdoAgentRunOptions O;MdoAgentRunOptionsInit(&O);O.Prompt="Config lifecycle";
 MdoAgentRun* R=S?MdoAgentRunCreate(S,&O,&E):NULL;xwork_run_result Result={0};
 if(!R||!MdoAgentRunStart(R,&E))printf("life_extra_error=%s\n",E.sMessage);
 else printf("life_config_result=%d\n",MdoAgentRunWait(R,MdoDeadlineAfterUs(5000000),&Result,&E));
 xworkRunResultUnit(&Result);MdoAgentRunDestroy(R);MdoAgentSessionRelease(S);
 Options.AgentId="agent.life-denied";S=MdoAgentSessionCreateWithRuntime(Runtime,&Options,&E);R=S?MdoAgentRunCreate(S,&O,&E):NULL;
 if(R&&MdoAgentRunStart(R,&E)){xwork_result Code=MdoAgentRunWait(R,MdoDeadlineAfterUs(5000000),&Result,&E);printf("life_denied=%d:%s\n",Code,E.sMessage);}
 else printf("life_extra_error=denied-create:%s\n",E.sMessage);
 xworkRunResultUnit(&Result);MdoAgentRunDestroy(R);MdoAgentSessionRelease(S);
}
'''

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--host',type=Path,default=fixture.ROOT/'.build/host/xs.exe');parser.add_argument('--background',action='store_true');args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='lifecycle-',dir=fixture.ROOT/'.build') as tmp:
        site=Path(tmp)/'site';fixture.write_site(site)
        for name in ('agent.h','sdk.h','lifecycle.h'):
            shutil.copy2(fixture.ROOT/'include/mdo'/name,site/'generated/module-sdk/mdo'/name)
        module=roles.MODULE.replace('#include "mdo/agent.h"','#include "mdo/lifecycle.h"')
        module=module.replace('static mdo_result Register(',LIFECYCLE+'\nstatic mdo_result Register(')
        module=module.replace('(void)Data;Host=H;mdo_result Result=',
            '(void)Data;Host=H;mdo_lifecycle_v1 L={sizeof(L),1,"global",NULL,10,false,MDO_AGENT_PHASES_ALL,NULL,Lifecycle};if(mdoRegisterLifecycle(R,&L,Error,Capacity)!=MDO_RESULT_OK)return MDO_RESULT_ERROR;mdo_result Result=')
        probe=roles.PROBE.replace('    char* At=Old?', '    char* Version=Old?strstr(Old,"LIFECYCLE-V1"):NULL;if(Version)Version[11]=\'2\';\n    char* At=Old?')
        if args.background:
            # Reuse the caller stack after session creation. Resolved profile
            # strings must remain valid for a child starting on another thread.
            probe='static void ReuseSelectionStack(void){volatile char Bytes[16384];for(size_t I=0;I<sizeof(Bytes);I++)Bytes[I]=(char)0x5a;}\n'+probe
            probe=probe.replace('    if(!Session){','    ReuseSelectionStack();\n    if(!Session){')
            module=module.replace('.Flags=MDO_AGENT_SUBAGENT,','.Flags=MDO_AGENT_SUBAGENT|MDO_AGENT_ALLOW_BACKGROUND,')
            probe=probe.replace('Write the result file\\\"}', 'Write the result file\\\",\\\"background\\\":true}')
            probe=probe.replace('++ModChildCalls;bool Write=false;','++ModChildCalls;xrtSleep(75);bool Write=false;')
            wait=r'''xwork_task_snapshot* T=xworkRuntimeTaskSnapshot(Runtime,0,&Error);
    uint64_t Task=0;for(size_t I=0;T&&I<xworkTaskSnapshotCount(T);I++){xwork_task_info Info;xworkTaskInfoInit(&Info);if(xworkTaskSnapshotTaskAt(T,I,&Info)&&Info.eKind==XWORK_TASK_AGENT)Task=Info.uTaskId;}
    xworkTaskSnapshotRelease(T);if(Task){uint64_t Done=0;printf("life_background_wait=%d\n",xworkRuntimeWaitTasks(Runtime,&Task,1,XWORK_TASK_WAIT_ALL,MdoDeadlineAfterUs(5000000),NULL,&Done,&Error));
    xwork_task_output Output;xworkTaskOutputInit(&Output);if(xworkRuntimeReadTaskOutput(Runtime,Task,0,0,0,8192,&Output,&Error))printf("life_background_result=%.*s stderr=%.*s\n",(int)Output.iResultSize,Output.sResult?Output.sResult:"",(int)Output.iStderrSize,Output.pStderr?(char*)Output.pStderr:"");xworkTaskOutputUnit(&Output);}
    '''
            probe=probe.replace('    printf("mod_result=',wait+'printf("mod_result=')
        source=(site/'probe.c').read_text(encoding='utf-8')
        source=source.replace('void ServiceInit(',probe+'\n'+EXTRA+'\nvoid ServiceInit(')
        files=[('modules/agents/mod.c',module),('modules/tools/lifecycle.c',OBSERVER),('agents/mod-main.md',roles.MAIN),('subagents/mod-child.md',roles.CHILD)]
        for identity in ('life-config','life-denied'):
            files.append(('agents/'+identity+'.md','---\nschema: mdo.agent.v1\nname: Config lifecycle\ntools: none\n---\nConfig lifecycle role'))
        insertion='\n'.join(f'    MdoHomeAtomicWrite({json.dumps(p)}, {json.dumps(t)}, {len(t.encode())}u, false);' for p,t in files)
        source=source.replace('xworkRuntimeConfigInit(&runtime_config);',insertion+'\n    xworkRuntimeConfigInit(&runtime_config);')
        source=source.replace('MdoMemoryWriteOptionsInit(&memory_write);','ModProbe(runtime);LifeExtra(runtime);\n    MdoMemoryWriteOptionsInit(&memory_write);')
        (site/'probe.c').write_text(source,encoding='utf-8')
        output=fixture.run_probe(args.host.resolve(),site,Path(tmp)/'home')
        assert 'mod_result=0:mod-complete' in output,output
        assert 'LIFECYCLE-V1 finished:agent.mod-main:mod-complete' in output,output
        assert 'LIFECYCLE-V1 finished:subagent.mod-child:child-complete' in output,output
        assert 'LIFECYCLE-V2 finished:agent.life-config:mod-complete' in output,output
        assert 'lifecycle-child-actually-finished:subagent.mod-child:1' in output,output
        if args.background:
            assert 'life_background_wait=0' in output,output
            assert output.index('finished:agent.mod-main:')<output.index('finished:subagent.mod-child:'),output
        assert 'lifecycle-observer-error-isolated' in output,output
        assert 'life_config_result=0' in output and 'lifecycle-failed:agent.life-denied:1' in output,output
        assert 'lifecycle_prepare_denied' in output and 'life_extra_error=' not in output,output
    print('PASS global C/config lifecycle, materialized outcomes, real child completion, observer isolation, control rejection and reload pinning')

if __name__=='__main__':main()
