"""Project API selects Home installations and never creates executable copies."""
import argparse
import json
from pathlib import Path
import shutil
import uuid
from native_product_runner import product

ROOT=Path(__file__).resolve().parents[1]
CONTRACT=r'''
static void QaFlag(xvalue* R,cstr K,bool V){xrtValueObjectSetNew(R,xrtStrView(K),xrtValueBool(V));}
xvalue* QaProductContract(const xvalue* P,xvfs V){
 xvalue* R=xrtValueObject(),*Out=NULL;char Error[512]={0};bool Home=QaProductHome(SdkField(P,"root"),SdkField(P,"app"),V);bool Ok=Home&&MdoExtensionsInit()&&MdoProjectLifecycleInit()&&MdoApiWriteInit()&&MdoApiExtensionsInit()&&MdoSkillManagerInit();
 XS_ServerInfo Server={0};Server.Name="project-selection-fixture";Server.Port=58013;SdkServer=&Server;g_MdoApiInitialized=true;mdo_sdk_context_v1 C={0};C.Size=sizeof(C);C.Version=MDO_SDK_VERSION;C.Api=MdoApplicationApi();C.ExtensionId="selection-fixture";C.Origin=MDO_ORIGIN_USER;mdo_error_v1 E={0};
 const cstr Kinds[]={"commands","skills"};size_t Passed=0;bool NoCopies=true,NoStale=true,NoSources=true,Runtime=true;
 for(size_t I=0;Ok&&I<2;I++){
  cstr Kind=Kinds[I],Physical=I?"1123456789ABCDEFGHJKMNPQRSTVWXYZ":"0123456789ABCDEFGHJKMNPQRSTVWXYZ",Alias=I?"QA-Skill":"QA-Command";char Before[65],After[65],Method[130];bool Item=MdoExtensionsFingerprint(Kind,Physical,Before,NULL,Error);
  xvalue* Q=SdkParse("{\"path\":{},\"query\":{\"project\":\"default\"}}");xvalue* Path=xrtValueObjectGet(Q,XRT_STR_LITERAL("path"));SdkPut(Path,I?"id":"command",Physical);snprintf(Method,sizeof(Method),"product.%s.project.by_%s.get",Kind,I?"id":"command");Item=Item&&mdoCall(&C,Method,Q,&Out,&E)==MDO_RESULT_OK;
  const xvalue* Row=SdkGet(Out,"data");Item=Item&&Row&&!strcmp(SdkField(Row,"directory_id"),Physical)&&!strcmp(SdkField(Row,"alias"),Alias)&&!strcmp(SdkField(Row,"revision"),"new");xrtValueRelease(Out);Out=NULL;
  xvalue* Body=SdkParse("{\"enabled\":false}");xrtValueObjectSet(Q,XRT_STR_LITERAL("body"),Body);xrtValueRelease(Body);SdkPut(Q,"revision","\"new\"");snprintf(Method,sizeof(Method),"product.%s.project.by_%s.enabled.post",Kind,I?"id":"command");Item=Item&&mdoCall(&C,Method,Q,&Out,&E)==MDO_RESULT_OK&&!MdoExtOpBool(SdkGet(Out,"data"),"project_enabled",true);char Revision[72]={0};if(Out&&SdkField(SdkGet(Out,"data"),"revision"))snprintf(Revision,sizeof(Revision),"\"%s\"",SdkField(SdkGet(Out,"data"),"revision"));xrtValueRelease(Out);Out=NULL;
  NoStale=NoStale&&mdoCall(&C,Method,Q,&Out,&E)!=MDO_RESULT_OK;xrtValueRelease(Out);Out=NULL;
  char* Workspace=MdoHomeDefaultWorkspacePath(false);if(I){MdoSkillCatalog* Skills=MdoSkillCatalogForWorkspace(Workspace);MdoSkillInfo Info={0};Info.Size=sizeof(Info);Runtime=Runtime&&Skills&&!MdoSkillCatalogFind(Skills,Alias,&Info);MdoSkillCatalogRelease(Skills);}else{xvalue* Catalog=MdoCommandCatalog(Workspace);Runtime=Runtime&&Catalog&&!MdoExtOpBool(SdkGet(Catalog,Alias),"enabled",true);xrtValueRelease(Catalog);}xrtFree(Workspace);
  xrtValueObjectRemove(Q,XRT_STR_LITERAL("body"));SdkPut(Q,"revision",Revision);snprintf(Method,sizeof(Method),"product.%s.project.by_%s.delete",Kind,I?"id":"command");Item=Item&&mdoCall(&C,Method,Q,&Out,&E)==MDO_RESULT_OK&&MdoExtOpBool(SdkGet(Out,"data"),"project_enabled",false);xrtValueRelease(Out);Out=NULL;
  snprintf(Method,sizeof(Method),"product.%s.project.by_%s.put",Kind,I?"id":"command");Body=SdkParse("{\"content\":\"old source body\"}");xrtValueObjectSet(Q,XRT_STR_LITERAL("body"),Body);xrtValueRelease(Body);NoSources=NoSources&&mdoCall(&C,Method,Q,&Out,&E)!=MDO_RESULT_OK;xrtValueRelease(Out);Out=NULL;
  NoCopies=NoCopies&&MdoExtensionsFingerprint(Kind,Physical,After,NULL,Error)&&!strcmp(Before,After);char* Copy=xrtFormat(MDO_DEFAULT_WORKSPACE_PATH "/.mdo/%s",Kind);bool Exists=true;xfileinfo Info;NoCopies=NoCopies&&MdoHomeExternalStat(Copy,&Exists,&Info)&&!Exists;xrtFree(Copy);xrtValueRelease(Q);if(Item)Passed++;
 }
 QaFlag(R,"both_project_selectors_use_home_directory_and_alias",Passed==2);QaFlag(R,"selection_changes_runtime_catalogue",Runtime);QaFlag(R,"stale_selection_rejected",NoStale);QaFlag(R,"source_writes_not_exposed",NoSources);QaFlag(R,"no_project_extension_copies",NoCopies);SdkPut(R,"error",E.Message);
 g_MdoApiInitialized=false;SdkServer=NULL;MdoSkillManagerUnit();MdoApiExtensionsUnit();MdoApiWriteUnit();MdoProjectLifecycleUnit();MdoExtensionTasksUnit();MdoExtensionsUnit();if(Home)MdoHomeUnit();return R;
}
'''

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--url',required=True);p.add_argument('--home',type=Path,required=True);p.add_argument('--project',default='default');a=p.parse_args()
 out=ROOT/'.build/project-extension-selection-native';out.mkdir(parents=True,exist_ok=True);fixture=out/('home-'+uuid.uuid4().hex[:12]);fixture.mkdir()
 for kind,ident,alias,entry,source in [('commands','0123456789ABCDEFGHJKMNPQRSTVWXYZ','QA-Command','command.c','/* mdo-command {"id":"QA-Command","description":"Fixture"} */\n'),('skills','1123456789ABCDEFGHJKMNPQRSTVWXYZ','QA-Skill','SKILL.md','---\nname: QA Skill\ndescription: Fixture\n---\nReference')]:
  folder=fixture/kind/ident;folder.mkdir(parents=True);(folder/'extension.json').write_text(json.dumps(dict(kind=kind,id=('command:' if kind=='commands' else 'skill:')+alias.lower(),name=alias,alias=alias,entry=entry,platforms=['any'])),encoding='utf-8');(folder/entry).write_text(source,encoding='utf-8')
 try:
  result=product(a,fixture,CONTRACT,out/'result.json');assert len(result)>=5 and all(v is True for k,v in result.items() if k!='error'),result;print(json.dumps(result,ensure_ascii=False))
 finally:
  if fixture.resolve().parent==out.resolve() and fixture.name.startswith('home-'):shutil.rmtree(fixture)

if __name__=='__main__':main()
