/* File workbench services reuse the filesystem path and write contracts.
 * Traversal is bounded in time, entries and queued directories; it never
 * follows directory symlinks or interprets file contents. */
static bool SdkFileContains(cstr Text,cstr Query){
    for(size_t i=0;Text[i];i++){
        size_t j=0;while(Query[j]&&Text[i+j]){
            unsigned char A=(unsigned char)Text[i+j],B=(unsigned char)Query[j];
            if(A>='A'&&A<='Z')A=(unsigned char)(A+'a'-'A');
            if(B>='A'&&B<='Z')B=(unsigned char)(B+'a'-'A');
            if(A!=B)break;j++;
        }if(!Query[j])return true;
    }return !Query[0];
}
static bool SdkFileSearch(const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E){
    cstr Query=SdkField(P,"query");uint64 Limit=100;bool Hidden=false;
    if(!Query||!Query[0]||strlen(Query)>256||(SdkGet(P,"limit")&&(!SdkUInt(SdkGet(P,"limit"),&Limit)||!Limit||Limit>200)))
        return MdoApplicationError(E,"sdk_arguments_invalid","Search needs 1-256 query bytes and a limit between 1 and 200","filesystem",400,false);
    char* Root=SdkNativePath(C,P,false,E);if(!Root)return false;
    xfileinfo Info={0};if(!xrtPathStat(Root,false,&Info)){xrtFree(Root);return SdkNativeFailure(E,"filesystem");}
    if(Info.Type!=XFILE_TYPE_DIRECTORY){xrtFree(Root);return MdoApplicationError(E,"sdk_arguments_invalid","Search root must be a directory","filesystem",400,false);}
    xrtValueGetBool(SdkGet(P,"hidden"),&Hidden);
    xvalue* Queue=xrtValueArray(),*Items=xrtValueArray();xrtValueArrayAppendNew(Queue,SdkString(""));
    uint64 Deadline=MdoDeadlineAfterUs(750000),Scanned=0,Skipped=0;bool Incomplete=false,Ok=true;
    for(size_t At=0;At<xrtValueCount(Queue);At++){
        char* Relative=xrtStrDup(SdkText(xrtValueArrayGet(Queue,At)));
        char* Directory=Relative?xrtPathJoin(Root,Relative):NULL;
        if(!Relative||!Directory){xrtFree(Relative);xrtFree(Directory);Ok=false;break;}
        xdir Dir=Directory?xrtDirOpen(Directory,XDIR_STAT):NULL;xrtFree(Directory);
        if(!Dir){Skipped++;xrtFree(Relative);continue;}
        xdirentry Entry;xdirnext Next;
        while((Next=xrtDirNext(Dir,&Entry))==XDIR_NEXT_ITEM){
            if(SdkCancelled(C)){Ok=MdoApplicationError(E,"sdk_cancelled","File search cancelled","filesystem",499,false);break;}
            if(++Scanned>10000||MdoDeadlineExpired(Deadline)){Incomplete=true;break;}
            char* Name=xrtStrDupN(Entry.Name.Data,Entry.Name.Size);
            if(!Name){Ok=false;break;}
            if(!strcmp(Name,".")||!strcmp(Name,"..")||!strcmp(Name,".git")||(!Hidden&&Name[0]=='.')){xrtFree(Name);continue;}
            char* Child=Relative[0]?xrtFormat("%s/%s",Relative,Name):xrtStrDup(Name);xrtFree(Name);
            if(!Child){Ok=false;break;}
            if(strlen(Child)>8192){xrtFree(Child);Incomplete=true;continue;}
            if(SdkFileContains(Child,Query)){
                xvalue* Value=SdkFileInfo(&Entry.Info);SdkPut(Value,"path",Child);xrtValueArrayAppendNew(Items,Value);
            }
            if(Entry.Info.Type==XFILE_TYPE_DIRECTORY){
                if(xrtValueCount(Queue)<1024)xrtValueArrayAppendNew(Queue,SdkString(Child));else Incomplete=true;
            }
            xrtFree(Child);if(xrtValueCount(Items)>=Limit){Incomplete=true;break;}
        }
        if(Next==XDIR_NEXT_ERROR)Skipped++;
        if(!xrtDirClose(Dir))Skipped++;
        xrtFree(Relative);
        if(!Ok||Scanned>10000||MdoDeadlineExpired(Deadline)||xrtValueCount(Items)>=Limit)break;
    }
    if(Ok){
        *R=xrtValueObject();SdkPut(*R,"root",Root);SdkPutUInt(*R,"scanned",Scanned);SdkPutUInt(*R,"skipped",Skipped);
        xrtValueObjectSetNew(*R,XRT_STR_LITERAL("incomplete"),xrtValueBool(Incomplete||Skipped>0));
        xrtValueObjectSet(*R,XRT_STR_LITERAL("items"),Items);
    }
    xrtFree(Root);xrtValueRelease(Queue);xrtValueRelease(Items);return Ok;
}
static bool SdkFilesCall(const mdo_sdk_context_v1* C,cstr Method,const xvalue* P,xvalue** R,mdo_error_v1* E){
    if(!strcmp(Method,"files.inspect")){
        const xvalue* Paths=SdkGet(P,"paths");cstr Space=SdkField(P,"space");
        if(Space&&strcmp(Space,"workspace")&&strcmp(Space,"native")&&strcmp(Space,"home")&&strcmp(Space,"application"))
            return MdoApplicationError(E,"sdk_arguments_invalid","Inspect needs a supported resource space","filesystem",400,false);
        if(xrtValueType(Paths)!=XVALUE_ARRAY||!xrtValueCount(Paths)||xrtValueCount(Paths)>64)
            return MdoApplicationError(E,"sdk_arguments_invalid","Inspect needs 1 to 64 file paths","filesystem",400,false);
        for(size_t i=0;i<xrtValueCount(Paths);i++){cstr Path=SdkText(xrtValueArrayGet(Paths,i));if(!Path||!Path[0]||strlen(Path)>4096)return MdoApplicationError(E,"sdk_arguments_invalid","Inspect paths must be nonempty strings up to 4096 bytes","filesystem",400,false);}
        *R=xrtValueArray();
        for(size_t i=0;i<xrtValueCount(Paths);i++){
            if(SdkCancelled(C)){xrtValueRelease(*R);*R=NULL;return MdoApplicationError(E,"sdk_cancelled","File inspection cancelled","filesystem",499,false);}
            cstr Path=SdkText(xrtValueArrayGet(Paths,i));xvalue* Q=xrtValueObject(),*Entry=NULL;mdo_error_v1 Error={0};SdkPut(Q,"path",Path);if(Space)SdkPut(Q,"space",Space);
            if(!SdkFileCall(C,"fs.stat",Q,&Entry,&Error)){xrtValueRelease(Entry);Entry=xrtValueObject();SdkPut(Entry,"error",Error.Message);SdkPut(Entry,"code",Error.Code);SdkPutUInt(Entry,"status",Error.Status);}
            SdkPut(Entry,"path",Path);xrtValueArrayAppendNew(*R,Entry);xrtValueRelease(Q);
        }return true;
    }
    bool Prepare=false;
    if(!strcmp(Method,"files.resolve")&&xrtValueGetBool(SdkGet(P,"prepare_default"),&Prepare)&&Prepare){
        cstr Name=SdkField(P,"path"),Space=SdkField(P,"space");
        if(!C->ProjectId||strcmp(C->ProjectId,"default")||!Name||strcmp(Name,".")||(Space&&strcmp(Space,"workspace")&&strcmp(Space,"native")))
            return MdoApplicationError(E,"sdk_arguments_invalid","Only the default workspace root may be prepared","filesystem",400,false);
        char* Expected=MdoHomeDefaultWorkspacePath(false);bool Match=Expected&&(!C->Workspace||!C->Workspace[0]||!strcmp(Expected,C->Workspace));xrtFree(Expected);
        if(!Match)return MdoApplicationError(E,"sdk_project_changed","The selected workspace is not the current default workspace","filesystem",409,false);
        xwork_error Error;MdoProjectLease* Lease=MdoProjectLeaseAcquire("default",MDO_PROJECT_LEASE_SHARED,&Error);
        if(!Lease)return MdoApplicationError(E,"sdk_project_busy",Error.sMessage,"filesystem",409,false);
        char* Directory=MdoHomeDefaultWorkspacePath(true);MdoProjectLeaseRelease(Lease);
        if(!Directory)return SdkNativeFailure(E,"filesystem");*R=xrtValueObject();bool Ok=SdkPut(*R,"path",Directory);xrtFree(Directory);return Ok;
    }
    if(!strcmp(Method,"files.search"))return SdkFileSearch(C,P,R,E);
    if(strcmp(Method,"files.directory")){
        char Name[48];snprintf(Name,sizeof(Name),"fs.%s",Method+6);return SdkFileCall(C,Name,P,R,E);
    }
    char* Path=SdkNativePath(C,P,false,E);if(!Path)return false;
    xfileinfo Before={0},After={0};xvalue *Start=NULL,*End=NULL;bool Ok=false;
    if(!xrtPathStat(Path,false,&Before)){SdkNativeFailure(E,"filesystem");goto done;}
    if(Before.Type!=XFILE_TYPE_DIRECTORY){MdoApplicationError(E,"sdk_arguments_invalid","Directory page needs a directory path","filesystem",400,false);goto done;}
    Start=SdkFileInfo(&Before);cstr Expected=SdkField(P,"revision");
    if(Expected&&strcmp(Expected,SdkField(Start,"revision"))){MdoApplicationError(E,"sdk_directory_changed","Directory changed; restart pagination","filesystem",409,false);goto done;}
    if(!SdkFileCall(C,"fs.list",P,R,E))goto done;
    if(!xrtPathStat(Path,false,&After)){SdkNativeFailure(E,"filesystem");goto done;}
    End=SdkFileInfo(&After);
    if(strcmp(SdkField(Start,"revision"),SdkField(End,"revision"))){MdoApplicationError(E,"sdk_directory_changed","Directory changed while reading; refresh this directory","filesystem",409,false);goto done;}
    bool Hidden=false;xrtValueGetBool(SdkGet(P,"hidden"),&Hidden);
    xvalue* Items=xrtValueArray();const xvalue* All=SdkGet(*R,"items");
    for(size_t i=0;i<xrtValueCount(All);i++){
        const xvalue* Entry=xrtValueArrayGet(All,i);cstr Name=SdkField(Entry,"name");
        if(Name&&strcmp(Name,".")&&strcmp(Name,"..")&&(Hidden||Name[0]!='.'))xrtValueArrayAppend(Items,Entry);
    }
    xrtValueObjectSetNew(*R,XRT_STR_LITERAL("items"),Items);SdkPut(*R,"revision",SdkField(Start,"revision"));SdkPut(*R,"path",Path);Ok=true;
done:xrtFree(Path);xrtValueRelease(Start);xrtValueRelease(End);if(!Ok){xrtValueRelease(*R);*R=NULL;}return Ok;
}
