#include "../../include/mdo/extension_tasks.h"
#include "../../include/mdo/extension_manifest.h"
#include "../../include/mdo/home.h"
#include "../../include/mdo/extensions.h"
#include "../../include/mdo/time.h"
#include "tasks_internal.h"

#define MDO_ARCHIVE_TASK_ROOT "data/extension-tasks"
#define MDO_ARCHIVE_TASK_RECORD 16384u
#define MDO_ARCHIVE_TASK_QUOTA (UINT64_C(1024)*1024*1024)
#define MDO_ARCHIVE_TASK_TTL 86400
static xmutex* MdoExtArchiveTasksLock;
static xcond* MdoExtArchiveTasksCondition;
static bool MdoExtArchiveTasksStopping;
struct MdoExtensionTaskUse {char Id[33];bool Sealing;xcancel* Cancel;struct MdoExtensionTaskUse* Next;};
static MdoExtensionTaskUse* MdoArchiveUses;
static MdoExtensionTaskUse* MdoExtArchiveUse(cstr Id){for(MdoExtensionTaskUse* U=MdoArchiveUses;U;U=U->Next)if(!strcmp(U->Id,Id))return U;return NULL;}
static bool MdoExtArchiveError(char Error[512],cstr Why){if(Error)snprintf(Error,512,"%s",Why);return false;}
static bool MdoExtArchivePut(xvalue* V,cstr K,cstr S){return S&&xrtValueObjectSetNew(V,xrtStrView(K),xrtValueString(xrtStrView(S)));}
static bool MdoExtArchiveId(cstr Id){return Id&&strlen(Id)==32&&MdoExtensionDirectoryId(Id);}
static bool MdoExtArchiveHash(cstr Hash){if(!Hash||strlen(Hash)!=64)return false;for(size_t I=0;I<64;I++)if(!strchr("0123456789abcdef",Hash[I]))return false;return true;}
static bool MdoExtArchiveNumber(const xvalue* V,cstr K,uint64* N){const xvalue* F=xrtValueObjectGet(V,xrtStrView(K));int64 I=0;if(xrtValueGetUInt(F,N))return true;if(xrtValueGetInt(F,&I)&&I>=0){*N=(uint64)I;return true;}return false;}
static int64 MdoExtArchiveNow(void){return MdoUtcUs()/1000000;}
static bool MdoExtArchiveLive(const xvalue* Task){uint64 Created=0;int64 Now=MdoExtArchiveNow();return Now>0&&MdoExtArchiveNumber(Task,"created",&Created)&&Created<=INT64_MAX-MDO_ARCHIVE_TASK_TTL&&(uint64)Now<=Created+MDO_ARCHIVE_TASK_TTL;}
static char* MdoExtArchivePath(cstr Id,cstr File){return MdoExtArchiveId(Id)?xrtFormat(MDO_ARCHIVE_TASK_ROOT "/%s/%s",Id,File):NULL;}
static bool MdoExtArchiveStat(cstr Path,bool* Exists,xfileinfo* Info){memset(Info,0,sizeof(*Info));return Path&&MdoHomeExternalStat(Path,Exists,Info);}
static bool MdoExtArchiveSave(const xvalue* Task){
 cstr Id=MdoExtensionText(Task,"id",32);char* Path=MdoExtArchivePath(Id,"task.json");size_t N=0;char* Text=xrtJsonStringify(Task,false,&N);bool Ok=Path&&Text&&N<=MDO_ARCHIVE_TASK_RECORD&&MdoHomeAtomicWrite(Path,Text,N,false);xrtFree(Path);xrtFree(Text);return Ok;
}
static xvalue* MdoExtArchiveLoad(cstr Id,char Error[512]){
 char* Path=MdoExtArchivePath(Id,"task.json");xfile F=Path?MdoHomeOpenRead(Path):NULL;xrtFree(Path);uint64 N=0;bool Ok=F&&xrtFileSize(F,&N)&&N&&N<=MDO_ARCHIVE_TASK_RECORD;
 char* Text=Ok?xrtMalloc((size_t)N+1):NULL;Ok=Text&&xrtReadFull(F,Text,(size_t)N,NULL);xrtClose(F);xvalue* V=Ok&&!memchr(Text,0,(size_t)N)?xrtJsonParse(xrtStrViewN(Text,(size_t)N)):NULL;xrtFree(Text);
 cstr Stored=MdoExtensionText(V,"id",32),Hash=MdoExtensionText(V,"sha256",64),State=MdoExtensionText(V,"state",24),Kind=MdoExtensionText(V,"kind",32),Purpose=MdoExtensionText(V,"purpose",16);uint64 Size=0,Created=0;
 bool Review=Purpose&&(!strcmp(Purpose,"review")||!strcmp(Purpose,"file"));Ok=V&&Stored&&Id&&!strcmp(Stored,Id)&&MdoExtArchiveHash(Hash)&&MdoExtArchiveNumber(V,"bytes",&Size)&&Size&&Size<=MDO_EXTENSION_ARCHIVE_BYTES&&MdoExtArchiveNumber(V,"created",&Created)&&Created&&Created<=INT64_MAX-MDO_ARCHIVE_TASK_TTL&&
  Purpose&&(Review||!strcmp(Purpose,"install")||!strcmp(Purpose,"publish"))&&(Review||MdoExtensionDirectoryKind(Kind))&&State&&(!strcmp(State,"receiving")||!strcmp(State,"ready")||!strcmp(State,"failed")||!strcmp(State,"cancel_requested")||!strcmp(State,"cancelled")||!strcmp(State,"complete"));
 if(!Ok){xrtValueRelease(V);MdoExtArchiveError(Error,"Archive task is missing or invalid");return NULL;}return V;
}
static bool MdoExtArchiveRemoveFile(cstr Id,cstr Name){char* Path=MdoExtArchivePath(Id,Name);bool Exists=false;xfileinfo Info;bool Ok=Path&&MdoExtArchiveStat(Path,&Exists,&Info)&&(!Exists||MdoHomeRemove(Path,false));xrtFree(Path);return Ok;}
static bool MdoExtArchiveForget(cstr Id){
 char* Record=MdoExtArchivePath(Id,"task.json"),*Base=MdoExtArchiveId(Id)?xrtFormat(MDO_ARCHIVE_TASK_ROOT "/%s",Id):NULL;bool Exists=false;xfileinfo Info;
 bool Ok=Record&&Base&&MdoExtArchiveStat(Record,&Exists,&Info)&&Exists&&Info.Type==XFILE_TYPE_FILE&&MdoExtArchiveRemoveFile(Id,"archive.part")&&MdoExtArchiveRemoveFile(Id,"archive.zip")&&MdoHomeRemove(Record,false)&&MdoHomeRemoveEmptyDirectory(Base);xrtFree(Record);xrtFree(Base);return Ok;
}
static xvalue* MdoExtArchiveInfo(const xvalue* Task){
 xvalue* Out=xrtValueClone(Task);cstr Id=MdoExtensionText(Task,"id",32);char* Path=MdoExtArchivePath(Id,"archive.zip");bool Exists=false;xfileinfo Info;bool Ok=Out&&MdoExtArchiveStat(Path,&Exists,&Info);xrtFree(Path);
 if(Ok&&!Exists){Path=MdoExtArchivePath(Id,"archive.part");Ok=MdoExtArchiveStat(Path,&Exists,&Info);xrtFree(Path);}uint64 Received=Exists?Info.Size:0;const cstr State=MdoExtensionText(Task,"state",24);if(State&&!strcmp(State,"complete"))MdoExtArchiveNumber(Task,"bytes",&Received);
 if(Ok)Ok=(!Exists||Info.Type==XFILE_TYPE_FILE)&&xrtValueObjectSetNew(Out,XRT_STR_LITERAL("stored_bytes"),xrtValueUInt(Exists?Info.Size:0))&&xrtValueObjectSetNew(Out,XRT_STR_LITERAL("received"),xrtValueUInt(Received));
 if(Ok)Ok=xrtValueObjectSetNew(Out,XRT_STR_LITERAL("active"),xrtValueBool(MdoExtArchiveUse(Id)!=NULL));
 if(Ok&&!MdoExtArchiveLive(Task)){MdoExtArchivePut(Out,"state","expired");MdoExtArchivePut(Out,"error","Archive task expired");}
 if(!Ok){xrtValueRelease(Out);return NULL;}return Out;
}
static xvalue* MdoExtArchiveList(char Error[512]){
 xvalue* Items=xrtValueArray();xdir D=Items?MdoHomeOpenDirectory(MDO_ARCHIVE_TASK_ROOT,XDIR_STAT):NULL;bool Ok=Items!=NULL;
 if(!D){const xerror* E=xrtGetError();if(E&&xrtErrorKind(E)==XERR_NOT_FOUND)xrtClearError();else Ok=false;}
 xdirentry E;xdirnext Next=XDIR_NEXT_END;size_t Scanned=0;
 while(Ok&&D&&(Next=xrtDirNext(D,&E))==XDIR_NEXT_ITEM){
  if(++Scanned>8192){Ok=false;break;}if(E.Info.Type!=XFILE_TYPE_DIRECTORY||!(E.Flags&XDIR_ENTRY_UTF8)||E.Name.Size!=32)continue;
  char Id[33];memcpy(Id,E.Name.Data,32);Id[32]=0;if(!MdoExtArchiveId(Id))continue;char* Path=MdoExtArchivePath(Id,"task.json");bool Exists=false;xfileinfo Info;Ok=MdoExtArchiveStat(Path,&Exists,&Info);xrtFree(Path);if(!Ok||!Exists)continue;
  char Why[512]={0};xvalue* Task=MdoExtArchiveLoad(Id,Why);if(!Task){xvalue* Bad=xrtValueObject();Ok=Bad&&MdoExtArchivePut(Bad,"id",Id)&&MdoExtArchivePut(Bad,"state","failed")&&MdoExtArchivePut(Bad,"error","Archive task metadata is unreadable; clear this task to release its files")&&xrtValueObjectSetNew(Bad,XRT_STR_LITERAL("invalid"),xrtValueBool(true))&&xrtValueObjectSetNew(Bad,XRT_STR_LITERAL("bytes"),xrtValueUInt(MDO_EXTENSION_ARCHIVE_BYTES))&&xrtValueCount(Items)<128&&xrtValueArrayAppend(Items,Bad);xrtValueRelease(Bad);continue;}uint64 Created=0;MdoExtArchiveNumber(Task,"created",&Created);
  cstr State=MdoExtensionText(Task,"state",24);bool Expired=MdoExtArchiveNow()>0&&(uint64)MdoExtArchiveNow()>Created+MDO_ARCHIVE_TASK_TTL;
        if(!MdoExtArchiveUse(Id)&&State&&!strcmp(State,"cancel_requested")){bool A=MdoExtArchiveRemoveFile(Id,"archive.part"),B=MdoExtArchiveRemoveFile(Id,"archive.zip");Ok=MdoExtArchivePut(Task,"state","cancelled")&&xrtValueObjectSetNew(Task,XRT_STR_LITERAL("cleanup_pending"),xrtValueBool(!A||!B))&&MdoExtArchiveSave(Task);}
        if(Expired&&!MdoExtArchiveUse(Id)){bool A=MdoExtArchiveRemoveFile(Id,"archive.part"),B=MdoExtArchiveRemoveFile(Id,"archive.zip");if(A&&B){char* Record=MdoExtArchivePath(Id,"task.json"),*Base=xrtFormat(MDO_ARCHIVE_TASK_ROOT "/%s",Id);Ok=Record&&MdoHomeRemove(Record,false);if(Ok)(void)MdoHomeRemoveEmptyDirectory(Base);xrtFree(Record);xrtFree(Base);xrtValueRelease(Task);continue;}else {xrtValueObjectSetNew(Task,XRT_STR_LITERAL("cleanup_pending"),xrtValueBool(true));Ok=MdoExtArchiveSave(Task);}}
  (void)State;xvalue* Row=Ok?MdoExtArchiveInfo(Task):NULL;Ok=Ok&&Row&&xrtValueCount(Items)<128&&xrtValueArrayAppend(Items,Row);xrtValueRelease(Row);xrtValueRelease(Task);
 }
 if(Next==XDIR_NEXT_ERROR)Ok=false;
 if(D&&!xrtDirClose(D))Ok=false;
 if(!Ok){xrtValueRelease(Items);
 MdoExtArchiveError(Error,"Archive tasks could not be read safely or exceed the retained task limit");
 return NULL;
 }
 return Items;
}
bool MdoExtensionTasksInit(void){if(MdoExtArchiveTasksLock)return !MdoExtArchiveTasksStopping;MdoExtArchiveTasksLock=xrtMutexCreate();MdoExtArchiveTasksCondition=xrtCondCreate();MdoExtArchiveTasksStopping=false;if(!MdoExtArchiveTasksLock||!MdoExtArchiveTasksCondition){if(MdoExtArchiveTasksLock)xrtMutexDestroy(MdoExtArchiveTasksLock);if(MdoExtArchiveTasksCondition)xrtCondDestroy(MdoExtArchiveTasksCondition);MdoExtArchiveTasksLock=NULL;MdoExtArchiveTasksCondition=NULL;return false;}return true;}
void MdoExtensionTasksUnit(void){if(!MdoExtArchiveTasksLock)return;xrtMutexLock(MdoExtArchiveTasksLock);MdoExtArchiveTasksStopping=true;for(MdoExtensionTaskUse* U=MdoArchiveUses;U;U=U->Next)xrtCancelRequest(U->Cancel);while(MdoArchiveUses)xrtCondWait(MdoExtArchiveTasksCondition,MdoExtArchiveTasksLock);xrtMutexUnlock(MdoExtArchiveTasksLock);xrtCondDestroy(MdoExtArchiveTasksCondition);xrtMutexDestroy(MdoExtArchiveTasksLock);MdoExtArchiveTasksCondition=NULL;MdoExtArchiveTasksLock=NULL;}
xvalue* MdoExtensionTasksList(char Error[512]){if(Error)Error[0]=0;if(!MdoExtArchiveTasksLock){MdoExtArchiveError(Error,"Archive task service unavailable");return NULL;}xrtMutexLock(MdoExtArchiveTasksLock);xvalue* R=MdoExtArchiveList(Error);xrtMutexUnlock(MdoExtArchiveTasksLock);return R;}
xvalue* MdoExtensionTaskRead(cstr Id,char Error[512]){if(Error)Error[0]=0;if(!MdoExtArchiveTasksLock||!MdoExtArchiveId(Id)){MdoExtArchiveError(Error,"Choose an archive task");return NULL;}xrtMutexLock(MdoExtArchiveTasksLock);xvalue* T=MdoExtArchiveLoad(Id,Error),*R=T?MdoExtArchiveInfo(T):NULL;xrtValueRelease(T);xrtMutexUnlock(MdoExtArchiveTasksLock);return R;}
bool MdoExtensionTaskBegin(const xvalue* Input,xvalue** Result,char Error[512]){
 *Result=NULL;if(Error)Error[0]=0;cstr Purpose=MdoExtensionText(Input,"purpose",16),Kind=MdoExtensionText(Input,"kind",32),Hash=MdoExtensionText(Input,"sha256",64);uint64 Size=0;
 bool Review=Purpose&&(!strcmp(Purpose,"review")||!strcmp(Purpose,"file"));
 if(!MdoExtArchiveTasksLock||!Purpose||(!Review&&strcmp(Purpose,"install")&&strcmp(Purpose,"publish"))||(!Review&&!MdoExtensionDirectoryKind(Kind))||!MdoExtArchiveHash(Hash)||!MdoExtArchiveNumber(Input,"bytes",&Size)||!Size||Size>MDO_EXTENSION_ARCHIVE_BYTES)return MdoExtArchiveError(Error,"Choose an archive purpose, type, SHA-256 and size up to 512 MiB");
 xrtMutexLock(MdoExtArchiveTasksLock);xvalue* Items=MdoExtArchiveList(Error);uint64 Reserved=0;size_t Active=0;
 if(Items&&xrtValueCount(Items)==128){const xvalue* Oldest=NULL;uint64 OldTime=UINT64_MAX;
  for(size_t I=0;I<xrtValueCount(Items);I++){const xvalue* T=xrtValueArrayGet(Items,I);cstr State=MdoExtensionText(T,"state",24);uint64 Created=0;MdoExtArchiveNumber(T,"created",&Created);if((!strcmp(State,"cancelled")||!strcmp(State,"complete"))&&Created<OldTime){Oldest=T;OldTime=Created;}}
  if(Oldest&&MdoExtArchiveForget(MdoExtensionText(Oldest,"id",32))){xrtValueRelease(Items);Items=MdoExtArchiveList(Error);}}
 for(size_t I=0;I<xrtValueCount(Items);I++){const xvalue* T=xrtValueArrayGet(Items,I);cstr State=MdoExtensionText(T,"state",24);uint64 Bytes=0;MdoExtArchiveNumber(T,"bytes",&Bytes);if(strcmp(State,"cancelled")&&strcmp(State,"complete")){Reserved+=Bytes;Active++;}else{uint64 Stored=0;MdoExtArchiveNumber(T,"stored_bytes",&Stored);Reserved+=Stored;}}
 bool Ok=!MdoExtArchiveTasksStopping&&Items&&xrtValueCount(Items)<128&&Active<3&&Reserved<=MDO_ARCHIVE_TASK_QUOTA&&Size<=MDO_ARCHIVE_TASK_QUOTA-Reserved;char* Id=Ok?xrtXidMakeString():NULL,*Base=Id?xrtFormat(MDO_ARCHIVE_TASK_ROOT "/%s",Id):NULL;
 xroot Directory=Base?MdoHomeOpenStorageDirectory(Base):NULL;Ok=Ok&&Directory;if(Directory)xrtRootClose(Directory);xvalue* T=xrtValueObject();
 Ok=Ok&&T&&MdoExtArchivePut(T,"id",Id)&&MdoExtArchivePut(T,"kind",Kind?Kind:"")&&MdoExtArchivePut(T,"purpose",Purpose)&&MdoExtArchivePut(T,"sha256",Hash)&&MdoExtArchivePut(T,"state","receiving")&&xrtValueObjectSetNew(T,XRT_STR_LITERAL("bytes"),xrtValueUInt(Size))&&xrtValueObjectSetNew(T,XRT_STR_LITERAL("created"),xrtValueUInt((uint64)MdoExtArchiveNow()))&&MdoExtArchiveSave(T);
 if(Ok)*Result=MdoExtArchiveInfo(T);
 Ok=Ok&&*Result;
 if(!Ok&&Base)MdoHomeRemoveEmptyDirectory(Base);
 xrtValueRelease(T);
 xrtValueRelease(Items);
 xrtFree(Id);
 xrtFree(Base);
 xrtMutexUnlock(MdoExtArchiveTasksLock);
 if(!Ok&&Error&&!Error[0])MdoExtArchiveError(Error,"Archive task storage is full or unavailable; cancel unused tasks first");
 return Ok;
}
static bool MdoExtArchiveWrite(cstr Id,MdoExtensionTaskUse* Use,uint64 Offset,const void* Data,size_t Bytes,xvalue** Result,char Error[512]){
 *Result=NULL;if(Error)Error[0]=0;if(!MdoExtArchiveTasksLock||!MdoExtArchiveId(Id)||!Data||!Bytes||Bytes>MDO_EXTENSION_TASK_CHUNK)return MdoExtArchiveError(Error,"Write one raw binary archive chunk of at most 1 MiB");
 xrtMutexLock(MdoExtArchiveTasksLock);xvalue* T=MdoExtArchiveLoad(Id,Error);uint64 Expected=0;bool Ok=T&&(Use?MdoExtArchiveUse(Id)==Use:!MdoExtArchiveUse(Id))&&(!Use||!xrtCancelRequested(Use->Cancel))&&MdoExtArchiveLive(T)&&MdoExtArchiveNumber(T,"bytes",&Expected)&&Offset<=Expected&&Bytes<=Expected-Offset&&!strcmp(MdoExtensionText(T,"state",24),"receiving");
 char* Path=Ok?MdoExtArchivePath(Id,"archive.part"):NULL,*Final=Ok?MdoExtArchivePath(Id,"archive.zip"):NULL;bool Exists=false,Sealed=false;xfileinfo Info;Ok=Ok&&MdoExtArchiveStat(Final,&Sealed,&Info)&&!Sealed&&MdoExtArchiveStat(Path,&Exists,&Info)&&(!Exists||Info.Type==XFILE_TYPE_FILE);uint64 Current=Exists?Info.Size:0;xrtFree(Final);
 xfile File=Ok?MdoHomeOpenWrite(Path,XFILE_READ|XFILE_WRITE|XFILE_CREATE):NULL;Ok=Ok&&File&&Offset<=Current&&Current<=Expected;
 /* A repeated request must match every byte already on disk. If a process
  * stopped during one chunk, its matching prefix can be completed safely. */
 size_t Overlap=Ok&&Current>Offset?(size_t)((Current-Offset)<Bytes?(Current-Offset):Bytes):0;uint8 Buffer[65536];
 for(size_t At=0;Ok&&At<Overlap;){size_t N=Overlap-At<sizeof(Buffer)?Overlap-At:sizeof(Buffer);Ok=xrtReadAtFull(File,Offset+At,Buffer,N,NULL)&&!memcmp(Buffer,(const uint8*)Data+At,N);At+=N;}
 if(Ok&&Overlap<Bytes)Ok=xrtWriteAtFull(File,Offset+Overlap,(const uint8*)Data+Overlap,Bytes-Overlap,NULL)&&xrtFlush(File);
 if(File&&!xrtClose(File))Ok=false;
 if(Ok)*Result=MdoExtArchiveInfo(T);
 Ok=Ok&&*Result;
 xrtFree(Path);
 xrtValueRelease(T);
 xrtMutexUnlock(MdoExtArchiveTasksLock);
 if(!Ok&&Error&&!Error[0])MdoExtArchiveError(Error,"Archive chunk conflicts with the received bytes or cannot be written; read the current offset before retrying");
 return Ok;
}
bool MdoExtensionTaskWrite(cstr Id,uint64 Offset,const void* Data,size_t Bytes,xvalue** Result,char Error[512]){return MdoExtArchiveWrite(Id,NULL,Offset,Data,Bytes,Result,Error);}
bool MdoExtensionTaskUseWrite(MdoExtensionTaskUse* Use,uint64 Offset,const void* Data,size_t Bytes,xvalue** Result,char Error[512]){if(!Use){*Result=NULL;return MdoExtArchiveError(Error,"An owned archive transfer is required");}return MdoExtArchiveWrite(Use->Id,Use,Offset,Data,Bytes,Result,Error);}
bool MdoExtensionTaskOrigin(MdoExtensionTaskUse* Use,const xvalue* Origin,char Error[512]){
 if(!Use||xrtValueType(Origin)!=XVALUE_OBJECT)return false;
 cstr Kind=MdoExtensionText(Origin,"kind",32),Id=MdoExtensionText(Origin,"xid",32),Version=MdoExtensionText(Origin,"version_id",32),Hash=MdoExtensionText(Origin,"sha256",64);
 char* Text=xrtJsonStringify(Origin,false,NULL);
 bool Valid=Text&&strlen(Text)<=8192&&MdoExtensionDirectoryKind(Kind)&&MdoExtArchiveId(Id)&&MdoExtArchiveId(Version)&&MdoExtArchiveHash(Hash);xrtFree(Text);if(!Valid)return MdoExtArchiveError(Error,"Invalid verified store origin");
 const cstr Fields[]={"kind","xid","version_id","sha256","bytes","owner","submitter","author","copyright","license","name","alias","platforms","state","version"};
 for(size_t I=0;I<xrtValueCount(Origin);I++){xstrview Key={0};xrtValueObjectAt(Origin,I,&Key);bool Known=false;for(size_t J=0;J<sizeof(Fields)/sizeof(*Fields);J++)if(xrtStrEqual(Key,xrtStrView(Fields[J])))Known=true;if(!Known)return MdoExtArchiveError(Error,"Unexpected field in verified store origin");}
 xrtMutexLock(MdoExtArchiveTasksLock);xvalue* Task=MdoExtArchiveLoad(Use->Id,Error);cstr State=MdoExtensionText(Task,"state",24);bool Ok=Task&&MdoExtArchiveUse(Use->Id)==Use&&State&&!strcmp(State,"receiving")&&!xrtCancelRequested(Use->Cancel)&&!strcmp(Kind,MdoExtensionText(Task,"kind",32))&&!strcmp(Hash,MdoExtensionText(Task,"sha256",64))&&xrtValueObjectSet(Task,XRT_STR_LITERAL("store"),Origin)&&MdoExtArchivePut(Task,"directory_id",Id)&&MdoExtArchiveSave(Task);
 xrtValueRelease(Task);xrtMutexUnlock(MdoExtArchiveTasksLock);return Ok||MdoExtArchiveError(Error,"Archive transfer changed before its store origin could be recorded");
}
bool MdoExtensionTaskPublication(MdoExtensionTaskUse* Use,const xvalue* Request,char Error[512]){
 if(!Use||!Request)return MdoExtArchiveError(Error,"Missing publication request");
 char* Text=xrtJsonStringify(Request,false,NULL);
 bool Valid=Text&&strlen(Text)<=12000;
 const cstr Allowed[]={"action","upload","nonce","proof_upload","publish","review_notes","source_url","source_included","work","expected_revision"}
 ;
 for(size_t I=0;Valid&&I<xrtValueCount(Request);I++){xstrview Key={0};xrtValueObjectAt(Request,I,&Key);bool Known=false;for(size_t J=0;J<sizeof(Allowed)/sizeof(*Allowed);J++)if(xrtStrEqual(Key,xrtStrView(Allowed[J])))Known=true;Valid=Known;}
 xrtMutexLock(MdoExtArchiveTasksLock);xvalue* Task=Valid?MdoExtArchiveLoad(Use->Id,Error):NULL;const xvalue* Previous=xrtValueObjectGet(Task,XRT_STR_LITERAL("publication"));char* Prior=Previous?xrtJsonStringify(Previous,false,NULL):NULL;
 cstr State=MdoExtensionText(Task,"state",24);bool Ok=Task&&MdoExtArchiveUse(Use->Id)==Use&&State&&!strcmp(State,"ready")&&!xrtCancelRequested(Use->Cancel)&&(!Previous||(Prior&&!strcmp(Prior,Text)))&&xrtValueObjectSet(Task,XRT_STR_LITERAL("publication"),Request)&&MdoExtArchiveSave(Task);
 xrtFree(Text);xrtFree(Prior);xrtValueRelease(Task);xrtMutexUnlock(MdoExtArchiveTasksLock);return Ok||MdoExtArchiveError(Error,"A retried publication must keep its original review metadata; cancel and repack to change it");
}
bool MdoExtensionTaskTransfer(MdoExtensionTaskUse* Use,uint64 Sent,cstr Phase,char Error[512]){
 if(!Use||!Phase||(strcmp(Phase,"uploading")&&strcmp(Phase,"submitting")))return MdoExtArchiveError(Error,"Invalid archive transfer phase");
 xrtMutexLock(MdoExtArchiveTasksLock);xvalue* Task=MdoExtArchiveLoad(Use->Id,Error);cstr State=MdoExtensionText(Task,"state",24);uint64 Bytes=0;
 bool Ok=Task&&MdoExtArchiveUse(Use->Id)==Use&&State&&!strcmp(State,"ready")&&!xrtCancelRequested(Use->Cancel)&&MdoExtArchiveNumber(Task,"bytes",&Bytes)&&Sent<=Bytes&&MdoExtArchivePut(Task,"phase",Phase)&&xrtValueObjectSetNew(Task,XRT_STR_LITERAL("sent"),xrtValueUInt(Sent))&&MdoExtArchiveSave(Task);
 xrtValueRelease(Task);xrtMutexUnlock(MdoExtArchiveTasksLock);return Ok||MdoExtArchiveError(Error,"Archive transfer was cancelled or changed");
}
static bool MdoExtArchiveDigest(xfile F,xcancel* Cancel,char Hash[65]){
 uint64 Size=0;bool Ok=F&&xrtFileSize(F,&Size)&&Size<=MDO_EXTENSION_ARCHIVE_BYTES;xsha256 S;xrtSha256Init(&S);uint8 B[65536],Digest[32];
 for(uint64 At=0;Ok&&At<Size;){size_t N=Size-At<sizeof(B)?(size_t)(Size-At):sizeof(B);Ok=(!Cancel||!xrtCancelRequested(Cancel))&&xrtReadAtFull(F,At,B,N,NULL)&&xrtSha256Update(&S,B,N);At+=N;}
 Ok=Ok&&xrtSha256Final(&S,Digest);if(Ok)for(size_t I=0;I<32;I++)snprintf(Hash+I*2,3,"%02x",Digest[I]);return Ok;
}
bool MdoExtensionTaskSeal(cstr Id,xcancel* Cancel,xvalue** Result,char Error[512]){
 *Result=NULL;if(Error)Error[0]=0;MdoExtensionTaskUse* Use=MdoExtensionTaskUseBegin(Id,true,Cancel,Error);if(!Use)return false;xcancel* Stop=MdoExtensionTaskUseCancel(Use);
 xrtMutexLock(MdoExtArchiveTasksLock);xvalue* T=MdoExtArchiveLoad(Id,Error);cstr State=MdoExtensionText(T,"state",24);bool Ready=State&&!strcmp(State,"ready");
 bool Ok=T&&State&&MdoExtArchiveLive(T)&&(Ready||!strcmp(State,"receiving"));char* Final=Ok?MdoExtArchivePath(Id,"archive.zip"):NULL,*Part=Ok?MdoExtArchivePath(Id,"archive.part"):NULL;bool Exists=false;xfileinfo Info;
 Ok=Ok&&MdoExtArchiveStat(Final,&Exists,&Info);uint64 Expected=0,Size=0;char Hash[65];Ok=Ok&&MdoExtArchiveNumber(T,"bytes",&Expected);xfile F=Ok&&!Ready?MdoHomeOpenRead(Exists?Final:Part):NULL;
 if(Ok&&Ready)Ok=Exists&&Info.Type==XFILE_TYPE_FILE&&Info.Size==Expected;
 xrtMutexUnlock(MdoExtArchiveTasksLock);
 /* Hashing/inflating can be substantial. The in-memory use pins the task
  * while the control mutex stays free for progress reads and cancellation. */
 bool DigestRead=Ok&&!Ready&&F&&xrtFileSize(F,&Size)&&Size==Expected&&MdoExtArchiveDigest(F,Stop,Hash);
 bool Invalid=false;if(Ok&&!Ready){Ok=DigestRead&&!strcmp(Hash,MdoExtensionText(T,"sha256",64));Invalid=DigestRead&&!Ok;}
 cstr Purpose=MdoExtensionText(T,"purpose",16);bool Review=Purpose&&(!strcmp(Purpose,"review")||!strcmp(Purpose,"file"));MdoZip Z={0};xvalue* Meta=NULL;
 if(Ok&&!Ready&&!Review){Ok=MdoZipOpen(&Z,F);if(Ok)Meta=MdoExtensionZipManifest(&Z,Stop);Ok=Ok&&Meta&&!strcmp(MdoExtensionText(Meta,"kind",32),MdoExtensionText(T,"kind",32));
  for(size_t I=0;
  Ok&&I<Z.Count;
  I++)Ok=MdoZipEntryRead(&Z,I,NULL,NULL,Stop,NULL);
  if(!Ok&&!xrtCancelRequested(Stop))Invalid=true;
  }
 MdoZipClose(&Z);xrtClose(F);
 xrtMutexLock(MdoExtArchiveTasksLock);xvalue* Current=MdoExtArchiveLoad(Id,Error);cstr CurrentState=MdoExtensionText(Current,"state",24);
 bool Cancelled=xrtCancelRequested(Stop)||(CurrentState&&!strcmp(CurrentState,"cancel_requested"));
 Ok=Ok&&Current&&CurrentState&&!Cancelled&&MdoExtArchiveLive(Current)&&(!strcmp(CurrentState,"receiving")||!strcmp(CurrentState,"ready"))&&!strcmp(MdoExtensionText(Current,"sha256",64),MdoExtensionText(T,"sha256",64));
 if(Ok&&!Ready&&!Exists)Ok=MdoHomeRenameNoReplace(Part,Final);
 if(Ok&&!Ready){if(Meta)Ok=MdoExtArchivePut(Current,"name",MdoExtensionText(Meta,"name",240))&&MdoExtArchivePut(Current,"alias",MdoExtensionText(Meta,"alias",64));Ok=Ok&&MdoExtArchivePut(Current,"state","ready")&&MdoExtArchiveSave(Current);}
 if(Ok)*Result=MdoExtArchiveInfo(Current);
 Ok=Ok&&*Result;
 if(!Ok&&Current&&!Ready&&Invalid&&!Cancelled){MdoExtArchivePut(Current,"state","failed");MdoExtArchivePut(Current,"error",Z.Error[0]?Z.Error:"Archive has the wrong type or failed its hash/ZIP checks");(void)MdoExtArchiveSave(Current);}
 if(!Ok&&Error&&!Error[0])MdoExtArchiveError(Error,Cancelled?"Archive validation was cancelled":"Archive did not pass its size, SHA-256, ZIP entries and root manifest checks");
 xrtValueRelease(Current);xrtValueRelease(Meta);xrtValueRelease(T);xrtFree(Final);xrtFree(Part);xrtMutexUnlock(MdoExtArchiveTasksLock);MdoExtensionTaskUseEnd(Use);return Ok;
}
bool MdoExtensionTaskCancel(cstr Id,xvalue** Result,char Error[512]){
 *Result=NULL;if(Error)Error[0]=0;if(!MdoExtArchiveTasksLock||!MdoExtArchiveId(Id))return MdoExtArchiveError(Error,"Choose an archive task");xrtMutexLock(MdoExtArchiveTasksLock);xvalue* T=MdoExtArchiveLoad(Id,Error);
 MdoExtensionTaskUse* Use=MdoExtArchiveUse(Id);cstr State=MdoExtensionText(T,"state",24);bool Complete=State&&!strcmp(State,"complete");
 bool Ok=T&&MdoExtArchivePut(T,"state",Complete?"complete":Use?"cancel_requested":"cancelled")&&MdoExtArchiveSave(T);if(Ok&&Use&&!Complete)xrtCancelRequest(Use->Cancel);
 if(Ok&&!Use){bool A=MdoExtArchiveRemoveFile(Id,"archive.part"),B=MdoExtArchiveRemoveFile(Id,"archive.zip");xrtValueObjectSetNew(T,XRT_STR_LITERAL("cleanup_pending"),xrtValueBool(!A||!B));Ok=MdoExtArchiveSave(T);}
 if(Ok)*Result=MdoExtArchiveInfo(T);
 Ok=Ok&&*Result;
 xrtValueRelease(T);
 xrtMutexUnlock(MdoExtArchiveTasksLock);
 return Ok||MdoExtArchiveError(Error,"Task cancellation could not be recorded");
}
bool MdoExtensionTaskForget(cstr Id,char Error[512]){
 if(Error)Error[0]=0;
 if(!MdoExtArchiveTasksLock||!MdoExtArchiveId(Id))return MdoExtArchiveError(Error,"Choose an archive task");
 xrtMutexLock(MdoExtArchiveTasksLock);
 xvalue* T=MdoExtArchiveLoad(Id,NULL);
 cstr State=MdoExtensionText(T,"state",24);
 bool Ok=!MdoExtArchiveUse(Id)&&(!T||(State&&(!strcmp(State,"cancelled")||!strcmp(State,"complete")||!strcmp(State,"failed"))))&&MdoExtArchiveForget(Id);xrtValueRelease(T);xrtMutexUnlock(MdoExtArchiveTasksLock);return Ok||MdoExtArchiveError(Error,"Cancel the task before clearing its retained files and receipt");
}
bool MdoExtensionTaskComplete(cstr Id,const xvalue* Outcome,xvalue** Result,char Error[512]){
 *Result=NULL;if(Error)Error[0]=0;if(!MdoExtArchiveTasksLock||!MdoExtArchiveId(Id))return MdoExtArchiveError(Error,"Choose a sealed archive task");xrtMutexLock(MdoExtArchiveTasksLock);xvalue* T=MdoExtArchiveLoad(Id,Error);cstr State=MdoExtensionText(T,"state",24);
 bool Ok=T&&State&&(!strcmp(State,"ready")||!strcmp(State,"cancel_requested")||!strcmp(State,"complete"));cstr Target=MdoExtensionText(Outcome,"id",96),Problem=MdoExtensionText(Outcome,"runtime_error",512);
 if(Ok&&Target)Ok=MdoExtArchivePut(T,"directory_id",Target);
 if(Ok&&Problem)Ok=MdoExtArchivePut(T,"error",Problem);
 Ok=Ok&&MdoExtArchivePut(T,"state","complete")&&MdoExtArchiveSave(T);
 if(Ok){bool A=MdoExtArchiveRemoveFile(Id,"archive.part"),B=MdoExtArchiveRemoveFile(Id,"archive.zip");xrtValueObjectSetNew(T,XRT_STR_LITERAL("cleanup_pending"),xrtValueBool(!A||!B));Ok=MdoExtArchiveSave(T);}
 if(Ok)*Result=MdoExtArchiveInfo(T);
 Ok=Ok&&*Result;
 xrtValueRelease(T);
 xrtMutexUnlock(MdoExtArchiveTasksLock);
 return Ok||MdoExtArchiveError(Error,"Installation finished but its transfer receipt or temporary-file cleanup needs attention");
}
MdoExtensionTaskUse* MdoExtensionTaskUseBegin(cstr Id,bool Sealing,xcancel* Parent,char Error[512]){
 if(Error)Error[0]=0;
 if(!MdoExtArchiveTasksLock||!MdoExtArchiveId(Id)){MdoExtArchiveError(Error,"Choose an archive task");
 return NULL;
 }
 xrtMutexLock(MdoExtArchiveTasksLock);
 xvalue* T=MdoExtArchiveLoad(Id,Error);
 cstr State=MdoExtensionText(T,"state",24);
 bool Ok=!MdoExtArchiveTasksStopping&&T&&State&&MdoExtArchiveLive(T)&&!MdoExtArchiveUse(Id)&&(!strcmp(State,"ready")||(Sealing&&!strcmp(State,"receiving")));MdoExtensionTaskUse* Use=Ok?xrtCalloc(1,sizeof(*Use)):NULL;
 if(Use){Use->Cancel=Parent?xrtCancelChild(Parent):xrtCancelCreate();if(!Use->Cancel){xrtFree(Use);Use=NULL;}else{snprintf(Use->Id,sizeof(Use->Id),"%s",Id);Use->Sealing=Sealing;Use->Next=MdoArchiveUses;MdoArchiveUses=Use;}}
 xrtValueRelease(T);xrtMutexUnlock(MdoExtArchiveTasksLock);if(!Use&&Error&&!Error[0])MdoExtArchiveError(Error,"Archive task is busy, cancelled, expired or not ready");return Use;
}
xcancel* MdoExtensionTaskUseCancel(MdoExtensionTaskUse* Use){return Use?Use->Cancel:NULL;}
void MdoExtensionTaskUseEnd(MdoExtensionTaskUse* Use){
 if(!Use||!MdoExtArchiveTasksLock)return;
 xrtMutexLock(MdoExtArchiveTasksLock);
 MdoExtensionTaskUse** Link=&MdoArchiveUses;
 while(*Link&&*Link!=Use)Link=&(*Link)->Next;
 if(*Link){*Link=Use->Next;xvalue* T=MdoExtArchiveLoad(Use->Id,NULL);cstr State=MdoExtensionText(T,"state",24);if(State&&!strcmp(State,"cancel_requested")){bool A=MdoExtArchiveRemoveFile(Use->Id,"archive.part"),B=MdoExtArchiveRemoveFile(Use->Id,"archive.zip");MdoExtArchivePut(T,"state","cancelled");xrtValueObjectSetNew(T,XRT_STR_LITERAL("cleanup_pending"),xrtValueBool(!A||!B));(void)MdoExtArchiveSave(T);}xrtValueRelease(T);xrtCancelDestroy(Use->Cancel);xrtFree(Use);xrtCondBroadcast(MdoExtArchiveTasksCondition);}xrtMutexUnlock(MdoExtArchiveTasksLock);
}
char* MdoExtensionTaskArchive(cstr Id,char Error[512]){
 if(Error)Error[0]=0;
 if(!MdoExtArchiveTasksLock||!MdoExtArchiveId(Id)){MdoExtArchiveError(Error,"Choose a sealed task");
 return NULL;
 }
 xrtMutexLock(MdoExtArchiveTasksLock);
 xvalue* T=MdoExtArchiveLoad(Id,Error);
 cstr State=MdoExtensionText(T,"state",24);
 char* Path=State&&MdoExtArchiveLive(T)&&!strcmp(State,"ready")?MdoExtArchivePath(Id,"archive.zip"):NULL;
 uint64 Expected=0;bool Exists=false;xfileinfo Info;if(Path&&(!MdoExtArchiveNumber(T,"bytes",&Expected)||!MdoExtArchiveStat(Path,&Exists,&Info)||!Exists||Info.Type!=XFILE_TYPE_FILE||Info.Size!=Expected)){xrtFree(Path);Path=NULL;}
 xrtValueRelease(T);xrtMutexUnlock(MdoExtArchiveTasksLock);if(!Path)MdoExtArchiveError(Error,"Archive task is not sealed, has expired, was cancelled or its file is unavailable");return Path;
}

bool MdoExtensionTaskPack(cstr Kind,cstr DirectoryId,xcancel* Cancel,xvalue** Result,char Error[512]){
 *Result=NULL;if(Error)Error[0]=0;if(!MdoExtensionDirectoryKind(Kind)||!MdoExtensionDirectoryId(DirectoryId))return MdoExtArchiveError(Error,"Choose a local extension directory to export");
 xvalue* Input=xrtValueObject(),*Task=NULL;char Zero[65];memset(Zero,'0',64);Zero[64]=0;
 bool Ok=Input&&MdoExtArchivePut(Input,"kind",Kind)&&MdoExtArchivePut(Input,"purpose","publish")&&MdoExtArchivePut(Input,"sha256",Zero)&&xrtValueObjectSetNew(Input,XRT_STR_LITERAL("bytes"),xrtValueUInt(MDO_EXTENSION_ARCHIVE_BYTES))&&MdoExtensionTaskBegin(Input,&Task,Error);xrtValueRelease(Input);
 char Id[33]={0};if(Ok)snprintf(Id,sizeof(Id),"%s",MdoExtensionText(Task,"id",32));xrtValueRelease(Task);Task=NULL;
 MdoExtensionTaskUse* Use=Ok?MdoExtensionTaskUseBegin(Id,true,Cancel,Error):NULL;char* Path=Use?MdoExtArchivePath(Id,"archive.part"):NULL;Ok=Ok&&Use&&Path;
 char Hash[65];uint64 Bytes=0;if(Ok)Ok=MdoExtensionsPack(Kind,DirectoryId,Path,MdoExtensionTaskUseCancel(Use),Hash,&Bytes,Error);
 if(Ok){xrtMutexLock(MdoExtArchiveTasksLock);Task=MdoExtArchiveLoad(Id,Error);cstr State=MdoExtensionText(Task,"state",24);
  Ok=Task&&State&&!strcmp(State,"receiving")&&!xrtCancelRequested(MdoExtensionTaskUseCancel(Use))&&MdoExtArchivePut(Task,"sha256",Hash)&&MdoExtArchivePut(Task,"directory_id",DirectoryId)&&xrtValueObjectSetNew(Task,XRT_STR_LITERAL("bytes"),xrtValueUInt(Bytes))&&MdoExtArchiveSave(Task);xrtValueRelease(Task);xrtMutexUnlock(MdoExtArchiveTasksLock);}
 xrtFree(Path);MdoExtensionTaskUseEnd(Use);
 if(Ok)Ok=MdoExtensionTaskSeal(Id,Cancel,Result,Error);
 if(!Ok&&Id[0]){xvalue* Ignored=NULL;char Cleanup[512];(void)MdoExtensionTaskCancel(Id,&Ignored,Cleanup);xrtValueRelease(Ignored);}return Ok;
}
