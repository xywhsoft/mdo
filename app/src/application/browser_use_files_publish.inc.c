/* Publish verified private staging bytes with a pinned root and no overwrite.
 * This creates a hard link, not an independent copy; the caller retains and
 * owns the source. Never remove an uncertain destination or an arbitrary source. */
static bool BuPublishNumber(const char** Cursor,uint64* Value){
    const char* P=*Cursor;uint64 N=0;if(*P<'0'||*P>'9')return false;
    if(*P=='0'&&P[1]>='0'&&P[1]<='9')return false;
    while(*P>='0'&&*P<='9'){uint64 Digit=(uint64)(*P-'0');
        if(N>(UINT64_MAX-Digit)/10)return false;N=N*10+Digit;P++;}
    *Cursor=P;*Value=N;return true;
}
static bool BuPublishRootMatches(cstr Revision,const xfileinfo* Root){
    if(!Revision||strlen(Revision)>160||strncmp(Revision,"f-",2))return false;
    const char* P=Revision+2;uint64 Device=0,Identity=0,Size=0,Modified=0;
    if(!BuPublishNumber(&P,&Device)||*P++!='-'||!BuPublishNumber(&P,&Identity)||*P++!='-'||
       !BuPublishNumber(&P,&Size)||*P++!='-')return false;
    bool Negative=*P=='-';if(Negative)P++;
    if(!BuPublishNumber(&P,&Modified)||*P||Modified>(Negative?(uint64)INT64_MAX+1:(uint64)INT64_MAX))return false;
    /* Creating staging entries legitimately changes directory size/time. Only
     * the observed physical root identity, never its spelling, pins this task. */
    return Root->Type==XFILE_TYPE_DIRECTORY&&Root->Device==Device&&Root->Identity==Identity;
}
static bool BuPublishPath(cstr Path){
    if(!Path||!SdkPortablePath(Path)||strlen(Path)>4096||Path[strlen(Path)-1]=='/')return false;
    for(const unsigned char* P=(const unsigned char*)Path;*P;P++)if(*P<32||*P==127)return false;
    return true;
}
static bool BuPublishFileSame(const xfileinfo* A,const xfileinfo* B){
    const uint32 Required=XFILE_INFO_IDENTITY|XFILE_INFO_SIZE|XFILE_INFO_MODIFY_TIME;
    return (A->Available&Required)==Required&&(B->Available&Required)==Required&&BuFileSame(A,B);
}
static bool BuFilePublish(const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E){
    cstr Source=SdkField(P,"source"),Target=SdkField(P,"path"),Expected=SdkField(P,"source_revision");
    cstr RootRevision=SdkField(P,"root_snapshot"),Hash=SdkField(P,"sha256");uint64 Size=0,Total=0;
    if(!BuPublishPath(Source)||!BuPublishPath(Target)||!strcmp(Source,Target)||!Expected||!Expected[0]||
       strlen(Expected)>160||!RootRevision||!Hash||strlen(Hash)!=64||
       !SdkUInt(SdkGet(P,"size"),&Size)||Size>67108864)
        return MdoApplicationError(E,"sdk_arguments_invalid","Publish requires relative source/target, root/source snapshots, size and lowercase SHA-256","file_publish",422,false);
    for(size_t I=0;I<64;I++)if(!((Hash[I]>='0'&&Hash[I]<='9')||(Hash[I]>='a'&&Hash[I]<='f')))
        return MdoApplicationError(E,"sdk_arguments_invalid","SHA-256 must contain 64 lowercase hexadecimal digits","file_publish",422,false);
    if(!C->Workspace||!C->Workspace[0]||!xrtPathIsAbs(C->Workspace))
        return MdoApplicationError(E,"sdk_workspace_required","Publish requires the original absolute task workspace","file_publish",422,false);
    xroot Root=NULL;xfile File=NULL,Published=NULL;unsigned char* Bytes=NULL;
    xfileinfo Directory={0},Named={0},Before={0},After={0},Current={0},Destination={0};
    xvalue* Metadata=NULL;bool Ok=false,Attempted=false;
    if(!BuFileContinue(C,E))goto done;
    Root=xrtRootOpen(C->Workspace);if(!Root||!xrtRootStat(Root,".",false,&Directory)){SdkNativeFailure(E,"file_publish");goto done;}
    if(!(Directory.Available&XFILE_INFO_IDENTITY)){
        MdoApplicationError(E,"sdk_file_publish_unsupported","Filesystem cannot identify the pinned workspace root","file_publish",501,false);goto done;
    }
    if(!BuPublishRootMatches(RootRevision,&Directory)){
        MdoApplicationError(E,"sdk_workspace_conflict","Workspace root changed since selection; no file was published","file_publish",412,false);goto done;
    }
    if(xrtRootStat(Root,Target,false,&Destination)){
        MdoApplicationError(E,"sdk_file_exists","Destination already exists; it was not replaced","file_publish",409,false);goto done;
    }
    const xerror* Missing=xrtGetError();if(!Missing||xrtErrorKind(Missing)!=XERR_NOT_FOUND){SdkNativeFailure(E,"file_publish");goto done;}
    if(!xrtRootStat(Root,Source,false,&Named)){SdkNativeFailure(E,"file_publish");goto done;}
    if(Named.Type!=XFILE_TYPE_FILE){MdoApplicationError(E,"sdk_file_type","Publish source must be a regular staging file","file_publish",422,false);goto done;}
    xfileoptions Options;xrtFileOptionsInit(&Options);Options.Flags=XFILE_READ|XFILE_NOFOLLOW;
    /* Deny new writers during verification, allow XRT's anchored link handle. */
    Options.Share=XFILE_SHARE_READ|XFILE_SHARE_DELETE;
    File=xrtRootFileOpen(Root,Source,&Options);if(!File||!xrtFileStat(File,&Before)){SdkNativeFailure(E,"file_publish");goto done;}
    Metadata=SdkFileInfo(&Before);if(!Metadata)goto done;
    const cstr Fields[]={"size","type","device","identity","modified","revision"};
    for(size_t I=0;I<sizeof(Fields)/sizeof(*Fields);I++)if(!SdkGet(Metadata,Fields[I]))goto done;
    cstr Observed=SdkField(Metadata,"revision");if(!Observed)goto done;
    if(!BuPublishFileSame(&Named,&Before)||strcmp(Expected,Observed)||Before.Size!=Size){
        MdoApplicationError(E,"sdk_file_conflict","Staging file differs from its selected snapshot or size","file_publish",412,false);goto done;
    }
    Bytes=xrtMalloc(65536);if(!Bytes)goto done;
    xsha256 Sha;uint8 Digest[XRT_SHA256_SIZE];char Hex[65];xrtSha256Init(&Sha);
    for(;;){
        if(!BuFileContinue(C,E))goto done;
        size_t Read=0;if(!xrtRead(File,Bytes,65536,&Read)){SdkNativeFailure(E,"file_publish");goto done;}
        if(!Read)break;
        if(Read>Size-Total){MdoApplicationError(E,"sdk_file_conflict","Staging file grew during verification","file_publish",412,false);goto done;}
        Total+=Read;if(!xrtSha256Update(&Sha,Bytes,Read))goto done;
    }
    if(!xrtSha256Final(&Sha,Digest))goto done;
    static const char Digits[]="0123456789abcdef";
    for(size_t I=0;I<XRT_SHA256_SIZE;I++){Hex[I*2]=Digits[Digest[I]>>4];Hex[I*2+1]=Digits[Digest[I]&15];}Hex[64]=0;
    if(strcmp(Hash,Hex)){MdoApplicationError(E,"sdk_file_digest_conflict","Complete staging SHA-256 does not match; no file was published","file_publish",412,false);goto done;}
    if(!xrtFileStat(File,&After)||!xrtRootStat(Root,Source,false,&Current)){SdkNativeFailure(E,"file_publish");goto done;}
    if(Total!=Size||!BuPublishFileSame(&Before,&After)||!BuPublishFileSame(&Before,&Current)){
        MdoApplicationError(E,"sdk_file_conflict","Staging file changed during verification; no file was published","file_publish",412,false);goto done;
    }
    /* Allocate the entire receipt before the first possible publication. */
    if(!SdkPut(Metadata,"path",Target)||!SdkPut(Metadata,"source",Source)||!SdkPut(Metadata,"sha256",Hex)||
       !xrtValueObjectSetNew(Metadata,XRT_STR_LITERAL("source_retained"),xrtValueBool(true))||
       !xrtValueObjectSetNew(Metadata,XRT_STR_LITERAL("published"),xrtValueBool(true)))goto done;
    if(!BuFileContinue(C,E))goto done;
    Attempted=true;
    if(!xrtRootLinkHard(Root,Source,Target)){
        const xerror* Failure=xrtGetError();bool Unsupported=Failure&&xrtErrorKind(Failure)==XERR_UNSUPPORTED;
        SdkNativeFailure(E,"file_publish");
        if(Unsupported){MdoApplicationError(E,"sdk_file_publish_unsupported","Filesystem cannot publish an anchored hard link; staging bytes were retained","file_publish",501,false);Attempted=false;}
        goto done;
    }
    Published=xrtRootFileOpen(Root,Target,&Options);
    if(!Published||!xrtFileStat(Published,&Destination)||!xrtFileStat(File,&After)||!xrtRootStat(Root,Target,false,&Current)){
        SdkNativeFailure(E,"file_publish");goto done;
    }
    if(!BuPublishFileSame(&Before,&Destination)||!BuPublishFileSame(&Before,&After)||!BuPublishFileSame(&Before,&Current)){
        MdoApplicationError(E,"sdk_file_publish_uncertain","Published entry changed; retain both paths and inspect before retrying","file_publish",409,true);goto done;
    }
    Ok=true;
done:
    if(Published&&!xrtClose(Published)&&Ok){Ok=false;SdkNativeFailure(E,"file_publish");}
    if(File&&!xrtClose(File)&&Ok){Ok=false;SdkNativeFailure(E,"file_publish");}
    if(Root&&!xrtRootClose(Root)&&Ok){Ok=false;SdkNativeFailure(E,"file_publish");}
    if(Bytes){xrtSecureZero(Bytes,65536);xrtFree(Bytes);}
    if(Ok){*R=Metadata;Metadata=NULL;}
    if(!Ok){
        if(!E||!E->Code[0])MdoApplicationError(E,"sdk_file_publish_failed","Cannot publish the verified staging file","file_publish",503,Attempted);
        if(E&&Attempted)E->MayHaveCompleted=true;
    }
    xrtValueRelease(Metadata);return Ok;
}
