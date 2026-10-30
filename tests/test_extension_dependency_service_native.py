"""Full store-plan adapter, five sealed downloads, atomic install and lost-reply retry."""
import argparse,json,re,shutil,uuid,zipfile
from pathlib import Path
from locked_sdk import sdk_root
from native_product_runner import product
from extension_zip_fixture import directory_zip,platform_store_source
from test_extension_service_native import TRANSPORT,PLATFORM
from test_platform_directory_store_native import CONTRACT as STORE
from test_extension_dependency_store_native import TEST as DEPENDENCIES
ROOT=Path(__file__).resolve().parents[1]
TRANSPORT=TRANSPORT.replace(' }else if(strstr(Path,"/catalog"))', ' }else if(strstr(Path,"/plan")){char* Version=QaQuery(Path,"version"),*Platform=QaQuery(Path,"platform");Out=MdStoreInstallPlan(&QaStore,Version,Platform,E);Ok=Out!=NULL;xrtFree(Version);xrtFree(Platform);\n }else if(strstr(Path,"/catalog"))')
CONTRACT=r'''
static void QaFlag(xvalue* R,cstr K,bool B){xrtValueObjectSetNew(R,xrtStrView(K),xrtValueBool(B));}
xvalue* QaProductContract(const xvalue* P,xvfs V){
 xvalue* R=xrtValueObject(),*Out=NULL,*Published=NULL;char E[512]={0};bool Home=QaProductHome(SdkField(P,"root"),SdkField(P,"app"),V);char* StorePath=xrtFormat("%s/platform",SdkField(P,"root"));bool Ok=Home&&MdoExtensionsInit()&&MdoExtensionTasksInit()&&MdStoreInit(&QaStore,StorePath,E);xrtFree(StorePath);
 for(size_t I=0;Ok&&I<5;I++){char Name[16];snprintf(Name,sizeof(Name),"kb%u",(unsigned)I);xrtValueRelease(Published);Published=QaDependencyPublish(&QaStore,SdkField(P,"root"),Name,NULL,0,E);Ok=Published!=NULL;}
 QaFlag(R,"five_published_original_archives",Ok);xvalue* Input=xrtValueObject();SdkPut(Input,"version_id",SdkField(Published,"version_id"));xvalue* Plan=NULL;
 QaLogin=false;bool Planned=Ok&&QaMdoExtensionServicePlan(Input,NULL,&Plan,E);SdkPut(R,"plan_error",E);QaFlag(R,"platform_plan_pins_complete_five_item_closure",Planned&&MdoGraphBool(Plan,"ready",false)&&xrtValueCount(SdkGet(Plan,"items"))==5);xrtValueRelease(Input);Input=xrtValueObject();SdkPut(Input,"plan_id",SdkField(Plan,"id"));SdkPut(Input,"revision",SdkField(Plan,"revision"));xrtValueObjectSetNew(Input,XRT_STR_LITERAL("trust_code"),xrtValueBool(true));
 bool Applied=Planned&&QaMdoExtensionServiceApplyPlan(Input,NULL,&Out,E);QaFlag(R,"more_than_three_pinned_downloads_commit_atomically",Applied&&MdoExtensionsPreflight("knowledge:qa.kb4",E));xrtValueRelease(Out);Out=NULL;
 xvalue* Tasks=MdoExtensionTasksList(E);size_t Completed=0;for(size_t I=0;I<xrtValueCount(Tasks);I++)if(!strcmp(SdkField(xrtValueArrayGet(Tasks,I),"state"),"complete"))Completed++;QaFlag(R,"all_transfer_tasks_have_completion_receipts",Completed==5);xrtValueRelease(Tasks);
 unsigned Before=QaRequests;QaFlag(R,"lost_reply_retry_returns_receipt_without_network",Applied&&QaMdoExtensionServiceApplyPlan(Input,NULL,&Out,E)&&QaRequests==Before&&Out);xrtValueRelease(Out);Out=NULL;
 QaFlag(R,"committed_plan_cannot_be_discarded",!MdoExtensionsPlanDiscard(SdkField(Plan,"id"),SdkField(Plan,"revision"),E));
 char* PublicPlan=SdkJson(Plan);QaFlag(R,"no_credential_in_public_plan",PublicPlan&&!strstr(PublicPlan,"native-fixture-credential"));xrtFree(PublicPlan);xrtValueRelease(Plan);xrtValueRelease(Input);xrtValueRelease(Published);
 MdStoreUnit(&QaStore);MdoExtensionTasksUnit();MdoExtensionsUnit();if(Home)MdoHomeUnit();SdkPut(R,"error",E);return R;
}
'''
def main():
 p=argparse.ArgumentParser();p.add_argument('--url',required=True);p.add_argument('--home',type=Path,required=True);p.add_argument('--project',default='default');a=p.parse_args();out=ROOT/'.build/dependency-service-native';out.mkdir(parents=True,exist_ok=True);fixture=out/('home-'+uuid.uuid4().hex[:10]);fixture.mkdir();shutil.copyfile(sdk_root()/'lib/sqlite3.h',fixture/'sqlite3.h')
 for i in range(5):
  path=fixture/f'kb{i}.zip';directory_zip(path,'knowledge',alias=f'kb{i}')
  with zipfile.ZipFile(path) as z:files={n:z.read(n) for n in z.namelist()}
  manifest=json.loads(files['extension.json']);manifest.update(id=f'knowledge:qa.kb{i}',dependencies=[dict(id=f'knowledge:qa.kb{j}') for j in range(i)]);files['extension.json']=json.dumps(manifest)
  with zipfile.ZipFile(path,'w',zipfile.ZIP_DEFLATED) as z:
   for name,content in files.items():z.writestr(name,content)
 client=(ROOT/'app/src/extensions/service.c').read_text(encoding='utf-8').replace('#include "service_plan.inc.c"',(ROOT/'app/src/extensions/service_plan.inc.c').read_text(encoding='utf-8'));client=re.sub(r'^#include .*$', '',client,flags=re.M);client=re.sub(r'\b(MdoExtService\w+|MdoExtensionService\w+)\b',r'Qa\1',client)
 for old,new in dict(MdoAccountAcquireServiceWait='QaLease',MdoAccountRelease='QaRelease',MdoAccountRejectAccess='QaReject',xsFetch='QaFetch',xsFetchResponseUnit='QaResponseUnit').items():client=re.sub(r'\b'+old+r'\b',new,client)
 symbols=('initialize','bind_int','bind_text','prepare_v2','finalize','step','exec','column_count','column_name','column_type','column_int64','column_text','column_bytes','bind_int64','open','busy_timeout','close','changes');setup='QaSqlite=LoadLibraryW(L"C:/Program Files/Python312/DLLs/sqlite3.dll");'+''.join(f'tcc_add_symbol(T,"sqlite3_{n}",GetProcAddress(QaSqlite,"sqlite3_{n}"));' for n in symbols)
 helpers=STORE.split('xvalue* QaProductContract(')[0]+DEPENDENCIES.split('xvalue* QaProductContract(')[0];passed=False
 try:
  result=product(a,fixture,platform_store_source(PLATFORM)+helpers+TRANSPORT+client+CONTRACT,out/'result.json',compiler_setup=setup,compiler_prelude='#include <windows.h>\nstatic HMODULE QaSqlite;',compiler_cleanup='if(QaSqlite)FreeLibrary(QaSqlite);');assert len(result)>=7 and all(value is True for key,value in result.items() if not key.endswith('error')),result;passed=True;print('PASS',len(result)-1,'native store-group service contracts')
 finally:
  if passed and fixture.resolve().parent==out.resolve():shutil.rmtree(fixture)
if __name__=='__main__':main()
