#include "internal.h"
#include "../../include/mdo/extension_files.h"
#include "../../include/mdo/modules.h"
#include "../../include/mdo/project_lifecycle.h"
#include "../../include/mdo/project_binding.h"
#include "../../include/mdo/time.h"

/* Operation identities are admission receipts, not retry instructions. Never
 * replay a persisted identity after a crash, even if its result was lost. */
#define SDK_JOBS 64u
#define SDK_ACTIVE_JOBS 8u
typedef struct SdkJob {
    char Id[37],Hash[65];
    char WriteToken[64];
    char *Extension,*Project,*Session,*Run,*Workspace,*Method;
    xvalue *Params,*Info,*Result;
    char* Json;
    size_t ResultSize;
    xthread* Thread;
    xcancel* Cancel;
    MdoModuleCatalog* Modules;
    MdoProjectLease* Lease;
    uint64 Deadline;
    bool Terminal,Disposing;
} SdkJob;
static struct {xmutex* Lock;SdkJob Jobs[SDK_JOBS];bool Stopping;} SdkJobs;
static const cstr SdkJobMethods[]={"operation.start","operation.get","operation.list","operation.wait","operation.cancel","operation.result","operation.release"};
static bool SdkRandomId(cstr Prefix,char Id[37]){
    unsigned char B[16];static const char Hex[]="0123456789abcdef";
    if(!xrtSecureRandom(B,sizeof(B)))return false;
    memcpy(Id,Prefix,4);for(size_t i=0;i<16;i++){Id[4+i*2]=Hex[B[i]>>4];Id[5+i*2]=Hex[B[i]&15];}Id[36]=0;return true;
}
static bool SdkOperationId(cstr Id){
    if(!Id||strlen(Id)!=36||memcmp(Id,"sdk-",4))return false;
    for(size_t i=4;i<36;i++)if(!((Id[i]>='0'&&Id[i]<='9')||(Id[i]>='a'&&Id[i]<='f')))return false;
    return true;
}
static SdkJob* SdkJobFind(cstr Id){for(size_t i=0;Id&&i<SDK_JOBS;i++)if(!strcmp(Id,SdkJobs.Jobs[i].Id))return &SdkJobs.Jobs[i];return NULL;}
static int SdkCanonicalKeyCompare(const void* A,const void* B){return strcmp(*(const char* const*)A,*(const char* const*)B);}
static xvalue* SdkCanonicalValue(const xvalue* V,unsigned Depth){
    if(Depth>64)return NULL;
    xvaluetype Type=xrtValueType(V);if(Type!=XVALUE_OBJECT&&Type!=XVALUE_ARRAY)return xrtValueClone(V);
    size_t Count=xrtValueCount(V);xvalue* Result=Type==XVALUE_OBJECT?xrtValueObject():xrtValueArray();
    const char** Keys=Type==XVALUE_OBJECT&&Count?xrtCalloc(Count,sizeof(*Keys)):NULL;
    bool Ok=Result&&(Type!=XVALUE_OBJECT||!Count||Keys);
    if(Type==XVALUE_OBJECT){for(size_t i=0;Ok&&i<Count;i++){Keys[i]=SdkKey(V,i);Ok=Keys[i]!=NULL;}if(Ok&&Count>1)qsort(Keys,Count,sizeof(*Keys),SdkCanonicalKeyCompare);}
    for(size_t i=0;Ok&&i<Count;i++){
        xvalue* Child=SdkCanonicalValue(Type==XVALUE_OBJECT?SdkGet(V,Keys[i]):xrtValueArrayGet(V,i),Depth+1);
        Ok=Child&&(Type==XVALUE_OBJECT?xrtValueObjectSet(Result,xrtStrView(Keys[i]),Child):xrtValueArrayAppend(Result,Child));xrtValueRelease(Child);
    }
    xrtFree(Keys);if(!Ok){xrtValueRelease(Result);return NULL;}return Result;
}
static bool SdkJobProject(const mdo_sdk_context_v1* C,cstr Method,MdoProjectLease** Lease,mdo_error_v1* E){
    *Lease=NULL;
    if(!C->ProjectId||!C->ProjectId[0]||MdoApplicationProductService(Method))return true;
    xwork_error Error;*Lease=MdoProjectLeaseAcquire(C->ProjectId,MDO_PROJECT_LEASE_SHARED,&Error);
    if(!*Lease)return MdoApplicationError(E,"sdk_project_busy",Error.sMessage,"operation",409,false);
    /* The HTTP/command context may have been captured before a project was
     * replaced. Never publish a job against a different physical workspace. */
    if(strcmp(C->ProjectId,"default")){
        MdoProjectBinding Binding={0};Binding.Size=sizeof(Binding);xfileinfo Expected={0};
        bool Ok=MdoProjectBindingGet(C->ProjectId,&Binding,&Error)&&C->Workspace&&C->Workspace[0]&&
            xrtPathStat(C->Workspace,true,&Expected)&&Expected.Type==XFILE_TYPE_DIRECTORY&&
            Expected.Device==Binding.WorkspaceIdentity.Device&&Expected.Identity==Binding.WorkspaceIdentity.Identity;
        if(!Ok){MdoProjectLeaseRelease(*Lease);*Lease=NULL;return MdoApplicationError(E,"sdk_project_changed","Project or workspace changed before the operation was admitted","operation",409,false);}
    }
    return true;
}
static void SdkJobPath(cstr Id,char P[128]){snprintf(P,128,"data/application/operations/%s.json",Id);}
static bool SdkJobSave(SdkJob* J){
    char P[128];SdkJobPath(J->Id,P);char* Text=SdkJson(J->Info);bool Ok=Text&&MdoHomeAtomicWrite(P,Text,strlen(Text),false);xrtFree(Text);return Ok;
}
static xvalue* SdkJobReceipt(cstr Id,bool* Missing){
    char P[128];SdkJobPath(Id,P);char* Text=MdoExtensionRead(P,true,65536,Missing);xvalue* V=SdkParse(Text);xrtFree(Text);
    if(V&&SdkField(V,"id")&&!strcmp(SdkField(V,"id"),Id)&&SdkField(V,"request_hash")){
        bool Terminal=false;xrtValueGetBool(SdkGet(V,"terminal"),&Terminal);
        if(!Terminal){SdkPut(V,"state","interrupted");xrtValueObjectSetNew(V,XRT_STR_LITERAL("terminal"),xrtValueBool(true));}
        xrtValueObjectSetNew(V,XRT_STR_LITERAL("result_available"),xrtValueBool(false));return V;
    }
    xrtValueRelease(V);return NULL;
}
static void SdkJobDispose(SdkJob* J){
    if(J->Thread){xrtThreadWait(J->Thread);xrtThreadDestroy(J->Thread);}
    xrtFree(J->Extension);xrtFree(J->Project);xrtFree(J->Session);xrtFree(J->Run);xrtFree(J->Workspace);xrtFree(J->Method);
    xrtValueRelease(J->Params);xrtValueRelease(J->Info);xrtValueRelease(J->Result);
    if(J->Json)xrtSecureZero(J->Json,J->ResultSize);
    xrtFree(J->Json);xrtCancelDestroy(J->Cancel);MdoModuleCatalogRelease(J->Modules);MdoProjectLeaseRelease(J->Lease);
}
static int32 SdkJobRun(void* User){
    SdkJob* J=User;MdoApplicationInvocation I={0};I.Cancel=J->Cancel;I.Catalog=J->Modules;snprintf(I.WriteToken,sizeof(I.WriteToken),"%s",J->WriteToken);
    mdo_sdk_context_v1 C;MdoApplicationContext(&C,&I,J->Extension,J->Id,J->Project,J->Session,J->Run,J->Workspace,J->Deadline);
    mdo_error_v1 E={0};xvalue* Result=NULL;mdo_result Code=MdoApplicationCall(&C,J->Method,J->Params,&Result,&E);
    char* Json=Result?SdkJson(Result):NULL;size_t Size=Json?strlen(Json):0;
    if(Size>16u*1024u*1024u){MdoApplicationError(&E,"sdk_result_limit","Result exceeded 16 MiB; use range reads or streams","result",413,true);Code=MDO_RESULT_LIMIT;xrtValueRelease(Result);Result=NULL;xrtSecureZero(Json,Size);xrtFree(Json);Json=NULL;Size=0;}
    xrtMutexLock(SdkJobs.Lock);J->Result=Result;J->Json=Json;J->ResultSize=Size;
    SdkPut(J->Info,"state",Code==MDO_RESULT_OK?"succeeded":E.Status==499?"cancelled":"failed");
    xrtValueObjectSetNew(J->Info,XRT_STR_LITERAL("terminal"),xrtValueBool(true));SdkPutUInt(J->Info,"finished_us",MdoMonotonicUs());
    SdkPutUInt(J->Info,"result_bytes",Size);xrtValueObjectSetNew(J->Info,XRT_STR_LITERAL("result_available"),xrtValueBool(Result!=NULL));
    if(Code!=MDO_RESULT_OK){xvalue* Error=xrtValueObject();SdkPut(Error,"code",E.Code);SdkPut(Error,"message",E.Message);SdkPut(Error,"stage",E.Stage);SdkPutUInt(Error,"status",E.Status);
        SdkPut(Error,"cause",E.Cause);
        xrtValueObjectSetNew(Error,XRT_STR_LITERAL("may_have_completed"),xrtValueBool(E.MayHaveCompleted));xrtValueObjectSetNew(J->Info,XRT_STR_LITERAL("error"),Error);}
    if(!SdkJobSave(J))xrtValueObjectSetNew(J->Info,XRT_STR_LITERAL("receipt_uncertain"),xrtValueBool(true));
    xvalue* Event=xrtValueClone(J->Info);xrtMutexUnlock(SdkJobs.Lock);
    MdoApplicationEvent("operation.finished",&C,Event);xrtValueRelease(Event);
    MdoProjectLeaseRelease(J->Lease);J->Lease=NULL;
    xrtMutexLock(SdkJobs.Lock);J->Terminal=true;xrtMutexUnlock(SdkJobs.Lock);return 0;
}
bool MdoApplicationJobsInit(void){SdkJobs.Lock=xrtMutexCreate();return SdkJobs.Lock!=NULL;}
void MdoApplicationJobsStop(void){
    if(!SdkJobs.Lock)return;
    xrtMutexLock(SdkJobs.Lock);SdkJobs.Stopping=true;
    for(size_t i=0;i<SDK_JOBS;i++)if(SdkJobs.Jobs[i].Cancel)xrtCancelRequest(SdkJobs.Jobs[i].Cancel);
    xrtMutexUnlock(SdkJobs.Lock);
    for(size_t i=0;i<SDK_JOBS;i++){SdkJob* J=&SdkJobs.Jobs[i];if(J->Thread){xrtThreadWait(J->Thread);xrtThreadDestroy(J->Thread);J->Thread=NULL;}}
}
void MdoApplicationJobsUnit(void){
    if(!SdkJobs.Lock)return;
    MdoApplicationJobsStop();for(size_t i=0;i<SDK_JOBS;i++)SdkJobDispose(&SdkJobs.Jobs[i]);
    xrtMutexDestroy(SdkJobs.Lock);memset(&SdkJobs,0,sizeof(SdkJobs));
}
xvalue* MdoApplicationJobsCatalog(void){
    xvalue* A=xrtValueArray();for(size_t i=0;i<sizeof(SdkJobMethods)/sizeof(*SdkJobMethods);i++){
        xvalue* V=xrtValueObject();SdkPut(V,"id",SdkJobMethods[i]);SdkPut(V,"domain","operation");SdkPut(V,"transport","native");xrtValueArrayAppendNew(A,V);
    }return A;
}
bool MdoApplicationJobsCall(const mdo_sdk_context_v1* C,cstr Method,const xvalue* P,xvalue** R,mdo_error_v1* E,bool* Found){
    *Found=false;for(size_t i=0;i<sizeof(SdkJobMethods)/sizeof(*SdkJobMethods);i++)if(!strcmp(Method,SdkJobMethods[i]))*Found=true;
    if(!*Found)return true;
    if(!SdkJobs.Lock)return MdoApplicationError(E,"sdk_unavailable","Operation service unavailable","operation",503,false);
    cstr Id=SdkField(P,"id");char Generated[37];bool Start=!strcmp(Method,"operation.start");
    if(!strcmp(Method,"operation.list")){
        *R=xrtValueArray();xrtMutexLock(SdkJobs.Lock);for(size_t i=0;i<SDK_JOBS;i++)if(SdkJobs.Jobs[i].Id[0])xrtValueArrayAppendNew(*R,xrtValueClone(SdkJobs.Jobs[i].Info));xrtMutexUnlock(SdkJobs.Lock);return true;
    }
    if(Start&&!Id){if(!SdkRandomId("sdk-",Generated))return false;Id=Generated;}
    if(!SdkOperationId(Id))return MdoApplicationError(E,"sdk_arguments_invalid","An sdk- operation identity with 32 hexadecimal digits is required","operation",400,false);
    cstr Target=SdkField(P,"method");char Hash[65]={0};
    if(Start){
        if(!SdkIdValid(Target)||!strncmp(Target,"operation.",10))return MdoApplicationError(E,"sdk_arguments_invalid","Supply a non-operation service method","operation",400,false);
        xvalue* Admission=xrtValueObject();SdkPut(Admission,"method",Target);SdkPut(Admission,"extension",C->ExtensionId);SdkPut(Admission,"project",C->ProjectId);SdkPut(Admission,"session",C->SessionId);SdkPut(Admission,"run",C->RunId);SdkPut(Admission,"workspace",C->Workspace);
        const xvalue* Params=SdkGet(P,"params");xrtValueObjectSetNew(Admission,XRT_STR_LITERAL("params"),Params&&xrtValueType(Params)!=XVALUE_NULL?xrtValueClone(Params):xrtValueObject());
        xvalue* Canonical=SdkCanonicalValue(Admission,0);char* Text=Canonical?SdkJson(Canonical):NULL;xrtValueRelease(Canonical);xrtValueRelease(Admission);
        bool Ok=Text&&MdoExtensionHash(Text,Hash);if(Text)xrtSecureZero(Text,strlen(Text));xrtFree(Text);
        if(!Ok)return MdoApplicationError(E,"sdk_arguments_invalid","Operation parameters must be serializable JSON with at most 64 nested levels","operation",400,false);
    }
    uint64 Wait=0;if(!strcmp(Method,"operation.wait")){if(SdkGet(P,"timeout_ms")&&!SdkUInt(SdkGet(P,"timeout_ms"),&Wait))return MdoApplicationError(E,"sdk_arguments_invalid","Invalid wait timeout","operation",400,false);if(Wait>60000)Wait=60000;}
    uint64 End=MdoDeadlineAfterUs(Wait*1000);
    for(;;){
        xrtMutexLock(SdkJobs.Lock);SdkJob* J=SdkJobFind(Id);
        if(J&&J->Disposing){xrtMutexUnlock(SdkJobs.Lock);return MdoApplicationError(E,"sdk_operation_releasing","Operation is being released","operation",409,false);}
        if(J&&Start&&strcmp(Hash,J->Hash)){xrtMutexUnlock(SdkJobs.Lock);return MdoApplicationError(E,"sdk_identity_conflict","Operation identity already has a different request","operation",409,false);}
        if(!J){
            bool Missing=false;xvalue* Receipt=SdkJobReceipt(Id,&Missing);
            if(Receipt){bool Match=!Start||!strcmp(Hash,SdkField(Receipt,"request_hash"));xrtMutexUnlock(SdkJobs.Lock);if(!Match){xrtValueRelease(Receipt);return MdoApplicationError(E,"sdk_identity_conflict","Persisted identity belongs to another request","operation",409,false);}if(!strcmp(Method,"operation.result")){xrtValueRelease(Receipt);return MdoApplicationError(E,"sdk_result_expired","Result was released or belongs to a previous process; the admission receipt remains","operation",410,false);}*R=Receipt;return true;}
            if(!Missing||!Start){xrtMutexUnlock(SdkJobs.Lock);return MdoApplicationError(E,Missing?"sdk_operation_missing":"sdk_receipt_unreadable",Missing?"Operation is not retained":"Cannot read the admission receipt; inspect before repeating work","operation",Missing?404:409,!Missing);}
            if(SdkJobs.Stopping){xrtMutexUnlock(SdkJobs.Lock);return MdoApplicationError(E,"sdk_stopping","Application is stopping","operation",503,false);}
            size_t Active=0;for(size_t i=0;i<SDK_JOBS;i++){if(SdkJobs.Jobs[i].Id[0]&&!SdkJobs.Jobs[i].Terminal)Active++;if(!SdkJobs.Jobs[i].Id[0]&&!J)J=&SdkJobs.Jobs[i];}
            if(!J||Active>=SDK_ACTIVE_JOBS){xrtMutexUnlock(SdkJobs.Lock);return MdoApplicationError(E,"sdk_operation_limit","Release completed operations or wait for active work","operation",429,false);}
            MdoProjectLease* Lease=NULL;
            if(!SdkJobProject(C,Target,&Lease,E)){xrtMutexUnlock(SdkJobs.Lock);return false;}
            snprintf(J->Id,sizeof(J->Id),"%s",Id);snprintf(J->Hash,sizeof(J->Hash),"%s",Hash);
            J->Lease=Lease;
            J->Extension=xrtStrDup(C->ExtensionId);J->Project=xrtStrDup(C->ProjectId);J->Session=xrtStrDup(C->SessionId);J->Run=xrtStrDup(C->RunId);J->Workspace=xrtStrDup(C->Workspace);J->Method=xrtStrDup(Target);
            J->Params=SdkGet(P,"params")?xrtValueClone(SdkGet(P,"params")):xrtValueObject();J->Cancel=xrtCancelCreate();J->Modules=MdoModuleCatalogSnapshot();J->Deadline=C->Deadline;J->Info=xrtValueObject();
            MdoApplicationInvocation* Caller=C->Opaque;if(Caller)snprintf(J->WriteToken,sizeof(J->WriteToken),"%s",Caller->WriteToken);
            SdkPut(J->Info,"id",Id);SdkPut(J->Info,"request_hash",Hash);SdkPut(J->Info,"method",Target);SdkPut(J->Info,"extension",C->ExtensionId);SdkPut(J->Info,"state","running");SdkPutUInt(J->Info,"started_us",MdoMonotonicUs());
            xrtValueObjectSetNew(J->Info,XRT_STR_LITERAL("terminal"),xrtValueBool(false));
            bool Ok=J->Extension&&J->Project&&J->Session&&J->Run&&J->Workspace&&J->Method&&J->Params&&J->Cancel&&J->Info&&SdkJobSave(J);
            if(Ok)J->Thread=xrtThreadCreate(SdkJobRun,J,0);
            if(!Ok||!J->Thread){MdoProjectLeaseRelease(J->Lease);J->Lease=NULL;J->Terminal=true;SdkPut(J->Info,"state","not_started");xrtValueObjectSetNew(J->Info,XRT_STR_LITERAL("terminal"),xrtValueBool(true));(void)SdkJobSave(J);xrtMutexUnlock(SdkJobs.Lock);return MdoApplicationError(E,"sdk_operation_admission_failed","Operation was not started; inspect its receipt","operation",500,false);}
        }
        if(!strcmp(Method,"operation.cancel")&&!J->Terminal)xrtCancelRequest(J->Cancel);
        if(!strcmp(Method,"operation.release")){
            if(!J->Terminal){xrtMutexUnlock(SdkJobs.Lock);return MdoApplicationError(E,"sdk_operation_busy","Cancel and wait before releasing an operation","operation",409,false);}
            J->Disposing=true;xrtMutexUnlock(SdkJobs.Lock);SdkJobDispose(J);xrtMutexLock(SdkJobs.Lock);memset(J,0,sizeof(*J));xrtMutexUnlock(SdkJobs.Lock);*R=xrtValueObject();return true;
        }
        if(!strcmp(Method,"operation.result")){
            if(!J->Terminal){xrtMutexUnlock(SdkJobs.Lock);return MdoApplicationError(E,"sdk_operation_pending","Operation has not finished","operation",409,false);}
            uint64 Offset=0,Limit=16384;bool Range=SdkGet(P,"offset")||SdkGet(P,"limit");
            if((SdkGet(P,"offset")&&!SdkUInt(SdkGet(P,"offset"),&Offset))||(SdkGet(P,"limit")&&!SdkUInt(SdkGet(P,"limit"),&Limit))||!Limit||Limit>16384||Offset>J->ResultSize){xrtMutexUnlock(SdkJobs.Lock);return MdoApplicationError(E,"sdk_arguments_invalid","Invalid result byte range","operation",400,false);}
            if(!Range&&J->ResultSize<=16384)*R=J->Result?xrtValueClone(J->Result):xrtValueNull();
            else {*R=xrtValueObject();SdkPutUInt(*R,"bytes",J->ResultSize);SdkPut(*R,"id",J->Id);SdkPut(*R,"encoding","utf-8-json");
                if(Range){size_t Size=J->ResultSize-(size_t)Offset;if(Size>Limit)Size=(size_t)Limit;char* B64=SdkBase64(J->Json?J->Json+Offset:"",Size);SdkPut(*R,"base64",B64);xrtFree(B64);SdkPutUInt(*R,"next",Offset+Size);}}
            xrtMutexUnlock(SdkJobs.Lock);return true;
        }
        bool Terminal=J->Terminal;*R=xrtValueClone(J->Info);xrtMutexUnlock(SdkJobs.Lock);
        if(strcmp(Method,"operation.wait")||Terminal||!Wait||MdoDeadlineExpired(End))return *R!=NULL;
        xrtValueRelease(*R);*R=NULL;if(SdkCancelled(C))return MdoApplicationError(E,"sdk_cancelled","Operation wait cancelled; the operation retains its own cancellation handle","operation",499,false);xrtSleep(10);
    }
}
