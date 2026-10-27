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
static bool MdoExtServiceFetch(cstr Path,cstr Method,const void* Body,size_t Bytes,bool Binary,const MdoAccountLease* Lease,xcancel* Cancel,XS_FetchResponse* Response,char Error[512]){
 char* Url=xrtFormat(MDO_ACCOUNT_SERVICE_ORIGIN "%s",Path);char Authorization[4200]={0};XS_FetchHeader Headers[3]={{"Accept",Binary&&!strcmp(Method,"GET")?"application/zip":"application/json"},{"Content-Type",Binary?"application/octet-stream":"application/json"}};size_t Count=2;
 bool Ok=Url&&Path&&!strncmp(Path,MDO_EXT_SERVICE,sizeof(MDO_EXT_SERVICE)-1)&&Bytes<=1048576&&(!Cancel||!xrtCancelRequested(Cancel));
 if(Ok&&Lease&&Lease->Managed){Ok=Lease->AccessToken&&strlen(Lease->AccessToken)<=4096;if(Ok){snprintf(Authorization,sizeof(Authorization),"Bearer %s",Lease->AccessToken);Headers[Count++]=(XS_FetchHeader){"Authorization",Authorization};}}
 XS_FetchRequest Request={0};Request.Size=sizeof(Request);Request.Version=XS_FETCH_REQUEST_VERSION;Request.Url=Url;Request.Method=Method;Request.Headers=Headers;Request.HeaderCount=Count;Request.Body=Body;Request.BodySize=Bytes;Request.Timeout=30000000;Request.IdleTimeout=10000000;Request.MaxBodyBytes=Binary?1048576:8u*1024u*1024u;Request.Cancel=Lease&&Lease->Managed?Lease->Cancel:Cancel;Response->Size=sizeof(*Response);
 if(Ok){xrtClearError();Ok=xsFetch(&Request,Response);}xrtSecureZero(Authorization,sizeof(Authorization));xrtFree(Url);
 if(!Ok){if(Request.Cancel&&xrtCancelRequested(Request.Cancel))return MdoExtServiceError(Error,"Extension transfer cancelled or its account session changed");const xerror* Cause=xrtGetError();if(Error&&Cause)snprintf(Error,512,"The extension service connection failed: %.400s",xrtErrorMessage(Cause));else MdoExtServiceError(Error,"The extension service connection could not complete");return false;}
 if(Response->Status==401&&Lease&&Lease->Managed)(void)MdoAccountRejectAccess(Lease);
 if(Response->Redirects||Response->Status<200||Response->Status>=300){char Message[512]="The extension service rejected this request";xvalue* Envelope=Response->Body&&Response->BodySize<=65536?xrtJsonParse(xrtStrViewN((cstr)Response->Body,Response->BodySize)):NULL;cstr Text=MdoExtensionText(Envelope,"message",500);if(Text)snprintf(Message,sizeof(Message),"%s",Text);xrtValueRelease(Envelope);return MdoExtServiceError(Error,Message);}
 return true;
}
static xvalue* MdoExtServiceDecode(const XS_FetchResponse* Response,char Error[512]){
 xjsonreadconfig Config;xrtJsonReadConfigInit(&Config);Config.MaxInputBytes=8u*1024u*1024u;Config.MaxStringBytes=1048576;Config.MaxValues=200000;Config.MaxDepth=20;
 xvalue* Envelope=xrtJsonRead(xrtStrViewN((cstr)Response->Body,Response->BodySize),&Config);int64 Code=-1;bool Ok=Envelope&&xrtValueGetInt(xrtValueObjectGet(Envelope,XRT_STR_LITERAL("code")),&Code)&&Code==0;
 xvalue* Data=Ok?xrtValueClone(xrtValueObjectGet(Envelope,XRT_STR_LITERAL("data"))):NULL;if(!Data&&Error&&!Error[0]){cstr Message=MdoExtensionText(Envelope,"message",500);MdoExtServiceError(Error,Message?Message:"The extension service returned invalid metadata");}xrtValueRelease(Envelope);return Data;
}
static xvalue* MdoExtServiceJson(cstr Path,cstr Method,const xvalue* Body,const MdoAccountLease* Lease,xcancel* Cancel,char Error[512]){
 size_t Bytes=0;char* Text=Body?xrtJsonStringify(Body,false,&Bytes):NULL;XS_FetchResponse Response={0};bool Ok=(!Body||Text)&&Bytes<=16384&&MdoExtServiceFetch(Path,Method,Text,Bytes,false,Lease,Cancel,&Response,Error);xrtFree(Text);
 xvalue* Data=Ok?MdoExtServiceDecode(&Response,Error):NULL;xsFetchResponseUnit(&Response);return Data;
}
static char* MdoExtServiceQuery(cstr Base,const xvalue* Input){
 char* Path=xrtStrDup(Base);const cstr TextFields[]={"kind","platform","q","state","work","path"};const size_t Limits[]={32,32,240,16,32,4096};
 for(size_t I=0;Path&&I<6;I++){const xvalue* Field=xrtValueObjectGet(Input,xrtStrView(TextFields[I]));if(!Field)continue;cstr Text=MdoExtensionText(Input,TextFields[I],Limits[I]);if(!Text){xrtFree(Path);return NULL;}char* Encoded=xrtPercentEncodeNew(Text,strlen(Text),XRT_STR_LITERAL(""),NULL),*Next=Encoded?xrtFormat("%s%c%s=%s",Path,strchr(Path,'?')?'&':'?',TextFields[I],Encoded):NULL;xrtFree(Encoded);xrtFree(Path);Path=Next;}
 const cstr Numbers[]={"offset","limit"};for(size_t I=0;Path&&I<2;I++){if(!xrtValueObjectHas(Input,xrtStrView(Numbers[I])))continue;uint64 N=0;if(!MdoExtServiceNumber(Input,Numbers[I],&N)||N>(I?200:100000)){xrtFree(Path);return NULL;}char* Next=xrtFormat("%s%c%s=%llu",Path,strchr(Path,'?')?'&':'?',Numbers[I],(unsigned long long)N);xrtFree(Path);Path=Next;}return Path;
}
bool MdoExtensionServiceRead(const xvalue* Input,xcancel* Cancel,xvalue** Result,char Error[512]){
 *Result=NULL;if(Error)Error[0]=0;cstr Action=MdoExtensionText(Input,"action",24),Kind=MdoExtensionText(Input,"kind",32),Version=MdoExtensionText(Input,"version_id",32);if(!Action||(Kind&&Kind[0]&&!MdoExtensionDirectoryKind(Kind)))return MdoExtServiceError(Error,"Choose an extension service action and type");
 bool Required=!strcmp(Action,"mine")||!strcmp(Action,"profile")||!strcmp(Action,"requests");char* Base=NULL;
 if(!strcmp(Action,"catalog"))Base=xrtStrDup(MDO_EXT_SERVICE "/catalog");
 else if(!strcmp(Action,"mine")||!strcmp(Action,"profile")||!strcmp(Action,"requests"))Base=xrtFormat(MDO_EXT_SERVICE "/creator?scope=%s",!strcmp(Action,"mine")?"works":Action);
 else if((!strcmp(Action,"detail")||!strcmp(Action,"files")||!strcmp(Action,"preview"))&&MdoExtServiceId(Version))Base=xrtFormat(MDO_EXT_SERVICE "/%s?version=%s",Action,Version);
 if(!Base)return MdoExtServiceError(Error,"Unsupported extension read; no arbitrary service URL is accepted");char* Path=MdoExtServiceQuery(Base,Input);xrtFree(Base);MdoAccountLease Lease={0};bool Ok=Path&&MdoExtServiceLease(Cancel,Required,&Lease,Error);if(Ok)*Result=MdoExtServiceJson(Path,"GET",NULL,&Lease,Cancel,Error);MdoAccountRelease(&Lease);xrtFree(Path);return *Result!=NULL;
}
static xvalue* MdoExtServiceOrigin(const xvalue* Entry){
 const cstr Names[]={"kind","xid","version_id","sha256","bytes","owner","submitter","author","copyright","license","name","alias","platforms","state","version"};xvalue* Origin=xrtValueObject();bool Ok=Origin!=NULL;
 for(size_t I=0;Ok&&I<sizeof(Names)/sizeof(*Names);I++){const xvalue* Value=xrtValueObjectGet(Entry,xrtStrView(Names[I]));if(Value)Ok=xrtValueObjectSet(Origin,xrtStrView(Names[I]),Value);}if(!Ok){xrtValueRelease(Origin);return NULL;}return Origin;
}
bool MdoExtensionServiceDownload(cstr Kind,cstr Version,xcancel* Cancel,xvalue** Result,char Error[512]){
 *Result=NULL;if(Error)Error[0]=0;if(!MdoExtensionDirectoryKind(Kind)||!MdoExtServiceId(Version))return MdoExtServiceError(Error,"Choose a published extension version");
#if defined(__ANDROID__)
 if(!strcmp(Kind,"external-tools"))return MdoExtServiceError(Error,"Android external programs are installed and updated with the full APK");
#endif
 xvalue* Query=xrtValueObject(),*Entry=NULL,*Task=NULL,*Origin=NULL;bool Ok=Query&&MdoExtServicePut(Query,"action","detail")&&MdoExtServicePut(Query,"version_id",Version)&&MdoExtensionServiceRead(Query,Cancel,&Entry,Error);xrtValueRelease(Query);
 cstr ActualKind=MdoExtensionText(Entry,"kind",32),State=MdoExtensionText(Entry,"state",16),Hash=MdoExtensionText(Entry,"sha256",64);uint64 Bytes=0;
 cstr ActualVersion=MdoExtensionText(Entry,"version_id",32);Ok=Ok&&ActualKind&&!strcmp(Kind,ActualKind)&&ActualVersion&&!strcmp(Version,ActualVersion)&&State&&!strcmp(State,"published")&&MdoExtServiceNumber(Entry,"bytes",&Bytes)&&Bytes&&Bytes<=MDO_EXTENSION_ARCHIVE_BYTES;
 xvalue* Input=xrtValueObject();Ok=Ok&&Input&&MdoExtServicePut(Input,"kind",Kind)&&MdoExtServicePut(Input,"purpose","install")&&MdoExtServicePut(Input,"sha256",Hash)&&xrtValueObjectSetNew(Input,XRT_STR_LITERAL("bytes"),xrtValueUInt(Bytes))&&MdoExtensionTaskBegin(Input,&Task,Error);xrtValueRelease(Input);
 char Id[33]={0};if(Ok)snprintf(Id,sizeof(Id),"%s",MdoExtensionText(Task,"id",32));MdoExtensionTaskUse* Use=Ok?MdoExtensionTaskUseBegin(Id,true,Cancel,Error):NULL;MdoAccountLease Lease={0};Origin=Ok?MdoExtServiceOrigin(Entry):NULL;
 Ok=Ok&&Use&&Origin&&MdoExtensionTaskOrigin(Use,Origin,Error)&&MdoExtServiceLease(MdoExtensionTaskUseCancel(Use),false,&Lease,Error);
 for(uint64 Offset=0;Ok&&Offset<Bytes;){char* Path=xrtFormat(MDO_EXT_SERVICE "/archive?version=%s&offset=%llu",Version,(unsigned long long)Offset);XS_FetchResponse Response={0};uint64 Remaining=Bytes-Offset;size_t Want=(size_t)(Remaining<MDO_EXTENSION_TASK_CHUNK?Remaining:MDO_EXTENSION_TASK_CHUNK);
  Ok=Path&&MdoExtServiceFetch(Path,"GET",NULL,0,true,&Lease,MdoExtensionTaskUseCancel(Use),&Response,Error)&&Response.BodySize==Want&&Response.ContentType&&!strncmp(Response.ContentType,"application/zip",15);
  xvalue* Progress=NULL;if(Ok)Ok=MdoExtensionTaskUseWrite(Use,Offset,Response.Body,Response.BodySize,&Progress,Error);xrtValueRelease(Progress);xsFetchResponseUnit(&Response);xrtFree(Path);Offset+=Want;
 }
 MdoAccountRelease(&Lease);MdoExtensionTaskUseEnd(Use);if(Ok)Ok=MdoExtensionTaskSeal(Id,Cancel,Result,Error);
 if(!Ok&&Id[0]){xvalue* Receipt=NULL;char Ignored[512];(void)MdoExtensionTaskCancel(Id,&Receipt,Ignored);xrtValueRelease(Receipt);}
 xrtValueRelease(Entry);xrtValueRelease(Task);xrtValueRelease(Origin);if(!Ok&&Error&&!Error[0])MdoExtServiceError(Error,"The store archive changed or failed its byte/hash/ZIP validation");return Ok;
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
 if(Ok)Remote=MdoExtServiceJson(MDO_EXT_SERVICE "/uploads","POST",Begin,Lease,MdoExtensionTaskUseCancel(Use),Error);xrtValueRelease(Begin);
 cstr State=MdoExtensionText(Remote,"state",16);bool Finished=State&&(!strcmp(State,"sealed")||!strcmp(State,"consumed"));Ok=Ok&&Remote&&State&&!strcmp(MdoExtensionText(Remote,"xid",32)?MdoExtensionText(Remote,"xid",32):"",Id)&&(Finished||!strcmp(State,"receiving"));xrtValueRelease(Remote);Remote=NULL;
 uint64 Offset=0;if(Ok&&!Finished){char* Path=xrtFormat(MDO_EXT_SERVICE "/uploads?id=%s",Id);Remote=Path?MdoExtServiceJson(Path,"GET",NULL,Lease,MdoExtensionTaskUseCancel(Use),Error):NULL;xrtFree(Path);Ok=Remote&&MdoExtServiceNumber(Remote,"received",&Offset)&&Offset<=Bytes;xrtValueRelease(Remote);Remote=NULL;}
 uint8* Buffer=Ok&&!Finished?xrtMalloc(MDO_EXTENSION_TASK_CHUNK):NULL;if(Ok&&!Finished&&!Buffer)Ok=false;
 while(Ok&&!Finished&&Offset<Bytes){size_t N=(size_t)(Bytes-Offset<MDO_EXTENSION_TASK_CHUNK?Bytes-Offset:MDO_EXTENSION_TASK_CHUNK);char* Path=xrtFormat(MDO_EXT_SERVICE "/uploads?id=%s&offset=%llu",Id,(unsigned long long)Offset);XS_FetchResponse Response={0};
  Ok=Path&&MdoExtensionTaskTransfer(Use,Offset,"uploading",Error)&&xrtReadAtFull(F,Offset,Buffer,N,NULL)&&MdoExtServiceFetch(Path,"PUT",Buffer,N,true,Lease,MdoExtensionTaskUseCancel(Use),&Response,Error);
  Remote=Ok?MdoExtServiceDecode(&Response,Error):NULL;uint64 Received=0;Ok=Ok&&Remote&&MdoExtServiceNumber(Remote,"received",&Received)&&Received==Offset+N;xrtValueRelease(Remote);Remote=NULL;xsFetchResponseUnit(&Response);xrtFree(Path);Offset+=N;
 }
 xrtFree(Buffer);xrtClose(F);
 if(Ok&&!Finished){xvalue* Seal=xrtValueObject();Ok=Seal&&MdoExtServicePut(Seal,"action","seal")&&MdoExtServicePut(Seal,"id",Id);if(Ok)Remote=MdoExtServiceJson(MDO_EXT_SERVICE "/uploads","POST",Seal,Lease,MdoExtensionTaskUseCancel(Use),Error);xrtValueRelease(Seal);State=MdoExtensionText(Remote,"state",16);Ok=Ok&&State&&!strcmp(State,"sealed");xrtValueRelease(Remote);}
 if(Ok)Ok=MdoExtensionTaskTransfer(Use,Bytes,"submitting",Error);xrtValueRelease(Task);if(!Ok&&Error&&!Error[0])MdoExtServiceError(Error,"The owned archive upload could not be resumed, transferred or verified");return Ok;
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
 if(Ok)*Result=MdoExtServiceJson(MDO_EXT_SERVICE "/creator","POST",Body,&Lease,Cancel,Error);Ok=Ok&&*Result;
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
