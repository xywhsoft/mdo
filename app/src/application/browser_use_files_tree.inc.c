/* Read-only, bounded transfer manifest. Keep every enumerated directory and
 * regular file pinned until the final identity checks. This detects observed
 * changes; it is not an atomic filesystem snapshot or a lease across calls. */
#define BU_TREE_FILES 128u
#define BU_TREE_DIRS 128u
#define BU_TREE_PATH 1024u
#define BU_TREE_NAME 240u
typedef struct BuTreeDirectory {
    xroot Root; xfileinfo Info; unsigned Parent,Depth;
    char Path[BU_TREE_PATH+1],Name[BU_TREE_NAME+1];
} BuTreeDirectory;
typedef struct BuTreeFile {
    xfile File; xfileinfo Info; unsigned Parent;
    char Path[BU_TREE_PATH+1],Name[BU_TREE_NAME+1],Hash[65];
} BuTreeFile;
typedef struct BuTree {
    BuTreeDirectory Directories[BU_TREE_DIRS+1]; BuTreeFile Files[BU_TREE_FILES];
    unsigned DirectoryCount,FileCount,MaxFiles,MaxDirectories,MaxDepth;
    uint64 Bytes,MaxBytes,MaxFileBytes; unsigned char Buffer[65536];
} BuTree;
static bool BuTreeConflict(mdo_error_v1* E){
    return MdoApplicationError(E,"sdk_tree_conflict","Directory contents or identity changed; request a fresh complete manifest","file_tree",412,false);
}
static bool BuTreeInfo(const xfileinfo* I){
    const uint32 Required=XFILE_INFO_IDENTITY|XFILE_INFO_SIZE|XFILE_INFO_MODIFY_TIME;
    return (I->Available&Required)==Required;
}
static bool BuTreeSame(const xfileinfo* A,const xfileinfo* B){
    return BuTreeInfo(A)&&BuTreeInfo(B)&&A->Type==B->Type&&A->Device==B->Device&&
        A->Identity==B->Identity&&A->Size==B->Size&&A->Modified==B->Modified&&
        (A->Available&XFILE_INFO_CHANGE_TIME)==(B->Available&XFILE_INFO_CHANGE_TIME)&&
        (!(A->Available&XFILE_INFO_CHANGE_TIME)||A->Changed==B->Changed);
}
static bool BuTreeName(xstrview Name){
    if(!Name.Data||!Name.Size||Name.Size>BU_TREE_NAME||!xrtUtf8Valid(Name,NULL))return false;
    if((Name.Size==1&&Name.Data[0]=='.')||(Name.Size==2&&!memcmp(Name.Data,"..",2)))return false;
    for(size_t I=0;I<Name.Size;I++){unsigned char C=(unsigned char)Name.Data[I];
        if(C<32||C==127||C=='/'||C=='\\'||C==':')return false;}
    return true;
}
static void BuTreeHex(const uint8 Digest[XRT_SHA256_SIZE],char Hex[65]){
    static const char Digits[]="0123456789abcdef";
    for(size_t I=0;I<XRT_SHA256_SIZE;I++){Hex[I*2]=Digits[Digest[I]>>4];Hex[I*2+1]=Digits[Digest[I]&15];}Hex[64]=0;
}
static bool BuTreeFileHash(const mdo_sdk_context_v1* C,BuTree* T,BuTreeFile* F,mdo_error_v1* E){
    xsha256 Sha;uint8 Digest[XRT_SHA256_SIZE];uint64 Total=0;xrtSha256Init(&Sha);
    for(;;){
        if(!BuFileContinue(C,E))return false;
        size_t Read=0;if(!xrtRead(F->File,T->Buffer,sizeof(T->Buffer),&Read))return SdkNativeFailure(E,"file_tree");
        if(!Read)break;
        if(Read>F->Info.Size-Total)return BuTreeConflict(E);
        Total+=Read;if(!xrtSha256Update(&Sha,T->Buffer,Read))return false;
    }
    if(Total!=F->Info.Size)return BuTreeConflict(E);
    if(!xrtSha256Final(&Sha,Digest))return false;BuTreeHex(Digest,F->Hash);return true;
}
static bool BuTreeUnique(const BuTree* T,unsigned Parent,cstr Name){
    for(unsigned I=1;I<T->DirectoryCount;I++)if(T->Directories[I].Parent==Parent&&!strcmp(T->Directories[I].Name,Name))return false;
    for(unsigned I=0;I<T->FileCount;I++)if(T->Files[I].Parent==Parent&&!strcmp(T->Files[I].Name,Name))return false;
    return true;
}
static bool BuTreeScan(const mdo_sdk_context_v1* C,BuTree* T,mdo_error_v1* E){
    for(unsigned At=0;At<T->DirectoryCount;At++){
        BuTreeDirectory* Parent=&T->Directories[At];xdir Dir=xrtRootDirOpen(Parent->Root,".",0);
        if(!Dir)return SdkNativeFailure(E,"file_tree");
        xdirentry Entry;xdirnext Next=XDIR_NEXT_END;bool Ok=true;
        while((Next=xrtDirNext(Dir,&Entry))==XDIR_NEXT_ITEM){
            if(!BuFileContinue(C,E)){Ok=false;break;}
            if(!(Entry.Flags&XDIR_ENTRY_UTF8)||!BuTreeName(Entry.Name)){
                Ok=MdoApplicationError(E,"sdk_tree_path","Tree entries need UTF-8 names up to 240 bytes without controls, separators or colons","file_tree",422,false);break;
            }
            char Name[BU_TREE_NAME+1],Path[BU_TREE_PATH+1];memcpy(Name,Entry.Name.Data,Entry.Name.Size);Name[Entry.Name.Size]=0;
            int Length=snprintf(Path,sizeof(Path),"%s%s%s",Parent->Path,Parent->Path[0]?"/":"",Name);
            if(Length<0||(size_t)Length>=sizeof(Path)){
                Ok=MdoApplicationError(E,"sdk_tree_limit","Tree relative path exceeds 1024 UTF-8 bytes","file_tree",413,false);break;
            }
            if(!BuTreeUnique(T,At,Name)){Ok=BuTreeConflict(E);break;}
            xfileinfo Named={0},Opened={0};
            if(!xrtRootStat(Parent->Root,Name,false,&Named)){Ok=SdkNativeFailure(E,"file_tree");break;}
            if(!BuTreeInfo(&Named)){
                Ok=MdoApplicationError(E,"sdk_tree_unsupported","Filesystem lacks stable identity, size or modification metadata","file_tree",501,false);break;
            }
            if(Named.Type==XFILE_TYPE_DIRECTORY){
                if(T->DirectoryCount-1>=T->MaxDirectories||Parent->Depth>=T->MaxDepth){
                    Ok=MdoApplicationError(E,"sdk_tree_limit","Tree exceeds its directory count or depth budget","file_tree",413,false);break;
                }
                BuTreeDirectory* Child=&T->Directories[T->DirectoryCount++];Child->Parent=At;Child->Depth=Parent->Depth+1;
                memcpy(Child->Path,Path,(size_t)Length+1);memcpy(Child->Name,Name,Entry.Name.Size+1);Child->Info=Named;
                Child->Root=xrtRootOpenIn(Parent->Root,Name);
                if(!Child->Root||!xrtRootStat(Child->Root,".",false,&Opened)){Ok=SdkNativeFailure(E,"file_tree");break;}
                if(!BuTreeSame(&Named,&Opened)){Ok=BuTreeConflict(E);break;}
            }else if(Named.Type==XFILE_TYPE_FILE){
                if(T->FileCount>=T->MaxFiles||Named.Size>T->MaxFileBytes||Named.Size>T->MaxBytes-T->Bytes){
                    Ok=MdoApplicationError(E,"sdk_tree_limit","Tree exceeds its file count, file size or total byte budget","file_tree",413,false);break;
                }
                BuTreeFile* File=&T->Files[T->FileCount++];File->Parent=At;File->Info=Named;
                memcpy(File->Path,Path,(size_t)Length+1);memcpy(File->Name,Name,Entry.Name.Size+1);
                xfileoptions Options;xrtFileOptionsInit(&Options);Options.Flags=XFILE_READ|XFILE_NOFOLLOW;Options.Share=XFILE_SHARE_READ;
                File->File=xrtRootFileOpen(Parent->Root,Name,&Options);
                if(!File->File||!xrtFileStat(File->File,&Opened)){Ok=SdkNativeFailure(E,"file_tree");break;}
                if(!BuTreeSame(&Named,&Opened)){Ok=BuTreeConflict(E);break;}
                if(!BuTreeFileHash(C,T,File,E)){Ok=false;break;}T->Bytes+=Named.Size;
            }else{Ok=MdoApplicationError(E,"sdk_tree_type","Tree contains a link, reparse point or special entry; no entries were omitted","file_tree",422,false);break;}
        }
        if(Next==XDIR_NEXT_ERROR&&Ok)Ok=SdkNativeFailure(E,"file_tree");
        if(!xrtDirClose(Dir)&&Ok)Ok=SdkNativeFailure(E,"file_tree");
        if(!Ok)return false;
    }return true;
}
static bool BuTreeVerify(const mdo_sdk_context_v1* C,const BuTree* T,cstr Path,mdo_error_v1* E){
    xfileinfo Current={0};
    if(!BuFileContinue(C,E))return false;
    if(!xrtPathStat(Path,false,&Current))return SdkNativeFailure(E,"file_tree");
    if(!BuTreeSame(&T->Directories[0].Info,&Current))return BuTreeConflict(E);
    for(unsigned I=0;I<T->DirectoryCount;I++){
        const BuTreeDirectory* D=&T->Directories[I];if(!BuFileContinue(C,E))return false;
        if(!xrtRootStat(D->Root,".",false,&Current))return SdkNativeFailure(E,"file_tree");
        if(!BuTreeSame(&D->Info,&Current))return BuTreeConflict(E);
        if(I){
            if(!xrtRootStat(T->Directories[D->Parent].Root,D->Name,false,&Current))return SdkNativeFailure(E,"file_tree");
            if(!BuTreeSame(&D->Info,&Current))return BuTreeConflict(E);
        }
    }
    for(unsigned I=0;I<T->FileCount;I++){
        const BuTreeFile* F=&T->Files[I];if(!BuFileContinue(C,E))return false;
        if(!xrtFileStat(F->File,&Current))return SdkNativeFailure(E,"file_tree");
        if(!BuTreeSame(&F->Info,&Current))return BuTreeConflict(E);
        if(!xrtRootStat(T->Directories[F->Parent].Root,F->Name,false,&Current))return SdkNativeFailure(E,"file_tree");
        if(!BuTreeSame(&F->Info,&Current))return BuTreeConflict(E);
    }return BuFileContinue(C,E);
}
static int BuTreeDirectoryCompare(const void* A,const void* B){return strcmp((*(const BuTreeDirectory*const*)A)->Path,(*(const BuTreeDirectory*const*)B)->Path);}
static int BuTreeFileCompare(const void* A,const void* B){return strcmp((*(const BuTreeFile*const*)A)->Path,(*(const BuTreeFile*const*)B)->Path);}
static bool BuTreeHashField(xsha256* Sha,cstr Text){
    char Length[32];size_t N=strlen(Text);int Count=snprintf(Length,sizeof(Length),"%llu:",(unsigned long long)N);
    return Count>0&&xrtSha256Update(Sha,Length,(size_t)Count)&&xrtSha256Update(Sha,Text,N);
}
static xvalue* BuTreeMetadata(xsha256* Sha,const xfileinfo* I,cstr Path,cstr Hash){
    char Fields[224],Revision[160];
    snprintf(Fields,sizeof(Fields),"%u:%llu:%llu:%llu:%lld:%u:%lld",I->Type,(unsigned long long)I->Device,
        (unsigned long long)I->Identity,(unsigned long long)I->Size,(long long)I->Modified,
        (unsigned)(I->Available&XFILE_INFO_CHANGE_TIME),(long long)((I->Available&XFILE_INFO_CHANGE_TIME)?I->Changed:0));
    snprintf(Revision,sizeof(Revision),"f-%llu-%llu-%llu-%lld",(unsigned long long)I->Device,
        (unsigned long long)I->Identity,(unsigned long long)I->Size,(long long)I->Modified);
    if(!BuTreeHashField(Sha,Path)||!BuTreeHashField(Sha,Fields)||!BuTreeHashField(Sha,Hash?Hash:""))return NULL;
    xvalue* R=xrtValueObject();
    if(!R||!SdkPut(R,"path",Path)||!SdkPut(R,"revision",Revision)||!SdkPutUInt(R,"type",I->Type)||
       !SdkPutUInt(R,"size",I->Size)||!SdkPutUInt(R,"device",I->Device)||!SdkPutUInt(R,"identity",I->Identity)||
       !xrtValueObjectSetNew(R,XRT_STR_LITERAL("modified"),xrtValueInt(I->Modified))||
       ((I->Available&XFILE_INFO_CHANGE_TIME)&&!xrtValueObjectSetNew(R,XRT_STR_LITERAL("changed"),xrtValueInt(I->Changed)))||
       (Hash&&!SdkPut(R,"sha256",Hash))){xrtValueRelease(R);return NULL;}return R;
}
static xvalue* BuTreeResult(const BuTree* T){
    const BuTreeDirectory* Directories[BU_TREE_DIRS+1];const BuTreeFile* Files[BU_TREE_FILES];
    for(unsigned I=0;I<T->DirectoryCount;I++)Directories[I]=&T->Directories[I];
    for(unsigned I=0;I<T->FileCount;I++)Files[I]=&T->Files[I];
    qsort(Directories,T->DirectoryCount,sizeof(*Directories),BuTreeDirectoryCompare);qsort(Files,T->FileCount,sizeof(*Files),BuTreeFileCompare);
    xvalue *R=xrtValueObject(),*D=xrtValueArray(),*F=xrtValueArray();xsha256 Sha;uint8 Digest[XRT_SHA256_SIZE];char Revision[68];
    xrtSha256Init(&Sha);bool Ok=R&&D&&F&&BuTreeHashField(&Sha,"mdo.file-tree.v1");
    if(Ok)Ok=xrtValueObjectSetNew(R,XRT_STR_LITERAL("root"),BuTreeMetadata(&Sha,&T->Directories[0].Info,".",NULL));
    for(unsigned I=1;Ok&&I<T->DirectoryCount;I++)Ok=xrtValueArrayAppendNew(D,BuTreeMetadata(&Sha,&Directories[I]->Info,Directories[I]->Path,NULL));
    if(Ok)Ok=BuTreeHashField(&Sha,"files");
    for(unsigned I=0;Ok&&I<T->FileCount;I++)Ok=xrtValueArrayAppendNew(F,BuTreeMetadata(&Sha,&Files[I]->Info,Files[I]->Path,Files[I]->Hash));
    if(Ok)Ok=xrtSha256Final(&Sha,Digest);
    if(Ok){memcpy(Revision,"t1-",3);BuTreeHex(Digest,Revision+3);
        Ok=SdkPut(R,"format","mdo.file-tree.v1")&&SdkPut(R,"revision",Revision)&&SdkPutUInt(R,"file_count",T->FileCount)&&
           SdkPutUInt(R,"directory_count",T->DirectoryCount-1)&&SdkPutUInt(R,"total_bytes",T->Bytes)&&
           xrtValueObjectSet(R,XRT_STR_LITERAL("directories"),D)&&xrtValueObjectSet(R,XRT_STR_LITERAL("files"),F);
    }
    xrtValueRelease(D);xrtValueRelease(F);if(!Ok){xrtValueRelease(R);R=NULL;}return R;
}
static bool BuTreeBudget(const xvalue* P,cstr Key,uint64 Default,uint64 Min,uint64 Max,uint64* Out){
    const xvalue* V=SdkGet(P,Key);*Out=Default;return (!V||SdkUInt(V,Out))&&*Out>=Min&&*Out<=Max;
}
static bool BuFileTree(const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E){
    cstr Name=SdkField(P,"path"),Expected=SdkField(P,"revision");uint64 Files,Directories,Depth,Bytes,FileBytes;
    if(!Name||!Name[0]||strlen(Name)>4096||
       !BuTreeBudget(P,"max_files",128,1,128,&Files)||!BuTreeBudget(P,"max_directories",128,0,128,&Directories)||
       !BuTreeBudget(P,"max_depth",16,0,16,&Depth)||!BuTreeBudget(P,"max_bytes",67108864,1,67108864,&Bytes)||
       !BuTreeBudget(P,"max_file_bytes",33554432,1,33554432,&FileBytes))
        return MdoApplicationError(E,"sdk_arguments_invalid","Select a directory path and bounded tree budgets","file_tree",422,false);
    if(SdkGet(P,"revision")){
        if(!Expected||strlen(Expected)!=67||strncmp(Expected,"t1-",3))goto invalid_revision;
        for(size_t I=3;I<67;I++)if(!((Expected[I]>='0'&&Expected[I]<='9')||(Expected[I]>='a'&&Expected[I]<='f')))goto invalid_revision;
    }
    char* Path=SdkNativePath(C,P,false,E);
    if(!Path){
        if(!E||!E->Code[0])MdoApplicationError(E,"sdk_tree_failed","Cannot allocate the selected directory path","file_tree",503,false);
        return false;
    }
    char* Native=NULL;if(!BuFileSnapshotPath(Path,&Native,E)){xrtFree(Path);return false;}if(Native){xrtFree(Path);Path=Native;}
    BuTree* T=xrtCalloc(1,sizeof(*T));xvalue* Result=NULL;bool Ok=false;xfileinfo Named={0},Opened={0};
    if(!T)goto done;T->MaxFiles=(unsigned)Files;T->MaxDirectories=(unsigned)Directories;T->MaxDepth=(unsigned)Depth;T->MaxBytes=Bytes;T->MaxFileBytes=FileBytes;
    if(!BuFileContinue(C,E))goto done;
    if(!xrtPathStat(Path,false,&Named)){SdkNativeFailure(E,"file_tree");goto done;}
    if(Named.Type!=XFILE_TYPE_DIRECTORY){MdoApplicationError(E,"sdk_tree_type","Tree snapshots require a real directory, not a file or link","file_tree",422,false);goto done;}
    if(!BuTreeInfo(&Named)){MdoApplicationError(E,"sdk_tree_unsupported","Filesystem lacks stable directory metadata","file_tree",501,false);goto done;}
    T->DirectoryCount=1;T->Directories[0].Info=Named;T->Directories[0].Root=xrtRootOpen(Path);
    if(!T->Directories[0].Root||!xrtRootStat(T->Directories[0].Root,".",false,&Opened)){SdkNativeFailure(E,"file_tree");goto done;}
    if(!BuTreeSame(&Named,&Opened)){BuTreeConflict(E);goto done;}
    if(!BuTreeScan(C,T,E)||!BuTreeVerify(C,T,Path,E))goto done;
    Result=BuTreeResult(T);if(!Result)goto done;
    if(Expected&&strcmp(Expected,SdkField(Result,"revision"))){BuTreeConflict(E);goto done;}
    Ok=BuFileContinue(C,E);
done:
    if(T){
        for(unsigned I=0;I<T->FileCount;I++)if(T->Files[I].File&&!xrtClose(T->Files[I].File)&&Ok){Ok=false;SdkNativeFailure(E,"file_tree");}
        for(unsigned I=T->DirectoryCount;I>0;I--)if(T->Directories[I-1].Root&&!xrtRootClose(T->Directories[I-1].Root)&&Ok){Ok=false;SdkNativeFailure(E,"file_tree");}
        xrtSecureZero(T->Buffer,sizeof(T->Buffer));xrtFree(T);
    }
    xrtFree(Path);if(Ok){*R=Result;return true;}xrtValueRelease(Result);
    if(!E||!E->Code[0])MdoApplicationError(E,"sdk_tree_failed","Cannot produce the complete bounded directory manifest","file_tree",503,false);return false;
invalid_revision:
    return MdoApplicationError(E,"sdk_arguments_invalid","Tree revision must be t1- followed by 64 lowercase hexadecimal digits","file_tree",422,false);
}
