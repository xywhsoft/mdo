/* A plan is host-owned inert metadata. Installation publishes one recoverable
 * set of directories and one state file; no archive hook runs during staging. */
#define MDO_EXT_GROUPS "data/extension-groups"
#define MDO_EXT_PLANS "data/extension-plans"
#define MDO_EXT_PLAN_LIMIT 128u
#define MDO_EXT_PLAN_BYTES (4u*1024u*1024u)
#include "../../include/mdo/time.h"
#define MDO_EXT_PLAN_TTL 86400u
static bool MdoExtGroupNumber(const xvalue* Object,cstr Key,uint64* Number){const xvalue* V=xrtValueObjectGet(Object,xrtStrView(Key));int64 N=0;if(xrtValueGetUInt(V,Number))return true;if(!xrtValueGetInt(V,&N)||N<0)return false;*Number=(uint64)N;return true;}
static bool MdoExtPlanCreated(const xvalue* Plan,uint64* Created){return MdoExtGroupNumber(Plan,"created",Created);}
static bool MdoExtPlanLive(const xvalue* Plan){uint64 Created=0;int64 Now=MdoUtcUs()/1000000;return Now>0&&MdoExtPlanCreated(Plan,&Created)&&Created>0&&Created<=INT64_MAX-MDO_EXT_PLAN_TTL&&Created<=(uint64)Now+300u&&(uint64)Now<=Created+MDO_EXT_PLAN_TTL;}
/* Keep retries for 24 hours, while bounding abandoned reviews and receipts. */
static bool MdoExtPlansPrune(char Error[512]){
    xdir D=MdoHomeOpenDirectory(MDO_EXT_PLANS,XDIR_STAT);if(!D)return MdoExtMissing();xdirentry E;xdirnext Next=XDIR_NEXT_END;size_t Entries=0,Live=0;bool Ok=true;
    while(Ok&&(Next=xrtDirNext(D,&E))==XDIR_NEXT_ITEM){
        if(++Entries>512||!(E.Flags&XDIR_ENTRY_UTF8)||E.Info.Type!=XFILE_TYPE_FILE){Ok=false;break;}
        if(E.Name.Size!=37||memcmp(E.Name.Data+32,".json",5))continue;
        char Id[33];memcpy(Id,E.Name.Data,32);Id[32]=0;if(!MdoExtensionDirectoryId(Id)){Ok=false;break;}
        char* Path=xrtFormat(MDO_EXT_PLANS "/%s.json",Id);xvalue* P=Path?MdoExtReadJson(Path,MDO_EXT_PLAN_BYTES,false):NULL;uint64 Created=0;
        bool Expired=P&&MdoExtPlanCreated(P,&Created)&&Created>0&&Created<=INT64_MAX-MDO_EXT_PLAN_TTL&&MdoUtcUs()/1000000>(int64)(Created+MDO_EXT_PLAN_TTL);
        if(Expired){char* Receipt=xrtFormat(MDO_EXT_PLANS "/%s.receipt.json",Id);bool Present=false;xfileinfo Info;Ok=Receipt&&MdoExtStat(Receipt,&Present,&Info)&&(!Present||MdoHomeRemove(Receipt,false))&&MdoHomeRemove(Path,false);xrtFree(Receipt);}else Live++;
        xrtValueRelease(P);xrtFree(Path);
    }
    if(Next==XDIR_NEXT_ERROR)Ok=false;
    if(!xrtDirClose(D))Ok=false;
    return (Ok&&Live<128)||MdoExtError(Error,"Dependency review storage is full or unreadable. Cancel unused plans or retry after their 24-hour expiry; existing receipts were preserved");
}
static bool MdoExtGroupSaveReceipt(cstr Plan,const xvalue* Receipt);
#include "group_plugins.inc.c"
static bool MdoExtValueHash(const xvalue* Value,char Hash[65]){char* Json=xrtJsonStringify(Value,false,NULL);uint8 Digest[32];bool Ok=Json&&xrtSha256(Json,strlen(Json),Digest);if(Ok)for(size_t I=0;I<32;I++)snprintf(Hash+I*2,3,"%02x",Digest[I]);xrtFree(Json);return Ok;}
static bool MdoExtGroupDigest(cstr Root,cstr Expected,char Error[512]){
    MdoExtTree T={0};char Actual[65];bool Ok=Expected&&strlen(Expected)==64&&MdoExtCollect(Root,&T,NULL,Error)&&MdoExtFingerprint(Root,&T,Actual,NULL)&&!strcmp(Actual,Expected);MdoExtTreeUnit(&T);return Ok;
}
static bool MdoExtGroupCleanup(cstr Base,size_t Count){
    bool Ok=Count<=MDO_EXT_PLAN_LIMIT;for(size_t I=0;Ok&&I<Count;I++){char* Part=xrtFormat("%s/%u",Base,(unsigned)I);bool Present=false;xfileinfo Info;Ok=Part&&MdoExtStat(Part,&Present,&Info)&&(!Present||MdoExtRemoveTree(Part));xrtFree(Part);}
    xdir D=Ok?MdoHomeOpenDirectory(Base,XDIR_STAT):NULL;xdirentry E;xdirnext Next=XDIR_NEXT_END;Ok=Ok&&D;
    while(Ok&&(Next=xrtDirNext(D,&E))==XDIR_NEXT_ITEM){if(!(E.Flags&XDIR_ENTRY_UTF8)||E.Info.Type==XFILE_TYPE_DIRECTORY){Ok=false;break;}if(E.Name.Size==10&&!memcmp(E.Name.Data,"stage.json",10))continue;char* Path=xrtFormat("%s/%.*s",Base,(int)E.Name.Size,E.Name.Data);Ok=Path&&MdoHomeRemove(Path,false);xrtFree(Path);}
    if(Next==XDIR_NEXT_ERROR)Ok=false;
    if(D&&!xrtDirClose(D))Ok=false;
    char* Stage=xrtFormat("%s/stage.json",Base);bool Present=false;xfileinfo Info;
    Ok=Ok&&Stage&&MdoExtStat(Stage,&Present,&Info)&&(!Present||MdoHomeRemove(Stage,false))&&MdoHomeRemoveEmptyDirectory(Base);xrtFree(Stage);return Ok;
}
static bool MdoExtGroupRecover(cstr Task,char Error[512]){
    if(!MdoExtensionDirectoryId(Task)||strlen(Task)!=32)return false;
    char *Base=xrtFormat(MDO_EXT_GROUPS "/%s",Task),*Path=Base?xrtFormat("%s/journal.json",Base):NULL;bool Exists=false;xfileinfo Info;bool Ok=Base&&Path&&MdoExtStat(Path,&Exists,&Info);
    if(Ok&&!Exists){
        char* Stage=xrtFormat("%s/stage.json",Base);xvalue* Intent=Stage?MdoExtReadJson(Stage,4096,false):NULL;uint64 Count=0,Protocol=0;
        if(Intent)Ok=MdoExtGroupNumber(Intent,"protocol",&Protocol)&&Protocol==1&&MdoExtGroupNumber(Intent,"count",&Count)&&Count<=MDO_EXT_PLAN_LIMIT&&MdoExtGroupCleanup(Base,(size_t)Count);
        else{bool Found=false;Ok=Stage&&MdoExtStat(Stage,&Found,&Info)&&!Found&&MdoHomeRemoveEmptyDirectory(Base);}
        xrtValueRelease(Intent);xrtFree(Stage);xrtFree(Base);xrtFree(Path);return Ok;
    }
    xvalue* J=Ok?MdoExtReadJson(Path,MDO_EXT_PLAN_BYTES,false):NULL;const xvalue* Items=xrtValueObjectGet(J,XRT_STR_LITERAL("items"));cstr Phase=MdoExtensionText(J,"phase",16);
    char *BeforePath=Base?xrtFormat("%s/before.json",Base):NULL,*AfterPath=Base?xrtFormat("%s/after.json",Base):NULL;
    xvalue* Before=BeforePath?MdoExtReadJson(BeforePath,MDO_EXT_STATE_LIMIT,false):NULL,*After=AfterPath?MdoExtReadJson(AfterPath,MDO_EXT_STATE_LIMIT,false):NULL,*Current=MdoExtReadJson(MDO_EXT_STATE,MDO_EXT_STATE_LIMIT,true);
    char Hash[65];cstr BeforeHash=MdoExtensionText(J,"before_hash",64),AfterHash=MdoExtensionText(J,"after_hash",64),SuspendedHash=MdoExtensionText(J,"suspended_hash",64);
    Ok=Ok&&J&&Phase&&(!strcmp(Phase,"prepared")||!strcmp(Phase,"committed"))&&xrtValueType(Items)==XVALUE_ARRAY&&xrtValueCount(Items)<=MDO_EXT_PLAN_LIMIT&&BeforeHash&&AfterHash&&SuspendedHash&&
        MdoExtValueHash(Before,Hash)&&!strcmp(Hash,BeforeHash)&&MdoExtValueHash(After,Hash)&&!strcmp(Hash,AfterHash)&&MdoExtValueHash(Current,Hash)&&(!strcmp(Hash,BeforeHash)||!strcmp(Hash,AfterHash)||!strcmp(Hash,SuspendedHash));
    bool Committed=Ok&&!strcmp(Phase,"committed");xvalue* Seen=xrtValueObject();Ok=Ok&&Seen;
    /* Validate every path and digest before removing any candidate directory. */
    for(size_t I=0;Ok&&I<xrtValueCount(Items);I++){
        const xvalue* Item=xrtValueArrayGet(Items,I);cstr Kind=MdoExtensionText(Item,"kind",32),Id=MdoExtensionText(Item,"directory_id",96),Op=MdoExtensionText(Item,"operation",16);bool Old=false;
        char* Key=MdoExtKey(Kind,Id);Ok=Key&&MdoExtensionDirectoryKind(Kind)&&MdoExtensionDirectoryId(Id)&&Op&&(!strcmp(Op,"install")||!strcmp(Op,"remove")||!strcmp(Op,"flags"))&&xrtValueGetBool(xrtValueObjectGet(Item,XRT_STR_LITERAL("old_exists")),&Old)&&!xrtValueObjectHas(Seen,xrtStrView(Key))&&xrtValueObjectSetNew(Seen,xrtStrView(Key),xrtValueBool(true));xrtFree(Key);
        char *Root=Ok?MdoExtensionDirectoryPath(Kind,Id):NULL,*Prior=xrtFormat("%s/%u/prior",Base,(unsigned)I),*Incoming=xrtFormat("%s/%u/incoming",Base,(unsigned)I);bool R=false,P=false,N=false;
        Ok=Ok&&Root&&Prior&&Incoming&&MdoExtStat(Root,&R,&Info)&&MdoExtStat(Prior,&P,&Info)&&MdoExtStat(Incoming,&N,&Info);
        cstr OldDigest=MdoExtensionText(Item,"old_digest",64),NewDigest=MdoExtensionText(Item,"new_digest",64);
        if(Ok&&P)Ok=Old&&MdoExtGroupDigest(Prior,OldDigest,Error);
        if(Ok&&Committed)Ok=!strcmp(Op,"remove")?!R:R&&MdoExtGroupDigest(Root,!strcmp(Op,"install")?NewDigest:OldDigest,Error);
        else if(Ok&&!strcmp(Op,"flags"))Ok=Old&&R&&!P;
        else if(Ok&&P)Ok=!R||(!strcmp(Op,"install")&&!N&&MdoExtGroupDigest(Root,NewDigest,Error));
        else if(Ok&&Old)Ok=R&&(!strcmp(Op,"remove")||N||MdoExtGroupDigest(Root,OldDigest,Error));
        else if(Ok)Ok=!P&&(!R||(!N&&!strcmp(Op,"install")&&MdoExtGroupDigest(Root,NewDigest,Error)));
        xrtFree(Root);xrtFree(Prior);xrtFree(Incoming);
    }
    for(size_t I=xrtValueCount(Items);Ok&&!Committed&&I>0;I--){
        const xvalue* Item=xrtValueArrayGet(Items,I-1);cstr Kind=MdoExtensionText(Item,"kind",32),Id=MdoExtensionText(Item,"directory_id",96),Op=MdoExtensionText(Item,"operation",16);if(!strcmp(Op,"flags"))continue;
        char *Root=MdoExtensionDirectoryPath(Kind,Id),*Prior=xrtFormat("%s/%u/prior",Base,(unsigned)(I-1));bool R=false,P=false;Ok=Root&&Prior&&MdoExtStat(Root,&R,&Info)&&MdoExtStat(Prior,&P,&Info);
        if(Ok&&P){if(R)Ok=MdoExtRemoveTree(Root);if(Ok)Ok=MdoHomeRenameNoReplace(Prior,Root);}
        else if(Ok&&!MdoGraphBool(Item,"old_exists",false)&&R)Ok=MdoExtRemoveTree(Root);
        xrtFree(Root);xrtFree(Prior);
    }
    if(Ok)Ok=MdoExtSaveJson(MDO_EXT_STATE,Committed?After:Before,MDO_EXT_STATE_LIMIT);
    if(Ok&&Committed){cstr Plan=MdoExtensionText(J,"plan_id",32);const xvalue* Receipt=xrtValueObjectGet(J,XRT_STR_LITERAL("receipt"));Ok=Plan&&strlen(Plan)==32&&MdoExtensionDirectoryId(Plan)&&Receipt&&MdoExtGroupSaveReceipt(Plan,Receipt);}
    if(Ok){char* Done=xrtFormat("%s/%s.json",Base,Committed?"committed":"rolledback");Ok=Done&&MdoHomeRenameNoReplace(Path,Done);xrtFree(Done);}
    if(Ok)Ok=MdoExtGroupCleanup(Base,xrtValueCount(Items));
    if(!Ok&&Error)snprintf(Error,512,"Extension group recovery could not prove directory/state ownership; its journal and files were preserved at %.240s",Base?Base:MDO_EXT_GROUPS);
    xrtValueRelease(Seen);xrtValueRelease(J);xrtValueRelease(Before);xrtValueRelease(After);xrtValueRelease(Current);xrtFree(Base);xrtFree(Path);xrtFree(BeforePath);xrtFree(AfterPath);return Ok;
}
static bool MdoExtGroupsRecover(char Error[512]){
    xdir D=MdoHomeOpenDirectory(MDO_EXT_GROUPS,XDIR_STAT);if(!D)return MdoExtMissing();xvalue* Names=xrtValueArray();bool Ok=Names!=NULL;xdirentry E;xdirnext Next=XDIR_NEXT_END;
    while(Ok&&(Next=xrtDirNext(D,&E))==XDIR_NEXT_ITEM){if(E.Info.Type!=XFILE_TYPE_DIRECTORY||!(E.Flags&XDIR_ENTRY_UTF8)||E.Name.Size!=32){Ok=false;break;}Ok=xrtValueCount(Names)<128&&xrtValueArrayAppendNew(Names,xrtValueString(xrtStrViewN(E.Name.Data,E.Name.Size)));}
    if(Next==XDIR_NEXT_ERROR)Ok=false;
    if(!xrtDirClose(D))Ok=false;
    for(size_t I=0;Ok&&I<xrtValueCount(Names);I++){xstrview Name;xrtValueGetString(xrtValueArrayGet(Names,I),&Name);Ok=MdoExtGroupRecover(Name.Data,Error);}xrtValueRelease(Names);return Ok;
}
static bool MdoExtManualMigrationsReady(char Error[512]){
    xdir D=MdoHomeOpenDirectory("data/extension-migrations",XDIR_STAT);if(!D)return MdoExtMissing();bool Ok=true;xdirentry E;xdirnext Next=XDIR_NEXT_END;size_t Count=0;
    while(Ok&&(Next=xrtDirNext(D,&E))==XDIR_NEXT_ITEM){
        if(E.Info.Type!=XFILE_TYPE_DIRECTORY||!(E.Flags&XDIR_ENTRY_UTF8)||E.Name.Size!=32||++Count>128){Ok=false;break;}
        char* Path=xrtFormat("data/extension-migrations/%.*s/journal.json",(int)E.Name.Size,E.Name.Data);bool Present=false;xfileinfo Info;Ok=Path&&MdoExtStat(Path,&Present,&Info);xvalue* Journal=Ok&&Present?MdoExtReadJson(Path,MDO_EXT_STATE_LIMIT,false):NULL;cstr Phase=MdoExtensionText(Journal,"phase",16);
        if(Present)Ok=Ok&&Phase&&(!strcmp(Phase,"committed")||!strcmp(Phase,"rolledback"));
        xrtValueRelease(Journal);xrtFree(Path);
    }
    if(Next==XDIR_NEXT_ERROR)Ok=false;
    if(!xrtDirClose(D))Ok=false;
    if(!Ok&&Error)snprintf(Error,512,"An offline extension migration is incomplete. Close mdo, run tools/migrate_extension_dependencies.py --home <Home> --recover with its explicit workspace roots, then restart. Backups remain in data/extension-migrations");
    return Ok;
}
static bool MdoExtPlanFlag(xvalue* State,const xvalue* Row,bool Enabled){
    cstr Kind=MdoExtensionText(Row,"kind",32),Directory=MdoExtensionText(Row,"directory_id",96),Ref=MdoExtensionText(Row,"id",MDO_EXTENSION_REF_BYTES);char* Key=MdoExtKey(Kind,Directory);const xvalue* Old=Key?xrtValueObjectGet(State,xrtStrView(Key)):NULL;xvalue* R=Old?xrtValueClone(Old):xrtValueObject();
    bool Ok=R&&Key&&MdoExtPut(R,"kind",Kind)&&MdoExtPut(R,"id",Directory)&&MdoExtPut(R,"logical_id",Ref)&&(!Old?MdoExtPut(R,"source","manual"):true)&&xrtValueObjectSetNew(R,XRT_STR_LITERAL("enabled"),xrtValueBool(Enabled))&&xrtValueObjectSet(State,xrtStrView(Key),R);xrtFree(Key);xrtValueRelease(R);return Ok;
}
/* A legacy store directory can acquire its first logical ID only through a
 * verified replacement of the same work, with its approved files intact. */
static xvalue* MdoExtLegacyStore(cstr Kind,cstr Directory,cstr Ref,const xvalue* State,char Error[512]){
    char* Key=MdoExtKey(Kind,Directory);const xvalue* Record=Key?xrtValueObjectGet(State,xrtStrView(Key)):NULL;cstr Source=MdoExtensionText(Record,"source",16),Work=MdoExtensionText(Record,"xid",32),Digest=MdoExtensionText(Record,"directory_digest",64);
    char *Root=MdoExtensionDirectoryPath(Kind,Directory),*Path=Root?xrtFormat("%s/extension.json",Root):NULL;xvalue* Meta=Path?MdoExtReadJson(Path,MDO_EXTENSION_MANIFEST_BYTES,false):NULL;
    bool Ok=Source&&!strcmp(Source,"store")&&Work&&!strcmp(Work,Directory)&&!MdoExtensionText(Record,"logical_id",MDO_EXTENSION_REF_BYTES)&&Meta&&!xrtValueObjectHas(Meta,XRT_STR_LITERAL("id"))&&MdoExtCanonical(Kind,Directory)&&MdoExtGroupDigest(Root,Digest,Error)&&MdoExtPut(Meta,"id",Ref)&&MdoExtensionManifest(Meta,Error,512)&&!strcmp(Kind,MdoExtensionText(Meta,"kind",32))&&!MdoGraphBool(Record,"pending_remove",false);
    Ok=Ok&&MdoExtPut(Meta,"directory_id",Directory)&&MdoExtPut(Meta,"source","store")&&xrtValueObjectSet(Meta,XRT_STR_LITERAL("installation"),Record)&&xrtValueObjectSetNew(Meta,XRT_STR_LITERAL("desired_enabled"),xrtValueBool(MdoGraphBool(Record,"enabled",true)));
    xrtFree(Key);xrtFree(Root);xrtFree(Path);if(!Ok){xrtValueRelease(Meta);return NULL;}return Meta;
}
static bool MdoExtPlanAffected(MdoExtensionGraph* G,bool* Selected){
    for(size_t Pass=0;Pass<G->Count;Pass++){bool Changed=false;for(size_t I=0;I<G->Count;I++){
        if(Selected[I])continue;
        const xvalue* Deps=xrtValueObjectGet(G->Nodes[I].Row,XRT_STR_LITERAL("dependencies"));
        for(size_t J=0;J<xrtValueCount(Deps);J++){const xvalue* D=xrtValueArrayGet(Deps,J);if(MdoGraphBool(D,"optional",false))continue;size_t At=MdoGraphFind(G,MdoExtensionText(D,"id",MDO_EXTENSION_REF_BYTES));if(At!=SIZE_MAX&&Selected[At]){Selected[I]=true;Changed=true;break;}}
    }if(!Changed)return true;}return true;
}
/* Packages is verified store metadata, supplied only by the first-party
 * service adapter. It is never accepted directly from an HTTP request. */
bool MdoExtensionsPlan(const xvalue* Input,const xvalue* Packages,xvalue** Result,char Error[512]){
    *Result=NULL;if(Error)Error[0]=0;cstr Action=MdoExtensionText(Input,"action",16),Ref=MdoExtensionText(Input,"ref",MDO_EXTENSION_REF_BYTES);
    bool Install=Action&&!strcmp(Action,"install"),Enable=Action&&!strcmp(Action,"enable"),Disable=Action&&!strcmp(Action,"disable"),Remove=Action&&!strcmp(Action,"remove"),Cascade=MdoGraphBool(Input,"cascade",false);
    if(!MdoExtensionRefValid(Ref)||(!Install&&!Enable&&!Disable&&!Remove)||(Install&&xrtValueType(Packages)!=XVALUE_ARRAY))return MdoExtError(Error,"Choose an extension ID and dependency plan action");
    xrtMutexLock(MdoExtLock);xvalue *State=MdoExtReadJson(MDO_EXT_STATE,MDO_EXT_STATE_LIMIT,true),*Rows=State?MdoExtSnapshotLocked(State,Error):NULL,*Plan=xrtValueObject(),*Items=xrtValueArray(),*Affected=xrtValueArray(),*Issues=xrtValueArray();MdoExtensionGraph G={0};bool Ok=!MdoExtGroupActive&&MdoExtPlansPrune(Error)&&Rows&&Plan&&Items&&Affected&&Issues&&MdoGraphInit(&G,Rows,Error);
    size_t Root=Ok?MdoGraphFind(&G,Ref):SIZE_MAX;bool* Selected=Ok?xrtCalloc(G.Count?G.Count:1,sizeof(bool)):NULL;Ok=Ok&&Selected&&(Install||Root!=SIZE_MAX);char StateHash[65];if(Ok)Ok=MdoExtValueHash(State,StateHash);
    if(Ok&&Install){
        Ok=xrtValueCount(Packages)&&xrtValueCount(Packages)<=MDO_EXT_PLAN_LIMIT;
        bool RootWanted=Root!=SIZE_MAX?G.Nodes[Root].Wanted:true,RootFound=false;
        for(size_t I=0;Ok&&I<xrtValueCount(Packages);I++){const xvalue* P=xrtValueArrayGet(Packages,I);cstr Id=MdoExtensionText(P,"id",MDO_EXTENSION_REF_BYTES);if(Id&&!strcmp(Id,Ref)){
            if(RootFound){Ok=false;break;}RootFound=true;cstr K=MdoExtensionText(P,"kind",32),D=MdoExtensionText(P,"xid",32);if(Root==SIZE_MAX){xvalue* Legacy=K&&D?MdoExtLegacyStore(K,D,Ref,State,NULL):NULL;RootWanted=Legacy?MdoGraphBool(Legacy,"desired_enabled",false):K&&strcmp(K,"plugins")!=0;xrtValueRelease(Legacy);}
        }}Ok=Ok&&RootFound;
        for(size_t I=0;Ok&&I<xrtValueCount(Packages);I++){
            const xvalue* Package=xrtValueArrayGet(Packages,I),*Manifest=xrtValueObjectGet(Package,XRT_STR_LITERAL("manifest"));cstr Id=MdoExtensionText(Package,"id",MDO_EXTENSION_REF_BYTES),Kind=MdoExtensionText(Package,"kind",32),Xid=MdoExtensionText(Package,"xid",32),Hash=MdoExtensionText(Package,"sha256",64);size_t At=MdoGraphFind(&G,Id);
            Ok=Manifest&&MdoExtensionManifest(Manifest,Error,512)&&Id&&Kind&&Xid&&strlen(Xid)==32&&MdoExtensionDirectoryId(Xid)&&Hash&&strlen(Hash)==64&&!strcmp(Id,MdoExtensionText(Manifest,"id",MDO_EXTENSION_REF_BYTES))&&!strcmp(Kind,MdoExtensionText(Manifest,"kind",32));
            xvalue* Legacy=Ok&&At==SIZE_MAX?MdoExtLegacyStore(Kind,Xid,Id,State,Error):NULL;if(!Legacy&&Error)Error[0]=0;const xvalue* Old=At!=SIZE_MAX?G.Nodes[At].Row:Legacy;const xvalue* Record=xrtValueObjectGet(Old,XRT_STR_LITERAL("installation"));cstr Source=MdoExtensionText(Old,"source",16),OwnerWork=MdoExtensionText(Record,"xid",32),PriorHash=MdoExtensionText(Record,"sha256",64);
            if(Ok&&At==SIZE_MAX&&!Legacy)for(size_t J=0;J<G.Count;J++)if(!strcmp(Kind,MdoExtensionText(G.Nodes[J].Row,"kind",32))&&!strcmp(Xid,MdoExtensionText(G.Nodes[J].Row,"directory_id",96))){Ok=false;break;}
            if(Ok&&Old)Ok=Source&&!strcmp(Source,"store")&&OwnerWork&&!strcmp(OwnerWork,Xid)&&(At==SIZE_MAX||!G.Nodes[At].Duplicate);
            if(!Ok){MdoExtError(Error,"A required logical ID is owned by another local installation or the store plan is invalid");xrtValueRelease(Legacy);break;}
            xvalue* Item=xrtValueClone(Package);bool Same=Old&&!Legacy&&PriorHash&&!strcmp(PriorHash,Hash)&&MdoExtVerify(Kind,MdoExtensionText(Old,"directory_id",96),State,NULL,Error);
            bool Wanted=!strcmp(Id,Ref)?RootWanted:RootWanted||MdoGraphBool(Old,"desired_enabled",false);
            Ok=Item&&xrtValueObjectSetNew(Item,XRT_STR_LITERAL("legacy_replacement"),xrtValueBool(Legacy!=NULL))&&MdoExtPut(Item,"directory_id",Old?MdoExtensionText(Old,"directory_id",96):Xid)&&MdoExtPut(Item,"operation",Same?"flags":"install")&&xrtValueObjectSetNew(Item,XRT_STR_LITERAL("desired_enabled"),xrtValueBool(Wanted))&&xrtValueArrayAppend(Items,Item);xrtValueRelease(Item);if(Old&&At!=SIZE_MAX)Selected[At]=true;xrtValueRelease(Legacy);
        }
    }else if(Ok&&Enable){
        xvalue* Closure=MdoGraphClosure(&G,Ref,Error);Ok=Closure!=NULL;
        for(size_t I=0;Ok&&I<xrtValueCount(Closure);I++){size_t At=MdoGraphFind(&G,MdoExtensionText(xrtValueArrayGet(Closure,I),"id",MDO_EXTENSION_REF_BYTES));Ok=At!=SIZE_MAX;if(Ok)Selected[At]=true;}xrtValueRelease(Closure);
    }else if(Ok){Selected[Root]=true;MdoExtPlanAffected(&G,Selected);if(!Cascade)for(size_t I=0;Ok&&I<G.Count;I++)if(I!=Root&&Selected[I]&&G.Nodes[I].Wanted)Ok=MdoExtError(Error,"Other enabled extensions require this provider; inspect a cascade plan to disable its users together");}
    if(Ok&&Install)MdoExtPlanAffected(&G,Selected);
    for(size_t I=0;Ok&&I<G.Count;I++)if(Selected[I]){
        xvalue* Item=xrtValueClone(G.Nodes[I].Row);
        if(!Install){Ok=Item&&MdoExtPut(Item,"operation",Remove&&I==Root?"remove":"flags")&&xrtValueObjectSetNew(Item,XRT_STR_LITERAL("desired_enabled"),xrtValueBool(Enable));if(Ok)Ok=xrtValueArrayAppend(Items,Item);}
        if(Ok)Ok=Item&&xrtValueArrayAppend(Affected,Item);
        xrtValueRelease(Item);
    }
    /* Preserve and revalidate affected users during an update. */
    if(Ok&&Install)for(size_t I=0;Ok&&I<xrtValueCount(Affected);I++){
        const xvalue* A=xrtValueArrayGet(Affected,I);bool Exists=false;for(size_t J=0;J<xrtValueCount(Items);J++)if(!strcmp(MdoExtensionText(A,"id",MDO_EXTENSION_REF_BYTES),MdoExtensionText(xrtValueArrayGet(Items,J),"id",MDO_EXTENSION_REF_BYTES)))Exists=true;
        if(!Exists){xvalue* C=xrtValueClone(A);Ok=C&&MdoExtPut(C,"operation","flags")&&xrtValueArrayAppend(Items,C);xrtValueRelease(C);}
    }
    if(Ok)Ok=xrtValueCount(Items)&&xrtValueCount(Items)<=MDO_EXT_PLAN_LIMIT;
    xvalue* Unique=Ok?xrtValueObject():NULL;Ok=Ok&&Unique;
    for(size_t I=0;Ok&&I<xrtValueCount(Items);I++){
        xvalue* Item=xrtValueArrayGet(Items,I);cstr Kind=MdoExtensionText(Item,"kind",32),Directory=MdoExtensionText(Item,"directory_id",96);char* Path=MdoExtensionDirectoryPath(Kind,Directory);bool Present=false;xfileinfo Info;char Digest[65]="";MdoExtTree Tree={0};
        cstr Identity=MdoExtensionText(Item,"id",MDO_EXTENSION_REF_BYTES);char* Location=xrtFormat("physical/%s/%s",Kind,Directory);Ok=Identity&&Location&&!xrtValueObjectHas(Unique,xrtStrView(Identity))&&!xrtValueObjectHas(Unique,xrtStrView(Location))&&xrtValueObjectSetNew(Unique,xrtStrView(Identity),xrtValueBool(true))&&xrtValueObjectSetNew(Unique,xrtStrView(Location),xrtValueBool(true));xrtFree(Location);
        Ok=Ok&&Path&&MdoExtStat(Path,&Present,&Info)&&(!Present||(Info.Type==XFILE_TYPE_DIRECTORY&&MdoExtCollect(Path,&Tree,NULL,Error)&&MdoExtFingerprint(Path,&Tree,Digest,NULL)))&&MdoExtPut(Item,"expected_revision",Digest)&&xrtValueObjectSetNew(Item,XRT_STR_LITERAL("old_exists"),xrtValueBool(Present));MdoExtTreeUnit(&Tree);xrtFree(Path);
    }
    /* Candidate rows use actual installed metadata for reuse and pinned store
     * manifests for replacements. Unrelated invalid drafts remain editable. */
    xvalue* Candidate=Ok?xrtValueArray():NULL;Ok=Ok&&Candidate;
    for(size_t I=0;Ok&&I<G.Count;I++){cstr Id=MdoExtensionText(G.Nodes[I].Row,"id",MDO_EXTENSION_REF_BYTES);bool Changed=false;for(size_t J=0;J<xrtValueCount(Items);J++)if((Id&&!strcmp(Id,MdoExtensionText(xrtValueArrayGet(Items,J),"id",MDO_EXTENSION_REF_BYTES)))||(!strcmp(MdoExtensionText(G.Nodes[I].Row,"kind",32),MdoExtensionText(xrtValueArrayGet(Items,J),"kind",32))&&!strcmp(MdoExtensionText(G.Nodes[I].Row,"directory_id",96),MdoExtensionText(xrtValueArrayGet(Items,J),"directory_id",96))))Changed=true;if(!Changed)Ok=xrtValueArrayAppend(Candidate,G.Nodes[I].Row);}
    for(size_t I=0;Ok&&I<xrtValueCount(Items);I++){
        const xvalue* Item=xrtValueArrayGet(Items,I);cstr Op=MdoExtensionText(Item,"operation",16);if(!strcmp(Op,"remove"))continue;
        size_t At=MdoGraphFind(&G,MdoExtensionText(Item,"id",MDO_EXTENSION_REF_BYTES));const xvalue* Meta=!strcmp(Op,"install")?xrtValueObjectGet(Item,XRT_STR_LITERAL("manifest")):(At!=SIZE_MAX?G.Nodes[At].Row:NULL);xvalue* Row=Meta?xrtValueClone(Meta):NULL;bool Wanted=MdoGraphBool(Item,"desired_enabled",false);
        Ok=Row&&MdoExtPut(Row,"directory_id",MdoExtensionText(Item,"directory_id",96))&&MdoExtPut(Row,"source",Install&&xrtValueObjectHas(Item,XRT_STR_LITERAL("manifest"))?"store":MdoExtensionText(Item,"source",16))&&xrtValueObjectSetNew(Row,XRT_STR_LITERAL("desired_enabled"),xrtValueBool(Wanted))&&xrtValueObjectSetNew(Row,XRT_STR_LITERAL("platform_supported"),xrtValueBool(MdoExtensionSupportsPlatform(Meta,MdoExtensionRuntimePlatform())))&&xrtValueArrayAppend(Candidate,Row);xrtValueRelease(Row);
    }
    MdoExtensionGraph Next={0};bool HasNext=Ok&&MdoGraphInit(&Next,Candidate,Error);Ok=HasNext&&MdoGraphEvaluate(&Next,Error);
    for(size_t I=0;Ok&&I<Next.Count;I++){
        cstr Id=MdoExtensionText(Next.Nodes[I].Row,"id",MDO_EXTENSION_REF_BYTES);size_t Old=MdoGraphFind(&G,Id);bool Changed=false;for(size_t J=0;J<xrtValueCount(Items);J++)if(Id&&!strcmp(Id,MdoExtensionText(xrtValueArrayGet(Items,J),"id",MDO_EXTENSION_REF_BYTES)))Changed=true;
        if(Next.Nodes[I].Wanted&&!Next.Nodes[I].Ready&&(Changed||(Old!=SIZE_MAX&&MdoGraphBool(G.Nodes[Old].Row,"effective_enabled",false))))for(size_t J=0;Ok&&J<xrtValueCount(Next.Nodes[I].Issues);J++)if(!MdoGraphBool(xrtValueArrayGet(Next.Nodes[I].Issues,J),"optional",false))Ok=xrtValueArrayAppend(Issues,xrtValueArrayGet(Next.Nodes[I].Issues,J));
    }
    char* Task=Ok?xrtXidMakeString():NULL;char* Path=Task?xrtFormat(MDO_EXT_PLANS "/%s.json",Task):NULL;char Revision[65];
    Ok=Ok&&Task&&Path&&MdoExtPut(Plan,"id",Task)&&MdoExtPut(Plan,"action",Action)&&MdoExtPut(Plan,"root",Ref)&&MdoExtPut(Plan,"state_hash",StateHash)&&xrtValueObjectSetNew(Plan,XRT_STR_LITERAL("created"),xrtValueUInt((uint64)(MdoUtcUs()/1000000)))&&xrtValueObjectSet(Plan,XRT_STR_LITERAL("items"),Items)&&xrtValueObjectSet(Plan,XRT_STR_LITERAL("affected"),Affected)&&xrtValueObjectSet(Plan,XRT_STR_LITERAL("issues"),Issues)&&xrtValueObjectSetNew(Plan,XRT_STR_LITERAL("ready"),xrtValueBool(xrtValueCount(Issues)==0))&&MdoExtValueHash(Plan,Revision)&&MdoExtPut(Plan,"revision",Revision)&&MdoExtEnsure(MDO_EXT_PLANS)&&MdoExtSaveJson(Path,Plan,MDO_EXT_PLAN_BYTES);
    if(Ok)*Result=xrtValueClone(Plan);
    if(!Ok&&Error&&!Error[0])MdoExtError(Error,"Cannot build the complete dependency operation plan within its 128-item resource limit");
    if(HasNext)MdoGraphUnit(&Next);
    MdoGraphUnit(&G);xrtFree(Selected);xrtFree(Task);xrtFree(Path);xrtValueRelease(Unique);xrtValueRelease(Candidate);xrtValueRelease(State);xrtValueRelease(Rows);xrtValueRelease(Plan);xrtValueRelease(Items);xrtValueRelease(Affected);xrtValueRelease(Issues);xrtMutexUnlock(MdoExtLock);return Ok;
}
xvalue* MdoExtensionsPlanRead(cstr Id,cstr Expected,char Error[512]){
    if(!MdoExtensionDirectoryId(Id)||strlen(Id)!=32||!Expected||strlen(Expected)!=64){MdoExtError(Error,"Choose an inspected dependency plan and its revision");return NULL;}
    char* Path=xrtFormat(MDO_EXT_PLANS "/%s.json",Id);xvalue* Plan=Path?MdoExtReadJson(Path,MDO_EXT_PLAN_BYTES,false):NULL,*Body=Plan?xrtValueClone(Plan):NULL;char Hash[65];xrtFree(Path);
    if(Body)xrtValueObjectRemove(Body,XRT_STR_LITERAL("revision"));
    bool Ok=Body&&MdoExtPlanLive(Plan)&&MdoExtValueHash(Body,Hash)&&!strcmp(Hash,Expected)&&MdoExtensionText(Plan,"id",32)&&!strcmp(Id,MdoExtensionText(Plan,"id",32));xrtValueRelease(Body);
    if(!Ok){xrtValueRelease(Plan);MdoExtError(Error,"The dependency plan changed or is unavailable; inspect a new plan");return NULL;}return Plan;
}
static xvalue* MdoExtGroupDownload(const xvalue* Downloads,cstr Ref){
    for(size_t I=0;I<xrtValueCount(Downloads);I++){xvalue* D=xrtValueArrayGet(Downloads,I);cstr Id=MdoExtensionText(D,"id",MDO_EXTENSION_REF_BYTES);if(Id&&!strcmp(Id,Ref))return D;}return NULL;
}
static xvalue* MdoExtGroupReceipt(cstr Plan){char* Path=xrtFormat(MDO_EXT_PLANS "/%s.receipt.json",Plan);xvalue* R=Path?MdoExtReadJson(Path,MDO_EXT_PLAN_BYTES,false):NULL;xrtFree(Path);return R;}
xvalue* MdoExtensionsPlanReceipt(cstr Id,cstr Revision,char Error[512]){
    if(Error)Error[0]=0;
    if(!MdoExtensionDirectoryId(Id)||strlen(Id)!=32||!Revision||strlen(Revision)!=64)return NULL;
    xrtMutexLock(MdoExtLock);xvalue* Receipt=MdoExtGroupReceipt(Id);xrtMutexUnlock(MdoExtLock);
    if(Receipt){cstr Seen=MdoExtensionText(Receipt,"revision",64);if(!Seen||strcmp(Seen,Revision)){xrtValueRelease(Receipt);if(Error)snprintf(Error,512,"Dependency plan receipt revision mismatch");return NULL;}}else xrtClearError();return Receipt;
}
static bool MdoExtGroupSaveReceipt(cstr Plan,const xvalue* Receipt){char* Path=xrtFormat(MDO_EXT_PLANS "/%s.receipt.json",Plan);bool Ok=Path&&MdoExtSaveJson(Path,Receipt,MDO_EXT_PLAN_BYTES);xrtFree(Path);return Ok;}
bool MdoExtensionsPlanReceiptRefresh(cstr Id,cstr Revision,const xvalue* Receipt,char Error[512]){
    if(!MdoExtLock||!Id||strlen(Id)!=32||!MdoExtensionDirectoryId(Id)||!Revision)return MdoExtError(Error,"Choose a committed dependency plan receipt");
    xrtMutexLock(MdoExtLock);xvalue* Before=MdoExtGroupReceipt(Id);cstr Seen=MdoExtensionText(Before,"revision",64),After=MdoExtensionText(Receipt,"revision",64),Task=MdoExtensionText(Before,"id",32),Next=MdoExtensionText(Receipt,"id",32),Plan=MdoExtensionText(Receipt,"plan_id",32);
    bool Ok=Seen&&After&&Task&&Next&&Plan&&!strcmp(Seen,Revision)&&!strcmp(After,Revision)&&!strcmp(Task,Next)&&!strcmp(Plan,Id)&&MdoExtGroupSaveReceipt(Id,Receipt);xrtValueRelease(Before);xrtMutexUnlock(MdoExtLock);return Ok||MdoExtError(Error,"The committed receipt could not retain its final runtime status");
}
bool MdoExtensionsPlanDiscard(cstr Id,cstr Revision,char Error[512]){
    xvalue* Plan=MdoExtensionsPlanRead(Id,Revision,Error);if(!Plan)return false;xrtValueRelease(Plan);
    xrtMutexLock(MdoExtLock);xvalue* Receipt=MdoExtGroupReceipt(Id);bool Ok=!MdoExtGroupActive&&!Receipt;char* Path=xrtFormat(MDO_EXT_PLANS "/%s.json",Id);
    Ok=Ok&&Path&&MdoHomeRemove(Path,false);xrtFree(Path);xrtValueRelease(Receipt);xrtMutexUnlock(MdoExtLock);return Ok||MdoExtError(Error,"An applying or completed plan retains its receipt for safe retries");
}
bool MdoExtensionsApplyGroup(const xvalue* Plan,const xvalue* Downloads,bool Trust,xcancel* Cancel,MdoExtensionReload Reload,void* Data,xvalue** Result,char Error[512]){
    *Result=NULL;if(Error)Error[0]=0;cstr PlanId=MdoExtensionText(Plan,"id",32),Revision=MdoExtensionText(Plan,"revision",64);const xvalue* PlanItems=xrtValueObjectGet(Plan,XRT_STR_LITERAL("items"));
    if(!PlanId||strlen(PlanId)!=32||!MdoExtensionDirectoryId(PlanId)||!Revision||!MdoExtPlanLive(Plan)||!MdoGraphBool(Plan,"ready",false)||!xrtValueCount(PlanItems)||xrtValueCount(PlanItems)>MDO_EXT_PLAN_LIMIT)return MdoExtError(Error,"Inspect a complete, unblocked dependency plan before applying it");
    xrtMutexLock(MdoExtLock);xvalue* Receipt=MdoExtGroupReceipt(PlanId);if(Receipt){bool Same=MdoExtensionText(Receipt,"revision",64)&&!strcmp(Revision,MdoExtensionText(Receipt,"revision",64));if(Same)*Result=Receipt;else xrtValueRelease(Receipt);xrtMutexUnlock(MdoExtLock);return Same||MdoExtError(Error,"The plan receipt belongs to another revision");}xrtClearError();
    xvalue *Before=MdoExtReadJson(MDO_EXT_STATE,MDO_EXT_STATE_LIMIT,true),*After=Before?xrtValueClone(Before):NULL,*Suspended=Before?xrtValueClone(Before):NULL,*Items=xrtValueClone(PlanItems),*J=xrtValueObject(),*Out=xrtValueObject();char Hash[65];
    cstr Expected=MdoExtensionText(Plan,"state_hash",64);bool Ok=!MdoExtGroupActive&&!MdoExtRecoveryAll&&Before&&After&&Suspended&&Items&&J&&Out&&Expected&&MdoExtValueHash(Before,Hash)&&!strcmp(Hash,Expected)&&(!Cancel||!xrtCancelRequested(Cancel));
    char* Task=Ok?xrtXidMakeString():NULL;char *Base=Task?xrtFormat(MDO_EXT_GROUPS "/%s",Task):NULL,*Journal=Base?xrtFormat("%s/journal.json",Base):NULL,*BeforePath=Base?xrtFormat("%s/before.json",Base):NULL,*AfterPath=Base?xrtFormat("%s/after.json",Base):NULL,*Stage=Base?xrtFormat("%s/stage.json",Base):NULL;
    xvalue* Intent=xrtValueObject();Ok=Ok&&Task&&Base&&Journal&&BeforePath&&AfterPath&&Stage&&Intent&&xrtValueObjectSetNew(Intent,XRT_STR_LITERAL("protocol"),xrtValueUInt(1))&&xrtValueObjectSetNew(Intent,XRT_STR_LITERAL("count"),xrtValueUInt(xrtValueCount(Items)))&&MdoExtEnsure(Base)&&MdoExtSaveJson(Stage,Intent,4096);
    for(size_t I=0;Ok&&I<xrtValueCount(Items);I++){
        xvalue* Item=xrtValueArrayGet(Items,I);cstr Ref=MdoExtensionText(Item,"id",MDO_EXTENSION_REF_BYTES),Kind=MdoExtensionText(Item,"kind",32),Directory=MdoExtensionText(Item,"directory_id",96),Op=MdoExtensionText(Item,"operation",16),OldDigest=MdoExtensionText(Item,"expected_revision",64);bool Old=MdoGraphBool(Item,"old_exists",false),Wanted=MdoGraphBool(Item,"desired_enabled",false);
        Ok=MdoExtensionRefValid(Ref)&&MdoExtensionDirectoryKind(Kind)&&MdoExtensionDirectoryId(Directory)&&Op&&(!strcmp(Op,"install")||!strcmp(Op,"remove")||!strcmp(Op,"flags"))&&!MdoExtRecoveryBlockedLocked(Kind,Directory)&&OldDigest;
        char *Root=Ok?MdoExtensionDirectoryPath(Kind,Directory):NULL,*Part=Base?xrtFormat("%s/%u",Base,(unsigned)I):NULL,*Incoming=Part?xrtFormat("%s/incoming",Part):NULL,*Key=Ok?MdoExtKey(Kind,Directory):NULL;bool Exists=false;xfileinfo Info;
        Ok=Ok&&Root&&Part&&Incoming&&Key&&MdoExtStat(Root,&Exists,&Info)&&Exists==Old&&(!Old||MdoExtGroupDigest(Root,OldDigest,Error))&&MdoExtPut(Item,"old_digest",OldDigest);
        xvalue* Existing=Ok&&Old?(MdoGraphBool(Item,"legacy_replacement",false)?MdoExtLegacyStore(Kind,Directory,Ref,Before,Error):MdoExtRead(Kind,Directory,Before,Error)):NULL;
        if(Ok&&Old)Ok=Existing&&MdoExtensionText(Existing,"id",MDO_EXTENSION_REF_BYTES)&&!strcmp(Ref,MdoExtensionText(Existing,"id",MDO_EXTENSION_REF_BYTES))&&MdoExtPlanFlag(Suspended,Existing,false);
        if(Ok&&!strcmp(Op,"install")){
#if defined(__ANDROID__)
            if(!strcmp(Kind,"external-tools"))Ok=MdoExtError(Error,"Android external programs require the full APK");
#endif
            const xvalue* D=MdoExtGroupDownload(Downloads,Ref),*Credits=xrtValueObjectGet(D,XRT_STR_LITERAL("credits"));cstr Archive=MdoExtensionText(D,"archive",4096),Pinned=MdoExtensionText(Item,"sha256",64),Version=MdoExtensionText(Item,"version_id",32),Work=MdoExtensionText(Item,"xid",32);
            Ok=Ok&&Archive&&MdoExtensionRelativePath(Archive)&&Credits&&Pinned&&MdoExtensionText(Credits,"source",16)&&!strcmp(MdoExtensionText(Credits,"source",16),"store")&&Version&&Work&&MdoExtensionText(Credits,"version_id",32)&&!strcmp(Version,MdoExtensionText(Credits,"version_id",32))&&MdoExtensionText(Credits,"xid",32)&&!strcmp(Work,MdoExtensionText(Credits,"xid",32));
            xfile Input=Ok?MdoHomeOpenRead(Archive):NULL;MdoZip Z={0};xvalue* Meta=Input&&MdoZipOpen(&Z,Input)?MdoExtensionZipManifest(&Z,Cancel):NULL;char Actual[65],MetadataHash[65],PinnedMetadataHash[65],DirectoryHash[65];uint64 Bytes=0;
            Ok=Ok&&Meta&&!strcmp(Kind,MdoExtensionText(Meta,"kind",32))&&!strcmp(Ref,MdoExtensionText(Meta,"id",MDO_EXTENSION_REF_BYTES))&&MdoExtIdentityLocked(Kind,Directory,Meta,Credits,Error)&&MdoExtensionSupportsPlatform(Meta,MdoExtensionRuntimePlatform())&&(!MdoExtRequiresTrust(Meta)||Trust)&&
                MdoExtValueHash(Meta,MetadataHash)&&MdoExtValueHash(xrtValueObjectGet(Item,XRT_STR_LITERAL("manifest")),PinnedMetadataHash)&&!strcmp(MetadataHash,PinnedMetadataHash)&&MdoExtFileDigest(Input,Cancel,Actual,&Bytes)&&!strcmp(Actual,Pinned)&&MdoExtEnsure(Part)&&MdoExtExtractZip(&Z,Incoming,Cancel,Error)&&MdoExtDeclaredFiles(Incoming,Meta,Error);
            MdoExtTree Tree={0};if(Ok)Ok=MdoExtCollect(Incoming,&Tree,Cancel,Error)&&MdoExtFingerprint(Incoming,&Tree,DirectoryHash,Cancel);MdoExtTreeUnit(&Tree);
            char Recheck[65];uint64 Current=0;if(Ok)Ok=MdoExtFileDigest(Input,Cancel,Recheck,&Current)&&Current==Bytes&&!strcmp(Recheck,Actual);
            xvalue* Record=xrtValueObject();Ok=Ok&&Record&&MdoExtPut(Record,"source","store")&&MdoExtPut(Record,"kind",Kind)&&MdoExtPut(Record,"id",Directory)&&MdoExtPut(Record,"logical_id",Ref)&&MdoExtPut(Record,"sha256",Actual)&&MdoExtPut(Record,"directory_digest",DirectoryHash)&&xrtValueObjectSetNew(Record,XRT_STR_LITERAL("enabled"),xrtValueBool(Wanted));
            const cstr Fields[]={"owner","submitter","author","copyright","license","xid","version_id","version"};for(size_t F=0;Ok&&F<sizeof(Fields)/sizeof(*Fields);F++){const xvalue* V=xrtValueObjectGet(Credits,xrtStrView(Fields[F]));if(V)Ok=xrtValueObjectSet(Record,xrtStrView(Fields[F]),V);}
            Ok=Ok&&xrtValueObjectSet(After,xrtStrView(Key),Record)&&MdoExtPut(Item,"new_digest",DirectoryHash);xrtValueRelease(Record);xrtValueRelease(Meta);MdoZipClose(&Z);xrtClose(Input);
        }else if(Ok&&!strcmp(Op,"remove")){Ok=Old;if(Ok)xrtValueObjectRemove(After,xrtStrView(Key));}
        else if(Ok)Ok=Old&&MdoExtPlanFlag(After,Existing,Wanted);
        xrtValueRelease(Existing);xrtFree(Key);xrtFree(Root);xrtFree(Part);xrtFree(Incoming);
    }
    if(Ok)Ok=MdoExtGroupPlugins(Items,Base,Before,Error);
    char AfterHash[65],SuspendedHash[65];bool Prepared=false,Committed=false;
    Ok=Ok&&MdoExtValueHash(After,AfterHash)&&MdoExtValueHash(Suspended,SuspendedHash)&&MdoExtPut(Out,"id",Task)&&MdoExtPut(Out,"plan_id",PlanId)&&MdoExtPut(Out,"revision",Revision)&&xrtValueObjectSet(Out,XRT_STR_LITERAL("items"),Items)&&
        MdoExtPut(J,"phase","prepared")&&MdoExtPut(J,"before_hash",Expected)&&MdoExtPut(J,"after_hash",AfterHash)&&MdoExtPut(J,"suspended_hash",SuspendedHash)&&MdoExtPut(J,"plan_id",PlanId)&&xrtValueObjectSet(J,XRT_STR_LITERAL("receipt"),Out)&&xrtValueObjectSet(J,XRT_STR_LITERAL("items"),Items)&&
        MdoExtSaveJson(BeforePath,Before,MDO_EXT_STATE_LIMIT)&&MdoExtSaveJson(AfterPath,After,MDO_EXT_STATE_LIMIT)&&MdoExtSaveJson(Journal,J,MDO_EXT_PLAN_BYTES);
    Prepared=Ok;
    if(Ok){MdoExtGroupActive=true;Ok=MdoExtSaveJson(MDO_EXT_STATE,Suspended,MDO_EXT_STATE_LIMIT);}
    xrtMutexUnlock(MdoExtLock);
    if(Ok&&Reload)Ok=Reload(Items,true,Data,Error);
    xrtMutexLock(MdoExtLock);
    xvalue* Current=Ok?MdoExtReadJson(MDO_EXT_STATE,MDO_EXT_STATE_LIMIT,true):NULL;if(Ok)Ok=MdoExtValueHash(Current,Hash)&&!strcmp(Hash,SuspendedHash)&&(!Cancel||!xrtCancelRequested(Cancel));xrtValueRelease(Current);
    for(size_t I=0;Ok&&I<xrtValueCount(Items);I++){
        const xvalue* Item=xrtValueArrayGet(Items,I);cstr Kind=MdoExtensionText(Item,"kind",32),Directory=MdoExtensionText(Item,"directory_id",96),Op=MdoExtensionText(Item,"operation",16);bool Old=MdoGraphBool(Item,"old_exists",false);
        char* Root=MdoExtensionDirectoryPath(Kind,Directory);Ok=Root&&(!Old||MdoExtGroupDigest(Root,MdoExtensionText(Item,"old_digest",64),Error))&&(!strcmp(Op,"flags")||!MdoExtBusy(Kind,Directory));xrtFree(Root);
        if(!Ok&&Error&&!Error[0])MdoExtError(Error,"An affected extension is still in use or changed after inspection; finish its calls and inspect a new plan");
    }
    for(size_t I=0;Ok&&I<xrtValueCount(Items);I++){
        const xvalue* Item=xrtValueArrayGet(Items,I);cstr Kind=MdoExtensionText(Item,"kind",32),Directory=MdoExtensionText(Item,"directory_id",96),Op=MdoExtensionText(Item,"operation",16);if(!strcmp(Op,"flags"))continue;
        char *Root=MdoExtensionDirectoryPath(Kind,Directory),*Prior=xrtFormat("%s/%u/prior",Base,(unsigned)I),*Incoming=xrtFormat("%s/%u/incoming",Base,(unsigned)I),*Part=xrtFormat("%s/%u",Base,(unsigned)I);
        Ok=Root&&Prior&&Incoming&&Part&&MdoExtEnsure(Part)&&MdoExtEnsure(Kind)&&(!Cancel||!xrtCancelRequested(Cancel));if(Ok&&MdoGraphBool(Item,"old_exists",false))Ok=MdoHomeRenameNoReplace(Root,Prior);if(Ok&&!strcmp(Op,"install"))Ok=MdoHomeRenameNoReplace(Incoming,Root);xrtFree(Root);xrtFree(Prior);xrtFree(Incoming);xrtFree(Part);
    }
    if(Ok)Ok=MdoExtSaveJson(MDO_EXT_STATE,After,MDO_EXT_STATE_LIMIT)&&MdoExtPut(J,"phase","committed")&&MdoExtSaveJson(Journal,J,MDO_EXT_PLAN_BYTES);
    Committed=Ok;
    bool Recovered=true;if(!Ok&&Prepared){char Why[512]={0};Recovered=MdoExtGroupRecover(Task,Why);if(!Recovered){MdoExtRecoveryAll=true;(void)MdoExtRecoveryIssue(NULL,Why);if(Error)snprintf(Error,512,"%s",Why);}}
    bool Clean=true;if(Committed){char* Done=xrtFormat("%s/committed.json",Base);bool Saved=Done&&MdoExtGroupSaveReceipt(PlanId,Out)&&MdoHomeRenameNoReplace(Journal,Done);xrtFree(Done);Clean=Saved&&MdoExtGroupCleanup(Base,xrtValueCount(Items));}
    else if(!Prepared&&Base)(void)MdoExtGroupCleanup(Base,xrtValueCount(Items));
    MdoExtGroupActive=false;xrtMutexUnlock(MdoExtLock);
    char RuntimeError[512]={0};bool Runtime=Recovered&&(!Reload||Reload(Items,false,Data,RuntimeError));
    if(Committed){xrtValueObjectSetNew(Out,XRT_STR_LITERAL("runtime_ready"),xrtValueBool(Runtime));xrtValueObjectSetNew(Out,XRT_STR_LITERAL("cleanup_pending"),xrtValueBool(!Clean));if(!Runtime)MdoExtPut(Out,"runtime_error",RuntimeError);*Result=xrtValueClone(Out);(void)MdoExtGroupSaveReceipt(PlanId,Out);}
    else if(Error&&!Error[0])MdoExtError(Error,"The dependency plan became stale, was cancelled, lacks code approval, or failed before commit; previous directories and desired states were restored");
    xrtValueRelease(Before);xrtValueRelease(After);xrtValueRelease(Suspended);xrtValueRelease(Items);xrtValueRelease(J);xrtValueRelease(Out);xrtValueRelease(Intent);xrtFree(Task);xrtFree(Base);xrtFree(Journal);xrtFree(BeforePath);xrtFree(AfterPath);xrtFree(Stage);return Committed&&*Result;
}
