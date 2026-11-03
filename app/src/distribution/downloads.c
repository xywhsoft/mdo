#include "../../include/mdo/downloads.h"
#include "../../include/mdo/home.h"
#include "../../include/mdo/distribution.h"
#include "../../include/mdo/update.h"
#include "../../include/mdo/extension_service.h"
#include "../../include/mdo/extension_tasks.h"
#include "../../include/mdo/extension_manifest.h"
#include "../../include/mdo/time.h"
#include "../../include/mdo/aria2.h"

#define DL_COUNT 128u
#define DL_RECORD "data/downloads/tasks.json"
struct MdoNativeDownload {
    xvalue* Record;
    bool Active,Paused,Cancelled;
    xcancel *Cancel,*DispatchCancel;
    uint64 SampleAt,SampleBytes,SavedAt;
};
static struct {
    xmutex* Lock;xthread* Thread;xcancel* Cancel;xnetengine* Engine;
    MdoNativeDownload Tasks[DL_COUNT];bool Stopping,StorageFault;
} MdoDownloads;
static cstr DlText(const xvalue* V,cstr K){cstr S=MdoExtensionText(V,K,4096);return S?S:"";}
static bool DlPut(xvalue* V,cstr K,cstr S){return xrtValueObjectSetNew(V,xrtStrView(K),xrtValueString(xrtStrView(S?S:"")));}
static uint64 DlNumber(const xvalue* V,cstr K){uint64 N=0;int64 I=0;const xvalue* F=xrtValueObjectGet(V,xrtStrView(K));if(!xrtValueGetUInt(F,&N)&&xrtValueGetInt(F,&I)&&I>=0)N=(uint64)I;return N;}
static bool DlUInt(xvalue* V,cstr K,uint64 N){return xrtValueObjectSetNew(V,xrtStrView(K),xrtValueUInt(N));}
static bool DlHash(cstr H){if(!H||strlen(H)!=64)return false;for(size_t I=0;I<64;I++)if(!strchr("0123456789abcdef",H[I]))return false;return true;}
static bool DlContext(cstr Source,cstr Identity,const xvalue* C){
    if(xrtValueType(C)!=XVALUE_OBJECT||!Identity||!Identity[0]||strlen(Identity)>512)return false;
    if(!strcmp(Source,"extension"))return strlen(Identity)==32&&MdoExtensionDirectoryId(Identity)&&MdoExtensionDirectoryKind(DlText(C,"kind"))&&strlen(DlText(C,"archive_id"))==32&&MdoExtensionDirectoryId(DlText(C,"archive_id"));
    cstr Path=DlText(C,"path"),Relative=DlText(C,"relative");
    if(strcmp(Path,Identity)||Path[0]!='/'||strchr(Path,'\r')||strchr(Path,'\n')||!MdoExtensionRelativePath(Relative))return false;
    if(!strcmp(Source,"update"))return !strcmp(Relative,"data/update/new.exe")||!strcmp(Relative,"data/update/new.bin")||!strcmp(Relative,"data/update/new.apk");
    size_t Length=strlen(Relative);
    return !strcmp(Source,"transfer")&&!strncmp(Relative,"data/downloads/",15)&&strcmp(Relative,DL_RECORD)&&
        (Length<12||strcmp(Relative+Length-12,".resume.json"));
}
static bool DlState(const MdoNativeDownload* T,cstr S){return T&&T->Record&&!strcmp(DlText(T->Record,"state"),S);}
static bool DlSave(void){
    if(MdoDownloads.StorageFault)return false;
    xvalue* A=xrtValueArray();bool Ok=A!=NULL;
    for(size_t I=0;Ok&&I<DL_COUNT;I++)if(MdoDownloads.Tasks[I].Record)Ok=xrtValueArrayAppend(A,MdoDownloads.Tasks[I].Record);
    char* S=Ok?xrtJsonStringify(A,false,NULL):NULL;Ok=S&&MdoHomeAtomicWrite(DL_RECORD,S,strlen(S),false);
    xrtFree(S);xrtValueRelease(A);return Ok;
}
static xvalue* DlRead(cstr Path){
    xfile F=MdoHomeOpenRead(Path);uint64 N=0;char* S=F&&xrtFileSize(F,&N)&&N&&N<1048576?xrtMalloc((size_t)N+1):NULL;
    bool Ok=S&&xrtReadFull(F,S,(size_t)N,NULL);xrtClose(F);if(Ok)S[N]=0;
    xvalue* R=Ok&&!memchr(S,0,(size_t)N)?xrtJsonParse(xrtStrViewN(S,(size_t)N)):NULL;xrtFree(S);return R;
}
static xvalue* DlInfo(MdoNativeDownload* T){
    xvalue* V=xrtValueClone(T->Record);if(!V)return NULL;
    const xvalue* Context=xrtValueObjectGet(T->Record,XRT_STR_LITERAL("context"));
    if(!strcmp(DlText(T->Record,"source"),"extension")){DlPut(V,"archive_id",DlText(Context,"archive_id"));DlPut(V,"kind",DlText(Context,"kind"));}
    xrtValueObjectRemove(V,XRT_STR_LITERAL("context"));
    xrtValueObjectSetNew(V,XRT_STR_LITERAL("active"),xrtValueBool(T->Active));
    if(!T->Active||T->Paused){DlUInt(V,"speed",0);xrtValueObjectRemove(V,XRT_STR_LITERAL("eta_seconds"));}
    return V;
}
xvalue* MdoDownloadsFind(cstr Source,cstr Identity,cstr Hash){
    if(!MdoDownloads.Lock)return NULL;xvalue* V=NULL;xrtMutexLock(MdoDownloads.Lock);
    for(size_t I=0;I<DL_COUNT;I++){MdoNativeDownload* T=&MdoDownloads.Tasks[I];if(T->Record&&!T->Cancelled&&!strcmp(DlText(T->Record,"source"),Source)&&!strcmp(DlText(T->Record,"identity"),Identity)&&!strcmp(DlText(T->Record,"sha256"),Hash)){V=xrtValueClone(T->Record);break;}}
    xrtMutexUnlock(MdoDownloads.Lock);return V;
}
MdoNativeDownload* MdoDownloadsBegin(cstr Source,cstr Identity,cstr Name,cstr Hash,uint64 Bytes,const xvalue* Context,xcancel* Cancel,char Error[512]){
    if(Error)Error[0]=0;
    if(!MdoDownloads.Lock)return NULL;
    if(!Identity||!Name||strlen(Name)>240||!DlContext(Source,Identity,Context)||!DlHash(Hash)||!Bytes||Bytes>INT64_MAX||!Context||
       (strcmp(Source,"extension")&&strcmp(Source,"update")&&strcmp(Source,"transfer"))){if(Error)snprintf(Error,512,"Invalid native download identity, destination or metadata");return NULL;}
    xrtMutexLock(MdoDownloads.Lock);MdoNativeDownload* T=NULL,*Empty=NULL;
    for(size_t I=0;I<DL_COUNT;I++){
        MdoNativeDownload* C=&MdoDownloads.Tasks[I];if(!C->Record){if(!Empty)Empty=C;continue;}
        if(C->Active&&strcmp(Source,"extension")&&!strcmp(DlText(xrtValueObjectGet(C->Record,XRT_STR_LITERAL("context")),"relative"),DlText(Context,"relative"))){if(Error)snprintf(Error,512,"Another download owns this destination");xrtMutexUnlock(MdoDownloads.Lock);return NULL;}
        if(!strcmp(DlText(C->Record,"source"),Source)&&!strcmp(DlText(C->Record,"identity"),Identity)&&!strcmp(DlText(C->Record,"sha256"),Hash))T=C;
    }
    if(MdoDownloads.Stopping||MdoDownloads.StorageFault||(Cancel&&xrtCancelRequested(Cancel))||(T&&(T->Active||(T->Cancelled&&T->DispatchCancel))))goto failed;
    if(!T)T=Empty;if(!T)goto failed;
    if(!T->Record){char* Id=xrtXidMakeString();T->Record=xrtValueObject();bool Ok=Id&&T->Record&&DlPut(T->Record,"id",Id);xrtFree(Id);if(!Ok)goto failed;}
    T->Cancel=xrtCancelChild(Cancel);if(!T->Cancel)goto failed;
    T->Active=true;T->Paused=false;T->Cancelled=false;T->SampleAt=xllmClockUs();T->SampleBytes=0;T->SavedAt=0;
    bool Ok=DlPut(T->Record,"source",Source)&&DlPut(T->Record,"identity",Identity)&&DlPut(T->Record,"name",Name)&&DlPut(T->Record,"sha256",Hash)&&
        DlPut(T->Record,"state","downloading")&&DlPut(T->Record,"engine","native")&&DlPut(T->Record,"fallback_reason","")&&DlPut(T->Record,"error","")&&DlPut(T->Record,"error_code","")&&DlUInt(T->Record,"bytes",Bytes)&&DlUInt(T->Record,"speed",0)&&
        DlUInt(T->Record,"updated_at",MdoUtcUs())&&xrtValueObjectSet(T->Record,XRT_STR_LITERAL("context"),Context)&&DlSave();
    if(!Ok){T->Active=false;xrtCancelDestroy(T->Cancel);T->Cancel=NULL;goto failed;}
    xrtMutexUnlock(MdoDownloads.Lock);return T;
failed:
    if(T&&!T->Active&&T->DispatchCancel&&xrtCancelRequested(T->DispatchCancel)){xrtCancelDestroy(T->DispatchCancel);T->DispatchCancel=NULL;}
    if(Error)snprintf(Error,512,"Download is already owned, storage is full, or its task record cannot be saved");
    xrtMutexUnlock(MdoDownloads.Lock);return NULL;
}
xcancel* MdoDownloadsCancelToken(MdoNativeDownload* T,xcancel* Fallback){return T?T->Cancel:Fallback;}
bool MdoDownloadsControl(MdoNativeDownload* T,bool* Paused,xcancel* Cancel){
    if(Paused)*Paused=false;if(!T)return !(Cancel&&xrtCancelRequested(Cancel));
    xrtMutexLock(MdoDownloads.Lock);bool Ok=!MdoDownloads.Stopping&&!T->Cancelled&&!(Cancel&&xrtCancelRequested(Cancel));
    if(Paused)*Paused=T->Paused;xrtMutexUnlock(MdoDownloads.Lock);return Ok;
}
bool MdoDownloadsEngine(MdoNativeDownload* T,cstr Engine,cstr Fallback){
    if(!T)return true;xrtMutexLock(MdoDownloads.Lock);
    bool Ok=DlPut(T->Record,"engine",Engine)&&DlPut(T->Record,"fallback_reason",Fallback)&&DlUInt(T->Record,"speed",0)&&DlSave();
    T->SampleAt=xllmClockUs();T->SampleBytes=0;xrtMutexUnlock(MdoDownloads.Lock);return Ok;
}
bool MdoDownloadsIdentity(MdoNativeDownload* T,char Id[33]){
    if(!T||!Id)return false;xrtMutexLock(MdoDownloads.Lock);snprintf(Id,33,"%s",DlText(T->Record,"id"));xrtMutexUnlock(MdoDownloads.Lock);return strlen(Id)==32;
}
bool MdoDownloadsGate(MdoNativeDownload* T,xcancel* Cancel){
    if(!T)return !(Cancel&&xrtCancelRequested(Cancel));
    for(;;){xrtMutexLock(MdoDownloads.Lock);bool Stop=MdoDownloads.Stopping||T->Cancelled||(Cancel&&xrtCancelRequested(Cancel)),Paused=T->Paused;xrtMutexUnlock(MdoDownloads.Lock);
        if(Stop)return false;if(!Paused)return true;xrtSleep(50);}
}
bool MdoDownloadsCancelled(MdoNativeDownload* T){if(!T||!MdoDownloads.Lock)return false;xrtMutexLock(MdoDownloads.Lock);bool C=T->Cancelled;xrtMutexUnlock(MdoDownloads.Lock);return C;}
bool MdoDownloadsProgress(MdoNativeDownload* T,uint64 Done,cstr Phase){
    if(!T)return true;
again:
    xrtMutexLock(MdoDownloads.Lock);
    if(!strcmp(Phase,"verifying")&&T->Paused){xrtMutexUnlock(MdoDownloads.Lock);if(!MdoDownloadsGate(T,T->Cancel))return false;goto again;}
    uint64 Now=xllmClockUs(),Total=DlNumber(T->Record,"bytes");bool Ok=Done<=Total;
    if(Ok){
        DlUInt(T->Record,"received",Done);if(!T->Paused)DlPut(T->Record,"state",Phase);
        uint64 Elapsed=Now-T->SampleAt;
        if(Done<T->SampleBytes||!T->SampleBytes){T->SampleBytes=Done;T->SampleAt=Now;}
        else if(Elapsed>=500000){uint64 Speed=(Done-T->SampleBytes)*1000000/Elapsed;DlUInt(T->Record,"speed",Speed);if(Speed)DlUInt(T->Record,"eta_seconds",(Total-Done)/Speed);T->SampleBytes=Done;T->SampleAt=Now;}
        if(Now-T->SavedAt>=1000000||Done==Total){DlUInt(T->Record,"updated_at",MdoUtcUs());Ok=DlSave();T->SavedAt=Now;}
    }xrtMutexUnlock(MdoDownloads.Lock);return Ok;
}
static bool DlPartial(const xvalue* R,bool RemoveFile){
    const xvalue* C=xrtValueObjectGet(R,XRT_STR_LITERAL("context"));
    bool Ok=true;
    if(!strcmp(DlText(R,"source"),"extension")){if(RemoveFile){xvalue* Out=NULL;char E[512]={0};Ok=MdoExtensionTaskCancel(DlText(C,"archive_id"),&Out,E);xrtValueRelease(Out);}}
    else {
        cstr Relative=DlText(C,"relative");char* Meta=xrtFormat("%s.resume.json",Relative);xvalue* Proof=Meta?DlRead(Meta):NULL;
        /* Old failed versions may share an updater destination with a newer
         * package. Delete only the partial whose durable receipt still matches. */
        if(Proof&&!strcmp(DlText(Proof,"path"),DlText(C,"path"))&&!strcmp(DlText(Proof,"sha256"),DlText(R,"sha256"))){
            bool Exists=false;xfileinfo Info;
            if(RemoveFile)Ok=MdoHomeExternalStat(Relative,&Exists,&Info)&&(!Exists||MdoHomeRemove(Relative,false));
            if(Ok)Ok=MdoHomeRemove(Meta,false);
        }
        xrtValueRelease(Proof);xrtFree(Meta);
    }
    return MdoAriaCleanup(DlText(R,"id"))&&Ok;
}
bool MdoDownloadsEnd(MdoNativeDownload* T,bool Ok,cstr Code,cstr Error,xcancel* Cancel){
    if(!T)return Ok;xrtMutexLock(MdoDownloads.Lock);T->Active=false;bool Discard=T->Cancelled;
    Ok=Ok&&!Discard&&!MdoDownloads.Stopping&&!(Cancel&&xrtCancelRequested(Cancel));
    cstr State=Discard?"cancelled":Ok?"ready":MdoDownloads.Stopping||(Cancel&&xrtCancelRequested(Cancel))?"paused":"failed";
    T->Paused=!strcmp(State,"paused");DlPut(T->Record,"state",State);DlPut(T->Record,"error",Ok||T->Paused||Discard?"":Error);DlPut(T->Record,"error_code",Ok||T->Paused||Discard?"":Code);DlUInt(T->Record,"speed",0);DlUInt(T->Record,"updated_at",MdoUtcUs());
    bool Clean=!(Discard||Ok)||DlPartial(T->Record,Discard);xrtValueObjectSetNew(T->Record,XRT_STR_LITERAL("cleanup_pending"),xrtValueBool(!Clean));
    DlSave();xrtCancelDestroy(T->Cancel);T->Cancel=NULL;xrtCancelDestroy(T->DispatchCancel);T->DispatchCancel=NULL;xrtMutexUnlock(MdoDownloads.Lock);return Ok;
}
xvalue* MdoDownloadsList(void){
    if(MdoDownloads.StorageFault)return NULL;
    xvalue* A=xrtValueArray();if(!MdoDownloads.Lock)return A;xrtMutexLock(MdoDownloads.Lock);
    for(size_t I=0;I<DL_COUNT;I++)if(MdoDownloads.Tasks[I].Record){xvalue* V=DlInfo(&MdoDownloads.Tasks[I]);if(V)xrtValueArrayAppendNew(A,V);}
    xrtMutexUnlock(MdoDownloads.Lock);return A;
}
static int32 DlWorker(void* Unused){
    (void)Unused;
    for(;;){xrtMutexLock(MdoDownloads.Lock);bool Stop=MdoDownloads.Stopping;MdoNativeDownload* T=NULL;xvalue* R=NULL;
        for(size_t I=0;!Stop&&I<DL_COUNT;I++)if(!MdoDownloads.Tasks[I].Active&&DlState(&MdoDownloads.Tasks[I],"queued")){T=&MdoDownloads.Tasks[I];T->DispatchCancel=xrtCancelChild(MdoDownloads.Cancel);DlPut(T->Record,"state","starting");R=xrtValueClone(T->Record);break;}
        xrtMutexUnlock(MdoDownloads.Lock);if(Stop)break;if(!R){xrtSleep(50);continue;}
        const xvalue* C=xrtValueObjectGet(R,XRT_STR_LITERAL("context"));bool Ok=false;char E[512]={0};xvalue* Result=NULL;
        xcancel* Dispatch=T->DispatchCancel;
        if(!strcmp(DlText(R,"source"),"extension"))Ok=MdoExtensionServiceDownload(DlText(C,"kind"),DlText(R,"identity"),Dispatch,&Result,E);
        else if(!strcmp(DlText(R,"source"),"update")){
            MdoUpdateStatus S={0};Ok=MdoUpdateGetStatus(&S)&&!strcmp(S.Hash,DlText(R,"sha256"))&&MdoUpdateResumeDownload(Dispatch);
            if(!Ok)snprintf(E,sizeof(E),"Update metadata changed or another update operation is active; open the update details");
        }else Ok=MdoTransferDownload(MdoDownloads.Engine,DlText(C,"path"),DlText(C,"relative"),DlNumber(R,"bytes"),DlText(R,"sha256"),Dispatch);
        xrtValueRelease(Result);xrtValueRelease(R);xrtMutexLock(MdoDownloads.Lock);
        if(!Ok&&!T->Active){if(DlState(T,"starting")){DlPut(T->Record,"state","failed");DlPut(T->Record,"error_code","resume");DlPut(T->Record,"error",E[0]?E:"Download could not be restarted");DlSave();}xrtCancelDestroy(T->DispatchCancel);T->DispatchCancel=NULL;}
        xrtMutexUnlock(MdoDownloads.Lock);
    }return 0;
}
bool MdoDownloadsAction(cstr Id,cstr Action,char Error[512]){
    if(Error)Error[0]=0;if(!MdoDownloads.Lock||!Id||!Action)return false;xrtMutexLock(MdoDownloads.Lock);MdoNativeDownload* T=NULL;
    for(size_t I=0;I<DL_COUNT;I++)if(MdoDownloads.Tasks[I].Record&&!strcmp(DlText(MdoDownloads.Tasks[I].Record,"id"),Id)){T=&MdoDownloads.Tasks[I];break;}
    bool Ok=T&&!MdoDownloads.Stopping&&!MdoDownloads.StorageFault;
    if(Ok&&!strcmp(Action,"pause")){
        Ok=T->Active&&!DlState(T,"verifying");if(Ok){T->Paused=true;DlPut(T->Record,"state","paused");DlUInt(T->Record,"speed",0);}
    }else if(Ok&&(!strcmp(Action,"resume")||!strcmp(Action,"retry"))){
        Ok=!DlState(T,"ready")&&!DlState(T,"cancelled")&&!DlState(T,"queued")&&!DlState(T,"starting")&&(!T->Active||T->Paused);
        if(Ok){T->Paused=false;T->Cancelled=false;T->SampleAt=xllmClockUs();T->SampleBytes=DlNumber(T->Record,"received");DlPut(T->Record,"state",T->Active?"downloading":"queued");DlPut(T->Record,"error","");DlPut(T->Record,"error_code","");}
    }else if(Ok&&!strcmp(Action,"cancel")){
        Ok=!DlState(T,"ready");if(Ok){T->Cancelled=true;T->Paused=false;if(T->Cancel)xrtCancelRequest(T->Cancel);if(T->DispatchCancel)xrtCancelRequest(T->DispatchCancel);DlPut(T->Record,"state",T->Active?"cancelling":"cancelled");if(!T->Active)DlPartial(T->Record,true);}
    }else if(Ok&&!strcmp(Action,"forget")){
        Ok=!T->Active&&!T->DispatchCancel&&(DlState(T,"ready")||DlState(T,"failed")||DlState(T,"cancelled"));
        if(Ok&&!DlState(T,"ready"))Ok=DlPartial(T->Record,true);
        if(Ok){xrtValueRelease(T->Record);memset(T,0,sizeof(*T));}
    }else Ok=false;
    if(Ok)Ok=DlSave();if(!Ok&&Error)snprintf(Error,512,"Download state changed, the action is unavailable, or its record cannot be saved");xrtMutexUnlock(MdoDownloads.Lock);return Ok;
}
bool MdoDownloadsInit(xnetengine* Engine){
    if(MdoDownloads.Lock)return true;MdoDownloads.Lock=xrtMutexCreate();MdoDownloads.Cancel=xrtCancelCreate();MdoDownloads.Engine=Engine;
    if(!MdoDownloads.Lock||!MdoDownloads.Cancel)return false;
    xvalue* A=DlRead(DL_RECORD);bool Exists=false;xfileinfo Info;size_t N=xrtValueCount(A);
    if(!MdoHomeExternalStat(DL_RECORD,&Exists,&Info)||(Exists&&xrtValueType(A)!=XVALUE_ARRAY)||N>DL_COUNT)MdoDownloads.StorageFault=true;
    if(N>DL_COUNT)N=0;
    for(size_t I=0;I<N;I++){
        const xvalue* R=xrtValueArrayGet(A,I);cstr Id=DlText(R,"id"),Source=DlText(R,"source"),Hash=DlText(R,"sha256");
        if(strlen(Id)!=32||!MdoExtensionDirectoryId(Id)||!DlHash(Hash)||!DlNumber(R,"bytes")||DlNumber(R,"received")>DlNumber(R,"bytes")||strlen(DlText(R,"name"))>240||
           !DlContext(Source,DlText(R,"identity"),xrtValueObjectGet(R,XRT_STR_LITERAL("context")))){MdoDownloads.StorageFault=true;continue;}
        for(size_t J=0;J<I;J++)if(MdoDownloads.Tasks[J].Record&&(!strcmp(Id,DlText(MdoDownloads.Tasks[J].Record,"id"))||
            (!strcmp(Source,DlText(MdoDownloads.Tasks[J].Record,"source"))&&!strcmp(DlText(R,"identity"),DlText(MdoDownloads.Tasks[J].Record,"identity"))&&!strcmp(Hash,DlText(MdoDownloads.Tasks[J].Record,"sha256")))))MdoDownloads.StorageFault=true;
        MdoNativeDownload* T=&MdoDownloads.Tasks[I];T->Record=xrtValueClone(R);if(!T->Record)continue;
        if(!DlState(T,"ready")&&!DlState(T,"cancelled")&&!DlState(T,"failed")){T->Paused=true;DlPut(T->Record,"state","paused");}
        T->Cancelled=DlState(T,"cancelled");DlUInt(T->Record,"speed",0);
    }xrtValueRelease(A);MdoDownloads.Thread=xrtThreadCreate(DlWorker,NULL,0);return MdoDownloads.Thread!=NULL;
}
void MdoDownloadsStop(void){if(!MdoDownloads.Lock)return;xrtMutexLock(MdoDownloads.Lock);MdoDownloads.Stopping=true;xrtCancelRequest(MdoDownloads.Cancel);for(size_t I=0;I<DL_COUNT;I++){if(MdoDownloads.Tasks[I].Cancel)xrtCancelRequest(MdoDownloads.Tasks[I].Cancel);if(MdoDownloads.Tasks[I].DispatchCancel)xrtCancelRequest(MdoDownloads.Tasks[I].DispatchCancel);}xrtMutexUnlock(MdoDownloads.Lock);}
void MdoDownloadsUnit(void){
    if(!MdoDownloads.Lock)return;MdoDownloadsStop();if(MdoDownloads.Thread){xrtThreadWait(MdoDownloads.Thread);xrtThreadDestroy(MdoDownloads.Thread);}
    for(;;){bool Active=false;xrtMutexLock(MdoDownloads.Lock);for(size_t I=0;I<DL_COUNT;I++)Active=Active||MdoDownloads.Tasks[I].Active;xrtMutexUnlock(MdoDownloads.Lock);if(!Active)break;xrtSleep(25);}
    for(size_t I=0;I<DL_COUNT;I++){xrtValueRelease(MdoDownloads.Tasks[I].Record);xrtCancelDestroy(MdoDownloads.Tasks[I].DispatchCancel);}
    xrtCancelDestroy(MdoDownloads.Cancel);xrtMutexDestroy(MdoDownloads.Lock);memset(&MdoDownloads,0,sizeof(MdoDownloads));
}
