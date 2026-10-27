"""Exercise actual Agent, SubAgent and Agent-tool directory loaders and code."""
import argparse
import json
from pathlib import Path
import shutil
import uuid
from native_product_runner import product

ROOT = Path(__file__).resolve().parents[1]
CONTRACT = r'''
static char QaWritten[1024];
static bool QaWriteText(void* U,cstr Text){(void)U;snprintf(QaWritten,sizeof(QaWritten),"%s",Text);return true;}
static bool QaSuccess(void* U,bool Value){(void)U;return Value;}
static bool QaTool(MdoModuleCatalog* Catalog){
 if(!Catalog||Catalog->ToolCount!=1)return false;MdoModuleToolBinding* T=Catalog->Tools[0];
 mdo_tool_context_v1 C={0};C.Size=sizeof(C);C.AbiVersion=MDO_MODULE_ABI_VERSION;C.Application=&T->Owner->Application;
 mdo_result_writer_v1 W={0};W.Size=sizeof(W);W.AbiVersion=MDO_MODULE_ABI_VERSION;W.WriteText=QaWriteText;W.SetSuccess=QaSuccess;
 char Error[1024]={0};return T->Execute(T->UserData,&C,"{}",&W,Error,sizeof(Error))==MDO_RESULT_OK&&!strcmp(QaWritten,"tool resource");
}
static bool QaRole(MdoModuleCatalog* Catalog,cstr Id,int64 Expected){
 mdo_agent_context_v1 C={0};C.Size=sizeof(C);C.AbiVersion=MDO_MODULE_ABI_VERSION;C.AgentId=Id;C.WorkspaceRoot="";
 C.ProjectId="";C.SessionId="";C.State=xrtValueObject();xwork_error Error;xworkErrorInit(&Error);
 bool Ok=MdoModuleCatalogAgentDrive(Catalog,Id,&C,&Error)==XWORK_RESULT_OK;int64 N=0;
 Ok=Ok&&xrtValueGetInt(xrtValueObjectGet(C.State,XRT_STR_LITERAL("value")),&N)&&N==Expected;xrtValueRelease(C.State);return Ok;
}
xvalue* QaProductContract(const xvalue* P,xvfs V){
 xvalue* R=xrtValueObject();cstr Root=SdkField(P,"root"),App=SdkField(P,"app");char Error[512]={0};
 bool Home=QaProductHome(Root,App,V),Ok=Home&&MdoExtensionsInit()&&MdoExtensionsRecover(Error)&&MdoExtensionsSeedBuiltins(Error);
 xwork_runtime_config Config;xworkRuntimeConfigInit(&Config);xwork_error Work;xworkErrorInit(&Work);
 xwork_runtime* Runtime=Ok?xworkRuntimeCreate(&Config,&Work):NULL;MdoModuleManagerAttachApplication(MdoApplicationApi());
 xrtValueObjectSetNew(R,XRT_STR_LITERAL("home_ready"),xrtValueBool(Home));xrtValueObjectSetNew(R,XRT_STR_LITERAL("seeded"),xrtValueBool(Ok));
 Ok=Ok&&Runtime&&MdoModuleManagerInit(Runtime);xrtValueObjectSetNew(R,XRT_STR_LITERAL("loaded"),xrtValueBool(Ok));
 if(!Ok&&xrtGetError())SdkPut(R,"load_error",xrtErrorMessage(xrtGetError()));
 MdoModuleCatalog* Catalog=Ok?MdoModuleCatalogSnapshot():NULL;
 xrtValueObjectSetNew(R,XRT_STR_LITERAL("tool_executed_with_resource"),xrtValueBool(Ok&&QaTool(Catalog)));
 xrtValueObjectSetNew(R,XRT_STR_LITERAL("agent_driver"),xrtValueBool(Ok&&QaRole(Catalog,"agent.craftsman",17)));
 xrtValueObjectSetNew(R,XRT_STR_LITERAL("subagent_driver"),xrtValueBool(Ok&&QaRole(Catalog,"subagent.worker",17)));
 MdoModuleAgentBinding* Child=MdoModulesFindAgent(Catalog,"agent.craftsman");
 xvalue* Definition=Child&&Child->ConfigurationJson?xrtJsonParse(xrtStrView(Child->ConfigurationJson)):NULL;
 const xvalue* Context=MdoAgentConfigGet(Definition,"context");
 cstr ParentOrigin=MdoAgentConfigText(MdoAgentConfigGet(Context,"custom.parent"),"origin"),ChildOrigin=MdoAgentConfigText(MdoAgentConfigGet(Context,"custom.child"),"origin");
 bool Origins=ParentOrigin&&ChildOrigin&&!strcmp(ParentOrigin,"agents/parent-directory")&&!strcmp(ChildOrigin,"agents/0123456789ABCDEFGHJKMNPQRSTVWXYZ")&&strstr(Child->SystemPrompt,"child prompt file");
 if(!Origins){SdkPut(R,"parent_origin",ParentOrigin);SdkPut(R,"child_origin",ChildOrigin);SdkPut(R,"child_prompt",Child?Child->SystemPrompt:"");}
 xrtValueObjectSetNew(R,XRT_STR_LITERAL("inherited_and_own_resource_roots"),xrtValueBool(Origins));xrtValueRelease(Definition);
 char Hash[65]={0};MdoModuleGeneration* Owner=Child?Child->Owner:NULL;if(Owner)snprintf(Hash,sizeof(Hash),"%s",Owner->SourceHash);
 MdoModuleCatalogRelease(Catalog);Catalog=NULL;
 bool Edited=Ok&&MdoHomeAtomicWrite("agents/0123456789ABCDEFGHJKMNPQRSTVWXYZ/helper.h","#define QA_NUMBER 29\n",20,false)&&
   MdoHomeAtomicWrite("subagents/worker-directory/helper.h","#define QA_NUMBER 29\n",20,false)&&MdoModuleManagerReload();
 Catalog=Edited?MdoModuleCatalogSnapshot():NULL;Child=MdoModulesFindAgent(Catalog,"agent.craftsman");
 Edited=Edited&&Child&&strcmp(Hash,Child->Owner->SourceHash)&&QaRole(Catalog,"agent.craftsman",29)&&QaRole(Catalog,"subagent.worker",29);
 xrtValueObjectSetNew(R,XRT_STR_LITERAL("auxiliary_header_reloaded"),xrtValueBool(Edited));MdoModuleCatalogRelease(Catalog);Catalog=NULL;
 bool Disabled=MdoExtensionsSetEnabled("agents","0123456789ABCDEFGHJKMNPQRSTVWXYZ",false,Error)&&MdoModuleManagerReload();
 Catalog=Disabled?MdoModuleCatalogSnapshot():NULL;
 Disabled=Disabled&&!MdoModulesFindAgent(Catalog,"agent.craftsman");MdoModuleCatalogRelease(Catalog);
 xrtValueObjectSetNew(R,XRT_STR_LITERAL("whole_role_disable"),xrtValueBool(Disabled));
 const xerror* Native=xrtGetError();if(Native)SdkPut(R,"native_error",xrtErrorMessage(Native));
 MdoModuleDiagnostics* D=MdoModuleDiagnosticsSnapshot();xvalue* Messages=xrtValueArray();
 for(size_t I=0;D&&I<D->Count;I++)xrtValueArrayAppendNew(Messages,xrtValueString(xrtStrView(D->Entries[I].Message)));
 xrtValueObjectSetNew(R,XRT_STR_LITERAL("diagnostics"),Messages);MdoModuleDiagnosticsRelease(D);
 MdoModuleManagerUnit();MdoModuleManagerAttachApplication(NULL);if(Runtime)xworkRuntimeRelease(Runtime);
 xvalue* Removed=NULL;bool Uninstalled=MdoExtensionsRemove("agents","0123456789ABCDEFGHJKMNPQRSTVWXYZ",&Removed,Error);xrtValueRelease(Removed);
 xrtValueObjectSetNew(R,XRT_STR_LITERAL("uninstalled_after_runtime_release"),xrtValueBool(Uninstalled));
 SdkPut(R,"error",Error);MdoExtensionsUnit();if(Home)MdoHomeUnit();return R;
}
'''

ROLE = r'''
#include "mdo/module.h"
#include "mdo/sdk.h"
#include <xsbase.h>
#include <stdio.h>
#include "helper.h"
static mdo_result Drive(void* U,const mdo_agent_context_v1* C,char* Error,size_t Capacity){
 (void)U;xvalue* Current=NULL;mdo_error_v1 Failure={0};
 if(mdoCall(C->Application,"extension.current",NULL,&Current,&Failure)!=MDO_RESULT_OK){snprintf(Error,Capacity,"%s",Failure.Message);return MDO_RESULT_ERROR;}
 xstrview Kind={0};bool Ok=xrtValueGetString(xrtValueObjectGet(Current,XRT_STR_LITERAL("kind")),&Kind)&&Kind.Size;
 xrtValueRelease(Current);xrtValueObjectSetNew(C->State,XRT_STR_LITERAL("value"),xrtValueInt(QA_NUMBER));return Ok?MDO_RESULT_OK:MDO_RESULT_ERROR;
}
static mdo_result Register(const mdo_host_services_v1* H,const mdo_registrar_v1* R,void** D,char* E,size_t N){
 (void)H;(void)D;mdo_agent_v1 A={0};A.Size=sizeof(A);A.AbiVersion=MDO_MODULE_ABI_VERSION;A.Id="ROLE_ID";
 A.Name="Directory role";A.Description="Directory resource role";A.SystemPrompt="Native role prompt";
 A.Flags=ROLE_FLAG;A.AllowedEffects=MDO_TOOL_EFFECT_ALL;A.MaxTurns=8;A.MaxFinalBytes=65536;A.MaxDepth=2;A.TimeoutMilliseconds=30000;
 mdo_agent_program_v1 Program={sizeof(Program),MDO_MODULE_ABI_VERSION,NULL,Drive};A.Program=&Program;
 return R->AddAgent(R->Context,&A,E,N);
}
static const mdo_module_v1 Module={MDO_V1_HEADER(mdo_module_v1),"MODULE_ID","Directory role module","Role regression","1",0,NULL,0,Register,NULL,NULL};
MDO_EXPORT const mdo_module_v1* mdoModuleEntry(void){return &Module;}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', required=True); parser.add_argument('--home', type=Path, required=True)
    parser.add_argument('--project', default='default'); args = parser.parse_args()
    out = ROOT / '.build/module-directory-native'; out.mkdir(parents=True, exist_ok=True)
    fixture = out / ('home-' + uuid.uuid4().hex[:12]); fixture.mkdir()
    def add(kind, ident, alias, entry, files, native=None):
        directory = fixture / kind / ident; directory.mkdir(parents=True)
        meta = {'kind': kind, 'name': alias, 'alias': alias, 'entry': entry, 'platforms': ['any']}
        if native: meta['native'] = native
        (directory / 'extension.json').write_text(json.dumps(meta), encoding='utf-8')
        for name, text in files.items():
            path = directory / name; path.parent.mkdir(parents=True, exist_ok=True); path.write_text(text, encoding='utf-8')
    parent = {'schema': 'mdo.agent.v1', 'name': 'Parent role', 'description': 'Parent role', 'context': {'custom.parent': {'file': 'parent.txt'}}, 'prompt': 'parent prompt'}
    add('agents', 'parent-directory', 'parent', 'agent.json', {'agent.json': json.dumps(parent), 'parent.txt': 'parent context'})
    child = {'schema': 'mdo.agent.v1', 'name': 'Craftsman', 'description': 'Child C role', 'code': True, 'extends': 'agent.parent', 'prompt_file': 'prompts/base.txt', 'context': {'custom.child': {'file': 'child.txt'}}}
    code = ROLE.replace('ROLE_ID', 'agent.craftsman').replace('ROLE_FLAG', 'MDO_AGENT_MAIN').replace('MODULE_ID', 'qa.agent.module')
    add('agents', '0123456789ABCDEFGHJKMNPQRSTVWXYZ', 'craftsman', 'agent.json', {'agent.json': json.dumps(child), 'agent.c': code,
        'prompts/base.txt': 'child prompt file', 'child.txt': 'child context', 'helper.h': '#define QA_NUMBER 17\n'}, native='agent.c')
    code = ROLE.replace('ROLE_ID', 'subagent.worker').replace('ROLE_FLAG', 'MDO_AGENT_SUBAGENT').replace('MODULE_ID', 'qa.subagent.module')
    add('subagents', 'worker-directory', 'worker', 'agent.c', {'agent.c': code, 'helper.h': '#define QA_NUMBER 17\n'})
    tool = (ROOT / 'tests/fixtures/modules/local-tool.c').read_text(encoding='utf-8')
    tool = tool.replace('#include "mdo/module.h"', '#include "mdo/module.h"\n#include "mdo/sdk.h"\n#include <xsbase.h>\n#include "helper.h"')
    tool = tool.replace('    return Writer->WriteText(Writer->Context, "LOCAL TOOL CALLBACK RESULT") &&', '''    char* Path=xrtPathJoin(Context->Application->ExtensionRoot,"resource.txt");size_t Length=0;
    char* Text=(char*)xrtFileReadAllLimit(Path,1024,&Length);xrtFree(Path);if(!Text||QA_NUMBER!=17){xrtFree(Text);return MDO_RESULT_ERROR;}
    bool Written=Writer->WriteText(Writer->Context,Text);xrtFree(Text);return Written &&''')
    add('tools', 'tool-directory', 'fixture', 'tool.c', {'tool.c': tool, 'resource.txt': 'tool resource', 'helper.h': '#define QA_NUMBER 17\n'})
    try:
        result = product(args, fixture, CONTRACT, out / 'result.json')
        for key in ('loaded', 'tool_executed_with_resource', 'agent_driver', 'subagent_driver', 'inherited_and_own_resource_roots', 'auxiliary_header_reloaded', 'whole_role_disable', 'uninstalled_after_runtime_release'):
            assert result.get(key), (key, result)
        print(json.dumps(result, ensure_ascii=False))
    finally:
        if fixture.resolve().parent == out.resolve() and fixture.name.startswith('home-'):
            shutil.rmtree(fixture)


if __name__ == '__main__': main()
