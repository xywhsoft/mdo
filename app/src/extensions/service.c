#include "../../include/mdo/extension_service.h"
#include "../../include/mdo/extension_manifest.h"
#include "../../include/mdo/extension_tasks.h"
#include "../../include/mdo/extensions.h"
#include "../../include/mdo/account.h"
#include "../../include/mdo/distribution.h"
#include "tasks_internal.h"

#define MDO_EXT_SERVICE "/api/v1/mdo/extensions"
static bool MdoExtServiceError(char Error[512],cstr Message){if(Error)snprintf(Error,512,"%s",Message);return false;}
static bool MdoExtServiceId(cstr Id){return Id&&strlen(Id)==32&&MdoExtensionDirectoryId(Id);}
static bool MdoExtServicePut(xvalue* V,cstr K,cstr Text){return Text&&xrtValueObjectSetNew(V,xrtStrView(K),xrtValueString(xrtStrView(Text)));}
static bool MdoExtServiceNumber(const xvalue* V,cstr Key,uint64* N){int64 I=0;const xvalue* Value=xrtValueObjectGet(V,xrtStrView(Key));if(xrtValueGetUInt(Value,N))return true;if(xrtValueGetInt(Value,&I)&&I>=0){*N=(uint64)I;return true;}return false;}
static bool MdoExtServiceLease(xcancel* Cancel,bool Required,MdoAccountLease* Lease,char Error[512]){
 memset(Lease,0,sizeof(*Lease));if(MdoAccountAcquireServiceWait(Cancel,30000,Lease))return true;return !Required||MdoExtServiceError(Error,"Sign in to the account before publishing or managing its extensions");
}
#define MDO_EXT_INSTALL_RETRIES 5u
/* Retry only known transient transport failures. Unknown errors, TLS trust,
 * malformed HTTP and resource/permission failures require explicit action. */
static bool MdoExtServiceTransientTransport(const xerror* Cause){
 cstr RootDomain=Cause?xrtErrorDomain(Cause):NULL;
 bool Incomplete=RootDomain&&!strcmp(RootDomain,"xs.fetch")&&xrtErrorCode(Cause)==XS_FETCH_ERROR_INCOMPLETE,Transient=Incomplete,Deadline=false;
 for(size_t Depth=0;Cause&&Depth<16;Depth++,Cause=xrtErrorCause(Cause)){
  xerrkind Kind=xrtErrorKind(Cause);cstr Domain=xrtErrorDomain(Cause);int32 Code=xrtErrorCode(Cause);
  if(Kind==XERR_TIMEOUT)Deadline=true;
  if(Domain&&!strcmp(Domain,"xrt.tls")&&(Code==XTLS_ERROR_VERIFY||Code==XTLS_ERROR_CERTIFICATE||Code==XTLS_ERROR_IDENTITY))return false;
  /* A total deadline cancels the HTTP client's private TLS/TCP operations.
   * Those nested cancellations are not a user cancellation. The request's
   * actual cancel token is checked separately before allowing any retry. */
  if(Kind==XERR_CANCELLED){if(!Deadline)return false;continue;}
  if(Kind==XERR_ARGUMENT||Kind==XERR_TYPE||Kind==XERR_VALUE||Kind==XERR_RANGE||Kind==XERR_MEMORY||Kind==XERR_PERMISSION||Kind==XERR_UNSUPPORTED||Kind==XERR_NOT_FOUND||Kind==XERR_EXISTS)return false;
  if(Kind==XERR_PROTOCOL&&(!Incomplete||(Domain&&!strcmp(Domain,"xrt.tls"))))return false;
  if(Kind==XERR_IO||Kind==XERR_AGAIN||Kind==XERR_TIMEOUT||Kind==XERR_CLOSED)Transient=true;
 }
 return !Cause&&Transient;
}
static bool MdoExtServiceWaitRetry(unsigned Retry,xcancel* Cancel,char Error[512]){
 unsigned Delay=250u<<((Retry<4)?Retry:4);
 for(unsigned Wait=0;Wait<Delay;Wait+=25){if((Cancel&&xrtCancelRequested(Cancel))||xrtThreadStopping())return MdoExtServiceError(Error,"Extension installation cancelled during automatic retry");xrtSleep(25);}
 return ((!Cancel||!xrtCancelRequested(Cancel))&&!xrtThreadStopping())||MdoExtServiceError(Error,"Extension installation cancelled during automatic retry");
}
static bool MdoExtServiceFetchAttempt(cstr Path,cstr Method,const void* Body,size_t Bytes,bool Binary,const MdoAccountLease* Lease,xcancel* Cancel,XS_FetchResponse* Response,bool* Retryable,char Error[512]){
 if(Retryable)*Retryable=false;
 if(Error)Error[0]=0;
 char* Url=xrtFormat(MDO_ACCOUNT_SERVICE_ORIGIN "%s",Path);char Authorization[4200]={0};XS_FetchHeader Headers[3]={{"Accept",Binary&&!strcmp(Method,"GET")?"application/zip":"application/json"},{"Content-Type",Binary?"application/octet-stream":"application/json"}};size_t Count=2;
 bool Ok=Url&&Path&&!strncmp(Path,MDO_EXT_SERVICE,sizeof(MDO_EXT_SERVICE)-1)&&Bytes<=1048576&&(!Cancel||!xrtCancelRequested(Cancel));
 if(Ok&&Lease&&Lease->Managed){Ok=Lease->AccessToken&&strlen(Lease->AccessToken)<=4096;if(Ok){snprintf(Authorization,sizeof(Authorization),"Bearer %s",Lease->AccessToken);Headers[Count++]=(XS_FetchHeader){"Authorization",Authorization};}}
 bool ArchiveRead=Binary&&!strcmp(Method,"GET");
 XS_FetchRequest Request={0};Request.Size=sizeof(Request);Request.Version=XS_FETCH_REQUEST_VERSION;Request.Url=Url;Request.Method=Method;Request.Headers=Headers;Request.HeaderCount=Count;Request.Body=Body;Request.BodySize=Bytes;Request.Timeout=ArchiveRead?120000000:30000000;Request.IdleTimeout=ArchiveRead?30000000:10000000;Request.MaxBodyBytes=Binary?1048576:8u*1024u*1024u;Request.Cancel=Lease&&Lease->Managed?Lease->Cancel:Cancel;Response->Size=sizeof(*Response);
 bool Read=Ok&&!strcmp(Method,"GET");if(Ok){xrtClearError();Ok=xsFetch(&Request,Response);}
 bool Cancelled=Request.Cancel&&xrtCancelRequested(Request.Cancel);
 if(Retryable&&Read&&!Cancelled&&!Response->Redirects)*Retryable=!Ok?MdoExtServiceTransientTransport(xrtGetError()):Response->Status==408||Response->Status==425||Response->Status==500||Response->Status==502||Response->Status==503||Response->Status==504;
 xrtSecureZero(Authorization,sizeof(Authorization));xrtFree(Url);
 if(!Ok){if(Cancelled)return MdoExtServiceError(Error,"Extension transfer cancelled or its account session changed");const xerror* Cause=xrtGetError();if(Cause&&xrtErrorCause(Cause)&&xrtErrorDomain(Cause)&&!strcmp(xrtErrorDomain(Cause),"xs.fetch"))Cause=xrtErrorCause(Cause);if(Error&&Cause)snprintf(Error,512,"The extension service connection failed: %.400s",xrtErrorMessage(Cause));else MdoExtServiceError(Error,"The extension service connection could not complete");return false;}
 if(Response->Status==401&&Lease&&Lease->Managed)(void)MdoAccountRejectAccess(Lease);
 if(Response->Redirects||Response->Status<200||Response->Status>=300){char Message[512];snprintf(Message,sizeof(Message),"The extension service rejected this request (HTTP %u)",(unsigned)Response->Status);xvalue* Envelope=Response->Body&&Response->BodySize<=65536?xrtJsonParse(xrtStrViewN((cstr)Response->Body,Response->BodySize)):NULL;cstr Text=MdoExtensionText(Envelope,"message",450);if(Text&&Text[0])snprintf(Message,sizeof(Message),"HTTP %u: %s",(unsigned)Response->Status,Text);xrtValueRelease(Envelope);return MdoExtServiceError(Error,Message);}
 return true;
}
static bool MdoExtServiceFetch(cstr Path,cstr Method,const void* Body,size_t Bytes,bool Binary,const MdoAccountLease* Lease,xcancel* Cancel,XS_FetchResponse* Response,char Error[512]){
 for(unsigned Retry=0;;Retry++){
  bool Transient=false;if(MdoExtServiceFetchAttempt(Path,Method,Body,Bytes,Binary,Lease,Cancel,Response,&Transient,Error))return true;
  if(!Transient||Retry==MDO_EXT_INSTALL_RETRIES)return false;
  xsFetchResponseUnit(Response);Response->Size=sizeof(*Response);
  if(!MdoExtServiceWaitRetry(Retry,Lease&&Lease->Managed?Lease->Cancel:Cancel,Error))return false;
 }
}
/* One download/group shares a retry budget. Keep the current archive and
 * verified offset while retrying a read instead of replaying its prefix. */
static bool MdoExtServiceRetryRead(bool Transient,unsigned* Retries,xcancel* Cancel,char Error[512]){
 if(!Transient||!Retries||*Retries>=MDO_EXT_INSTALL_RETRIES)return false;
 return MdoExtServiceWaitRetry((*Retries)++,Cancel,Error);
}
static bool MdoExtServiceFetchBudget(cstr Path,const MdoAccountLease* Lease,xcancel* Cancel,XS_FetchResponse* Response,unsigned* Retries,char Error[512]){
 for(;;){bool Transient=false;if(MdoExtServiceFetchAttempt(Path,"GET",NULL,0,true,Lease,Cancel,Response,&Transient,Error))return true;
  xsFetchResponseUnit(Response);if(!MdoExtServiceRetryRead(Transient,Retries,Lease&&Lease->Managed?Lease->Cancel:Cancel,Error))return false;
 }
}
static xvalue* MdoExtServiceDecode(const XS_FetchResponse* Response,char Error[512]){
 xjsonreadconfig Config;xrtJsonReadConfigInit(&Config);Config.MaxInputBytes=8u*1024u*1024u;Config.MaxStringBytes=1048576;Config.MaxValues=200000;Config.MaxDepth=20;
 xvalue* Envelope=xrtJsonRead(xrtStrViewN((cstr)Response->Body,Response->BodySize),&Config);int64 Code=-1;bool Ok=Envelope&&xrtValueGetInt(xrtValueObjectGet(Envelope,XRT_STR_LITERAL("code")),&Code)&&Code==0;
 xvalue* Data=Ok?xrtValueClone(xrtValueObjectGet(Envelope,XRT_STR_LITERAL("data"))):NULL;if(!Data&&Error&&!Error[0]){cstr Message=MdoExtensionText(Envelope,"message",500);MdoExtServiceError(Error,Message?Message:"The extension service returned invalid metadata");}xrtValueRelease(Envelope);return Data;
}
static xvalue* MdoExtServiceJsonAttempt(cstr Path,cstr Method,const xvalue* Body,const MdoAccountLease* Lease,xcancel* Cancel,bool* Retryable,char Error[512]){
 size_t Bytes=0;char* Text=Body?xrtJsonStringify(Body,false,&Bytes):NULL;XS_FetchResponse Response={0};bool Ok=(!Body||Text)&&Bytes<=16384&&(Retryable?MdoExtServiceFetchAttempt(Path,Method,Text,Bytes,false,Lease,Cancel,&Response,Retryable,Error):MdoExtServiceFetch(Path,Method,Text,Bytes,false,Lease,Cancel,&Response,Error));xrtFree(Text);
 xvalue* Data=Ok?MdoExtServiceDecode(&Response,Error):NULL;xsFetchResponseUnit(&Response);return Data;
}
static xvalue* MdoExtServiceJson(cstr Path,cstr Method,const xvalue* Body,const MdoAccountLease* Lease,xcancel* Cancel,char Error[512]){return MdoExtServiceJsonAttempt(Path,Method,Body,Lease,Cancel,NULL,Error);}
static char* MdoExtServiceQuery(cstr Base,const xvalue* Input){
 char* Path=xrtStrDup(Base);const cstr TextFields[]={"kind","platform","q","state","work","path","ref"};const size_t Limits[]={32,32,240,16,32,4096,MDO_EXTENSION_REF_BYTES};
 for(size_t I=0;Path&&I<7;I++){const xvalue* Field=xrtValueObjectGet(Input,xrtStrView(TextFields[I]));if(!Field)continue;cstr Text=MdoExtensionText(Input,TextFields[I],Limits[I]);if(!Text){xrtFree(Path);return NULL;}char* Encoded=xrtPercentEncodeNew(Text,strlen(Text),XRT_STR_LITERAL(""),NULL),*Next=Encoded?xrtFormat("%s%c%s=%s",Path,strchr(Path,'?')?'&':'?',TextFields[I],Encoded):NULL;xrtFree(Encoded);xrtFree(Path);Path=Next;}
 const cstr Numbers[]={"offset","limit"};for(size_t I=0;Path&&I<2;I++){if(!xrtValueObjectHas(Input,xrtStrView(Numbers[I])))continue;uint64 N=0;if(!MdoExtServiceNumber(Input,Numbers[I],&N)||N>(I?200:100000)){xrtFree(Path);return NULL;}char* Next=xrtFormat("%s%c%s=%llu",Path,strchr(Path,'?')?'&':'?',Numbers[I],(unsigned long long)N);xrtFree(Path);Path=Next;}return Path;
}
static bool MdoExtServiceReadAttempt(const xvalue* Input,xcancel* Cancel,xvalue** Result,bool* Retryable,char Error[512]){
 *Result=NULL;if(Error)Error[0]=0;cstr Action=MdoExtensionText(Input,"action",24),Kind=MdoExtensionText(Input,"kind",32),Version=MdoExtensionText(Input,"version_id",32);if(!Action||(Kind&&Kind[0]&&!MdoExtensionDirectoryKind(Kind)))return MdoExtServiceError(Error,"Choose an extension service action and type");
 bool Required=!strcmp(Action,"mine")||!strcmp(Action,"profile")||!strcmp(Action,"requests");char* Base=NULL;
 if(!strcmp(Action,"catalog"))Base=xrtStrDup(MDO_EXT_SERVICE "/catalog");
 else if(!strcmp(Action,"resolve")&&MdoExtensionRefValid(MdoExtensionText(Input,"ref",MDO_EXTENSION_REF_BYTES)))Base=xrtStrDup(MDO_EXT_SERVICE "/resolve");
 else if(!strcmp(Action,"plan")&&MdoExtServiceId(Version))Base=xrtFormat(MDO_EXT_SERVICE "/plan?version=%s",Version);
 else if(!strcmp(Action,"mine")||!strcmp(Action,"profile")||!strcmp(Action,"requests"))Base=xrtFormat(MDO_EXT_SERVICE "/creator?scope=%s",!strcmp(Action,"mine")?"works":Action);
 else if((!strcmp(Action,"detail")||!strcmp(Action,"files")||!strcmp(Action,"preview"))&&MdoExtServiceId(Version))Base=xrtFormat(MDO_EXT_SERVICE "/%s?version=%s",Action,Version);
 if(!Base)return MdoExtServiceError(Error,"Unsupported extension read; no arbitrary service URL is accepted");
 char* Path=MdoExtServiceQuery(Base,Input);
 xrtFree(Base);
 if(Path&&(!strcmp(Action,"catalog")||!strcmp(Action,"plan")||!strcmp(Action,"resolve"))&&!xrtValueObjectHas(Input,XRT_STR_LITERAL("platform"))){char* Filtered=xrtFormat("%s%cplatform=%s",Path,strchr(Path,'?')?'&':'?',MdoExtensionRuntimePlatform());xrtFree(Path);Path=Filtered;}
 MdoAccountLease Lease={0};bool Ok=Path&&MdoExtServiceLease(Cancel,Required,&Lease,Error);if(Ok)*Result=MdoExtServiceJsonAttempt(Path,"GET",NULL,&Lease,Cancel,Retryable,Error);MdoAccountRelease(&Lease);xrtFree(Path);return *Result!=NULL;
}
bool MdoExtensionServiceRead(const xvalue* Input,xcancel* Cancel,xvalue** Result,char Error[512]){return MdoExtServiceReadAttempt(Input,Cancel,Result,NULL,Error);}
static bool MdoExtServiceReadBudget(const xvalue* Input,xcancel* Cancel,xvalue** Result,unsigned* Retries,char Error[512]){
 for(;;){bool Transient=false;if(MdoExtServiceReadAttempt(Input,Cancel,Result,&Transient,Error))return true;
  xrtValueRelease(*Result);*Result=NULL;if(!MdoExtServiceRetryRead(Transient,Retries,Cancel,Error))return false;
 }
}
static xvalue* MdoExtServiceOrigin(const xvalue* Entry){
 const cstr Names[]={"id","kind","xid","version_id","sha256","bytes","owner","submitter","author","copyright","license","name","alias","platforms","state","version","serial","sort_order","tags"};xvalue* Origin=xrtValueObject();bool Ok=Origin!=NULL;
 for(size_t I=0;Ok&&I<sizeof(Names)/sizeof(*Names);I++){const xvalue* Value=xrtValueObjectGet(Entry,xrtStrView(Names[I]));if(Value)Ok=xrtValueObjectSet(Origin,xrtStrView(Names[I]),Value);}if(!Ok){xrtValueRelease(Origin);return NULL;}return Origin;
}
static bool MdoExtServiceDownloadAttempt(cstr Kind,cstr Version,xcancel* Cancel,xvalue** Result,unsigned* Retries,char Error[512]){
 *Result=NULL;if(Error)Error[0]=0;if(!MdoExtensionDirectoryKind(Kind)||!MdoExtServiceId(Version))return MdoExtServiceError(Error,"Choose a published extension version");
#if defined(__ANDROID__)
 if(!strcmp(Kind,"external-tools"))return MdoExtServiceError(Error,"Android external programs are installed and updated with the full APK");
#endif
 xvalue* Query=xrtValueObject(),*Entry=NULL,*Task=NULL,*Origin=NULL;bool Ok=Query&&MdoExtServicePut(Query,"action","detail")&&MdoExtServicePut(Query,"version_id",Version)&&MdoExtServiceReadBudget(Query,Cancel,&Entry,Retries,Error);xrtValueRelease(Query);
 cstr ActualKind=MdoExtensionText(Entry,"kind",32),State=MdoExtensionText(Entry,"state",16),Hash=MdoExtensionText(Entry,"sha256",64);uint64 Bytes=0;
 cstr ActualVersion=MdoExtensionText(Entry,"version_id",32);Ok=Ok&&ActualKind&&!strcmp(Kind,ActualKind)&&ActualVersion&&!strcmp(Version,ActualVersion)&&State&&!strcmp(State,"published")&&MdoExtServiceNumber(Entry,"bytes",&Bytes)&&Bytes&&Bytes<=MDO_EXTENSION_ARCHIVE_BYTES;
 xvalue* Input=xrtValueObject();Ok=Ok&&Input&&MdoExtServicePut(Input,"kind",Kind)&&MdoExtServicePut(Input,"purpose","install")&&MdoExtServicePut(Input,"sha256",Hash)&&xrtValueObjectSetNew(Input,XRT_STR_LITERAL("bytes"),xrtValueUInt(Bytes))&&MdoExtensionTaskBeginWait(Input,Cancel,&Task,Error);xrtValueRelease(Input);
 char Id[33]={0};if(Ok)snprintf(Id,sizeof(Id),"%s",MdoExtensionText(Task,"id",32));MdoExtensionTaskUse* Use=Ok?MdoExtensionTaskUseBegin(Id,true,Cancel,Error):NULL;MdoAccountLease Lease={0};Origin=Ok?MdoExtServiceOrigin(Entry):NULL;
 Ok=Ok&&Use&&Origin&&MdoExtensionTaskOrigin(Use,Origin,Error)&&MdoExtServiceLease(MdoExtensionTaskUseCancel(Use),false,&Lease,Error);
 for(uint64 Offset=0;Ok&&Offset<Bytes;){char* Path=xrtFormat(MDO_EXT_SERVICE "/archive?dependency_protocol=1&version=%s&offset=%llu",Version,(unsigned long long)Offset);XS_FetchResponse Response={0};uint64 Remaining=Bytes-Offset;size_t Want=(size_t)(Remaining<MDO_EXTENSION_TASK_CHUNK?Remaining:MDO_EXTENSION_TASK_CHUNK);
  Ok=Path&&MdoExtServiceFetchBudget(Path,&Lease,MdoExtensionTaskUseCancel(Use),&Response,Retries,Error);
  if(Ok&&(Response.BodySize!=Want||!Response.ContentType||strncmp(Response.ContentType,"application/zip",15))){snprintf(Error,512,"Store archive chunk mismatch: expected %zu application/zip bytes, received %zu bytes (HTTP %u, %.80s)",Want,Response.BodySize,(unsigned)Response.Status,Response.ContentType?Response.ContentType:"no Content-Type");Ok=false;}
  xvalue* Progress=NULL;if(Ok)Ok=MdoExtensionTaskUseWrite(Use,Offset,Response.Body,Response.BodySize,&Progress,Error);xrtValueRelease(Progress);xsFetchResponseUnit(&Response);xrtFree(Path);Offset+=Want;
 }
 MdoAccountRelease(&Lease);MdoExtensionTaskUseEnd(Use);if(Ok)Ok=MdoExtensionTaskSeal(Id,Cancel,Result,Error);
 if(!Ok&&Id[0]){xvalue* Receipt=NULL;char Ignored[512];if(Cancel&&xrtCancelRequested(Cancel))(void)MdoExtensionTaskCancel(Id,&Receipt,Ignored);else (void)MdoExtensionTaskFail(Id,Error&&Error[0]?Error:"Store archive download or validation failed",&Receipt,Ignored);xrtValueRelease(Receipt);}
 xrtValueRelease(Entry);xrtValueRelease(Task);xrtValueRelease(Origin);if(!Ok&&Error&&!Error[0])MdoExtServiceError(Error,"The store archive changed or failed its byte/hash/ZIP validation");return Ok;
}
bool MdoExtensionServiceDownload(cstr Kind,cstr Version,xcancel* Cancel,xvalue** Result,char Error[512]){
 unsigned Retries=0;return MdoExtServiceDownloadAttempt(Kind,Version,Cancel,Result,&Retries,Error);
}
bool MdoExtensionServiceInstall(cstr Id,const xvalue* Input,xcancel* Cancel,xvalue** Result,char Error[512]){
 *Result=NULL;if(Error)Error[0]=0;MdoExtensionTaskUse* Use=MdoExtensionTaskUseBegin(Id,false,Cancel,Error);xvalue* Task=Use?MdoExtensionTaskRead(Id,Error):NULL;const xvalue* Origin=xrtValueObjectGet(Task,XRT_STR_LITERAL("store"));cstr Kind=MdoExtensionText(Origin,"kind",32),Target=MdoExtensionText(Origin,"xid",32);char* Archive=Use?MdoExtensionTaskArchive(Id,Error):NULL;
 xvalue* Request=Input?xrtValueClone(Input):xrtValueObject(),*Credits=Origin?xrtValueClone(Origin):NULL;bool Ok=Use&&Task&&Credits&&Request&&Archive&&MdoExtensionDirectoryKind(Kind)&&MdoExtServiceId(Target)&&MdoExtServicePut(Request,"action","install")&&MdoExtServicePut(Request,"kind",Kind)&&MdoExtServicePut(Request,"id",Target)&&MdoExtServicePut(Request,"archive",Archive)&&MdoExtServicePut(Credits,"source","store")&&MdoExtensionsManage(Request,Credits,MdoExtensionTaskUseCancel(Use),Result,Error);
 if(Ok){xvalue* Complete=NULL;char ReceiptError[512]={0};if(!MdoExtensionTaskComplete(Id,*Result,&Complete,ReceiptError)&&*Result)MdoExtServicePut(*Result,"transfer_receipt_error",ReceiptError);xrtValueRelease(Complete);}
 xrtValueRelease(Task);xrtValueRelease(Request);xrtValueRelease(Credits);xrtFree(Archive);MdoExtensionTaskUseEnd(Use);if(!Ok&&Error&&!Error[0])MdoExtServiceError(Error,"Only an owned, sealed store download can be installed as managed content");return Ok;
}

/* Only small control metadata is JSON. Archive/review data stays in owned
 * task files and is read through a single bounded buffer. The upload key and
 * submit nonce survive lost replies, so explicit retries never create copies. */
static bool MdoExtServiceUpload(cstr Id,MdoExtensionTaskUse* Use,const MdoAccountLease* Lease,char Error[512]){
 xvalue* Task=MdoExtensionTaskRead(Id,Error);cstr Purpose=MdoExtensionText(Task,"purpose",16),Kind=MdoExtensionText(Task,"kind",32),Hash=MdoExtensionText(Task,"sha256",64);uint64 Bytes=0;
 char* Archive=Use?MdoExtensionTaskArchive(Id,Error):NULL;xfile F=Archive?MdoHomeOpenRead(Archive):NULL;xrtFree(Archive);bool Review=Purpose&&!strcmp(Purpose,"review");
 bool Ok=Task&&F&&!xrtValueObjectHas(Task,XRT_STR_LITERAL("store"))&&Purpose&&(Review||!strcmp(Purpose,"publish"))&&MdoExtServiceNumber(Task,"bytes",&Bytes);
 xvalue* Begin=xrtValueObject(),*Remote=NULL;Ok=Ok&&Begin&&MdoExtServicePut(Begin,"action","begin")&&MdoExtServicePut(Begin,"request_id",Id)&&MdoExtServicePut(Begin,"purpose",Review?"review":"archive")&&MdoExtServicePut(Begin,"kind",Kind?Kind:"")&&MdoExtServicePut(Begin,"sha256",Hash)&&xrtValueObjectSetNew(Begin,XRT_STR_LITERAL("bytes"),xrtValueUInt(Bytes));
 if(Ok)Remote=MdoExtServiceJson(MDO_EXT_SERVICE "/uploads","POST",Begin,Lease,MdoExtensionTaskUseCancel(Use),Error);
 xrtValueRelease(Begin);
 cstr State=MdoExtensionText(Remote,"state",16);bool Finished=State&&(!strcmp(State,"sealed")||!strcmp(State,"consumed"));Ok=Ok&&Remote&&State&&!strcmp(MdoExtensionText(Remote,"xid",32)?MdoExtensionText(Remote,"xid",32):"",Id)&&(Finished||!strcmp(State,"receiving"));xrtValueRelease(Remote);Remote=NULL;
 uint64 Offset=0;if(Ok&&!Finished){char* Path=xrtFormat(MDO_EXT_SERVICE "/uploads?id=%s",Id);Remote=Path?MdoExtServiceJson(Path,"GET",NULL,Lease,MdoExtensionTaskUseCancel(Use),Error):NULL;xrtFree(Path);Ok=Remote&&MdoExtServiceNumber(Remote,"received",&Offset)&&Offset<=Bytes;xrtValueRelease(Remote);Remote=NULL;}
 uint8* Buffer=Ok&&!Finished?xrtMalloc(MDO_EXTENSION_TASK_CHUNK):NULL;if(Ok&&!Finished&&!Buffer)Ok=false;
 while(Ok&&!Finished&&Offset<Bytes){size_t N=(size_t)(Bytes-Offset<MDO_EXTENSION_TASK_CHUNK?Bytes-Offset:MDO_EXTENSION_TASK_CHUNK);char* Path=xrtFormat(MDO_EXT_SERVICE "/uploads?id=%s&offset=%llu",Id,(unsigned long long)Offset);XS_FetchResponse Response={0};
  Ok=Path&&MdoExtensionTaskTransfer(Use,Offset,"uploading",Error)&&xrtReadAtFull(F,Offset,Buffer,N,NULL)&&MdoExtServiceFetch(Path,"PUT",Buffer,N,true,Lease,MdoExtensionTaskUseCancel(Use),&Response,Error);
  Remote=Ok?MdoExtServiceDecode(&Response,Error):NULL;uint64 Received=0;Ok=Ok&&Remote&&MdoExtServiceNumber(Remote,"received",&Received)&&Received==Offset+N;xrtValueRelease(Remote);Remote=NULL;xsFetchResponseUnit(&Response);xrtFree(Path);Offset+=N;
 }
 xrtFree(Buffer);xrtClose(F);
 if(Ok&&!Finished){xvalue* Seal=xrtValueObject();Ok=Seal&&MdoExtServicePut(Seal,"action","seal")&&MdoExtServicePut(Seal,"id",Id);if(Ok)Remote=MdoExtServiceJson(MDO_EXT_SERVICE "/uploads","POST",Seal,Lease,MdoExtensionTaskUseCancel(Use),Error);xrtValueRelease(Seal);State=MdoExtensionText(Remote,"state",16);Ok=Ok&&State&&!strcmp(State,"sealed");xrtValueRelease(Remote);}
 if(Ok)Ok=MdoExtensionTaskTransfer(Use,Bytes,"submitting",Error);
 xrtValueRelease(Task);
 if(!Ok&&Error&&!Error[0])MdoExtServiceError(Error,"The owned archive upload could not be resumed, transferred or verified");
 return Ok;
}
bool MdoExtensionServicePublish(const xvalue* Input,xcancel* Cancel,xvalue** Result,char Error[512]){
 *Result=NULL;if(Error)Error[0]=0;cstr Id=MdoExtensionText(Input,"task",32),Proof=MdoExtensionText(Input,"proof_task",32);if(!MdoExtServiceId(Id)||(Proof&&(!MdoExtServiceId(Proof)||!strcmp(Id,Proof))))return MdoExtServiceError(Error,"Choose a sealed local publication task and optional separate review file task");
 MdoExtensionTaskUse* Use=MdoExtensionTaskUseBegin(Id,false,Cancel,Error),*ProofUse=Use&&Proof?MdoExtensionTaskUseBegin(Proof,false,MdoExtensionTaskUseCancel(Use),Error):NULL;MdoAccountLease Lease={0};
 bool Ok=Use&&(!Proof||ProofUse)&&MdoExtServiceLease(MdoExtensionTaskUseCancel(Use),true,&Lease,Error);xvalue* Profile=Ok?MdoExtServiceJson(MDO_EXT_SERVICE "/creator?scope=profile","GET",NULL,&Lease,Cancel,Error):NULL;
 xvalue* Task=Use?MdoExtensionTaskRead(Id,Error):NULL,*ProofTask=ProofUse?MdoExtensionTaskRead(Proof,Error):NULL;uint64 Bytes=0,ProofBytes=0,Limit=0;
 Ok=Ok&&Profile&&Task&&MdoExtServiceNumber(Profile,"version_bytes",&Limit)&&MdoExtServiceNumber(Task,"bytes",&Bytes)&&(!Proof||(ProofTask&&MdoExtServiceNumber(ProofTask,"bytes",&ProofBytes)))&&Bytes<=Limit&&ProofBytes<=Limit-Bytes;xrtValueRelease(Profile);xrtValueRelease(Task);xrtValueRelease(ProofTask);
 xvalue* Body=xrtValueObject();const cstr Fields[]={"publish","review_notes","source_url","source_included","work","expected_revision"};
 for(size_t I=0;Body&&I<sizeof(Fields)/sizeof(*Fields);I++){const xvalue* Value=xrtValueObjectGet(Input,xrtStrView(Fields[I]));if(Value&&!xrtValueObjectSet(Body,xrtStrView(Fields[I]),Value)){xrtValueRelease(Body);Body=NULL;break;}}
 Ok=Ok&&Body&&MdoExtServicePut(Body,"action","submit")&&MdoExtServicePut(Body,"upload",Id)&&MdoExtServicePut(Body,"nonce",Id)&&(!Proof||MdoExtServicePut(Body,"proof_upload",Proof));
 if(Ok)Ok=MdoExtensionTaskPublication(Use,Body,Error);
 MdoAccountLease ProofLease={0};if(Ok&&Proof)Ok=MdoExtServiceLease(MdoExtensionTaskUseCancel(ProofUse),true,&ProofLease,Error)&&ProofLease.Generation==Lease.Generation&&ProofLease.MemberId==Lease.MemberId;
 if(Ok)Ok=MdoExtServiceUpload(Id,Use,&Lease,Error)&&(!Proof||MdoExtServiceUpload(Proof,ProofUse,&ProofLease,Error));
 if(Ok)*Result=MdoExtServiceJson(MDO_EXT_SERVICE "/creator","POST",Body,&Lease,Cancel,Error);
 Ok=Ok&&*Result;
 if(Ok){const cstr Ids[]={Id,Proof};for(size_t I=0;I<2;I++)if(Ids[I]){xvalue* Receipt=NULL;char Why[512]={0};if(!MdoExtensionTaskComplete(Ids[I],NULL,&Receipt,Why))MdoExtServicePut(*Result,"transfer_receipt_error",Why);xrtValueRelease(Receipt);}}
 xrtValueRelease(Body);MdoAccountRelease(&ProofLease);MdoAccountRelease(&Lease);MdoExtensionTaskUseEnd(ProofUse);MdoExtensionTaskUseEnd(Use);if(!Ok&&Error&&!Error[0])MdoExtServiceError(Error,"Publication exceeds the account quota or its local tasks are unavailable");return Ok;
}
bool MdoExtensionServiceWrite(const xvalue* Input,xcancel* Cancel,xvalue** Result,char Error[512]){
 *Result=NULL;if(Error)Error[0]=0;cstr Action=MdoExtensionText(Input,"action",24);bool Allowed=Action&&(!strcmp(Action,"publish")||!strcmp(Action,"withdraw")||!strcmp(Action,"remove")||!strcmp(Action,"remove_version")||!strcmp(Action,"request"));
 if(!Allowed)return MdoExtServiceError(Error,"Unsupported creator action; upload archive files through owned tasks");
 const cstr Fields[]={"action","version","work","expected_revision","review_notes","source_url","source_included","proof_upload","type","requested","reason"};xvalue* Body=xrtValueObject();bool Ok=Body!=NULL;
 for(size_t I=0;Ok&&I<sizeof(Fields)/sizeof(*Fields);I++){const xvalue* Value=xrtValueObjectGet(Input,xrtStrView(Fields[I]));if(Value)Ok=xrtValueObjectSet(Body,xrtStrView(Fields[I]),Value);}
 MdoAccountLease Lease={0};Ok=Ok&&MdoExtServiceLease(Cancel,true,&Lease,Error);if(Ok)*Result=MdoExtServiceJson(MDO_EXT_SERVICE "/creator","POST",Body,&Lease,Cancel,Error);xrtValueRelease(Body);MdoAccountRelease(&Lease);return *Result!=NULL;
}

#include "service_plan.inc.c"
