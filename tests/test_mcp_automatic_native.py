"""Approved directory defaults: real background handshake, durable overrides, retirement."""
import argparse, json, shutil, subprocess, uuid, zipfile, threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from native_product_runner import product
from mcp_template_fixture import template
ROOT = Path(__file__).resolve().parents[1]
CONTRACT = r'''
static bool QaAutoReady(cstr Ref){
 uint64 End=MdoDeadlineAfterUs(6000000);bool Ready=false;
 do{xvalue* S=MdoMcpExtensionStatus(Ref);cstr State=SdkField(S,"state");Ready=State&&!strcmp(State,"ready")&&xrtValueCount(SdkGet(S,"missing_inputs"))==0;xrtValueRelease(S);if(Ready)break;xrtSleep(20);}while(!MdoDeadlineExpired(End));return Ready;
}
static bool QaAutoInstall(cstr Id,cstr Zip,char Error[512]){
 xvalue* Input=xrtValueObject(),*Result=NULL,*Before=MdoExtensionsRead("mcp",Id,NULL);char Revision[65]="";if(Before)MdoExtensionsFingerprint("mcp",Id,Revision,NULL,Error);xrtValueRelease(Before);SdkPut(Input,"kind","mcp");SdkPut(Input,"id",Id);SdkPut(Input,"archive",Zip);SdkPut(Input,"action","install");SdkPut(Input,"expected_revision",Revision);xrtValueObjectSetNew(Input,XRT_STR_LITERAL("trust_code"),xrtValueBool(true));
 bool Ok=MdoExtensionsManage(Input,NULL,NULL,&Result,Error);xrtValueRelease(Result);xrtValueRelease(Input);return Ok;
}
xvalue* QaProductContract(const xvalue* P,xvfs V){
 xvalue* R=xrtValueObject();char Error[512]={0};bool Home=QaProductHome(SdkField(P,"root"),SdkField(P,"app"),V),Ok=Home&&MdoExtensionsInit();
 xwork_runtime_config Config;xworkRuntimeConfigInit(&Config);xwork_error E;xworkErrorInit(&E);xwork_runtime* Runtime=Ok?xworkRuntimeCreate(&Config,&E):NULL;Ok=Ok&&Runtime&&MdoMcpManagerInit(Runtime);
 cstr Ref="mcp:qa.auto-fixture",Id="qa-auto";char AutoId[97];McpDefaultId(Ref,AutoId);
 Ok=Ok&&QaAutoInstall(Id,"data/ready.zip",Error);bool Ready=Ok&&QaAutoReady(Ref);xrtValueObjectSetNew(R,XRT_STR_LITERAL("install_initializes_and_discovers_without_user_action"),xrtValueBool(Ready));
 if(!Ready){xvalue* Detail=MdoMcpExtensionStatus(Ref);xrtValueObjectSet(R,XRT_STR_LITERAL("initial_status"),Detail);xrtValueRelease(Detail);SdkPut(R,"initial_error",Error);}
 char* Path=xrtFormat("data/mcp/connections/%s.json",AutoId);bool Exists=false;xfileinfo Info;bool Inert=Path&&MdoHomeExternalStat(Path,&Exists,&Info)&&!Exists; xrtValueObjectSetNew(R,XRT_STR_LITERAL("no_generated_private_file"),xrtValueBool(Inert));
 xvalue* Rows=MdoMcpConnections(NULL,Error);bool One=Rows&&xrtValueCount(Rows)==1&&MdoExtOpBool(xrtValueArrayGet(Rows,0),"managed_default",false);xrtValueRelease(Rows);
 char* Chosen=MdoMcpDependencyConnection(Ref,NULL,NULL,true,Error);One=One&&Chosen&&!strcmp(Chosen,AutoId);xrtFree(Chosen);xrtValueObjectSetNew(R,XRT_STR_LITERAL("one_stable_default_and_automatic_dependency_binding"),xrtValueBool(One));
 bool Disconnected=Ready&&MdoMcpManagerDisconnect(AutoId,&E),Settled=false;uint64 SettleEnd=MdoDeadlineAfterUs(1000000);do{xvalue* State=MdoMcpExtensionStatus(Ref);cstr Name=SdkField(State,"state");Settled=Name&&!strcmp(Name,"disconnected");xrtValueRelease(State);if(Settled)break;xrtSleep(20);}while(!MdoDeadlineExpired(SettleEnd));xrtValueObjectSetNew(R,XRT_STR_LITERAL("live_disconnect_does_not_show_stale_ready_status"),xrtValueBool(Disconnected&&Settled));McpPrepareSchedule();xrtValueObjectSetNew(R,XRT_STR_LITERAL("authorization_style_wakeup_rediscovers_without_catalogue_reload"),xrtValueBool(QaAutoReady(Ref)));
 bool Disabled=Ready&&MdoMcpManagerSetEnabled(AutoId,false,&E);MdoMcpManagerUnit();Disabled=Disabled&&MdoMcpManagerInit(Runtime);MdoMcpCatalog* C=MdoMcpCatalogSnapshot();Disabled=Disabled&&C&&C->Count==1&&!C->Entries[0].Enabled;MdoMcpCatalogRelease(C);xrtValueObjectSetNew(R,XRT_STR_LITERAL("disabled_default_is_durable"),xrtValueBool(Disabled));
 bool Restored=Disabled&&MdoMcpManagerSetEnabled(AutoId,true,&E)&&QaAutoReady(Ref);xrtValueObjectSetNew(R,XRT_STR_LITERAL("re_enable_connects_automatically"),xrtValueBool(Restored));
 bool Updated=Restored&&QaAutoInstall(Id,"data/update.zip",Error)&&QaAutoReady(Ref);Rows=MdoMcpConnections(NULL,Error);Updated=Updated&&Rows&&xrtValueCount(Rows)==1;xrtValueRelease(Rows);xrtValueObjectSetNew(R,XRT_STR_LITERAL("verified_update_preserves_private_override_without_duplicates"),xrtValueBool(Updated));
 bool Missing=QaAutoInstall("qa-input","data/input.zip",Error);xvalue* S=MdoMcpExtensionStatus("mcp:qa.input-fixture");cstr State=SdkField(S,"state");Missing=Missing&&State&&!strcmp(State,"needs_configuration")&&xrtValueCount(SdkGet(S,"missing_inputs"))==1;xrtValueRelease(S);xrtValueObjectSetNew(R,XRT_STR_LITERAL("only_used_required_inputs_block_activation"),xrtValueBool(Missing));
 bool Retry=QaAutoInstall("qa-retry","data/retry.zip",Error)&&QaAutoReady("mcp:qa.retry-fixture");xrtValueObjectSetNew(R,XRT_STR_LITERAL("transient_initialization_failure_recovers_automatically"),xrtValueBool(Retry));
 bool Auth=QaAutoInstall("qa-oauth","data/oauth.zip",Error);uint64 AuthEnd=MdoDeadlineAfterUs(6000000);do{S=MdoMcpExtensionStatus("mcp:qa.oauth-fixture");State=SdkField(S,"state");Auth=State&&!strcmp(State,"needs_authorization");xrtValueRelease(S);if(Auth)break;xrtSleep(20);}while(!MdoDeadlineExpired(AuthEnd));xrtValueObjectSetNew(R,XRT_STR_LITERAL("oauth_waits_for_user_without_auto_login_or_install_failure"),xrtValueBool(Auth));
 char AuthId[97];McpDefaultId("mcp:qa.oauth-fixture",AuthId);S=MdoMcpAuthorizationStatus(AuthId);cstr Url=SdkField(S,"authorization_url");xrtValueObjectSetNew(R,XRT_STR_LITERAL("oauth_does_not_open_an_authorization_flow_in_background"),xrtValueBool(S&&!MdoExtOpBool(S,"busy",true)&&(!Url||!Url[0])));xrtValueRelease(S);
 bool Secret=QaAutoInstall("qa-secret","data/secret.zip",Error)&&xrtEnvSet("MDO_QA_AUTO_TOKEN","fixture-private-token");char SecretId[97];McpDefaultId("mcp:qa.secret-fixture",SecretId);xvalue* Private=MdoMcpConnections(SecretId,Error),*Save=SdkParse("{\"action\":\"save\",\"extension_id\":\"mcp:qa.secret-fixture\",\"variant\":\"default\",\"values\":{\"marker\":\"alpha\"},\"secret_refs\":{\"token\":\"env:MDO_QA_AUTO_TOKEN\"},\"enabled\":true}"),*Saved=NULL;
 SdkPut(Save,"id",SecretId);SdkPut(Save,"expected_revision",SdkField(Private,"revision"));SdkPut(Save,"approval_digest",SdkField(Private,"approval_digest"));Secret=Secret&&MdoMcpConnectionManage(Save,NULL,&Saved,Error)&&QaAutoReady("mcp:qa.secret-fixture");xrtValueRelease(Private);xrtValueRelease(Save);xrtValueRelease(Saved);
 Secret=Secret&&QaAutoInstall("qa-secret","data/secret-update.zip",Error)&&QaAutoReady("mcp:qa.secret-fixture");Private=MdoMcpConnections(SecretId,Error);Secret=Secret&&Private&&MdoExtOpBool(SdkGet(Private,"secrets_set"),"token",false)&&!SdkGet(SdkGet(Private,"values"),"token");xrtValueRelease(Private);xrtValueObjectSetNew(R,XRT_STR_LITERAL("verified_update_preserves_working_private_credentials"),xrtValueBool(Secret));
 bool Target=Secret&&QaAutoInstall("qa-secret","data/secret-target.zip",Error);S=MdoMcpExtensionStatus("mcp:qa.secret-fixture");State=SdkField(S,"state");Target=Target&&State&&!strcmp(State,"needs_approval");xrtValueRelease(S);xrtValueObjectSetNew(R,XRT_STR_LITERAL("changed_target_never_reuses_saved_credentials_automatically"),xrtValueBool(Target));
 bool Limited=QaAutoInstall("qa-limit","data/limit.zip",Error);uint64 LimitEnd=MdoDeadlineAfterUs(6000000);do{S=MdoMcpExtensionStatus("mcp:qa.limit-fixture");State=SdkField(S,"state");Limited=State&&!strcmp(State,"failed");xrtValueRelease(S);if(Limited)break;xrtSleep(20);}while(!MdoDeadlineExpired(LimitEnd));xrtValueObjectSetNew(R,XRT_STR_LITERAL("persistent_failure_finishes_bounded_background_retries"),xrtValueBool(Limited));
 xvalue* Warnings=xrtValueArray();bool Warn=MdoExtensionsWarnings(Warnings),InputWarning=false,AuthWarning=false;for(size_t I=0;I<xrtValueCount(Warnings);I++){cstr Code=SdkField(xrtValueArrayGet(Warnings,I),"code");if(Code&&!strcmp(Code,"mcp_configuration_required"))InputWarning=true;if(Code&&!strcmp(Code,"mcp_authorization_required"))AuthWarning=true;}xrtValueObjectSetNew(R,XRT_STR_LITERAL("actionable_states_use_the_existing_unified_warning_feed"),xrtValueBool(Warn&&InputWarning&&AuthWarning));xrtValueRelease(Warnings);
 bool Changed=MdoHomeAtomicWrite("mcp/qa-auto/support/banner.txt","tampered",8,false)&&MdoMcpManagerReload();S=MdoMcpExtensionStatus(Ref);State=SdkField(S,"state");MdoMcpServerStatus Blocked={0};Blocked.Size=sizeof(Blocked);Changed=Changed&&State&&(!strcmp(State,"failed")||!strcmp(State,"needs_approval"))&&!MdoMcpManagerGetStatus(AutoId,&Blocked);xrtValueRelease(S);xrtValueObjectSetNew(R,XRT_STR_LITERAL("modified_directory_is_not_autoapproved"),xrtValueBool(Changed));
 bool Back=QaAutoInstall(Id,"data/ready.zip",Error)&&QaAutoReady(Ref);char Digest[65];Back=Back&&MdoExtensionsFingerprint("mcp",Id,Digest,NULL,Error);xvalue* Input=xrtValueObject(),*Result=NULL;SdkPut(Input,"kind","mcp");SdkPut(Input,"id",Id);SdkPut(Input,"action","remove");SdkPut(Input,"expected_revision",Digest);bool Gone=Back&&MdoExtensionsManage(Input,NULL,NULL,&Result,Error);xrtValueRelease(Input);xrtValueRelease(Result);
 Gone=Gone&&MdoHomeExternalStat(Path,&Exists,&Info)&&!Exists;Rows=MdoMcpConnections(NULL,Error);Gone=Gone&&Rows&&xrtValueCount(Rows)==5;xrtValueRelease(Rows);xrtValueObjectSetNew(R,XRT_STR_LITERAL("uninstall_retires_process_and_private_override"),xrtValueBool(Gone));
 SdkPut(R,"error",Error);SdkPut(R,"work_error",E.sMessage);xrtFree(Path);MdoMcpManagerUnit();xrtEnvSet("MDO_QA_AUTO_TOKEN",NULL);if(Runtime)xworkRuntimeRelease(Runtime);MdoExtensionsUnit();if(Home)MdoHomeUnit();return R;
}
'''
class Peer(BaseHTTPRequestHandler):
 retry_initializes=0
 auth_requests=0
 limit_requests=0
 def log_message(self,*_):pass
 def do_POST(self):
  body=json.loads(self.rfile.read(int(self.headers.get('Content-Length',0))));method=body.get('method');status=200
  if self.path=='/auth':Peer.auth_requests+=1;status=401;result={}
  elif self.path=='/limit':
   if method=='initialize':Peer.limit_requests+=1
   status=503;result={}
  elif method=='initialize':
   Peer.retry_initializes+=1;status=503 if Peer.retry_initializes<3 else 200
   result=dict(protocolVersion='2025-11-25',serverInfo=dict(name='retry',version='1'),capabilities=dict(tools={}))
  elif method=='tools/list':result={'tools':[dict(name='echo',description='Retry fixture',inputSchema=dict(type='object',properties={}))]}
  else:result={};status=202 if 'id' not in body else 200
  payload=json.dumps(dict(jsonrpc='2.0',id=body.get('id'),result=result)).encode();self.send_response(status);self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(payload)));self.end_headers();self.wfile.write(payload)
 def do_DELETE(self):self.send_response(204);self.end_headers()
def main():
 p=argparse.ArgumentParser();p.add_argument('--url',required=True);p.add_argument('--home',type=Path,required=True);p.add_argument('--project',default='default');p.add_argument('--cc',default='E:/software/w64devkit/bin/gcc.exe');a=p.parse_args()
 out=ROOT/'.build/mcp-auto-native';out.mkdir(exist_ok=True);fixture=out/('home-'+uuid.uuid4().hex[:10]);(fixture/'data').mkdir(parents=True)
 binary=out/'server.exe';subprocess.run([a.cc,'-Os','-s','-DQA_AUTO',str(ROOT/'tests/fixtures/mcp-directory-server.c'),'-o',str(binary)],check=True,capture_output=True)
 peer=ThreadingHTTPServer(('127.0.0.1',0),Peer);threading.Thread(target=peer.serve_forever,daemon=True).start()
 def archive(file,alias,marker,remote=False,oauth=False,limit=False,secret=False,changed=False):
  t=template(alias,'stdio');t['inputs']=[dict(id='marker',type='text',label='Marker',description='Test',required=True,secret=False,**({'default':marker} if marker else {})),dict(id='unused',type='text',label='Unused advanced key',description='Not used',required=True,secret=True)]
  t['variants'][0]['config']['transport']=dict(type='stdio',program='bin/server.exe',arguments=[dict(input='marker')],environment=[],inherit_environment=False)
  if secret:
   t['inputs'].append(dict(id='token',type='text',label='Token',description='Private fixture credential',required=True,secret=True));t['variants'][0]['config']['transport']['environment']=[dict(name='QA_PRIVATE_TOKEN',secret_ref='input:token')];t['variants'][0]['config']['transport']['arguments'].append('new-grant' if changed else 'require-token')
  if remote:
   t['inputs']=[dict(id='endpoint',type='text',label='Endpoint',description='Local fixture',required=True,secret=False,default=f'http://127.0.0.1:{peer.server_port}/'+('auth' if oauth else 'limit' if limit else 'retry'))];t['variants'][0]['config']['transport']=dict(type='streamable-http',endpoint=dict(input='endpoint'),headers=[])
   if oauth:t['variants'][0]['config']['oauth']=dict(enabled=True,client_id='fixture',scope='read')
  m=dict(kind='mcp',id='mcp:qa.'+alias,name=alias,alias=alias,entry='mcp.json',platforms=['windows-x86_64'])
  with zipfile.ZipFile(fixture/'data'/file,'w',zipfile.ZIP_DEFLATED) as z:
   z.writestr('extension.json',json.dumps(m));z.writestr('mcp.json',json.dumps(t));z.write(binary,'bin/server.exe');z.writestr('support/banner.txt',file)
 archive('ready.zip','auto-fixture','alpha');archive('update.zip','auto-fixture','alpha');archive('input.zip','input-fixture',None)
 archive('retry.zip','retry-fixture',None,remote=True);archive('oauth.zip','oauth-fixture',None,remote=True,oauth=True)
 archive('secret.zip','secret-fixture','alpha',secret=True);archive('secret-update.zip','secret-fixture','alpha',secret=True);archive('secret-target.zip','secret-fixture','alpha',secret=True,changed=True)
 archive('limit.zip','limit-fixture',None,remote=True,limit=True)
 try:
  result=product(a,fixture,CONTRACT,out/'result.json');print(json.dumps(result,ensure_ascii=False));assert all(v for k,v in result.items() if k not in ('error','work_error')),result
  assert Peer.retry_initializes==3,Peer.retry_initializes
  # Subsequent explicit catalogue reloads may start another bounded batch.
  assert Peer.limit_requests<=15 and Peer.limit_requests>=5,Peer.limit_requests
 finally:
  if fixture.resolve().parent==out.resolve() and fixture.name.startswith('home-'):shutil.rmtree(fixture)
  peer.shutdown();peer.server_close()
if __name__=='__main__':main()
