#include "internal.h"
#include "write_admission.h"
#include "../account/internal.h"
#include "../../include/mdo/ecosystem_package.h"
#include "../../include/mdo/distribution.h"
#include "../../include/mdo/extension_files.h"
bool MdoApiEcosystemLocal(MdoApiContext*,const xvalue*);

/* Preview never enters a runtime manager. Both download and local import use
 * the same platform and explicit C trust checks before preparing any writes. */
bool MdoEcosystemPackageCompatible(const xvalue* package,const xvalue* input,char* error,size_t capacity)
{
    if(!MdoPackageValidate(package,error,capacity))return false;
    const xvalue* manifest=xrtValueObjectGet(package,XRT_STR_LITERAL("manifest"));
    const xvalue* platforms=xrtValueObjectGet(manifest,XRT_STR_LITERAL("platforms"));bool compatible=false,code=false,trust=false;
    for(size_t i=0;i<xrtValueCount(platforms);i++){xstrview p;if(xrtValueGetString(xrtValueArrayGet(platforms,i),&p)&&xrtStrEqual(p,xrtStrView(MdoToolPlatform())))compatible=true;}
    const xvalue* resources=xrtValueObjectGet(package,XRT_STR_LITERAL("resources"));
    for(size_t i=0;i<xrtValueCount(resources);i++){cstr kind=MdoPackageText(xrtValueArrayGet(resources,i),"kind",16);if(kind&&(!strcmp(kind,"tools")||!strncmp(kind,"c-",2)))code=true;}
    (void)xrtValueGetBool(xrtValueObjectGet(input,XRT_STR_LITERAL("trust_code")),&trust);
    if(!compatible|| (code&&!trust)){snprintf(error,capacity,"Package does not support this device, or C code approval is required");return false;}
    return true;
}

/* First-party proxy: tokens never enter JavaScript, paths are constructed from
 * validated fields, redirects and ambiguous POST retries are forbidden. */
static xvalue* EcoService(cstr path,cstr method,const xvalue* body,MdoAccountLease* lease,uint16* status)
{
    char url[1024],auth[4200];XS_FetchHeader headers[3];XS_FetchRequest req={0};XS_FetchResponse resp={0};size_t size=0;
    char* json=body?xrtJsonStringify(body,false,&size):NULL;xvalue* envelope=NULL,*data=NULL;*status=0;
    if(!lease->Managed||!lease->AccessToken||size>MDO_PACKAGE_LIMIT||xrtCancelRequested(lease->Cancel))goto done;
    snprintf(url,sizeof(url),MDO_ACCOUNT_SERVICE_ORIGIN "%s",path);snprintf(auth,sizeof(auth),"Bearer %s",lease->AccessToken);
    headers[0]=(XS_FetchHeader){"Authorization",auth};headers[1]=(XS_FetchHeader){"Accept","application/json"};headers[2]=(XS_FetchHeader){"Content-Type","application/json"};
    req.Size=sizeof(req);req.Version=XS_FETCH_REQUEST_VERSION;req.Url=url;req.Method=method;req.Headers=headers;req.HeaderCount=3;req.Body=json;req.BodySize=size;
    req.Timeout=20000000;req.IdleTimeout=10000000;req.MaxBodyBytes=MDO_PACKAGE_LIMIT+65536;req.Cancel=lease->Cancel;resp.Size=sizeof(resp);
    if(!xsFetch(&req,&resp))goto done;*status=resp.Status;
    if(!resp.Body||!xrtUtf8Valid((xstrview){(cstr)resp.Body,resp.BodySize},NULL))goto done;
    xjsonreadconfig limits;xrtJsonReadConfigInit(&limits);limits.MaxInputBytes=MDO_PACKAGE_LIMIT+65536;limits.MaxDepth=16;limits.MaxValues=24000;limits.MaxStringBytes=MDO_PACKAGE_LIMIT;
    envelope=xrtJsonRead((xstrview){(cstr)resp.Body,resp.BodySize},&limits);int64 code=-1;
    if(!envelope||!MdoPackageNumber(envelope,"code",&code)||code)goto done;
    data=xrtValueObjectTake(envelope,XRT_STR_LITERAL("data"));
done:
    xrtSecureZero(auth,sizeof(auth));xrtFree(json);xrtValueRelease(envelope);xsFetchResponseUnit(&resp);
    if(xrtCancelRequested(lease->Cancel)){xrtValueRelease(data);data=NULL;*status=0;}return data;
}
static bool EcoHandleInput(MdoApiContext* c,xvalue* input)
{
    cstr action=MdoPackageText(input,"action",16);char path[1000];bool valid=action!=NULL;
    const xvalue* body=NULL;const char* method="GET";int64 id=0;
    if(valid&&(!strcmp(action,"catalog")||!strcmp(action,"mine"))){cstr query=MdoPackageText(input,"query",100),kind=MdoPackageText(input,"kind",16);int64 before=0;
        const xvalue* v=xrtValueObjectGet(input,XRT_STR_LITERAL("before"));if(v&&!MdoPackageNumber(input,"before",&before))valid=false;if(before<0)valid=false;
        if(kind&&kind[0]&&!MdoPackageKind(kind))valid=false;
        size_t n=0;char* encoded=xrtPercentEncodeNew(query?query:"",query?strlen(query):0,xrtStrView(""),&n);
        if(!encoded)valid=false;snprintf(path,sizeof(path),"/api/v1/mdo/ecosystem/packages?q=%s&kind=%s%s%s%lld",encoded?encoded:"",kind?kind:"",!strcmp(action,"mine")?"&mine=1":"",before?"&before=":"&unused=",(long long)before);xrtFree(encoded);
    }else if(valid&&(!strcmp(action,"detail")||!strcmp(action,"latest")||!strcmp(action,"install")||!strcmp(action,"history"))){valid=MdoPackageNumber(input,"id",&id)&&id>0;
        int64 before=0;if(xrtValueObjectGet(input,XRT_STR_LITERAL("before"))&&(!MdoPackageNumber(input,"before",&before)||before<0))valid=false;
        snprintf(path,sizeof(path),"/api/v1/mdo/ecosystem/%s?id=%lld%s&before=%lld",!strcmp(action,"history")?"history":"package",(long long)id,!strcmp(action,"latest")?"&latest=1":"",(long long)(before?before:INT64_MAX));
    }else if(valid&&!strcmp(action,"withdraw")){int64 revision=0;valid=MdoPackageNumber(input,"id",&id)&&id>0&&MdoPackageNumber(input,"revision",&revision)&&revision>0;body=input;strcpy(path,"/api/v1/mdo/ecosystem/withdraw");method="POST";
    }else if(valid&&!strcmp(action,"submit")){body=xrtValueObjectGet(input,XRT_STR_LITERAL("package"));char error[256];valid=MdoPackageValidate(body,error,sizeof(error));strcpy(path,"/api/v1/mdo/ecosystem/submit");method="POST";
    }else if(valid&&(!strcmp(action,"uninstall")||!strcmp(action,"export")||!strcmp(action,"c_sources")||!strcmp(action,"import")||!strcmp(action,"drafts")||!strcmp(action,"draft_read")||!strcmp(action,"draft_save")||!strcmp(action,"draft_delete"))){bool result=MdoApiEcosystemLocal(c,input);xrtValueRelease(input);return result;
    }else valid=false;
    if(!valid){xrtValueRelease(input);return MdoApiReplyError(c,422,"ecosystem_request_invalid","Invalid store request or portable package",NULL);}
    MdoAccountLease lease={0};uint16 status=0;
    if(!MdoAccountAcquireService(c->SendCancel,&lease)){xrtValueRelease(input);return MdoApiReplyError(c,401,"ecosystem_login_refresh","Login is renewing; retry shortly",NULL);}
    xvalue* data=EcoService(path,method,body,&lease,&status);if(status==401)(void)MdoAccountRejectAccess(&lease);MdoAccountRelease(&lease);
    if(!data){xrtValueRelease(input);return MdoApiReplyError(c,status>=400&&status<600?status:502,"ecosystem_service_failed",status==409?"This version already exists; publish a new version":status==401?"Sign in again or wait for login renewal":"The extension service could not complete this request",NULL);}
    if(!strcmp(action,"install")){
        const xvalue* package=xrtValueObjectGet(data,XRT_STR_LITERAL("package"));char error[256];bool compatible=MdoEcosystemPackageCompatible(package,input,error,sizeof(error));
        cstr state=MdoPackageText(data,"state",16);compatible=compatible&&state&&!strcmp(state,"published");
        size_t n=0;char* json=xrtJsonStringify(package,false,&n);char hash[65];cstr expected=MdoPackageText(data,"sha256",64);compatible=compatible&&json&&expected&&MdoExtensionHashBytes(json,n,hash)&&!strcmp(hash,expected);xrtFree(json);
        bool result;
        if(!compatible)result=MdoApiReplyError(c,409,"ecosystem_incompatible","Package is unpublished, incompatible, unverified, or C code approval is required",NULL);
        else {xvalue* local=xrtValueObject();MdoApiValueSetString(local,"action","install");xrtValueObjectSet(local,XRT_STR_LITERAL("entry"),data);result=MdoApiEcosystemLocal(c,local);xrtValueRelease(local);}
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
    MdoApiLiveChanged(NULL);return job->Sent?XTASK_SUCCESS:XTASK_FAILED;
}
void MdoApiEcosystemUnit(void)
{
    if(!EcoApi.Lock)return;xrtMutexLock(EcoApi.Lock);EcoApi.Stopping=true;xtaskpool* pool=EcoApi.Pool;xrtMutexUnlock(EcoApi.Lock);
    if(pool){xrtTaskPoolCancel(pool);xrtTaskPoolWait(pool);xrtTaskPoolDestroy(pool);}
    xrtMutexDestroy(EcoApi.Lock);memset(&EcoApi,0,sizeof(EcoApi));
}
bool MdoApiEcosystemRoute(MdoApiContext* c)
{
    if(!MdoAccountHasSession())return MdoApiReplyError(c,401,"ecosystem_login_required","Sign in to use the extension store",NULL);
    if(c->Request->head->MethodCode==XHTTP_METHOD_GET){xvalue* input=xrtValueObject();MdoApiValueSetString(input,"action","installed");bool result=MdoApiEcosystemLocal(c,input);xrtValueRelease(input);return result;}
    char* raw=NULL;size_t length=0;MdoApiBodyStatus read=MdoApiBinaryBodyRead(c,MDO_PACKAGE_LIMIT+4096,&raw,&length);
    if(read!=MDO_API_BODY_OK)return MdoApiReplyBodyError(c,read);
    xjsonreadconfig limits;xrtJsonReadConfigInit(&limits);limits.MaxInputBytes=MDO_PACKAGE_LIMIT+4096;limits.MaxStringBytes=MDO_PACKAGE_LIMIT;limits.MaxDepth=16;limits.MaxValues=24000;
    xvalue* input=xrtJsonRead(xrtStrViewN(raw,length),&limits);xrtFree(raw);
    if(!input)return MdoApiReplyError(c,422,"ecosystem_request_invalid","Expected a bounded JSON object",NULL);
    EcoApiJob* job=xrtCalloc(1,sizeof(*job));xfuture* future=NULL;
    if(!job||!EcoApi.Lock)goto failed;
    job->Head.MethodCode=XHTTP_METHOD_POST;job->Request.head=&job->Head;
    if(c->Request->tls)job->Request.tls=xrtTlsStreamRef(c->Request->tls);else if(c->Request->tcp)job->Request.tcp=xrtNetStreamRef(c->Request->tcp);
    if(!job->Request.tls&&!job->Request.tcp)goto failed;
    job->Context.Request=&job->Request;job->Context.Target.Path=xrtStrView("/api/v1/ecosystem");job->Context.CloseResponse=true;
    job->Context.SendDeadline=xrtDeadlineAfter(30000000);memcpy(job->Context.RequestId,c->RequestId,sizeof(c->RequestId));job->Input=input;
    /* Transfer the admitted write count to the owned job. Home purge/import
     * stays fenced until network work and any resource publication finish. */
    job->Context.WriteShared=c->WriteShared;job->Context.WriteExclusive=c->WriteExclusive;
    xrtMutexLock(EcoApi.Lock);
    if(!EcoApi.Stopping&&EcoApi.Count<6){
        if(!EcoApi.Pool){xtaskpoolconfig config={0};config.Threads=2;config.QueueLimit=4;EcoApi.Pool=xrtTaskPoolCreate(&config);}
        if(EcoApi.Pool){xtaskargs args={0};args.Destroy=EcoApiDrop;++EcoApi.Count;future=xrtTaskSubmit(EcoApi.Pool,EcoApiRun,job,&args);if(!future)--EcoApi.Count;}
    }
    xrtMutexUnlock(EcoApi.Lock);
    if(future){c->WriteShared=c->WriteExclusive=false;c->Takeover=true;xrtFutureDestroy(future);return true;}
failed:
    if(job){xrtTlsStreamDestroy(job->Request.tls);xrtNetStreamDestroy(job->Request.tcp);xrtFree(job);}xrtValueRelease(input);
    return MdoApiReplyError(c,503,"ecosystem_busy","Extension operation unavailable; retry shortly",NULL);
}
