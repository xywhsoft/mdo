"""Compile current product sources in an existing host; no app/runtime copies."""
import argparse
from pathlib import Path
from native_product_runner import product

ROOT = Path(__file__).resolve().parents[1]
CONTRACT = r'''
static void QaPromptCheck(xvalue* R,cstr Key,bool Ok){xrtValueObjectSetNew(R,xrtStrView(Key),xrtValueBool(Ok));}
static xllm_result QaPromptModel(void* U,const xllm_request* Q,const xllm_stream_callbacks* S,xllm_response** R,xllm_error* E){(void)U;(void)Q;(void)S;(void)R;(void)E;return XLLM_RESULT_ERROR;}
static xwork_result QaPromptTool(void* U,const xwork_tool_context* C,cstr A,xwork_tool_output* O,xwork_error* E){(void)U;(void)C;(void)A;(void)O;(void)E;return XWORK_RESULT_OK;}
static xvalue* QaPromptPlan(MdoAgentOwner* O,MdoAgentInstance* I,xwork_agent_extension_context* N,xwork_error* E){
 xvalue* Plan=SdkParse("{\"sources\":[]}");if(!MdoAgentsPluginSource(O,I,N,Plan,E)){xrtValueRelease(Plan);return NULL;}return Plan;
}
static void QaPromptRequest(xllm_request* Q){xllmRequestInit(Q);xllmRequestAddTextMessage(Q,XLLM_ROLE_SYSTEM,"ROLE-BASE");xllmRequestAddTextMessage(Q,XLLM_ROLE_USER,"USER-TASK");xllmRequestAddTool(Q,"qa_tool","QA tool","{}",false);}
static void QaPromptRoles(xvalue* R,MdoModuleCatalog* C,MdoModuleGeneration* G,cstr Workspace){
 xwork_error E;xworkErrorInit(&E);xwork_agent_config Config;xworkAgentConfigInit(&Config);Config.sWorkspaceRoot=Workspace;Config.sModel="offline";Config.OnModelComplete=QaPromptModel;Config.bRegisterBuiltinTools=false;Config.bAutoSaveSession=false;Config.bAllowArtifactWrites=false;
 xllm_session_config SC;xllmSessionConfigInit(&SC);xllm_session* S1=xllmSessionCreate(&SC,NULL),*S2=xllmSessionCreate(&SC,NULL);
 Config.pSession=S1;xwork_agent* Parent=xworkAgentCreate(&Config,&E);Config.pSession=S2;xwork_agent* Child=xworkAgentCreate(&Config,&E);
 xwork_tool_definition D={0};D.sName="qa_tool";D.sDescription="QA tool";D.sParametersJson="{}";D.uEffects=XWORK_TOOL_EFFECT_READ;D.OnExecute=QaPromptTool;
 bool Ready=Parent&&Child&&xworkAgentRegisterTool(Child,&D,&E);QaPromptCheck(R,"actual_child_catalog",Ready);
 if(!Ready){SdkPut(R,"role_error",E.sMessage);if(Parent)xworkAgentDestroy(Parent);if(Child)xworkAgentDestroy(Child);if(S1)xllmSessionDestroy(S1);if(S2)xllmSessionDestroy(S2);return;}
 MdoModuleToolBinding Tool={0};Tool.Owner=G;Tool.Id="qa_tool";MdoModuleToolBinding* Tools[]={&Tool};C->Tools=Tools;C->ToolCount=1;
 MdoAgentOwner O={0};O.Modules=C;O.RootAgent=Parent;O.InstanceLock=xrtMutexCreate();
 MdoAgentInstance I={0};I.Definition.Id="qa-role";I.Definition.Flags=MDO_AGENT_MAIN|MDO_AGENT_SUBAGENT;I.Configuration=xrtValueObject();
 xwork_agent_extension_context N={0};I.Plan=QaPromptPlan(&O,&I,&N,&E);xvalue* Group=MdoAgentsPluginGroup(I.Plan);
 QaPromptCheck(R,"static_preview_is_candidate",Group&&!strcmp(SdkField(Group,"resolution"),"candidate")&&!SdkField(Group,"text")[0]);xrtValueRelease(I.Plan);
 N.pAgent=Parent;I.Plan=QaPromptPlan(&O,&I,&N,&E);Group=MdoAgentsPluginGroup(I.Plan);
 QaPromptCheck(R,"parent_does_not_advertise_child_tools",Group&&!SdkField(Group,"text")[0]);xrtValueRelease(I.Plan);
 N.pAgent=Child;I.Plan=QaPromptPlan(&O,&I,&N,&E);Group=MdoAgentsPluginGroup(I.Plan);
 QaPromptCheck(R,"child_independent_capability_selection",Group&&strstr(SdkField(Group,"text"),"NATIVE-GUIDE")&&!strstr(SdkField(Group,"text"),"MANIFEST-GUIDE"));
 char* Base=MdoAgentsContextRenderBase(I.Plan,"system",&E);QaPromptCheck(R,"ledger_base_excludes_contributions",Base&&!Base[0]);xrtFree(Base);
 xllm_request Q;QaPromptRequest(&Q);N.pRequest=&Q;MdoAgentPluginMessage B={0};
 bool Prepared=MdoAgentsPluginRequest(&O,&I,&N,&B,false,&E);size_t Before=Q.iMessageCount;
 xvalue* Preview=Prepared?MdoAgentsRequestPreview(&Q):NULL;uint64 Tokens=0;if(Preview)SdkUInt(SdkGet(Preview,"estimated_input_tokens"),&Tokens);xrtValueRelease(Preview);
 QaPromptCheck(R,"included_in_final_request_budget",Prepared&&Tokens>0&&Before==3&&strstr(Q.pMessages[1].sContent,"NATIVE-GUIDE"));
 Q.iToolCount=0;bool Final=MdoAgentsPluginRequest(&O,&I,&N,&B,true,&E);
 QaPromptCheck(R,"final_tool_removal_removes_guidance",Final&&Q.iMessageCount==2&&!strstr(Q.pMessages[0].sContent,"NATIVE-GUIDE"));xrtFree(B.Text);
 /* Restore the allocated tool count before releasing the descriptor. */
 Q.iToolCount=1;xllmRequestUnit(&Q);QaPromptRequest(&Q);memset(&B,0,sizeof(B));
 Prepared=MdoAgentsPluginRequest(&O,&I,&N,&B,false,&E);Final=Prepared&&MdoAgentsPluginRequest(&O,&I,&N,&B,true,&E);
 QaPromptCheck(R,"retries_do_not_accumulate",Final&&Q.iMessageCount==3);xrtFree(B.Text);xllmRequestUnit(&Q);
 QaPromptRequest(&Q);memset(&B,0,sizeof(B));Prepared=MdoAgentsPluginRequest(&O,&I,&N,&B,false,&E);
 if(Prepared){xllmMessageUnit(&Q.pMessages[1]);xllmMessageInit(&Q.pMessages[1],XLLM_ROLE_SYSTEM);xllmMessageSetContent(&Q.pMessages[1],"MANUAL-REQUEST");}
 Final=Prepared&&MdoAgentsPluginRequest(&O,&I,&N,&B,true,&E);Group=MdoAgentsPluginGroup(I.Plan);
 QaPromptCheck(R,"advanced_request_override_respected",Final&&Q.iMessageCount==3&&!strcmp(SdkField(Group,"delivery"),"agent_request_override"));xrtFree(B.Text);xllmRequestUnit(&Q);
 Group=MdoAgentsPluginGroup(I.Plan);SdkPut(Group,"text","ROLE-OVERRIDE");QaPromptRequest(&Q);memset(&B,0,sizeof(B));
 Prepared=MdoAgentsPluginRequest(&O,&I,&N,&B,false,&E);Final=Prepared&&MdoAgentsPluginRequest(&O,&I,&N,&B,true,&E);
 QaPromptCheck(R,"context_override_survives_finalization",Final&&Q.iMessageCount==3&&!strcmp(Q.pMessages[1].sContent,"ROLE-OVERRIDE"));xrtFree(B.Text);xllmRequestUnit(&Q);xrtValueRelease(I.Plan);
 xrtValueRelease(I.Configuration);I.Configuration=SdkParse("{\"context\":{\"plugins.instructions\":{\"include\":[\"plugin:qa.prompts\"]}}}");I.Plan=QaPromptPlan(&O,&I,&N,&E);Group=MdoAgentsPluginGroup(I.Plan);
 QaPromptCheck(R,"explicit_prompt_requires_role_choice",Group&&strstr(SdkField(Group,"text"),"MANIFEST-GUIDE"));
 xvalue* Row=(xvalue*)xrtValueArrayGet(SdkGet(Group,"items"),1);SdkPut(Row,"plugin","plugin:forged");
 bool Selected=MdoAgentsPluginSelect(&O,&I,&N,NULL,Group,&E);Row=(xvalue*)xrtValueArrayGet(SdkGet(Group,"items"),1);
 QaPromptCheck(R,"role_edits_cannot_forge_plugin_provenance",Selected&&!strcmp(SdkField(Row,"plugin"),"plugin:qa.prompts"));
 xrtValueRelease(I.Plan);xrtValueRelease(I.Configuration);I.Configuration=SdkParse("{\"context\":{\"plugins.instructions\":{\"max_bytes\":8}}}");I.Plan=QaPromptPlan(&O,&I,&N,&E);Group=MdoAgentsPluginGroup(I.Plan);
 QaPromptCheck(R,"budget_drops_whole_fragment",Group&&!SdkField(Group,"text")[0]&&!strcmp(SdkField(xrtValueArrayGet(SdkGet(Group,"items"),1),"reason"),"budget"));
 xrtValueRelease(I.Plan);xrtValueRelease(I.Configuration);I.Configuration=SdkParse("{\"context\":{\"plugins.instructions\":false}}");I.Plan=QaPromptPlan(&O,&I,&N,&E);Group=MdoAgentsPluginGroup(I.Plan);
 QaPromptCheck(R,"disabled_source",Group&&!SdkField(Group,"text")[0]);
 xrtValueRelease(I.Plan);xrtValueRelease(I.Configuration);xrtMutexDestroy(O.InstanceLock);C->Tools=NULL;C->ToolCount=0;xworkAgentDestroy(Child);xworkAgentDestroy(Parent);xllmSessionDestroy(S1);xllmSessionDestroy(S2);
}
xvalue* QaProductContract(const xvalue* Params,xvfs V){
 (void)V;xvalue* R=xrtValueObject();char Error[512]={0};
 xvalue* Recipe=SdkParse("{\"prompts\":[{\"id\":\"guide\",\"text\":\"EXPLICIT-GUIDE\",\"activation\":\"explicit\"}]}"),*Files=SdkParse("[{\"path\":\"plugin.json\",\"bytes\":1}]");
 QaPromptCheck(R,"prompt_only_plugin",MdoPluginValidate(Recipe,Files,"qa",Error,sizeof(Error)));
 xvalue* P=(xvalue*)xrtValueArrayGet(SdkGet(Recipe,"prompts"),0);SdkPut(P,"activation","with-tools");
 QaPromptCheck(R,"implicit_global_rejected",!MdoPluginValidate(Recipe,Files,"qa",Error,sizeof(Error)));
 SdkPut(P,"activation","explicit");SdkPut(P,"file","../outside.md");
 QaPromptCheck(R,"path_and_dual_content_rejected",!MdoPluginValidate(Recipe,Files,"qa",Error,sizeof(Error)));xrtValueObjectRemove(P,XRT_STR_LITERAL("file"));
 SdkPut(P,"role","system");QaPromptCheck(R,"identity_escalation_rejected",!MdoPluginValidate(Recipe,Files,"qa",Error,sizeof(Error)));xrtValueObjectRemove(P,XRT_STR_LITERAL("role"));
 xvalue* TooLong=xrtValueString(xrtStrViewN("\xc3\x28",2));xrtValueObjectSet(P,XRT_STR_LITERAL("text"),TooLong);xrtValueRelease(TooLong);
 QaPromptCheck(R,"invalid_utf8_rejected",!MdoPluginValidate(Recipe,Files,"qa",Error,sizeof(Error)));SdkPut(P,"text","EXPLICIT-GUIDE");
 MdoModuleGeneration G={0};G.PluginEntry=true;G.Id="plugin.qa";G.Directory=SdkParse("{\"id\":\"plugin:qa.prompts\"}");memset(G.DirectoryDigest,'a',64);G.DirectoryDigest[64]=0;
 MdoModuleRegistrarContext Registrar={0};Registrar.Generation=&G;char Body[]="NATIVE-GUIDE";const char* Tools[]={"qa_tool"};
 mdo_prompt_contribution_v1 D={0};D.Size=sizeof(D);D.AbiVersion=MDO_MODULE_ABI_VERSION;D.Id="native";D.Text=Body;D.Targets=MDO_PROMPT_PRIMARY|MDO_PROMPT_SUBAGENT;D.AnyTools=Tools;D.AnyToolCount=1;D.Priority=10;
 bool Registered=MdoModulesRegistrarAddPrompt(&Registrar,&D,Error,sizeof(Error))==MDO_RESULT_OK;Body[0]='X';
 QaPromptCheck(R,"native_registration_copies",Registered&&!strcmp(SdkField(xrtValueArrayGet(G.Prompts,0),"text"),"NATIVE-GUIDE"));
 QaPromptCheck(R,"duplicate_native_rejected",MdoModulesRegistrarAddPrompt(&Registrar,&D,Error,sizeof(Error))!=MDO_RESULT_OK);
 SdkPlugins.Lock=xrtMutexCreate();SdkPlugins.Effective=SdkParse("[{\"id\":\"plugin:qa.prompts\",\"alias\":\"qa\",\"version\":\"1\",\"revision\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"prompt_contributions\":[{\"id\":\"declarative\",\"text\":\"MANIFEST-GUIDE\",\"activation\":\"explicit\"}]}]");
 MdoModuleGeneration* Modules[]={&G};MdoModuleCatalog C={0};C.Modules=Modules;C.ModuleCount=1;
 bool Collected=MdoModulesCollectPrompts(&C);QaPromptCheck(R,"shared_registry",Collected&&xrtValueCount(C.Prompts)==2);
 const xvalue* First=xrtValueArrayGet(C.Prompts,0);QaPromptCheck(R,"host_owned_provenance",First&&!strcmp(SdkField(First,"key"),"plugin:qa.prompts/declarative")&&!strcmp(SdkField(First,"plugin"),"plugin:qa.prompts")&&strlen(SdkField(First,"hash"))==64);
 QaPromptRoles(R,&C,&G,SdkField(Params,"root"));
 xrtValueRelease(G.Prompts);G.Prompts=NULL;xrtValueRelease(SdkPlugins.Effective);SdkPlugins.Effective=NULL;
 QaPromptCheck(R,"snapshot_owns_content",Collected&&!strcmp(SdkField(xrtValueArrayGet(C.Prompts,1),"text"),"NATIVE-GUIDE"));
 xrtValueRelease(C.Prompts);xrtValueRelease(G.Directory);xrtMutexDestroy(SdkPlugins.Lock);memset(&SdkPlugins,0,sizeof(SdkPlugins));
 xrtValueRelease(Recipe);xrtValueRelease(Files);return R;
}
'''

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--url',required=True);p.add_argument('--home',type=Path,required=True);p.add_argument('--project',default='default')
    args=p.parse_args();out=ROOT/'.build/prompt-contributions';fixture=out/'fixture';fixture.mkdir(parents=True,exist_ok=True)
    result=product(args,fixture,CONTRACT,out/'registry.json')
    assert result and all(value is True for value in result.values()),result
    print(f'Plugin prompt registry: {len(result)} native checks PASS')

if __name__=='__main__':main()
