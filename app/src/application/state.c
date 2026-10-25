#include "internal.h"
#include "../../include/mdo/extension_files.h"

#define SDK_STATE_BYTES (4u*1024u*1024u)
#define SDK_STATE_RECORD_BYTES 16384u
#define SDK_STATE_RECORDS 1024u
static struct {xmutex* Lock;char Epoch[33];} SdkState;
static const cstr SdkStateMethods[]={"state.get","state.list","state.put","state.compare_exchange","state.remove","state.transaction","state.receipt.get","lease.acquire","lease.inspect","lease.validate","lease.renew","lease.release"};
bool MdoApplicationStateInit(void){SdkState.Lock=xrtMutexCreate();return SdkState.Lock&&SdkCallIdentity(SdkState.Epoch);}
void MdoApplicationStateUnit(void){if(SdkState.Lock)xrtMutexDestroy(SdkState.Lock);memset(&SdkState,0,sizeof(SdkState));}
static uint64 SdkStateRevision(const xvalue* V){uint64 N=0;SdkUInt(SdkGet(V,"revision"),&N);return N;}
static bool SdkStateDeleted(const xvalue* V){bool Deleted=false;xrtValueGetBool(SdkGet(V,"deleted"),&Deleted);return Deleted;}
/* One checksummed atomic document per namespace: a transaction never publishes
 * a subset of its keys. Tombstones retain revisions across remove/recreate. */
static xvalue* SdkStateLoad(const mdo_sdk_context_v1* C,const xvalue* P,char Path[160],mdo_error_v1* E){
    cstr Name=SdkField(P,"namespace");if(!Name)Name=C->ExtensionId;
    cstr Project=C->ProjectId&&C->ProjectId[0]?C->ProjectId:"default";
    if(!SdkIdValid(Name)||strlen(Name)>128||!SdkIdValid(Project)){MdoApplicationError(E,"sdk_state_namespace","Select a valid namespace and project","state",400,false);return NULL;}
    char* Identity=xrtFormat("%s\n%s",Project,Name);char Hash[65];bool Hashed=Identity&&MdoExtensionHash(Identity,Hash);xrtFree(Identity);
    if(!Hashed)return NULL;
    snprintf(Path,160,"data/application/state/%s.json",Hash);
    bool Missing=false;char* Text=MdoExtensionRead(Path,true,SDK_STATE_BYTES*2u+4096u,&Missing);
    if(!Text&&Missing){
        xvalue* Root=xrtValueObject();SdkPut(Root,"schema","mdo.state.v1");SdkPut(Root,"namespace",Name);SdkPut(Root,"project",Project);
        SdkPutUInt(Root,"revision",0);xrtValueObjectSetNew(Root,XRT_STR_LITERAL("records"),xrtValueObject());xrtValueObjectSetNew(Root,XRT_STR_LITERAL("requests"),xrtValueObject());return Root;
    }
    bool HadText=Text!=NULL;xvalue* Envelope=SdkParse(Text);xrtFree(Text);cstr Payload=SdkField(Envelope,"payload"),Expected=SdkField(Envelope,"sha256");
    char Actual[65];bool Valid=Payload&&Expected&&strlen(Payload)<=SDK_STATE_BYTES&&MdoExtensionHash(Payload,Actual)&&!strcmp(Actual,Expected);
    xvalue* Root=Valid?SdkParse(Payload):NULL;
    uint64 Revision=0;
    Valid=Root&&SdkField(Root,"schema")&&!strcmp(SdkField(Root,"schema"),"mdo.state.v1")&&
        SdkField(Root,"namespace")&&!strcmp(SdkField(Root,"namespace"),Name)&&SdkField(Root,"project")&&!strcmp(SdkField(Root,"project"),Project)&&
        SdkUInt(SdkGet(Root,"revision"),&Revision)&&xrtValueType(SdkGet(Root,"records"))==XVALUE_OBJECT&&xrtValueCount(SdkGet(Root,"records"))<=SDK_STATE_RECORDS;
    const xvalue* Records=SdkGet(Root,"records");
    for(size_t I=0;Valid&&I<xrtValueCount(Records);I++){
        cstr Key=SdkKey(Records,I);const xvalue* Entry=SdkGet(Records,Key);uint64 ItemRevision=0;bool Deleted=false;
        Valid=SdkIdValid(Key)&&strlen(Key)<=128&&SdkUInt(SdkGet(Entry,"revision"),&ItemRevision)&&ItemRevision>0&&ItemRevision<=Revision&&
            xrtValueGetBool(SdkGet(Entry,"deleted"),&Deleted)&&(Deleted||SdkGet(Entry,"value"));
    }
    const xvalue* Requests=SdkGet(Root,"requests");
    if(Valid&&!Requests){Valid=xrtValueObjectSetNew(Root,XRT_STR_LITERAL("requests"),xrtValueObject());Requests=SdkGet(Root,"requests");}
    if(Valid)Valid=xrtValueType(Requests)==XVALUE_OBJECT&&xrtValueCount(Requests)<=128;
    xrtValueRelease(Envelope);
    if(!Valid){xrtValueRelease(Root);MdoApplicationError(E,HadText?"sdk_state_corrupt":"sdk_state_unreadable","State is unreadable, corrupt or unsupported; original data was preserved","state",422,false);return NULL;}
    return Root;
}
static bool SdkStateSave(cstr Path,const xvalue* Root,mdo_error_v1* E){
    char* Payload=SdkJson(Root);char Hash[65];bool Ok=Payload&&strlen(Payload)<=SDK_STATE_BYTES;
    if(!Ok){xrtFree(Payload);return MdoApplicationError(E,"sdk_state_limit","Namespace exceeds its 4 MiB storage budget","state",413,false);}
    xvalue* Envelope=xrtValueObject();Ok=MdoExtensionHash(Payload,Hash)&&SdkPut(Envelope,"payload",Payload)&&SdkPut(Envelope,"sha256",Hash);xrtFree(Payload);
    char* Text=Ok?SdkJson(Envelope):NULL;xrtValueRelease(Envelope);
    if(!Text)return MdoApplicationError(E,"sdk_state_allocation","Cannot serialize state; no changes published","state",500,false);
    if(SdkStateRevision(Root)==1){
        char* Native=MdoHomeExternalPath("data/application/state");xdir Directory=Native?xrtDirOpen(Native,0):NULL;xrtFree(Native);
        if(Directory){xdirentry Item;size_t Count=0;xdirnext Next;while((Next=xrtDirNext(Directory,&Item))==XDIR_NEXT_ITEM)Count++;
            bool Closed=xrtDirClose(Directory);if(!Closed||Next==XDIR_NEXT_ERROR||Count>=256){xrtFree(Text);return MdoApplicationError(E,"sdk_state_limit","Namespace storage is unavailable or has reached 256 files","state",413,false);}
        }else {const xerror* Error=xrtGetError();if(Error&&xrtErrorKind(Error)!=XERR_NOT_FOUND){xrtFree(Text);return MdoApplicationError(E,"sdk_state_unreadable","Cannot inspect namespace storage","state",503,false);}xrtClearError();}
    }
    Ok=MdoHomeAtomicWrite(Path,Text,strlen(Text),false);xrtFree(Text);
    return Ok||MdoApplicationError(E,"sdk_state_commit_failed","Atomic state publication failed; inspect revisions before retrying","state",503,true);
}
static xvalue* SdkStateEntry(cstr Key,const xvalue* Entry){
    xvalue* R=xrtValueObject();SdkPut(R,"key",Key);SdkPutUInt(R,"revision",SdkStateRevision(Entry));
    bool Exists=Entry&&!SdkStateDeleted(Entry);xrtValueObjectSetNew(R,XRT_STR_LITERAL("exists"),xrtValueBool(Exists));
    if(Exists)xrtValueObjectSetNew(R,XRT_STR_LITERAL("value"),xrtValueClone(SdkGet(Entry,"value")));return R;
}
static bool SdkStateKey(cstr Key){return SdkIdValid(Key)&&strlen(Key)<=128;}
static bool SdkStateCheck(const xvalue* Records,const xvalue* Check,mdo_error_v1* E){
    cstr Key=SdkField(Check,"key");uint64 Expected=0;
    if(!SdkStateKey(Key)||!SdkUInt(SdkGet(Check,"expected_revision"),&Expected))return MdoApplicationError(E,"sdk_state_arguments","Each check/write needs key and expected_revision","state",400,false);
    if(!strncmp(Key,"sys.",4))return MdoApplicationError(E,"sdk_state_reserved","System records are managed through their typed services","state",400,false);
    return SdkStateRevision(SdkGet(Records,Key))==Expected||MdoApplicationError(E,"sdk_state_conflict","State revision changed; reread before updating","state",409,false);
}
static bool SdkStateApply(xvalue* Root,const xvalue* Writes,const xvalue* Checks,xvalue** Out,mdo_error_v1* E){
    xvalue* Records=(xvalue*)SdkGet(Root,"records");
    if(xrtValueType(Writes)!=XVALUE_ARRAY||!xrtValueCount(Writes)||xrtValueCount(Writes)>64||
        (Checks&&xrtValueType(Checks)!=XVALUE_ARRAY)||xrtValueCount(Checks)>64)
        return MdoApplicationError(E,"sdk_state_arguments","Supply 1..64 writes and at most 64 checks","state",400,false);
    for(size_t I=0;I<xrtValueCount(Checks);I++)if(!SdkStateCheck(Records,xrtValueArrayGet(Checks,I),E))return false;
    size_t NewKeys=0;
    for(size_t I=0;I<xrtValueCount(Writes);I++){
        const xvalue* W=xrtValueArrayGet(Writes,I);if(!SdkStateCheck(Records,W,E))return false;
        cstr Key=SdkField(W,"key");bool Remove=false;
        if(SdkGet(W,"remove")&&!xrtValueGetBool(SdkGet(W,"remove"),&Remove))return MdoApplicationError(E,"sdk_state_arguments","remove must be boolean","state",400,false);
        if(Remove&&SdkGet(W,"value"))return MdoApplicationError(E,"sdk_state_arguments","A remove cannot also set a value","state",400,false);
        if(!Remove&&!SdkGet(W,"value"))return MdoApplicationError(E,"sdk_state_arguments","A write must supply value, including explicit null","state",400,false);
        for(size_t J=0;J<I;J++)if(!strcmp(Key,SdkField(xrtValueArrayGet(Writes,J),"key")))return MdoApplicationError(E,"sdk_state_arguments","A transaction cannot write the same key twice","state",400,false);
        char* Json=Remove?NULL:SdkJson(SdkGet(W,"value"));bool TooLarge=Json&&strlen(Json)>SDK_STATE_RECORD_BYTES;xrtFree(Json);
        if(TooLarge)return MdoApplicationError(E,"sdk_state_limit","A state value exceeds 16 KiB","state",413,false);
        if(!SdkGet(Records,Key))NewKeys++;
    }
    uint64 Revision=SdkStateRevision(Root);
    if(Revision==UINT64_MAX||NewKeys>SDK_STATE_RECORDS-xrtValueCount(Records))return MdoApplicationError(E,"sdk_state_limit","Release or compact the namespace; record/revision budget exhausted","state",413,false);
    Revision++;xvalue* Results=xrtValueArray();bool Ok=Results!=NULL;
    for(size_t I=0;Ok&&I<xrtValueCount(Writes);I++){
        const xvalue* W=xrtValueArrayGet(Writes,I);cstr Key=SdkField(W,"key");bool Remove=false;xrtValueGetBool(SdkGet(W,"remove"),&Remove);
        xvalue* Entry=xrtValueObject();Ok=Entry&&SdkPutUInt(Entry,"revision",Revision)&&xrtValueObjectSetNew(Entry,XRT_STR_LITERAL("deleted"),xrtValueBool(Remove));
        if(Ok&&!Remove)Ok=xrtValueObjectSetNew(Entry,XRT_STR_LITERAL("value"),xrtValueClone(SdkGet(W,"value")));
        if(Ok)Ok=xrtValueObjectSet(Records,xrtStrView(Key),Entry)&&xrtValueArrayAppendNew(Results,SdkStateEntry(Key,Entry));xrtValueRelease(Entry);
    }
    if(Ok)Ok=SdkPutUInt(Root,"revision",Revision);
    if(!Ok){xrtValueRelease(Results);return MdoApplicationError(E,"sdk_state_allocation","Cannot construct transaction; no changes published","state",500,false);}
    *Out=xrtValueObject();SdkPutUInt(*Out,"revision",Revision);xrtValueObjectSetNew(*Out,XRT_STR_LITERAL("items"),Results);return *Out!=NULL;
}
/* A retained request receipt is committed in the same document as its effects.
 * Canonical hashing ignores object key order but preserves array/value types. */
static int SdkStateRequest(xvalue* Root,cstr Method,const xvalue* P,xvalue** R,char Hash[65],mdo_error_v1* E){
    cstr Id=SdkField(P,"request_id");if(!Id)return 0;
    if(!SdkStateKey(Id)){MdoApplicationError(E,"sdk_state_arguments","request_id must be a portable identity","state",400,false);return -1;}
    xvalue* Canonical=SdkCanonicalValue(P,0);char* Params=Canonical?SdkJson(Canonical):NULL;char* Text=Params?xrtFormat("%s\n%s",Method,Params):NULL;
    xrtValueRelease(Canonical);xrtFree(Params);bool Ok=Text&&MdoExtensionHash(Text,Hash);xrtFree(Text);
    if(!Ok){MdoApplicationError(E,"sdk_state_arguments","Request cannot be canonicalized","state",400,false);return -1;}
    const xvalue* Requests=SdkGet(Root,"requests"),*Receipt=SdkGet(Requests,Id);
    if(Receipt){
        if(!SdkField(Receipt,"hash")||strcmp(SdkField(Receipt,"hash"),Hash)){MdoApplicationError(E,"sdk_identity_conflict","request_id belongs to a different state mutation","state",409,false);return -1;}
        *R=xrtValueClone(SdkGet(Receipt,"result"));return *R?1:-1;
    }
    if(xrtValueCount(Requests)>=128){MdoApplicationError(E,"sdk_state_limit","Namespace retains 128 request receipts; use a new execution namespace","state",413,false);return -1;}return 0;
}
static bool SdkStateRemember(xvalue* Root,const xvalue* P,cstr Hash,const xvalue* Result){
    cstr Id=SdkField(P,"request_id");if(!Id)return true;
    xvalue* Entry=xrtValueObject();bool Ok=Entry&&SdkPut(Entry,"hash",Hash)&&xrtValueObjectSetNew(Entry,XRT_STR_LITERAL("result"),xrtValueClone(Result))&&
        xrtValueObjectSet((xvalue*)SdkGet(Root,"requests"),xrtStrView(Id),Entry);xrtValueRelease(Entry);return Ok;
}
#include "state_leases.inc.c"
xvalue* MdoApplicationStateCatalog(void){
    xvalue* A=xrtValueArray();for(size_t I=0;I<sizeof(SdkStateMethods)/sizeof(*SdkStateMethods);I++){
        xvalue* V=xrtValueObject();SdkPut(V,"id",SdkStateMethods[I]);SdkPut(V,"domain","state");SdkPut(V,"transport","native");xrtValueArrayAppendNew(A,V);
    }return A;
}
bool MdoApplicationStateCall(const mdo_sdk_context_v1* C,cstr Method,const xvalue* P,xvalue** R,mdo_error_v1* E,bool* Found){
    *Found=false;for(size_t I=0;I<sizeof(SdkStateMethods)/sizeof(*SdkStateMethods);I++)if(!strcmp(Method,SdkStateMethods[I]))*Found=true;
    if(!*Found)return true;
    if(!SdkState.Lock)return MdoApplicationError(E,"sdk_unavailable","State service unavailable","state",503,false);
    char Path[160];bool Read=!strcmp(Method,"state.get"),List=!strcmp(Method,"state.list"),Ok=false,Changed=false;
    xrtMutexLock(SdkState.Lock);xvalue* Root=SdkStateLoad(C,P,Path,E);
    if(!Root){xrtMutexUnlock(SdkState.Lock);return false;}
    const xvalue* Records=SdkGet(Root,"records");
    if(!strncmp(Method,"lease.",6)){
        Ok=SdkLeaseApply(Method,P,Root,R,E,&Changed);
        if(Ok&&Changed)Ok=SdkStateSave(Path,Root,E);
        if(!Ok){xrtValueRelease(*R);*R=NULL;}
    }
    else if(Read){cstr Key=SdkField(P,"key");Ok=SdkStateKey(Key)&&strncmp(Key,"sys.",4);if(Ok)*R=SdkStateEntry(Key,SdkGet(Records,Key));else MdoApplicationError(E,"sdk_state_arguments","A valid key is required","state",400,false);}
    else if(!strcmp(Method,"state.receipt.get")){
        cstr Id=SdkField(P,"request_id");const xvalue* Receipt=SdkStateKey(Id)?SdkGet(SdkGet(Root,"requests"),Id):NULL;
        if(Receipt){*R=xrtValueClone(Receipt);Ok=*R!=NULL;}else MdoApplicationError(E,"sdk_receipt_missing","Request receipt is unavailable","state",404,false);
    }
    else if(List){
        uint64 Offset=0,Limit=64,Expected=0;
        Ok=(!SdkGet(P,"offset")||SdkUInt(SdkGet(P,"offset"),&Offset))&&(!SdkGet(P,"limit")||SdkUInt(SdkGet(P,"limit"),&Limit))&&Limit&&Limit<=256;
        if(!Ok)MdoApplicationError(E,"sdk_state_arguments","Invalid list offset or limit","state",400,false);
        else if(SdkGet(P,"expected_revision")&&(!SdkUInt(SdkGet(P,"expected_revision"),&Expected)||Expected!=SdkStateRevision(Root)))Ok=MdoApplicationError(E,"sdk_state_conflict","Namespace changed during pagination","state",409,false);
        if(Ok){*R=xrtValueObject();xvalue* Items=xrtValueArray();size_t Count=xrtValueCount(Records);uint64 At=Offset;
            while(At<Count&&xrtValueCount(Items)<Limit){cstr Key=SdkKey(Records,(size_t)At++);const xvalue* Entry=SdkGet(Records,Key);if(SdkStateDeleted(Entry)||!strncmp(Key,"sys.",4))continue;
                xvalue* Item=xrtValueObject();SdkPut(Item,"key",Key);SdkPutUInt(Item,"revision",SdkStateRevision(Entry));xrtValueArrayAppendNew(Items,Item);}
            SdkPutUInt(*R,"revision",SdkStateRevision(Root));SdkPutUInt(*R,"next",At);xrtValueObjectSetNew(*R,XRT_STR_LITERAL("has_more"),xrtValueBool(At<Count));xrtValueObjectSetNew(*R,XRT_STR_LITERAL("items"),Items);
        }
    }else{
        char Hash[65]={0};int Replayed=SdkStateRequest(Root,Method,P,R,Hash,E);
        if(Replayed){xrtValueRelease(Root);xrtMutexUnlock(SdkState.Lock);return Replayed>0;}
        xvalue* Writes=NULL;const xvalue* Checks=SdkGet(P,"checks");
        if(!strcmp(Method,"state.transaction"))Writes=xrtValueClone(SdkGet(P,"writes"));
        else {Writes=xrtValueArray();xvalue* W=xrtValueClone(P);if(!strcmp(Method,"state.remove"))xrtValueObjectSetNew(W,XRT_STR_LITERAL("remove"),xrtValueBool(true));xrtValueArrayAppendNew(Writes,W);}
        Ok=(!SdkGet(P,"lease_guard")||SdkLeaseGuard(Root,SdkGet(P,"lease_guard"),E))&&SdkStateApply(Root,Writes,Checks,R,E);xrtValueRelease(Writes);
        if(Ok&&!SdkStateRemember(Root,P,Hash,*R))Ok=MdoApplicationError(E,"sdk_state_allocation","Cannot construct receipt; no changes published","state",500,false);
        if(Ok){Ok=SdkStateSave(Path,Root,E);Changed=Ok;}
        if(!Ok){xrtValueRelease(*R);*R=NULL;}
    }
    xvalue* Event=Ok&&Changed?xrtValueObject():NULL;
    if(Event){SdkPut(Event,"namespace",SdkField(Root,"namespace"));SdkPutUInt(Event,"revision",SdkStateRevision(Root));}
    xrtValueRelease(Root);xrtMutexUnlock(SdkState.Lock);
    if(Event){MdoApplicationEvent("state.changed",C,Event);xrtValueRelease(Event);}
    return Ok&&*R;
}
