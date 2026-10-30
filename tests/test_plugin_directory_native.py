"""Actual plugin C entry, resources/overlays, conflicts, ZIP and directory pins."""
import argparse
import json
from pathlib import Path
import shutil
import uuid
import zipfile
from native_product_runner import product

ROOT = Path(__file__).resolve().parents[1]
ID = '0123456789ABCDEFGHJKMNPQRSTVWXYZ' * 3
CONTRACT = r'''
static char QaOutput[100];
static bool QaWrite(void* U,cstr T){(void)U;snprintf(QaOutput,sizeof(QaOutput),"%s",T);return true;}
static bool QaSuccess(void* U,bool B){(void)U;return B;}
static bool QaExecute(MdoModuleCatalog* Catalog,cstr Expected){
 if(!Catalog||Catalog->ToolCount!=1)return false;MdoModuleToolBinding* T=Catalog->Tools[0];mdo_tool_context_v1 C={0};C.Size=sizeof(C);C.AbiVersion=MDO_MODULE_ABI_VERSION;C.Application=&T->Owner->Application;
 mdo_result_writer_v1 W={0};W.Size=sizeof(W);W.AbiVersion=MDO_MODULE_ABI_VERSION;W.WriteText=QaWrite;W.SetSuccess=QaSuccess;char E[1024];
 return T->Execute(T->UserData,&C,"{}",&W,E,sizeof(E))==MDO_RESULT_OK&&!strcmp(QaOutput,Expected)&&C.Application->DirectoryId&&!strcmp(C.Application->DirectoryId,"0123456789ABCDEFGHJKMNPQRSTVWXYZ");
}
static void QaBool(xvalue* R,cstr K,bool B){xrtValueObjectSetNew(R,xrtStrView(K),xrtValueBool(B));}
xvalue* QaProductContract(const xvalue* P,xvfs V){
 char Error[512]={0};xvalue* R=xrtValueObject();bool Home=QaProductHome(SdkField(P,"root"),SdkField(P,"app"),V);
 char* PluginMode=xrtEnvGet("MDO_PLUGINS_DISABLED");xrtEnvSet("MDO_PLUGINS_DISABLED","0");
 bool Started=Home&&MdoExtensionsInit()&&MdoExtensionsSetEnabled("plugins","other",false,Error)&&MdoExtensionsSetEnabled("plugins","fresh",false,Error)&&MdoApplicationPluginsInit();
 xvalue* Enable=SdkParse("{\"kind\":\"plugins\",\"id\":\"plugin:qa.fresh\",\"action\":\"enable\",\"enabled\":true}"),*Pending=NULL;
 bool Waiting=Started&&MdoExtensionsManage(Enable,NULL,NULL,&Pending,Error)&&MdoExtOpBool(Pending,"restart_required",false)&&MdoExtOpBool(Pending,"pending",false)&&!MdoExtOpBool(Pending,"runtime_ready",true);
 QaBool(R,"native_activation_reports_restart",Waiting);xrtValueRelease(Pending);Pending=NULL;xrtValueObjectSetNew(Enable,XRT_STR_LITERAL("enabled"),xrtValueBool(false));MdoExtensionsManage(Enable,NULL,NULL,&Pending,Error);xrtValueRelease(Pending);xrtValueRelease(Enable);MdoApplicationPluginsUnit();
 bool Ok=Home&&MdoExtensionsInit()&&MdoExtensionsSetEnabled("plugins","other",false,Error)&&MdoApplicationPluginsInit();QaBool(R,"loaded",Ok&&!MdoApplicationPluginsRecovery());
 size_t N=0;bytes Overlay=xrtVfsReadAllLimit(V,"/app/web/qa-overlay.txt",1024,&N);QaBool(R,"physical_directory_overlay",Overlay&&N==14&&!memcmp(Overlay,"plugin overlay",14));xrtFree(Overlay);
 xvalue* Owner=MdoApplicationPluginFileOwner("web/qa-overlay.txt");QaBool(R,"credits",Owner&&!strcmp(SdkField(Owner,"id"),"plugin:qa.main")&&!strcmp(SdkField(Owner,"author"),"QA author"));xrtValueRelease(Owner);
 xwork_runtime_config Config;xworkRuntimeConfigInit(&Config);xwork_error E;xworkErrorInit(&E);xwork_runtime* Runtime=Ok?xworkRuntimeCreate(&Config,&E):NULL;MdoModuleManagerAttachApplication(MdoApplicationApi());
 Ok=Ok&&Runtime&&MdoModuleManagerInit(Runtime);MdoModuleCatalog* Catalog=Ok?MdoModuleCatalogSnapshot():NULL;QaBool(R,"real_native_entry",Ok&&QaExecute(Catalog,"17:plugin resource"));QaBool(R,"plugin_registers_roles",Catalog&&Catalog->AgentCount==1&&!strcmp(Catalog->Agents[0]->Id,"agent.plugin-fixture"));xvalue* Meta=MdoExtensionsRead("plugins","0123456789ABCDEFGHJKMNPQRSTVWXYZ",Error);QaBool(R,"native_macro_preserves_display_version",Catalog&&Catalog->ToolCount==1&&Meta&&!strcmp(Catalog->Tools[0]->Owner->Version,SdkField(Meta,"version")));xrtValueRelease(Meta);MdoModuleCatalogRelease(Catalog);
 xvalue* Removed=NULL;QaBool(R,"active_directory_pinned",!MdoExtensionsRemove("plugins","0123456789ABCDEFGHJKMNPQRSTVWXYZ",&Removed,Error));xrtValueRelease(Removed);
 xvalue* Changes=SdkParse("[{\"id\":\"plugin:qa.other\",\"enabled\":true}]");mdo_error_v1 Failure={0};xvalue* Plan=SdkPluginPlan(SdkPlugins.Config,Changes,NULL,&Failure);bool Allowed=true;xrtValueGetBool(SdkGet(Plan,"allowed"),&Allowed);QaBool(R,"exclusive_conflict",Plan&&!Allowed);xrtValueRelease(Plan);xrtValueRelease(Changes);
 char Hash[65];uint64 Bytes;QaBool(R,"whole_directory_zip",MdoExtensionsPack("plugins","0123456789ABCDEFGHJKMNPQRSTVWXYZ","data/extension-tasks/export/plugin.zip",NULL,Hash,&Bytes,Error));
 bool Edited=MdoHomeAtomicWrite("plugins/0123456789ABCDEFGHJKMNPQRSTVWXYZ/helper.h","#define QA_NUMBER 29\n",20,false);
 bool Rejected=Edited&&!MdoModuleManagerReload();Catalog=MdoModuleCatalogSnapshot();QaBool(R,"changed_native_requires_restart",Rejected&&QaExecute(Catalog,"17:plugin resource"));MdoModuleCatalogRelease(Catalog);
 MdoModuleManagerUnit();MdoApplicationPluginsUnit();Ok=MdoApplicationPluginsInit()&&MdoModuleManagerInit(Runtime);Catalog=Ok?MdoModuleCatalogSnapshot():NULL;QaBool(R,"header_after_restart",Ok&&QaExecute(Catalog,"29:plugin resource"));MdoModuleCatalogRelease(Catalog);
 MdoModuleDiagnostics* D=MdoModuleDiagnosticsSnapshot();xvalue* Messages=xrtValueArray();for(size_t I=0;D&&I<D->Count;I++)xrtValueArrayAppendNew(Messages,xrtValueString(xrtStrView(D->Entries[I].Message)));xrtValueObjectSetNew(R,XRT_STR_LITERAL("diagnostics"),Messages);MdoModuleDiagnosticsRelease(D);
 xvalue* Delete=xrtValueObject();SdkPut(Delete,"kind","plugins");SdkPut(Delete,"id","0123456789ABCDEFGHJKMNPQRSTVWXYZ");SdkPut(Delete,"action","remove");MdoExtensionsFingerprint("plugins","0123456789ABCDEFGHJKMNPQRSTVWXYZ",Hash,NULL,Error);SdkPut(Delete,"expected_revision",Hash);
 Removed=NULL;bool Scheduled=MdoExtensionsManage(Delete,NULL,NULL,&Removed,Error)&&MdoExtOpBool(Removed,"pending_remove",false)&&!MdoExtOpBool(Removed,"removed",true);QaBool(R,"native_uninstall_deferred_honestly",Scheduled);xrtValueRelease(Delete);xrtValueRelease(Removed);
 MdoModuleManagerUnit();MdoModuleManagerAttachApplication(NULL);MdoApplicationPluginsUnit();if(Runtime)xworkRuntimeRelease(Runtime);
 bool Exists=false;xfileinfo Info;QaBool(R,"whole_directory_removed",MdoExtensionsRecover(Error)&&MdoHomeExternalStat("plugins/0123456789ABCDEFGHJKMNPQRSTVWXYZ",&Exists,&Info)&&!Exists);
 bool Frontend=MdoExtensionsSetEnabled("plugins","other",true,Error)&&MdoApplicationPluginsInit();const xvalue* Other=SdkPluginFind(SdkPlugins.Config,"plugin:qa.other");
 xvalue* Input=SdkParse("{\"client\":\"qa-client\",\"page\":\"qa-page\",\"id\":\"plugin:qa.other\"}"),*Reply=NULL;SdkPut(Input,"revision",SdkField(Other,"revision"));
 Frontend=Frontend&&SdkPluginFrontendCall("plugin.frontend.enter",Input,&Reply,&Failure);xrtValueRelease(Reply);Reply=NULL;
 xvalue* Before=NULL,*Desired=NULL;Changes=SdkParse("[{\"id\":\"plugin:qa.other\",\"enabled\":false}]");bool Begun=MdoApplicationPluginsStoreBegin(&Before,Error,sizeof(Error));
 Plan=Begun?SdkPluginPlan(Before,Changes,&Desired,&Failure):NULL;
 Frontend=Frontend&&Plan&&SdkPluginConfigSave(Desired)&&MdoApplicationPluginsStorePublish(Desired,Error,sizeof(Error))&&SdkPlugins.Pending;MdoApplicationPluginsStoreEnd();
 Removed=NULL;Frontend=Frontend&&!MdoExtensionsRemove("plugins","other",&Removed,Error);xrtValueRelease(Removed);Removed=NULL;
 Frontend=Frontend&&SdkPluginFrontendCall("plugin.frontend.leave",Input,&Reply,&Failure)&&!SdkPlugins.Pending&&MdoExtensionsRemove("plugins","other",&Removed,Error);QaBool(R,"frontend_cleanup_before_uninstall",Frontend);
 xrtValueRelease(Removed);xrtValueRelease(Input);xrtValueRelease(Reply);xrtValueRelease(Changes);xrtValueRelease(Before);xrtValueRelease(Desired);xrtValueRelease(Plan);MdoApplicationPluginsUnit();
 QaBool(R,"restart_no_resurrection",MdoApplicationPluginsInit()&&xrtValueCount(SdkPlugins.Config)==1);MdoApplicationPluginsUnit();SdkPut(R,"error",Error);MdoExtensionsUnit();if(Home)MdoHomeUnit();if(PluginMode){xrtEnvSet("MDO_PLUGINS_DISABLED",PluginMode);xrtFree(PluginMode);}else xrtEnvRemove("MDO_PLUGINS_DISABLED");return R;
}
'''


def main():
    p = argparse.ArgumentParser(description=__doc__); p.add_argument('--url', required=True); p.add_argument('--home', type=Path, required=True); p.add_argument('--project', default='default'); a = p.parse_args()
    out = ROOT / '.build/plugin-directory-native'; out.mkdir(parents=True, exist_ok=True); fixture = out / ('home-' + uuid.uuid4().hex[:12]); directory = fixture / 'plugins' / ID; directory.mkdir(parents=True)
    tool = (ROOT / 'tests/fixtures/modules/local-tool.c').read_text()
    tool = tool.replace('#include "mdo/module.h"', '#include "mdo/module.h"\n#include "mdo/sdk.h"\n#include <xsbase.h>\n#include <stdio.h>\n#include "helper.h"')
    tool = tool.replace('"user.tools.fixture"', 'MDO_PLUGIN_ID').replace('"1.0.0"', 'MDO_PLUGIN_VERSION')
    tool = tool.replace('    return Writer->WriteText(Writer->Context, "LOCAL TOOL CALLBACK RESULT") &&', '''    char* Path=xrtPathJoin(Context->Application->ExtensionRoot,"resource.txt");size_t N=0;char* Text=(char*)xrtFileReadAllLimit(Path,1024,&N);xrtFree(Path);char Output[100];if(!Text)return MDO_RESULT_ERROR;snprintf(Output,sizeof(Output),"%d:%s",QA_NUMBER,Text);xrtFree(Text);
    return Writer->WriteText(Writer->Context,Output) &&''')
    tool = tool.replace('    return Registrar->AddTool', '    mdo_agent_v1 A={0};A.Size=sizeof(A);A.AbiVersion=1;A.Id="agent.plugin-fixture";A.Name="Plugin role";A.Description="Native plugin role";A.SystemPrompt="Plugin instructions";A.Flags=MDO_AGENT_MAIN;A.AllowedEffects=MDO_TOOL_EFFECT_READ;A.MaxTurns=8;A.TimeoutMilliseconds=1000;A.MaxFinalBytes=1024;A.MaxDepth=1;\n    if(Registrar->AddAgent(Registrar->Context,&A,Error,Capacity)!=MDO_RESULT_OK)return MDO_RESULT_ERROR;\n    return Registrar->AddTool')
    (directory / 'extension.json').write_text(json.dumps(dict(kind='plugins', id='plugin:qa.main', name='QA plugin', alias='qa-plugin', entry='plugin.json', platforms=['any'], author='QA author',version='2026.10 \"directory\" \\ build')), encoding='utf-8')
    recipe = dict(entry=dict(native='plugin.c', frontend='frontend.js'), overrides=[dict(source='overlay.txt', target='web/qa-overlay.txt')], exclusive=['qa-layout'])
    for name, content in {'plugin.json': json.dumps(recipe), 'plugin.c': tool, 'helper.h': '#define QA_NUMBER 17\n', 'resource.txt': 'plugin resource', 'frontend.js': 'export function activate(){}', 'overlay.txt': 'plugin overlay'}.items(): (directory / name).write_text(content, encoding='utf-8')
    (directory / '.empty').mkdir()
    fresh=fixture/'plugins/fresh';fresh.mkdir();(fresh/'extension.json').write_text(json.dumps(dict(kind='plugins',id='plugin:qa.fresh',name='Fresh',alias='fresh',entry='plugin.json',platforms=['any'])))
    (fresh/'plugin.json').write_text(json.dumps(dict(entry=dict(native='native.c'))));(fresh/'native.c').write_text('/* Native selection must wait for restart; never compiled by this test. */')
    other = fixture / 'plugins/other'; other.mkdir(); (other / 'extension.json').write_text(json.dumps(dict(kind='plugins', id='plugin:qa.other', name='Other', alias='other', entry='plugin.json', platforms=['any'])))
    (other / 'plugin.json').write_text(json.dumps(dict(entry=dict(frontend='entry.js'), exclusive=['qa-layout']))); (other / 'entry.js').write_text('export function activate(){}')
    try:
        result = product(a, fixture, CONTRACT.replace('0123456789ABCDEFGHJKMNPQRSTVWXYZ',ID), out / 'result.json')
        for key in ('native_activation_reports_restart', 'loaded', 'physical_directory_overlay', 'credits', 'real_native_entry', 'plugin_registers_roles', 'native_macro_preserves_display_version', 'active_directory_pinned', 'exclusive_conflict', 'whole_directory_zip', 'changed_native_requires_restart', 'header_after_restart', 'native_uninstall_deferred_honestly', 'whole_directory_removed', 'frontend_cleanup_before_uninstall', 'restart_no_resurrection'): assert result.get(key), (key, result)
        with zipfile.ZipFile(fixture / 'data/extension-tasks/export/plugin.zip') as z:
            assert z.testzip() is None and 'plugin.c' in z.namelist() and '.empty/' in z.namelist()
            assert not any('revisions/' in name or name.startswith('files/') for name in z.namelist())
            assert '#define QA_NUMBER 17' in z.read('helper.h').decode()
        assert not directory.exists(); print(json.dumps(result, ensure_ascii=False))
    finally:
        if fixture.resolve().parent == out.resolve() and fixture.name.startswith('home-'): shutil.rmtree(fixture)


if __name__ == '__main__': main()
