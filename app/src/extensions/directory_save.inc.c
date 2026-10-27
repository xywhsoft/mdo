/* Private staging uses hardlinks for unchanged binaries; every edited file is
 * atomically replaced, never truncated through a link to the live directory. */
static bool MdoExtCloneFile(cstr Source,cstr Target,unsigned Mode,xcancel* Cancel)
{
    if(Cancel&&xrtCancelRequested(Cancel))return false;
    if(MdoHomeLinkNoReplace(Source,Target))return true;
    xrtClearError();xfile Input=MdoHomeOpenRead(Source),Output=Input?MdoHomeOpenWrite(Target,XFILE_CREATE|XFILE_EXCLUSIVE):NULL;
    bool Ok=Input&&Output;unsigned char Buffer[65536];
    while(Ok){size_t N=0;Ok=(!Cancel||!xrtCancelRequested(Cancel))&&xrtRead(Input,Buffer,sizeof(Buffer),&N);if(!Ok||!N)break;Ok=xrtWriteFull(Output,Buffer,N,NULL);}
    if(Ok)Ok=xrtFlush(Output);if(Input&&!xrtClose(Input))Ok=false;if(Output&&!xrtClose(Output))Ok=false;
#if !defined(_WIN32) && !defined(_WIN64)
    if(Ok)Ok=MdoHomeSetMode(Target,Mode);
#endif
    return Ok;
}
static bool MdoExtCloneDirectory(cstr Root,cstr Incoming,const MdoExtTree* Tree,xcancel* Cancel)
{
    bool Ok=MdoExtEnsure(Incoming);
    for(size_t I=0;Ok&&I<Tree->Count;I++){
        const MdoExtFile* F=&Tree->Files[I];char* Source=xrtFormat("%s/%s",Root,F->Path),*Target=xrtFormat("%s/%s",Incoming,F->Path);
        Ok=Source&&Target&&(!Cancel||!xrtCancelRequested(Cancel));if(Ok)Ok=F->Directory?MdoExtEnsure(Target):MdoExtCloneFile(Source,Target,F->Info.Mode,Cancel);
        xrtFree(Source);xrtFree(Target);
    }return Ok;
}
static bool MdoExtDeclaredFiles(cstr Root,const xvalue* Manifest,char Error[512])
{
    const cstr Keys[]={"entry","native","frontend","readme"};
    for(size_t I=0;I<4;I++){cstr Path=MdoExtensionText(Manifest,Keys[I],4096);if(!Path)continue;char* Full=xrtFormat("%s/%s",Root,Path);bool Exists=false;xfileinfo Info;
        bool Ok=Full&&MdoExtStat(Full,&Exists,&Info)&&Exists&&Info.Type==XFILE_TYPE_FILE&&(Info.Size||!strcmp(Keys[I],"readme"));xrtFree(Full);
        if(!Ok){if(Error)snprintf(Error,512,"Declared %s file is missing or empty: %.300s",Keys[I],Path);return false;}}
    return true;
}
static bool MdoExtSaveTaskFile(cstr Target,MdoExtensionTaskUse* Use,cstr Id,unsigned Mode,xcancel* Cancel,char Error[512])
{
    xvalue* Task=MdoExtensionTaskRead(Id,Error);cstr Purpose=MdoExtensionText(Task,"purpose",16),Hash=MdoExtensionText(Task,"sha256",64);char Expected[65]={0};if(Hash)snprintf(Expected,sizeof(Expected),"%s",Hash);
    char* Path=Task&&Purpose&&!strcmp(Purpose,"file")&&!xrtValueObjectHas(Task,XRT_STR_LITERAL("store"))?MdoExtensionTaskArchive(Id,Error):NULL;xfile Input=Path?MdoHomeOpenRead(Path):NULL;uint64 ExpectedBytes=0;
    int64 Signed=0;const xvalue* Size=xrtValueObjectGet(Task,XRT_STR_LITERAL("bytes"));bool Ok=Input&&(xrtValueGetUInt(Size,&ExpectedBytes)||(xrtValueGetInt(Size,&Signed)&&Signed>0&&(ExpectedBytes=(uint64)Signed,true)));xrtFree(Path);xrtValueRelease(Task);
    bool Exists=false;xfileinfo Info;Ok=Ok&&MdoExtStat(Target,&Exists,&Info)&&(!Exists||(Info.Type==XFILE_TYPE_FILE&&MdoHomeRemove(Target,false)));
    xfile Output=Ok?MdoHomeOpenWrite(Target,XFILE_CREATE|XFILE_EXCLUSIVE):NULL;Ok=Ok&&Output;uint8 Buffer[65536],Digest[32];xsha256 Sha;xrtSha256Init(&Sha);uint64 Total=0;
    while(Ok){size_t N=0;Ok=(!Cancel||!xrtCancelRequested(Cancel))&&!xrtCancelRequested(MdoExtensionTaskUseCancel(Use))&&xrtRead(Input,Buffer,sizeof(Buffer),&N);if(!Ok||!N)break;
        Ok=N<=ExpectedBytes-Total&&xrtWriteFull(Output,Buffer,N,NULL)&&xrtSha256Update(&Sha,Buffer,N);Total+=N;}
    char Actual[65]={0};Ok=Ok&&Total==ExpectedBytes&&xrtSha256Final(&Sha,Digest);if(Ok){for(size_t I=0;I<32;I++)snprintf(Actual+I*2,3,"%02x",Digest[I]);Ok=!strcmp(Expected,Actual)&&xrtFlush(Output);}
    if(Input&&!xrtClose(Input))Ok=false;if(Output&&!xrtClose(Output))Ok=false;
#if !defined(_WIN32) && !defined(_WIN64)
    if(Ok)Ok=MdoHomeSetMode(Target,Mode);
#endif
    if(!Ok&&Error&&!Error[0])MdoExtError(Error,"Resource file task was cancelled, changed or could not be written");return Ok;
}
bool MdoExtensionsSave(cstr Kind,cstr Id,const xvalue* Manifest,const xvalue* Edits,cstr Expected,bool Trust,xcancel* Cancel,xvalue** Result,char Error[512])
{
    *Result=NULL;if(Error)Error[0]=0;
    if(!MdoExtLock||!MdoExtensionDirectoryKind(Kind)||!MdoExtensionDirectoryId(Id)||!Expected||(Expected[0]&&strlen(Expected)!=64)||!MdoExtensionManifest(Manifest,Error,512)||strcmp(Kind,MdoExtensionText(Manifest,"kind",32))||xrtValueType(Edits)!=XVALUE_ARRAY||xrtValueCount(Edits)>32)
        return MdoExtError(Error,"Save requires a manifest, a directory revision (empty for new) and at most 32 text/file task edits");
    if(MdoExtRequiresTrust(Manifest)&&!Trust)return MdoExtError(Error,"Approve this executable extension before saving its directory");
    size_t Total=0;for(size_t I=0;I<xrtValueCount(Edits);I++){
        const xvalue* Edit=xrtValueArrayGet(Edits,I);cstr Path=MdoExtensionText(Edit,"path",4096),Text=MdoExtensionText(Edit,"text",1048576),File=MdoExtensionText(Edit,"task",32);bool Executable=false;
        if(!Path||!MdoExtensionRelativePath(Path)||!strcmp(Path,"extension.json")||((Text!=NULL)==(File!=NULL))||(Text&&strlen(Text)>1048576-Total)||(File&&(strlen(File)!=32||!MdoExtensionDirectoryId(File)))||(xrtValueObjectHas(Edit,XRT_STR_LITERAL("executable"))&&!xrtValueGetBool(xrtValueObjectGet(Edit,XRT_STR_LITERAL("executable")),&Executable)))return MdoExtError(Error,"Use a relative file path and either UTF-8 text or a sealed file task; text edits total at most 1 MiB");
        for(size_t J=0;J<xrtValueCount(Edit);J++){xstrview K={0};xrtValueObjectAt(Edit,J,&K);if(!xrtStrEqual(K,XRT_STR_LITERAL("path"))&&!xrtStrEqual(K,XRT_STR_LITERAL("text"))&&!xrtStrEqual(K,XRT_STR_LITERAL("task"))&&!xrtStrEqual(K,XRT_STR_LITERAL("executable")))return MdoExtError(Error,"File edits do not accept embedded binary fields");}
        for(size_t J=0;J<I;J++)if(!strcmp(Path,MdoExtensionText(xrtValueArrayGet(Edits,J),"path",4096)))return MdoExtError(Error,"The same file cannot be edited twice in one save");if(Text)Total+=strlen(Text);
    }
    MdoExtensionTaskUse* Files[32]={0};bool Pinned=true;for(size_t I=0;Pinned&&I<xrtValueCount(Edits);I++){cstr File=MdoExtensionText(xrtValueArrayGet(Edits,I),"task",32);if(File){Files[I]=MdoExtensionTaskUseBegin(File,false,Cancel,Error);Pinned=Files[I]!=NULL;}}
    if(!Pinned){for(size_t I=0;I<32;I++)MdoExtensionTaskUseEnd(Files[I]);return false;}
    xrtMutexLock(MdoExtLock);char* Root=MdoExtensionsRoot(Kind,Id);bool Exists=false;xfileinfo Info;
    xvalue* State=MdoExtReadJson(MDO_EXT_STATE,MDO_EXT_STATE_LIMIT,true);char* Key=MdoExtKey(Kind,Id);const xvalue* Before=State&&Key?xrtValueObjectGet(State,xrtStrView(Key)):NULL;
    cstr Source=Before?MdoExtensionText(Before,"source",16):"manual";bool Pending=false;xrtValueGetBool(xrtValueObjectGet(Before,XRT_STR_LITERAL("pending_remove")),&Pending);
    MdoExtTree Tree={0};char PriorHash[65]={0},NewHash[65]={0};
    bool Ok=Root&&Key&&State&&xrtValueType(State)==XVALUE_OBJECT&&Source&&!strcmp(Source,"manual")&&!Pending&&!MdoExtBusy(Kind,Id)&&MdoExtStat(Root,&Exists,&Info)&&
        (Expected[0]?(Exists&&MdoExtCanonical(Kind,Id)&&MdoExtCollect(Root,&Tree,Cancel,Error)&&MdoExtFingerprint(Root,&Tree,PriorHash,Cancel)&&!strcmp(Expected,PriorHash)):!Exists);
    char* Task=Ok?xrtXidMakeString():NULL,*Base=Task?xrtFormat(MDO_EXT_TASKS "/%s",Task):NULL,*Incoming=Base?xrtFormat("%s/incoming",Base):NULL;
    Ok=Ok&&Base&&Incoming&&MdoExtStage(Base,Kind,Id)&&MdoExtCloneDirectory(Root,Incoming,&Tree,Cancel);
    for(size_t I=0;Ok&&I<xrtValueCount(Edits);I++){
        const xvalue* Edit=xrtValueArrayGet(Edits,I);cstr Path=MdoExtensionText(Edit,"path",4096),Text=MdoExtensionText(Edit,"text",1048576),File=MdoExtensionText(Edit,"task",32);char* Full=xrtFormat("%s/%s",Incoming,Path);
        MdoExtTree Current={0};bool Present=false;Ok=Full&&MdoExtCollect(Incoming,&Current,Cancel,Error)&&MdoExtStat(Full,&Present,&Info);
        if(Ok&&Present){bool Exact=false;for(size_t J=0;J<Current.Count;J++)if(!strcmp(Current.Files[J].Path,Path)&&!Current.Files[J].Directory)Exact=true;Ok=Exact;}
        else if(Ok)Ok=MdoExtTreePaths(&Current,Path,false,Error);MdoExtTreeUnit(&Current);
        bool Executable=false;xrtValueGetBool(xrtValueObjectGet(Edit,XRT_STR_LITERAL("executable")),&Executable);unsigned Mode=Present?Info.Mode:0600;if(xrtValueObjectHas(Edit,XRT_STR_LITERAL("executable")))Mode=Executable?Mode|0100:Mode&~0111u;
        Ok=Ok&&(!Cancel||!xrtCancelRequested(Cancel))&&(File?MdoExtSaveTaskFile(Full,Files[I],File,Mode,Cancel,Error):MdoHomeAtomicWrite(Full,Text,strlen(Text),false));
#if !defined(_WIN32) && !defined(_WIN64)
        if(Ok&&!File)Ok=MdoHomeSetMode(Full,Mode);
#endif
        xrtFree(Full);
    }
    char* MetaPath=Incoming?xrtFormat("%s/extension.json",Incoming):NULL;
    Ok=Ok&&MetaPath&&MdoExtSaveJson(MetaPath,Manifest,MDO_EXTENSION_MANIFEST_BYTES)&&MdoExtDeclaredFiles(Incoming,Manifest,Error);
    MdoExtTree Staged={0};Ok=Ok&&MdoExtCollect(Incoming,&Staged,Cancel,Error)&&MdoExtFingerprint(Incoming,&Staged,NewHash,Cancel);MdoExtTreeUnit(&Staged);
    /* Creating links changes ctime. Compare a fresh source content fingerprint. */
    if(Ok&&Exists){MdoExtTree Current={0};char Now[65];Ok=MdoExtCollect(Root,&Current,Cancel,Error)&&MdoExtFingerprint(Root,&Current,Now,Cancel)&&!strcmp(PriorHash,Now);MdoExtTreeUnit(&Current);}
    xvalue* Record=Before?xrtValueClone(Before):xrtValueObject();Ok=Ok&&Record&&MdoExtPut(Record,"source","manual")&&MdoExtPut(Record,"kind",Kind)&&MdoExtPut(Record,"id",Id);
    if(Ok&&!Exists)Ok=xrtValueObjectSetNew(Record,XRT_STR_LITERAL("enabled"),xrtValueBool(false));for(size_t I=0;Ok&&I<32;I++)if(Files[I]&&xrtCancelRequested(MdoExtensionTaskUseCancel(Files[I])))Ok=false;if(Ok)Ok=MdoExtPublish(Kind,Id,Task,false,Record,Cancel,Result,Error);
    if(Ok&&*Result)MdoExtPut(*Result,"revision",NewHash);
    if(!Ok&&Base){char* Journal=xrtFormat("%s/transaction.json",Base);bool Prepared=false;if(Journal&&MdoExtStat(Journal,&Prepared,&Info)&&!Prepared)(void)MdoExtRemoveTree(Base);xrtFree(Journal);}
    MdoExtTreeUnit(&Tree);xrtValueRelease(Record);xrtValueRelease(State);xrtFree(Key);xrtFree(Root);xrtFree(Task);xrtFree(Base);xrtFree(Incoming);xrtFree(MetaPath);xrtMutexUnlock(MdoExtLock);
    for(size_t I=0;I<32;I++)if(Files[I]){if(Ok){xvalue* Receipt=NULL;char Why[512];cstr File=MdoExtensionText(xrtValueArrayGet(Edits,I),"task",32);if(!MdoExtensionTaskComplete(File,NULL,&Receipt,Why)&&*Result)MdoExtPut(*Result,"transfer_receipt_error",Why);xrtValueRelease(Receipt);}MdoExtensionTaskUseEnd(Files[I]);}
    if(!Ok&&Error&&!Error[0])MdoExtError(Error,"The directory is managed, busy or changed, or its staged files could not be saved; the original directory was retained");return Ok;
}
