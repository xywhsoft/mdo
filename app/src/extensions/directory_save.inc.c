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
bool MdoExtensionsSave(cstr Kind,cstr Id,const xvalue* Manifest,const xvalue* Edits,cstr Expected,bool Trust,xcancel* Cancel,xvalue** Result,char Error[512])
{
    *Result=NULL;if(Error)Error[0]=0;
    if(!MdoExtLock||!MdoExtensionDirectoryKind(Kind)||!MdoExtensionDirectoryId(Id)||!Expected||(Expected[0]&&strlen(Expected)!=64)||!MdoExtensionManifest(Manifest,Error,512)||strcmp(Kind,MdoExtensionText(Manifest,"kind",32))||xrtValueType(Edits)!=XVALUE_ARRAY||xrtValueCount(Edits)>32)
        return MdoExtError(Error,"Save requires a manifest, a directory revision (empty for new) and at most 32 text edits");
    if(MdoExtRequiresTrust(Manifest)&&!Trust)return MdoExtError(Error,"Approve this executable extension before saving its directory");
    size_t Total=0;for(size_t I=0;I<xrtValueCount(Edits);I++){
        const xvalue* Edit=xrtValueArrayGet(Edits,I);cstr Path=MdoExtensionText(Edit,"path",4096),Text=MdoExtensionText(Edit,"text",1048576);
        if(!Path||!Text||!MdoExtensionRelativePath(Path)||!strcmp(Path,"extension.json")||strlen(Text)>1048576-Total)return MdoExtError(Error,"Use portable text paths; the manifest is separate and total edited text is limited to 1 MiB");
        for(size_t J=0;J<I;J++)if(!strcmp(Path,MdoExtensionText(xrtValueArrayGet(Edits,J),"path",4096)))return MdoExtError(Error,"The same file cannot be edited twice in one save");Total+=strlen(Text);
    }
    xrtMutexLock(MdoExtLock);char* Root=MdoExtensionsRoot(Kind,Id);bool Exists=false;xfileinfo Info;
    xvalue* State=MdoExtReadJson(MDO_EXT_STATE,MDO_EXT_STATE_LIMIT,true);char* Key=MdoExtKey(Kind,Id);const xvalue* Before=State&&Key?xrtValueObjectGet(State,xrtStrView(Key)):NULL;
    cstr Source=Before?MdoExtensionText(Before,"source",16):"manual";bool Pending=false;xrtValueGetBool(xrtValueObjectGet(Before,XRT_STR_LITERAL("pending_remove")),&Pending);
    MdoExtTree Tree={0};char PriorHash[65]={0},NewHash[65]={0};
    bool Ok=Root&&Key&&State&&xrtValueType(State)==XVALUE_OBJECT&&Source&&!strcmp(Source,"manual")&&!Pending&&!MdoExtBusy(Kind,Id)&&MdoExtStat(Root,&Exists,&Info)&&
        (Expected[0]?(Exists&&MdoExtCanonical(Kind,Id)&&MdoExtCollect(Root,&Tree,Cancel,Error)&&MdoExtFingerprint(Root,&Tree,PriorHash,Cancel)&&!strcmp(Expected,PriorHash)):!Exists);
    char* Task=Ok?xrtXidMakeString():NULL,*Base=Task?xrtFormat(MDO_EXT_TASKS "/%s",Task):NULL,*Incoming=Base?xrtFormat("%s/incoming",Base):NULL;
    Ok=Ok&&Base&&Incoming&&MdoExtStage(Base,Kind,Id)&&MdoExtCloneDirectory(Root,Incoming,&Tree,Cancel);
    for(size_t I=0;Ok&&I<xrtValueCount(Edits);I++){
        const xvalue* Edit=xrtValueArrayGet(Edits,I);cstr Path=MdoExtensionText(Edit,"path",4096),Text=MdoExtensionText(Edit,"text",1048576);char* Full=xrtFormat("%s/%s",Incoming,Path);
        MdoExtTree Current={0};bool Present=false;Ok=Full&&MdoExtCollect(Incoming,&Current,Cancel,Error)&&MdoExtStat(Full,&Present,&Info);
        if(Ok&&Present){bool Exact=false;for(size_t J=0;J<Current.Count;J++)if(!strcmp(Current.Files[J].Path,Path)&&!Current.Files[J].Directory)Exact=true;Ok=Exact;}
        else if(Ok)Ok=MdoExtTreePaths(&Current,Path,false,Error);MdoExtTreeUnit(&Current);
        Ok=Ok&&(!Cancel||!xrtCancelRequested(Cancel))&&MdoHomeAtomicWrite(Full,Text,strlen(Text),false);xrtFree(Full);
    }
    char* MetaPath=Incoming?xrtFormat("%s/extension.json",Incoming):NULL;
    Ok=Ok&&MetaPath&&MdoExtSaveJson(MetaPath,Manifest,MDO_EXTENSION_MANIFEST_BYTES)&&MdoExtDeclaredFiles(Incoming,Manifest,Error);
    MdoExtTree Staged={0};Ok=Ok&&MdoExtCollect(Incoming,&Staged,Cancel,Error)&&MdoExtFingerprint(Incoming,&Staged,NewHash,Cancel);MdoExtTreeUnit(&Staged);
    /* Creating links changes ctime. Compare a fresh source content fingerprint. */
    if(Ok&&Exists){MdoExtTree Current={0};char Now[65];Ok=MdoExtCollect(Root,&Current,Cancel,Error)&&MdoExtFingerprint(Root,&Current,Now,Cancel)&&!strcmp(PriorHash,Now);MdoExtTreeUnit(&Current);}
    xvalue* Record=Before?xrtValueClone(Before):xrtValueObject();Ok=Ok&&Record&&MdoExtPut(Record,"source","manual")&&MdoExtPut(Record,"kind",Kind)&&MdoExtPut(Record,"id",Id);
    if(Ok&&!Exists)Ok=xrtValueObjectSetNew(Record,XRT_STR_LITERAL("enabled"),xrtValueBool(false));if(Ok)Ok=MdoExtPublish(Kind,Id,Task,false,Record,Cancel,Result,Error);
    if(Ok&&*Result)MdoExtPut(*Result,"revision",NewHash);
    if(!Ok&&Base){char* Journal=xrtFormat("%s/transaction.json",Base);bool Prepared=false;if(Journal&&MdoExtStat(Journal,&Prepared,&Info)&&!Prepared)(void)MdoExtRemoveTree(Base);xrtFree(Journal);}
    MdoExtTreeUnit(&Tree);xrtValueRelease(Record);xrtValueRelease(State);xrtFree(Key);xrtFree(Root);xrtFree(Task);xrtFree(Base);xrtFree(Incoming);xrtFree(MetaPath);xrtMutexUnlock(MdoExtLock);
    if(!Ok&&Error&&!Error[0])MdoExtError(Error,"The directory is managed, busy or changed, or its staged files could not be saved; the original directory was retained");return Ok;
}
