#include "../../include/mdo/application.h"
#include "../remote/loopback.h"
#include "../api/write_admission.h"
#include "../../include/mdo/home.h"
#include "internal.h"
static XS_ServerInfo* SdkServer;
static xmutex* SdkEventLock;
static xvalue* SdkEventRing[256];
static uint64 SdkEventSequence;
static char SdkEventEpoch[33];
XS_ServerInfo* MdoApplicationServer(void){return SdkServer;}
bool MdoApplicationInit(XS_ServerInfo* Server){
    if(SdkServer)return true;
    SdkEventLock=xrtMutexCreate();SdkServer=xsServerRetain(Server);
    unsigned char Epoch[16];if(!xrtSecureRandom(Epoch,sizeof(Epoch))){MdoApplicationUnit();return false;}
    for(size_t i=0;i<16;i++)snprintf(SdkEventEpoch+i*2,3,"%02x",Epoch[i]);
    if(!SdkEventLock||!SdkServer||!MdoApplicationStreamsInit()||!MdoApplicationJobsInit()){MdoApplicationUnit();return false;}return true;
}
void MdoApplicationUnit(void){
    MdoApplicationJobsUnit();
    MdoApplicationStreamsUnit();
    for(size_t i=0;i<256;i++){xrtValueRelease(SdkEventRing[i]);SdkEventRing[i]=NULL;}
    if(SdkEventLock)xrtMutexDestroy(SdkEventLock);
    SdkEventLock=NULL;
    xsServerRelease(SdkServer);SdkServer=NULL;SdkEventSequence=0;
}
void MdoApplicationEvent(cstr Topic,const mdo_sdk_context_v1* C,const xvalue* Data){
    if(!SdkEventLock||!Topic)return;
    xvalue* V=xrtValueObject();if(!V)return;
    SdkPut(V,"topic",Topic);SdkPut(V,"extension",C?C->ExtensionId:"");SdkPut(V,"invocation",C?C->InvocationId:"");
    SdkPut(V,"project",C?C->ProjectId:"");SdkPut(V,"session",C?C->SessionId:"");
    xrtValueObjectSetNew(V,XRT_STR_LITERAL("data"),Data?xrtValueClone(Data):xrtValueNull());
    xrtMutexLock(SdkEventLock);uint64 N=++SdkEventSequence;SdkPutUInt(V,"sequence",N);SdkPutUInt(V,"time_us",SdkClock());
    size_t I=(size_t)(N%256u);xrtValueRelease(SdkEventRing[I]);SdkEventRing[I]=V;xrtMutexUnlock(SdkEventLock);
}
xvalue* MdoApplicationEvents(const xvalue* P,mdo_error_v1* E){
    if(!SdkEventLock){MdoApplicationError(E,"sdk_unavailable","Event journal is unavailable","events",503,false);return NULL;}
    uint64 After=0;const xvalue* A=SdkGet(P,"after");if(A&&!SdkUInt(A,&After)){MdoApplicationError(E,"sdk_arguments_invalid","after must be an unsigned sequence","events",400,false);return NULL;}
    cstr Topic=SdkField(P,"topic");xvalue* R=xrtValueObject(),*Items=xrtValueArray();
    if(!R||!Items){xrtValueRelease(R);xrtValueRelease(Items);return NULL;}
    xrtMutexLock(SdkEventLock);uint64 Last=SdkEventSequence,First=Last>=256?Last-255:1;
    for(uint64 N=First;N<=Last;N++)if(N>After){const xvalue* V=SdkEventRing[N%256];
        if(V&&(!Topic||!strcmp(Topic,SdkField(V,"topic"))))xrtValueArrayAppend(Items,V);}
    SdkPutUInt(R,"cursor",Last);SdkPutUInt(R,"oldest",First);SdkPut(R,"epoch",SdkEventEpoch);
    cstr Epoch=SdkField(P,"epoch");
    xrtValueObjectSetNew(R,XRT_STR_LITERAL("resync_required"),xrtValueBool((Epoch&&strcmp(Epoch,SdkEventEpoch))||After>Last||(After&&First>After&&First-After>1)));
    xrtMutexUnlock(SdkEventLock);xrtValueObjectSetNew(R,XRT_STR_LITERAL("items"),Items);return R;
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
    MdoRemoteHttpRequest Request={0};Request.Method=Route->Method;Request.Target=Target;Request.Headers=Headers;Request.HeaderCount=Count;
    Request.Body=(xbytesview){Binary?Binary:(const unsigned char*)Json,Binary?BinarySize:Json?strlen(Json):0};
    MdoApplicationInvocation* I=C->Opaque;SdkResponse Reply={0};MdoRemoteHttpSink Sink={SdkResponseHead,SdkResponseBody,&Reply};
    xvalue* NativeHeaders=Fields?xrtValueClone(Fields):xrtValueObject();if(Revision)SdkPut(NativeHeaders,"If-Match",Revision);
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
    xvalue* Parsed=Reply.Body?xrtJsonParse(xrtStrViewN(Reply.Body,Reply.Bytes)):NULL;
    *Out=xrtValueObject();bool Success=Reply.Status>=200&&Reply.Status<300;
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
