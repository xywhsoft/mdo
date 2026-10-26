#include "internal.h"
#include "../../include/mdo/extension_files.h"
#include "../../include/mdo/session_external.h"

#define SDK_STATE_BYTES (4u*1024u*1024u)
#define SDK_STATE_RECORD_BYTES 16384u
#define SDK_STATE_RECORDS 1024u
#define SDK_STATE_SHARD_FILES 256u
#define SDK_STATE_SHARD_BYTES (16u*1024u*1024u)
typedef struct SdkMemberActive {char Identity[65];xcancel* Cancel;} SdkMemberActive;
static struct {xmutex* Lock;xcond* Drained;char Epoch[33];SdkMemberActive Members[8];bool Stopping;} SdkState;
static const cstr SdkStateMethods[]={"state.get","state.list","state.put","state.compare_exchange","state.remove","state.transaction","state.receipt.get","lease.acquire","lease.inspect","lease.validate","lease.renew","lease.release","mailbox.send","mailbox.get","mailbox.read","mailbox.ack","mailbox.wait","instance.create","instance.get","instance.list","instance.history","instance.recovery","instance.send","instance.step","instance.cancel","instance.close","instance.result","instance.wait","instance.resume","instance.abandon","work.create","work.get","work.list","work.claim","work.complete","work.release","work.cancel"};
bool MdoApplicationStateInit(void){SdkState.Lock=xrtMutexCreate();SdkState.Drained=xrtCondCreate();return SdkState.Lock&&SdkState.Drained&&SdkCallIdentity(SdkState.Epoch);}
void MdoApplicationInstancesStop(void){
    if(!SdkState.Lock)return;xrtMutexLock(SdkState.Lock);SdkState.Stopping=true;
    for(size_t I=0;I<8;I++)if(SdkState.Members[I].Cancel)xrtCancelRequest(SdkState.Members[I].Cancel);
    for(;;){bool Active=false;for(size_t I=0;I<8;I++)if(SdkState.Members[I].Cancel)Active=true;if(!Active)break;xrtCondWait(SdkState.Drained,SdkState.Lock);}
    xrtMutexUnlock(SdkState.Lock);
}
void MdoApplicationStateUnit(void){MdoApplicationInstancesStop();if(SdkState.Drained)xrtCondDestroy(SdkState.Drained);if(SdkState.Lock)xrtMutexDestroy(SdkState.Lock);memset(&SdkState,0,sizeof(SdkState));}
static uint64 SdkStateRevision(const xvalue* V){uint64 N=0;SdkUInt(SdkGet(V,"revision"),&N);return N;}
static bool SdkStateDeleted(const xvalue* V){bool Deleted=false;xrtValueGetBool(SdkGet(V,"deleted"),&Deleted);return Deleted;}
/* Keep the logical flat path stable: persistent member directory identities
 * already derive from it. Only the state document's physical location changes.
 * Existing flat documents stay in place; new documents use 256 bounded shards. */
static bool SdkStatePhysical(cstr Logical,char Physical[160],bool* Legacy,mdo_error_v1* E){
    cstr Leaf=strrchr(Logical,'/');Leaf=Leaf?Leaf+1:Logical;
    if(strlen(Leaf)!=69||strcmp(Leaf+64,".json"))return MdoApplicationError(E,"sdk_state_path","Invalid namespace storage identity","state",500,false);
    char Shard[160];snprintf(Shard,sizeof(Shard),"data/application/state/%.2s/%s",Leaf,Leaf);
    bool FlatExists=false,ShardExists=false;xfileinfo Info;
    if(!MdoHomeExternalStat(Logical,&FlatExists,&Info)||!MdoHomeExternalStat(Shard,&ShardExists,&Info))
        return MdoApplicationError(E,"sdk_state_unreadable","Cannot inspect namespace storage","state",503,false);
    if(FlatExists&&ShardExists)return MdoApplicationError(E,"sdk_state_ambiguous","Both legacy and sharded state exist; neither copy was overwritten","state",409,false);
    *Legacy=FlatExists;snprintf(Physical,160,"%s",FlatExists?Logical:Shard);return true;
}
static bool SdkStateShardBudget(cstr Physical,size_t Bytes,mdo_error_v1* E){
    char DirectoryPath[160];snprintf(DirectoryPath,sizeof(DirectoryPath),"%s",Physical);char* Slash=strrchr(DirectoryPath,'/');
    cstr Leaf=strrchr(Physical,'/');if(!Slash||!Leaf)return false;*Slash=0;Leaf++;
    char* Native=MdoHomeExternalPath(DirectoryPath);xdir Directory=Native?xrtDirOpen(Native,XDIR_STAT):NULL;xrtFree(Native);
    if(!Directory){const xerror* Error=xrtGetError();bool Missing=Error&&xrtErrorKind(Error)==XERR_NOT_FOUND;xrtClearError();
        return Missing||MdoApplicationError(E,"sdk_state_unreadable","Cannot inspect namespace shard","state",503,false);}
    xdirentry Item;xdirnext Next=XDIR_NEXT_ERROR;size_t Count=0,OtherBytes=0;bool Existing=false,Ok=true;
    while((Next=xrtDirNext(Directory,&Item))==XDIR_NEXT_ITEM){
        if(Item.Info.Type!=XFILE_TYPE_FILE||!(Item.Info.Available&XFILE_INFO_SIZE)||Item.Info.Size>SDK_STATE_SHARD_BYTES){Ok=false;break;}
        Count++;if(xrtStrEqual(Item.Name,xrtStrView(Leaf)))Existing=true;
        else if(Item.Info.Size>SDK_STATE_SHARD_BYTES-OtherBytes){Ok=false;break;}
        else OtherBytes+=(size_t)Item.Info.Size;
        if(Count>SDK_STATE_SHARD_FILES){Ok=false;break;}
    }
    bool Closed=xrtDirClose(Directory);
    if(!Ok||!Closed||Next!=XDIR_NEXT_END)return MdoApplicationError(E,"sdk_state_unreadable","Namespace shard contains unsupported or unreadable storage","state",503,false);
    if((!Existing&&Count>=SDK_STATE_SHARD_FILES)||Bytes>SDK_STATE_SHARD_BYTES-OtherBytes)
        return MdoApplicationError(E,"sdk_state_limit","Namespace shard reached its 256-file or 16 MiB budget; existing state was preserved","state",413,false);
    return true;
}
/* One checksummed atomic document per namespace: a transaction never publishes
 * a subset of its keys. Tombstones retain revisions across remove/recreate. */
static xvalue* SdkStateLoad(const mdo_sdk_context_v1* C,const xvalue* P,char Path[160],mdo_error_v1* E){
    cstr Name=SdkField(P,"namespace");if(!Name)Name=C->Namespace&&C->Namespace[0]?C->Namespace:C->ExtensionId;
    cstr Project=C->ProjectId&&C->ProjectId[0]?C->ProjectId:"default";
    if(!SdkIdValid(Name)||strlen(Name)>128||!SdkIdValid(Project)){MdoApplicationError(E,"sdk_state_namespace","Select a valid namespace and project","state",400,false);return NULL;}
    char* Identity=xrtFormat("%s\n%s",Project,Name);char Hash[65];bool Hashed=Identity&&MdoExtensionHash(Identity,Hash);xrtFree(Identity);
    if(!Hashed)return NULL;
    snprintf(Path,160,"data/application/state/%s.json",Hash);
    char Physical[160];bool Legacy=false;if(!SdkStatePhysical(Path,Physical,&Legacy,E))return NULL;
    bool Missing=false;char* Text=MdoExtensionRead(Physical,true,SDK_STATE_BYTES*2u+4096u,&Missing);
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
    char Physical[160];bool Legacy=false;
    if(!SdkStatePhysical(Path,Physical,&Legacy,E)||(!Legacy&&!SdkStateShardBudget(Physical,strlen(Text),E))){xrtFree(Text);return false;}
    Ok=MdoHomeAtomicWrite(Physical,Text,strlen(Text),false);xrtFree(Text);
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
#include "state_mailbox.inc.c"
#include "state_instances.inc.c"
#include "state_work.inc.c"
xvalue* MdoApplicationStateCatalog(void){
    xvalue* A=xrtValueArray();for(size_t I=0;I<sizeof(SdkStateMethods)/sizeof(*SdkStateMethods);I++){
        xvalue* V=xrtValueObject();SdkPut(V,"id",SdkStateMethods[I]);SdkPut(V,"domain",!strncmp(SdkStateMethods[I],"work.",5)?"work":!strncmp(SdkStateMethods[I],"instance.",9)?"instance":!strncmp(SdkStateMethods[I],"mailbox.",8)?"mailbox":!strncmp(SdkStateMethods[I],"lease.",6)?"lease":"state");SdkPut(V,"transport","native");xrtValueArrayAppendNew(A,V);
    }return A;
}
bool MdoApplicationStateCall(const mdo_sdk_context_v1* C,cstr Method,const xvalue* P,xvalue** R,mdo_error_v1* E,bool* Found){
    *Found=false;for(size_t I=0;I<sizeof(SdkStateMethods)/sizeof(*SdkStateMethods);I++)if(!strcmp(Method,SdkStateMethods[I]))*Found=true;
    if(!*Found)return true;
    if(!SdkState.Lock)return MdoApplicationError(E,"sdk_unavailable","State service unavailable","state",503,false);
    if(!strncmp(Method,"instance.",9))return SdkInstanceCall(C,Method,P,R,E);
    if(!strcmp(Method,"mailbox.wait"))return SdkMailboxWait(C,P,R,E);
    char Path[160];bool Read=!strcmp(Method,"state.get"),List=!strcmp(Method,"state.list"),Ok=false,Changed=false;
    xrtMutexLock(SdkState.Lock);xvalue* Root=SdkStateLoad(C,P,Path,E);
    if(!Root){xrtMutexUnlock(SdkState.Lock);return false;}
    const xvalue* Records=SdkGet(Root,"records");
    if(!strncmp(Method,"work.",5)){
        Ok=SdkWorkApply(Method,P,Root,R,E,&Changed);if(Ok&&Changed)Ok=SdkStateSave(Path,Root,E);if(!Ok){xrtValueRelease(*R);*R=NULL;}
    }
    else if(!strncmp(Method,"mailbox.",8)){
        Ok=SdkMailboxApply(C,Method,P,Root,R,E,&Changed);
        if(Ok&&Changed)Ok=SdkStateSave(Path,Root,E);
        if(!Ok){xrtValueRelease(*R);*R=NULL;}
    }
    else if(!strncmp(Method,"lease.",6)){
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
    if(Event){MdoApplicationEvent(!strncmp(Method,"work.",5)?"work.changed":!strncmp(Method,"mailbox.",8)?"mailbox.changed":!strncmp(Method,"lease.",6)?"lease.changed":"state.changed",C,Event);xrtValueRelease(Event);}
    return Ok&&*R;
}
#include "state_run_route.inc.c"
