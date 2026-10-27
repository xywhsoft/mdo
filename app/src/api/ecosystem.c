#include "../../include/mdo/time.h"
#include "../../include/mdo/extensions.h"
#include "../../include/mdo/extension_manifest.h"
#include "../../include/mdo/extension_tasks.h"
#include "../../include/mdo/extension_service.h"
#include "internal.h"
#include "write_admission.h"

static bool EcoHandleInput(MdoApiContext* C,xvalue* Input){
 char Error[512]={0};xvalue* Out=NULL;cstr Action=MdoExtensionText(Input,"action",24);bool Ok=false;
 const cstr Retired[]={"package","resources","files","base64","format","min_build_id","single_kind","artifact","references"};for(size_t I=0;I<sizeof(Retired)/sizeof(*Retired);I++)if(xrtValueObjectHas(Input,xrtStrView(Retired[I]))){xrtValueRelease(Input);return MdoApiReplyError(C,422,"extension_json_retired","Extensions are complete directory ZIP files; embedded file payloads and legacy package fields are unsupported",NULL);}
 if(Action&&!strcmp(Action,"installed")){
  const cstr Kinds[]={"agents","subagents","tools","mcp","skills","commands","external-tools","knowledge","plugins"};Out=xrtValueObject();Ok=Out!=NULL;
  for(size_t I=0;Ok&&I<9;I++){xvalue* Rows=MdoExtensionsList(Kinds[I],Error);Ok=Rows!=NULL;for(size_t J=0;Ok&&J<xrtValueCount(Rows);J++){const xvalue* Row=xrtValueArrayGet(Rows,J);cstr Id=MdoExtensionText(Row,"id",96);char* Key=Id?xrtFormat("%s:%s",Kinds[I],Id):NULL;Ok=Key&&xrtValueObjectSet(Out,xrtStrView(Key),Row);xrtFree(Key);}xrtValueRelease(Rows);}
 }else if(Action&&(!strcmp(Action,"catalog")||!strcmp(Action,"detail")||!strcmp(Action,"files")||!strcmp(Action,"preview")||!strcmp(Action,"mine")||!strcmp(Action,"profile")||!strcmp(Action,"requests")))Ok=MdoExtensionServiceRead(Input,C->SendCancel,&Out,Error);
 else if(Action&&!strcmp(Action,"download"))Ok=MdoExtensionServiceDownload(MdoExtensionText(Input,"kind",32),MdoExtensionText(Input,"version_id",32),C->SendCancel,&Out,Error);
 else if(Action&&!strcmp(Action,"submit"))Ok=MdoExtensionServicePublish(Input,C->SendCancel,&Out,Error);
 else if(Action)Ok=MdoExtensionServiceWrite(Input,C->SendCancel,&Out,Error);
 xrtValueRelease(Input);if(Ok)return MdoApiReplySuccessTake(C,200,Out,NULL);xrtValueRelease(Out);
 return MdoApiReplyError(C,422,"extension_service_failed",Error[0]?Error:"Choose a directory store action; JSON packages and embedded file payloads are unsupported",NULL);
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
    if(!EcoApi.Lock)return;
    xrtMutexLock(EcoApi.Lock);EcoApi.Stopping=true;xtaskpool* pool=EcoApi.Pool;xrtMutexUnlock(EcoApi.Lock);
    if(pool){xrtTaskPoolCancel(pool);xrtTaskPoolWait(pool);xrtTaskPoolDestroy(pool);}
    xrtMutexDestroy(EcoApi.Lock);memset(&EcoApi,0,sizeof(EcoApi));
}
bool MdoApiEcosystemRoute(MdoApiContext* c)
{
    if(c->Request->head->MethodCode==XHTTP_METHOD_GET){xvalue* input=xrtValueObject();MdoApiValueSetString(input,"action","installed");return EcoHandleInput(c,input);}
    char* raw=NULL;size_t length=0;MdoApiBodyStatus read=MdoApiBinaryBodyRead(c,16384,&raw,&length);
    if(read!=MDO_API_BODY_OK)return MdoApiReplyBodyError(c,read);
    xjsonreadconfig limits;xrtJsonReadConfigInit(&limits);limits.MaxInputBytes=16384;limits.MaxStringBytes=8192;limits.MaxDepth=8;limits.MaxValues=256;
    xvalue* input=xrtJsonRead(xrtStrViewN(raw,length),&limits);xrtFree(raw);
    if(!input)return MdoApiReplyError(c,422,"ecosystem_request_invalid","Expected a bounded JSON object",NULL);
    if(c->ApplicationResponse)return EcoHandleInput(c,input);
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
