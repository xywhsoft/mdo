"""Actual client ZIP workers against actual platform SQLite core; HTTP is stubbed."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import uuid
from native_product_runner import product
from extension_zip_fixture import platform_store_source

ROOT = Path(__file__).resolve().parents[1]
PLATFORM = ROOT.parent / 'home/host/xywhsoft_ai/plugin/mdo/modules/ecosystem'

TRANSPORT = r'''
static MdDirectoryStore QaStore;
static bool QaLogin=true,QaLoseSubmit=true,QaRedirect,QaCorrupt,QaCancel,QaRaw=true;
static unsigned QaChunks,QaRequests,QaLeaks;
static bool QaLease(xcancel* Parent,uint32 Timeout,MdoAccountLease* L){
 (void)Timeout;memset(L,0,sizeof(*L));if(!QaLogin)return false;L->Cancel=Parent?xrtCancelChild(Parent):xrtCancelCreate();L->AccessToken=xrtStrDup("native-fixture-credential");L->Managed=true;L->MemberId=12;L->Generation=7;return L->Cancel&&L->AccessToken;
}
static void QaRelease(MdoAccountLease* L){if(L->AccessToken){xrtSecureZero(L->AccessToken,strlen(L->AccessToken));xrtFree(L->AccessToken);}xrtCancelDestroy(L->Cancel);memset(L,0,sizeof(*L));}
static bool QaReject(const MdoAccountLease* L){(void)L;return true;}
static void QaResponseUnit(XS_FetchResponse* R){xrtFree(R->Body);xrtFree(R->ContentType);memset(R,0,sizeof(*R));}
static char* QaQuery(cstr Url,cstr Key){char* Tag=xrtFormat("%s=",Key);cstr At=Tag?strstr(Url,Tag):NULL;xrtFree(Tag);if(!At)return NULL;At+=strlen(Key)+1;size_t N=strcspn(At,"&");return xrtStrDupN(At,N);}
static bool QaFetch(const XS_FetchRequest* Q,XS_FetchResponse* R){
 QaRequests++;if(!Q->Url||strncmp(Q->Url,"https://ai.xywhsoft.com/api/v1/mdo/extensions/",sizeof("https://ai.xywhsoft.com/api/v1/mdo/extensions/")-1))return false;
 if(QaCancel){xrtCancelRequest(Q->Cancel);return false;}if(Q->Cancel&&xrtCancelRequested(Q->Cancel))return false;
 bool Auth=false;for(size_t I=0;I<Q->HeaderCount;I++)if(!strcmp(Q->Headers[I].Name,"Authorization"))Auth=!strcmp(Q->Headers[I].Value,"Bearer native-fixture-credential");
 cstr Path=strstr(Q->Url,"/api/v1/mdo/extensions/");xvalue* Input=Q->BodySize&&strcmp(Q->Method,"PUT")?xrtJsonParse(xrtStrViewN(Q->Body,Q->BodySize)):NULL,*Out=NULL;char E[512]={0};bool Ok=false;
 if(Input){char* S=xrtJsonStringify(Input,false,NULL);if(S&&strstr(S,"native-fixture-credential"))QaLeaks++;xrtFree(S);}
 if(strstr(Path,"/uploads")){
  char* Id=QaQuery(Path,"id");cstr Action=MdoExtensionText(Input,"action",24);
  if(!strcmp(Q->Method,"GET")){Out=Id?MdStoreUpload(&QaStore,Id,12):NULL;char* File=Id?xrtFormat("uploads/%s.part",Id):NULL;bool Exists=false;xfileinfo Info;if(Out&&File&&MdStoreStat(&QaStore,File,&Exists,&Info)){xrtValueObjectSetNew(Out,XRT_STR_LITERAL("received"),xrtValueUInt(Exists?Info.Size:0));Ok=true;}xrtFree(File);}
  else if(!Auth)Ok=false;
  else if(!strcmp(Q->Method,"PUT")){char* At=QaQuery(Path,"offset");uint64 Offset=0;if(At)MdoArchiveApiOffset(xrtStrView(At),&Offset);QaChunks++;QaRaw=QaRaw&&Q->BodySize>0&&Q->BodySize<=1048576&&(!Offset?((const char*)Q->Body)[0]=='P'&&((const char*)Q->Body)[1]=='K':true);Ok=MdStoreChunk(&QaStore,12,Id,Offset,Q->Body,Q->BodySize,&Out,E);xrtFree(At);}
  else if(Action&&!strcmp(Action,"begin"))Ok=MdStoreBegin(&QaStore,12,false,Input,&Out,E);
  else if(Action&&!strcmp(Action,"seal"))Ok=MdStoreSeal(&QaStore,12,MdoExtensionText(Input,"id",32),Q->Cancel,&Out,E);
  xrtFree(Id);
 }else if(strstr(Path,"/creator")){
  if(Auth&&!strcmp(Q->Method,"GET")){Out=MdStoreProfile(&QaStore,12,E);Ok=Out!=NULL;}
  else if(Auth){Ok=MdStoreSubmit(&QaStore,12,false,Input,&Out,E);if(Ok&&QaLoseSubmit){QaLoseSubmit=false;xrtValueRelease(Input);xrtValueRelease(Out);return false;}}
 }else if(strstr(Path,"/detail")){char* Id=QaQuery(Path,"version");Out=MdStoreDetail(&QaStore,Auth?12:0,false,Id,E);Ok=Out!=NULL;xrtFree(Id);
 }else if(strstr(Path,"/catalog")){Out=MdStoreList(&QaStore,0,false,0,NULL,E);Ok=Out!=NULL;
 }else if(strstr(Path,"/archive")){char* Id=QaQuery(Path,"version"),*At=QaQuery(Path,"offset");char Hash[65];uint64 Bytes=0,Offset=0;if(At)MdoArchiveApiOffset(xrtStrView(At),&Offset);xfile F=MdStoreDownload(&QaStore,Auth?12:0,false,Id,false,Hash,&Bytes,E);size_t N=F&&Bytes>Offset?(size_t)(Bytes-Offset<1048576?Bytes-Offset:1048576):0;R->Body=N?xrtMalloc(N):NULL;Ok=R->Body&&xrtReadAtFull(F,Offset,R->Body,N,NULL);R->BodySize=N;R->ContentType=xrtStrDup("application/zip");R->Status=Ok?200:404;R->Redirects=QaRedirect?1:0;if(QaCorrupt&&N)((uint8*)R->Body)[N-1]^=1;xrtClose(F);xrtFree(Id);xrtFree(At);xrtValueRelease(Input);return true;}
 xrtValueRelease(Input);xvalue* Envelope=xrtValueObject();xrtValueObjectSetNew(Envelope,XRT_STR_LITERAL("code"),xrtValueInt(Ok?0:422));MdStorePut(Envelope,"message",E);if(Out)xrtValueObjectSet(Envelope,XRT_STR_LITERAL("data"),Out);size_t N=0;R->Body=xrtJsonStringify(Envelope,false,&N);R->BodySize=N;R->Status=Ok?200:422;R->ContentType=xrtStrDup("application/json");R->Redirects=QaRedirect?1:0;xrtValueRelease(Envelope);xrtValueRelease(Out);return true;
}
'''

CONTRACT = r'''
static void QaFlag(xvalue* R,cstr K,bool V){xrtValueObjectSetNew(R,xrtStrView(K),xrtValueBool(V));}
static void QaDiscard(cstr Id){xvalue* O=NULL;char E[512];MdoExtensionTaskCancel(Id,&O,E);xrtValueRelease(O);MdoExtensionTaskForget(Id,E);}
xvalue* QaProductContract(const xvalue* P,xvfs V){
 xvalue* R=xrtValueObject(),*Out=NULL;char E[512]={0};bool Home=QaProductHome(SdkField(P,"root"),SdkField(P,"app"),V);char* StorePath=xrtFormat("%s/platform",SdkField(P,"root"));bool Ok=Home&&MdoExtensionsInit()&&MdoExtensionTasksInit()&&MdStoreInit(&QaStore,StorePath,E);xrtFree(StorePath);
 QaLogin=false;xvalue* Input=SdkParse("{\"action\":\"catalog\"}");bool Public=Ok&&QaMdoExtensionServiceRead(Input,NULL,&Out,E);xrtValueRelease(Out);Out=NULL;SdkPut(Input,"action","profile");Public=Public&&!QaMdoExtensionServiceRead(Input,NULL,&Out,E);xrtValueRelease(Out);Out=NULL;xrtValueRelease(Input);QaFlag(R,"anonymous_catalog_and_private_login_required",Public);QaLogin=true;
 Ok=Ok&&MdoExtensionTaskPack("skills","authored",NULL,&Out,E);char Task[33]={0};if(Ok)snprintf(Task,sizeof(Task),"%s",SdkField(Out,"id"));xrtValueRelease(Out);Out=NULL;
 Input=SdkParse("{\"publish\":true,\"review_notes\":\"Source files are included\",\"source_included\":true}");SdkPut(Input,"task",Task);
 bool Lost=Ok&&!QaMdoExtensionServicePublish(Input,NULL,&Out,E);xrtValueRelease(Out);Out=NULL;Out=MdoExtensionTaskRead(Task,E);Lost=Lost&&Out&&!strcmp(SdkField(Out,"state"),"ready")&&MdStoreScalar(&QaStore,"SELECT count(*) FROM version WHERE owner=?1",12)==1;QaFlag(R,"lost_submit_reply_keeps_retryable_task",Lost);QaFlag(R,"review_request_is_retained_without_payload",SdkGet(Out,"publication")&&!strcmp(SdkField(SdkGet(Out,"publication"),"nonce"),Task));xrtValueRelease(Out);Out=NULL;
 SdkPut(Input,"review_notes","Changed after an unknown result");bool SameRequest=!QaMdoExtensionServicePublish(Input,NULL,&Out,E)&&MdStoreScalar(&QaStore,"SELECT count(*) FROM version WHERE owner=?1",12)==1;QaFlag(R,"changed_retry_cannot_duplicate_a_publication",SameRequest);xrtValueRelease(Out);Out=NULL;SdkPut(Input,"review_notes","Source files are included");
 bool Retry=QaMdoExtensionServicePublish(Input,NULL,&Out,E);char Version[33]={0},Work[33]={0};if(Retry){snprintf(Version,sizeof(Version),"%s",SdkField(Out,"version_id"));snprintf(Work,sizeof(Work),"%s",SdkField(Out,"xid"));}xrtValueRelease(Out);Out=MdoExtensionTaskRead(Task,E);Retry=Retry&&Out&&!strcmp(SdkField(Out,"state"),"complete")&&MdStoreScalar(&QaStore,"SELECT count(*) FROM version WHERE owner=?1",12)==1;QaFlag(R,"publication_retry_has_one_work_and_version",Retry);QaFlag(R,"ordinary_zip_raw_multi_chunk_upload",QaRaw&&QaChunks>=2);xrtValueRelease(Out);Out=NULL;xrtValueRelease(Input);
 xvalue* Review=SdkParse("{\"action\":\"approve\",\"expected_revision\":1,\"reason\":\"Fixture source checked\"}");SdkPut(Review,"version",Version);Ok=Retry&&MdStoreReview(&QaStore,9,true,Review,&Out,E);xrtValueRelease(Out);Out=NULL;xrtValueRelease(Review);
 QaLogin=false;Ok=Ok&&QaMdoExtensionServiceDownload("skills",Version,NULL,&Out,E);char Download[33]={0};if(Ok)snprintf(Download,sizeof(Download),"%s",SdkField(Out,"id"));QaFlag(R,"published_zip_downloads_without_login",Ok);QaFlag(R,"verified_origin_records_platform_identity",Ok&&SdkGet(Out,"store")&&!strcmp(SdkField(SdkGet(Out,"store"),"xid"),Work));xrtValueRelease(Out);Out=NULL;
 Input=SdkParse("{\"enabled\":false,\"trust_code\":true}");Ok=Ok&&QaMdoExtensionServiceInstall(Download,Input,NULL,&Out,E);QaFlag(R,"store_installs_to_work_xid",Ok&&SdkField(Out,"id")&&!strcmp(SdkField(Out,"id"),Work));xrtValueRelease(Out);Out=NULL;xrtValueRelease(Input);
 xvalue* Installed=MdoExtensionsRead("skills",Work,E);QaFlag(R,"installed_version_identity_is_preserved",Installed&&SdkField(SdkGet(Installed,"installation"),"version_id")&&!strcmp(SdkField(SdkGet(Installed,"installation"),"version_id"),Version));xvalue* Edit=SdkParse("{\"action\":\"file.write\",\"kind\":\"skills\",\"path\":\"SKILL.md\",\"text\":\"altered\"}");SdkPut(Edit,"id",Work);SdkPut(Edit,"expected_revision",SdkField(Installed,"revision"));bool Immutable=Installed&&!strcmp(SdkField(Installed,"source"),"store")&&!MdoExtensionsManage(Edit,NULL,NULL,&Out,E)&&strstr(E,"locally authored");QaFlag(R,"store_install_is_immutable",Immutable);xrtValueRelease(Out);Out=NULL;xrtValueRelease(Edit);xrtValueRelease(Installed);
 bool Updated=QaMdoExtensionServiceDownload("skills",Version,NULL,&Out,E);char UpdateTask[33]={0},UpdateHash[65]={0};if(Updated)snprintf(UpdateTask,sizeof(UpdateTask),"%s",SdkField(Out,"id"));xrtValueRelease(Out);Out=NULL;Input=SdkParse("{\"trust_code\":true}");Updated=Updated&&MdoExtensionsFingerprint("skills",Work,UpdateHash,NULL,E);SdkPut(Input,"expected_revision",UpdateHash);Updated=Updated&&QaMdoExtensionServiceInstall(UpdateTask,Input,NULL,&Out,E)&&!MdoExtOpBool(Out,"enabled",true);QaFlag(R,"store_replacement_uses_revision_and_retains_disabled_state",Updated);xrtValueRelease(Out);Out=NULL;xrtValueRelease(Input);
 char* Archive=NULL;QaRedirect=true;bool Bad=!QaMdoExtensionServiceDownload("skills",Version,NULL,&Out,E);xrtValueRelease(Out);Out=NULL;QaRedirect=false;QaFlag(R,"redirects_rejected",Bad);
 QaCorrupt=true;Bad=!QaMdoExtensionServiceDownload("skills",Version,NULL,&Out,E);xrtValueRelease(Out);Out=NULL;QaCorrupt=false;QaFlag(R,"corrupted_zip_never_installs",Bad);
 QaCancel=true;Bad=!QaMdoExtensionServiceDownload("skills",Version,NULL,&Out,E);xrtValueRelease(Out);Out=NULL;QaCancel=false;QaFlag(R,"transfer_cancellation_propagates",Bad);
 QaFlag(R,"wrong_type_rejected",!QaMdoExtensionServiceDownload("mcp",Version,NULL,&Out,E));xrtValueRelease(Out);Out=NULL;
 bool Collision=MdoExtensionsRemove("skills",Work,&Out,E);xrtValueRelease(Out);Out=NULL;Input=SdkParse("{\"kind\":\"skills\",\"name\":\"Local owner\",\"alias\":\"local-owner\",\"entry\":\"SKILL.md\",\"platforms\":[\"any\"]}");Collision=Collision&&MdoExtensionsCreate("skills",Work,Input,"local resource",14,false,&Out,E);xrtValueRelease(Input);xrtValueRelease(Out);Out=NULL;Collision=Collision&&QaMdoExtensionServiceDownload("skills",Version,NULL,&Out,E);char CollisionTask[33]={0};if(Collision)snprintf(CollisionTask,sizeof(CollisionTask),"%s",SdkField(Out,"id"));xrtValueRelease(Out);Out=NULL;Input=SdkParse("{\"trust_code\":true}");Collision=Collision&&MdoExtensionsFingerprint("skills",Work,UpdateHash,NULL,E);SdkPut(Input,"expected_revision",UpdateHash);Collision=Collision&&!QaMdoExtensionServiceInstall(CollisionTask,Input,NULL,&Out,E);xrtValueRelease(Input);xrtValueRelease(Out);Out=NULL;Installed=MdoExtensionsRead("skills",Work,E);QaFlag(R,"store_does_not_overwrite_colliding_local_directory",Collision&&Installed&&!strcmp(SdkField(Installed,"source"),"manual"));xrtValueRelease(Installed);
 xvalue* Items=MdoExtensionTasksList(E);char* Receipt=xrtJsonStringify(Items,false,NULL);QaFlag(R,"credentials_never_enter_tasks",Receipt&&!strstr(Receipt,"native-fixture-credential")&&!QaLeaks);xrtFree(Receipt);
 for(size_t I=0;I<xrtValueCount(Items);I++)QaDiscard(SdkField(xrtValueArrayGet(Items,I),"id"));xrtValueRelease(Items);
 MdoExtensionsRemove("skills",Work,&Out,E);xrtValueRelease(Out);MdStoreUnit(&QaStore);MdoExtensionTasksUnit();MdoExtensionsUnit();if(Home)MdoHomeUnit();SdkPut(R,"error",E);return R;
}
'''


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--url', required=True); p.add_argument('--home', type=Path, required=True); p.add_argument('--project', default='default'); a = p.parse_args()
    out = ROOT / '.build/extension-service-native'; out.mkdir(parents=True, exist_ok=True)
    fixture = out / ('home-' + uuid.uuid4().hex[:12]); fixture.mkdir()
    local = fixture / 'skills/authored'; local.mkdir(parents=True)
    (local / 'extension.json').write_text(json.dumps(dict(kind='skills', name='Client archive', alias='client-archive', entry='SKILL.md', platforms=['any'])), encoding='utf-8')
    (local / 'SKILL.md').write_text('---\nname: client-archive\ndescription: Native transport fixture\n---\nTest skill\n', encoding='utf-8')
    (local / '.hidden.bin').write_bytes(os.urandom(1536000))
    shutil.copyfile(ROOT / '.build/sdk-overlays/16f28b0bd9ecf791b505bbff9001a0c18bbaf398e29d906dc01df967818bcb84/lib/sqlite3.h', fixture / 'sqlite3.h')
    core = platform_store_source(PLATFORM)
    client = (ROOT / 'app/src/extensions/service.c').read_text(encoding='utf-8')
    client = re.sub(r'^#include .*$', '', client, flags=re.M)
    client = re.sub(r'\b(MdoExtService\w+|MdoExtensionService\w+)\b', r'Qa\1', client)
    for old, new in dict(MdoAccountAcquireServiceWait='QaLease', MdoAccountRelease='QaRelease', MdoAccountRejectAccess='QaReject', xsFetch='QaFetch', xsFetchResponseUnit='QaResponseUnit').items():
        client = re.sub(r'\b' + old + r'\b', new, client)
    symbols = ('initialize', 'bind_int', 'bind_text', 'prepare_v2', 'finalize', 'step', 'exec', 'column_count', 'column_name', 'column_type', 'column_int64', 'column_text', 'column_bytes', 'bind_int64', 'open', 'busy_timeout', 'close', 'changes')
    setup = 'QaSqlite=LoadLibraryW(L"C:/Program Files/Python312/DLLs/sqlite3.dll");if(!QaSqlite){snprintf(QaLog,sizeof(QaLog),"SQLite loader failed");xsDestroyTCC(T);T=NULL;}'
    setup += ''.join(f'if(T)tcc_add_symbol(T,"sqlite3_{n}",GetProcAddress(QaSqlite,"sqlite3_{n}"));' for n in symbols)
    try:
        result = product(a, fixture, core + TRANSPORT + client + CONTRACT, out / 'result.json', compiler_setup=setup, compiler_prelude='#include <windows.h>\nstatic HMODULE QaSqlite;', compiler_cleanup='if(QaSqlite)FreeLibrary(QaSqlite);')
        assert len(result) >= 12 and all(v is True for k, v in result.items() if k != 'error'), result
        print(json.dumps(result, ensure_ascii=False))
    finally:
        if fixture.resolve().parent == out.resolve() and fixture.name.startswith('home-'):
            shutil.rmtree(fixture)


if __name__ == '__main__': main()
