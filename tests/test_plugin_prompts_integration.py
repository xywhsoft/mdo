"""Actual plugin loading and managed model requests using an offline model stub."""
import argparse
import json
from pathlib import Path
from native_product_runner import product
from model_fixture import model_catalog

ROOT=Path(__file__).resolve().parents[1]
PLUGIN=r'''
#include "mdo/module.h"
static mdo_result Execute(void* U,const mdo_tool_context_v1* C,const char* A,mdo_result_writer_v1* W,char* E,size_t N){
 (void)U;(void)C;(void)A;(void)E;(void)N;return W->WriteText(W->Context,"QA tool complete")&&W->SetSuccess(W->Context,true)?MDO_RESULT_OK:MDO_RESULT_ERROR;
}
static mdo_result Register(const mdo_host_services_v1* H,const mdo_registrar_v1* R,void** D,char* E,size_t N){
 (void)H;(void)D;mdo_tool_v1 T={MDO_V1_HEADER(mdo_tool_v1),"qa_tool","QA tool","Offline read fixture","{}",MDO_TOOL_EFFECT_READ,0,NULL,NULL,4096,NULL,NULL,Execute,NULL};
 mdo_result Code=R->AddTool(R->Context,&T,E,N);if(Code!=MDO_RESULT_OK)return Code;
 Code=R->AddBuiltinTool(R->Context,"agent",E,N);if(Code!=MDO_RESULT_OK)return Code;
 const char* Tools[]={"qa_tool"};mdo_prompt_contribution_v1 P={0};P.Size=sizeof(P);P.AbiVersion=1;P.Id="native";P.Text="NATIVE-INTEGRATION-GUIDE";P.Targets=3;P.AnyTools=Tools;P.AnyToolCount=1;P.Priority=20;
 return R->AddPromptContribution(R->Context,&P,E,N);
}
static const mdo_module_v1 M={MDO_V1_HEADER(mdo_module_v1),MDO_PLUGIN_ID,"Prompt integration","Offline fixture",MDO_PLUGIN_VERSION,0,NULL,0,Register,NULL};
const mdo_module_v1* mdoModuleEntry(void){return &M;}
'''
CONTRACT=r'''
static unsigned QaCalls,QaParentCalls,QaChildCalls;static bool QaEveryRequest=true,QaToolRun,QaDelegated,QaDelegatedViews=true;static char QaDelegateOutput[2048];
static MdoAgentSession* QaInspectSession;static bool QaChildDetails;
static void QaCheck(xvalue* R,cstr Key,bool Ok){xrtValueObjectSetNew(R,xrtStrView(Key),xrtValueBool(Ok));}
static xllm_result QaComplete(void* U,const xllm_request* Q,const xllm_stream_callbacks* S,xllm_response** Out,xllm_error* E){
 (void)U;(void)S;(void)E;++QaCalls;unsigned Native=0,Manifest=0;bool Child=false;
 for(size_t I=0;I<Q->iMessageCount;I++){cstr T=Q->pMessages[I].sContent;if(!T)continue;if(strstr(T,"NATIVE-INTEGRATION-GUIDE"))++Native;if(strstr(T,"MANIFEST-INTEGRATION-GUIDE"))++Manifest;if(strstr(T,"QA tool complete"))QaToolRun=true;if(strstr(T,"CHILD-BASE"))Child=true;if(QaDelegated&&Q->pMessages[I].eRole==XLLM_ROLE_TOOL)snprintf(QaDelegateOutput,sizeof(QaDelegateOutput),"%s",T);}
 bool Tool=QaCalls==1;
 if(QaDelegated){if(Child)++QaChildCalls;else ++QaParentCalls;QaDelegatedViews=QaDelegatedViews&&(Child?(Native==1&&Manifest==1):(Native==0&&Manifest==0));Tool=!Child&&QaParentCalls==1;}
 else QaEveryRequest=QaEveryRequest&&Native==1&&Manifest==1;
 if(QaDelegated&&Child&&QaInspectSession){
  xwork_error IE={0};xvalue* Inspect=MdoAgentSessionInspect(QaInspectSession,&IE);const xvalue* Instances=SdkGet(Inspect,"instances");
  for(size_t J=0;J<xrtValueCount(Instances);J++){const xvalue* Item=xrtValueArrayGet(Instances,J);cstr Id=SdkField(Item,"agent_id");if(Id&&!strcmp(Id,"subagent.qa-child")){xvalue* G=MdoAgentsPluginGroup((xvalue*)SdkGet(Item,"context"));QaChildDetails=G&&strstr(SdkField(G,"text"),"NATIVE-INTEGRATION-GUIDE")&&SdkGet(Item,"request");}}
  xrtValueRelease(Inspect);
 }
 xllm_response* R=calloc(1,sizeof(*R));if(!R)return XLLM_RESULT_ERROR;R->sContent=strdup(Tool?"":"offline integration complete");R->sModel=strdup("fixture");R->sFinishReason=strdup(Tool?"tool_calls":"stop");R->uHttpStatus=200;
 if(Tool){R->eFinish=XLLM_FINISH_TOOL_CALLS;R->iToolCallCount=1;R->pToolCalls=calloc(1,sizeof(*R->pToolCalls));R->pToolCalls[0].sId=strdup(QaDelegated?"qa-delegate":"qa-call");R->pToolCalls[0].sName=strdup(QaDelegated?"agent":"qa_tool");R->pToolCalls[0].sArgumentsJson=strdup(QaDelegated?"{\"name\":\"subagent.qa-child\",\"prompt\":\"Complete the independent child task\"}":"{}");}
 else R->eFinish=XLLM_FINISH_STOP;
 *Out=R;return XLLM_RESULT_OK;
}
xvalue* QaProductContract(const xvalue* P,xvfs V){
 xvalue* R=xrtValueObject();char Why[512]={0};xwork_error E;xworkErrorInit(&E);
 bool Home=QaProductHome(SdkField(P,"root"),SdkField(P,"app"),V);
 bool Ready=Home&&MdoExtensionsInit()&&MdoProjectLifecycleInit()&&MdoConfigInit()&&MdoModelManagerInit()&&MdoSkillManagerInit()&&MdoApprovalManagerInit()&&MdoApplicationPluginsInit();
 xwork_runtime_config RC;xworkRuntimeConfigInit(&RC);xwork_runtime* Runtime=Ready?xworkRuntimeCreate(&RC,&E):NULL;
 MdoModuleManagerAttachApplication(MdoApplicationApi());Ready=Ready&&Runtime&&MdoMemoryManagerInit(Runtime)&&MdoModuleManagerInit(Runtime);QaCheck(R,"real_plugin_and_role_load",Ready);
 if(!Ready){SdkPut(R,"initialization_error",E.sMessage);MdoModuleDiagnostics* D=MdoModuleDiagnosticsSnapshot();for(size_t I=0;D&&I<D->Count;I++)SdkPut(R,"module_error",D->Entries[I].Message);MdoModuleDiagnosticsRelease(D);goto done;}
 MdoModuleCatalog* Pinned=MdoModuleCatalogSnapshot();QaCheck(R,"manifest_and_native_share_catalog",xrtValueCount(MdoModuleCatalogPromptContributions(Pinned))==3);
 MdoAgentSessionOptions O;MdoAgentSessionOptionsInit(&O);O.AgentId="agent.qa";O.ProjectId="default";O.ProductSessionId="qa-plugin-prompts";O.WorkspaceRoot=SdkField(P,"root");O.OnModelComplete=QaComplete;
 MdoAgentSession* Session=MdoAgentSessionCreateWithRuntime(Runtime,&O,&E);QaCheck(R,"managed_session_created",Session!=NULL);
 if(Session){
  MdoAgentInstance ScopeRole={0};ScopeRole.Definition.Flags=MDO_AGENT_MAIN;ScopeRole.Configuration=SdkParse("{\"context\":{\"plugins.instructions\":{\"include\":[\"plugin:qa.passive\"]}}}");
  xwork_agent_extension_context ScopeNative={0};ScopeNative.pAgent=Session->Agent;xvalue* ScopePlan=SdkParse("{\"sources\":[]}");
  cstr DisabledScope="{\"plugin:qa.passive\":false}";bool Scoped=MdoHomeAtomicWrite(".mdo/plugins.json",DisabledScope,strlen(DisabledScope),false)&&MdoAgentsPluginSource(Session->Owner,&ScopeRole,&ScopeNative,ScopePlan,&E);
  xvalue* ScopeGroup=MdoAgentsPluginGroup(ScopePlan);QaCheck(R,"prompt_only_plugin_respects_project_disable",Scoped&&ScopeGroup&&!SdkField(ScopeGroup,"text")[0]&&!strcmp(SdkField(SdkGet(ScopeGroup,"scope"),"plugin:qa.passive"),"project_scope"));xrtValueRelease(ScopePlan);
  ScopePlan=SdkParse("{\"sources\":[]}");Scoped=MdoHomeAtomicWrite(".mdo/plugins.json","{}",2,false)&&MdoAgentsPluginSource(Session->Owner,&ScopeRole,&ScopeNative,ScopePlan,&E);ScopeGroup=MdoAgentsPluginGroup(ScopePlan);
  QaCheck(R,"project_scope_change_refreshes_next_context",Scoped&&strstr(SdkField(ScopeGroup,"text"),"EXPLICIT-PASSIVE-GUIDE"));xrtValueRelease(ScopePlan);xrtValueRelease(ScopeRole.Configuration);
  QaCheck(R,"base_prompt_never_contains_plugin_text",!strstr(xllmSessionGetSystemPrompt(Session->Owner->LlmSession),"INTEGRATION-GUIDE"));
  MdoAgentRunOptions RunOptions;MdoAgentRunOptionsInit(&RunOptions);RunOptions.Prompt="Run the offline tool and finish.";MdoAgentRun* Run=MdoAgentRunCreate(Session,&RunOptions,&E);
  bool Started=Run&&MdoAgentRunStart(Run,&E);xwork_run_result Result={0};xwork_result Code=Started?MdoAgentRunWait(Run,MdoDeadlineAfterUs(10000000),&Result,&E):XWORK_RESULT_ERROR;
  QaCheck(R,"managed_tool_loop_completes",Code==XWORK_RESULT_OK&&QaCalls==2&&QaToolRun);QaCheck(R,"each_actual_request_has_one_of_each_guide",QaEveryRequest&&QaCalls==2);
  if(Code!=XWORK_RESULT_OK)SdkPut(R,"run_error",E.sMessage);
  xvalue* Inspect=MdoAgentSessionInspect(Session,&E);QaCheck(R,"runtime_inspector_has_delivery",Inspect&&strstr(SdkField(MdoAgentsPluginGroup((xvalue*)SdkGet(Inspect,"context")),"delivery"),"request_message"));xrtValueRelease(Inspect);
  MdoAgentRunDestroy(Run);xworkRunResultUnit(&Result);MdoAgentSessionRelease(Session);
 }
 QaDelegated=true;O.AgentId="agent.coordinator";O.ProductSessionId="qa-plugin-delegation";Session=MdoAgentSessionCreateWithRuntime(Runtime,&O,&E);QaInspectSession=Session;
 bool DelegatedOk=false;
 if(Session){MdoAgentRunOptions RO;MdoAgentRunOptionsInit(&RO);RO.Prompt="Delegate the independent task.";MdoAgentRun* Run=MdoAgentRunCreate(Session,&RO,&E);xwork_run_result Result={0};bool Started=Run&&MdoAgentRunStart(Run,&E);xwork_result Code=Started?MdoAgentRunWait(Run,MdoDeadlineAfterUs(10000000),&Result,&E):XWORK_RESULT_ERROR;
  DelegatedOk=Code==XWORK_RESULT_OK&&QaParentCalls==2&&QaChildCalls==1;
  if(!DelegatedOk){SdkPut(R,"delegation_error",E.sMessage);SdkPut(R,"delegation_output",QaDelegateOutput);SdkPutUInt(R,"parent_calls",QaParentCalls);SdkPutUInt(R,"child_calls",QaChildCalls);}
  QaCheck(R,"child_runtime_prompt_details_available",QaChildDetails);QaInspectSession=NULL;
  MdoAgentRunDestroy(Run);xworkRunResultUnit(&Result);MdoAgentSessionRelease(Session);
 }
 QaCheck(R,"real_coordinator_child_run",DelegatedOk);QaCheck(R,"only_child_receives_browser_capability_guides",DelegatedOk&&QaDelegatedViews);
 size_t InspectBudget=1;xvalue* Detail=xrtValueObject(),*Large=SdkParse("{\"text\":\"bounded inspection fixture\"}");
 QaCheck(R,"inspection_budget_is_explicit",MdoAgentsInspectDetail(Detail,"context",Large,&InspectBudget)&&MdoExtOpBool(Detail,"details_truncated",false)&&!SdkGet(Detail,"context"));xrtValueRelease(Detail);xrtValueRelease(Large);
 /* A headless prompt-only plugin is effective without any frontend instance. */
 bool Disabled=MdoExtensionsSetEnabled("plugins","passive",false,Why)&&MdoApplicationPluginsReload(Why,sizeof(Why));MdoModuleCatalog* Current=MdoModuleCatalogSnapshot();
 QaCheck(R,"headless_disable_refreshes_new_catalog",Disabled&&xrtValueCount(MdoModuleCatalogPromptContributions(Current))==2);
 MdoApplicationInvocation Invocation={0};Invocation.Catalog=Pinned;mdo_sdk_context_v1 Context={0};Context.Opaque=&Invocation;mdo_error_v1 Failure={0};bool Found=false;xvalue* Reply=NULL,*Query=xrtValueObject();
 bool Queried=MdoApplicationPluginsCall(&Context,"plugin.prompts",Query,&Reply,&Failure,&Found);QaCheck(R,"c_sdk_reads_its_pinned_catalog",Queried&&Found&&xrtValueCount(SdkGet(Reply,"items"))==3);
 const xvalue* First=xrtValueArrayGet(MdoModuleCatalogPromptContributions(Pinned),0);char* Original=xrtStrDup(SdkField(First,"text"));
 SdkPut((xvalue*)xrtValueArrayGet(SdkGet(Reply,"items"),0),"text","MUTATED-RETURN");QaCheck(R,"sdk_result_cannot_mutate_registry",Original&&!strcmp(Original,SdkField(First,"text")));xrtFree(Original);xrtValueRelease(Reply);xrtValueRelease(Query);
 QaCheck(R,"old_snapshot_retains_original_content",xrtValueCount(MdoModuleCatalogPromptContributions(Pinned))==3);MdoModuleCatalogRelease(Current);
 bool Enabled=MdoExtensionsSetEnabled("plugins","passive",true,Why)&&MdoApplicationPluginsReload(Why,sizeof(Why));Current=MdoModuleCatalogSnapshot();QaCheck(R,"headless_reenable_refreshes_catalog",Enabled&&xrtValueCount(MdoModuleCatalogPromptContributions(Current))==3);MdoModuleCatalogRelease(Current);MdoModuleCatalogRelease(Pinned);
done:
 MdoModuleManagerUnit();MdoMemoryManagerUnit();MdoSkillManagerUnit();if(Runtime)xworkRuntimeRelease(Runtime);MdoModelManagerUnit();MdoApprovalManagerUnit();MdoConfigUnit();MdoProjectLifecycleUnit();MdoApplicationPluginsUnit();MdoExtensionsUnit();if(Home)MdoHomeUnit();MdoModuleManagerAttachApplication(NULL);return R;
}
'''

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--url',required=True);p.add_argument('--home',type=Path,required=True);p.add_argument('--project',default='default');args=p.parse_args()
    out=ROOT/'.build/prompt-contributions';fixture=out/'integration';fixture.mkdir(parents=True,exist_ok=True)
    def write(path,value):
        dest=fixture/path;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_text(value if isinstance(value,str) else json.dumps(value),encoding='utf-8')
    write('config/models.json',dict(schema_version=1,patch=model_catalog()))
    write('plugins/qa/extension.json',dict(kind='plugins',id='plugin:qa.integration',alias='qa',name='QA plugin',entry='plugin.json',version='1.0.0',platforms=['any']))
    write('plugins/qa/plugin.json',dict(entry=dict(native='plugin.c'),prompts=[dict(id='manifest',file='guide.md',when=dict(any_tools=['qa_tool']))]))
    write('plugins/qa/guide.md','MANIFEST-INTEGRATION-GUIDE');write('plugins/qa/plugin.c',PLUGIN)
    write('plugins/passive/extension.json',dict(kind='plugins',id='plugin:qa.passive',alias='passive',name='Passive guide',entry='plugin.json',version='1.0.0',platforms=['any']))
    write('plugins/passive/plugin.json',dict(prompts=[dict(id='passive',text='EXPLICIT-PASSIVE-GUIDE',activation='explicit')]))
    write('agents/qa/extension.json',dict(kind='agents',id='agent:qa.integration',alias='qa',name='QA role',entry='agent.json',platforms=['any']))
    write('agents/qa/agent.json',dict(schema='mdo.agent.v1',name='QA role',prompt='ROLE-BASE',model='fixture',tools=['qa_tool'],context={name:False for name in ['memory.global','memory.project','skills.discovery','knowledge','external_tools','user_instructions']}))
    write('agents/coordinator/extension.json',dict(kind='agents',id='agent:qa.coordinator',alias='coordinator',name='Coordinator',entry='agent.json',platforms=['any']))
    write('agents/coordinator/agent.json',dict(schema='mdo.agent.v1',name='Coordinator',prompt='COORDINATOR-BASE',model='fixture',tools=['agent'],read_only=True,allow_delegation=True,delegation=dict(allow=['subagent.qa-child']),context={'memory.global':False,'memory.project':False,'external_tools':False,'skills.discovery':False}))
    write('subagents/qa-child/extension.json',dict(kind='subagents',id='subagent:qa.child',alias='qa-child',name='Independent child',entry='agent.json',platforms=['any']))
    write('subagents/qa-child/agent.json',dict(schema='mdo.agent.v1',name='Independent child',mode='subagent',prompt='CHILD-BASE',model='fixture',tools=['qa_tool'],context={'memory.global':False,'memory.project':False,'external_tools':False,'skills.discovery':False}))
    result=product(args,fixture,CONTRACT,out/'integration.json')
    assert result and all(value is True for value in result.values()),result
    print(f'Plugin prompt integration: {len(result)} checks PASS (offline model stub)')

if __name__=='__main__':main()
