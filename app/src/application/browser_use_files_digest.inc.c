/* Transfer snapshots are read-only and bounded. Never return file contents or
 * accept a digest of a prefix as the digest of the whole file. */
static bool BuFileSnapshotPath(cstr Path,char** Owned,mdo_error_v1* E){
    *Owned=NULL;
#if defined(_WIN32) || defined(_WIN64)
    /* Resolve ordinary long paths before adding the Windows namespace: extended
     * paths do not interpret '/' or dot segments. Keep already explicit paths
     * and short-path behavior unchanged. No namespace path is returned on wire. */
    if(strlen(Path)<248||!strncmp(Path,"\\\\?\\",4))return true;
    if(!strncmp(Path,"\\\\.\\",4)||!strncmp(Path,"\\??\\",4))
        return MdoApplicationError(E,"sdk_arguments_invalid","Snapshot paths must name ordinary files or directories","filesystem",422,false);
    char* Absolute=xrtPathAbs(Path);if(!Absolute)return SdkNativeFailure(E,"filesystem");
    for(char* At=Absolute;*At;At++)if(*At=='/')*At='\\';
    if(((Absolute[0]>='a'&&Absolute[0]<='z')||(Absolute[0]>='A'&&Absolute[0]<='Z'))&&Absolute[1]==':'&&Absolute[2]=='\\')
        *Owned=xrtFormat("\\\\?\\%s",Absolute);
    else if(Absolute[0]=='\\'&&Absolute[1]=='\\'&&Absolute[2]!='?'&&Absolute[2]!='.')
        *Owned=xrtFormat("\\\\?\\UNC\\%s",Absolute+2);
    xrtFree(Absolute);
    if(!*Owned)return MdoApplicationError(E,"sdk_file_path_failed","Cannot prepare the selected snapshot path","filesystem",503,false);
#else
    (void)Path;(void)E;
#endif
    return true;
}
static bool BuFileSame(const xfileinfo* A,const xfileinfo* B){
    return A->Type==XFILE_TYPE_FILE&&B->Type==XFILE_TYPE_FILE&&A->Device==B->Device&&
        A->Identity==B->Identity&&A->Size==B->Size&&A->Modified==B->Modified;
}
static bool BuFileContinue(const mdo_sdk_context_v1* C,mdo_error_v1* E){
    if(!SdkCancelled(C))return true;
    MdoApplicationInvocation* Invocation=C->Opaque;
    bool Cancelled=Invocation&&Invocation->Cancel&&xrtCancelRequested(Invocation->Cancel);
    return MdoApplicationError(E,Cancelled?"sdk_cancelled":"sdk_deadline_expired",
        Cancelled?"File snapshot cancelled":"File snapshot deadline expired","filesystem",Cancelled?499:504,false);
}
static bool BuFileRead(const mdo_sdk_context_v1* C,const xvalue* P,cstr Path,xvalue** R,mdo_error_v1* E){
    uint64 Offset=0,Limit=16384;const xvalue* O=SdkGet(P,"offset"),*L=SdkGet(P,"limit");
    cstr Expected=SdkField(P,"revision");
    if(!Expected||!Expected[0]||strlen(Expected)>160||(O&&!SdkUInt(O,&Offset))||
       (L&&!SdkUInt(L,&Limit))||!Limit||Limit>262144)
        return MdoApplicationError(E,"sdk_arguments_invalid","Read needs a nonempty revision and bounded byte range","filesystem",422,false);
    xfileinfo Named={0},Before={0},After={0},Current={0};xvalue* Metadata=NULL;
    xfile File=NULL;unsigned char* Bytes=NULL;size_t Read=0;bool Ok=false;
    char* Native=NULL;if(!BuFileSnapshotPath(Path,&Native,E))return false;if(Native)Path=Native;
    if(!xrtPathStat(Path,false,&Named)){SdkNativeFailure(E,"filesystem");goto done;}
    if(Named.Type!=XFILE_TYPE_FILE){MdoApplicationError(E,"sdk_file_type","Revision reads require a regular file","filesystem",422,false);goto done;}
    xfileoptions Options;xrtFileOptionsInit(&Options);Options.Flags=XFILE_READ|XFILE_NOFOLLOW;Options.Share=XFILE_SHARE_READ;
    File=xrtFileOpen(Path,&Options);if(!File||!xrtFileStat(File,&Before)){SdkNativeFailure(E,"filesystem");goto done;}
    Metadata=SdkFileInfo(&Before);if(!Metadata)goto done;
    if(!BuFileSame(&Named,&Before)||strcmp(Expected,SdkField(Metadata,"revision"))){
        MdoApplicationError(E,"sdk_file_conflict","File changed since its snapshot; no bytes were returned","filesystem",412,false);goto done;
    }
    if(Offset>Before.Size){MdoApplicationError(E,"sdk_file_range","Offset exceeds the selected file size","filesystem",416,false);goto done;}
    if(!BuFileContinue(C,E))goto done;
    Bytes=xrtMalloc((size_t)Limit);if(!Bytes)goto done;
    if(!xrtReadAt(File,Offset,Bytes,(size_t)Limit,&Read)||!xrtFileStat(File,&After)||!xrtPathStat(Path,false,&Current)){
        SdkNativeFailure(E,"filesystem");goto done;
    }
    if(!BuFileSame(&Before,&After)||!BuFileSame(&Before,&Current)||Read!=((Before.Size-Offset)<Limit?(size_t)(Before.Size-Offset):(size_t)Limit)){
        MdoApplicationError(E,"sdk_file_conflict","File changed during reading; no bytes were returned","filesystem",412,false);goto done;
    }
    if(!BuFileContinue(C,E))goto done;
    if(!xrtClose(File)){File=NULL;SdkNativeFailure(E,"filesystem");goto done;}File=NULL;
    Ok=SdkByteValue(Metadata,Bytes,Read)&&SdkPutUInt(Metadata,"next",Offset+Read);
    if(Ok){*R=Metadata;Metadata=NULL;}
done:
    if(File&&!xrtClose(File)&&(!E||!E->Code[0]))SdkNativeFailure(E,"filesystem");
    if(Bytes){xrtSecureZero(Bytes,(size_t)Limit);xrtFree(Bytes);}xrtFree(Native);xrtValueRelease(Metadata);
    if(!Ok&&(!E||!E->Code[0]))MdoApplicationError(E,"sdk_file_read_failed","Cannot read the selected bounded file snapshot","filesystem",503,false);
    return Ok;
}
static bool BuFileDigest(const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E){
    uint64 Limit=32u*1024u*1024u,Total=0;const xvalue* Max=SdkGet(P,"max_bytes");
    cstr Expected=SdkField(P,"revision"),Name=SdkField(P,"path");
    if(!Name||!Name[0]||strlen(Name)>4096)return MdoApplicationError(E,"sdk_arguments_invalid","Select a nonempty file path up to 4096 bytes","filesystem",422,false);
    if((Max&&(!SdkUInt(Max,&Limit)||!Limit||Limit>64u*1024u*1024u))||
       (SdkGet(P,"revision")&&(!Expected||!Expected[0]||strlen(Expected)>160)))
        return MdoApplicationError(E,"sdk_arguments_invalid","Digest needs a budget from 1 through 67108864 bytes and an optional observed revision","filesystem",422,false);
    char* Path=SdkNativePath(C,P,false,E);if(!Path)return false;
    char* Native=NULL;if(!BuFileSnapshotPath(Path,&Native,E)){xrtFree(Path);return false;}if(Native){xrtFree(Path);Path=Native;}
    xfileinfo Named={0},Before={0},After={0},Current={0};xfile File=NULL;unsigned char* Bytes=NULL;
    xvalue* Metadata=NULL;bool Ok=false;
    if(!xrtPathStat(Path,false,&Named)){SdkNativeFailure(E,"filesystem");goto done;}
    if(Named.Type!=XFILE_TYPE_FILE){MdoApplicationError(E,"sdk_file_type","Digest requires a regular file, not a directory or link","filesystem",422,false);goto done;}
    xfileoptions Options;xrtFileOptionsInit(&Options);Options.Flags=XFILE_READ|XFILE_NOFOLLOW;Options.Share=XFILE_SHARE_READ;
    File=xrtFileOpen(Path,&Options);if(!File||!xrtFileStat(File,&Before)){SdkNativeFailure(E,"filesystem");goto done;}
    if(!BuFileSame(&Named,&Before)){MdoApplicationError(E,"sdk_file_conflict","File identity changed while opening the snapshot","filesystem",412,false);goto done;}
    Metadata=SdkFileInfo(&Before);if(!Metadata)goto done;
    if(Expected&&strcmp(Expected,SdkField(Metadata,"revision"))){MdoApplicationError(E,"sdk_file_conflict","File changed since it was selected","filesystem",412,false);goto done;}
    if(Before.Size>Limit){MdoApplicationError(E,"sdk_file_limit","File exceeds its digest byte budget","filesystem",413,false);goto done;}
    Bytes=xrtMalloc(65536);if(!Bytes)goto done;
    xsha256 Sha;uint8 Digest[XRT_SHA256_SIZE];char Hex[65];xrtSha256Init(&Sha);
    for(;;){
        if(!BuFileContinue(C,E))goto done;
        size_t Read=0;
        if(!xrtRead(File,Bytes,65536,&Read)){SdkNativeFailure(E,"filesystem");goto done;}
        if(!Read)break;
        if(Read>Limit-Total){MdoApplicationError(E,"sdk_file_conflict","File grew beyond its selected snapshot or byte budget","filesystem",412,false);goto done;}
        Total+=Read;if(!xrtSha256Update(&Sha,Bytes,Read))goto done;
    }
    if(!xrtFileStat(File,&After)||!xrtPathStat(Path,false,&Current)){SdkNativeFailure(E,"filesystem");goto done;}
    if(!BuFileSame(&Before,&After)||!BuFileSame(&Before,&Current)||Total!=Before.Size){
        MdoApplicationError(E,"sdk_file_conflict","File changed during hashing; select a fresh snapshot","filesystem",412,false);goto done;
    }
    if(!xrtSha256Final(&Sha,Digest))goto done;
    static const char Digits[]="0123456789abcdef";
    for(size_t I=0;I<XRT_SHA256_SIZE;I++){Hex[I*2]=Digits[Digest[I]>>4];Hex[I*2+1]=Digits[Digest[I]&15];}Hex[64]=0;
    if(!xrtClose(File)){File=NULL;SdkNativeFailure(E,"filesystem");goto done;}File=NULL;
    Ok=SdkPut(Metadata,"sha256",Hex);if(Ok){*R=Metadata;Metadata=NULL;}
done:
    if(File&&!xrtClose(File)&&(!E||!E->Code[0]))SdkNativeFailure(E,"filesystem");
    if(Bytes){xrtSecureZero(Bytes,65536);xrtFree(Bytes);}xrtFree(Path);xrtValueRelease(Metadata);
    if(!Ok&&(!E||!E->Code[0]))MdoApplicationError(E,"sdk_file_digest_failed","Cannot compute the complete bounded file snapshot","filesystem",503,false);
    return Ok;
}
