"""Real SQLite migration, editorial authority, ordering and immutable ZIP metadata."""
import argparse,json,shutil,uuid
from pathlib import Path
from locked_sdk import sdk_root
from native_product_runner import product
from extension_zip_fixture import platform_store_source
ROOT=Path(__file__).resolve().parents[1]
PLATFORM=Path('D:/GIT/home/host/xywhsoft_ai/plugin/mdo/modules/ecosystem')
CONTRACT=r'''
static void QaFlag(xvalue* R,cstr K,bool V){xrtValueObjectSetNew(R,xrtStrView(K),xrtValueBool(V));}
xvalue* QaProductContract(const xvalue* P,xvfs V){
 (void)V;xvalue* R=xrtValueObject(),*Out=NULL;char Error[512]={0};char* Directory=xrtFormat("%s/store",SdkField(P,"root")),*Db=xrtPathJoin(Directory,"catalog.db");xrtDirCreateAll(Directory);sqlite3* Legacy=NULL;sqlite3_initialize();
 bool Ok=sqlite3_open(Db,&Legacy)==SQLITE_OK&&sqlite3_exec(Legacy,"CREATE TABLE work(xid TEXT PRIMARY KEY,owner INTEGER NOT NULL,kind TEXT NOT NULL,alias TEXT NOT NULL,name TEXT NOT NULL,revision INTEGER NOT NULL DEFAULT 1,withdrawn INTEGER NOT NULL DEFAULT 0,deleted INTEGER NOT NULL DEFAULT 0,created INTEGER NOT NULL)",NULL,NULL,NULL)==SQLITE_OK;
 const cstr Aliases[]={"python","busybox","curl","jq","openssh","aria2","ripgrep","7zip","git"};char Works[9][33],Versions[9][33];
 for(size_t I=0;Ok&&I<9;I++){char* Id=xrtXidMakeString();snprintf(Works[I],33,"%s",Id);xrtFree(Id);sqlite3_stmt* Q=NULL;Ok=sqlite3_prepare_v2(Legacy,"INSERT INTO work(xid,owner,kind,alias,name,created) VALUES(?,0,'external-tools',?,?,0)",-1,&Q,NULL)==SQLITE_OK;if(Q){MdStoreBind(Q,1,Works[I]);MdStoreBind(Q,2,Aliases[I]);MdStoreBind(Q,3,Aliases[I]);}Ok=Ok&&MdStoreDone(Q);}sqlite3_close(Legacy);xrtFree(Db);
 MdDirectoryStore Store;Ok=Ok&&MdStoreInit(&Store,Directory,Error);QaFlag(R,"old_schema_migrated",Ok);
 for(size_t I=0;Ok&&I<9;I++){
  xvalue* Work=MdStoreWork(&Store,Works[I]);xvalue* Tags=MdStoreDisplayTags(Work);Ok=Work&&MdStoreInt(Work,"sort_order",-1)==(I==0?1000:I<8?900:0)&&xrtValueCount(Tags)==(I<8?1:0);xrtValueRelease(Tags);xrtValueRelease(Work);
  char* Id=xrtXidMakeString();snprintf(Versions[I],33,"%s",Id);xrtFree(Id);
  xvalue* M=SdkParse("{\"kind\":\"external-tools\",\"entry\":\"tool.json\",\"platforms\":[\"any\"],\"sort_order\":2147483647,\"tags\":[\"forged\"]}");char* Ref=xrtFormat("external-tool:mdo.%s",Aliases[I]);MdStorePut(M,"id",Ref);MdStorePut(M,"name",Aliases[I]);MdStorePut(M,"alias",Aliases[I]);xrtFree(Ref);char* Manifest=xrtJsonStringify(M,false,NULL);xrtValueRelease(M);
  sqlite3_stmt* Q=MdStoreSql(&Store,"INSERT INTO version(xid,work,owner,nonce,request_digest,manifest,sha256,bytes,proof_hash,proof_bytes,notes,source_url,source_included,state,requested,created) VALUES(?,?,0,?,'digest',?,'unchanged-hash',123,'',0,'','',1,'published',1,0)");if(Q){MdStoreBind(Q,1,Versions[I]);MdStoreBind(Q,2,Works[I]);MdStoreBind(Q,3,Versions[I]);MdStoreBind(Q,4,Manifest);}Ok=Ok&&MdStoreDone(Q);xrtFree(Manifest);
  Q=MdStoreSql(&Store,"INSERT INTO published(work,platform,version) VALUES(?,'any',?)");if(Q){MdStoreBind(Q,1,Works[I]);MdStoreBind(Q,2,Versions[I]);}Ok=Ok&&MdStoreDone(Q);
 }
 QaFlag(R,"only_original_seven_and_python_seeded",Ok);
 xvalue* Filter=SdkParse("{\"platform\":\"windows-x86_64\",\"limit\":2}");xvalue* Page=MdStoreList(&Store,0,false,0,Filter,Error);const xvalue* Items=SdkGet(Page,"items");bool Order=Ok&&Page&&MdStoreInt(Page,"total",0)==9&&!strcmp(SdkField(xrtValueArrayGet(Items,0),"alias"),"python")&&!strcmp(SdkField(xrtValueArrayGet(Items,1),"alias"),"7zip")&&MdStoreInt(xrtValueArrayGet(Items,0),"sort_order",0)==1000;
 xrtValueRelease(Page);SdkPutUInt(Filter,"offset",2);Page=MdStoreList(&Store,0,false,0,Filter,Error);Order=Order&&Page&&!strcmp(SdkField(xrtValueArrayGet(SdkGet(Page,"items"),0),"alias"),"ripgrep");xrtValueRelease(Page);QaFlag(R,"server_rank_then_id_before_pagination",Order);
 xvalue* Input=SdkParse("{\"sort_order\":1100,\"tags\":[\"推荐\",\"CLI\"],\"expected_revision\":1}");MdStorePut(Input,"work",Works[8]);bool Auth=!MdStoreSetPresentation(&Store,12,false,Input,&Out,Error)&&!MdStoreSetPresentation(&Store,0,true,Input,&Out,Error);xrtValueRelease(Out);Out=NULL;QaFlag(R,"creator_cannot_promote_work",Auth);
 bool Edit=MdStoreSetPresentation(&Store,9,true,Input,&Out,Error)&&MdStoreInt(Out,"sort_order",0)==1100&&xrtValueCount(SdkGet(Out,"tags"))==2;xrtValueRelease(Out);Out=NULL;Edit=Edit&&!MdStoreSetPresentation(&Store,9,true,Input,&Out,Error);xrtValueRelease(Out);Out=NULL;QaFlag(R,"administrator_edit_and_stale_revision_guard",Edit);
 SdkPutUInt(Input,"expected_revision",2);SdkPutUInt(Input,"sort_order",2147483648ULL);bool Invalid=!MdStoreSetPresentation(&Store,9,true,Input,&Out,Error);SdkPutUInt(Input,"sort_order",1);xvalue* Bad=SdkParse("[\"same\",\"same\"]");xrtValueObjectSet(Input,XRT_STR_LITERAL("tags"),Bad);xrtValueRelease(Bad);Invalid=Invalid&&!MdStoreSetPresentation(&Store,9,true,Input,&Out,Error);Bad=SdkParse("[\"bad\\nlabel\"]");xrtValueObjectSet(Input,XRT_STR_LITERAL("tags"),Bad);xrtValueRelease(Bad);Invalid=Invalid&&!MdStoreSetPresentation(&Store,9,true,Input,&Out,Error);QaFlag(R,"invalid_tags_and_overflow_rejected",Invalid);
 xvalue* Detail=MdStoreDetail(&Store,0,false,Versions[8],Error);bool Unchanged=Detail&&!strcmp(SdkField(Detail,"sha256"),"unchanged-hash")&&MdStoreInt(Detail,"bytes",0)==123&&MdStoreInt(SdkGet(Detail,"manifest"),"sort_order",0)==2147483647&&MdStoreInt(Detail,"sort_order",0)==1100;xrtValueRelease(Detail);QaFlag(R,"presentation_does_not_rewrite_archive_or_manifest",Unchanged);
 xrtValueRelease(Input);Input=SdkParse("{\"sort_order\":-8,\"tags\":[],\"expected_revision\":2}");MdStorePut(Input,"work",Works[0]);bool Reset=MdStoreSetPresentation(&Store,9,true,Input,&Out,Error);xrtValueRelease(Out);Out=NULL;xrtValueRelease(Input);MdStoreUnit(&Store);Reset=Reset&&MdStoreInit(&Store,Directory,Error);xvalue* Work=MdStoreWork(&Store,Works[0]);xvalue* Tags=MdStoreDisplayTags(Work);Reset=Reset&&MdStoreInt(Work,"sort_order",0)==-8&&xrtValueCount(Tags)==0;xrtValueRelease(Tags);xrtValueRelease(Work);QaFlag(R,"recommendations_do_not_override_later_admin_changes",Reset);
 xrtValueRelease(Filter);MdStoreUnit(&Store);xrtFree(Directory);return R;
}
'''
def main():
    p=argparse.ArgumentParser();p.add_argument('--url',required=True);p.add_argument('--home',type=Path,required=True);p.add_argument('--project',default='default');a=p.parse_args()
    out=ROOT/'.build/platform-presentation-native';out.mkdir(parents=True,exist_ok=True);fixture=out/('home-'+uuid.uuid4().hex[:12]);fixture.mkdir()
    shutil.copyfile(sdk_root()/'lib/sqlite3.h',fixture/'sqlite3.h')
    names=('initialize','bind_int','bind_text','prepare_v2','finalize','step','exec','column_count','column_name','column_type','column_int64','column_text','column_bytes','bind_int64','open','busy_timeout','close','changes')
    setup='QaSqlite=LoadLibraryW(L"C:/Program Files/Python312/DLLs/sqlite3.dll");if(!QaSqlite){xsDestroyTCC(T);T=NULL;}'
    setup+=''.join(f'if(T)tcc_add_symbol(T,"sqlite3_{name}",GetProcAddress(QaSqlite,"sqlite3_{name}"));' for name in names)
    try:
        result=product(a,fixture,platform_store_source(PLATFORM)+'\n'+CONTRACT,out/'result.json',compiler_setup=setup,compiler_prelude='#include <windows.h>\nstatic HMODULE QaSqlite;',compiler_cleanup='if(QaSqlite)FreeLibrary(QaSqlite);')
        assert result and all(v is True for v in result.values()),result
        print(json.dumps(result,ensure_ascii=False))
    finally:shutil.rmtree(fixture)
if __name__=='__main__':main()
