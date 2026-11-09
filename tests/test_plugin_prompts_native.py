"""Compile current product sources in an existing host; no app/runtime copies."""
import argparse
from pathlib import Path
from native_product_runner import product

ROOT = Path(__file__).resolve().parents[1]
CONTRACT = r'''
static void QaPromptCheck(xvalue* R,cstr Key,bool Ok){xrtValueObjectSetNew(R,xrtStrView(Key),xrtValueBool(Ok));}
xvalue* QaProductContract(const xvalue* Params,xvfs V){
 (void)Params;(void)V;xvalue* R=xrtValueObject();char Error[512]={0};
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
