from locked_sdk import sdk_root
"""Actual platform routes and SDK boundaries, without creating another service."""
import argparse
import json
from pathlib import Path
import shutil
import uuid
import zipfile
from native_product_runner import product
from extension_zip_fixture import platform_store_source

ROOT=Path(__file__).resolve().parents[1]
PLATFORM=Path('D:/GIT/home/host/xywhsoft_ai')
ECO=PLATFORM/'plugin/mdo/modules/ecosystem'
PRELUDE=r'''
typedef void* XS_ServerObject;typedef void* XS_HostObject;
typedef struct QaSiteRequest {void* raw;bool deferred,owned_work,replied;int Method,Status;const char* Path;const void* Body;size_t BodyBytes;const char* Type;const char* Csrf;xvalue* Query;xvalue* Json;xbuffer Bytes;} QaSiteRequest;
typedef QaSiteRequest* XS_RequestObject;typedef QaSiteRequest* XS_ResponseObject;
#define XS_PLUGIN_HOST_SIDE
#include "/app/qa/plugin_sdk/xs_plugin_work.h"
static int xsReqMethodID(XS_RequestObject R){return R->Method;}
static int xsReqQueryValue(XS_RequestObject R,cstr Name,char* Out,size_t Capacity){cstr V=SdkField(R->Query,Name);if(!V)return -1;size_t N=strlen(V);if(Capacity){size_t Copy=N<Capacity-1?N:Capacity-1;memcpy(Out,V,Copy);Out[Copy]=0;}return (int)N;}
static const char* XAdmin_ReqBody(XS_RequestObject R){return R->Body;}
static size_t XAdmin_ReqBodyLen(XS_RequestObject R){return R->BodyBytes;}
static cstr XAdmin_ReqPath(XS_RequestObject R){return R->Path;}
static int XAdmin_ReqHeaderCopy(XS_RequestObject R,cstr Name,char* Out,size_t Capacity){cstr V=!strcmp(Name,"Content-Type")?R->Type:!strcmp(Name,"X-CSRF-Token")?R->Csrf:NULL;if(!V)return -1;size_t N=strlen(V);if(Capacity){snprintf(Out,Capacity,"%s",V);}return (int)N;}
static void xsHttpReplyAuto(XS_ResponseObject R,int Status,cstr Headers,const void* Bytes,size_t N){(void)Headers;R->Status=Status;R->replied=true;xrtValueRelease(R->Json);R->Json=xrtJsonParse(xrtStrViewN(Bytes,N));}
static int XAdmin_MemberContactStatus(xvalue* S){cstr Source=SdkField(S,"source");return Source&&(!strcmp(Source,"cookie")||!strcmp(Source,"bearer"))?0:-1;}
static cstr XAdmin_AdminCSRFToken(xvalue* S){(void)S;return "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";}
static bool XAdmin_CheckAdminCSRF(XS_RequestObject R,xvalue* S){return S&&R->Csrf&&!strcmp(R->Csrf,XAdmin_AdminCSRFToken(S));}
static int XAdmin_DeferRoute(XAdminPluginHandle H,XS_RequestObject R,xvalue* S,XAdminAsyncRouteProc F){(void)H;R->deferred=true;F(NULL,NULL,R,R,S);return 0;}
static int XAdmin_StreamBegin(XS_RequestObject R,uint16 Status,const xhttpfield* F,size_t Count,unsigned Timeout){(void)Timeout;for(size_t I=0;I<Count;I++)if(xrtStrCaseEqual(F[I].Name,XRT_STR_LITERAL("Content-Length"))||xrtStrCaseEqual(F[I].Name,XRT_STR_LITERAL("Transfer-Encoding"))||xrtStrCaseEqual(F[I].Name,XRT_STR_LITERAL("Connection")))return -1;R->Status=Status;R->replied=true;return 0;}
static int XAdmin_StreamWrite(XS_RequestObject R,const void* B,size_t N){return xrtBufferAppend(&R->Bytes,(xbytesview){B,N})?0:-1;}
static int XAdmin_StreamFinish(XS_RequestObject R,bool Ok){(void)R;return Ok?0:-1;}
typedef struct PluginInstance {bool started,stopping;size_t activeIo;} PluginInstance;
static PluginInstance G_Plugins[1];static size_t G_PluginCount=1;static xmutex* G_RequestLock;
'''
CONTRACT=r'''
static void QaFlag(xvalue* R,cstr K,bool V){xrtValueObjectSetNew(R,xrtStrView(K),xrtValueBool(V));}
static void QaSiteReset(QaSiteRequest* R,cstr Path,int Method,const xvalue* Query,cstr Body){xrtValueRelease(R->Json);xrtValueRelease(R->Query);xrtBufferUnit(&R->Bytes);memset(R,0,sizeof(*R));R->Path=Path;R->Method=Method;R->raw=R;R->Type="application/json";R->Body=Body;R->BodyBytes=Body?strlen(Body):0;R->Query=xrtValueClone(Query);xrtBufferInit(&R->Bytes);}
static void QaSiteCall(QaSiteRequest* R,xvalue* Session){xrtMutexLock(G_RequestLock);MdDirectoryRoute(NULL,NULL,R,R,Session);xrtMutexUnlock(G_RequestLock);}
xvalue* QaProductContract(const xvalue* P,xvfs V){
 (void)V;xvalue* R=xrtValueObject();char Error[512]={0};char* Directory=xrtFormat("%s/store",SdkField(P,"root"));bool Ok=MdStoreInit(&MdDirectory,Directory,Error);xrtFree(Directory);
 G_RequestLock=xrtMutexCreate();G_Plugins[0].started=true;G_Plugins[0].activeIo=1;MdDirectoryHandle=&G_Plugins[0];XAdminHostContext Host={0};MdDirectoryHost=&Host;sqlite3_open(":memory:",&Host.main_db);sqlite3_exec(Host.main_db,"CREATE TABLE member(id INTEGER PRIMARY KEY,username TEXT,isDelete INTEGER);INSERT INTO member VALUES(7,'creator-seven',0)",NULL,NULL,NULL);
 xvalue* Member=SdkParse("{\"id\":7,\"source\":\"bearer\"}"),*Other=SdkParse("{\"id\":8,\"source\":\"bearer\"}"),*Cookie=SdkParse("{\"id\":7,\"source\":\"cookie\"}"),*Admin=SdkParse("{\"id\":9,\"source\":\"admin\"}");char CookieHash[65];MdStoreTextHash("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",CookieHash);SdkPut(Cookie,"csrf_hash",CookieHash);
 QaSiteRequest Request={0};xrtBufferInit(&Request.Bytes);QaSiteReset(&Request,"/api/v1/mdo/extensions/uploads",XHTTP_METHOD_POST,NULL,"{\"action\":\"begin\"}");QaSiteCall(&Request,Cookie);QaFlag(R,"cookie_csrf_rejected_before_worker",Request.Status==403&&!Request.deferred);
 char* Archive=xrtFormat("%s/command.zip",SdkField(P,"root"));xfile F=xrtOpen(Archive,XFILE_READ);uint64 Size=0;char Hash[65];Ok=Ok&&F&&MdStoreDigest(F,NULL,Hash,&Size);uint8* Bytes=xrtMalloc((size_t)Size);Ok=Ok&&Bytes&&xrtReadAtFull(F,0,Bytes,(size_t)Size,NULL);xrtClose(F);xrtFree(Archive);
 xvalue* Begin=SdkParse("{\"action\":\"begin\",\"purpose\":\"archive\",\"kind\":\"commands\"}");SdkPut(Begin,"sha256",Hash);SdkPutUInt(Begin,"bytes",Size);char* Body=SdkJson(Begin);QaSiteReset(&Request,"/api/v1/mdo/extensions/uploads",XHTTP_METHOD_POST,NULL,Body);QaSiteCall(&Request,Member);Ok=Ok&&Request.Status==200;char Upload[33]={0};if(Ok)snprintf(Upload,sizeof(Upload),"%s",SdkField(SdkGet(Request.Json,"data"),"xid"));xrtFree(Body);xrtValueRelease(Begin);
 xvalue* Query=SdkParse("{\"offset\":\"0\"}");SdkPut(Query,"id",Upload);QaSiteReset(&Request,"/api/v1/mdo/extensions/uploads",XHTTP_METHOD_PUT,Query,NULL);Request.Body=Bytes;Request.BodyBytes=(size_t)Size;QaSiteCall(&Request,Member);QaFlag(R,"json_chunks_not_accepted",Request.Status==415&&!Request.deferred);
 QaSiteReset(&Request,"/api/v1/mdo/extensions/uploads",XHTTP_METHOD_PUT,Query,NULL);Request.Body=Bytes;Request.BodyBytes=(size_t)Size;Request.Type="application/octet-stream";QaSiteCall(&Request,Other);bool Isolation=Request.Status>=400;
 for(uint64 At=0;At<Size;){char Position[24];snprintf(Position,sizeof(Position),"%llu",(unsigned long long)At);SdkPut(Query,"offset",Position);size_t Chunk=(size_t)(Size-At>1048576?1048576:Size-At);QaSiteReset(&Request,"/api/v1/mdo/extensions/uploads",XHTTP_METHOD_PUT,Query,NULL);Request.Body=Bytes+At;Request.BodyBytes=Chunk;Request.Type="application/octet-stream";QaSiteCall(&Request,Member);Ok=Ok&&Request.Status==200;At+=Chunk;}QaFlag(R,"raw_upload_and_account_isolation",Ok&&Isolation);xrtValueRelease(Query);
 xvalue* Seal=SdkParse("{\"action\":\"seal\"}");SdkPut(Seal,"id",Upload);Body=SdkJson(Seal);QaSiteReset(&Request,"/api/v1/mdo/extensions/uploads",XHTTP_METHOD_POST,NULL,Body);QaSiteCall(&Request,Member);Ok=Ok&&Request.Status==200;xrtFree(Body);xrtValueRelease(Seal);
 xvalue* Submit=SdkParse("{\"action\":\"submit\",\"publish\":true,\"source_included\":true,\"review_notes\":\"Review the C source in the ZIP\"}");char* Nonce=xrtXidMakeString();SdkPut(Submit,"nonce",Nonce);xrtFree(Nonce);SdkPut(Submit,"upload",Upload);Body=SdkJson(Submit);QaSiteReset(&Request,"/api/v1/mdo/extensions/creator",XHTTP_METHOD_POST,NULL,Body);QaSiteCall(&Request,Member);Ok=Ok&&Request.Status==200;char Version[33]={0};if(Ok)snprintf(Version,sizeof(Version),"%s",SdkField(SdkGet(Request.Json,"data"),"version_id"));xrtFree(Body);xrtValueRelease(Submit);QaFlag(R,"zip_submission_through_real_route",Ok);
 Query=xrtValueObject();SdkPut(Query,"version",Version);QaSiteReset(&Request,"/api/v1/mdo/extensions/archive",XHTTP_METHOD_GET,Query,NULL);QaSiteCall(&Request,NULL);bool Privacy=Request.Status==404;
 QaSiteReset(&Request,"/api/v1/mdo/extensions/archive",XHTTP_METHOD_GET,Query,NULL);QaSiteCall(&Request,Other);Privacy=Privacy&&Request.Status==404;QaFlag(R,"pending_zip_not_public_or_other_owner",Privacy);
 QaSiteReset(&Request,"/api/v1/mdo/extensions/archive",XHTTP_METHOD_GET,Query,NULL);QaSiteCall(&Request,Member);QaFlag(R,"owner_download_is_original_zip",Request.Status==200&&Request.Bytes.Size==Size&&!memcmp(Request.Bytes.Data,Bytes,(size_t)Size));
 xvalue* Review=SdkParse("{\"action\":\"approve\",\"expected_revision\":1,\"reason\":\"Source inspected\"}");SdkPut(Review,"version",Version);Body=SdkJson(Review);QaSiteReset(&Request,"/admin/api/mdo/extensions",XHTTP_METHOD_POST,NULL,Body);QaSiteCall(&Request,Admin);bool Csrf=Request.Status==403;
 QaSiteReset(&Request,"/admin/api/mdo/extensions",XHTTP_METHOD_POST,NULL,Body);Request.Csrf=XAdmin_AdminCSRFToken(Admin);QaSiteCall(&Request,Admin);Ok=Ok&&Request.Status==200;QaFlag(R,"admin_review_requires_csrf",Csrf&&Ok);xrtFree(Body);xrtValueRelease(Review);
 SdkPut(Query,"offset","10");QaSiteReset(&Request,"/api/v1/mdo/extensions/archive",XHTTP_METHOD_GET,Query,NULL);QaSiteCall(&Request,NULL);QaFlag(R,"public_binary_range_after_approval",Request.Status==200&&Request.Bytes.Size==(Size-10>1048576?1048576:Size-10)&&!memcmp(Request.Bytes.Data,Bytes+10,Request.Bytes.Size));SdkPut(Query,"offset","1048576");QaSiteReset(&Request,"/api/v1/mdo/extensions/archive",XHTTP_METHOD_GET,Query,NULL);QaSiteCall(&Request,NULL);QaFlag(R,"range_offsets_exceed_catalogue_limits",Size>1048576&&Request.Status==200&&Request.Bytes.Size==Size-1048576&&!memcmp(Request.Bytes.Data,Bytes+1048576,(size_t)Size-1048576));xrtValueObjectRemove(Query,XRT_STR_LITERAL("offset"));
 SdkPut(Query,"path","command.c");QaSiteReset(&Request,"/api/v1/mdo/extensions/preview",XHTTP_METHOD_GET,Query,NULL);QaSiteCall(&Request,NULL);QaFlag(R,"review_preview_reads_individual_text_file",Request.Status==200&&SdkField(SdkGet(Request.Json,"data"),"text")&&strstr(SdkField(SdkGet(Request.Json,"data"),"text"),"qa-directory-command"));xrtValueRelease(Query);
 QaSiteReset(&Request,"/api/v1/mdo/extensions/catalog",XHTTP_METHOD_GET,NULL,NULL);QaSiteCall(&Request,NULL);const xvalue* Row=xrtValueArrayGet(SdkGet(SdkGet(Request.Json,"data"),"items"),0);QaFlag(R,"catalogue_keeps_username_and_copyright",Request.Status==200&&Row&&!strcmp(SdkField(Row,"submitter"),"creator-seven")&&!strcmp(SdkField(Row,"copyright"),"Fixture author"));
 QaSiteReset(&Request,"/",XHTTP_METHOD_GET,NULL,NULL);xrtBufferUnit(&Request.Bytes);xrtFree(Bytes);xrtValueRelease(Member);xrtValueRelease(Other);xrtValueRelease(Cookie);xrtValueRelease(Admin);MdStoreUnit(&MdDirectory);sqlite3_close(Host.main_db);MdDirectoryHost=NULL;MdDirectoryHandle=NULL;xrtMutexDestroy(G_RequestLock);SdkPut(R,"error",Error);return R;
}
'''

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--url',required=True);p.add_argument('--home',type=Path,required=True);p.add_argument('--project',default='default');a=p.parse_args()
    out=ROOT/'.build/platform-directory-routes-native';out.mkdir(parents=True,exist_ok=True);fixture=out/('home-'+uuid.uuid4().hex[:12]);fixture.mkdir()
    sdk=fixture/'plugin_sdk';sdk.mkdir();(fixture/'include/xadmin').mkdir(parents=True)
    for name in ('xs_plugin.h','xs_plugin_work.h','plugin_support.h','value_util.h'):shutil.copyfile(PLATFORM/'plugin_sdk'/name,sdk/name)
    shutil.copyfile(PLATFORM/'include/xadmin/time.h',fixture/'include/xadmin/time.h')
    shutil.copyfile(sdk_root()/'lib/sqlite3.h',sdk/'sqlite3.h')
    for name in ('extension_manifest.h','extension_identity.h','extension_path.h','extension_casefold.h','extension_zip.h'):shutil.copyfile(ECO/name,fixture/name)
    with zipfile.ZipFile(fixture/'command.zip','w',zipfile.ZIP_DEFLATED) as z:
        z.writestr('extension.json',json.dumps(dict(kind='commands',name='Route command',alias='qa-directory-command',entry='command.c',platforms=['any'],copyright='Fixture author',license='MIT')))
        z.writestr('command.c','/* mdo-command {"id":"qa-directory-command","description":"Inert source preview"} */\n')
        z.writestr('.hidden/deep/dependency.txt','dependency\n')
        import os
        z.writestr('resources/random.bin',os.urandom(1048576+257))
    core=platform_store_source(ECO,None)
    routes=(ECO/'directory_routes.inc.c').read_text(encoding='utf-8').replace('#include "directory_store.inc.c"',core)
    try:
        names=('initialize','bind_int','bind_text','prepare_v2','finalize','step','exec','column_count','column_name','column_type','column_int64','column_int','column_text','column_bytes','bind_int64','open','busy_timeout','close','changes')
        setup='QaSqlite=LoadLibraryW(L"C:/Program Files/Python312/DLLs/sqlite3.dll");tcc_add_include_path(T,"/app/qa/plugin_sdk");tcc_add_include_path(T,"/app/qa");'
        setup+=''.join(f'tcc_add_symbol(T,"sqlite3_{name}",GetProcAddress(QaSqlite,"sqlite3_{name}"));' for name in names)
        result=product(a,fixture,PRELUDE+(PLATFORM/'src/net/plugin_owned_work.c').read_text(encoding='utf-8')+routes+CONTRACT,out/'result.json',compiler_setup=setup,compiler_prelude='#include <windows.h>\nstatic HMODULE QaSqlite;',compiler_cleanup='if(QaSqlite)FreeLibrary(QaSqlite);')
        assert len(result)>=10 and all(value is True for key,value in result.items() if key!='error'),result
        print(json.dumps(result,ensure_ascii=False))
    finally:
        if fixture.resolve().parent==out.resolve() and fixture.name.startswith('home-'):shutil.rmtree(fixture)

if __name__=='__main__':main()
