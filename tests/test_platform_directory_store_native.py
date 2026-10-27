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
static bool QaStoreFinish(MdDirectoryStore* S,int64 Owner,cstr Kind,cstr File,char Id[33],char Error[512]){
 bool Ok=QaStoreInput(S,Owner,Kind,File,Id,Error);xfile F=Ok?xrtOpen(File,XFILE_READ):NULL;uint64 Size=0;xrtFileSize(F,&Size);unsigned char B[65536];xvalue* Out=NULL;
 for(uint64 At=0;Ok&&At<Size;){size_t N=(size_t)(Size-At<sizeof(B)?Size-At:sizeof(B));Ok=xrtReadAtFull(F,At,B,N,NULL)&&MdStoreChunk(S,Owner,Id,At,B,N,&Out,Error);xrtValueRelease(Out);Out=NULL;At+=N;}
 xrtClose(F);if(Ok)Ok=MdStoreSeal(S,Owner,Id,NULL,&Out,Error);xrtValueRelease(Out);return Ok;
}
static xvalue* QaStoreSubmitBody(cstr Upload,bool Publish){
 xvalue* B=SdkParse("{\"review_notes\":\"Review the complete ZIP source and dependencies\",\"source_included\":true}");char* Nonce=xrtXidMakeString();MdStorePut(B,"nonce",Nonce);MdStorePut(B,"upload",Upload);xrtFree(Nonce);xrtValueObjectSetNew(B,XRT_STR_LITERAL("publish"),xrtValueBool(Publish));return B;
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
 QaStoreFlag(R,"all_object_files_are_zip",Ok);
 size_t Lifecycles=0;bool Idempotent=true,Permissions=true,Withdraw=true;
 for(size_t I=0;I<9;I++){
  int64 Owner=100+(int64)I;char U[33]={0},Version[33]={0},Work[33]={0},Hash[65];uint64 Bytes=0;Path=xrtFormat("%s/%s.zip",SdkField(P,"root"),Kinds[I]);bool Good=QaStoreFinish(&Store,Owner,Kinds[I],Path,U,Error);xvalue* Body=QaStoreSubmitBody(U,true);
  Good=Good&&MdStoreSubmit(&Store,Owner,false,Body,&Out,Error);if(Good){snprintf(Version,sizeof(Version),"%s",MdoExtensionText(Out,"version_id",32));snprintf(Work,sizeof(Work),"%s",MdoExtensionText(Out,"xid",32));}xrtValueRelease(Out);Out=NULL;
  bool Duplicate=Good&&MdStoreSubmit(&Store,Owner,false,Body,&Out,Error)&&!strcmp(Version,MdoExtensionText(Out,"version_id",32));xrtValueRelease(Out);Out=NULL;MdStorePut(Body,"review_notes","Changed retry");Duplicate=Duplicate&&!MdStoreSubmit(&Store,Owner,false,Body,&Out,Error);xrtValueRelease(Out);Out=NULL;Idempotent=Idempotent&&Duplicate;xrtValueRelease(Body);
  xvalue* Review=SdkParse("{\"action\":\"approve\",\"expected_revision\":1,\"reason\":\"Source checked\"}");MdStorePut(Review,"version",Version);
  Permissions=Permissions&&!MdStoreDetail(&Store,0,false,Version,Error)&&!MdStoreReview(&Store,Owner,false,Review,&Out,Error);xrtValueRelease(Out);Out=NULL;
  Good=Good&&MdStoreReview(&Store,9,true,Review,&Out,Error);xrtValueRelease(Out);Out=NULL;xfile Download=MdStoreDownload(&Store,0,false,Version,false,Hash,&Bytes,Error);Good=Good&&Download&&Bytes>0;xrtClose(Download);
  if(I==0&&Good){char Pending[2][33];for(size_t J=0;J<2&&Good;J++){char NewUpload[33];Good=QaStoreFinish(&Store,Owner,Kinds[I],Path,NewUpload,Error);xvalue* Update=QaStoreSubmitBody(NewUpload,true);MdStorePut(Update,"work",Work);SdkPutUInt(Update,"expected_revision",2+J);Good=Good&&MdStoreSubmit(&Store,Owner,false,Update,&Out,Error);if(Good)snprintf(Pending[J],33,"%s",MdoExtensionText(Out,"version_id",32));xrtValueRelease(Out);Out=NULL;xrtValueRelease(Update);}
   MdStorePut(Review,"version",Pending[1]);Good=Good&&MdStoreReview(&Store,9,true,Review,&Out,Error);xrtValueRelease(Out);Out=NULL;MdStorePut(Review,"version",Pending[0]);Good=Good&&!MdStoreReview(&Store,9,true,Review,&Out,Error);xrtValueRelease(Out);Out=NULL;QaStoreFlag(R,"old_review_cannot_replace_a_newer_release",Good);
  }
  if(I==3&&Good){char NewUpload[33],PendingVersion[33];Good=QaStoreFinish(&Store,Owner,Kinds[I],Path,NewUpload,Error);xvalue* Update=QaStoreSubmitBody(NewUpload,true);MdStorePut(Update,"work",Work);SdkPutUInt(Update,"expected_revision",2);Good=Good&&MdStoreSubmit(&Store,Owner,false,Update,&Out,Error);if(Good)snprintf(PendingVersion,sizeof(PendingVersion),"%s",MdoExtensionText(Out,"version_id",32));xrtValueRelease(Out);Out=NULL;xrtValueRelease(Update);
   sqlite3_stmt* Head=MdStoreSql(&Store,"SELECT version FROM published WHERE work=? AND platform='any'");MdStoreBind(Head,1,Work);xvalue* OldHead=MdStoreRow(Head);Good=Good&&OldHead&&!strcmp(Version,MdoExtensionText(OldHead,"version",32));xrtValueRelease(OldHead);QaStoreFlag(R,"pending_update_keeps_published_version",Good);
   xvalue* Action=SdkParse("{\"action\":\"withdraw\",\"expected_revision\":3}");MdStorePut(Action,"work",Work);Withdraw=Withdraw&&!MdStoreWorkAction(&Store,999,false,Action,&Out,Error);xrtValueRelease(Out);Out=NULL;Withdraw=Withdraw&&MdStoreWorkAction(&Store,Owner,false,Action,&Out,Error);xrtValueRelease(Out);Out=NULL;
   MdStorePut(Review,"version",PendingVersion);Withdraw=Withdraw&&!MdStoreReview(&Store,9,true,Review,&Out,Error)&&!MdStoreDetail(&Store,0,false,Version,Error);xrtValueRelease(Out);Out=NULL;MdStorePut(Action,"action","remove");SdkPutUInt(Action,"expected_revision",4);xvalue* RemovedVersion=MdStoreVersion(&Store,Version);char RemovedHash[65]={0};if(RemovedVersion)snprintf(RemovedHash,sizeof(RemovedHash),"%s",MdoExtensionText(RemovedVersion,"sha256",64));xrtValueRelease(RemovedVersion);
   Withdraw=Withdraw&&MdStoreWorkAction(&Store,Owner,false,Action,&Out,Error);xrtValueRelease(Out);Out=NULL;char* Object=xrtFormat("objects/%s",RemovedHash);bool Remains=true;xfileinfo ObjectInfo;bool Clean=MdStoreStat(&Store,Object,&Remains,&ObjectInfo)&&!Remains;QaStoreFlag(R,"deleted_work_releases_unshared_archive",Clean);xrtFree(Object);xrtValueRelease(Action);
  }
  xrtValueRelease(Review);xrtFree(Path);if(Good)Lifecycles++;
 }
 SdkPutUInt(R,"published_types",Lifecycles);QaStoreFlag(R,"submission_retry_is_idempotent",Idempotent);QaStoreFlag(R,"private_and_review_permissions",Permissions);QaStoreFlag(R,"withdrawal_cancels_pending_publication",Withdraw);
 xvalue* Filter=SdkParse("{\"platform\":\"windows-x86_64\",\"limit\":2}");xvalue* Page=MdStoreList(&Store,0,false,0,Filter,Error);bool Paged=Page&&MdStoreInt(Page,"total",0)==8&&xrtValueCount(SdkGet(Page,"items"))==2;xrtValueRelease(Page);SdkPutUInt(Filter,"offset",2);Page=MdStoreList(&Store,0,false,0,Filter,Error);Paged=Paged&&Page&&xrtValueCount(SdkGet(Page,"items"))==2;xrtValueRelease(Page);Paged=Paged&&!MdStoreList(&Store,100,false,2,Filter,Error);xrtValueRelease(Filter);QaStoreFlag(R,"public_paging_and_admin_scope",Paged);
 bool WorkQuota=true;Path=xrtFormat("%s/skills.zip",SdkField(P,"root"));xvalue* Last=NULL;
 for(size_t J=0;J<4;J++){char NewUpload[33];bool Fresh=QaStoreFinish(&Store,777,"skills",Path,NewUpload,Error);xvalue* Request=QaStoreSubmitBody(NewUpload,false);bool Saved=Fresh&&MdStoreSubmit(&Store,777,false,Request,&Out,Error);xrtValueRelease(Out);Out=NULL;if(J<3)WorkQuota=WorkQuota&&Saved;else{WorkQuota=WorkQuota&&!Saved;Last=Request;Request=NULL;}xrtValueRelease(Request);}
 Page=MdStoreProfile(&Store,777,Error);WorkQuota=WorkQuota&&Page&&MdStoreInt(Page,"works",0)==3&&MdStoreInt(Page,"work_limit",0)==3&&MdStoreInt(Page,"version_bytes",0)==10485760;xrtValueRelease(Page);QaStoreFlag(R,"default_three_work_quota",WorkQuota);
 xvalue* Apply=SdkParse("{\"type\":\"creator\",\"reason\":\"Publish additional source tools\"}");bool Creator=MdStoreCreatorRequest(&Store,777,Apply,&Out,Error);char RequestId[33]={0};if(Creator)snprintf(RequestId,sizeof(RequestId),"%s",MdoExtensionText(Out,"xid",32));xrtValueRelease(Out);Out=NULL;xrtValueRelease(Apply);
 xvalue* Decision=SdkParse("{\"action\":\"approve\",\"expected_revision\":1,\"reason\":\"Source reviewed\"}");MdStorePut(Decision,"request",RequestId);Creator=Creator&&!MdStoreCreatorDecision(&Store,777,false,Decision,&Out,Error);xrtValueRelease(Out);Out=NULL;Creator=Creator&&MdStoreCreatorDecision(&Store,9,true,Decision,&Out,Error)&&MdStoreInt(Out,"work_limit",0)==20&&MdStoreInt(Out,"version_bytes",0)==104857600;xrtValueRelease(Out);Out=NULL;Creator=Creator&&MdStoreSubmit(&Store,777,false,Last,&Out,Error);xrtValueRelease(Out);Out=NULL;xrtValueRelease(Last);xrtValueRelease(Decision);QaStoreFlag(R,"creator_application_unlocks_twenty_works",Creator);
 Apply=SdkParse("{\"type\":\"capacity\",\"requested\":35,\"reason\":\"More extension projects\"}");bool Capacity=MdStoreCreatorRequest(&Store,777,Apply,&Out,Error);if(Capacity)snprintf(RequestId,sizeof(RequestId),"%s",MdoExtensionText(Out,"xid",32));xrtValueRelease(Out);Out=NULL;xrtValueRelease(Apply);Decision=SdkParse("{\"action\":\"approve\",\"expected_revision\":1,\"reason\":\"Capacity approved\"}");MdStorePut(Decision,"request",RequestId);Capacity=Capacity&&MdStoreCreatorDecision(&Store,9,true,Decision,&Out,Error)&&MdStoreInt(Out,"work_limit",0)==35;xrtValueRelease(Out);Out=NULL;xrtValueRelease(Decision);QaStoreFlag(R,"capacity_request_and_admin_approval",Capacity);
 xvalue* Grant=SdkParse("{\"owner\":778,\"creator\":true,\"work_limit\":40,\"expected_revision\":0}");bool Granted=!MdStoreGrant(&Store,777,false,Grant,&Out,Error);xrtValueRelease(Out);Out=NULL;Granted=Granted&&MdStoreGrant(&Store,9,true,Grant,&Out,Error)&&MdStoreInt(Out,"work_limit",0)==40;xrtValueRelease(Out);Out=NULL;Granted=Granted&&!MdStoreGrant(&Store,9,true,Grant,&Out,Error);xrtValueRelease(Out);Out=NULL;xrtValueRelease(Grant);xrtFree(Path);QaStoreFlag(R,"direct_grants_require_admin_and_revision",Granted);
 Page=MdStoreList(&Store,777,false,1,NULL,Error);const xvalue* PrivateRow=xrtValueArrayGet(SdkGet(Page,"items"),0);char PrivateVersion[33]={0},PrivateWork[33]={0};if(PrivateRow){snprintf(PrivateVersion,sizeof(PrivateVersion),"%s",MdoExtensionText(PrivateRow,"version_id",32));snprintf(PrivateWork,sizeof(PrivateWork),"%s",MdoExtensionText(PrivateRow,"xid",32));}xrtValueRelease(Page);
 xvalue* Publish=SdkParse("{\"expected_revision\":1,\"review_notes\":\"The ZIP contains source\",\"source_included\":true}");MdStorePut(Publish,"version",PrivateVersion);bool Existing=!MdStorePublishExisting(&Store,888,Publish,&Out,Error);xrtValueRelease(Out);Out=NULL;Existing=Existing&&MdStorePublishExisting(&Store,777,Publish,&Out,Error)&&!strcmp(MdoExtensionText(Out,"state",16),"pending");xrtValueRelease(Out);Out=NULL;xrtValueRelease(Publish);
 xvalue* Review=SdkParse("{\"action\":\"approve\",\"expected_revision\":2,\"reason\":\"Reviewed private version\"}");MdStorePut(Review,"version",PrivateVersion);Existing=Existing&&MdStoreReview(&Store,9,true,Review,&Out,Error);xrtValueRelease(Out);Out=NULL;xrtValueRelease(Review);QaStoreFlag(R,"existing_private_version_can_request_publication",Existing);
 xvalue* Delete=SdkParse("{\"expected_revision\":3}");MdStorePut(Delete,"version",PrivateVersion);bool Deleted=!MdStoreDeleteVersion(&Store,777,false,Delete,&Out,Error);xrtValueRelease(Out);Out=NULL;Apply=SdkParse("{\"action\":\"withdraw\",\"expected_revision\":3}");MdStorePut(Apply,"work",PrivateWork);Deleted=Deleted&&MdStoreWorkAction(&Store,777,false,Apply,&Out,Error);xrtValueRelease(Out);Out=NULL;xrtValueRelease(Apply);SdkPutUInt(Delete,"expected_revision",4);Deleted=Deleted&&MdStoreDeleteVersion(&Store,777,false,Delete,&Out,Error);xrtValueRelease(Out);Out=NULL;xrtValueRelease(Delete);QaStoreFlag(R,"published_version_must_be_withdrawn_before_delete",Deleted);
 Page=MdStoreRequests(&Store,777,false,NULL,Error);bool Requests=Page&&MdStoreInt(Page,"total",0)==2;xrtValueRelease(Page);Page=MdStoreRequests(&Store,888,false,NULL,Error);Requests=Requests&&Page&&MdStoreInt(Page,"total",-1)==0;xrtValueRelease(Page);QaStoreFlag(R,"creator_requests_are_owner_scoped",Requests);
 Filter=SdkParse("{\"kind\":\"skills\"}");Page=MdStoreList(&Store,0,false,0,Filter,Error);xrtValueRelease(Filter);const xvalue* Public=xrtValueArrayGet(SdkGet(Page,"items"),0);cstr PublicVersion=MdoExtensionText(Public,"version_id",32);char PublicHash[65];uint64 PublicBytes=0;xfile Shared=PublicVersion?MdStoreDownload(&Store,0,false,PublicVersion,false,PublicHash,&PublicBytes,Error):NULL;xvalue* DeletedRaw=MdStoreVersion(&Store,PrivateVersion);cstr DeletedSha=MdoExtensionText(DeletedRaw,"sha256",64);QaStoreFlag(R,"deleting_shared_bytes_keeps_other_publications",Shared!=NULL&&PublicBytes>0&&DeletedSha&&!strcmp(DeletedSha,PublicHash));xrtValueRelease(DeletedRaw);xrtClose(Shared);xrtValueRelease(Page);

 char LinuxWork[33]={0};bool Linux=true;
 for(size_t I=0;I<2;I++){char Upload[33];Path=xrtFormat("%s/linux-%s.zip",SdkField(P,"root"),I?"musl":"generic");Linux=Linux&&QaStoreFinish(&Store,9001,"skills",Path,Upload,Error);xrtFree(Path);xvalue* Submit=QaStoreSubmitBody(Upload,true);if(I){MdStorePut(Submit,"work",LinuxWork);SdkPutUInt(Submit,"expected_revision",2);}Linux=Linux&&MdStoreSubmit(&Store,9001,false,Submit,&Out,Error);char Version[33]={0};if(Linux){snprintf(LinuxWork,sizeof(LinuxWork),"%s",MdoExtensionText(Out,"xid",32));snprintf(Version,sizeof(Version),"%s",MdoExtensionText(Out,"version_id",32));}xrtValueRelease(Out);Out=NULL;xrtValueRelease(Submit);xvalue* Review=SdkParse("{\"action\":\"approve\",\"expected_revision\":1,\"reason\":\"Linux platform fixture\"}");MdStorePut(Review,"version",Version);Linux=Linux&&MdStoreReview(&Store,9,true,Review,&Out,Error);xrtValueRelease(Out);Out=NULL;xrtValueRelease(Review);}
 for(size_t I=0;I<2;I++){Filter=SdkParse("{\"limit\":10}");MdStorePut(Filter,"work",LinuxWork);MdStorePut(Filter,"platform",I?"linux-x86_64-musl":"linux-x86_64-glibc");Page=MdStoreList(&Store,0,false,0,Filter,Error);const xvalue* Row=xrtValueArrayGet(SdkGet(Page,"items"),0);cstr Version=MdoExtensionText(Row,"version",128);Linux=Linux&&Page&&MdStoreInt(Page,"total",0)==1&&Version&&!strcmp(Version,I?"musl":"generic");xrtValueRelease(Page);xrtValueRelease(Filter);}QaStoreFlag(R,"linux_generic_fallback_and_exact_libc_priority",Linux);
 SdkPut(R,"error",Error);MdStoreUnit(&Store);return R;
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
    for variant in ('generic','musl'):
        with zipfile.ZipFile(fixture/f'linux-{variant}.zip','w',zipfile.ZIP_STORED) as z:
            z.writestr('extension.json',json.dumps(dict(kind='skills',name='Linux package',alias='Linux-Alias',entry='SKILL.md',platforms=['linux-x86_64' if variant=='generic' else 'linux-x86_64-musl'],version=variant)))
            z.writestr('SKILL.md','---\nname: Linux\ndescription: Fixture\n---\nReference')
    core=(PLATFORM/'directory_store.inc.c').read_text(encoding='utf-8').replace('#include "extension_manifest.h"','#include "/app/include/mdo/extension_manifest.h"').replace('#include <sqlite3.h>','#include "/app/qa/sqlite3.h"')
    core=core.replace('#include "directory_lifecycle.inc.c"',(PLATFORM/'directory_lifecycle.inc.c').read_text(encoding='utf-8'))
    core=core.replace('#include "directory_versions.inc.c"',(PLATFORM/'directory_versions.inc.c').read_text(encoding='utf-8'))
    try:
        names=('initialize','bind_int','bind_text','prepare_v2','finalize','step','exec','column_count','column_name','column_type','column_int64','column_text','column_bytes','bind_int64','open','busy_timeout','close','changes')
        setup='QaSqlite=LoadLibraryW(L"C:/Program Files/Python312/DLLs/sqlite3.dll");if(!QaSqlite){snprintf(QaLog,sizeof(QaLog),"SQLite loader failed %lu",GetLastError());xsDestroyTCC(T);T=NULL;}'
        setup+=''.join(f'if(T){{void* Address=GetProcAddress(QaSqlite,"sqlite3_{name}");if(Address)tcc_add_symbol(T,"sqlite3_{name}",Address);else{{snprintf(QaLog,sizeof(QaLog),"Missing SQLite symbol {name}");xsDestroyTCC(T);T=NULL;}}}}' for name in names)
        result=product(a,fixture,core+'\n'+CONTRACT,out/'result.json',compiler_setup=setup,compiler_prelude='#include <windows.h>\nstatic HMODULE QaSqlite;',compiler_cleanup='if(QaSqlite)FreeLibrary(QaSqlite);')
        assert result.get('types')==9 and result.get('published_types')==9 and all(value is True for key,value in result.items() if key not in ('types','published_types','error')),result
        for path in (fixture/'store/objects').iterdir():
            with zipfile.ZipFile(path) as z:assert z.testzip() is None
        print(json.dumps(result,ensure_ascii=False))
    finally:
        if fixture.resolve().parent==out.resolve() and fixture.name.startswith('home-'):shutil.rmtree(fixture)

if __name__=='__main__':main()
