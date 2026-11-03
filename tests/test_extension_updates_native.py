"""Real store lookup, update worker and pinned multi-root installation contracts."""
import argparse,json,re,shutil,uuid,zipfile
from pathlib import Path
from locked_sdk import sdk_root
from native_product_runner import product
from extension_zip_fixture import directory_zip,platform_store_source
from test_extension_dependency_service_native import TRANSPORT,PLATFORM
from test_platform_directory_store_native import CONTRACT as STORE
from test_extension_dependency_store_native import TEST as DEPENDENCIES
ROOT=Path(__file__).resolve().parents[1]
EXTRA=r'''
static void QaFlag(xvalue* R,cstr K,bool B){xrtValueObjectSetNew(R,xrtStrView(K),xrtValueBool(B));}
static char QaFailVersion[33],QaSharedVersion[33];static unsigned QaSharedReads;
static char* QaDecode(cstr Input){char* R=xrtStrDup(Input?Input:"");size_t W=0;for(size_t I=0;R&&R[I];I++){if(R[I]=='%'&&R[I+1]&&R[I+2]){char H[3]={R[I+1],R[I+2],0};R[W++]=(char)strtoul(H,NULL,16);I+=2;}else R[W++]=R[I];}if(R)R[W]=0;return R;}
'''
LOOKUP=r''' }else if(strstr(Path,"/updates")){char* Enc=QaQuery(Path,"refs"),*Refs=QaDecode(Enc),*Platform=QaQuery(Path,"platform");xvalue* Q=xrtValueObject();MdStorePut(Q,"refs",Refs);MdStorePut(Q,"platform",Platform);Out=MdStoreUpdates(&QaStore,Q,E);Ok=Out!=NULL;xrtValueRelease(Q);xrtFree(Enc);xrtFree(Refs);xrtFree(Platform);
 }else if(strstr(Path,"/catalog"))'''
WRAPPER=r'''
static bool QaUpdateFetch(const XS_FetchRequest* Q,XS_FetchResponse* R){
 bool Before=QaCorrupt;if(QaFailVersion[0]&&strstr(Q->Url,"/archive?")&&strstr(Q->Url,QaFailVersion))QaCorrupt=true;
 if(QaSharedVersion[0]&&strstr(Q->Url,"/archive?")&&strstr(Q->Url,QaSharedVersion)&&strstr(Q->Url,"offset=0"))QaSharedReads++;
 bool Ok=QaFetch(Q,R);QaCorrupt=Before;return Ok;
}
'''
CONTRACT=r'''
static xvalue* QaUpdatePlan(cstr Version,char E[512]){xvalue* Q=xrtValueObject(),*P=NULL;SdkPut(Q,"action","install_plan");SdkPut(Q,"version_id",Version);QaMdoExtensionServicePlan(Q,NULL,&P,E);xrtValueRelease(Q);return P;}
static bool QaUpdateInstall(cstr Version,char E[512]){xvalue* P=QaUpdatePlan(Version,E),*Q=xrtValueObject(),*R=NULL;bool Ok=P&&Q&&SdkPut(Q,"plan_id",SdkField(P,"id"))&&SdkPut(Q,"revision",SdkField(P,"revision"))&&xrtValueObjectSetNew(Q,XRT_STR_LITERAL("trust_code"),xrtValueBool(true))&&QaMdoExtensionServiceApplyPlan(Q,NULL,&R,E);xrtValueRelease(P);xrtValueRelease(Q);xrtValueRelease(R);return Ok;}
xvalue* QaProductContract(const xvalue* P,xvfs V){
 xvalue* R=xrtValueObject();char E[512]={0};bool Home=QaProductHome(SdkField(P,"root"),SdkField(P,"app"),V);char* Root=xrtFormat("%s/platform",SdkField(P,"root"));bool Ok=Home&&MdoExtensionsInit()&&MdoExtensionTasksInit()&&MdStoreInit(&QaStore,Root,E);xrtFree(Root);
 const cstr Names[]={"a","b","c","d"};xvalue* Old[4]={0},*New[4]={0};
 for(size_t I=0;Ok&&I<4;I++){char File[16];snprintf(File,sizeof(File),"%s-old",Names[I]);Old[I]=QaDependencyPublish(&QaStore,SdkField(P,"root"),File,NULL,0,E);Ok=Old[I]!=NULL;}
 for(size_t I=1;Ok&&I<4;I++)Ok=QaUpdateInstall(SdkField(Old[I],"version_id"),E);
 xvalue* Disable=SdkParse("{\"action\":\"disable\",\"ref\":\"knowledge:qa.b\"}"),*Plan=NULL,*Applied=NULL;
 Ok=Ok&&MdoExtensionsPlan(Disable,NULL,&Plan,E);xrtValueRelease(Disable);Disable=xrtValueObject();SdkPut(Disable,"plan_id",SdkField(Plan,"id"));SdkPut(Disable,"revision",SdkField(Plan,"revision"));xrtValueObjectSetNew(Disable,XRT_STR_LITERAL("trust_code"),xrtValueBool(true));Ok=Ok&&MdoExtensionsApplyPlan(Disable,NULL,NULL,&Applied,E);xrtValueRelease(Plan);xrtValueRelease(Applied);xrtValueRelease(Disable);
 for(size_t I=0;Ok&&I<4;I++){char File[16];snprintf(File,sizeof(File),"%s-new",Names[I]);New[I]=QaDependencyPublish(&QaStore,SdkField(P,"root"),File,SdkField(Old[I],"xid"),(uint64)MdStoreInt(Old[I],"work_revision",0),E);Ok=New[I]!=NULL;}
 QaFlag(R,"publish_and_install_old_versions_then_publish_updates",Ok);
 xvalue* Query=SdkParse("{\"refs\":\"knowledge:qa.a,knowledge:qa.b,knowledge:qa.missing\",\"platform\":\"windows-x86_64\"}");xvalue* Lookup=MdStoreUpdates(&QaStore,Query,E);QaFlag(R,"batch_lookup_is_public_and_omits_missing_private_works",Lookup&&xrtValueCount(SdkGet(Lookup,"items"))==2);xrtValueRelease(Lookup);
 SdkPut(Query,"refs","knowledge:qa.a,knowledge:qa.a");Lookup=MdStoreUpdates(&QaStore,Query,E);QaFlag(R,"duplicate_lookup_ids_rejected",Lookup==NULL);xrtValueRelease(Lookup);SdkPut(Query,"refs","knowledge:qa.a,");Lookup=MdStoreUpdates(&QaStore,Query,E);QaFlag(R,"malformed_lookup_rejected",Lookup==NULL);xrtValueRelease(Lookup);xrtValueRelease(Query);
 bool Started=Ok&&QaMdoExtensionUpdatesInit()&&QaMdoExtensionUpdatesRequest();uint64 End=MdoDeadlineAfterUs(8000000);xvalue* Snapshot=NULL;
 do{xrtValueRelease(Snapshot);Snapshot=QaMdoExtensionUpdatesSnapshot();if(MdoUpdatesNumber(Snapshot,"checked_at")&&!MdoGraphBool(Snapshot,"busy",false))break;xrtSleep(20);}while(Started&&!MdoDeadlineExpired(End));
 QaFlag(R,"background_worker_finds_all_four_updates",Started&&Snapshot&&xrtValueCount(SdkGet(Snapshot,"items"))==4);
 xvalue* Candidate=xrtValueDeepClone(xrtValueArrayGet(SdkGet(Snapshot,"items"),0)),*Inventory=QaMdoUpdatesInventory(E);const xvalue* Local=Candidate?QaMdoUpdatesFind(Inventory,SdkField(Candidate,"id")):NULL;
 uint64 Serial=QaMdoUpdatesNumber(SdkGet(Local,"installation"),"serial");SdkPut(Candidate,"version","0.0.0");QaFlag(R,"display_version_does_not_control_updates",Candidate&&QaMdoUpdatesCandidate(Local,Candidate,Serial));
 SdkPutUInt(Candidate,"serial",Serial);QaFlag(R,"withdrawal_or_older_head_is_not_an_update",!QaMdoUpdatesCandidate(Local,Candidate,Serial));SdkPutUInt(Candidate,"serial",Serial+1);SdkPut(Candidate,"xid","ffffffffffffffffffffffffffffffff");QaFlag(R,"identity_mismatch_is_not_an_update",!QaMdoUpdatesCandidate(Local,Candidate,Serial));xrtValueRelease(Candidate);xrtValueRelease(Inventory);xrtValueRelease(Snapshot);Snapshot=NULL;
 QaTransientReads=1;QaReadStatus=403;QaMdoExtensionUpdatesRequest();End=MdoDeadlineAfterUs(3000000);
 do{xrtValueRelease(Snapshot);Snapshot=QaMdoExtensionUpdatesSnapshot();if(SdkField(Snapshot,"message")&&SdkField(Snapshot,"message")[0])break;xrtSleep(20);}while(!MdoDeadlineExpired(End));
 QaFlag(R,"failed_check_keeps_known_updates",Snapshot&&xrtValueCount(SdkGet(Snapshot,"items"))==4&&MdoGraphBool(Snapshot,"stale",false));xrtValueRelease(Snapshot);QaReadStatus=0;QaTransientReads=0;QaMdoExtensionUpdatesUnit();
 xvalue* Batch=xrtValueObject(),*Plans=xrtValueArray();
 for(size_t I=1;I<4;I++){Plan=QaUpdatePlan(SdkField(New[I],"version_id"),E);xvalue* Item=xrtValueObject();Ok=Ok&&Plan&&SdkPut(Item,"id",SdkField(Plan,"id"))&&SdkPut(Item,"revision",SdkField(Plan,"revision"))&&xrtValueArrayAppend(Plans,Item);xrtValueRelease(Plan);xrtValueRelease(Item);}
 xrtValueObjectSet(Batch,XRT_STR_LITERAL("plans"),Plans);xrtValueRelease(Plans);xrtValueObjectSetNew(Batch,XRT_STR_LITERAL("trust_code"),xrtValueBool(true));
 snprintf(QaSharedVersion,sizeof(QaSharedVersion),"%s",SdkField(New[0],"version_id"));snprintf(QaFailVersion,sizeof(QaFailVersion),"%s",SdkField(New[3],"version_id"));QaSharedReads=0;
 xvalue* Receipt=NULL;bool Ran=Ok&&QaMdoExtensionServiceUpdateBatch(Batch,NULL,&Receipt,E);const xvalue* Results=SdkGet(Receipt,"results");
 QaFlag(R,"batch_successes_survive_unrelated_corrupt_package",Ran&&xrtValueCount(Results)==3&&!strcmp(SdkField(xrtValueArrayGet(Results,0),"state"),"complete")&&!strcmp(SdkField(xrtValueArrayGet(Results,1),"state"),"complete")&&!strcmp(SdkField(xrtValueArrayGet(Results,2),"state"),"failed"));
 QaFlag(R,"shared_dependency_downloaded_once",Ran&&QaSharedReads==1);
 xvalue* B=MdoExtensionsResolve("knowledge:qa.b",E),*C=MdoExtensionsResolve("knowledge:qa.c",E),*D=MdoExtensionsResolve("knowledge:qa.d",E);
 QaFlag(R,"disabled_extension_stays_disabled",B&&!MdoGraphBool(B,"desired_enabled",true));
 QaFlag(R,"successful_roots_updated_and_failed_root_kept_old",B&&C&&D&&!strcmp(SdkField(SdkGet(B,"installation"),"version_id"),SdkField(New[1],"version_id"))&&!strcmp(SdkField(SdkGet(C,"installation"),"version_id"),SdkField(New[2],"version_id"))&&!strcmp(SdkField(SdkGet(D,"installation"),"version_id"),SdkField(Old[3],"version_id")));
 xrtValueRelease(B);xrtValueRelease(C);xrtValueRelease(D);xrtValueRelease(Receipt);xrtValueRelease(Batch);
 for(size_t I=0;I<4;I++){xrtValueRelease(Old[I]);xrtValueRelease(New[I]);}MdStoreUnit(&QaStore);MdoExtensionTasksUnit();MdoExtensionsUnit();if(Home)MdoHomeUnit();SdkPut(R,"error",E);return R;
}
'''
def main():
 p=argparse.ArgumentParser();p.add_argument('--url',required=True);p.add_argument('--home',type=Path,required=True);p.add_argument('--project',default='default');a=p.parse_args();out=ROOT/'.build/extension-updates-native';out.mkdir(exist_ok=True);fixture=out/('case-'+uuid.uuid4().hex[:8]);fixture.mkdir();shutil.copyfile(sdk_root()/'lib/sqlite3.h',fixture/'sqlite3.h')
 for name in 'abcd':
  for stage in ('old','new'):
   path=fixture/f'{name}-{stage}.zip';directory_zip(path,'knowledge',alias=name,version='1.0.0')
   with zipfile.ZipFile(path) as z:files={n:z.read(n) for n in z.namelist()}
   meta=json.loads(files['extension.json']);meta.update(id='knowledge:qa.'+name,dependencies=[] if name in 'ad' else [dict(id='knowledge:qa.a')]);files['extension.json']=json.dumps(meta);files['index.md']=stage+' reference'
   with zipfile.ZipFile(path,'w',zipfile.ZIP_DEFLATED) as z:
    for file,data in files.items():z.writestr(file,data)
 client=(ROOT/'app/src/extensions/service.c').read_text(encoding='utf-8').replace('#include "service_plan.inc.c"',(ROOT/'app/src/extensions/service_plan.inc.c').read_text(encoding='utf-8'));client=re.sub(r'^#include .*$', '',client,flags=re.M)
 updates=re.sub(r'^#include .*$', '',(ROOT/'app/src/extensions/updates.c').read_text(encoding='utf-8'),flags=re.M)
 client=re.sub(r'\b(MdoExtService\w+|MdoExtensionService\w+)\b',r'Qa\1',client)
 updates=re.sub(r'\b(MdoUpdates\w*|MdoExtensionUpdates\w*|MdoExtensionService\w+)\b',r'Qa\1',updates)
 updates='void QaMdoExtensionUpdatesUnit(void);\n'+updates
 for old,new in dict(MdoAccountAcquireServiceWait='QaLease',MdoAccountRelease='QaRelease',MdoAccountRejectAccess='QaReject',xsFetch='QaUpdateFetch',xsFetchResponseUnit='QaResponseUnit').items():client=re.sub(r'\b'+old+r'\b',new,client)
 transport=TRANSPORT.replace(' }else if(strstr(Path,"/catalog"))',LOOKUP)
 symbols=('initialize','bind_int','bind_text','prepare_v2','finalize','step','exec','column_count','column_name','column_type','column_int64','column_text','column_bytes','bind_int64','open','busy_timeout','close','changes');setup='QaSqlite=LoadLibraryW(L"C:/Program Files/Python312/DLLs/sqlite3.dll");'+''.join(f'tcc_add_symbol(T,"sqlite3_{n}",GetProcAddress(QaSqlite,"sqlite3_{n}"));' for n in symbols)
 source=platform_store_source(PLATFORM)+(PLATFORM/'directory_updates.inc.c').read_text(encoding='utf-8')+STORE.split('xvalue* QaProductContract(')[0]+DEPENDENCIES.split('xvalue* QaProductContract(')[0]+EXTRA+transport+WRAPPER+client+updates+CONTRACT
 result=product(a,fixture,source,out/'result.json',compiler_setup=setup,compiler_prelude='#include <windows.h>\nstatic HMODULE QaSqlite;',compiler_cleanup='if(QaSqlite)FreeLibrary(QaSqlite);')
 assert all(v is True for k,v in result.items() if k!='error'),result
 print('PASS',sum(type(v) is bool for v in result.values()),'native update contracts')
 if fixture.resolve().parent==out.resolve():shutil.rmtree(fixture)
if __name__=='__main__':main()
