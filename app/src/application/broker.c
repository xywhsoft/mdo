#include "../../include/mdo/application.h"
#include "../remote/loopback.h"
#include "../api/write_admission.h"
#include "../../include/mdo/home.h"
#include "internal.h"
static XS_ServerInfo* SdkServer;
static xmutex* SdkEventLock;
static xcond* SdkEventCondition;
static bool SdkEventStopping;
static size_t SdkEventWaiters;
static xvalue* SdkEventRing[256];
static uint64 SdkEventSequence;
static char SdkEventEpoch[33];
XS_ServerInfo* MdoApplicationServer(void){return SdkServer;}
bool MdoApplicationInit(XS_ServerInfo* Server){
    if(SdkServer)return true;
    SdkEventLock=xrtMutexCreate();SdkEventCondition=xrtCondCreate();SdkEventStopping=false;SdkServer=xsServerRetain(Server);
    unsigned char Epoch[16];if(!xrtSecureRandom(Epoch,sizeof(Epoch))){MdoApplicationUnit();return false;}
    for(size_t i=0;i<16;i++)snprintf(SdkEventEpoch+i*2,3,"%02x",Epoch[i]);
    if(!SdkEventLock||!SdkEventCondition||!SdkServer||!MdoApplicationChangesInit()||!MdoApplicationStreamsInit()||!MdoApplicationJobsInit()||!MdoApplicationStateInit()||!MdoApplicationTerminalsInit()||!MdoApplicationPluginsInit()||!MdoApplicationUiInit()||!MdoApplicationWorkbenchInit()){MdoApplicationUnit();return false;}return true;
}
void MdoApplicationUnit(void){
    if(SdkEventLock){xrtMutexLock(SdkEventLock);SdkEventStopping=true;if(SdkEventCondition)xrtCondBroadcast(SdkEventCondition);xrtMutexUnlock(SdkEventLock);}
    MdoApplicationJobsUnit();
    MdoApplicationStateUnit();
    MdoApplicationTerminalsUnit();
    MdoApplicationStreamsUnit();
    MdoApplicationPluginsUnit();
    MdoApplicationUiUnit();
    MdoApplicationWorkbenchUnit();
    MdoApplicationChangesUnit();
    if(SdkEventLock&&SdkEventCondition){xrtMutexLock(SdkEventLock);while(SdkEventWaiters)xrtCondWait(SdkEventCondition,SdkEventLock);xrtMutexUnlock(SdkEventLock);}
    for(size_t i=0;i<256;i++){xrtValueRelease(SdkEventRing[i]);SdkEventRing[i]=NULL;}
    if(SdkEventCondition)xrtCondDestroy(SdkEventCondition);
    SdkEventCondition=NULL;
    if(SdkEventLock)xrtMutexDestroy(SdkEventLock);
    SdkEventLock=NULL;
    xsServerRelease(SdkServer);SdkServer=NULL;SdkEventSequence=0;
}
void MdoApplicationEvent(cstr Topic,const mdo_sdk_context_v1* C,const xvalue* Data){
    if(!SdkEventLock||!Topic)return;
    xvalue* V=xrtValueObject();if(!V)return;
    SdkPut(V,"topic",Topic);SdkPut(V,"extension",C?C->ExtensionId:"");SdkPut(V,"invocation",C?C->InvocationId:"");
    SdkPut(V,"project",C?C->ProjectId:"");SdkPut(V,"session",C?C->SessionId:"");
    char* Payload=Data?SdkJson(Data):NULL;size_t PayloadBytes=Payload?strlen(Payload):0;
    xrtValueObjectSetNew(V,XRT_STR_LITERAL("data"),Data&&PayloadBytes<=8192?xrtValueClone(Data):xrtValueNull());
    if(PayloadBytes>8192){xrtValueObjectSetNew(V,XRT_STR_LITERAL("data_omitted"),xrtValueBool(true));SdkPutUInt(V,"data_bytes",PayloadBytes);}
    xrtFree(Payload);
    xrtMutexLock(SdkEventLock);uint64 N=++SdkEventSequence;SdkPutUInt(V,"sequence",N);SdkPutUInt(V,"time_us",SdkClock());
    size_t I=(size_t)(N%256u);xrtValueRelease(SdkEventRing[I]);SdkEventRing[I]=V;if(SdkEventCondition)xrtCondBroadcast(SdkEventCondition);xrtMutexUnlock(SdkEventLock);
}
xvalue* MdoApplicationEvents(const xvalue* P,mdo_error_v1* E){
    if(!SdkEventLock){MdoApplicationError(E,"sdk_unavailable","Event journal is unavailable","events",503,false);return NULL;}
    uint64 After=0;const xvalue* A=SdkGet(P,"after");if(A&&!SdkUInt(A,&After)){MdoApplicationError(E,"sdk_arguments_invalid","after must be an unsigned sequence","events",400,false);return NULL;}
    uint64 Limit=64;if((SdkGet(P,"limit")&&!SdkUInt(SdkGet(P,"limit"),&Limit))||!Limit||Limit>256){MdoApplicationError(E,"sdk_arguments_invalid","limit must be 1..256","events",400,false);return NULL;}
    cstr Topic=SdkField(P,"topic");xvalue* R=xrtValueObject(),*Items=xrtValueArray();
    if(!R||!Items){xrtValueRelease(R);xrtValueRelease(Items);return NULL;}
    xrtMutexLock(SdkEventLock);uint64 Last=SdkEventSequence,First=Last>=256?Last-255:1;
    uint64 Cursor=After>Last?Last:After<First?First-1:After;size_t Bytes=0;
    for(uint64 N=First;N<=Last;N++)if(N>After){const xvalue* V=SdkEventRing[N%256];
        if(V&&(!Topic||!strcmp(Topic,SdkField(V,"topic")))){
            char* Json=SdkJson(V);size_t Size=Json?strlen(Json):32769;xrtFree(Json);
            if(xrtValueCount(Items)>=Limit||Size>32768-Bytes)break;
            if(!xrtValueArrayAppend(Items,V))break;
            Bytes+=Size;
        }Cursor=N;
    }
    SdkPutUInt(R,"cursor",Cursor);SdkPutUInt(R,"latest",Last);SdkPutUInt(R,"oldest",First);SdkPut(R,"epoch",SdkEventEpoch);
    xrtValueObjectSetNew(R,XRT_STR_LITERAL("has_more"),xrtValueBool(Cursor<Last));
    cstr Epoch=SdkField(P,"epoch");
    xrtValueObjectSetNew(R,XRT_STR_LITERAL("resync_required"),xrtValueBool((Epoch&&strcmp(Epoch,SdkEventEpoch))||After>Last||(After&&First>After&&First-After>1)));
    xrtMutexUnlock(SdkEventLock);xrtValueObjectSetNew(R,XRT_STR_LITERAL("items"),Items);return R;
}
/* Predicate + sequence recheck closes the read/wait lost-wakeup race. Wait
 * slices exist only to observe a borrowed cancellation token promptly. */
xvalue* MdoApplicationEventsWait(const mdo_sdk_context_v1* C,const xvalue* P,mdo_error_v1* E){
    uint64 Timeout=0;if((SdkGet(P,"timeout_ms")&&!SdkUInt(SdkGet(P,"timeout_ms"),&Timeout))||Timeout>60000){MdoApplicationError(E,"sdk_arguments_invalid","timeout_ms must be 0..60000","events",400,false);return NULL;}
    if(!SdkEventLock||!SdkEventCondition){MdoApplicationError(E,"sdk_unavailable","Event waiting is unavailable","events",503,false);return NULL;}
    xrtMutexLock(SdkEventLock);if(SdkEventStopping){xrtMutexUnlock(SdkEventLock);MdoApplicationError(E,"sdk_stopping","Event service is stopping","events",503,false);return NULL;}SdkEventWaiters++;xrtMutexUnlock(SdkEventLock);
    uint64 End=MdoDeadlineAfterUs(Timeout*1000);xvalue* Result=NULL;
    for(;;){
        if(SdkCancelled(C)){MdoApplicationInvocation* I=C->Opaque;bool Cancelled=I&&I->Cancel&&xrtCancelRequested(I->Cancel);
            MdoApplicationError(E,Cancelled?"sdk_cancelled":"sdk_deadline_expired",Cancelled?"Event wait cancelled":"Event wait deadline expired","events",Cancelled?499:504,false);break;}
        Result=MdoApplicationEvents(P,E);if(!Result)break;
        bool Resync=false;xrtValueGetBool(SdkGet(Result,"resync_required"),&Resync);uint64 Last=0;SdkUInt(SdkGet(Result,"latest"),&Last);
        if(Resync||xrtValueCount(SdkGet(Result,"items"))||!Timeout||MdoDeadlineExpired(End))break;
        xrtValueRelease(Result);Result=NULL;
        xrtMutexLock(SdkEventLock);bool Stopping=SdkEventStopping;bool WaitFailed=false;
        if(!Stopping&&SdkEventSequence==Last){
            uint64 Remaining=MdoDeadlineRemainingUs(End);if(C->Deadline){uint64 Caller=MdoDeadlineRemainingUs(C->Deadline);if(Caller<Remaining)Remaining=Caller;}
            int64 Ms=MdoDurationMs(Remaining);if(Ms>100)Ms=100;if(Ms>0){xwaitresult Wait=xrtCondWaitFor(SdkEventCondition,SdkEventLock,Ms);WaitFailed=Wait==XWAIT_ERROR||Wait==XWAIT_CLOSED;}
        }xrtMutexUnlock(SdkEventLock);
        if(WaitFailed){MdoApplicationError(E,"sdk_event_wait_failed","Native event wait failed","events",503,false);break;}
        if(Stopping){MdoApplicationError(E,"sdk_stopping","Event service is stopping","events",503,false);break;}
    }
    xrtMutexLock(SdkEventLock);SdkEventWaiters--;xrtCondBroadcast(SdkEventCondition);xrtMutexUnlock(SdkEventLock);return Result;
}

typedef struct SdkResponse {unsigned Status;char *Body,*Type,*Revision;size_t Bytes;} SdkResponse;
static bool SdkResponseHead(const xhttp1head* H,void* U){
    SdkResponse* R=U;R->Status=H->Status;
    for(size_t i=0;i<H->FieldCount;i++){
        char** To=xrtStrCaseEqual(H->Fields[i].Name,XRT_STR_LITERAL("ETag"))?&R->Revision:
            xrtStrCaseEqual(H->Fields[i].Name,XRT_STR_LITERAL("Content-Type"))?&R->Type:NULL;
        if(To){xrtFree(*To);*To=xrtStrDupN(H->Fields[i].Value.Data,H->Fields[i].Value.Size);if(!*To)return false;}
    }return true;
}
static bool SdkResponseBody(xbytesview B,void* U){
    SdkResponse* R=U;if(B.Size>MDO_REMOTE_HTTP_RESPONSE_MAX-R->Bytes)return false;
    char* N=xrtRealloc(R->Body,R->Bytes+B.Size+1);if(!N)return false;
    R->Body=N;memcpy(N+R->Bytes,B.Data,B.Size);R->Bytes+=B.Size;N[R->Bytes]=0;return true;
}
static bool SdkAddEncoded(char* To,size_t Cap,size_t* At,xstrview S){
    static const char Hex[]="0123456789ABCDEF";
    for(size_t i=0;i<S.Size;i++){
        unsigned char C=(unsigned char)S.Data[i];bool Plain=(C>='a'&&C<='z')||(C>='A'&&C<='Z')||(C>='0'&&C<='9')||C=='-'||C=='_'||C=='.'||C=='~';
        size_t N=Plain?1:3;if(*At+N>=Cap)return false;
        if(Plain)To[(*At)++]=(char)C;else {To[(*At)++]='%';To[(*At)++]=Hex[C>>4];To[(*At)++]=Hex[C&15];}
    }To[*At]=0;return true;
}
static bool SdkTarget(const mdo_sdk_context_v1* C,const MdoApplicationRoute* Route,const xvalue* P,char Target[8192],mdo_error_v1* E){
    cstr Pattern=Route->Path;size_t At=0;const xvalue* Path=SdkGet(P,"path");
    for(size_t i=0;Pattern[i];i++){
        if(Pattern[i]!='{'){if(At+1>=8192)goto bad;Target[At++]=Pattern[i];continue;}
        char Key[65];size_t N=0;i++;
        while(Pattern[i]&&Pattern[i]!='}') {if(N>=64)goto bad;Key[N++]=Pattern[i++];}Key[N]=0;
        cstr V=SdkField(Path,Key);
        if(!V&&!strcmp(Key,"project"))V=C->ProjectId;
        if(!V&&!strcmp(Key,"session"))V=C->SessionId;
        if(!V&&!strcmp(Key,"run"))V=C->RunId;
        if(!V||!V[0]||!strcmp(V,".")||!strcmp(V,"..")||!SdkAddEncoded(Target,8192,&At,xrtStrView(V)))goto bad;
    }Target[At]=0;
    const xvalue* Query=SdkGet(P,"query");
    if(Query&&xrtValueType(Query)!=XVALUE_OBJECT)goto bad;
    for(size_t i=0;i<xrtValueCount(Query);i++){
        xstrview Key={0};const xvalue* V=xrtValueObjectAt(Query,i,&Key);char* Text=NULL;
        if(xrtValueType(V)==XVALUE_STRING)Text=xrtStrDup(SdkText(V));else if(xrtValueType(V)!=XVALUE_OBJECT&&xrtValueType(V)!=XVALUE_ARRAY)Text=SdkJson(V);
        if(!Text||At+1>=8192){xrtFree(Text);goto bad;}
        Target[At++]=i?'&':'?';bool Ok=SdkAddEncoded(Target,8192,&At,Key)&&At+1<8192;
        if(Ok){Target[At++]='=';Ok=SdkAddEncoded(Target,8192,&At,xrtStrView(Text));}xrtFree(Text);if(!Ok)goto bad;
    }return true;
bad:return MdoApplicationError(E,"sdk_arguments_invalid","Missing/invalid resource identity or query; use path/query objects","resolve",400,false);
}
bool MdoApplicationRouteInvoke(const mdo_sdk_context_v1* C,const MdoApplicationRoute* Route,const xvalue* P,xvalue** Out,mdo_error_v1* E){
    if(!SdkServer)return MdoApplicationError(E,"sdk_unavailable","Application services are not ready","dispatch",503,false);
    if(P&&xrtValueType(P)!=XVALUE_OBJECT)return MdoApplicationError(E,"sdk_arguments_invalid","Service parameters must be an object","resolve",400,false);
    char Target[8192]={0};if(!SdkTarget(C,Route,P,Target,E))return false;
    const xvalue *Body=SdkGet(P,"body"),*Fields=SdkGet(P,"headers");cstr Revision=SdkField(P,"revision");
    char* Json=Body?SdkJson(Body):NULL;XS_FetchHeader Headers[20];size_t Count=0;
    cstr BinaryText=SdkField(P,"body_base64");unsigned char* Binary=NULL;size_t BinarySize=0;
    bool ArchiveRoute=!strncmp(Route->Path,"/api/v1/extension-tasks",23);cstr BodyFile=SdkField(P,"body_file"),ResponseFile=SdkField(P,"response_file");
    if(ArchiveRoute&&BinaryText){xrtFree(Json);return MdoApplicationError(E,"sdk_arguments_invalid","Archive bytes use native body_file/response_file or raw HTTP, never a Base64 JSON container","resolve",400,false);}
    if(ArchiveRoute&&BodyFile){uint64 Offset=0,Bytes=0;bool Valid=!Body&&!BinaryText&&xrtPathIsRooted(BodyFile)&&SdkUInt(SdkGet(P,"body_bytes"),&Bytes)&&Bytes&&Bytes<=1048576;
        if(SdkGet(P,"body_offset")&&!SdkUInt(SdkGet(P,"body_offset"),&Offset))Valid=false;
        xfile File=Valid?xrtOpen(BodyFile,XFILE_READ|XFILE_NOFOLLOW):NULL;uint64 Size=0;Valid=Valid&&File&&xrtFileSize(File,&Size)&&Offset<=Size&&Bytes<=Size-Offset;Binary=Valid?xrtMalloc((size_t)Bytes):NULL;
        Valid=Valid&&Binary&&xrtReadAtFull(File,Offset,Binary,(size_t)Bytes,NULL);xrtClose(File);if(!Valid){xrtFree(Binary);xrtFree(Json);return MdoApplicationError(E,"sdk_arguments_invalid","Archive source file or byte range is unavailable; chunks must be at most 1 MiB","resolve",400,false);}BinarySize=(size_t)Bytes;
    }
    if(BinaryText){
        bool Valid=!Body&&xrtBase64Decode(BinaryText,strlen(BinaryText),NULL,0,&BinarySize,NULL)&&BinarySize<=MDO_REMOTE_HTTP_BODY_MAX;
        Binary=Valid?xrtMalloc(BinarySize+1):NULL;
        if(!Binary||!xrtBase64Decode(BinaryText,strlen(BinaryText),Binary,BinarySize,&BinarySize,NULL)){xrtFree(Binary);xrtFree(Json);return MdoApplicationError(E,"sdk_arguments_invalid","Invalid or oversized binary body","resolve",400,false);}
    }
    char Token[MDO_API_WRITE_TOKEN_CAPACITY];bool Ok=!Fields||xrtValueType(Fields)==XVALUE_OBJECT;
    for(size_t i=0;Ok&&i<xrtValueCount(Fields);i++){
        cstr Key=SdkKey(Fields,i),Value=SdkText(xrtValueObjectGet(Fields,xrtStrView(Key)));
        Ok=Count<16&&Key&&Value&&!strpbrk(Key,"\r\n")&&!strpbrk(Value,"\r\n")&&
            !xrtStrCaseEqual(xrtStrView(Key),XRT_STR_LITERAL("X-Mdo-Write-Token"))&&
            !xrtStrCaseEqual(xrtStrView(Key),XRT_STR_LITERAL("Host"))&&!xrtStrCaseEqual(xrtStrView(Key),XRT_STR_LITERAL("Origin"));
        if(Ok)Headers[Count++]=(XS_FetchHeader){Key,Value};
    }
    if(!Ok){xrtFree(Binary);xrtFree(Json);return MdoApplicationError(E,"sdk_arguments_invalid","Invalid, reserved or excessive service headers","resolve",400,false);}
    MdoApplicationInvocation* Caller=C->Opaque;
    if(strcmp(Route->Method,"GET")){if(Caller&&Caller->WriteToken[0])snprintf(Token,sizeof(Token),"%s",Caller->WriteToken);else Ok=Ok&&MdoApiWriteToken(Token);Headers[Count++]=(XS_FetchHeader){"X-Mdo-Write-Token",Token};}
    if(Revision)Headers[Count++]=(XS_FetchHeader){"If-Match",Revision};
    if(Json)Headers[Count++]=(XS_FetchHeader){"Content-Type","application/json"};
    else if(ArchiveRoute&&Binary)Headers[Count++]=(XS_FetchHeader){"Content-Type","application/octet-stream"};
    MdoRemoteHttpRequest Request={0};Request.Method=Route->Method;Request.Target=Target;Request.Headers=Headers;Request.HeaderCount=Count;
    Request.Body=(xbytesview){Binary?Binary:(const unsigned char*)Json,Binary?BinarySize:Json?strlen(Json):0};
    MdoApplicationInvocation* I=C->Opaque;SdkResponse Reply={0};MdoRemoteHttpSink Sink={SdkResponseHead,SdkResponseBody,&Reply};
    xvalue* NativeHeaders=Fields?xrtValueClone(Fields):xrtValueObject();if(Revision)SdkPut(NativeHeaders,"If-Match",Revision);
    if(ArchiveRoute&&Binary)SdkPut(NativeHeaders,"Content-Type","application/octet-stream");
    MdoApplicationResponse Captured={0};
    bool Native=Ok&&MdoApiApplicationInvoke(C,Route->Method,Target,Body,NativeHeaders,
        (xbytesview){Binary,BinarySize},&Captured,E);
    xrtValueRelease(NativeHeaders);
    if(Native){Reply.Status=Captured.Status;Reply.Body=Captured.Body;Reply.Bytes=Captured.Bytes;Reply.Type=Captured.ContentType;Reply.Revision=Captured.Revision;}
    else if(!E||!E->Code[0]){
        Ok=Ok&&(!Body||Json)&&MdoRemoteLoopbackCall(SdkServer,&Request,I?I->Cancel:NULL,&Sink);
    }else Ok=false;
    if(Json)xrtSecureZero(Json,strlen(Json));
    xrtFree(Json);
    if(Binary)xrtSecureZero(Binary,BinarySize);
    xrtFree(Binary);
    if(!Ok){
        xrtFree(Reply.Body);xrtFree(Reply.Type);xrtFree(Reply.Revision);
        xrtFree(Captured.Body);xrtFree(Captured.ContentType);xrtFree(Captured.Revision);
        if(E&&E->Code[0])return false;
        return MdoApplicationError(E,"sdk_transport_failed","Service transport failed; inspect the operation receipt before repeating a mutation","transport",503,strcmp(Route->Method,"GET")!=0);
    }
    bool RawArchive=ArchiveRoute&&strstr(Route->Path,"/file/")!=NULL;bool Success=Reply.Status>=200&&Reply.Status<300;
    if(RawArchive&&Success){uint64 Offset=0;bool Valid=ResponseFile&&xrtPathIsRooted(ResponseFile)&&Reply.Bytes<=1048576;
        if(SdkGet(P,"response_offset")&&!SdkUInt(SdkGet(P,"response_offset"),&Offset))Valid=false;
        Valid=Valid&&Offset<=UINT64_C(512)*1024*1024&&Reply.Bytes<=UINT64_C(512)*1024*1024-Offset;
        xfile File=Valid?xrtOpen(ResponseFile,XFILE_WRITE|XFILE_CREATE|XFILE_NOFOLLOW|(Offset?0:XFILE_TRUNCATE)):NULL;uint64 Current=0;Valid=Valid&&File&&xrtFileSize(File,&Current)&&Offset<=Current&&xrtWriteAtFull(File,Offset,Reply.Body,Reply.Bytes,NULL)&&xrtFlush(File);xrtClose(File);
        if(Valid){*Out=xrtValueObject();xvalue* Info=xrtValueObject();SdkPut(Info,"path",ResponseFile);SdkPutUInt(Info,"bytes",Reply.Bytes);SdkPutUInt(Info,"offset",Offset);xrtValueObjectSetNew(*Out,XRT_STR_LITERAL("data"),Info);SdkPutUInt(*Out,"status",Reply.Status);SdkPut(*Out,"revision",Reply.Revision);SdkPut(*Out,"content_type",Reply.Type);SdkPut(*Out,"transport",Native?"native":"http-stream");}
        if(Reply.Body)xrtSecureZero(Reply.Body,Reply.Bytes);
        xrtFree(Reply.Body);
        xrtFree(Reply.Type);
        xrtFree(Reply.Revision);
        return Valid||MdoApplicationError(E,"sdk_archive_destination_failed","Raw archive range could not be written to response_file; inspect it before retrying","filesystem",500,true);
    }
    xvalue* Parsed=Reply.Body?xrtJsonParse(xrtStrViewN(Reply.Body,Reply.Bytes)):NULL;
    *Out=xrtValueObject();
    SdkPutUInt(*Out,"status",Reply.Status);SdkPut(*Out,"revision",Reply.Revision);SdkPut(*Out,"content_type",Reply.Type);
    SdkPut(*Out,"transport",Native?"native":"http-stream");
    if(Parsed){const xvalue* Data=SdkGet(Parsed,"data");xrtValueObjectSetNew(*Out,XRT_STR_LITERAL("data"),xrtValueClone(Data?Data:Parsed));}
    else {size_t N=0;bool EncodedOk=xrtBase64Encode(Reply.Body,Reply.Bytes,NULL,0,&N,NULL);char* Encoded=EncodedOk?xrtMalloc(N+1):NULL;
        if(Encoded&&xrtBase64Encode(Reply.Body,Reply.Bytes,Encoded,N+1,&N,NULL)){Encoded[N]=0;SdkPut(*Out,"base64",Encoded);}else Success=false;
        xrtFree(Encoded);}
    if(!Success){const xvalue* Error=SdkGet(Parsed,"error");MdoApplicationError(E,SdkField(Error,"code"),SdkField(Error,"message"),"service",Reply.Status,false);}
    xrtValueRelease(Parsed);if(Reply.Body)xrtSecureZero(Reply.Body,Reply.Bytes);xrtFree(Reply.Body);xrtFree(Reply.Type);xrtFree(Reply.Revision);
    return Success;
}
