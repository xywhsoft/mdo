#include "../../include/mdo/downloads.h"
#include "../../include/mdo/home.h"
#include "../../include/mdo/distribution.h"
#include "../../include/mdo/update.h"
#include "../../include/mdo/extension_service.h"
#include "../../include/mdo/extension_tasks.h"
#include "../../include/mdo/extension_manifest.h"
#include "../../include/mdo/time.h"
#include "../../include/mdo/aria2.h"
#include "../../include/mdo/download_network.h"
#include "../../include/mdo/http_url.h"

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
    if(xrtValueType(C)!=XVALUE_OBJECT||!Identity||!Identity[0]||strlen(Identity)>2048)return false;
    if(!strcmp(Source,"extension"))return strlen(Identity)==32&&MdoExtensionDirectoryId(Identity)&&MdoExtensionDirectoryKind(DlText(C,"kind"))&&strlen(DlText(C,"archive_id"))==32&&MdoExtensionDirectoryId(DlText(C,"archive_id"));
    cstr Path=DlText(C,"path"),Relative=DlText(C,"relative");
    if(strcmp(Path,Identity)||(Path[0]!='/'&&!MdoHttpUrlValid(xrtStrView(Path),false))||strchr(Path,'\r')||strchr(Path,'\n')||!MdoExtensionRelativePath(Relative))return false;
    const xvalue* Hosts=xrtValueObjectGet(C,XRT_STR_LITERAL("redirect_hosts"));
    if(Hosts&&(strcmp(Source,"transfer")||xrtValueType(Hosts)!=XVALUE_ARRAY||xrtValueCount(Hosts)>8))return false;
    for(size_t I=0;I<xrtValueCount(Hosts);I++){xstrview H={0};if(!xrtValueGetString(xrtValueArrayGet(Hosts,I),&H)||!H.Size||H.Size>253||memchr(H.Data,0,H.Size))return false;
        for(size_t J=0;J<H.Size;J++)if(!isalnum((unsigned char)H.Data[J])&&!strchr(".-:[]",H.Data[J]))return false;}
    if(!strcmp(Source,"update"))return !strcmp(Relative,"data/update/new.exe")||!strcmp(Relative,"data/update/new.bin")||!strcmp(Relative,"data/update/new.apk");
    if(Path[0]!='/'&&strncmp(Relative,"data/downloads/assets/",22))return false;
    size_t Length=strlen(Relative);
    return !strcmp(Source,"transfer")&&!strncmp(Relative,"data/downloads/",15)&&strcmp(Relative,DL_RECORD)&&
        (Length<12||strcmp(Relative+Length-12,".resume.json"));
}
static bool DlState(const MdoNativeDownload* T,cstr S){return T&&T->Record&&!strcmp(DlText(T->Record,"state"),S);}
/* Completed raw files remain on disk; unfinished/ready installation packages
 * retain their record until consumed or explicitly cleared. */
static bool DlPrunable(MdoNativeDownload* T){bool Pending=false;return !T->Active&&!T->DispatchCancel&&
    (DlState(T,"cancelled")||DlState(T,"complete")||(DlState(T,"ready")&&!strcmp(DlText(T->Record,"source"),"transfer")))&&
    (!xrtValueGetBool(xrtValueObjectGet(T->Record,XRT_STR_LITERAL("cleanup_pending")),&Pending)||!Pending);}
static void DlPrune(bool Pressure){
    MdoNativeDownload* Oldest=NULL;uint64 Now=MdoUtcUs();
    for(size_t I=0;I<DL_COUNT;I++){MdoNativeDownload* T=&MdoDownloads.Tasks[I];if(!DlPrunable(T))continue;
        uint64 At=DlNumber(T->Record,"updated_at");
        if(At&&Now>At&&Now-At>UINT64_C(2592000000000)){xrtValueRelease(T->Record);memset(T,0,sizeof(*T));}
        else if(!Oldest||At<DlNumber(Oldest->Record,"updated_at"))Oldest=T;
    }
    if(Pressure&&Oldest){xrtValueRelease(Oldest->Record);memset(Oldest,0,sizeof(*Oldest));}
}
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
    if(!strcmp(DlText(T->Record,"source"),"update"))DlUInt(V,"build_id",DlNumber(Context,"build_id"));
    xrtValueObjectRemove(V,XRT_STR_LITERAL("context"));
    xrtValueObjectSetNew(V,XRT_STR_LITERAL("active"),xrtValueBool(T->Active));
    if(!T->Active||T->Paused){DlUInt(V,"speed",0);xrtValueObjectRemove(V,XRT_STR_LITERAL("eta_seconds"));}
    return V;
}
xvalue* MdoDownloadsFind(cstr Source,cstr Identity,cstr Hash){
    if(!MdoDownloads.Lock)return NULL;
    xvalue* V=NULL;
    xrtMutexLock(MdoDownloads.Lock);
    for(size_t I=0;I<DL_COUNT;I++){MdoNativeDownload* T=&MdoDownloads.Tasks[I];if(T->Record&&!T->Cancelled&&!strcmp(DlText(T->Record,"source"),Source)&&!strcmp(DlText(T->Record,"identity"),Identity)&&!strcmp(DlText(T->Record,"sha256"),Hash)){V=xrtValueClone(T->Record);break;}}
    xrtMutexUnlock(MdoDownloads.Lock);return V;
}
MdoNativeDownload* MdoDownloadsBegin(cstr Source,cstr Identity,cstr Name,cstr Hash,uint64 Bytes,const xvalue* Context,xcancel* Cancel,char Error[512]){
    if(Error)Error[0]=0;
    if(!MdoDownloads.Lock)return NULL;
    if(!Identity||!Name||strlen(Name)>240||!DlContext(Source,Identity,Context)||!DlHash(Hash)||!Bytes||Bytes>INT64_MAX||!Context||
       (strcmp(Source,"extension")&&strcmp(Source,"update")&&strcmp(Source,"transfer"))){if(Error)snprintf(Error,512,"Invalid native download identity, destination or metadata");return NULL;}
    xrtMutexLock(MdoDownloads.Lock);MdoNativeDownload* T=NULL,*Empty=NULL;xvalue* Original=NULL;bool Replaced=false,WasPaused=false,WasCancelled=false;
    if(!MdoDownloads.StorageFault)DlPrune(false);
    for(size_t I=0;I<DL_COUNT;I++){
        MdoNativeDownload* C=&MdoDownloads.Tasks[I];if(!C->Record){if(!Empty)Empty=C;continue;}
        if(C->Active&&strcmp(Source,"extension")&&!strcmp(DlText(xrtValueObjectGet(C->Record,XRT_STR_LITERAL("context")),"relative"),DlText(Context,"relative"))){if(Error)snprintf(Error,512,"Another download owns this destination");xrtMutexUnlock(MdoDownloads.Lock);return NULL;}
        if(!strcmp(DlText(C->Record,"source"),Source)&&!strcmp(DlText(C->Record,"identity"),Identity)&&!strcmp(DlText(C->Record,"sha256"),Hash))T=C;
    }
    if(MdoDownloads.Stopping||MdoDownloads.StorageFault||(Cancel&&xrtCancelRequested(Cancel))||(T&&(T->Active||(T->Cancelled&&T->DispatchCancel))))goto failed;
    if(!T&&!Empty){DlPrune(true);for(size_t I=0;I<DL_COUNT;I++)if(!MdoDownloads.Tasks[I].Record){Empty=&MdoDownloads.Tasks[I];break;}}
    if(!T)T=Empty;
    if(!T)goto failed;
    Original=T->Record;WasPaused=T->Paused;WasCancelled=T->Cancelled;T->Record=Original?xrtValueClone(Original):xrtValueObject();Replaced=true;if(!T->Record)goto failed;
    if(!Original){char* Id=xrtXidMakeString();bool Ok=Id&&DlPut(T->Record,"id",Id);xrtFree(Id);if(!Ok)goto failed;}
    T->Cancel=xrtCancelChild(Cancel);if(!T->Cancel)goto failed;
    T->Active=true;T->Paused=false;T->Cancelled=false;T->SampleAt=xllmClockUs();T->SampleBytes=0;T->SavedAt=0;
    bool Ok=DlPut(T->Record,"source",Source)&&DlPut(T->Record,"identity",Identity)&&DlPut(T->Record,"name",Name)&&DlPut(T->Record,"sha256",Hash)&&
        DlPut(T->Record,"state","downloading")&&DlPut(T->Record,"engine","native")&&DlPut(T->Record,"fallback_reason","")&&DlPut(T->Record,"error","")&&DlPut(T->Record,"error_code","")&&DlUInt(T->Record,"bytes",Bytes)&&DlUInt(T->Record,"speed",0)&&
        DlUInt(T->Record,"updated_at",MdoUtcUs())&&xrtValueObjectSet(T->Record,XRT_STR_LITERAL("context"),Context)&&DlSave();
    if(!Ok){T->Active=false;xrtCancelDestroy(T->Cancel);T->Cancel=NULL;goto failed;}
    xrtValueRelease(Original);xrtMutexUnlock(MdoDownloads.Lock);return T;
failed:
    if(Replaced){xrtValueRelease(T->Record);T->Record=Original;T->Paused=WasPaused;T->Cancelled=WasCancelled;}
    if(T&&!T->Active&&T->DispatchCancel&&xrtCancelRequested(T->DispatchCancel)){xrtCancelDestroy(T->DispatchCancel);T->DispatchCancel=NULL;}
    if(Error)snprintf(Error,512,"Download is already owned, storage is full, or its task record cannot be saved");
    xrtMutexUnlock(MdoDownloads.Lock);return NULL;
}
xcancel* MdoDownloadsCancelToken(MdoNativeDownload* T,xcancel* Fallback){return T?T->Cancel:Fallback;}
bool MdoDownloadsControl(MdoNativeDownload* T,bool* Paused,xcancel* Cancel){
    if(Paused)*Paused=false;
    if(!T)return !(Cancel&&xrtCancelRequested(Cancel));
    xrtMutexLock(MdoDownloads.Lock);bool Ok=!MdoDownloads.Stopping&&!T->Cancelled&&!(Cancel&&xrtCancelRequested(Cancel));
    if(Paused)*Paused=T->Paused;
    xrtMutexUnlock(MdoDownloads.Lock);
    return Ok;
}
bool MdoDownloadsEngine(MdoNativeDownload* T,cstr Engine,cstr Fallback){
    if(!T)return true;
    xrtMutexLock(MdoDownloads.Lock);
    bool Ok=DlPut(T->Record,"engine",Engine)&&DlPut(T->Record,"fallback_reason",Fallback)&&DlUInt(T->Record,"speed",0)&&DlSave();
    T->SampleAt=xllmClockUs();T->SampleBytes=0;xrtMutexUnlock(MdoDownloads.Lock);return Ok;
}
bool MdoDownloadsIdentity(MdoNativeDownload* T,char Id[33]){
    if(!T||!Id)return false;
    xrtMutexLock(MdoDownloads.Lock);
    snprintf(Id,33,"%s",DlText(T->Record,"id"));
    xrtMutexUnlock(MdoDownloads.Lock);
    return strlen(Id)==32;
}
bool MdoDownloadsGate(MdoNativeDownload* T,xcancel* Cancel){
    if(!T)return !(Cancel&&xrtCancelRequested(Cancel));
    for(;;){xrtMutexLock(MdoDownloads.Lock);bool Stop=MdoDownloads.Stopping||T->Cancelled||(Cancel&&xrtCancelRequested(Cancel)),Paused=T->Paused;xrtMutexUnlock(MdoDownloads.Lock);
        if(Stop)return false;
        if(!Paused)return true;
        xrtSleep(50);
        }
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
    if(!T)return Ok;
    xrtMutexLock(MdoDownloads.Lock);
    T->Active=false;
    bool Discard=T->Cancelled;
    Ok=Ok&&!Discard&&!MdoDownloads.Stopping&&!(Cancel&&xrtCancelRequested(Cancel));
    cstr State=Discard?"cancelled":Ok?"ready":MdoDownloads.Stopping||(Cancel&&xrtCancelRequested(Cancel))?"paused":"failed";
    T->Paused=!strcmp(State,"paused");DlPut(T->Record,"state",State);DlPut(T->Record,"error",Ok||T->Paused||Discard?"":Error);DlPut(T->Record,"error_code",Ok||T->Paused||Discard?"":Code);DlUInt(T->Record,"speed",0);DlUInt(T->Record,"updated_at",MdoUtcUs());
    bool Clean=!(Discard||Ok)||DlPartial(T->Record,Discard);xrtValueObjectSetNew(T->Record,XRT_STR_LITERAL("cleanup_pending"),xrtValueBool(!Clean));
    if(!DlSave()){Ok=false;DlPut(T->Record,"state","failed");DlPut(T->Record,"error_code","write");DlPut(T->Record,"error","Download finished but its durable record could not be saved");(void)DlSave();}
    xrtCancelDestroy(T->Cancel);T->Cancel=NULL;xrtCancelDestroy(T->DispatchCancel);T->DispatchCancel=NULL;xrtMutexUnlock(MdoDownloads.Lock);return Ok;
}
static void DlReconcile(void){
    /* Read archive receipts outside the download mutex: producers can acquire
     * the archive manager before registering their download. */
    xvalue* Candidates=xrtValueArray();if(!Candidates)return;
    xrtMutexLock(MdoDownloads.Lock);
    for(size_t I=0;I<DL_COUNT;I++){MdoNativeDownload* T=&MdoDownloads.Tasks[I];if(!T->Active&&!T->DispatchCancel&&DlState(T,"ready"))xrtValueArrayAppendNew(Candidates,xrtValueClone(T->Record));}
    xrtMutexUnlock(MdoDownloads.Lock);
    for(size_t I=0;I<xrtValueCount(Candidates);I++){
        const xvalue* R=xrtValueArrayGet(Candidates,I),*C=xrtValueObjectGet(R,XRT_STR_LITERAL("context"));bool Complete=false;
        if(!strcmp(DlText(R,"source"),"update")){uint64 Build=DlNumber(C,"build_id");Complete=Build&&Build<=MdoBuildId();}
        if(!strcmp(DlText(R,"source"),"extension")){char Error[512]={0};xvalue* Archive=MdoExtensionTaskRead(DlText(C,"archive_id"),Error);Complete=Archive&&!strcmp(DlText(Archive,"state"),"complete");xrtValueRelease(Archive);}
        if(!Complete)continue;
        xrtMutexLock(MdoDownloads.Lock);
        for(size_t J=0;J<DL_COUNT;J++){MdoNativeDownload* T=&MdoDownloads.Tasks[J];if(!T->Active&&!T->DispatchCancel&&DlState(T,"ready")&&DlNumber(R,"updated_at")==DlNumber(T->Record,"updated_at")&&!strcmp(DlText(R,"id"),DlText(T->Record,"id"))){
            DlPut(T->Record,"state","complete");if(!DlSave())DlPut(T->Record,"state","ready");break;}}
        xrtMutexUnlock(MdoDownloads.Lock);
    }xrtValueRelease(Candidates);
}
xvalue* MdoDownloadsList(void){
    if(MdoDownloads.StorageFault)return NULL;
    xvalue* A=xrtValueArray();if(!MdoDownloads.Lock)return A;DlReconcile();xrtMutexLock(MdoDownloads.Lock);
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
        }else if(DlText(C,"path")[0]=='/')Ok=MdoTransferDownload(MdoDownloads.Engine,DlText(C,"path"),DlText(C,"relative"),DlNumber(R,"bytes"),DlText(R,"sha256"),Dispatch);
        else {cstr Hosts[8]={0};const xvalue* H=xrtValueObjectGet(C,XRT_STR_LITERAL("redirect_hosts"));size_t Count=xrtValueCount(H);
            for(size_t J=0;J<Count&&J<8;J++){xstrview V={0};xrtValueGetString(xrtValueArrayGet(H,J),&V);Hosts[J]=V.Data;}
            MdoDownloadSource Source={DlText(C,"path"),DlText(C,"relative"),DlText(R,"sha256"),DlNumber(R,"bytes"),Hosts,Count};
            Ok=MdoTransferDownloadSource(MdoDownloads.Engine,&Source,Dispatch,NULL,NULL,NULL);}
        xrtValueRelease(Result);xrtValueRelease(R);xrtMutexLock(MdoDownloads.Lock);
        if(!Ok&&!T->Active){if(DlState(T,"starting")){DlPut(T->Record,"state","failed");DlPut(T->Record,"error_code","resume");DlPut(T->Record,"error",E[0]?E:"Download could not be restarted");DlSave();}xrtCancelDestroy(T->DispatchCancel);T->DispatchCancel=NULL;}
        xrtMutexUnlock(MdoDownloads.Lock);
    }return 0;
}
bool MdoDownloadsAction(cstr Id,cstr Action,char Error[512]){
    if(Error)Error[0]=0;
    if(!MdoDownloads.Lock||!Id||!Action)return false;
    xrtMutexLock(MdoDownloads.Lock);
    MdoNativeDownload* T=NULL;
    for(size_t I=0;I<DL_COUNT;I++)if(MdoDownloads.Tasks[I].Record&&!strcmp(DlText(MdoDownloads.Tasks[I].Record,"id"),Id)){T=&MdoDownloads.Tasks[I];break;}
    bool Ok=T&&!MdoDownloads.Stopping&&!MdoDownloads.StorageFault;
    xvalue* Previous=Ok?xrtValueClone(T->Record):NULL,*Removed=NULL;bool WasPaused=T&&T->Paused;
    if(Ok&&!Previous)Ok=false;
    if(Ok&&!strcmp(Action,"pause")){
        Ok=T->Active&&!DlState(T,"verifying");if(Ok){T->Paused=true;DlPut(T->Record,"state","paused");DlUInt(T->Record,"speed",0);}
    }else if(Ok&&(!strcmp(Action,"resume")||!strcmp(Action,"retry"))){
        Ok=!DlState(T,"ready")&&!DlState(T,"complete")&&!DlState(T,"cancelled")&&!DlState(T,"queued")&&!DlState(T,"starting")&&(!T->Active||T->Paused);
        if(Ok){T->Paused=false;T->Cancelled=false;T->SampleAt=xllmClockUs();T->SampleBytes=DlNumber(T->Record,"received");DlPut(T->Record,"state",T->Active?"downloading":"queued");DlPut(T->Record,"error","");DlPut(T->Record,"error_code","");}
    }else if(Ok&&!strcmp(Action,"cancel")){
        Ok=!DlState(T,"ready")&&!DlState(T,"complete");if(Ok){T->Cancelled=true;T->Paused=false;if(T->Cancel)xrtCancelRequest(T->Cancel);if(T->DispatchCancel)xrtCancelRequest(T->DispatchCancel);DlPut(T->Record,"state",T->Active?"cancelling":"cancelled");if(!T->Active)xrtValueObjectSetNew(T->Record,XRT_STR_LITERAL("cleanup_pending"),xrtValueBool(!DlPartial(T->Record,true)));}
    }else if(Ok&&!strcmp(Action,"cleanup")){
        Ok=!T->Active&&!T->DispatchCancel&&(DlState(T,"cancelled")||DlState(T,"ready")||DlState(T,"complete"));
        if(Ok){bool Clean=DlPartial(T->Record,DlState(T,"cancelled"));xrtValueObjectSetNew(T->Record,XRT_STR_LITERAL("cleanup_pending"),xrtValueBool(!Clean));}
    }else if(Ok&&!strcmp(Action,"forget")){
        Ok=!T->Active&&!T->DispatchCancel&&(DlState(T,"ready")||DlState(T,"complete")||DlState(T,"failed")||DlState(T,"cancelled"));
        if(Ok&&!DlState(T,"ready")&&!DlState(T,"complete"))Ok=DlPartial(T->Record,true);
        if(Ok){Removed=T->Record;T->Record=NULL;}
    }else Ok=false;
    if(Ok&&!DlSave()){
        Ok=false;
        if(Removed){T->Record=Removed;Removed=NULL;}
        else if(strcmp(Action,"cancel")&&strcmp(Action,"cleanup")){xrtValueRelease(T->Record);T->Record=Previous;Previous=NULL;T->Paused=WasPaused;}
    }
    if(Removed){xrtValueRelease(Removed);memset(T,0,sizeof(*T));}xrtValueRelease(Previous);
    if(!Ok&&Error)snprintf(Error,512,"Download state changed, the action is unavailable, or its record cannot be saved");
    xrtMutexUnlock(MdoDownloads.Lock);
    return Ok;
}
bool MdoDownloadsInit(xnetengine* Engine){
    MdoDownloadNetworkSetEngine(Engine);
    if(MdoDownloads.Lock)return true;
    MdoDownloads.Lock=xrtMutexCreate();
    MdoDownloads.Cancel=xrtCancelCreate();
    MdoDownloads.Engine=Engine;
    if(!MdoDownloads.Lock||!MdoDownloads.Cancel)return false;
    xvalue* A=DlRead(DL_RECORD);bool Exists=false;xfileinfo Info;size_t N=xrtValueCount(A);
    bool Repair=Exists&&xrtValueType(A)!=XVALUE_ARRAY;size_t Loaded=0;
    if(!MdoHomeExternalStat(DL_RECORD,&Exists,&Info))MdoDownloads.StorageFault=true;
    Repair=Exists&&xrtValueType(A)!=XVALUE_ARRAY;
    for(size_t I=0;I<N;I++){
        const xvalue* R=xrtValueArrayGet(A,I);cstr Id=DlText(R,"id"),Source=DlText(R,"source"),Hash=DlText(R,"sha256");
        if(strlen(Id)!=32||!MdoExtensionDirectoryId(Id)||!DlHash(Hash)||!DlNumber(R,"bytes")||DlNumber(R,"received")>DlNumber(R,"bytes")||strlen(DlText(R,"name"))>240||
           !DlContext(Source,DlText(R,"identity"),xrtValueObjectGet(R,XRT_STR_LITERAL("context")))){Repair=true;continue;}
        bool Duplicate=false;
        for(size_t J=0;J<Loaded;J++)if(!strcmp(Id,DlText(MdoDownloads.Tasks[J].Record,"id"))||
            (!strcmp(Source,DlText(MdoDownloads.Tasks[J].Record,"source"))&&!strcmp(DlText(R,"identity"),DlText(MdoDownloads.Tasks[J].Record,"identity"))&&!strcmp(Hash,DlText(MdoDownloads.Tasks[J].Record,"sha256"))))Duplicate=true;
        if(Duplicate){Repair=true;continue;}
        if(Loaded==DL_COUNT){MdoDownloads.StorageFault=true;break;}
        MdoNativeDownload* T=&MdoDownloads.Tasks[Loaded];T->Record=xrtValueClone(R);if(!T->Record){MdoDownloads.StorageFault=true;break;}Loaded++;
        if(!DlState(T,"ready")&&!DlState(T,"complete")&&!DlState(T,"cancelled")&&!DlState(T,"failed")){T->Paused=true;DlPut(T->Record,"state","paused");}
        T->Cancelled=DlState(T,"cancelled");DlUInt(T->Record,"speed",0);
    }xrtValueRelease(A);
    /* Preserve the entire original before isolating damaged rows. A failed
     * quarantine keeps storage read-only rather than overwriting evidence. */
    if(Repair&&!MdoDownloads.StorageFault){char* Backup=xrtFormat("data/downloads/tasks.rejected-%llu.json",(unsigned long long)MdoUtcUs());
        MdoDownloads.StorageFault=!Backup||!MdoHomeRenameNoReplace(DL_RECORD,Backup);xrtFree(Backup);
        if(!MdoDownloads.StorageFault&&!DlSave())MdoDownloads.StorageFault=true;}
    MdoDownloads.Thread=xrtThreadCreate(DlWorker,NULL,0);return MdoDownloads.Thread!=NULL;
}
void MdoDownloadsStop(void){if(!MdoDownloads.Lock)return;xrtMutexLock(MdoDownloads.Lock);MdoDownloads.Stopping=true;xrtCancelRequest(MdoDownloads.Cancel);for(size_t I=0;I<DL_COUNT;I++){if(MdoDownloads.Tasks[I].Cancel)xrtCancelRequest(MdoDownloads.Tasks[I].Cancel);if(MdoDownloads.Tasks[I].DispatchCancel)xrtCancelRequest(MdoDownloads.Tasks[I].DispatchCancel);}xrtMutexUnlock(MdoDownloads.Lock);}
void MdoDownloadsUnit(void){
    if(!MdoDownloads.Lock)return;
    MdoDownloadsStop();
    if(MdoDownloads.Thread){xrtThreadWait(MdoDownloads.Thread);
    xrtThreadDestroy(MdoDownloads.Thread);
    }
    for(;;){bool Active=false;xrtMutexLock(MdoDownloads.Lock);for(size_t I=0;I<DL_COUNT;I++)Active=Active||MdoDownloads.Tasks[I].Active;xrtMutexUnlock(MdoDownloads.Lock);if(!Active)break;xrtSleep(25);}
    for(size_t I=0;I<DL_COUNT;I++){xrtValueRelease(MdoDownloads.Tasks[I].Record);xrtCancelDestroy(MdoDownloads.Tasks[I].DispatchCancel);}
    xrtCancelDestroy(MdoDownloads.Cancel);xrtMutexDestroy(MdoDownloads.Lock);memset(&MdoDownloads,0,sizeof(MdoDownloads));
}
