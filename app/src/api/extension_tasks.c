#include "internal.h"
#include "write_admission.h"
#include "../../include/mdo/extension_tasks.h"
#include "../../include/mdo/extensions.h"
#include "../../include/mdo/extension_manifest.h"
#include "../../include/mdo/home.h"
#include "../../include/mdo/time.h"
#include "../extensions/tasks_internal.h"

typedef struct MdoArchiveApiJob {MdoApiContext Context;XS_HttpReq Request;xhttp1head Head;char Id[33];xvalue* Input;unsigned Operation;bool Sent;} MdoArchiveApiJob;
static struct {xmutex* Lock;xtaskpool* Pool;size_t Count;bool Stopping;} MdoArchiveApi;
static bool MdoArchiveApiResult(MdoApiContext* C,bool Ok,xvalue* Result,cstr Error){
 if(Ok)return MdoApiReplySuccessTake(C,200,Result,NULL);xrtValueRelease(Result);return MdoApiReplyError(C,422,"extension_archive_failed",Error&&Error[0]?Error:"Archive operation could not complete",NULL);
}
static bool MdoArchiveApiInstall(MdoApiContext* C,cstr Id,const xvalue* Input){
 char Error[512]={0};MdoExtensionTaskUse* Use=MdoExtensionTaskUseBegin(Id,false,C->SendCancel,Error);char* Archive=Use?MdoExtensionTaskArchive(Id,Error):NULL;xvalue* Task=Archive?MdoExtensionTaskRead(Id,Error):NULL,*Request=Input?xrtValueClone(Input):xrtValueObject(),*Result=NULL;
 cstr Kind=MdoExtensionText(Task,"kind",32);bool Ok=Archive&&Task&&Request&&Kind&&MdoExtensionDirectoryKind(Kind)&&MdoExtArchivePut(Request,"action","install")&&MdoExtArchivePut(Request,"kind",Kind)&&MdoExtArchivePut(Request,"archive",Archive)&&MdoExtensionsManage(Request,NULL,MdoExtensionTaskUseCancel(Use),&Result,Error);
 if(Ok){xvalue* Finished=NULL;char ReceiptError[512]={0};if(MdoExtensionTaskComplete(Id,Result,&Finished,ReceiptError))xrtValueObjectSet(Result,XRT_STR_LITERAL("task"),Finished);else MdoExtArchivePut(Result,"transfer_receipt_error",ReceiptError);xrtValueRelease(Finished);}
 xrtValueRelease(Request);xrtValueRelease(Task);xrtFree(Archive);MdoExtensionTaskUseEnd(Use);return MdoArchiveApiResult(C,Ok,Result,Error);
}
static void MdoArchiveApiSecrets(xvalue* Input){
 const xvalue* Secrets=xrtValueObjectGet(Input,XRT_STR_LITERAL("secrets"));for(size_t I=0;I<xrtValueCount(Secrets);I++){xstrview Key={0},Value={0};const xvalue* V=xrtValueObjectAt(Secrets,I,&Key);if(xrtValueGetString(V,&Value))xrtSecureZero((void*)Value.Data,Value.Size);}
}
static void MdoArchiveApiDrop(ptr Value,ptr User){
 (void)User;MdoArchiveApiJob* J=Value;MdoApiWriteLeave(&J->Context);if(J->Operation==4)MdoArchiveApiSecrets(J->Input);xrtValueRelease(J->Input);
 if(J->Request.tls){if(J->Sent)xrtTlsStreamClose(J->Request.tls);else xrtTlsStreamAbort(J->Request.tls);xrtTlsStreamDestroy(J->Request.tls);}
 if(J->Request.tcp){if(J->Sent)xrtNetStreamClose(J->Request.tcp);else xrtNetStreamAbort(J->Request.tcp);xrtNetStreamDestroy(J->Request.tcp);}
 xrtMutexLock(MdoArchiveApi.Lock);if(MdoArchiveApi.Count)MdoArchiveApi.Count--;xrtMutexUnlock(MdoArchiveApi.Lock);xrtFree(J);
}
static xtaskoutcome MdoArchiveApiRun(xcancel* Cancel,ptr Value,xtaskvalue* Result){
 (void)Result;MdoArchiveApiJob* J=Value;J->Context.SendCancel=Cancel;if(!MdoApiDownloadLive(&J->Context))return XTASK_CANCELLED;
 char Error[512]={0};xvalue* Out=NULL;
 if(J->Operation==1)J->Sent=MdoArchiveApiInstall(&J->Context,J->Id,J->Input);
 else if(J->Operation>=2){bool Ok=J->Operation==4?MdoMcpConnectionManage(J->Input,Cancel,&Out,Error):J->Operation==3?MdoExtensionTaskPack(MdoExtensionText(J->Input,"kind",32),MdoExtensionText(J->Input,"id",96),Cancel,&Out,Error):MdoExtensionsManage(J->Input,NULL,Cancel,&Out,Error);J->Sent=MdoArchiveApiResult(&J->Context,Ok,Out,Error);}
 else{bool Ok=MdoExtensionTaskSeal(J->Id,Cancel,&Out,Error);J->Sent=MdoArchiveApiResult(&J->Context,Ok,Out,Error);}
 MdoApiLiveChanged(NULL);return J->Sent?XTASK_SUCCESS:XTASK_FAILED;
}
static bool MdoArchiveApiStart(MdoApiContext* C,cstr Id,const xvalue* Input,unsigned Operation){
 MdoArchiveApiJob* J=xrtCalloc(1,sizeof(*J));xfuture* Future=NULL;if(!J||!MdoArchiveApi.Lock)goto failed;
 J->Head.MethodCode=XHTTP_METHOD_POST;J->Request.head=&J->Head;if(C->Request->tls)J->Request.tls=xrtTlsStreamRef(C->Request->tls);else if(C->Request->tcp)J->Request.tcp=xrtNetStreamRef(C->Request->tcp);
 if(!J->Request.tls&&!J->Request.tcp)goto failed;J->Context.Request=&J->Request;J->Context.CloseResponse=true;J->Context.SendDeadline=MdoDeadlineAfterUs(UINT64_C(1200000000));
 memcpy(J->Context.RequestId,C->RequestId,sizeof(C->RequestId));snprintf(J->Id,sizeof(J->Id),"%s",Id);J->Context.WriteShared=C->WriteShared;J->Context.WriteExclusive=C->WriteExclusive;J->Operation=Operation;J->Input=Input?xrtValueClone(Input):NULL;
 if(Input&&!J->Input)goto failed;xrtMutexLock(MdoArchiveApi.Lock);
 if(!MdoArchiveApi.Stopping&&MdoArchiveApi.Count<3){if(!MdoArchiveApi.Pool){xtaskpoolconfig Config={0};Config.Threads=1;Config.QueueLimit=2;MdoArchiveApi.Pool=xrtTaskPoolCreate(&Config);}if(MdoArchiveApi.Pool){xtaskargs Args={0};Args.Destroy=MdoArchiveApiDrop;MdoArchiveApi.Count++;Future=xrtTaskSubmit(MdoArchiveApi.Pool,MdoArchiveApiRun,J,&Args);if(!Future)MdoArchiveApi.Count--;}}
 xrtMutexUnlock(MdoArchiveApi.Lock);if(Future){C->WriteShared=C->WriteExclusive=false;C->Takeover=true;xrtFutureDestroy(Future);return true;}
failed:if(J){xrtTlsStreamDestroy(J->Request.tls);xrtNetStreamDestroy(J->Request.tcp);if(J->Operation==4)MdoArchiveApiSecrets(J->Input);xrtValueRelease(J->Input);xrtFree(J);}return MdoApiReplyError(C,503,"extension_archive_busy","Another archive operation is still finishing",NULL);
}
static bool MdoApiDirectoryOperation(MdoApiContext* C,const xvalue* Input,bool Pack){
 if(C->ApplicationResponse){char Error[512]={0};xvalue* Out=NULL;bool Ok=Pack?MdoExtensionTaskPack(MdoExtensionText(Input,"kind",32),MdoExtensionText(Input,"id",96),C->SendCancel,&Out,Error):MdoExtensionsManage(Input,NULL,C->SendCancel,&Out,Error);return MdoArchiveApiResult(C,Ok,Out,Error);}
 return MdoArchiveApiStart(C,"",Input,Pack?3:2);
}
bool MdoApiExtensionTasksInit(void){if(MdoArchiveApi.Lock)return MdoExtensionTasksInit();MdoArchiveApi.Lock=xrtMutexCreate();if(!MdoArchiveApi.Lock)return false;if(!MdoExtensionTasksInit()){xrtMutexDestroy(MdoArchiveApi.Lock);memset(&MdoArchiveApi,0,sizeof(MdoArchiveApi));return false;}return true;}
void MdoApiExtensionTasksUnit(void){if(!MdoArchiveApi.Lock)return;xrtMutexLock(MdoArchiveApi.Lock);MdoArchiveApi.Stopping=true;xtaskpool* Pool=MdoArchiveApi.Pool;xrtMutexUnlock(MdoArchiveApi.Lock);if(Pool){xrtTaskPoolCancel(Pool);xrtTaskPoolWait(Pool);xrtTaskPoolDestroy(Pool);}xrtMutexDestroy(MdoArchiveApi.Lock);memset(&MdoArchiveApi,0,sizeof(MdoArchiveApi));}
static bool MdoArchiveApiOffset(xstrview Text,uint64* Offset){*Offset=0;if(!Text.Size||Text.Size>20)return false;for(size_t I=0;I<Text.Size;I++){unsigned char C=Text.Data[I];if(C<'0'||C>'9'||*Offset>(UINT64_MAX-(C-'0'))/10)return false;*Offset=*Offset*10+(C-'0');}return true;}
static bool MdoArchiveApiSuffix(MdoApiContext* C,cstr S){size_t N=strlen(S);return C->Target.Path.Size>=N&&!memcmp(C->Target.Path.Data+C->Target.Path.Size-N,S,N);}
bool MdoApiExtensionTasksRoute(MdoApiContext* C){
 char Error[512]={0},Id[33]={0};xvalue* Out=NULL;bool Ok=false;xhttpmethod Method=C->Request->head->MethodCode;
 if(!C->ParamCount){if(Method==XHTTP_METHOD_GET){Out=MdoExtensionTasksList(Error);return MdoArchiveApiResult(C,Out!=NULL,Out,Error);}
  MdoApiJsonBody Body={0};MdoApiBodyStatus Read=MdoApiJsonBodyRead(C,&Body);if(Read!=MDO_API_BODY_OK)return MdoApiReplyBodyError(C,Read);Ok=MdoExtensionTaskBegin(Body.Value,&Out,Error);MdoApiJsonBodyUnit(&Body);return MdoArchiveApiResult(C,Ok,Out,Error);}
 if(C->Params[0].Size!=32)return MdoApiReplyError(C,400,"extension_task_invalid","Expected an archive task identity",NULL);memcpy(Id,C->Params[0].Data,32);if(!MdoExtArchiveId(Id))return MdoApiReplyError(C,400,"extension_task_invalid","Invalid archive task identity",NULL);
 if(C->ParamCount==2){uint64 Offset=0;if(!MdoArchiveApiOffset(C->Params[1],&Offset))return MdoApiReplyError(C,400,"extension_task_offset","Invalid byte offset",NULL);
  if(Method==XHTTP_METHOD_PUT){char* Data=NULL;size_t Bytes=0;MdoApiBodyStatus Read=MdoApiBinaryBodyRead(C,MDO_EXTENSION_TASK_CHUNK,&Data,&Bytes);if(Read!=MDO_API_BODY_OK)return MdoApiReplyBodyError(C,Read);Ok=MdoExtensionTaskWrite(Id,Offset,Data,Bytes,&Out,Error);xrtFree(Data);return MdoArchiveApiResult(C,Ok,Out,Error);}
  char* Path=MdoExtensionTaskArchive(Id,Error);xvalue* Task=Path?MdoExtensionTaskRead(Id,Error):NULL;xfile F=Path?MdoHomeOpenRead(Path):NULL;uint64 Bytes=0;Ok=F&&xrtFileSize(F,&Bytes)&&Offset<Bytes;size_t Count=Ok?(size_t)(Bytes-Offset<MDO_EXTENSION_TASK_CHUNK?Bytes-Offset:MDO_EXTENSION_TASK_CHUNK):0;uint8* Data=Count?xrtMalloc(Count):NULL;Ok=Ok&&Data&&xrtReadAtFull(F,Offset,Data,Count,NULL);xrtClose(F);xrtFree(Path);
  char Tag[67];snprintf(Tag,sizeof(Tag),"\"%s\"",MdoExtensionText(Task,"sha256",64)?MdoExtensionText(Task,"sha256",64):"");bool Sent=Ok?MdoApiReplyDownload(C,Data,Count,"attachment; filename=extension.zip.part",Tag):MdoApiReplyError(C,416,"extension_task_range","Archive is unavailable or this range exceeds its length",NULL);xrtFree(Data);xrtValueRelease(Task);return Sent;
 }
 if(MdoArchiveApiSuffix(C,"/seal")){if(C->ApplicationResponse){Ok=MdoExtensionTaskSeal(Id,C->SendCancel,&Out,Error);return MdoArchiveApiResult(C,Ok,Out,Error);}return MdoArchiveApiStart(C,Id,NULL,false);}
 if(MdoArchiveApiSuffix(C,"/install")){MdoApiJsonBody Body={0};MdoApiBodyStatus Read=MdoApiJsonBodyRead(C,&Body);if(Read!=MDO_API_BODY_OK)return MdoApiReplyBodyError(C,Read);bool Sent=C->ApplicationResponse?MdoArchiveApiInstall(C,Id,Body.Value):MdoArchiveApiStart(C,Id,Body.Value,true);MdoApiJsonBodyUnit(&Body);return Sent;}
 if(MdoArchiveApiSuffix(C,"/forget")){Ok=MdoExtensionTaskForget(Id,Error);Out=Ok?xrtValueObject():NULL;}
 else if(Method==XHTTP_METHOD_DELETE)Ok=MdoExtensionTaskCancel(Id,&Out,Error);
 else{Out=MdoExtensionTaskRead(Id,Error);Ok=Out!=NULL;}return MdoArchiveApiResult(C,Ok,Out,Error);
}
