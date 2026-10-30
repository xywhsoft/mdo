"""Real MCP child processes use exact external exports and private dependency bindings."""
import argparse,json,shutil,subprocess,uuid,zipfile
from pathlib import Path
from native_product_runner import product
from test_mcp_directory_native import CONTRACT as BASE
from mcp_template_fixture import template
ROOT=Path(__file__).resolve().parents[1]
MCP='0123456789ABCDEFGHJKMNPQRSTVWXYZ'
PROVIDER='qa-provider'
CONTRACT='static char QaBindError[512];\n'+BASE.replace('MdoExtensionsFingerprint("mcp","'+MCP+'",Digest,NULL,Error)', 'MdoExtensionsApprovalDigest("mcp:qa.bundled-fixture",Digest,NULL,Error)').replace('MdoExtensionsFingerprint("mcp","'+MCP+'",NewDigest,NULL,Error)', 'MdoExtensionsApprovalDigest("mcp:qa.bundled-fixture",NewDigest,NULL,Error)')
CONTRACT=CONTRACT.replace('    xrtValueRelease(Old);xrtValueRelease(Input);xrtValueRelease(Out);return Ok;', '    if(!Ok)snprintf(QaBindError,sizeof(QaBindError),\"%s\",Error);xrtValueRelease(Old);xrtValueRelease(Input);xrtValueRelease(Out);return Ok;')
CONTRACT=CONTRACT.replace('    Ok=Ok&&xrtEnvSet', '    SdkPut(R,\"initial_error\",Error);xrtValueObjectSetNew(R,XRT_STR_LITERAL(\"initial_digest\"),xrtValueBool(Ok));\n    Ok=Ok&&xrtEnvSet').replace('    xvalue* PrivateDoc=', '    SdkPut(R,\"connection_error\",QaBindError);\n    xvalue* PrivateDoc=',1)
CONTRACT=CONTRACT.replace('    char ArchiveHash[65];', r'''
    bool ProviderPinned=MdoExtensionsInUse("external-tools","qa-provider");xrtValueObjectSetNew(R,XRT_STR_LITERAL("required_program_pinned_by_live_mcp"),xrtValueBool(ProviderPinned));
    char* Ambiguous=MdoMcpDependencyConnection("mcp:qa.bundled-fixture",NULL,NULL,true,Error);xrtValueObjectSetNew(R,XRT_STR_LITERAL("two_accounts_require_explicit_choice"),xrtValueBool(!Ambiguous));xrtFree(Ambiguous);
    xvalue* Binding=MdoExtensionsBindings("skill:qa.consumer","global",Error),*Bound=NULL;char BindingRevision[65];snprintf(BindingRevision,sizeof(BindingRevision),"%s",MdoExtensionText(Binding,"revision",64));
    bool BoundOk=MdoExtensionsBind("skill:qa.consumer","mcp:qa.bundled-fixture","global","qa-two",BindingRevision,&Bound,Error);xrtValueRelease(Bound);Bound=NULL;xrtValueRelease(Binding);
    xrtValueObjectSetNew(R,XRT_STR_LITERAL("unrefreshed_connection_blocks_consumer"),xrtValueBool(!MdoExtensionsRuntimeCheck("skill:qa.consumer",NULL,NULL,NULL,false,Error)));MdoMcpManagerRefresh("qa-two",NULL,MdoDeadlineAfterUs(10000000),&Work);
    char* Preferred=MdoExtensionsBinding("skill:qa.consumer","mcp:qa.bundled-fixture",NULL,Error),*Chosen=Preferred?MdoMcpDependencyConnection("mcp:qa.bundled-fixture",NULL,Preferred,true,Error):NULL;
    bool Scoped=Chosen&&!strcmp(Chosen,"qa-two")&&MdoExtensionsRuntimeCheck("skill:qa.consumer",NULL,NULL,NULL,false,Error);SdkPut(R,"binding_runtime_error",Error);xrtFree(Preferred);xrtFree(Chosen);
    xrtValueObjectSetNew(R,XRT_STR_LITERAL("private_binding_selects_exact_ready_connection"),xrtValueBool(BoundOk&&Scoped));

    MdoAgentOwner ChildOwner={0};snprintf(ChildOwner.ProductProject,sizeof(ChildOwner.ProductProject),"global");MdoModuleAgentInfo ChildRole={0};ChildRole.Size=sizeof(ChildRole);ChildRole.ExtensionId="agent:qa.child";xvalue* ChildConfig=SdkParse("{\"mcp_servers\":[\"mcp:qa.bundled-fixture\"]}");
    bool Inherited=MdoAgentsBindMcp(&ChildOwner,&ChildRole,ChildConfig,&Work);xstrview InheritedText={0};Inherited=Inherited&&xrtValueGetString(xrtValueArrayGet(MdoAgentConfigGet(ChildConfig,"mcp_servers"),0),&InheritedText)&&!strcmp(InheritedText.Data,"qa-two");xrtValueObjectSetNew(R,XRT_STR_LITERAL("inherited_mcp_selection_keeps_parent_private_binding"),xrtValueBool(Inherited));xrtValueRelease(ChildConfig);
    xvalue* ChildRemoved=NULL;MdoExtensionsRemove("agents","child",&ChildRemoved,Error);xrtValueRelease(ChildRemoved);
    xrtValueObjectSetNew(R,XRT_STR_LITERAL("stale_private_binding_edit_rejected"),xrtValueBool(!MdoExtensionsBind("skill:qa.consumer","mcp:qa.bundled-fixture","global","qa-one",BindingRevision,&Bound,Error)));xrtValueRelease(Bound);
    Binding=MdoExtensionsBindings("skill:qa.consumer","project-one",Error);BoundOk=MdoExtensionsBind("skill:qa.consumer","mcp:qa.bundled-fixture","project-one","qa-one",MdoExtensionText(Binding,"revision",64),&Bound,Error);xrtValueRelease(Bound);xrtValueRelease(Binding);
    Preferred=MdoExtensionsBinding("skill:qa.consumer","mcp:qa.bundled-fixture","project-one",Error);xrtValueObjectSetNew(R,XRT_STR_LITERAL("project_binding_overrides_global_choice"),xrtValueBool(BoundOk&&Preferred&&!strcmp(Preferred,"qa-one")));xrtFree(Preferred);
    MdoExtensionsRemove("skills","consumer",&Bound,Error);xrtValueRelease(Bound);
    char ArchiveHash[65];
''')
CONTRACT=CONTRACT.replace('    MdoMcpDiagnostics* D=', r'''
    char BeforeProvider[65],AfterProvider[65];MdoExtensionsApprovalDigest("mcp:qa.bundled-fixture",BeforeProvider,NULL,Error);bool ProviderChanged=MdoHomeAtomicWrite("external-tools/qa-provider/revision.txt","changed",7,false)&&MdoExtensionsApprovalDigest("mcp:qa.bundled-fixture",AfterProvider,NULL,Error)&&strcmp(BeforeProvider,AfterProvider)&&MdoMcpManagerReload();
    C=MdoMcpCatalogSnapshot();ProviderChanged=ProviderChanged&&C&&C->Count==0;MdoMcpCatalogRelease(C);xrtValueObjectSetNew(R,XRT_STR_LITERAL("changed_dependency_invalidates_program_approval"),xrtValueBool(ProviderChanged));
    bool Again=QaMcpConnection("qa-one","alpha-private-only",AfterProvider,true)&&QaMcpConnection("qa-two","beta-private-only",AfterProvider,true)&&MdoMcpManagerReload()&&QaMcpMarker("qa-two","beta-private-only");xrtValueObjectSetNew(R,XRT_STR_LITERAL("dependency_reapproval_reconnects_actual_process"),xrtValueBool(Again));
    MdoExtensionsFingerprint("mcp","0123456789ABCDEFGHJKMNPQRSTVWXYZ",Digest,NULL,Error);
    MdoMcpDiagnostics* D=
''')

def main():
 p=argparse.ArgumentParser();p.add_argument('--url',required=True);p.add_argument('--home',type=Path,required=True);p.add_argument('--project',default='default');a=p.parse_args();out=ROOT/'.build/mcp-dependency-native';out.mkdir(parents=True,exist_ok=True);fixture=out/('home-'+uuid.uuid4().hex[:10]);directory=fixture/'mcp'/MCP;provider=fixture/'external-tools'/PROVIDER;(directory/'support').mkdir(parents=True);(provider/'bin').mkdir(parents=True)
 (directory/'extension.json').write_text(json.dumps(dict(kind='mcp',id='mcp:qa.bundled-fixture',name='MCP dependency fixture',alias='bundled-fixture',entry='mcp.json',platforms=['windows-x86_64'],dependencies=[dict(id='external-tool:qa.provider',reason='Protocol executable')])),encoding='utf-8')
 (directory/'support/banner.txt').write_text('directory support',encoding='utf-8');shutil.copyfile(ROOT/'tests/fixtures/mcp-directory-server.c',provider/'server.c');subprocess.run(['E:/software/w64devkit/bin/gcc.exe','-Os','-s','-Wall','-Wextra',str(provider/'server.c'),'-o',str(provider/'bin/server.exe')],check=True,capture_output=True)
 (provider/'extension.json').write_text(json.dumps(dict(kind='external-tools',id='external-tool:qa.provider',name='Protocol provider',alias='qa-provider',entry='tool.json',platforms=['windows-x86_64'])),encoding='utf-8');(provider/'tool.json').write_text(json.dumps(dict(program='bin/server.exe',prompt='Protocol provider executable',command='qa-provider',commands=[dict(name='qa-provider',entry='bin/server.exe')])),encoding='utf-8')
 recipe=template('bundled-fixture','stdio');recipe['inputs']=[dict(id='marker',type='text',label='Marker',description='Private marker',required=True,secret=False),dict(id='token',type='text',label='Token',description='Private credential',required=True,secret=True)];recipe['variants'][0]['config']['transport']=dict(type='stdio',program=dict(ref='external-tool:qa.provider',export='qa-provider'),arguments=[dict(input='marker')],environment=[dict(name='QA_PRIVATE_TOKEN',secret_ref='input:token')],inherit_environment=False);(directory/'mcp.json').write_text(json.dumps(recipe),encoding='utf-8')
 consumer=fixture/'skills/consumer';consumer.mkdir(parents=True);(consumer/'extension.json').write_text(json.dumps(dict(kind='skills',id='skill:qa.consumer',name='Consumer',alias='consumer',entry='SKILL.md',platforms=['any'],dependencies=[dict(id='mcp:qa.bundled-fixture')])),encoding='utf-8');(consumer/'SKILL.md').write_text('---\nname: consumer\ndescription: Dependency fixture\n---\nUse the protocol.\n',encoding='utf-8')
 child=fixture/'agents/child';child.mkdir(parents=True);(child/'extension.json').write_text(json.dumps(dict(kind='agents',id='agent:qa.child',name='Inherited binding',alias='child',entry='agent.json',platforms=['any'],dependencies=[dict(id='skill:qa.consumer')])),encoding='utf-8');(child/'agent.json').write_text(json.dumps(dict(name='Child',prompt='Fixture',mcp_servers=[dict(ref='mcp:qa.bundled-fixture')])),encoding='utf-8')
 passed=False
 try:
  result=product(a,fixture,CONTRACT,out/'result.json');assert result and 'compile_error' not in result,result
  for key,value in result.items():
   if key not in ('diagnostics','error') and not key.endswith('_error'):assert value is True,(key,result)
  archive=fixture/'data/extension-tasks/export/mcp.zip'
  with zipfile.ZipFile(archive) as z:
   assert 'bin/server.exe' not in z.namelist();assert json.loads(z.read('extension.json'))['dependencies'][0]['id']=='external-tool:qa.provider'
   for name in z.namelist():assert b'private-fixture-token' not in z.read(name) and b'alpha-private-only' not in z.read(name)
  passed=True;print('PASS',len([v for v in result.values() if v is True]),'real MCP dependency and private-binding contracts')
 finally:
  if passed and fixture.resolve().parent==out.resolve():shutil.rmtree(fixture)
if __name__=='__main__':main()
