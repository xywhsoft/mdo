/* Keep publication intent next to its owned staging record before the atomic
 * filesystem boundary. Recovery verifies existing entries, never replays it. */
static xvalue* BuReceiveBegin(const mdo_sdk_context_v1* C,const xvalue* P,size_t* Slot,mdo_error_v1* E){
    cstr Id=SdkField(P,"id");*Slot=BU_RECEIVE_LIMIT;
    if(!BuReceiveId(Id)||!BuReceiveId(C->InvocationId)){
        MdoApplicationError(E,"sdk_arguments_invalid","Use an explicit operation.start identity for receiver publication or reconciliation","file_receive",422,false);return NULL;
    }
    xrtMutexLock(BuReceivers.Lock);bool Missing=false;xvalue* Row=BuReceiveLoad(Id,&Missing);
    if(!Row)MdoApplicationError(E,Missing?"sdk_file_receive_missing":"sdk_file_receive_unreadable","Receiver ownership is unavailable; preserve its files","file_receive",Missing?404:409,false);
    else if(!BuReceiveScope(C,Row,E)){}
    else if(BuReceiveActive(Id))MdoApplicationError(E,"sdk_file_receive_busy","Wait for the current receiver operation before changing publication state","file_receive",409,false);
    else {
        for(size_t I=0;I<BU_RECEIVE_LIMIT;I++)if(!BuReceivers.Active[I][0]){*Slot=I;snprintf(BuReceivers.Active[I],37,"%s",Id);break;}
        if(*Slot==BU_RECEIVE_LIMIT)MdoApplicationError(E,"sdk_file_receive_busy","Receiver workers are busy","file_receive",429,false);
    }
    xrtMutexUnlock(BuReceivers.Lock);
    if(*Slot==BU_RECEIVE_LIMIT){xrtValueRelease(Row);return NULL;}return Row;
}
static void BuReceiveEnd(size_t Slot){xrtMutexLock(BuReceivers.Lock);BuReceivers.Active[Slot][0]=0;xrtMutexUnlock(BuReceivers.Lock);}
static bool BuReceiveStoreLocked(const xvalue* Row){xrtMutexLock(BuReceivers.Lock);bool Ok=BuReceiveSave(Row);xrtMutexUnlock(BuReceivers.Lock);return Ok;}
static bool BuReceivePublish(const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E){
    cstr Target=SdkField(P,"path");if(!BuPublishPath(Target))return MdoApplicationError(E,"sdk_arguments_invalid","Select a new relative destination inside the original workspace","file_receive",422,false);
    size_t Slot; xvalue* Row=BuReceiveBegin(C,P,&Slot,E);if(!Row)return false;
    xvalue* Query=NULL;bool Ok=false,Intent=false;
    cstr State=SdkField(Row,"state");
    if(strcmp(State,"ready")){
        MdoApplicationError(E,"sdk_file_receive_review","Inspect the retained publication result before another save; old effects are never replayed","file_receive",409,BuReceiveRetained(State));goto done;
    }
    if(!BuFileContinue(C,E))goto done;
    Query=xrtValueObject();bool Built=Query&&SdkPut(Query,"source",SdkField(Row,"source"))&&SdkPut(Query,"path",Target)&&
        SdkPut(Query,"source_revision",SdkField(Row,"source_revision"))&&SdkPut(Query,"root_snapshot",SdkField(Row,"root_snapshot"))&&
        xrtValueObjectSet(Query,XRT_STR_LITERAL("size"),SdkGet(Row,"size"))&&SdkPut(Query,"sha256",SdkField(Row,"sha256"));
    if(!Built||!SdkPut(Row,"destination",Target)||!SdkPut(Row,"publication",C->InvocationId)||!SdkPut(Row,"state","publishing"))goto done;
    xrtValueObjectRemove(Row,XRT_STR_LITERAL("publication_file"));xrtValueObjectRemove(Row,XRT_STR_LITERAL("publication_verdict"));xrtValueObjectRemove(Row,XRT_STR_LITERAL("publication_error"));
    if(!BuReceiveStoreLocked(Row)){MdoApplicationError(E,"sdk_file_receive_receipt","Publication intent could not be confirmed; inspect this receiver before retrying","file_receive",503,true);goto done;}
    Intent=true;Ok=BuFilePublish(C,Query,R,E);
    if(Ok){
        if(!SdkPut(Row,"state","published")||!xrtValueObjectSet(Row,XRT_STR_LITERAL("publication_file"),*R)||!BuReceiveStoreLocked(Row)){
            Ok=MdoApplicationError(E,"sdk_file_receive_receipt","The destination may exist, but its final publication receipt was not confirmed","file_receive",503,true);
        }
    }else {
        bool Known=!E->MayHaveCompleted;
        if(Known&&!SdkPut(Row,"state","ready"))E->MayHaveCompleted=true;
        if(!SdkPut(Row,"publication_error",E->Code)||!BuReceiveStoreLocked(Row))E->MayHaveCompleted=true;
    }
done:
    if(!Ok&&!E->Code[0])MdoApplicationError(E,"sdk_file_receive_publish_failed","Cannot prepare the receiver publication","file_receive",503,Intent);
    BuReceiveEnd(Slot);xrtValueRelease(Query);xrtValueRelease(Row);return Ok;
}
/* Verify a single exact physical file through the held root. No path-based
 * digest fallback, partial hashes, file writes or destination recreation. */
static bool BuReceiveVerify(const mdo_sdk_context_v1* C,xroot Root,const xvalue* Row,cstr Name,xvalue** R,mdo_error_v1* E){
    uint64 Size=0,Device=0,Identity=0,Total=0;SdkUInt(SdkGet(Row,"size"),&Size);
    SdkUInt(SdkGet(Row,"stage_device"),&Device);SdkUInt(SdkGet(Row,"stage_identity"),&Identity);
    xfile File=NULL;unsigned char* Bytes=NULL;xvalue* Info=NULL;bool Ok=false;
    xfileinfo Named={0},Before={0},After={0},Current={0};
    if(!xrtRootStat(Root,Name,false,&Named)){SdkNativeFailure(E,"file_receive_review");goto done;}
    xfileoptions Options;xrtFileOptionsInit(&Options);Options.Flags=XFILE_READ|XFILE_NOFOLLOW;Options.Share=XFILE_SHARE_READ|XFILE_SHARE_DELETE;
    File=xrtRootFileOpen(Root,Name,&Options);if(!File||!xrtFileStat(File,&Before)){SdkNativeFailure(E,"file_receive_review");goto done;}
    Info=SdkFileInfo(&Before);cstr Revision=Info?SdkField(Info,"revision"):NULL;
    if(!Revision)goto done;
    const cstr Fields[]={"size","type","device","identity","modified"};
    for(size_t I=0;I<sizeof(Fields)/sizeof(*Fields);I++)if(!SdkGet(Info,Fields[I]))goto done;
    if(!BuPublishFileSame(&Named,&Before)||Before.Device!=Device||Before.Identity!=Identity||Before.Size!=Size||strcmp(Revision,SdkField(Row,"source_revision"))){
        MdoApplicationError(E,"sdk_file_receive_conflict","The retained file identity or revision changed; unknown files were preserved","file_receive_review",412,false);goto done;
    }
    Bytes=xrtMalloc(65536);if(!Bytes)goto done;
    xsha256 Sha;uint8 Digest[32];char Hex[65];xrtSha256Init(&Sha);
    for(;;){
        if(!BuFileContinue(C,E))goto done;size_t N=0;
        if(!xrtRead(File,Bytes,65536,&N)){SdkNativeFailure(E,"file_receive_review");goto done;}
        if(!N)break;if(N>Size-Total){MdoApplicationError(E,"sdk_file_receive_conflict","The retained file grew during inspection","file_receive_review",412,false);goto done;}
        Total+=N;if(!xrtSha256Update(&Sha,Bytes,N))goto done;
    }
    if(!xrtFileStat(File,&After)||!xrtRootStat(Root,Name,false,&Current)){SdkNativeFailure(E,"file_receive_review");goto done;}
    if(Total!=Size||!BuPublishFileSame(&Before,&After)||!BuPublishFileSame(&Before,&Current)){
        MdoApplicationError(E,"sdk_file_receive_conflict","The retained file changed during inspection","file_receive_review",412,false);goto done;
    }
    if(!xrtSha256Final(&Sha,Digest))goto done;
    static const char Digits[]="0123456789abcdef";for(size_t I=0;I<32;I++){Hex[I*2]=Digits[Digest[I]>>4];Hex[I*2+1]=Digits[Digest[I]&15];}Hex[64]=0;
    if(strcmp(Hex,SdkField(Row,"sha256"))){MdoApplicationError(E,"sdk_file_receive_digest","The retained bytes differ from their completed download hash","file_receive_review",412,false);goto done;}
    Ok=SdkPut(Info,"path",Name)&&SdkPut(Info,"sha256",Hex);
done:
    if(File&&!xrtClose(File)&&Ok){Ok=false;SdkNativeFailure(E,"file_receive_review");}
    if(Bytes){xrtSecureZero(Bytes,65536);xrtFree(Bytes);}
    if(Ok){*R=Info;Info=NULL;}else if(!E->Code[0])MdoApplicationError(E,"sdk_file_receive_review_failed","Cannot verify the complete retained file snapshot","file_receive_review",503,false);
    xrtValueRelease(Info);return Ok;
}
static bool BuReceiveReconcile(const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E){
    size_t Slot;xvalue* Row=BuReceiveBegin(C,P,&Slot,E);if(!Row)return false;
    xroot Root=NULL;xvalue* File=NULL,*Info=NULL;bool Ok=false;
    if(!BuReceiveRetained(SdkField(Row,"state"))){MdoApplicationError(E,"sdk_file_receive_incomplete","Incomplete stream bytes cannot be published; discard the interrupted staging explicitly","file_receive_review",409,false);goto done;}
    cstr Target=SdkField(Row,"destination");if(!Target){MdoApplicationError(E,"sdk_file_receive_destination","This older staging record has no destination intent; choose a path explicitly","file_receive_review",409,false);goto done;}
    Root=xrtRootOpen(C->Workspace);if(!Root||!BuReceiveRoot(Root,Row)){MdoApplicationError(E,"sdk_workspace_conflict","The original workspace root cannot be confirmed; files were preserved","file_receive_review",412,false);goto done;}
    xfileinfo Named={0};bool Exists=xrtRootStat(Root,Target,false,&Named),Owned=false;
    if(!Exists){const xerror* Error=xrtGetError();if(!Error||xrtErrorKind(Error)!=XERR_NOT_FOUND){SdkNativeFailure(E,"file_receive_review");goto done;}}
    uint64 Device=0,Identity=0;SdkUInt(SdkGet(Row,"stage_device"),&Device);SdkUInt(SdkGet(Row,"stage_identity"),&Identity);
    Owned=Exists&&Named.Type==XFILE_TYPE_FILE&&(Named.Available&XFILE_INFO_IDENTITY)&&Named.Device==Device&&Named.Identity==Identity;
    if(!BuReceiveVerify(C,Root,Row,Owned?Target:SdkField(Row,"source"),&File,E))goto done;
    if(!BuFileContinue(C,E))goto done;
    if(!SdkPut(Row,"state",Owned?"published":"ready")||!SdkPut(Row,"publication_verdict",Owned?"verified":Exists?"different_file":"missing"))goto done;
    if(Owned){
        if(!BuReceiveId(SdkField(Row,"publication"))){MdoApplicationError(E,"sdk_file_receive_review","Matching bytes exist, but the original publication intent is missing; no state was claimed","file_receive_review",409,false);goto done;}
        if(!xrtValueObjectSet(Row,XRT_STR_LITERAL("publication_file"),File))goto done;
    }else xrtValueObjectRemove(Row,XRT_STR_LITERAL("publication_file"));
    xrtValueObjectRemove(Row,XRT_STR_LITERAL("publication_error"));
    Info=xrtValueClone(Row);if(!Info)goto done;
    if(!BuReceiveStoreLocked(Row)){MdoApplicationError(E,"sdk_file_receive_receipt","File inspection completed but its recovery receipt was not saved","file_receive_review",503,false);goto done;}
    Ok=true;
done:
    if(Root&&!xrtRootClose(Root)&&Ok){Ok=false;SdkNativeFailure(E,"file_receive_review");}
    BuReceiveEnd(Slot);xrtValueRelease(Row);xrtValueRelease(File);
    if(Ok){*R=Info;Info=NULL;}else if(!E->Code[0])MdoApplicationError(E,"sdk_file_receive_review_failed","Cannot prepare the bounded recovery result","file_receive_review",503,false);
    xrtValueRelease(Info);return Ok;
}
