#include "../../include/mdo/time.h"
#include "internal.h"
#include "write_admission.h"
#include "../account/internal.h"
#include "../../include/mdo/ecosystem_package.h"
#include "../../include/mdo/distribution.h"
#include "../../include/mdo/extension_files.h"
bool MdoApiEcosystemLocal(MdoApiContext*,const xvalue*);
void MdoApiEcosystemRefreshCredits(const xvalue*);

/* Preview never enters a runtime manager. Both download and local import use
 * the same platform and explicit C trust checks before preparing any writes. */
bool MdoEcosystemPackageCompatible(const xvalue* package,const xvalue* input,char* error,size_t capacity)
{
    if(!MdoPackageValidate(package,error,capacity))return false;
    const xvalue* manifest=xrtValueObjectGet(package,XRT_STR_LITERAL("manifest"));
    int64 MinBuild=0;if(MdoPackageV2(package)&&(!MdoPackageNumber(manifest,"min_build_id",&MinBuild)||(uint64)MinBuild>MdoBuildId())){snprintf(error,capacity,"Update mdo before installing this package");return false;}
    const xvalue* platforms=xrtValueObjectGet(manifest,XRT_STR_LITERAL("platforms"));bool compatible=false,code=false,trust=false;
    for(size_t i=0;i<xrtValueCount(platforms);i++){xstrview p;if(xrtValueGetString(xrtValueArrayGet(platforms,i),&p)&&xrtStrEqual(p,xrtStrView(MdoToolPlatform())))compatible=true;}
    const xvalue* resources=xrtValueObjectGet(package,XRT_STR_LITERAL("resources"));
    cstr Single=MdoPackageText(input,"single_kind",16);
    if(xrtValueObjectHas(input,XRT_STR_LITERAL("single_kind"))&&(!Single||!MdoPackageSingleKind(Single)||xrtValueCount(resources)!=1||strcmp(MdoPackageText(xrtValueArrayGet(resources,0),"kind",16),Single))){snprintf(error,capacity,"This store installs exactly one Skill, Agent tool, editor command or MCP template of the selected type");return false;}
    for(size_t i=0;i<xrtValueCount(resources);i++){cstr kind=MdoPackageText(xrtValueArrayGet(resources,i),"kind",16);if(kind&&(!strcmp(kind,"tools")||!strcmp(kind,"commands")||!strncmp(kind,"c-",2)||!strcmp(kind,"modules")||!strcmp(kind,"plugins")||!strcmp(kind,"toolpacks")))code=true;
#if defined(__ANDROID__)
        if(kind&&!strcmp(kind,"toolpacks")){const xvalue* R=xrtValueArrayGet(resources,i);xvalue* Doc=xrtJsonParse(xrtStrView(MdoPackageText(R,"content",131072)));const xvalue* Programs=xrtValueObjectGet(Doc,XRT_STR_LITERAL("programs"));bool Scripts=true;
            for(size_t j=0;j<xrtValueCount(Programs);j++){cstr Interpreter=MdoPackageText(xrtValueArrayGet(Programs,j),"interpreter",64);if(!Interpreter||!Interpreter[0])Scripts=false;}xrtValueRelease(Doc);
            if(!Scripts){snprintf(error,capacity,"Android command-line packages need an APK-installed interpreter; downloaded native executables are unsupported");return false;}}
#endif
    }
    (void)xrtValueGetBool(xrtValueObjectGet(input,XRT_STR_LITERAL("trust_code")),&trust);
    if(!compatible|| (code&&!trust)){snprintf(error,capacity,"Package does not support this device, or C code approval is required");return false;}
    return true;
}

/* First-party proxy: tokens never enter JavaScript, paths are constructed from
 * validated fields, redirects and ambiguous POST retries are forbidden. */
static xvalue* EcoService(cstr path,cstr method,const xvalue* body,MdoAccountLease* lease,xcancel* cancel,uint16* status,char* error,size_t capacity)
{
    char url[1024],auth[4200];XS_FetchHeader headers[3];XS_FetchRequest req={0};XS_FetchResponse resp={0};size_t size=0;
    char* json=body?xrtJsonStringify(body,false,&size):NULL;xvalue* envelope=NULL,*data=NULL;*status=0;if(capacity)error[0]=0;
    if(size>MDO_PACKAGE_LIMIT||(cancel&&xrtCancelRequested(cancel)))goto done;
    snprintf(url,sizeof(url),MDO_ACCOUNT_SERVICE_ORIGIN "%s",path);
    headers[0]=(XS_FetchHeader){"Accept","application/json"};headers[1]=(XS_FetchHeader){"Content-Type","application/json"};
    size_t Count=2;if(lease->Managed&&lease->AccessToken){snprintf(auth,sizeof(auth),"Bearer %s",lease->AccessToken);headers[Count++]=(XS_FetchHeader){"Authorization",auth};}
    req.Size=sizeof(req);req.Version=XS_FETCH_REQUEST_VERSION;req.Url=url;req.Method=method;req.Headers=headers;req.HeaderCount=Count;req.Body=json;req.BodySize=size;
    req.Timeout=20000000;req.IdleTimeout=10000000;req.MaxBodyBytes=XS_FETCH_BODY_LIMIT;req.Cancel=lease->Managed?lease->Cancel:cancel;resp.Size=sizeof(resp);
    if(!xsFetch(&req,&resp))goto done;
    *status=resp.Status;
    if(!resp.Body||!xrtUtf8Valid((xstrview){(cstr)resp.Body,resp.BodySize},NULL))goto done;
    xjsonreadconfig limits;xrtJsonReadConfigInit(&limits);limits.MaxInputBytes=MDO_PACKAGE_LIMIT+65536;limits.MaxDepth=16;limits.MaxValues=24000;limits.MaxStringBytes=MDO_PACKAGE_LIMIT;
    envelope=xrtJsonRead((xstrview){(cstr)resp.Body,resp.BodySize},&limits);int64 code=-1;
    if(!envelope||!MdoPackageNumber(envelope,"code",&code)||code){cstr Message=MdoPackageText(envelope,"message",1000);if(!Message)Message=MdoPackageText(envelope,"msg",1000);if(capacity&&Message)snprintf(error,capacity,"%s",Message);goto done;}
    data=xrtValueObjectTake(envelope,XRT_STR_LITERAL("data"));
done:
    xrtSecureZero(auth,sizeof(auth));xrtFree(json);xrtValueRelease(envelope);xsFetchResponseUnit(&resp);
    if((cancel&&xrtCancelRequested(cancel))||(lease->Managed&&xrtCancelRequested(lease->Cancel))){xrtValueRelease(data);data=NULL;*status=0;}return data;
}
static bool EcoDownloadSource(xvalue* Data,MdoAccountLease* Lease,xcancel* Cancel,uint16* Status,char* Error,size_t Capacity)
{
    if(!xrtValueObjectGet(Data,XRT_STR_LITERAL("package_blob")))return true;
    int64 Size=0;
    cstr Hash=MdoPackageText(Data,"sha256",64);
    if(!Hash||strlen(Hash)!=64||!MdoPackageNumber(Data,"package_bytes",&Size)||Size<1||Size>MDO_PACKAGE_LIMIT)return false;
    char* Text=xrtMalloc((size_t)Size);bool Ok=Text!=NULL;int64 Offset=0;
    while(Ok&&Offset<Size){char Path[240];snprintf(Path,sizeof(Path),"/api/v1/mdo/ecosystem/blob?hash=%s&offset=%lld",Hash,(long long)Offset);xvalue* R=EcoService(Path,"GET",NULL,Lease,Cancel,Status,Error,Capacity);int64 End=0,Total=0;size_t N=0;cstr Enc=MdoPackageText(R,"base64",90000);void* Bytes=Enc?xrtBase64DecodeNew(Enc,strlen(Enc),&N,NULL):NULL;
        Ok=R&&Bytes&&N&&N<=65536&&MdoPackageNumber(R,"offset",&End)&&End==Offset+(int64)N&&End<=Size&&MdoPackageNumber(R,"total",&Total)&&Total==Size;if(Ok){memcpy(Text+Offset,Bytes,N);Offset=End;}xrtFree(Bytes);xrtValueRelease(R);}
    char Actual[65];Ok=Ok&&MdoExtensionHashBytes(Text,(size_t)Size,Actual)&&!strcmp(Actual,Hash);xjsonreadconfig Limits;xrtJsonReadConfigInit(&Limits);Limits.MaxInputBytes=MDO_PACKAGE_LIMIT;Limits.MaxStringBytes=MDO_PACKAGE_LIMIT;Limits.MaxDepth=16;Limits.MaxValues=24000;xvalue* P=Ok?xrtJsonRead(xrtStrViewN(Text,(size_t)Size),&Limits):NULL;xrtFree(Text);char Why[256];Ok=P&&MdoPackageValidate(P,Why,sizeof(Why));if(Ok)xrtValueObjectSet(Data,XRT_STR_LITERAL("package"),P);xrtValueRelease(P);return Ok;
}
static bool EcoUploadSource(xvalue* Input,MdoAccountLease* Lease,xcancel* Cancel,uint16* Status,char* Error,size_t Capacity)
{
    const xvalue* P=xrtValueObjectGet(Input,XRT_STR_LITERAL("package"));size_t Size=0;char* Json=xrtJsonStringify(P,false,&Size);char Hash[65],Upload[33]={0};
    bool Ok=Json&&MdoExtensionHashBytes(Json,Size,Hash);xvalue* B=Ok?xrtValueObject():NULL;
    if(B){MdoApiValueSetString(B,"action","begin");MdoApiValueSetString(B,"purpose","package");MdoApiValueSetString(B,"sha256",Hash);xrtValueObjectSetNew(B,XRT_STR_LITERAL("size"),xrtValueUInt(Size));}
    xvalue* R=Ok?EcoService("/api/v1/mdo/cli/upload","POST",B,Lease,Cancel,Status,Error,Capacity):NULL;xrtValueRelease(B);Ok=R!=NULL;
    if(Ok){cstr Id=MdoPackageText(R,"id",32);if(Id)snprintf(Upload,sizeof(Upload),"%s",Id);Ok=strlen(Upload)==32;}xrtValueRelease(R);
    for(size_t Offset=0;Ok&&Offset<Size;){size_t N=Size-Offset<65536?Size-Offset:65536;char* Enc=xrtBase64EncodeNew(Json+Offset,N,NULL);B=xrtValueObject();MdoApiValueSetString(B,"action","chunk");MdoApiValueSetString(B,"id",Upload);MdoApiValueSetString(B,"data",Enc?Enc:"");xrtFree(Enc);xrtValueObjectSetNew(B,XRT_STR_LITERAL("offset"),xrtValueUInt(Offset));R=EcoService("/api/v1/mdo/cli/upload","POST",B,Lease,Cancel,Status,Error,Capacity);xrtValueRelease(B);int64 Got=0;Ok=R&&MdoPackageNumber(R,"offset",&Got)&&Got==(int64)(Offset+N);xrtValueRelease(R);Offset+=N;}
    xrtFree(Json);
    if(Ok){B=xrtValueObject();MdoApiValueSetString(B,"action","finish");MdoApiValueSetString(B,"id",Upload);R=EcoService("/api/v1/mdo/cli/upload","POST",B,Lease,Cancel,Status,Error,Capacity);xrtValueRelease(B);Ok=R!=NULL;xrtValueRelease(R);}
    if(Ok){xrtValueObjectRemove(Input,XRT_STR_LITERAL("package"));MdoApiValueSetString(Input,"package_upload",Upload);}
    else if(Upload[0]){char Ignored[256];uint16 Code;B=xrtValueObject();MdoApiValueSetString(B,"action","cancel");MdoApiValueSetString(B,"id",Upload);R=EcoService("/api/v1/mdo/cli/upload","POST",B,Lease,Cancel,&Code,Ignored,sizeof(Ignored));xrtValueRelease(B);xrtValueRelease(R);}
    return Ok;
}
static bool EcoHandleInput(MdoApiContext* c,xvalue* input)
{
    cstr action=MdoPackageText(input,"action",16);char path[1000];bool valid=action!=NULL;
    cstr Single=MdoPackageText(input,"single_kind",16);bool Typed=xrtValueObjectHas(input,XRT_STR_LITERAL("single_kind"));
    if(Typed&&(!Single||!MdoPackageSingleKind(Single))){xrtValueRelease(input);return MdoApiReplyError(c,422,"ecosystem_request_invalid","Choose the Skill, Agent tool, editor command or MCP template store",NULL);}
    const xvalue* body=NULL;const char* method="GET";int64 id=0;
    if(valid&&(!strcmp(action,"catalog")||!strcmp(action,"mine"))){cstr query=MdoPackageText(input,"query",100),kind=MdoPackageText(input,"kind",16);int64 before=0;
        const xvalue* v=xrtValueObjectGet(input,XRT_STR_LITERAL("before"));if(v&&!MdoPackageNumber(input,"before",&before))valid=false;if(before<0)valid=false;
        if(Typed)kind=Single;
        if(kind&&kind[0]&&!MdoPackageKind(kind))valid=false;
        size_t n=0;char* encoded=xrtPercentEncodeNew(query?query:"",query?strlen(query):0,xrtStrView(""),&n);
        if(!encoded)valid=false;
        snprintf(path,sizeof(path),"/api/v1/mdo/ecosystem/packages?format=2&build_id=%llu&q=%s&kind=%s%s%s%lld",(unsigned long long)MdoBuildId(),encoded?encoded:"",kind?kind:"",!strcmp(action,"mine")?"&mine=1":"",before?"&before=":"&unused=",(long long)before);xrtFree(encoded);
        if(valid&&kind&&!strcmp(kind,"mcp")){const char* Keys[]={"mcp_transport","mcp_auth","mcp_platform"};for(size_t i=0;i<3;i++){cstr Value=MdoPackageText(input,Keys[i],40);if(Value&&*Value){bool Safe=true;for(cstr P=Value;*P;P++)if(!((*P>='a'&&*P<='z')||(*P>='0'&&*P<='9')||*P=='-'||*P=='_'))Safe=false;size_t N=strlen(path);int W=Safe?snprintf(path+N,sizeof(path)-N,"&%s=%s",Keys[i],Value):-1;if(W<0||(size_t)W>=sizeof(path)-N)valid=false;}}}
    }else if(valid&&(!strcmp(action,"detail")||!strcmp(action,"latest")||!strcmp(action,"install")||!strcmp(action,"plan")||!strcmp(action,"history"))){valid=MdoPackageNumber(input,"id",&id)&&id>0;
        int64 before=0;if(xrtValueObjectGet(input,XRT_STR_LITERAL("before"))&&(!MdoPackageNumber(input,"before",&before)||before<0))valid=false;
        snprintf(path,sizeof(path),"/api/v1/mdo/ecosystem/%s?format=2&build_id=%llu&id=%lld%s&before=%lld",!strcmp(action,"history")?"history":"package",(unsigned long long)MdoBuildId(),(long long)id,!strcmp(action,"latest")?"&latest=1":"",(long long)(before?before:INT64_MAX));
    }else if(valid&&!strcmp(action,"withdraw")){int64 revision=0;valid=MdoPackageNumber(input,"id",&id)&&id>0&&MdoPackageNumber(input,"revision",&revision)&&revision>0;body=input;strcpy(path,"/api/v1/mdo/ecosystem/withdraw");method="POST";
    }else if(valid&&!strcmp(action,"submit")){const xvalue* Package=xrtValueObjectGet(input,XRT_STR_LITERAL("package"));char error[256];valid=MdoPackageValidate(Package,error,sizeof(error));body=input;strcpy(path,"/api/v1/mdo/ecosystem/submit");method="POST";
    }else if(valid&&(!strcmp(action,"uninstall")||!strcmp(action,"export")||!strcmp(action,"c_sources")||!strcmp(action,"import")||!strcmp(action,"plan_import")||!strcmp(action,"drafts")||!strcmp(action,"draft_read")||!strcmp(action,"draft_save")||!strcmp(action,"draft_delete"))){bool result=MdoApiEcosystemLocal(c,input);xrtValueRelease(input);return result;
    }else valid=false;
    if(valid&&Typed&&(!strcmp(action,"catalog")||!strcmp(action,"mine")||!strcmp(action,"detail")||!strcmp(action,"latest")||!strcmp(action,"install")||!strcmp(action,"plan")||!strcmp(action,"history"))){size_t N=strlen(path);int W=snprintf(path+N,sizeof(path)-N,"&single=1&single_kind=%s",Single);if(W<0||(size_t)W>=sizeof(path)-N)valid=false;}
    if(valid&&Typed&&!strcmp(action,"submit")){const xvalue* Rs=xrtValueObjectGet(xrtValueObjectGet(input,XRT_STR_LITERAL("package")),XRT_STR_LITERAL("resources"));valid=xrtValueCount(Rs)==1&&!strcmp(MdoPackageText(xrtValueArrayGet(Rs,0),"kind",16),Single);if(valid){MdoApiValueSetString(input,"kind",Single);MdoApiValueSetBool(input,"single_resource",true);}}
    if(!valid){xrtValueRelease(input);return MdoApiReplyError(c,422,"ecosystem_request_invalid","Invalid store request or portable package",NULL);}
    MdoAccountLease lease={0};uint16 status=0;
    bool Acquired=MdoAccountAcquireService(c->SendCancel,&lease);
    if(!Acquired&&(!strcmp(action,"mine")||strcmp(method,"GET"))){xrtValueRelease(input);return MdoApiReplyError(c,401,"ecosystem_login_refresh","Sign in or wait for login renewal before publishing",NULL);}
    char serviceError[1001]={0};bool Uploaded=strcmp(action,"submit")||EcoUploadSource(input,&lease,c->SendCancel,&status,serviceError,sizeof(serviceError));
    xvalue* data=Uploaded?EcoService(path,method,body,&lease,c->SendCancel,&status,serviceError,sizeof(serviceError)):NULL;if(data&&!EcoDownloadSource(data,&lease,c->SendCancel,&status,serviceError,sizeof(serviceError))){xrtValueRelease(data);data=NULL;if(!serviceError[0])snprintf(serviceError,sizeof(serviceError),"Source download failed hash or format validation");}if(status==401&&Acquired)(void)MdoAccountRejectAccess(&lease);if(Acquired)MdoAccountRelease(&lease);
    if(!data){xrtValueRelease(input);return MdoApiReplyError(c,status>=400&&status<600?status:502,"ecosystem_service_failed",serviceError[0]?serviceError:status==409?"This version already exists; publish a new version":status==401?"Sign in again or wait for login renewal":"The extension service could not complete this request",NULL);}
    MdoApiEcosystemRefreshCredits(data);
    if(!strcmp(action,"install")||!strcmp(action,"plan")){
        bool Planning=!strcmp(action,"plan");xvalue* Consent=Planning?xrtValueClone(input):NULL;if(Consent)MdoApiValueSetBool(Consent,"trust_code",true);
        const xvalue* package=xrtValueObjectGet(data,XRT_STR_LITERAL("package"));char error[256];bool compatible=MdoEcosystemPackageCompatible(package,Consent?Consent:input,error,sizeof(error));xrtValueRelease(Consent);
        cstr state=MdoPackageText(data,"state",16);compatible=compatible&&state&&!strcmp(state,"published");
        size_t n=0;char* json=xrtJsonStringify(package,false,&n);char hash[65];cstr expected=MdoPackageText(data,"sha256",64);compatible=compatible&&json&&expected&&MdoExtensionHashBytes(json,n,hash)&&!strcmp(hash,expected);xrtFree(json);
        bool result;
        if(!compatible)result=MdoApiReplyError(c,409,"ecosystem_incompatible","Package is unpublished, incompatible, unverified, or C code approval is required",NULL);
        else {xvalue* local=xrtValueObject();MdoApiValueSetString(local,"action",Planning?"plan":"install");xrtValueObjectSet(local,XRT_STR_LITERAL("entry"),data);
            const xvalue* Expected=xrtValueObjectGet(input,XRT_STR_LITERAL("expected_plan"));if(Expected)xrtValueObjectSet(local,XRT_STR_LITERAL("expected_plan"),Expected);
            result=MdoApiEcosystemLocal(c,local);xrtValueRelease(local);}
        xrtValueRelease(data);xrtValueRelease(input);return result;
    }
    xrtValueRelease(input);return MdoApiReplySuccessTake(c,status==201?201:200,data,NULL);
}

typedef struct EcoApiJob {
    MdoApiContext Context;
    XS_HttpReq Request;
    xhttp1head Head;
    xvalue* Input;
    bool Sent;
} EcoApiJob;
static struct { xmutex* Lock;xtaskpool* Pool;size_t Count;bool Stopping; } EcoApi;
bool MdoApiEcosystemInit(void)
{if(EcoApi.Lock)return true;EcoApi.Lock=xrtMutexCreate();return EcoApi.Lock!=NULL;}
static void EcoApiDrop(ptr value,ptr user)
{
    (void)user;EcoApiJob* job=value;
    MdoApiWriteLeave(&job->Context);xrtValueRelease(job->Input);
    if(job->Request.tls){if(job->Sent)xrtTlsStreamClose(job->Request.tls);else xrtTlsStreamAbort(job->Request.tls);xrtTlsStreamDestroy(job->Request.tls);}
    if(job->Request.tcp){if(job->Sent)xrtNetStreamClose(job->Request.tcp);else xrtNetStreamAbort(job->Request.tcp);xrtNetStreamDestroy(job->Request.tcp);}
    xrtMutexLock(EcoApi.Lock);--EcoApi.Count;xrtMutexUnlock(EcoApi.Lock);xrtFree(job);
}
static xtaskoutcome EcoApiRun(xcancel* cancel,ptr value,xtaskvalue* out)
{
    (void)out;EcoApiJob* job=value;job->Context.SendCancel=cancel;
    if(!MdoApiDownloadLive(&job->Context))return XTASK_CANCELLED;
    xvalue* input=job->Input;job->Input=NULL;
    job->Sent=EcoHandleInput(&job->Context,input);
    if(job->Sent)MdoCliLegacyChanged();
    MdoApiLiveChanged(NULL);return job->Sent?XTASK_SUCCESS:XTASK_FAILED;
}
void MdoApiEcosystemUnit(void)
{
    if(!EcoApi.Lock)return;
    xrtMutexLock(EcoApi.Lock);EcoApi.Stopping=true;xtaskpool* pool=EcoApi.Pool;xrtMutexUnlock(EcoApi.Lock);
    if(pool){xrtTaskPoolCancel(pool);xrtTaskPoolWait(pool);xrtTaskPoolDestroy(pool);}
    xrtMutexDestroy(EcoApi.Lock);memset(&EcoApi,0,sizeof(EcoApi));
}
bool MdoApiEcosystemRoute(MdoApiContext* c)
{
    if(c->Request->head->MethodCode==XHTTP_METHOD_GET){xvalue* input=xrtValueObject();MdoApiValueSetString(input,"action","installed");bool result=MdoApiEcosystemLocal(c,input);xrtValueRelease(input);return result;}
    char* raw=NULL;size_t length=0;MdoApiBodyStatus read=MdoApiBinaryBodyRead(c,MDO_PACKAGE_LIMIT+16384,&raw,&length);
    if(read!=MDO_API_BODY_OK)return MdoApiReplyBodyError(c,read);
    xjsonreadconfig limits;xrtJsonReadConfigInit(&limits);limits.MaxInputBytes=MDO_PACKAGE_LIMIT+16384;limits.MaxStringBytes=MDO_PACKAGE_LIMIT;limits.MaxDepth=16;limits.MaxValues=24000;
    xvalue* input=xrtJsonRead(xrtStrViewN(raw,length),&limits);xrtFree(raw);
    if(!input)return MdoApiReplyError(c,422,"ecosystem_request_invalid","Expected a bounded JSON object",NULL);
    EcoApiJob* job=xrtCalloc(1,sizeof(*job));xfuture* future=NULL;
    if(!job||!EcoApi.Lock)goto failed;
    job->Head.MethodCode=XHTTP_METHOD_POST;job->Request.head=&job->Head;
    if(c->Request->tls)job->Request.tls=xrtTlsStreamRef(c->Request->tls);else if(c->Request->tcp)job->Request.tcp=xrtNetStreamRef(c->Request->tcp);
    if(!job->Request.tls&&!job->Request.tcp)goto failed;
    job->Context.Request=&job->Request;job->Context.Target.Path=xrtStrView("/api/v1/ecosystem");job->Context.CloseResponse=true;
    job->Context.SendDeadline=MdoDeadlineAfterUs(1200000000);memcpy(job->Context.RequestId,c->RequestId,sizeof(c->RequestId));job->Input=input;
    /* Transfer the admitted write count to the owned job. Home purge/import
     * stays fenced until network work and any resource publication finish. */
    job->Context.WriteShared=c->WriteShared;job->Context.WriteExclusive=c->WriteExclusive;
    xrtMutexLock(EcoApi.Lock);
    if(!EcoApi.Stopping&&EcoApi.Count<3){
        if(!EcoApi.Pool){xtaskpoolconfig config={0};config.Threads=1;config.QueueLimit=2;EcoApi.Pool=xrtTaskPoolCreate(&config);}
        if(EcoApi.Pool){xtaskargs args={0};args.Destroy=EcoApiDrop;++EcoApi.Count;future=xrtTaskSubmit(EcoApi.Pool,EcoApiRun,job,&args);if(!future)--EcoApi.Count;}
    }
    xrtMutexUnlock(EcoApi.Lock);
    if(future){c->WriteShared=c->WriteExclusive=false;c->Takeover=true;xrtFutureDestroy(future);return true;}
failed:
    if(job){xrtTlsStreamDestroy(job->Request.tls);xrtNetStreamDestroy(job->Request.tcp);xrtFree(job);}xrtValueRelease(input);
    return MdoApiReplyError(c,503,"ecosystem_busy","Extension operation unavailable; retry shortly",NULL);
}
