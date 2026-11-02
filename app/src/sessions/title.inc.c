/* One bounded native worker, independent of account state and Agent execution.
 * Only explicitly opted-in new sessions enter this state machine. */
#include "../../include/mdo/account.h"
#ifndef MDO_SESSION_TITLE_ENDPOINT
#define MDO_SESSION_TITLE_ENDPOINT MDO_ACCOUNT_SERVICE_ORIGIN "/api/v1/ai/session-title"
#endif
static struct {xmutex* Lock;xcond* Changed;xthread* Thread;xcancel* Cancel;bool Stop;MdoSession* Queue[32];size_t Count;} MdoTitle;
static size_t MdoTitleClip(char* out,size_t capacity,const char* text,size_t chars)
{
    size_t bytes=0,count=0;if(!text){out[0]=0;return 0;}
    const unsigned char* p=(const unsigned char*)text;size_t left=strlen(text);
    while(*p && count<chars){size_t n=*p<128?1:(*p&224)==192?2:(*p&240)==224?3:4;
        if(n>left || bytes+n>=capacity || !xrtUtf8Valid(xrtStrViewN((const char*)p,n),NULL))break;left-=n;
        if(*p<32){out[bytes++]=' ';p+=n;count++;continue;}
        memcpy(out+bytes,p,n);bytes+=n;p+=n;count++;
    }out[bytes]=0;return bytes;
}
static bool MdoTitlePath(char path[MDO_SESSION_PATH_CAPACITY],const MdoSessionInfo* info)
{return MdoSessionsPath(path,info->ProjectId,info->Id,"title-request.json");}
static void MdoTitleSave(MdoSession* session,const char* title,uint32 state)
{
    xwork_error error;MdoSessionDataLease* lease=MdoSessionDataAcquire(session->Info.ProjectId,session->Info.Id,MDO_SESSION_DATA_WRITE,&error);
    if(!lease)return;
    xrtMutexLock(session->Lock);xrtMutexLock(g_MdoSessions.Lock);
    MdoSessionInfo current,candidate;
    if(MdoSessionsMetaRead(session->Info.ProjectId,session->Info.Id,&current) && current.TitleState==2 && current.Status==MDO_SESSION_ACTIVE){
        bool open=session->Info.RuntimeOpen;session->Info=current;session->Info.RuntimeOpen=open;
        if(MdoSessionsCandidate(session,&candidate,&error)){
            candidate.TitleState=state;
            if(title && *title)snprintf(candidate.Title,sizeof(candidate.Title),"%s",title);
            (void)MdoSessionsCommit(session,&candidate,&error);
        }
    }
    xrtMutexUnlock(g_MdoSessions.Lock);xrtMutexUnlock(session->Lock);MdoSessionDataRelease(lease);
}
static bool MdoTitleEnqueue(MdoSession* session)
{
    if(!MdoTitle.Lock)return false;
    xrtMutexLock(MdoTitle.Lock);bool ok=!MdoTitle.Stop && MdoTitle.Count<32;
    if(ok){MdoTitle.Queue[MdoTitle.Count++]=MdoSessionRef(session);xrtCondSignal(MdoTitle.Changed);}
    xrtMutexUnlock(MdoTitle.Lock);return ok;
}
static void MdoTitleProcess(MdoSession* session)
{
    char path[MDO_SESSION_PATH_CAPACITY],*raw=NULL;size_t size=0;MdoSessionInfo info;info.Size=sizeof(info);
    xvalue* job=NULL,*response=NULL;char* body=NULL;size_t body_size=0;char title[257]={0};
    if(!MdoSessionGetInfo(session,&info) || info.TitleState!=2 || !MdoTitlePath(path,&info))return;
    xwork_error error;MdoSessionDataLease* lease=MdoSessionDataAcquire(info.ProjectId,info.Id,MDO_SESSION_DATA_WRITE,&error);
    if(lease){MdoSessionInfo current;
        if(MdoSessionsMetaRead(info.ProjectId,info.Id,&current) && current.TitleState==2 && current.Status==MDO_SESSION_ACTIVE &&
           MdoSessionsReadBounded(path,16384,&raw,&size))job=xrtJsonParse(xrtStrViewN(raw,size));
        MdoSessionDataRelease(lease);
    }
    xstrview content={0},id={0};uint64 created=0;
    bool valid=job && MdoSessionsValueString(job,"content",&content) && content.Size>0 && content.Size<=8192 &&
        MdoSessionsValueString(job,"request_id",&id) && id.Size==64 && MdoSessionsValueUInt(job,"created_at_us",&created);
    valid=valid && created<=INT64_MAX;
    if(valid && MdoUtcUs()>=(int64)created && MdoUtcUs()-(int64)created<60000000 && !xrtCancelRequested(MdoTitle.Cancel)){
        xvalue* request=xrtValueObject();MdoSessionsObjectString(request,"request_id",id.Data);MdoSessionsObjectString(request,"content",content.Data);MdoSessionsObjectString(request,"locale","auto");
        body=xrtJsonStringify(request,false,&body_size);xrtValueRelease(request);
        XS_FetchHeader headers[]={{"Content-Type","application/json"},{"Accept","application/json"}};
        XS_FetchRequest fetch={0};XS_FetchResponse result={0};fetch.Size=sizeof(fetch);fetch.Version=XS_FETCH_REQUEST_VERSION;
        fetch.Url=MDO_SESSION_TITLE_ENDPOINT;fetch.Method="POST";fetch.Headers=headers;fetch.HeaderCount=2;fetch.Body=body;fetch.BodySize=body_size;
        fetch.Timeout=8000000;fetch.IdleTimeout=6000000;fetch.MaxBodyBytes=4096;fetch.Cancel=MdoTitle.Cancel;
        if(body && xsFetch(&fetch,&result) && result.Status==200 && result.Body && !memchr(result.Body,0,result.BodySize)){
            response=xrtJsonParse(xrtStrViewN((const char*)result.Body,result.BodySize));uint64 code=1;xstrview text={0};
            if(MdoSessionsValueUInt(response,"code",&code) && !code && MdoSessionsValueString(xrtValueObjectGet(response,XRT_STR_LITERAL("data")),"title",&text) &&
               text.Size>0 && text.Size<sizeof(title) && xrtUtf8Valid(text,NULL)){
                bool clean=true;for(size_t i=0;i<text.Size;i++)if((unsigned char)text.Data[i]<32 || (unsigned char)text.Data[i]==127)clean=false;
                if(clean){memcpy(title,text.Data,text.Size);title[text.Size]=0;}
            }
        }
        if(result.Body)xrtSecureZero(result.Body,result.BodySize);xsFetchResponseUnit(&result);
    }
    /* Shutdown leaves the durable request pending for one idempotent restart.
     * Timeout, invalid output and all service refusals are silent fallbacks. */
    if(!xrtCancelRequested(MdoTitle.Cancel)){
        MdoTitleSave(session,title,title[0]?3u:4u);
        lease=MdoSessionDataAcquire(info.ProjectId,info.Id,MDO_SESSION_DATA_WRITE,&error);
        if(lease){(void)MdoHomeRemove(path,false);MdoSessionDataRelease(lease);}
    }
    if(body)xrtSecureZero(body,body_size);xrtFree(body);xrtValueRelease(response);xrtValueRelease(job);
    if(raw)xrtSecureZero(raw,size);xrtFree(raw);
}
static int32 MdoTitleWorker(void* unused)
{
    (void)unused;xwork_error error;MdoSessionCatalog* catalog=MdoSessionCatalogSnapshot(&error);
    for(size_t i=0;i<MdoSessionCatalogCount(catalog);i++){
        MdoSessionInfo info;info.Size=sizeof(info);if(!MdoSessionCatalogAt(catalog,i,&info)||info.TitleState!=2||info.Status!=MDO_SESSION_ACTIVE)continue;
        MdoSession* session=MdoSessionLoad(info.ProjectId,info.Id,&error);if(session){if(!MdoTitleEnqueue(session))MdoTitleSave(session,NULL,4);MdoSessionRelease(session);}
    }MdoSessionCatalogRelease(catalog);
    for(;;){
        xrtMutexLock(MdoTitle.Lock);while(!MdoTitle.Stop && !MdoTitle.Count)xrtCondWait(MdoTitle.Changed,MdoTitle.Lock);
        if(MdoTitle.Stop){xrtMutexUnlock(MdoTitle.Lock);break;}
        MdoSession* session=MdoTitle.Queue[0];MdoTitle.Count--;memmove(MdoTitle.Queue,MdoTitle.Queue+1,MdoTitle.Count*sizeof(*MdoTitle.Queue));
        xrtMutexUnlock(MdoTitle.Lock);MdoTitleProcess(session);MdoSessionRelease(session);
    }return 0;
}
static bool MdoTitleInit(void)
{
    memset(&MdoTitle,0,sizeof(MdoTitle));MdoTitle.Lock=xrtMutexCreate();MdoTitle.Changed=xrtCondCreate();MdoTitle.Cancel=xrtCancelCreate();
    if(MdoTitle.Lock && MdoTitle.Changed && MdoTitle.Cancel)MdoTitle.Thread=xrtThreadCreate(MdoTitleWorker,NULL,0);
    return MdoTitle.Thread!=NULL;
}
static void MdoTitleUnit(void)
{
    if(MdoTitle.Lock){xrtMutexLock(MdoTitle.Lock);MdoTitle.Stop=true;xrtCancelRequest(MdoTitle.Cancel);if(MdoTitle.Changed)xrtCondBroadcast(MdoTitle.Changed);xrtMutexUnlock(MdoTitle.Lock);}
    if(MdoTitle.Thread){xrtThreadWait(MdoTitle.Thread);xrtThreadDestroy(MdoTitle.Thread);}
    for(size_t i=0;i<MdoTitle.Count;i++)MdoSessionRelease(MdoTitle.Queue[i]);
    xrtCancelDestroy(MdoTitle.Cancel);xrtCondDestroy(MdoTitle.Changed);xrtMutexDestroy(MdoTitle.Lock);memset(&MdoTitle,0,sizeof(MdoTitle));
}
void MdoSessionTitleStart(MdoSession* session,const char* prompt,const xllm_message* message)
{
    if(!session)return;
    char seed[8193],fallback[257],path[MDO_SESSION_PATH_CAPACITY];seed[0]=0;
    const char* text=message?message->sContent:prompt;
    if(message && !text)for(size_t i=0;i<message->iPartCount;i++)if(message->pParts[i].eKind==XLLM_PART_TEXT){text=message->pParts[i].sText;break;}
    /* Prompt context envelopes contain extra quoted/file sources. Only the
     * visible user text is eligible for the anonymous naming service. */
    char visible[8193];size_t visible_size=text?strlen(text):0;
    const char* marker=text?strstr(text,"\n\n<!--mdo-context:v1-->\n"):NULL;
    if(marker)visible_size=(size_t)(marker-text);
    if(visible_size>8192){visible_size=8192;while(visible_size && ((unsigned char)text[visible_size]&192)==128)visible_size--;}
    if(visible_size)memcpy(visible,text,visible_size);visible[visible_size]=0;
    MdoTitleClip(seed,sizeof(seed),visible,2048);MdoTitleClip(fallback,sizeof(fallback),seed,80);xrtSecureZero(visible,sizeof(visible));
    if(!seed[0]){snprintf(seed,sizeof(seed),"Analyze the attached file");snprintf(fallback,sizeof(fallback),"File analysis");}
    xwork_error error;MdoSessionDataLease* lease=MdoSessionDataAcquire(session->Info.ProjectId,session->Info.Id,MDO_SESSION_DATA_WRITE,&error);if(!lease)return;
    bool pending=false;xrtMutexLock(session->Lock);xrtMutexLock(g_MdoSessions.Lock);
    MdoSessionInfo current,candidate;
    if(MdoSessionsMetaRead(session->Info.ProjectId,session->Info.Id,&current) && current.TitleState==1 && current.Status==MDO_SESSION_ACTIVE){
        bool open=session->Info.RuntimeOpen;session->Info=current;session->Info.RuntimeOpen=open;
        if(MdoSessionsCandidate(session,&candidate,&error) && MdoTitlePath(path,&current)){
            char identity[512],id[65];unsigned char hash[32];snprintf(identity,sizeof(identity),"title-v1/%s/%s/%lld",current.ProjectId,current.Id,(long long)current.CreatedAt);
            bool random_ok=xrtSha256(identity,strlen(identity),hash);for(size_t i=0;i<32;i++)snprintf(id+i*2,3,"%02x",hash[i]);
            xvalue* job=xrtValueObject();MdoSessionsObjectString(job,"request_id",id);MdoSessionsObjectString(job,"content",seed);MdoSessionsObjectTake(job,"created_at_us",xrtValueUInt(MdoUtcUs()));
            size_t size=0;char* json=xrtJsonStringify(job,false,&size);xrtValueRelease(job);
            candidate.TitleState=MdoTitle.Thread?2u:4u;snprintf(candidate.Title,sizeof(candidate.Title),"%s",fallback);
            if(random_ok && json && MdoHomeAtomicWrite(path,json,size,true) && MdoSessionsCommit(session,&candidate,&error))pending=candidate.TitleState==2;
            if(json)xrtSecureZero(json,size);xrtFree(json);
        }
    }
    xrtMutexUnlock(g_MdoSessions.Lock);xrtMutexUnlock(session->Lock);MdoSessionDataRelease(lease);xrtSecureZero(seed,sizeof(seed));
    if(pending && !MdoTitleEnqueue(session))MdoTitleSave(session,NULL,4);
}
