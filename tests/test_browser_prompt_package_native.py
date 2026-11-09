"""Load real Browser Use ZIPs and check tool-gated prompt composition offline."""
import argparse
from pathlib import Path, PurePosixPath
import zipfile
from native_product_runner import product

ROOT=Path(__file__).resolve().parents[1]
CONTRACT=r'''
static void QaFlag(xvalue* R,cstr K,bool Ok){xrtValueObjectSetNew(R,xrtStrView(K),xrtValueBool(Ok));}
xvalue* QaProductContract(const xvalue* P,xvfs V){
 xvalue* R=xrtValueObject();bool Home=QaProductHome(SdkField(P,"root"),SdkField(P,"app"),V);
 bool Ready=Home&&MdoExtensionsInit()&&MdoConfigInit()&&MdoSkillManagerInit()&&MdoApplicationPluginsInit();xwork_error E={0};xwork_runtime_config RC;xworkRuntimeConfigInit(&RC);xwork_runtime* Runtime=Ready?xworkRuntimeCreate(&RC,&E):NULL;
 MdoModuleManagerAttachApplication(MdoApplicationApi());Ready=Ready&&Runtime&&MdoModuleManagerInit(Runtime);QaFlag(R,"real_browser_plugin_load",Ready);
 MdoModuleCatalog* C=Ready?MdoModuleCatalogSnapshot():NULL;
 QaFlag(R,"browser_tools_and_three_contributions",C&&C->ToolCount==17&&xrtValueCount(MdoModuleCatalogPromptContributions(C))==3);
 if(!C||C->ToolCount!=17||xrtValueCount(MdoModuleCatalogPromptContributions(C))!=3){
  SdkPutUInt(R,"tool_count",C?C->ToolCount:0);SdkPutUInt(R,"prompt_count",xrtValueCount(MdoModuleCatalogPromptContributions(C)));
  for(size_t J=0;J<32;J++)if(SdkPlugins.Plugins[J].Error[0])SdkPut(R,"plugin_error",SdkPlugins.Plugins[J].Error);
  MdoModuleDiagnostics* D=MdoModuleDiagnosticsSnapshot();for(size_t J=0;D&&J<D->Count;J++)SdkPut(R,"module_error",D->Entries[J].Message);MdoModuleDiagnosticsRelease(D);
 }
 if(!Ready){MdoModuleDiagnostics* D=MdoModuleDiagnosticsSnapshot();for(size_t I=0;D&&I<D->Count;I++)SdkPut(R,"module_error",D->Entries[I].Message);MdoModuleDiagnosticsRelease(D);goto done;}
 xllm_session_config SC;xllmSessionConfigInit(&SC);xllm_session* Session=xllmSessionCreate(&SC,NULL);
 xwork_agent_definition_config DC;xworkAgentDefinitionConfigInit(&DC);DC.sId="browser-prompt-qa";DC.sModel="offline";DC.bRegisterBuiltinTools=false;DC.bAutoSaveSession=false;DC.bAllowArtifactWrites=false;DC.eApprovalMode=XWORK_APPROVAL_AUTO;
 xwork_agent_definition* Definition=xworkAgentDefinitionCreate(&DC,&E);xwork_agent_options AO;xworkAgentOptionsInit(&AO);AO.pSession=Session;AO.sWorkspaceRoot=SdkField(P,"root");
 xwork_agent* Agent=Definition&&Session?xworkAgentCreateWithRuntime(Runtime,Definition,&AO,&E):NULL;
 MdoAgentOwner O={0};O.Modules=C;O.RootAgent=Agent;O.Workspace=(char*)SdkField(P,"root");O.InstanceLock=xrtMutexCreate();MdoAgentInstance I={0};I.Configuration=xrtValueObject();I.Definition.Flags=MDO_AGENT_MAIN;I.Plan=SdkParse("{\"sources\":[]}");
 xwork_agent_extension_context N={0};N.pAgent=Agent;bool Collected=Agent&&MdoAgentsPluginSource(&O,&I,&N,I.Plan,&E);xvalue* G=MdoAgentsPluginGroup(I.Plan);
 QaFlag(R,"all_available_capabilities_have_guides",Collected&&strstr(SdkField(G,"text"),"Browser Use tools")&&strstr(SdkField(G,"text"),"async JavaScript")&&strstr(SdkField(G,"text"),"browser_upload only"));
 xllm_request Q;xllmRequestInit(&Q);xllmRequestAddTextMessage(&Q,XLLM_ROLE_USER,"Inspect tabs only");xllmRequestAddTool(&Q,"browser_tabs","Browser tabs","{}",false);N.pRequest=&Q;MdoAgentPluginMessage B={0};
 bool Applied=Collected&&MdoAgentsPluginRequest(&O,&I,&N,&B,false,&E)&&MdoAgentsPluginRequest(&O,&I,&N,&B,true,&E);
 G=MdoAgentsPluginGroup(I.Plan);cstr Text=SdkField(G,"text");QaFlag(R,"read_only_tool_subset_gets_only_basics",Applied&&strstr(Text,"Browser Use tools")&&!strstr(Text,"async JavaScript")&&!strstr(Text,"browser_upload only"));
 QaFlag(R,"no_skill_body_preload",Applied&&!strstr(Text,"## Frames")&&strlen(Text)<2048);
 xrtFree(B.Text);xllmRequestUnit(&Q);xrtValueRelease(I.Plan);xrtValueRelease(I.Configuration);xrtMutexDestroy(O.InstanceLock);if(Agent)xworkAgentDestroy(Agent);if(Definition)xworkAgentDefinitionRelease(Definition);if(Session)xllmSessionDestroy(Session);
done:
 MdoModuleCatalogRelease(C);MdoModuleManagerUnit();if(Runtime)xworkRuntimeRelease(Runtime);MdoApplicationPluginsUnit();MdoSkillManagerUnit();MdoConfigUnit();MdoExtensionsUnit();if(Home)MdoHomeUnit();MdoModuleManagerAttachApplication(NULL);return R;
}
'''

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--url',required=True);p.add_argument('--home',type=Path,required=True);p.add_argument('--project',default='default');p.add_argument('--plugin',type=Path,required=True);p.add_argument('--skill',type=Path,required=True);args=p.parse_args()
    out=ROOT/'.build/prompt-contributions';fixture=out/'browser-package';fixture.mkdir(parents=True,exist_ok=True)
    for source,kind in [(args.skill,'skills'),(args.plugin,'plugins')]:
        target=fixture/kind/'browser-use';target.mkdir(parents=True,exist_ok=True)
        with zipfile.ZipFile(source) as archive:
            for member in archive.infolist():
                path=PurePosixPath(member.filename)
                if path.is_absolute() or '..' in path.parts or '\\' in member.filename or member.external_attr>>16&0o170000==0o120000:raise ValueError('Unsafe package member')
                dest=target.joinpath(*path.parts)
                if member.is_dir():dest.mkdir(parents=True,exist_ok=True)
                else:dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(archive.read(member))
    result=product(args,fixture,CONTRACT,out/'browser-package.json')
    assert result and all(value is True for value in result.values()),result
    print(f'Browser Use real package: {len(result)} prompt checks PASS')

if __name__=='__main__':main()
