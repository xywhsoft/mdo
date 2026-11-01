"""Discover the original Cua package automatically, without desktop actions or binary copies."""
import argparse, hashlib, json, os, shutil, zipfile
from pathlib import Path
from native_product_runner import product
ROOT=Path(__file__).resolve().parents[1]
CONTRACT=r'''
xvalue* QaProductContract(const xvalue* P,xvfs V){
 xvalue* R=xrtValueObject();char Error[512]={0},Digest[65]={0};bool Home=QaProductHome(SdkField(P,"root"),SdkField(P,"app"),V),Ok=Home&&MdoExtensionsInit()&&MdoExtensionsFingerprint("mcp","qa-cua",Digest,NULL,Error);
 xvalue* Record=SdkParse("{\"source\":\"manual\",\"logical_id\":\"mcp:mdo.computer-use\",\"enabled\":true}");SdkPut(Record,"directory_digest",Digest);xrtMutexLock(MdoExtLock);Ok=Ok&&MdoExtRecord("mcp","qa-cua",Record);xrtMutexUnlock(MdoExtLock);xrtValueRelease(Record);
 xwork_runtime_config Config;xworkRuntimeConfigInit(&Config);xwork_error E={0};xwork_runtime* Runtime=Ok?xworkRuntimeCreate(&Config,&E):NULL;Ok=Ok&&Runtime&&MdoMcpManagerInit(Runtime);uint64 End=MdoDeadlineAfterUs(15000000);xvalue* S=NULL;do{xrtValueRelease(S);S=MdoMcpExtensionStatus("mcp:mdo.computer-use");cstr State=SdkField(S,"state");if(State&&!strcmp(State,"ready"))break;xrtSleep(40);}while(Ok&&!MdoDeadlineExpired(End));
 xrtValueObjectSet(R,XRT_STR_LITERAL("status"),S);xrtValueRelease(S);xvalue* Connections=MdoMcpConnections(NULL,Error);xrtValueObjectSetNew(R,XRT_STR_LITERAL("connections"),xrtValueUInt(xrtValueCount(Connections)));xrtValueRelease(Connections);MdoMcpCatalog* C=MdoMcpCatalogSnapshot();const MdoMcpServerEntry* Entry=C&&C->Count==1?&C->Entries[0]:NULL;
 bool Safe=Entry&&Entry->ArgumentCount==2&&!strcmp(Entry->Arguments[0],"mcp")&&!strcmp(Entry->Arguments[1],"--direct");xrtValueObjectSetNew(R,XRT_STR_LITERAL("safe_default_without_existing_profile_grant"),xrtValueBool(Safe));MdoMcpCatalogRelease(C);
 xllm_session_config SC;xllmSessionConfigInit(&SC);xllm_error LE={0};xllm_session* Session=xllmSessionCreate(&SC,&LE);xwork_agent_definition_config DC;xworkAgentDefinitionConfigInit(&DC);DC.sId="qa-mcp-visibility";DC.sModel="fixture-no-network";DC.bRegisterBuiltinTools=true;DC.eApprovalMode=XWORK_APPROVAL_AUTO;xwork_agent_definition* Definition=xworkAgentDefinitionCreate(&DC,&E);xwork_agent_options AO;xworkAgentOptionsInit(&AO);AO.pSession=Session;AO.sWorkspaceRoot=SdkField(P,"root");xwork_agent* Agent=Definition&&Session?xworkAgentCreateWithRuntime(Runtime,Definition,&AO,&E):NULL;
 xllm_executor Executor={0};bool Visible=Agent&&MdoMcpConfigureAgent(Agent,AO.sWorkspaceRoot,&E)&&xworkExecutorBind(&Executor,Agent,&E);xllm_tool_call Call={0};Call.sId="qa-discover";Call.sName="tools";Call.sArgumentsJson="{}";xllm_executor_ctx Context={0};Context.uDeadline=XLLM_DEADLINE_NEVER;xllm_executor_result Found={0};Visible=Visible&&Executor.pExecute(Executor.pUserData,&Call,&Context,&Found)&&Found.bSuccess&&Found.sContent&&strstr(Found.sContent,"auto-");xrtValueObjectSetNew(R,XRT_STR_LITERAL("agent_discovers_automatic_service_without_a_model_call"),xrtValueBool(Visible));
 if(Executor.pUserData)xworkExecutorUnbind(&Executor);if(Agent)xworkAgentDestroy(Agent);xworkAgentDefinitionRelease(Definition);if(Session)xllmSessionDestroy(Session);
 MdoMcpManagerUnit();if(Runtime)xworkRuntimeRelease(Runtime);MdoExtensionsUnit();if(Home)MdoHomeUnit();SdkPut(R,"error",Error);return R;
}
'''
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--url',required=True);p.add_argument('--home',required=True,type=Path);p.add_argument('--project',default='default');p.add_argument('--computer-use-root',type=Path,default=Path('D:/GIT/mdo-computer-use'));a=p.parse_args()
 out=ROOT/'.build/mcp-auto-20261102';fixture=out/'cua-native';d=fixture/'mcp/qa-cua'
 if fixture.exists():raise RuntimeError('Inspect the existing fixture before continuing')
 d.mkdir(parents=True)
 try:
  source=a.computer_use_root;lock=json.loads((source/'computer_use/cua-driver.lock.json').read_text());original=source/'.build/computer-use/cua'/lock['version']
  for name,expected in lock['files'].items():
   path=original/name
   with path.open('rb') as file:assert hashlib.file_digest(file,'sha256').hexdigest()==expected['sha256'],name
   os.link(path,d/name)
  with zipfile.ZipFile(source/'.build/computer-use/computer-use-windows-x86_64.zip') as z:
   for name in ('extension.json','mcp.json','README.md','LICENSE.md','provenance.json'):(d/name).write_bytes(z.read(name))
  result=product(a,fixture,CONTRACT,out/'cua-auto-result.json');print(json.dumps(result,ensure_ascii=False));assert result['status']['state']=='ready' and result['status']['tool_count']>=60 and result['safe_default_without_existing_profile_grant'] and result['connections']==1 and result['agent_discovers_automatic_service_without_a_model_call'],result
 finally:
  if fixture.resolve().parent==out.resolve() and fixture.name=='cua-native':shutil.rmtree(fixture)
if __name__=='__main__':main()
