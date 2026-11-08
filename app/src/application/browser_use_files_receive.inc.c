/* Bounded, task-owned stream -> verified staging. No URL fetch or whole-file
 * result. A small external-Home intent precedes creation and an inode receipt
 * precedes every nonempty write. The SDK operation ledger prevents replay. */
#include "../../include/mdo/extension_files.h"
static xvalue* BuStreamScopeCapture(const mdo_sdk_context_v1* C);
static bool BuStreamScopeMatches(const mdo_sdk_context_v1* C,const xvalue* Scope);
#define BU_RECEIVE_DIRECTORY "data/application/receivers"
#define BU_RECEIVE_LIMIT 8u
#define BU_RECEIVE_BYTES (128u*1024u*1024u)
static struct {xmutex* Lock;char Active[BU_RECEIVE_LIMIT][37];} BuReceivers;
bool MdoApplicationReceiversInit(void){BuReceivers.Lock=xrtMutexCreate();return BuReceivers.Lock!=NULL;}
void MdoApplicationReceiversUnit(void){if(BuReceivers.Lock)xrtMutexDestroy(BuReceivers.Lock);memset(&BuReceivers,0,sizeof(BuReceivers));}
static bool BuReceiveId(cstr Id){
    if(!Id||strlen(Id)!=36||strncmp(Id,"sdk-",4))return false;
    for(size_t I=4;I<36;I++)if(!((Id[I]>='0'&&Id[I]<='9')||(Id[I]>='a'&&Id[I]<='f')))return false;
    return true;
}
static void BuReceivePaths(cstr Id,char Ledger[128],char Source[64]){
    snprintf(Ledger,128,BU_RECEIVE_DIRECTORY "/%s.json",Id);
    snprintf(Source,64,".mdo-receive-%s.part",Id+4);
}
static bool BuReceiveActive(cstr Id){
    for(size_t I=0;I<BU_RECEIVE_LIMIT;I++)if(!strcmp(Id,BuReceivers.Active[I]))return true;return false;
}
static bool BuReceiveHash(cstr Hash){
    if(!Hash||strlen(Hash)!=64)return false;
    for(size_t I=0;I<64;I++)if(!((Hash[I]>='0'&&Hash[I]<='9')||(Hash[I]>='a'&&Hash[I]<='f')))return false;return true;
}
static bool BuReceiveRetained(cstr State){return State&&(!strcmp(State,"ready")||!strcmp(State,"publishing")||!strcmp(State,"published"));}
static bool BuReceiveSave(const xvalue* Row){
    char Ledger[128],Source[64];BuReceivePaths(SdkField(Row,"id"),Ledger,Source);
    char* Json=SdkJson(Row);bool Ok=Json&&strlen(Json)<=32768&&MdoHomeAtomicWrite(Ledger,Json,strlen(Json),false);xrtFree(Json);return Ok;
}
static bool BuReceiveValid(const xvalue* Row,cstr Id){
    uint64 Version=0,Size=0;char Ledger[128],Source[64];BuReceivePaths(Id,Ledger,Source);
    cstr Actual=SdkField(Row,"id"),Workspace=SdkField(Row,"workspace"),State=SdkField(Row,"state"),Name=SdkField(Row,"source");
    bool Valid=SdkUInt(SdkGet(Row,"version"),&Version)&&Version==1&&Actual&&!strcmp(Actual,Id)&&
        Workspace&&strlen(Workspace)<=4096&&xrtPathIsAbs(Workspace)&&State&&
        (!strcmp(State,"preparing")||!strcmp(State,"receiving")||BuReceiveRetained(State))&&
        Name&&!strcmp(Name,Source)&&SdkField(Row,"root_snapshot")&&BuReceiveHash(SdkField(Row,"sha256"))&&
        SdkUInt(SdkGet(Row,"size"),&Size)&&Size<=67108864&&xrtValueType(SdkGet(Row,"scope"))==XVALUE_OBJECT;
    if(!Valid)return false;
    if(SdkGet(Row,"destination")&&!BuPublishPath(SdkField(Row,"destination")))return false;
    if(strcmp(State,"preparing")){
        uint64 Device=0,Identity=0;if(!SdkUInt(SdkGet(Row,"stage_device"),&Device)||!SdkUInt(SdkGet(Row,"stage_identity"),&Identity))return false;
    }
    if(BuReceiveRetained(State)){cstr Revision=SdkField(Row,"source_revision");if(!Revision||!Revision[0]||strlen(Revision)>160)return false;}
    if(!strcmp(State,"publishing")||!strcmp(State,"published")){
        if(!BuReceiveId(SdkField(Row,"publication"))||!SdkField(Row,"destination"))return false;
    }
    if(!strcmp(State,"published")){
        const xvalue* File=SdkGet(Row,"publication_file");uint64 PublishedSize=0;
        cstr Path=SdkField(File,"path"),Hash=SdkField(File,"sha256"),Revision=SdkField(File,"revision");
        if(!Path||strcmp(Path,SdkField(Row,"destination"))||!Hash||strcmp(Hash,SdkField(Row,"sha256"))||!Revision||!Revision[0]||
           !SdkUInt(SdkGet(File,"size"),&PublishedSize)||PublishedSize!=Size)return false;
    }
    return true;
}
static xvalue* BuReceiveLoad(cstr Id,bool* Missing){
    char Ledger[128],Source[64];BuReceivePaths(Id,Ledger,Source);
    char* Text=MdoExtensionRead(Ledger,true,32768,Missing);xvalue* Row=Text?SdkParse(Text):NULL;xrtFree(Text);
    if(Row&&!BuReceiveValid(Row,Id)){xrtValueRelease(Row);Row=NULL;}return Row;
}
static bool BuReceiveRemoveLedger(cstr Id){char Ledger[128],Source[64];BuReceivePaths(Id,Ledger,Source);return MdoHomeRemove(Ledger,false);}
static bool BuReceiveRoot(xroot Root,const xvalue* Row){
    xfileinfo Info={0};return xrtRootStat(Root,".",false,&Info)&&(Info.Available&XFILE_INFO_IDENTITY)&&
        BuPublishRootMatches(SdkField(Row,"root_snapshot"),&Info);
}
/* Windows deletes the verified opened object, not a freshly resolved path.
 * XRT's POSIX delete-open contract requires namespace serialization unavailable
 * here. Do not replace it with unsafe path unlink; adapt that platform separately. */
static bool BuReceiveDrop(xroot Root,const xvalue* Row){
    cstr Source=SdkField(Row,"source");xfileinfo Named={0},Opened={0};
    if(!BuReceiveRoot(Root,Row))return false;
    if(!xrtRootStat(Root,Source,false,&Named)){
        const xerror* Error=xrtGetError();return Error&&xrtErrorKind(Error)==XERR_NOT_FOUND&&BuReceiveRemoveLedger(SdkField(Row,"id"));
    }
    uint64 Device=0,Identity=0;
    if(Named.Type!=XFILE_TYPE_FILE||!(Named.Available&XFILE_INFO_IDENTITY)||
       !SdkUInt(SdkGet(Row,"stage_device"),&Device)||!SdkUInt(SdkGet(Row,"stage_identity"),&Identity)||Named.Device!=Device||Named.Identity!=Identity)return false;
    xfileoptions Options;xrtFileOptionsInit(&Options);Options.Flags=XFILE_READ|XFILE_NOFOLLOW;Options.Share=XFILE_SHARE_READ|XFILE_SHARE_DELETE;
    xfile File=xrtRootFileOpen(Root,Source,&Options);if(!File)return false;
    bool Ok=xrtFileStat(File,&Opened)&&Opened.Type==XFILE_TYPE_FILE&&(Opened.Available&XFILE_INFO_IDENTITY)&&Opened.Device==Device&&Opened.Identity==Identity;
#if defined(_WIN32) || defined(_WIN64)
    if(Ok)Ok=xrtFileDeleteOpen(File,Source);
#else
    Ok=false;
#endif
    if(!xrtClose(File))Ok=false;
    if(Ok){
        if(xrtRootStat(Root,Source,false,&Named))Ok=false;
        else {const xerror* Error=xrtGetError();Ok=Error&&xrtErrorKind(Error)==XERR_NOT_FOUND;}
    }
    return Ok&&BuReceiveRemoveLedger(SdkField(Row,"id"));
}
static xvalue* BuReceiveInventory(bool Reap,mdo_error_v1* E){
    xdir Directory=MdoHomeOpenDirectory(BU_RECEIVE_DIRECTORY,XDIR_STAT);xvalue* Items=xrtValueArray();if(!Items)goto invalid;
    if(!Directory){const xerror* Error=xrtGetError();if(Error&&xrtErrorKind(Error)==XERR_NOT_FOUND)return Items;goto invalid;}
    xdirentry Entry;xdirnext Next;size_t Count=0;
    while((Next=xrtDirNext(Directory,&Entry))==XDIR_NEXT_ITEM){
        if(++Count>32||Entry.Name.Size!=41||Entry.Info.Type!=XFILE_TYPE_FILE||memcmp(Entry.Name.Data+36,".json",5))goto invalid;
        char Id[37];memcpy(Id,Entry.Name.Data,36);Id[36]=0;if(!BuReceiveId(Id))goto invalid;
        bool Missing=false;xvalue* Row=BuReceiveLoad(Id,&Missing);if(!Row)goto invalid;
        bool Active=BuReceiveActive(Id),Ready=BuReceiveRetained(SdkField(Row,"state"));
        if(Reap&&!Active&&!Ready){
            xroot Root=xrtRootOpen(SdkField(Row,"workspace"));bool Removed=Root&&BuReceiveDrop(Root,Row);
            if(Root)xrtRootClose(Root);if(Removed){xrtValueRelease(Row);continue;}
        }
        if(!xrtValueArrayAppendNew(Items,Row))goto invalid;
    }
    if(Next==XDIR_NEXT_ERROR)goto invalid;
    if(!xrtDirClose(Directory)){Directory=NULL;goto invalid;}return Items;
invalid:
    if(Directory)xrtDirClose(Directory);xrtValueRelease(Items);
    MdoApplicationError(E,"sdk_file_receive_inventory","Cannot verify the bounded receiver inventory; unknown entries were preserved","file_receive",409,false);return NULL;
}
static bool BuReceiveScope(const mdo_sdk_context_v1* C,const xvalue* Row,mdo_error_v1* E){
    if(!BuStreamScopeMatches(C,SdkGet(Row,"scope"))||!C->Workspace||strcmp(C->Workspace,SdkField(Row,"workspace")))
        return MdoApplicationError(E,"context_mismatch","File receiver belongs to another task or original workspace","file_receive",409,false);return true;
}
static xvalue* BuReceiveInfo(const xvalue* Row){
    xvalue* Info=xrtValueClone(Row);if(!Info)return NULL;
    bool Active=BuReceiveActive(SdkField(Row,"id")),Ready=BuReceiveRetained(SdkField(Row,"state"));
    if((!Active&&!Ready&&!SdkPut(Info,"state","interrupted"))||
       !xrtValueObjectSetNew(Info,XRT_STR_LITERAL("active"),xrtValueBool(Active))){xrtValueRelease(Info);return NULL;}return Info;
}
static bool BuReceiveControl(const mdo_sdk_context_v1* C,cstr Method,const xvalue* P,xvalue** R,mdo_error_v1* E){
    cstr Id=SdkField(P,"id");bool List=!strcmp(Method,"files.receive.list"),Release=!strcmp(Method,"files.receive.release");
    if(!List&&!BuReceiveId(Id))return MdoApplicationError(E,"sdk_arguments_invalid","An sdk- receiver identity is required","file_receive",422,false);
    xrtMutexLock(BuReceivers.Lock);bool Ok=false;xvalue* Row=NULL;
    if(List){
        xvalue* Items=BuReceiveInventory(false,E);if(!Items)goto done;
        *R=xrtValueArray();Ok=*R!=NULL;
        for(size_t I=0;Ok&&I<xrtValueCount(Items);I++){
            const xvalue* Item=xrtValueArrayGet(Items,I);
            if(BuStreamScopeMatches(C,SdkGet(Item,"scope"))&&C->Workspace&&!strcmp(C->Workspace,SdkField(Item,"workspace"))){
                xvalue* Info=BuReceiveInfo(Item);Ok=Info&&xrtValueArrayAppendNew(*R,Info);
            }
        }xrtValueRelease(Items);goto done;
    }
    bool Missing=false;Row=BuReceiveLoad(Id,&Missing);
    if(!Row){MdoApplicationError(E,Missing?"sdk_file_receive_missing":"sdk_file_receive_unreadable",Missing?"File receiver is not retained":"Receiver ownership cannot be read; preserve its files","file_receive",Missing?404:409,false);goto done;}
    if(!BuReceiveScope(C,Row,E))goto done;
    if(Release){
        if(BuReceiveActive(Id)){MdoApplicationError(E,"sdk_file_receive_busy","Cancel and wait for the receive operation before releasing its staging","file_receive",409,false);goto done;}
        *R=xrtValueObject();if(!*R||!xrtValueObjectSetNew(*R,XRT_STR_LITERAL("released"),xrtValueBool(true)))goto done;
        xroot Root=xrtRootOpen(SdkField(Row,"workspace"));Ok=Root&&BuReceiveDrop(Root,Row);if(Root&&!xrtRootClose(Root))Ok=false;
        if(!Ok)MdoApplicationError(E,"sdk_file_receive_cleanup","Cannot confirm owned staging cleanup; original or replaced files were preserved","file_receive",409,true);
    }else {*R=BuReceiveInfo(Row);Ok=*R!=NULL;}
done:
    xrtValueRelease(Row);xrtMutexUnlock(BuReceivers.Lock);
    if(!Ok){xrtValueRelease(*R);*R=NULL;if(!E->Code[0])MdoApplicationError(E,"sdk_file_receive_failed","Cannot prepare receiver result","file_receive",503,false);}return Ok;
}
static bool BuFileReceive(const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E){
#if !defined(_WIN32) && !defined(_WIN64)
    return MdoApplicationError(E,"sdk_file_receive_unsupported","Safe opened-file staging cleanup is not available on this platform","file_receive",501,false);
#else
    cstr Id=C->InvocationId,Handle=SdkField(P,"handle"),Hash=SdkField(P,"sha256"),RootSnapshot=SdkField(P,"root_snapshot"),Destination=SdkField(P,"destination");
    uint64 Size=0,Timeout=120000,Idle=30000,Total=0;
    if(!BuReceiveId(Id)||!Handle||strlen(Handle)!=36||strncmp(Handle,"str-",4)||!BuReceiveHash(Hash)||!RootSnapshot||
       !SdkUInt(SdkGet(P,"size"),&Size)||Size>67108864||
       (SdkGet(P,"timeout_ms")&&!SdkUInt(SdkGet(P,"timeout_ms"),&Timeout))||Timeout<100||Timeout>600000||
       (SdkGet(P,"idle_timeout_ms")&&!SdkUInt(SdkGet(P,"idle_timeout_ms"),&Idle))||Idle<100||Idle>60000)
        return MdoApplicationError(E,"sdk_arguments_invalid","Start files.receive through operation.start with a scoped stream, root snapshot, exact size/hash and bounded timeouts","file_receive",422,false);
    if(!C->Workspace||!xrtPathIsAbs(C->Workspace)||strlen(C->Workspace)>4096)
        return MdoApplicationError(E,"sdk_workspace_required","Receive requires the frozen absolute task workspace","file_receive",422,false);
    if(SdkGet(P,"destination")&&!BuPublishPath(Destination))
        return MdoApplicationError(E,"sdk_arguments_invalid","Destination intent must be a relative path in the original workspace","file_receive",422,false);
    xroot Root=NULL,Storage=NULL;xfile File=NULL;xvalue *Row=NULL,*Query=NULL,*Reply=NULL,*Result=NULL;unsigned char* Bytes=NULL;
    bool Ok=false,Admitted=false,Recorded=false,Ready=false,CleanupUncertain=false;size_t Slot=BU_RECEIVE_LIMIT,Buffered=0;char Ledger[128],Source[64];BuReceivePaths(Id,Ledger,Source);
    xfileinfo Directory={0},Created={0},After={0},Named={0};uint64 End=MdoDeadlineAfterUs(Timeout*1000),IdleEnd=MdoDeadlineAfterUs(Idle*1000);
    if(!BuFileContinue(C,E))goto done;
    Root=xrtRootOpen(C->Workspace);if(!Root||!xrtRootStat(Root,".",false,&Directory)){SdkNativeFailure(E,"file_receive");goto done;}
    if(!(Directory.Available&XFILE_INFO_IDENTITY)||!BuPublishRootMatches(RootSnapshot,&Directory)){
        MdoApplicationError(E,"sdk_workspace_conflict","Workspace changed since selection; no receiver was created","file_receive",412,false);goto done;
    }
    Query=xrtValueObject();if(!Query||!SdkPut(Query,"handle",Handle)||!SdkPutUInt(Query,"limit",16384))goto done;
    if(MdoApplicationCall(C,"stream.stat",Query,&Reply,E)!=MDO_RESULT_OK)goto done;xrtValueRelease(Reply);Reply=NULL;
    Row=xrtValueObject();xvalue* Scope=BuStreamScopeCapture(C);
    bool Built=Row&&Scope&&xrtValueObjectSet(Row,XRT_STR_LITERAL("scope"),Scope)&&SdkPutUInt(Row,"version",1)&&
        SdkPut(Row,"id",Id)&&SdkPut(Row,"workspace",C->Workspace)&&SdkPut(Row,"root_snapshot",RootSnapshot)&&
        SdkPut(Row,"source",Source)&&SdkPut(Row,"state","preparing")&&SdkPutUInt(Row,"size",Size)&&SdkPut(Row,"sha256",Hash)&&(!Destination||SdkPut(Row,"destination",Destination));
    xrtValueRelease(Scope);if(!Built)goto done;
    Storage=MdoHomeOpenStorageDirectory(BU_RECEIVE_DIRECTORY);if(!Storage){SdkNativeFailure(E,"file_receive");goto done;}
    if(!xrtRootClose(Storage)){Storage=NULL;SdkNativeFailure(E,"file_receive");goto done;}Storage=NULL;
    xrtMutexLock(BuReceivers.Lock);
    xvalue* Inventory=BuReceiveInventory(true,E);uint64 Reserved=0;size_t Count=Inventory?xrtValueCount(Inventory):0;
    if(Inventory)for(size_t I=0;I<Count;I++){
        const xvalue* Existing=xrtValueArrayGet(Inventory,I);uint64 N=0;SdkUInt(SdkGet(Existing,"size"),&N);Reserved+=N;
        if(!strcmp(Id,SdkField(Existing,"id"))){MdoApplicationError(E,"sdk_file_receive_exists","Receiver identity is already retained; inspect it instead of replaying","file_receive",409,false);break;}
    }
    if(Inventory&&!E->Code[0]){
        for(size_t I=0;I<BU_RECEIVE_LIMIT;I++)if(!BuReceivers.Active[I][0]){Slot=I;break;}
        if(Count>=BU_RECEIVE_LIMIT||Reserved>BU_RECEIVE_BYTES||Size>BU_RECEIVE_BYTES-Reserved||Slot==BU_RECEIVE_LIMIT)
            MdoApplicationError(E,"sdk_file_receive_limit","Release retained receivers; staging is limited to eight files and 128 MiB","file_receive",429,false);
        else {
            snprintf(BuReceivers.Active[Slot],37,"%s",Id);Admitted=true;
            if(BuReceiveSave(Row))Recorded=true;else MdoApplicationError(E,"sdk_file_receive_receipt","Cannot persist bounded receiver intent; no file bytes were written","file_receive",503,false);
        }
    }
    xrtValueRelease(Inventory);xrtMutexUnlock(BuReceivers.Lock);if(!Recorded)goto done;
    xfileoptions Options;xrtFileOptionsInit(&Options);Options.Flags=XFILE_READ|XFILE_WRITE|XFILE_CREATE|XFILE_EXCLUSIVE|XFILE_NOFOLLOW;
    Options.Mode=0600;Options.Share=XFILE_SHARE_READ|XFILE_SHARE_DELETE;
    File=xrtRootFileOpen(Root,Source,&Options);if(!File||!xrtFileStat(File,&Created)){SdkNativeFailure(E,"file_receive");goto done;}
    if(Created.Type!=XFILE_TYPE_FILE||!(Created.Available&XFILE_INFO_IDENTITY)||Created.Size){MdoApplicationError(E,"sdk_file_receive_conflict","Exclusive staging identity cannot be verified","file_receive",409,false);goto done;}
    if(!SdkPutUInt(Row,"stage_device",Created.Device)||!SdkPutUInt(Row,"stage_identity",Created.Identity)||!SdkPut(Row,"state","receiving"))goto done;
    xrtMutexLock(BuReceivers.Lock);bool Owned=BuReceiveSave(Row);xrtMutexUnlock(BuReceivers.Lock);
    if(!Owned){MdoApplicationError(E,"sdk_file_receive_receipt","Cannot persist staging ownership before writing file bytes","file_receive",503,false);goto done;}
    xsha256 Sha;uint8 Digest[XRT_SHA256_SIZE];char Hex[65];xrtSha256Init(&Sha);
    for(;;){
        if(!BuFileContinue(C,E))goto done;
        if(MdoDeadlineExpired(End)||MdoDeadlineExpired(IdleEnd)){MdoApplicationError(E,"sdk_file_receive_timeout","Receiver deadline or idle budget expired before explicit EOF","file_receive",504,false);goto done;}
        if(MdoApplicationCall(C,"stream.read",Query,&Reply,E)!=MDO_RESULT_OK){
            if(!strcmp(E->Code,"sdk_stream_would_block")){memset(E,0,sizeof(*E));xrtSleep(10);continue;}goto done;
        }
        size_t CountBytes=0;uint64 Reported=0;bool Eof=false;cstr Encoded=SdkField(Reply,"base64");
        if(!SdkUInt(SdkGet(Reply,"bytes"),&Reported)||Reported>16384||!xrtValueGetBool(SdkGet(Reply,"eof"),&Eof)||
           (!Reported&&!Eof)||!Encoded||strlen(Encoded)>21848||!xrtBase64Decode(Encoded,strlen(Encoded),NULL,0,&CountBytes,NULL)||CountBytes!=Reported){
            if(!E->Code[0])MdoApplicationError(E,"sdk_file_receive_stream","Stream returned an invalid bounded byte receipt","file_receive",409,false);goto done;
        }
        Buffered=CountBytes;Bytes=xrtMalloc(CountBytes+1);if(!Bytes)goto done;
        if(!xrtBase64Decode(Encoded,strlen(Encoded),Bytes,CountBytes,&CountBytes,NULL)){SdkNativeFailure(E,"file_receive");goto done;}
        if(CountBytes>Size-Total){MdoApplicationError(E,"sdk_file_receive_size","Stream exceeds the exact selected download size","file_receive",413,false);goto done;}
        if(CountBytes){
            if(!xrtWriteFull(File,Bytes,CountBytes,NULL)||!xrtSha256Update(&Sha,Bytes,CountBytes)){SdkNativeFailure(E,"file_receive");goto done;}
            Total+=CountBytes;IdleEnd=MdoDeadlineAfterUs(Idle*1000);
        }
        xrtSecureZero(Bytes,Buffered);xrtFree(Bytes);Bytes=NULL;Buffered=0;xrtValueRelease(Reply);Reply=NULL;
        if(Eof)break;
    }
    if(Total!=Size){MdoApplicationError(E,"sdk_file_receive_size","Explicit EOF arrived before the selected download size","file_receive",412,false);goto done;}
    if(!xrtSha256Final(&Sha,Digest))goto done;
    static const char Digits[]="0123456789abcdef";for(size_t I=0;I<XRT_SHA256_SIZE;I++){Hex[I*2]=Digits[Digest[I]>>4];Hex[I*2+1]=Digits[Digest[I]&15];}Hex[64]=0;
    if(strcmp(Hash,Hex)){MdoApplicationError(E,"sdk_file_receive_digest","Complete received SHA-256 does not match the original download","file_receive",412,false);goto done;}
    if(!BuFileContinue(C,E))goto done;
    if(!xrtFlush(File)||!xrtFileStat(File,&After)||!xrtRootStat(Root,Source,false,&Named)){SdkNativeFailure(E,"file_receive");goto done;}
    if(After.Device!=Created.Device||After.Identity!=Created.Identity||After.Size!=Size||!BuPublishFileSame(&After,&Named)){
        MdoApplicationError(E,"sdk_file_receive_conflict","Staging entry changed during receiving","file_receive",412,false);goto done;
    }
    xvalue* Snapshot=SdkFileInfo(&After);cstr Revision=Snapshot?SdkField(Snapshot,"revision"):NULL;
    bool Complete=Revision&&SdkPut(Row,"source_revision",Revision)&&SdkPut(Row,"state","ready");xrtValueRelease(Snapshot);if(!Complete)goto done;
    Result=xrtValueClone(Row);if(!Result)goto done;
    xrtMutexLock(BuReceivers.Lock);Ready=BuReceiveSave(Row);xrtMutexUnlock(BuReceivers.Lock);if(!Ready){MdoApplicationError(E,"sdk_file_receive_receipt","Cannot persist the completed staging receipt","file_receive",503,false);goto done;}
    Ok=true;
done:
    if(Bytes){xrtSecureZero(Bytes,Buffered);xrtFree(Bytes);}
    if(File&&!xrtClose(File)&&Ok){Ok=false;SdkNativeFailure(E,"file_receive");}
    if(Admitted){
        xrtMutexLock(BuReceivers.Lock);
        bool Clean=true;if(!Ready&&Recorded)Clean=BuReceiveDrop(Root,Row);
        BuReceivers.Active[Slot][0]=0;xrtMutexUnlock(BuReceivers.Lock);
        if(!Clean)CleanupUncertain=true;
    }
    if(Root&&!xrtRootClose(Root)&&Ok){Ok=false;SdkNativeFailure(E,"file_receive");}
    if(Storage)xrtRootClose(Storage);
    if(!Ok){if(!E->Code[0])MdoApplicationError(E,"sdk_file_receive_failed","Cannot finish the bounded receiver","file_receive",503,Ready||CleanupUncertain);else if(Ready||CleanupUncertain)E->MayHaveCompleted=true;}
    else {*R=Result;Result=NULL;}
    xrtValueRelease(Result);xrtValueRelease(Row);xrtValueRelease(Query);xrtValueRelease(Reply);return Ok;
#endif
}
