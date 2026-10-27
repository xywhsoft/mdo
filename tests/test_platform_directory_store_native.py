"""Compile actual new platform ZIP storage in the existing xs compiler host."""
import argparse
import json
from pathlib import Path
import shutil
import uuid
import zipfile
from native_product_runner import product

ROOT=Path(__file__).resolve().parents[1]
PLATFORM=Path('D:/GIT/home/host/xywhsoft_ai/plugin/mdo/modules/ecosystem')
CONTRACT=r'''
static void QaStoreFlag(xvalue* R,cstr K,bool V){xrtValueObjectSetNew(R,xrtStrView(K),xrtValueBool(V));}
static bool QaStoreInput(MdDirectoryStore* S,int64 Owner,cstr Kind,cstr File,char Id[33],char Error[512]){
 xfile F=xrtOpen(File,XFILE_READ);uint64 Size=0;char Hash[65];bool Ok=F&&MdStoreDigest(F,NULL,Hash,&Size);xrtClose(F);xvalue* Input=xrtValueObject(),*Out=NULL;
 MdStorePut(Input,"kind",Kind);MdStorePut(Input,"purpose","archive");MdStorePut(Input,"sha256",Hash);xrtValueObjectSetNew(Input,XRT_STR_LITERAL("bytes"),xrtValueUInt(Size));
 Ok=Ok&&MdStoreBegin(S,Owner,false,Input,&Out,Error);if(Ok)snprintf(Id,33,"%s",MdoExtensionText(Out,"xid",32));xrtValueRelease(Input);xrtValueRelease(Out);return Ok;
}
xvalue* QaProductContract(const xvalue* P,xvfs V){
 (void)V;xvalue* R=xrtValueObject(),*Out=NULL;char Error[512]={0};char* Directory=xrtFormat("%s/store",SdkField(P,"root"));MdDirectoryStore Store;bool Ok=MdStoreInit(&Store,Directory,Error);xrtFree(Directory);
 const cstr Kinds[]={"agents","subagents","tools","mcp","skills","commands","external-tools","knowledge","plugins"};size_t Passed=0;
 for(size_t I=0;Ok&&I<9;I++){
  char* Path=xrtFormat("%s/%s.zip",SdkField(P,"root"),Kinds[I]);char Id[33]={0};Ok=QaStoreInput(&Store,12,Kinds[I],Path,Id,Error);xfile F=Ok?xrtOpen(Path,XFILE_READ):NULL;uint64 Size=0;xrtFileSize(F,&Size);unsigned char Buffer[65536];
  for(uint64 At=0;Ok&&At<Size;){size_t N=(size_t)(Size-At<sizeof(Buffer)?Size-At:sizeof(Buffer));Ok=xrtReadAtFull(F,At,Buffer,N,NULL)&&MdStoreChunk(&Store,12,Id,At,Buffer,N,&Out,Error);xrtValueRelease(Out);Out=NULL;At+=N;}
  xrtClose(F);xrtFree(Path);Ok=Ok&&MdStoreSeal(&Store,12,Id,NULL,&Out,Error)&&!strcmp(MdoExtensionText(Out,"state",16),"sealed");xrtValueRelease(Out);Out=NULL;
  if(Ok&&I==0){sqlite3_stmt* Q=MdStoreSql(&Store,"UPDATE upload SET state='object_pending' WHERE xid=?");MdStoreBind(Q,1,Id);bool Recovered=MdStoreDone(Q)&&MdStoreSeal(&Store,12,Id,NULL,&Out,Error);xrtValueRelease(Out);Out=NULL;QaStoreFlag(R,"promoted_object_recovers_after_restart_window",Recovered);Ok=Ok&&Recovered;}
  Ok=Ok&&!MdStoreSeal(&Store,99,Id,NULL,&Out,Error);xrtValueRelease(Out);Out=NULL;Ok=Ok&&MdStoreCancel(&Store,12,Id,Error);if(Ok)Passed++;
 }
 QaStoreFlag(R,"nine_real_zip_types",Passed==9);SdkPutUInt(R,"types",Passed);
 char Id[33]={0};char* Path=xrtFormat("%s/mcp.zip",SdkField(P,"root"));bool Replay=QaStoreInput(&Store,12,"mcp",Path,Id,Error);xfile F=xrtOpen(Path,XFILE_READ);unsigned char Buffer[128];Replay=Replay&&F&&xrtReadFull(F,Buffer,sizeof(Buffer),NULL)&&MdStoreChunk(&Store,12,Id,0,Buffer,128,&Out,Error);xrtValueRelease(Out);Out=NULL;
 Replay=Replay&&MdStoreChunk(&Store,12,Id,0,Buffer,128,&Out,Error);xrtValueRelease(Out);Out=NULL;Buffer[0]^=1;Replay=Replay&&!MdStoreChunk(&Store,12,Id,0,Buffer,128,&Out,Error);xrtValueRelease(Out);Out=NULL;
 Replay=Replay&&!MdStoreChunk(&Store,99,Id,128,Buffer,128,&Out,Error);xrtValueRelease(Out);Out=NULL;QaStoreFlag(R,"raw_replay_conflict_and_owner_checks",Replay);
 bool Incomplete=!MdStoreSeal(&Store,12,Id,NULL,&Out,Error);xrtValueRelease(Out);Out=NULL;xvalue* Row=MdStoreUpload(&Store,Id,12);Incomplete=Incomplete&&Row&&!strcmp(MdoExtensionText(Row,"state",16),"receiving");xrtValueRelease(Row);QaStoreFlag(R,"incomplete_upload_can_resume",Incomplete);MdStoreCancel(&Store,12,Id,Error);xrtClose(F);xrtFree(Path);
 cstr Json="{\"format\":\"mdo.extension.v2\",\"files\":[]}";char Hash[65];MdoExtensionHash(Json,Hash);xvalue* Input=SdkParse("{\"purpose\":\"archive\",\"kind\":\"mcp\"}");MdStorePut(Input,"sha256",Hash);SdkPutUInt(Input,"bytes",strlen(Json));bool Legacy=MdStoreBegin(&Store,12,false,Input,&Out,Error);if(Legacy)snprintf(Id,33,"%s",MdoExtensionText(Out,"xid",32));xrtValueRelease(Out);Out=NULL;Legacy=Legacy&&MdStoreChunk(&Store,12,Id,0,Json,strlen(Json),&Out,Error);xrtValueRelease(Out);Out=NULL;Legacy=Legacy&&!MdStoreSeal(&Store,12,Id,NULL,&Out,Error);xrtValueRelease(Out);Out=NULL;QaStoreFlag(R,"json_publication_is_rejected",Legacy);xrtValueRelease(Input);
 char Guess[33]={0};Path=xrtFormat("%s/mcp.zip",SdkField(P,"root"));bool Isolation=QaStoreInput(&Store,12,"mcp",Path,Guess,Error)&&!MdStoreSeal(&Store,12,Guess,NULL,&Out,Error);xrtValueRelease(Out);Out=NULL;Row=MdStoreUpload(&Store,Guess,12);Isolation=Isolation&&Row&&!strcmp(MdoExtensionText(Row,"state",16),"receiving");xrtValueRelease(Row);QaStoreFlag(R,"known_hash_cannot_claim_another_archive",Isolation);
 MdStoreUse Held={0};snprintf(Held.Id,sizeof(Held.Id),"%s",Guess);Held.Cancel=xrtCancelCreate();Store.Uses=&Held;sqlite3_stmt* Change=MdStoreSql(&Store,"UPDATE upload SET state='sealing' WHERE xid=?");MdStoreBind(Change,1,Guess);MdStoreDone(Change);
 bool Busy=!MdStoreSeal(&Store,12,Guess,NULL,&Out,Error);xrtValueRelease(Out);Out=NULL;Row=MdStoreUpload(&Store,Guess,12);Busy=Busy&&Row&&!strcmp(MdoExtensionText(Row,"state",16),"sealing");xrtValueRelease(Row);Busy=Busy&&MdStoreCancel(&Store,12,Guess,Error)&&xrtCancelRequested(Held.Cancel);Store.Uses=NULL;xrtCancelDestroy(Held.Cancel);QaStoreFlag(R,"busy_validation_and_cancel_preserve_ownership",Busy);
 char A[33],B[33],C[33],D[33];bool Quota=QaStoreInput(&Store,15,"mcp",Path,A,Error)&&QaStoreInput(&Store,15,"mcp",Path,B,Error)&&QaStoreInput(&Store,15,"mcp",Path,C,Error)&&!QaStoreInput(&Store,15,"mcp",Path,D,Error);MdStoreCancel(&Store,15,A,Error);MdStoreCancel(&Store,15,B,Error);MdStoreCancel(&Store,15,C,Error);xrtFree(Path);QaStoreFlag(R,"per_account_staging_quota",Quota);
 QaStoreFlag(R,"all_object_files_are_zip",Ok);SdkPut(R,"error",Error);MdStoreUnit(&Store);return R;
}
'''

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--url',required=True);p.add_argument('--home',type=Path,required=True);p.add_argument('--project',default='default');a=p.parse_args()
    out=ROOT/'.build/platform-directory-store-native';out.mkdir(parents=True,exist_ok=True);fixture=out/('home-'+uuid.uuid4().hex[:12]);fixture.mkdir()
    shutil.copyfile(ROOT/'.build/sdk-overlays/16f28b0bd9ecf791b505bbff9001a0c18bbaf398e29d906dc01df967818bcb84/lib/sqlite3.h',fixture/'sqlite3.h')
    kinds=('agents','subagents','tools','mcp','skills','commands','external-tools','knowledge','plugins')
    for kind in kinds:
        with zipfile.ZipFile(fixture/(kind+'.zip'),'w',zipfile.ZIP_DEFLATED) as z:
            z.writestr('extension.json',json.dumps(dict(kind=kind,name='ZIP store fixture',alias='zip-fixture',entry='entry.txt',platforms=['any'])))
            z.writestr('entry.txt','Stored as a real file\n')
            z.writestr('.hidden/deep/data.bin',bytes(range(256))*7)
            z.writestr('empty/',b'')
    core=(PLATFORM/'directory_store.inc.c').read_text(encoding='utf-8').replace('#include "extension_manifest.h"','#include "/app/include/mdo/extension_manifest.h"').replace('#include <sqlite3.h>','#include "/app/qa/sqlite3.h"')
    try:
        names=('initialize','bind_text','prepare_v2','finalize','step','exec','column_count','column_name','column_type','column_int64','column_text','column_bytes','bind_int64','open','busy_timeout','close','changes')
        setup='QaSqlite=LoadLibraryW(L"C:/Program Files/Python312/DLLs/sqlite3.dll");if(!QaSqlite){snprintf(QaLog,sizeof(QaLog),"SQLite loader failed %lu",GetLastError());xsDestroyTCC(T);T=NULL;}'
        setup+=''.join(f'if(T){{void* Address=GetProcAddress(QaSqlite,"sqlite3_{name}");if(Address)tcc_add_symbol(T,"sqlite3_{name}",Address);else{{snprintf(QaLog,sizeof(QaLog),"Missing SQLite symbol {name}");xsDestroyTCC(T);T=NULL;}}}}' for name in names)
        result=product(a,fixture,core+'\n'+CONTRACT,out/'result.json',compiler_setup=setup,compiler_prelude='#include <windows.h>\nstatic HMODULE QaSqlite;',compiler_cleanup='if(QaSqlite)FreeLibrary(QaSqlite);')
        assert result.get('types')==9 and all(result.get(k) is True for k in ('nine_real_zip_types','raw_replay_conflict_and_owner_checks','incomplete_upload_can_resume','json_publication_is_rejected','all_object_files_are_zip','promoted_object_recovers_after_restart_window','known_hash_cannot_claim_another_archive','busy_validation_and_cancel_preserve_ownership','per_account_staging_quota')),result
        for path in (fixture/'store/objects').iterdir():
            with zipfile.ZipFile(path) as z:assert z.testzip() is None
        print(json.dumps(result,ensure_ascii=False))
    finally:
        if fixture.resolve().parent==out.resolve() and fixture.name.startswith('home-'):shutil.rmtree(fixture)

if __name__=='__main__':main()
