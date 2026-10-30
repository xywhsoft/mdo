"""Dependency gates run before a model request or a real native tool callback."""
import argparse
import json
from pathlib import Path
import shutil
import uuid
from native_product_runner import product

ROOT=Path(__file__).resolve().parents[1]
CONTRACT=r'''
static void QaRuntimeFlag(xvalue* R,cstr K,bool B){xrtValueObjectSetNew(R,xrtStrView(K),xrtValueBool(B));}
static bool QaRuntimeTool(xllm_executor* Executor,xllm_executor_result* Result){static unsigned Serial;char Id[32];snprintf(Id,sizeof(Id),"dependency-tool-%u",++Serial);xllm_tool_call Call={0};Call.sId=Id;Call.sName="user.fixture";Call.sArgumentsJson="{}";xllm_executor_ctx C={0};C.uDeadline=XLLM_DEADLINE_NEVER;return Executor->pExecute(Executor->pUserData,&Call,&C,Result);}
xvalue* QaProductContract(const xvalue* Input,xvfs V){
    xvalue* R=xrtValueObject();char Why[512]={0};bool Home=QaProductHome(SdkField(Input,"root"),SdkField(Input,"app"),V),Ok=Home&&MdoExtensionsInit();
    xwork_runtime_config RC;xworkRuntimeConfigInit(&RC);xwork_error E={0};xwork_runtime* Runtime=Ok?xworkRuntimeCreate(&RC,&E):NULL;Ok=Ok&&Runtime&&MdoModuleManagerInit(Runtime);
    MdoModuleCatalog* Modules=Ok?MdoModuleCatalogSnapshot():NULL;MdoModuleAgentInfo Role={0};Role.Size=sizeof(Role);bool RoleFound=Modules&&MdoModuleCatalogAgentFind(Modules,"agent.consumer",&Role);
    QaRuntimeFlag(R,"typed_role_selection_resolves_real_provider",RoleFound&&Role.ExtensionId&&!strcmp(Role.ExtensionId,"agent:qa.consumer")&&Role.ToolCount==1&&!strcmp(Role.Tools[0],"user.fixture"));
    xllm_session_config SC;xllmSessionConfigInit(&SC);xllm_error LE={0};xllm_session* Session=xllmSessionCreate(&SC,&LE);
    xwork_agent_definition_config DC;xworkAgentDefinitionConfigInit(&DC);DC.sId="dependency-fixture";DC.sSystemPrompt="Fixture";DC.sModel="fixture-no-network";DC.bRegisterBuiltinTools=false;DC.bAutoSaveSession=false;DC.bAllowArtifactWrites=false;DC.eApprovalMode=XWORK_APPROVAL_AUTO;
    xwork_agent_definition* Definition=xworkAgentDefinitionCreate(&DC,&E);xwork_agent_options AO;xworkAgentOptionsInit(&AO);AO.pSession=Session;AO.sWorkspaceRoot=SdkField(Input,"root");xwork_agent* Agent=Runtime&&Definition&&Session?xworkAgentCreateWithRuntime(Runtime,Definition,&AO,&E):NULL;
    MdoAgentOwner Owner={0};Owner.Modules=Modules;Owner.Workspace=(char*)SdkField(Input,"root");snprintf(Owner.ProductProject,sizeof(Owner.ProductProject),"default");size_t Count=0;uint64 Generation=0;
    bool Allowed=Agent&&RoleFound&&MdoAgentsApplyToolPolicy(Agent,&Role,XWORK_TOOL_EFFECT_READ,Owner.Workspace,&Count,&Generation,&E)&&MdoAgentsCheckDependencies(&Owner,&Role,Agent,&E);
    QaRuntimeFlag(R,"non_model_entry_requires_loaded_tool_provider",MdoExtensionsRuntimeCheck("agent:qa.consumer",Owner.Workspace,"default",NULL,false,Why));
    QaRuntimeFlag(R,"required_capabilities_are_checked_without_model_io",Allowed&&Count==1);
    unsigned* CallbackCount=NULL;for(size_t I=0;Modules&&I<Modules->ToolCount;I++)if(!strcmp(Modules->Tools[I]->Id,"user.fixture"))CallbackCount=tcc_get_symbol(Modules->Tools[I]->Owner->Tcc,"QaFixtureCalls");
    xllm_executor Executor={0};bool Bound=Allowed&&xworkExecutorBind(&Executor,Agent,&E);xllm_executor_result Output={0};bool Ran=Bound&&QaRuntimeTool(&Executor,&Output);QaRuntimeFlag(R,"ready_native_tool_executes",Ran&&Output.bSuccess&&Output.sContent&&strstr(Output.sContent,"LOCAL TOOL CALLBACK RESULT"));
    xvalue* Config=RoleFound?xrtJsonParse(xrtStrView(Role.ConfigurationJson)):NULL;MdoModuleAgentInfo Candidate=Role;char* Text=NULL;
    xvalue* Sources=SdkParse("{\"knowledge\":{\"enabled\":false}}");xrtValueObjectSet(Config,XRT_STR_LITERAL("context"),Sources);xrtValueRelease(Sources);Text=xrtJsonStringify(Config,false,NULL);Candidate.ConfigurationJson=Text;
    QaRuntimeFlag(R,"required_knowledge_cannot_be_hidden_by_context_policy",Allowed&&!MdoAgentsCheckDependencies(&Owner,&Candidate,Agent,&E));xrtFree(Text);xrtValueObjectRemove(Config,XRT_STR_LITERAL("context"));
    xvalue* Refs=xrtValueObjectGet(Config,XRT_STR_LITERAL("resolved_references"));xvalue* Ref=xrtValueArrayGet(Refs,0);xrtValueObjectSetNew(Ref,XRT_STR_LITERAL("explicit_export"),xrtValueBool(false));Text=xrtJsonStringify(Config,false,NULL);Candidate.ConfigurationJson=Text;
    QaRuntimeFlag(R,"multiple_exports_require_explicit_selection",Allowed&&!MdoAgentsCheckDependencies(&Owner,&Candidate,Agent,&E)&&strstr(E.sMessage,"explicit export"));xrtFree(Text);xrtValueRelease(Config);
    bool Missing=MdoHomeRemove("knowledge/base/index.md",false);QaRuntimeFlag(R,"missing_required_index_blocks_role",Missing&&!MdoAgentsCheckDependencies(&Owner,&Role,Agent,&E));
    unsigned BeforeCalls=CallbackCount?*CallbackCount:0;Ran=Bound&&QaRuntimeTool(&Executor,&Output);QaRuntimeFlag(R,"missing_index_stops_actual_callback",CallbackCount&&BeforeCalls==1&&*CallbackCount==BeforeCalls&&(!Ran||!Output.bSuccess));
    bool Restored=MdoHomeAtomicWrite("knowledge/base/index.md","# restored\n",11,false);QaRuntimeFlag(R,"dependency_restoration_reenables_role",Restored&&MdoAgentsCheckDependencies(&Owner,&Role,Agent,&E));
    bool Scoped=MdoHomeAtomicWrite(".mdo/tools.json","{\"tool:qa.git\":false}",21,false)&&MdoAgentsApplyToolPolicy(Agent,&Role,XWORK_TOOL_EFFECT_READ,Owner.Workspace,&Count,&Generation,&E);
    QaRuntimeFlag(R,"project_selection_does_not_grant_required_tool",Scoped&&Count==0&&!MdoAgentsCheckDependencies(&Owner,&Role,Agent,&E));
    MdoHomeRemove(".mdo/tools.json",false);QaRuntimeFlag(R,"parent_policy_does_not_get_reopened_by_dependencies",!MdoAgentsCheckDependencies(&Owner,&Role,Agent,&E));
    xvalue* References=MdoKnowledgeReferencesForWorkspace(Owner.Workspace);bool Base=false,Child=false;for(size_t I=0;I<xrtValueCount(References);I++){cstr Id=MdoExtensionText(xrtValueArrayGet(References,I),"id",MDO_EXTENSION_REF_BYTES);if(Id&&!strcmp(Id,"knowledge:qa.base"))Base=true;if(Id&&!strcmp(Id,"knowledge:qa.child"))Child=true;}QaRuntimeFlag(R,"knowledge_entry_contains_logical_id_and_actual_mount",Base&&Child&&xrtValueCount(References)==2);xrtValueRelease(References);
    char Invalid[512];xvalue* Bad=MdoAgentConfigResolveAt("consumer","{\"name\":\"Consumer\",\"extends\":\"agent.base\",\"prompt\":\"x\"}",true,"agents/consumer",Invalid,sizeof(Invalid));QaRuntimeFlag(R,"legacy_parent_alias_is_not_guessed",Bad==NULL);xrtValueRelease(Bad);
    MdoModuleDiagnostics* Diagnostics=MdoModuleDiagnosticsSnapshot();xvalue* Errors=xrtValueArray();for(size_t I=0;Diagnostics&&I<Diagnostics->Count;I++)xrtValueArrayAppendNew(Errors,xrtValueString(xrtStrView(Diagnostics->Entries[I].Message)));xrtValueObjectSet(R,XRT_STR_LITERAL("diagnostics"),Errors);xrtValueRelease(Errors);MdoModuleDiagnosticsRelease(Diagnostics);
    if(Bound)xworkExecutorUnbind(&Executor);if(Agent)xworkAgentDestroy(Agent);xworkAgentDefinitionRelease(Definition);if(Session)xllmSessionDestroy(Session);MdoModuleCatalogRelease(Modules);MdoModuleManagerUnit();QaRuntimeFlag(R,"unloaded_tool_provider_blocks_non_model_entry",!MdoExtensionsRuntimeCheck("agent:qa.consumer",Owner.Workspace,"default",NULL,false,Why));if(Runtime)xworkRuntimeRelease(Runtime);MdoExtensionsUnit();if(Home)MdoHomeUnit();SdkPut(R,"error",E.sMessage);return R;
}
'''


def main():
    p=argparse.ArgumentParser();p.add_argument('--url',required=True);p.add_argument('--home',type=Path,required=True);p.add_argument('--project',default='default');a=p.parse_args();out=ROOT/'.build/extension-dependencies/runtime';out.mkdir(parents=True,exist_ok=True);fixture=out/('home-'+uuid.uuid4().hex[:10]);fixture.mkdir()
    def directory(kind,name,identity,entry,content,deps=()):
        folder=fixture/kind/name;folder.mkdir(parents=True);(folder/'extension.json').write_text(json.dumps(dict(kind=kind,id=identity,name=name,alias='user.fixture' if kind=='tools' else name,entry=entry,platforms=['any'],dependencies=[dict(id=ref) for ref in deps])),encoding='utf-8');(folder/entry).write_text(content if isinstance(content,str) else json.dumps(content),encoding='utf-8');return folder
    tool=(ROOT/'tests/fixtures/modules/local-tool.c').read_text(encoding='utf-8').replace('#include "mdo/module.h"','#include "mdo/module.h"\nunsigned QaFixtureCalls;').replace('(void)UserData;', '++QaFixtureCalls;(void)UserData;').replace('    return Registrar->AddTool(Registrar->Context, &Tool, Error, Capacity);','    if(Registrar->AddTool(Registrar->Context,&Tool,Error,Capacity)!=MDO_RESULT_OK)return MDO_RESULT_ERROR;\n    mdo_tool_v1 Extra=Tool;Extra.Id="user.fixture-extra";return Registrar->AddTool(Registrar->Context,&Extra,Error,Capacity);')
    directory('tools','git','tool:qa.git','tool.c',tool,['knowledge:qa.base'])
    for name,deps in [('base',()),('child',('knowledge:qa.base',))]:
        root=directory('knowledge',name,'knowledge:qa.'+name,'knowledge.json',dict(format='mdo.knowledge.v1',name=name,description='Native knowledge fixture',index='index.md',prompt='Read {$path}/index.md',inject_prompt=True),deps);(root/'index.md').write_text('# Index\n',encoding='utf-8')
    directory('agents','consumer','agent:qa.consumer','agent.json',dict(name='Consumer',description='Typed dependency fixture',prompt='Do the task',tools=[dict(ref='tool:qa.git',export='user.fixture')]),['tool:qa.git','knowledge:qa.child'])
    passed=False
    try:
        result=product(a,fixture,CONTRACT,out/'result.json');assert result and 'compile_error' not in result,result
        for name,value in result.items():
            if name not in ('error','diagnostics'):assert value is True,(name,result)
        passed=True;print('PASS',len(result)-2,'native runtime dependency contracts')
    finally:
        if passed and fixture.resolve().parent==out.resolve():shutil.rmtree(fixture)

if __name__=='__main__':main()
