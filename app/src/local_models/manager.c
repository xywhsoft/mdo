#include "../../include/mdo/local_models.h"
#include "../../include/mdo/local_model_plan.h"
#include "../../include/mdo/home.h"
#include "../../include/mdo/extensions.h"
#include "../../include/mdo/extension_service.h"
#include "../../include/mdo/downloads.h"
#include "../../include/mdo/distribution.h"
#include "../../include/mdo/time.h"

#define LM_STATE "data/local-models/state.json"
#define LM_STATE_LIMIT (16u*1024u*1024u)
#define LM_LIMIT 128u
typedef struct LmStoreLock {char Id[33];char* Path;xfile File;} LmStoreLock;
typedef struct LmPin {char Id[33];MdoExtensionLease *Recipe,*Runtime;} LmPin;
static struct {xmutex *Lock,*Writer;xthread* Thread;xcancel *Stop,*ActiveCancel;xnetengine* Engine;xvalue* State;bool Fault,Stopping;char Active[33],Owner[33];LmPin Pins[LM_LIMIT];LmStoreLock Stores[LM_LIMIT];} g_Lm;
static const xvalue* LmGet(const xvalue* V,cstr K){return xrtValueObjectGet(V,xrtStrView(K));}
static cstr LmStr(const xvalue* V,cstr K){cstr S=MdoExtensionText(V,K,4096);return S?S:"";}
static bool LmPut(xvalue* V,cstr K,cstr S){return xrtValueObjectSetNew(V,xrtStrView(K),xrtValueString(xrtStrView(S?S:"")));}
static bool LmFail(char E[512],cstr S){if(E)snprintf(E,512,"%s",S);return false;}
static uint64 LmUInt(const xvalue* V,cstr K){int64 N=0;MdoExtensionNumber(V,K,&N);return N>0?(uint64)N:0;}
static bool LmXid(cstr Id){if(!Id||strlen(Id)!=32)return false;for(size_t I=0;I<32;I++)if(!strchr("-0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ_abcdefghijklmnopqrstuvwxyz",Id[I]))return false;return true;}
static bool LmSaveJson(cstr P,const xvalue* V){char* S=xrtJsonStringify(V,false,NULL);bool Ok=S&&strlen(S)<=LM_STATE_LIMIT&&MdoHomeAtomicWrite(P,S,strlen(S),false);xrtFree(S);return Ok;}
static xvalue* LmReadJson(cstr P){char* S=MdoExtensionRead(P,true,LM_STATE_LIMIT,NULL);xvalue* V=S?xrtJsonParse(xrtStrView(S)):NULL;xrtFree(S);return V;}
static bool LmSave(void){return !g_Lm.Fault&&LmSaveJson(LM_STATE,g_Lm.State);}
static xvalue* LmEdit(cstr Section,cstr Id){xvalue* A=xrtValueObjectEdit(g_Lm.State,xrtStrView(Section));return A?xrtValueObjectEdit(A,xrtStrView(Id)):NULL;}
static bool LmPublish(xvalue* Before){bool Ok=LmSave();if(Ok)xrtValueRelease(Before);else{xrtValueRelease(g_Lm.State);g_Lm.State=Before;}return Ok;}
static bool LmCommit(xvalue* Before,bool Ready){if(!Before)return false;if(Ready)return LmPublish(Before);xrtValueRelease(g_Lm.State);g_Lm.State=Before;return false;}
static bool LmJobState(cstr Id,cstr State,cstr Error){xrtMutexLock(g_Lm.Lock);xvalue* Before=xrtValueClone(g_Lm.State),*J=LmEdit("installations",Id);bool Ok=Before&&J&&LmPut(J,"state",State)&&LmPut(J,"error",Error)&&xrtValueObjectSetNew(J,XRT_STR_LITERAL("updated_at"),xrtValueUInt(MdoUtcUs()));Ok=LmCommit(Before,Ok);xrtMutexUnlock(g_Lm.Lock);return Ok;}
static xvalue* LmJob(cstr Id){xrtMutexLock(g_Lm.Lock);const xvalue* J=LmGet(LmGet(g_Lm.State,"installations"),Id);xvalue* Out=J?xrtValueClone(J):NULL;xrtMutexUnlock(g_Lm.Lock);return Out;}
static xfile LmFile(xroot R,cstr P,uint32 Flags){xfileoptions O;xrtFileOptionsInit(&O);O.Flags=Flags|XFILE_NOFOLLOW;return R?xrtRootFileOpen(R,P,&O):NULL;}
static bool LmHash(xfile F,uint64 Bytes,char Hash[65],xcancel* Cancel,xfile Copy){
 uint64 Size=0;if(!F||!xrtFileSize(F,&Size)||Size!=Bytes||!xrtSeek(F,0,XSEEK_START,NULL))return false;
 xsha256 S;xrtSha256Init(&S);uint8 Buffer[65536],Digest[32];uint64 At=0;bool Ok=true;
 while(Ok&&At<Bytes){size_t N=(size_t)(Bytes-At>sizeof(Buffer)?sizeof(Buffer):Bytes-At);Ok=(!Cancel||!xrtCancelRequested(Cancel))&&xrtReadFull(F,Buffer,N,NULL)&&xrtSha256Update(&S,Buffer,N)&&(!Copy||xrtWriteFull(Copy,Buffer,N,NULL));At+=N;}
 Ok=Ok&&xrtSha256Final(&S,Digest);if(Ok)for(size_t I=0;I<32;I++)snprintf(Hash+I*2,3,"%02x",Digest[I]);return Ok;
}
#include "state.inc.c"
static bool LmBlob(xroot R,cstr Hash,uint64 Bytes,xcancel* Cancel){char* P=xrtFormat("blobs/%s",Hash);xfile F=P?LmFile(R,P,XFILE_READ):NULL;char Actual[65];bool Ok=F&&LmHash(F,Bytes,Actual,Cancel,NULL)&&!strcmp(Hash,Actual);xrtClose(F);xrtFree(P);return Ok;}
static xvalue* LmStore(cstr Id){xrtMutexLock(g_Lm.Lock);const xvalue* R=LmGet(LmGet(g_Lm.State,"roots"),Id);xvalue* Out=R?xrtValueClone(R):NULL;xrtMutexUnlock(g_Lm.Lock);return Out;}
static xroot LmRoot(const xvalue* Store,char** Native,char E[512]){
 *Native=MdoPathResolve(LmStr(Store,"path"));xroot R=*Native?xrtRootOpen(*Native):NULL;
 xfile F=R?LmFile(R,"mdo-model-store.json",XFILE_READ):NULL;char B[256]={0};uint64 N=0;bool Ok=F&&xrtFileSize(F,&N)&&N<sizeof(B)&&xrtReadFull(F,B,(size_t)N,NULL);xrtClose(F);
 xvalue* Marker=Ok?xrtJsonParse(xrtStrViewN(B,(size_t)N)):NULL;Ok=Ok&&!strcmp(LmStr(Marker,"id"),LmStr(Store,"id"))&&!strcmp(LmStr(Marker,"owner"),g_Lm.Owner);xrtValueRelease(Marker);
 if(Ok)Ok=LmStoreLease(R,LmStr(Store,"id"),*Native,E);
 if(!Ok){xrtRootClose(R);xrtFree(*Native);*Native=NULL;if(!E[0])LmFail(E,"The selected resource store is missing or has a different ownership marker");return NULL;}return R;
}
static bool LmNewStore(cstr Path,xvalue** Result,char E[512]){
 *Result=NULL;char* Native=Path&&Path[0]?MdoPathResolve(Path):MdoHomeExternalPath("data/local-models/resources");
 if(!Native||!xrtPathIsAbs(Native)){xrtFree(Native);return LmFail(E,"Select an absolute resource directory");}
 if(!xrtDirExists(Native)&&!xrtDirCreateAll(Native)){xrtFree(Native);return LmFail(E,"Cannot create the selected resource directory");}
 xroot R=xrtRootOpen(Native);xfile F=R?LmFile(R,"mdo-model-store.json",XFILE_READ):NULL;char B[256]={0};uint64 N=0;char* Id=NULL;bool Existing=F!=NULL;
 if(F){if(xrtFileSize(F,&N)&&N<sizeof(B)&&xrtReadFull(F,B,(size_t)N,NULL)){xvalue* V=xrtJsonParse(xrtStrViewN(B,(size_t)N));if(LmXid(LmStr(V,"id"))&&!strcmp(LmStr(V,"owner"),g_Lm.Owner))Id=xrtStrDup(LmStr(V,"id"));xrtValueRelease(V);}xrtClose(F);}
 else if(R){xdir D=xrtRootDirOpen(R,".",XDIR_STAT);xdirentry Entry;bool Empty=D&&xrtDirNext(D,&Entry)==XDIR_NEXT_END;if(D)xrtDirClose(D);if(Empty)Id=xrtXidMakeString();}
 bool Ok=R&&Id;if(Ok&&!Existing){char* Text=xrtFormat("{\"id\":\"%s\",\"owner\":\"%s\",\"schema\":\"mdo.model-store.v1\"}",Id,g_Lm.Owner);F=LmFile(R,"mdo-model-store.json",XFILE_WRITE|XFILE_CREATE|XFILE_EXCLUSIVE);Ok=Text&&F&&xrtWriteFull(F,Text,strlen(Text),NULL)&&xrtFlush(F);xrtClose(F);xrtFree(Text);}
 const cstr Dirs[]={"partial","blobs"};for(size_t I=0;Ok&&I<2;I++){xfileinfo Info;if(!xrtRootStat(R,Dirs[I],false,&Info))Ok=xrtRootDirCreate(R,Dirs[I],0700);else Ok=Info.Type==XFILE_TYPE_DIRECTORY;}
 if(Ok)Ok=LmStoreLease(R,Id,Native,E);
 xvalue* Store=Ok?xrtValueObject():NULL;char* Stored=Native?MdoPathStore(Native):NULL;Ok=Ok&&Store&&Stored&&LmPut(Store,"id",Id)&&LmPut(Store,"path",Stored);
 if(Ok){xrtMutexLock(g_Lm.Lock);xvalue* Before=xrtValueClone(g_Lm.State),*Roots=xrtValueObjectEdit(g_Lm.State,XRT_STR_LITERAL("roots"));Ok=Before&&Roots&&(xrtValueCount(Roots)<LM_LIMIT||LmGet(Roots,Id))&&xrtValueObjectSet(Roots,xrtStrView(Id),Store)&&LmPut(g_Lm.State,"default_root",Id);Ok=LmCommit(Before,Ok);xrtMutexUnlock(g_Lm.Lock);}
 if(Ok)*Result=Store;else{xrtValueRelease(Store);if(!E[0])LmFail(E,"Use an empty directory or an existing mdo model resource store");}xrtRootClose(R);xrtFree(Stored);xrtFree(Id);xrtFree(Native);return Ok;
}
static bool LmPins(cstr Id,const xvalue* Job,char E[512]){
 const xvalue* Plan=LmGet(Job,"plan"),*P=LmGet(Plan,"profile"),*D=LmGet(Plan,"document"),*R=LmFind(LmGet(D,"runtimes"),LmStr(P,"runtime")),*Recipe=LmGet(Plan,"recipe");
 if(!Plan)return true;
 xvalue* RuntimeRow=MdoExtensionsResolve(LmStr(R,"ref"),E),*RecipeRow=MdoExtensionsResolve(LmStr(Recipe,"id"),E);
 bool Match=RuntimeRow&&RecipeRow&&!strcmp(LmStr(LmGet(RuntimeRow,"installation"),"version_id"),LmStr(R,"version_id"))&&!strcmp(LmStr(LmGet(RuntimeRow,"installation"),"sha256"),LmStr(R,"sha256"))&&!strcmp(LmStr(LmGet(RecipeRow,"installation"),"sha256"),LmStr(Recipe,"sha256"));
 xrtValueRelease(RuntimeRow);xrtValueRelease(RecipeRow);if(!Match)return LmFail(E,"The pinned model recipe or runtime version is missing or changed");
 for(size_t I=0;I<LM_LIMIT;I++)if(!strcmp(g_Lm.Pins[I].Id,Id))return true;
 for(size_t I=0;I<LM_LIMIT;I++)if(!g_Lm.Pins[I].Id[0]){LmPin* Pin=&g_Lm.Pins[I];char* Dir=MdoExtensionsDirectoryId("external-tools",LmStr(R,"ref"),E);Pin->Runtime=Dir?MdoExtensionsAcquire("external-tools",Dir,E):NULL;xrtFree(Dir);Pin->Recipe=MdoExtensionsAcquire("local-models",LmStr(Recipe,"xid"),E);if(!Pin->Runtime||!Pin->Recipe){MdoExtensionsRelease(Pin->Runtime);MdoExtensionsRelease(Pin->Recipe);memset(Pin,0,sizeof(*Pin));return false;}snprintf(Pin->Id,33,"%s",Id);return true;}return LmFail(E,"Too many pinned deployments");
}
static void LmUnpin(cstr Id){for(size_t I=0;I<LM_LIMIT;I++)if(!strcmp(g_Lm.Pins[I].Id,Id)){MdoExtensionsRelease(g_Lm.Pins[I].Recipe);MdoExtensionsRelease(g_Lm.Pins[I].Runtime);memset(&g_Lm.Pins[I],0,sizeof(g_Lm.Pins[I]));}}
static bool LmRuntime(const xvalue* J,xcancel* Cancel,char E[512]){
 const xvalue* P=LmGet(J,"plan"),*Profile=LmGet(P,"profile"),*D=LmGet(P,"document"),*R=LmFind(LmGet(D,"runtimes"),LmStr(Profile,"runtime"));
 xvalue* Row=MdoExtensionsResolve(LmStr(R,"ref"),E);const xvalue* Installation=LmGet(Row,"installation");bool Same=Row&&!strcmp(LmStr(Installation,"version_id"),LmStr(R,"version_id"))&&!strcmp(LmStr(Installation,"sha256"),LmStr(R,"sha256"));xrtValueRelease(Row);
 if(Same)return true;E[0]=0;const xvalue* Native=LmGet(J,"runtime_plan");xvalue* Request=xrtValueObject(),*Receipt=NULL;bool Ok=Request&&LmPut(Request,"plan_id",LmStr(Native,"id"))&&LmPut(Request,"revision",LmStr(Native,"revision"))&&xrtValueObjectSetNew(Request,XRT_STR_LITERAL("trust_code"),xrtValueBool(true))&&MdoExtensionServiceApplyPlan(Request,Cancel,&Receipt,E);xrtValueRelease(Request);xrtValueRelease(Receipt);return Ok;
}
static bool LmRecipe(const xvalue* J,xcancel* Cancel,char E[512]){
 const xvalue* Recipe=LmGet(LmGet(J,"plan"),"recipe");xvalue* Row=MdoExtensionsResolve(LmStr(Recipe,"id"),E);
 bool Same=Row&&!strcmp(LmStr(LmGet(Row,"installation"),"sha256"),LmStr(Recipe,"sha256"));xrtValueRelease(Row);if(Same)return true;E[0]=0;
 xvalue *Task=NULL,*Out=NULL,*Input=xrtValueObject();bool Ok=Input&&MdoExtensionServiceDownload("local-models",LmStr(Recipe,"version_id"),Cancel,&Task,E)&&MdoExtensionServiceInstall(LmStr(Task,"id"),Input,Cancel,&Out,E);xrtValueRelease(Input);xrtValueRelease(Task);xrtValueRelease(Out);return Ok;
}
static bool LmAsset(cstr Id,const xvalue* F,xroot Root,cstr Native,cstr StoreId,xcancel* Cancel,char E[512]){
 cstr Hash=LmStr(F,"sha256");uint64 Bytes=LmUInt(F,"bytes");if(LmBlob(Root,Hash,Bytes,Cancel))return true;
 char* Existing=xrtFormat("blobs/%s",Hash);xfileinfo Info;bool Corrupt=Existing&&xrtRootStat(Root,Existing,false,&Info);xrtFree(Existing);if(Corrupt)return LmFail(E,"An existing shared resource failed verification. Uninstall its dependent models with file cleanup before retrying");
 char* Partial=xrtFormat("partial/%s.part",Hash),*Ledger=xrtFormat("data/downloads/assets/%s/%s.part",StoreId,Hash);const xvalue* Urls=LmGet(F,"urls"),*Hosts=LmGet(F,"redirect_hosts");cstr Allowed[8]={0};for(size_t I=0;I<xrtValueCount(Hosts);I++){xstrview V={0};xrtValueGetString(xrtValueArrayGet(Hosts,I),&V);Allowed[I]=V.Data;}
 xstrview First={0};xrtValueGetString(xrtValueArrayGet(Urls,0),&First);xvalue* Context=xrtValueObject();bool Ok=Context&&Partial&&Ledger&&LmPut(Context,"path",First.Data)&&LmPut(Context,"relative",Ledger)&&LmPut(Context,"local_model_install",Id);
 MdoNativeDownload* Task=Ok?MdoDownloadsBegin("transfer",First.Data,LmStr(F,"path"),Hash,Bytes,Context,Cancel,E):NULL;xrtValueRelease(Context);Ok=Task!=NULL;MdoTransferReport Report={0};
 bool Done=false;for(size_t I=0;Ok&&!Done&&I<xrtValueCount(Urls);I++){xstrview Url={0};xrtValueGetString(xrtValueArrayGet(Urls,I),&Url);MdoDownloadSource Source={Url.Data,Partial,Hash,Bytes,Allowed,xrtValueCount(Hosts)};Done=MdoTransferDownloadManaged(g_Lm.Engine,&Source,Root,Native,Task,MdoDownloadsCancelToken(Task,Cancel),&Report);if(xrtCancelRequested(Cancel)||MdoDownloadsCancelled(Task))break;}
 if(Task&&MdoDownloadsCancelled(Task))xrtCancelRequest(Cancel);
 if(Task)Ok=MdoDownloadsEnd(Task,Done,Report.Code,Report.Detail,Cancel);char* Blob=xrtFormat("blobs/%s",Hash);
 if(Ok)Ok=LmBlob(Root,Hash,Bytes,Cancel)||xrtRootFileRename(Root,Partial,Blob,false);if(!Ok)LmFail(E,Report.Detail[0]?Report.Detail:"Cannot commit the verified model resource");
 if(Ok){char* Resume=xrtFormat("%s.resume.json",Partial);if(Resume)xrtRootRemove(Root,Resume);xrtFree(Resume);}xrtFree(Blob);xrtFree(Partial);xrtFree(Ledger);return Ok;
}
static bool LmImport(cstr Id,const xvalue* J,xroot Root,cstr Native,xcancel* Cancel,char E[512]){
 cstr Path=LmStr(J,"source_path");char* Parent=xrtPathParent(Path);cstr Leaf=strrchr(Path,'/'),Back=strrchr(Path,'\\');if(Back&&(!Leaf||Back>Leaf))Leaf=Back;Leaf=Leaf?Leaf+1:Path;
 xroot InputRoot=Parent?xrtRootOpen(Parent):NULL;xfile In=LmFile(InputRoot,Leaf,XFILE_READ);uint64 Size=0;uint8 Magic[8];bool Ok=In&&xrtFileSize(In,&Size)&&Size>=8&&Size<=MD_LOCAL_ASSET_BYTES&&xrtReadFull(In,Magic,8,NULL)&&!memcmp(Magic,"GGUF",4)&&(Magic[4]==2||Magic[4]==3)&&!Magic[5]&&!Magic[6]&&!Magic[7];
 bool Copy=!strcmp(LmStr(J,"kind"),"copy");char Hash[65]={0};Ok=Ok&&LmHash(In,Size,Hash,Cancel,NULL);
 /* Hash before allocating storage: importing a shared file needs no second weight-sized temporary copy. */
 char* Partial=NULL,*Blob=NULL;if(Ok&&Copy&&!LmBlob(Root,Hash,Size,Cancel)){
  Partial=xrtFormat("partial/%s.import",Id);Blob=xrtFormat("blobs/%s",Hash);
  xfile Out=Partial&&MdoHomeDiskSpace(Native,Size)?LmFile(Root,Partial,XFILE_WRITE|XFILE_CREATE|XFILE_TRUNCATE):NULL;char Copied[65]={0};
  Ok=Out&&LmHash(In,Size,Copied,Cancel,Out)&&!strcmp(Hash,Copied)&&xrtFlush(Out);xrtClose(Out);
  if(Ok)Ok=Blob&&xrtRootFileRename(Root,Partial,Blob,false);
 }
 xrtClose(In);xrtRootClose(InputRoot);xrtFree(Parent);xrtFree(Blob);if(!Ok&&Partial)xrtRootRemove(Root,Partial);xrtFree(Partial);
 char ReferencedStore[33]={0};
 if(Ok&&!Copy){xrtMutexLock(g_Lm.Lock);xvalue* Roots=xrtValueClone(LmGet(g_Lm.State,"roots"));xrtMutexUnlock(g_Lm.Lock);
  for(size_t I=0;I<xrtValueCount(Roots);I++){xstrview K={0};const xvalue* Store=xrtValueObjectAt(Roots,I,&K);char* R=MdoPathResolve(LmStr(Store,"path")),*Blob=R?xrtFormat("%s/blobs/%s",R,Hash):NULL;bool Same=false;
   if(Blob){char* Normal=xrtPathReal(Path),*Target=xrtPathReal(Blob);
#if defined(_WIN32) || defined(_WIN64)
    Same=Normal&&Target&&xrtStrCaseEqual(xrtStrView(Normal),xrtStrView(Target));
#else
    Same=Normal&&Target&&!strcmp(Normal,Target);
#endif
    xrtFree(Normal);xrtFree(Target);}
   xrtFree(R);xrtFree(Blob);if(Same){snprintf(ReferencedStore,33,"%s",K.Data);break;}
  }xrtValueRelease(Roots);
 }
 if(Ok){xrtMutexLock(g_Lm.Lock);xvalue* Before=xrtValueClone(g_Lm.State),*Job=LmEdit("installations",Id),*File=xrtValueObject(),*Assets=xrtValueArray();Ok=Before&&Job&&File&&Assets&&(!ReferencedStore[0]||LmPut(Job,"store",ReferencedStore))&&LmPut(File,"sha256",Hash)&&xrtValueObjectSetNew(File,XRT_STR_LITERAL("bytes"),xrtValueUInt(Size))&&LmPut(File,"path",Copy?"model.gguf":Path)&&xrtValueObjectSetNew(File,XRT_STR_LITERAL("owned"),xrtValueBool(Copy))&&xrtValueArrayAppend(Assets,File)&&xrtValueObjectSet(Job,XRT_STR_LITERAL("assets"),Assets);Ok=LmCommit(Before,Ok);xrtValueRelease(File);xrtValueRelease(Assets);xrtMutexUnlock(g_Lm.Lock);}
 if(!Ok)LmFail(E,"Import failed: expected a readable GGUF v2/v3 file, sufficient free space and an unchanged source");return Ok;
}
static bool LmRun(cstr Id,xcancel* Cancel,bool QueuedOnly,char E[512]){
 if(g_Lm.Stopping||!xrtMutexTryLock(g_Lm.Writer))return LmFail(E,"Another model resource operation is running");
 xrtMutexLock(g_Lm.Lock);const xvalue* Current=LmGet(LmGet(g_Lm.State,"installations"),Id);cstr State=LmStr(Current,"state");if(!Current||(QueuedOnly?strcmp(State,"queued"):!LmOneOf(State,"queued interrupted failed attention"))){xrtMutexUnlock(g_Lm.Lock);xrtMutexUnlock(g_Lm.Writer);return LmFail(E,"This installation is no longer resumable");}snprintf(g_Lm.Active,33,"%s",Id);g_Lm.ActiveCancel=xrtCancelChild(Cancel);Cancel=g_Lm.ActiveCancel;xrtMutexUnlock(g_Lm.Lock);
 xvalue* J=LmJob(Id);char* Native=NULL;xvalue* Store=J?LmStore(LmStr(J,"store")):NULL;xroot Root=Store?LmRoot(Store,&Native,E):NULL;
 bool Ok=Cancel&&J&&Root&&LmJobState(Id,"installing","");const xvalue* Plan=LmGet(J,"plan");
 if(Ok&&Plan){Ok=MdoLocalModelPlanValid(Plan,E)&&LmRuntime(J,Cancel,E)&&LmRecipe(J,Cancel,E)&&LmPins(Id,J,E);const xvalue* Assets=LmGet(J,"assets");for(size_t I=0;Ok&&I<xrtValueCount(Assets);I++)Ok=LmAsset(Id,xrtValueArrayGet(Assets,I),Root,Native,LmStr(Store,"id"),Cancel,E);if(Ok)Ok=LmPins(Id,J,E);}
 else if(Ok)Ok=LmImport(Id,J,Root,Native,Cancel,E);
 bool Saved=LmJobState(Id,Ok?"installed":xrtCancelRequested(Cancel)?"interrupted":"failed",Ok?"":E[0]?E:"Model installation failed; downloaded resources were retained");if(!Saved)Ok=false;
 xrtRootClose(Root);xrtFree(Native);xrtValueRelease(Store);xrtValueRelease(J);xrtMutexLock(g_Lm.Lock);xrtCancelDestroy(g_Lm.ActiveCancel);g_Lm.ActiveCancel=NULL;g_Lm.Active[0]=0;xrtMutexUnlock(g_Lm.Lock);xrtMutexUnlock(g_Lm.Writer);return Ok;
}
bool MdoLocalModelsResume(cstr Id,xcancel* Cancel,char E[512]){if(!g_Lm.Lock||g_Lm.Stopping||!LmXid(Id)||!MdoApiWriteRetainBackground())return false;bool Ok=LmRun(Id,Cancel,false,E);MdoApiWriteReleaseBackground();return Ok;}
static int32 LmWorker(void* Data){(void)Data;while(!xrtCancelRequested(g_Lm.Stop)){
 char Id[33]={0};xrtMutexLock(g_Lm.Lock);const xvalue* Jobs=LmGet(g_Lm.State,"installations");for(size_t I=0;I<xrtValueCount(Jobs);I++){xstrview Key={0};const xvalue* J=xrtValueObjectAt(Jobs,I,&Key);if(!strcmp(LmStr(J,"state"),"queued")){snprintf(Id,33,"%s",Key.Data);break;}}xrtMutexUnlock(g_Lm.Lock);
 if(!Id[0]){xrtSleep(50);continue;}if(!MdoApiWriteRetainBackground()){xrtSleep(100);continue;}
 char E[512]={0};LmRun(Id,g_Lm.Stop,true,E);MdoApiWriteReleaseBackground();
 }return 0;}
bool MdoLocalModelsInit(xnetengine* Engine){
 if(g_Lm.Lock)return true;g_Lm.Lock=xrtMutexCreate();g_Lm.Writer=xrtMutexCreate();g_Lm.Stop=xrtCancelCreate();g_Lm.Engine=Engine;if(!g_Lm.Lock||!g_Lm.Writer||!g_Lm.Stop)return false;
 bool Exists=false;xfileinfo Info;if(!MdoHomeExternalStat(LM_STATE,&Exists,&Info))g_Lm.Fault=true;g_Lm.State=Exists?LmReadJson(LM_STATE):xrtJsonParse(XRT_STR_LITERAL("{\"schema\":\"mdo.local-model-state.v1\",\"roots\":{},\"installations\":{}}"));
 if(!Exists&&g_Lm.State){char* Owner=xrtXidMakeString();if(!Owner||!LmPut(g_Lm.State,"owner",Owner))g_Lm.Fault=true;xrtFree(Owner);}
 if(!LmStateValid(g_Lm.State)){g_Lm.Fault=true;xrtValueRelease(g_Lm.State);g_Lm.State=xrtValueObject();}

 snprintf(g_Lm.Owner,33,"%s",LmStr(g_Lm.State,"owner"));if(!Exists&&!g_Lm.Fault&&!LmSave())g_Lm.Fault=true;
 xvalue* Jobs=xrtValueObjectEdit(g_Lm.State,XRT_STR_LITERAL("installations"));bool Changed=false;for(size_t I=0;I<xrtValueCount(Jobs);I++){xstrview Key={0};xrtValueObjectAt(Jobs,I,&Key);xvalue* J=xrtValueObjectEdit(Jobs,Key);cstr State=LmStr(J,"state");if(!strcmp(State,"queued")||!strcmp(State,"installing")||!strcmp(State,"removing")){LmPut(J,"state",!strcmp(State,"removing")?"cleanup_pending":"interrupted");Changed=true;}if(LmGet(J,"plan")){char E[512]={0};if(!LmPins(Key.Data,J,E)){LmPut(J,"state","attention");LmPut(J,"error",E);Changed=true;}}}
 if(Changed&&!LmSave())g_Lm.Fault=true;g_Lm.Thread=xrtThreadCreate(LmWorker,NULL,0);return g_Lm.Thread!=NULL;
}
void MdoLocalModelsStop(void){if(!g_Lm.Lock)return;xrtMutexLock(g_Lm.Lock);g_Lm.Stopping=true;xrtCancelRequest(g_Lm.Stop);if(g_Lm.ActiveCancel)xrtCancelRequest(g_Lm.ActiveCancel);xrtMutexUnlock(g_Lm.Lock);}
void MdoLocalModelsUnit(void){if(!g_Lm.Lock)return;MdoLocalModelsStop();if(g_Lm.Thread){xrtThreadWait(g_Lm.Thread);xrtThreadDestroy(g_Lm.Thread);}/* A download-center resume can own the writer on its dispatcher thread. */xrtMutexLock(g_Lm.Writer);xrtMutexUnlock(g_Lm.Writer);for(size_t I=0;I<LM_LIMIT;I++){xrtClose(g_Lm.Stores[I].File);xrtFree(g_Lm.Stores[I].Path);MdoExtensionsRelease(g_Lm.Pins[I].Recipe);MdoExtensionsRelease(g_Lm.Pins[I].Runtime);}xrtValueRelease(g_Lm.State);xrtCancelDestroy(g_Lm.Stop);xrtMutexDestroy(g_Lm.Writer);xrtMutexDestroy(g_Lm.Lock);memset(&g_Lm,0,sizeof(g_Lm));}
xvalue* MdoLocalModelsSnapshot(char E[512]){(void)E;if(!g_Lm.Lock)return NULL;xrtMutexLock(g_Lm.Lock);xvalue* Out=xrtValueDeepClone(g_Lm.State);if(Out){LmPut(Out,"platform",MdoExtensionRuntimePlatform());xrtValueObjectSetNew(Out,XRT_STR_LITERAL("read_only"),xrtValueBool(g_Lm.Fault));xvalue* Jobs=xrtValueObjectGet(Out,XRT_STR_LITERAL("installations"));for(size_t I=0;I<xrtValueCount(Jobs);I++){xvalue* J=xrtValueObjectAt(Jobs,I,NULL);const xvalue* Doc=LmGet(LmGet(J,"plan"),"document");if(Doc){xvalue* Labels=xrtValueObject();const cstr Fields[]={"default_locale","i18n","tags"};for(size_t K=0;Labels&&K<3;K++){const xvalue* V=LmGet(Doc,Fields[K]);if(V)xrtValueObjectSet(Labels,xrtStrView(Fields[K]),V);}if(Labels)xrtValueObjectSetNew(J,XRT_STR_LITERAL("local_model"),Labels);}xrtValueObjectRemove(J,XRT_STR_LITERAL("runtime_plan"));xrtValueObjectRemove(J,XRT_STR_LITERAL("plan"));}}xrtMutexUnlock(g_Lm.Lock);return Out;}

#include "operations.inc.c"
