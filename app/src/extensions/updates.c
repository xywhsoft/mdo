#include "../../include/mdo/extension_updates.h"
#include "../../include/mdo/extensions.h"
#include "../../include/mdo/extension_service.h"
#include "../../include/mdo/extension_manifest.h"
#include "../../include/mdo/extension_files.h"
#include "../../include/mdo/home.h"
#include "../../include/mdo/time.h"

#define MDO_UPDATES_CACHE "data/cache/extension-updates.json"
#define MDO_UPDATES_INTERVAL_US (21600ULL*1000000ULL)
static struct {xmutex* Lock;xthread* Thread;xcancel* Cancel;xvalue* Cache;bool Requested,Busy;char Error[512];} MdoUpdates;
void MdoApiLiveChanged(void* Data);
static bool MdoUpdatesPut(xvalue* V,cstr K,cstr S){return S&&xrtValueObjectSetNew(V,xrtStrView(K),xrtValueString(xrtStrView(S)));}
static uint64 MdoUpdatesNumber(const xvalue* V,cstr K){uint64 N=0;int64 I=0;const xvalue* X=xrtValueObjectGet(V,xrtStrView(K));return xrtValueGetUInt(X,&N)?N:xrtValueGetInt(X,&I)&&I>0?(uint64)I:0;}
static bool MdoUpdatesSame(cstr A,cstr B){return A&&B&&!strcmp(A,B);}
static bool MdoUpdatesDigest(const xvalue* Rows,char Hash[65]){char* Text=xrtJsonStringify(Rows,false,NULL);uint8 D[32];bool Ok=Text&&xrtSha256(Text,strlen(Text),D);xrtFree(Text);if(Ok)for(size_t I=0;I<32;I++)snprintf(Hash+I*2,3,"%02x",D[I]);return Ok;}
static xvalue* MdoUpdatesInventory(char Error[512]){
 xvalue* All=MdoExtensionsSnapshot(Error),*Rows=xrtValueArray();bool Ok=All&&Rows;
 for(size_t I=0;Ok&&I<xrtValueCount(All);I++){const xvalue* R=xrtValueArrayGet(All,I),*Receipt=xrtValueObjectGet(R,XRT_STR_LITERAL("installation"));
  cstr Source=MdoExtensionText(R,"source",16),Id=MdoExtensionText(R,"id",MDO_EXTENSION_REF_BYTES),Work=MdoExtensionText(Receipt,"xid",32),Hash=MdoExtensionText(Receipt,"sha256",64);
  if(!Source||strcmp(Source,"store")||!MdoExtensionRefValid(Id)||!Work||!Hash)continue;
#if defined(__ANDROID__)
  if(MdoUpdatesSame(MdoExtensionText(R,"kind",32),"external-tools"))continue;
#endif
  xvalue* Item=xrtValueObject();const cstr Fields[]={"id","kind","directory_id","name","version","enabled"};Ok=Item!=NULL;
  for(size_t J=0;Ok&&J<6;J++){const xvalue* V=xrtValueObjectGet(R,xrtStrView(Fields[J]));if(V)Ok=xrtValueObjectSet(Item,xrtStrView(Fields[J]),V);}
  Ok=Ok&&MdoUpdatesPut(Item,"work",Work)&&MdoUpdatesPut(Item,"sha256",Hash)&&xrtValueObjectSet(Item,XRT_STR_LITERAL("installation"),Receipt)&&xrtValueArrayAppend(Rows,Item);xrtValueRelease(Item);
 }xrtValueRelease(All);if(!Ok){xrtValueRelease(Rows);return NULL;}return Rows;
}
static const xvalue* MdoUpdatesFind(const xvalue* Rows,cstr Id){for(size_t I=0;Id&&I<xrtValueCount(Rows);I++){const xvalue* R=xrtValueArrayGet(Rows,I);if(MdoUpdatesSame(Id,MdoExtensionText(R,"id",MDO_EXTENSION_REF_BYTES)))return R;}return NULL;}
static bool MdoUpdatesCandidate(const xvalue* Local,const xvalue* Remote,uint64 Serial){
 return Serial&&MdoUpdatesNumber(Remote,"serial")>Serial&&MdoUpdatesSame(MdoExtensionText(Local,"id",MDO_EXTENSION_REF_BYTES),MdoExtensionText(Remote,"id",MDO_EXTENSION_REF_BYTES))&&MdoUpdatesSame(MdoExtensionText(Local,"work",32),MdoExtensionText(Remote,"xid",32))&&MdoUpdatesSame(MdoExtensionText(Local,"kind",32),MdoExtensionText(Remote,"kind",32))&&!MdoUpdatesSame(MdoExtensionText(Local,"sha256",64),MdoExtensionText(Remote,"sha256",64));
}
static bool MdoUpdatesFetch(xvalue* Inventory,xvalue** Result,char Error[512]){
 *Result=NULL;xvalue* Rows=xrtValueArray();bool Ok=Rows!=NULL;
 for(size_t Offset=0;Ok&&Offset<xrtValueCount(Inventory);Offset+=24){char Refs[4097]={0};size_t Used=0,End=Offset+24;if(End>xrtValueCount(Inventory))End=xrtValueCount(Inventory);
  for(size_t I=Offset;Ok&&I<End;I++){cstr Id=MdoExtensionText(xrtValueArrayGet(Inventory,I),"id",MDO_EXTENSION_REF_BYTES);int N=Id?snprintf(Refs+Used,sizeof(Refs)-Used,"%s%s",Used?",":"",Id):-1;Ok=N>0&&(size_t)N<sizeof(Refs)-Used;if(Ok)Used+=(size_t)N;}
  xvalue* Request=xrtValueObject(),*Response=NULL;Ok=Ok&&Request&&MdoUpdatesPut(Request,"action","updates")&&MdoUpdatesPut(Request,"refs",Refs)&&MdoExtensionServiceRead(Request,MdoUpdates.Cancel,&Response,Error);xrtValueRelease(Request);
  const xvalue* Items=xrtValueObjectGet(Response,XRT_STR_LITERAL("items"));Ok=Ok&&xrtValueType(Items)==XVALUE_ARRAY&&xrtValueCount(Items)<=End-Offset;
  xvalue* Seen=xrtValueObject();Ok=Ok&&Seen;
  for(size_t I=0;Ok&&I<xrtValueCount(Items);I++){const xvalue* Remote=xrtValueArrayGet(Items,I);cstr Id=MdoExtensionText(Remote,"id",MDO_EXTENSION_REF_BYTES);const xvalue* Local=MdoUpdatesFind(Inventory,Id);Ok=Local&&Id&&!xrtValueObjectHas(Seen,xrtStrView(Id))&&xrtValueObjectSetNew(Seen,xrtStrView(Id),xrtValueBool(true));
   if(!Ok)break;const xvalue* Receipt=xrtValueObjectGet(Local,XRT_STR_LITERAL("installation"));uint64 Serial=MdoUpdatesNumber(Receipt,"serial");
   if(!Serial){cstr Version=MdoExtensionText(Receipt,"version_id",32);xvalue* Query=xrtValueObject(),*Prior=NULL;
    bool Read=Version&&Query&&MdoUpdatesPut(Query,"action","detail")&&MdoUpdatesPut(Query,"version_id",Version)&&MdoExtensionServiceRead(Query,MdoUpdates.Cancel,&Prior,Error);
    if(Read&&MdoUpdatesSame(MdoExtensionText(Prior,"xid",32),MdoExtensionText(Local,"work",32))&&MdoUpdatesSame(MdoExtensionText(Prior,"sha256",64),MdoExtensionText(Local,"sha256",64)))Serial=MdoUpdatesNumber(Prior,"serial");
    xrtValueRelease(Query);xrtValueRelease(Prior);if(!Read){Ok=false;break;}
   }
   if(MdoUpdatesCandidate(Local,Remote,Serial)){xvalue* Item=xrtValueDeepClone(Remote);const cstr Fields[]={"version","sha256","directory_id","enabled"},Names[]={"installed_version","installed_sha256","directory_id","enabled"};
    for(size_t J=0;Item&&J<4;J++){const xvalue* V=xrtValueObjectGet(Local,xrtStrView(Fields[J]));if(V&&!xrtValueObjectSet(Item,xrtStrView(Names[J]),V)){xrtValueRelease(Item);Item=NULL;}}
    Ok=Item&&xrtValueArrayAppend(Rows,Item);xrtValueRelease(Item);
   }
  }xrtValueRelease(Seen);xrtValueRelease(Response);
 }
 xvalue* Out=Ok?xrtValueObject():NULL;char Hash[65]={0};Ok=Ok&&Out&&MdoUpdatesDigest(Inventory,Hash)&&MdoUpdatesPut(Out,"inventory",Hash)&&MdoUpdatesPut(Out,"platform",MdoExtensionRuntimePlatform())&&xrtValueObjectSet(Out,XRT_STR_LITERAL("items"),Rows)&&xrtValueObjectSetNew(Out,XRT_STR_LITERAL("checked_at"),xrtValueInt(MdoUtcUs()/1000000));xrtValueRelease(Rows);
 if(!Ok){xrtValueRelease(Out);if(Error&&!Error[0])snprintf(Error,512,"Extension update metadata is unavailable or invalid");return false;}*Result=Out;return true;
}
static int32 MdoUpdatesWorker(void* Data){
 (void)Data;uint64 Next=MdoDeadlineAfterUs(5000000),Inspect=0;unsigned Failures=0;char Last[65]={0};
 while(!xrtThreadStopping()&&!xrtCancelRequested(MdoUpdates.Cancel)){
  xrtMutexLock(MdoUpdates.Lock);bool Requested=MdoUpdates.Requested;MdoUpdates.Requested=false;xrtMutexUnlock(MdoUpdates.Lock);
  bool Due=Requested||MdoDeadlineExpired(Next);char Error[512]={0};xvalue* Inventory=NULL;
  if(Due||!Inspect||MdoDeadlineExpired(Inspect)){Inventory=MdoUpdatesInventory(Error);char Hash[65]={0};if(Inventory&&MdoUpdatesDigest(Inventory,Hash)){if(Last[0]&&strcmp(Last,Hash))Due=true;snprintf(Last,sizeof(Last),"%s",Hash);}Inspect=MdoDeadlineAfterUs(30000000);}
  if(Due){if(!Inventory)Inventory=MdoUpdatesInventory(Error);xrtMutexLock(MdoUpdates.Lock);MdoUpdates.Busy=true;xrtMutexUnlock(MdoUpdates.Lock);MdoApiLiveChanged(NULL);
   xvalue* Result=NULL;bool Ok=Inventory&&MdoUpdatesFetch(Inventory,&Result,Error);
   if(Ok){char* Text=xrtJsonStringify(Result,false,NULL);if(Text&&strlen(Text)<=2097152)(void)MdoHomeAtomicWrite(MDO_UPDATES_CACHE,Text,strlen(Text),false);xrtFree(Text);}
   xrtMutexLock(MdoUpdates.Lock);if(Ok){xrtValueRelease(MdoUpdates.Cache);MdoUpdates.Cache=Result;Result=NULL;}MdoUpdates.Busy=false;snprintf(MdoUpdates.Error,sizeof(MdoUpdates.Error),"%s",Ok?"":Error[0]?Error:"Unable to inspect installed extensions");xrtMutexUnlock(MdoUpdates.Lock);xrtValueRelease(Result);
   Failures=Ok?0:Failures<4?Failures+1:4;Next=MdoDeadlineAfterUs(Ok?MDO_UPDATES_INTERVAL_US:(60000000ULL<<(Failures?Failures-1:0)));MdoApiLiveChanged(NULL);
  }xrtValueRelease(Inventory);xrtSleep(100);
 }return 0;
}
bool MdoExtensionUpdatesInit(void){
 if(MdoUpdates.Lock)return true;MdoUpdates.Lock=xrtMutexCreate();MdoUpdates.Cancel=xrtCancelCreate();bool Missing=false;char* Text=MdoExtensionRead(MDO_UPDATES_CACHE,true,2097152,&Missing);MdoUpdates.Cache=Text?xrtJsonParse(xrtStrView(Text)):NULL;xrtFree(Text);
 if(MdoUpdates.Cache&&!MdoUpdatesSame(MdoExtensionText(MdoUpdates.Cache,"platform",32),MdoExtensionRuntimePlatform())){xrtValueRelease(MdoUpdates.Cache);MdoUpdates.Cache=NULL;}
 if(!MdoUpdates.Cache)MdoUpdates.Cache=xrtValueObject();MdoUpdates.Busy=true;
 if(!MdoUpdates.Lock||!MdoUpdates.Cancel||!MdoUpdates.Cache){MdoExtensionUpdatesUnit();return false;}MdoUpdates.Thread=xrtThreadCreate(MdoUpdatesWorker,NULL,0);if(!MdoUpdates.Thread){MdoExtensionUpdatesUnit();return false;}return true;
}
void MdoExtensionUpdatesUnit(void){if(MdoUpdates.Cancel)xrtCancelRequest(MdoUpdates.Cancel);if(MdoUpdates.Thread){xrtThreadStop(MdoUpdates.Thread);xrtThreadWait(MdoUpdates.Thread);xrtThreadDestroy(MdoUpdates.Thread);}xrtValueRelease(MdoUpdates.Cache);xrtCancelDestroy(MdoUpdates.Cancel);if(MdoUpdates.Lock)xrtMutexDestroy(MdoUpdates.Lock);memset(&MdoUpdates,0,sizeof(MdoUpdates));}
bool MdoExtensionUpdatesRequest(void){if(!MdoUpdates.Lock)return false;xrtMutexLock(MdoUpdates.Lock);if(!MdoUpdates.Busy)MdoUpdates.Requested=true;xrtMutexUnlock(MdoUpdates.Lock);return true;}
xvalue* MdoExtensionUpdatesSnapshot(void){
 if(!MdoUpdates.Lock)return NULL;char Error[512]={0};xvalue* Inventory=MdoUpdatesInventory(Error),*Out=NULL,*Rows=xrtValueArray();char Hash[65]={0};bool Read=Inventory&&MdoUpdatesDigest(Inventory,Hash);
 xrtMutexLock(MdoUpdates.Lock);Out=xrtValueDeepClone(MdoUpdates.Cache);bool Busy=MdoUpdates.Busy||MdoUpdates.Requested;char Message[512];snprintf(Message,sizeof(Message),"%s",Error[0]?Error:MdoUpdates.Error);xrtMutexUnlock(MdoUpdates.Lock);
 const xvalue* Cached=xrtValueObjectGet(Out,XRT_STR_LITERAL("items"));
 for(size_t I=0;Read&&Rows&&I<xrtValueCount(Cached);I++){const xvalue* R=xrtValueArrayGet(Cached,I),*Local=MdoUpdatesFind(Inventory,MdoExtensionText(R,"id",MDO_EXTENSION_REF_BYTES));if(Local&&MdoUpdatesSame(MdoExtensionText(Local,"sha256",64),MdoExtensionText(R,"installed_sha256",64)))xrtValueArrayAppend(Rows,R);}
 bool Stale=!Read||!MdoUpdatesSame(Hash,MdoExtensionText(Out,"inventory",64))||Message[0]||MdoUpdatesNumber(Out,"checked_at")+21600<(uint64)(MdoUtcUs()/1000000);
 bool Ok=Out&&Rows&&xrtValueObjectSet(Out,XRT_STR_LITERAL("items"),Rows)&&xrtValueObjectSetNew(Out,XRT_STR_LITERAL("busy"),xrtValueBool(Busy))&&xrtValueObjectSetNew(Out,XRT_STR_LITERAL("stale"),xrtValueBool(Stale))&&MdoUpdatesPut(Out,"message",Message);
 xrtValueRelease(Rows);xrtValueRelease(Inventory);if(!Ok){xrtValueRelease(Out);return NULL;}return Out;
}
