"""Approved directory defaults: real background handshake, durable overrides, retirement."""
import argparse, json, shutil, subprocess, uuid, zipfile
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
 bool Disabled=Ready&&MdoMcpManagerSetEnabled(AutoId,false,&E)&&MdoMcpManagerReload();MdoMcpCatalog* C=MdoMcpCatalogSnapshot();Disabled=Disabled&&C&&C->Count==1&&!C->Entries[0].Enabled;MdoMcpCatalogRelease(C);xrtValueObjectSetNew(R,XRT_STR_LITERAL("disabled_default_is_durable"),xrtValueBool(Disabled));
 bool Restored=Disabled&&MdoMcpManagerSetEnabled(AutoId,true,&E)&&QaAutoReady(Ref);xrtValueObjectSetNew(R,XRT_STR_LITERAL("re_enable_connects_automatically"),xrtValueBool(Restored));
 bool Updated=Restored&&QaAutoInstall(Id,"data/update.zip",Error)&&QaAutoReady(Ref);Rows=MdoMcpConnections(NULL,Error);Updated=Updated&&Rows&&xrtValueCount(Rows)==1;xrtValueRelease(Rows);xrtValueObjectSetNew(R,XRT_STR_LITERAL("verified_update_preserves_private_override_without_duplicates"),xrtValueBool(Updated));
 bool Missing=QaAutoInstall("qa-input","data/input.zip",Error);xvalue* S=MdoMcpExtensionStatus("mcp:qa.input-fixture");cstr State=SdkField(S,"state");Missing=Missing&&State&&!strcmp(State,"needs_configuration")&&xrtValueCount(SdkGet(S,"missing_inputs"))==1;xrtValueRelease(S);xrtValueObjectSetNew(R,XRT_STR_LITERAL("only_used_required_inputs_block_activation"),xrtValueBool(Missing));
 bool Changed=MdoHomeAtomicWrite("mcp/qa-auto/support/banner.txt","tampered",8,false)&&MdoMcpManagerReload();S=MdoMcpExtensionStatus(Ref);State=SdkField(S,"state");Changed=Changed&&State&&!strcmp(State,"failed");xrtValueRelease(S);xrtValueObjectSetNew(R,XRT_STR_LITERAL("modified_directory_is_not_autoapproved"),xrtValueBool(Changed));
 bool Back=QaAutoInstall(Id,"data/ready.zip",Error)&&QaAutoReady(Ref);char Digest[65];Back=Back&&MdoExtensionsFingerprint("mcp",Id,Digest,NULL,Error);xvalue* Input=xrtValueObject(),*Result=NULL;SdkPut(Input,"kind","mcp");SdkPut(Input,"id",Id);SdkPut(Input,"action","remove");SdkPut(Input,"expected_revision",Digest);bool Gone=Back&&MdoExtensionsManage(Input,NULL,NULL,&Result,Error);xrtValueRelease(Input);xrtValueRelease(Result);
 Gone=Gone&&MdoHomeExternalStat(Path,&Exists,&Info)&&!Exists;Rows=MdoMcpConnections(NULL,Error);Gone=Gone&&Rows&&xrtValueCount(Rows)==1;xrtValueRelease(Rows);xrtValueObjectSetNew(R,XRT_STR_LITERAL("uninstall_retires_process_and_private_override"),xrtValueBool(Gone));
 SdkPut(R,"error",Error);SdkPut(R,"work_error",E.sMessage);xrtFree(Path);MdoMcpManagerUnit();if(Runtime)xworkRuntimeRelease(Runtime);MdoExtensionsUnit();if(Home)MdoHomeUnit();return R;
}
'''
def main():
 p=argparse.ArgumentParser();p.add_argument('--url',required=True);p.add_argument('--home',type=Path,required=True);p.add_argument('--project',default='default');p.add_argument('--cc',default='E:/software/w64devkit/bin/gcc.exe');a=p.parse_args()
 out=ROOT/'.build/mcp-auto-native';out.mkdir(exist_ok=True);fixture=out/('home-'+uuid.uuid4().hex[:10]);(fixture/'data').mkdir(parents=True)
 binary=out/'server.exe';subprocess.run([a.cc,'-Os','-s','-DQA_AUTO',str(ROOT/'tests/fixtures/mcp-directory-server.c'),'-o',str(binary)],check=True,capture_output=True)
 def archive(file,alias,marker):
  t=template(alias,'stdio');t['inputs']=[dict(id='marker',type='text',label='Marker',description='Test',required=True,secret=False,**({'default':marker} if marker else {})),dict(id='unused',type='text',label='Unused advanced key',description='Not used',required=True,secret=True)]
  t['variants'][0]['config']['transport']=dict(type='stdio',program='bin/server.exe',arguments=[dict(input='marker')],environment=[],inherit_environment=False)
  m=dict(kind='mcp',id='mcp:qa.'+alias,name=alias,alias=alias,entry='mcp.json',platforms=['windows-x86_64'])
  with zipfile.ZipFile(fixture/'data'/file,'w',zipfile.ZIP_DEFLATED) as z:
   z.writestr('extension.json',json.dumps(m));z.writestr('mcp.json',json.dumps(t));z.write(binary,'bin/server.exe');z.writestr('support/banner.txt',file)
 archive('ready.zip','auto-fixture','alpha');archive('update.zip','auto-fixture','alpha');archive('input.zip','input-fixture',None)
 try:
  result=product(a,fixture,CONTRACT,out/'result.json');print(json.dumps(result,ensure_ascii=False));assert all(v for k,v in result.items() if k not in ('error','work_error')),result
 finally:
  if fixture.resolve().parent==out.resolve() and fixture.name.startswith('home-'):shutil.rmtree(fixture)
if __name__=='__main__':main()
