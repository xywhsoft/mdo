static bool CliReadExact(xfile F,void* P,size_t N)
{size_t Got=0;return xrtRead(F,P,N,&Got)&&Got==N;}
static bool CliWriteExact(xfile F,const void* P,size_t N)
{size_t Written=0;return xrtWrite(F,P,N,&Written)&&Written==N;}
static bool CliFileHash(cstr Path,char Hash[65],uint64* Size,xcancel* Cancel)
{
    xfile F=xrtOpen(Path,XFILE_READ);uint8 Buffer[65536],Digest[32];xsha256 Sha;uint64 Total=0;bool Ok=F&&xrtFileSize(F,Size)&&*Size<=MDO_CLI_BYTES;xrtSha256Init(&Sha);
    while(Ok){size_t N=0;Ok=!xrtCancelRequested(Cancel)&&xrtRead(F,Buffer,sizeof(Buffer),&N);if(!Ok||!N)break;Total+=N;Ok=Total<=*Size&&xrtSha256Update(&Sha,Buffer,N);}
    Ok=Ok&&Total==*Size&&xrtSha256Final(&Sha,Digest);if(Ok)CliHex(Digest,Hash);xrtClose(F);return Ok;
}
static bool CliCollectFiles(xvalue* Files,cstr Root,cstr Relative,unsigned Depth,xcancel* Cancel,uint64* Total)
{
    if(Depth>16||xrtCancelRequested(Cancel))return false;
    char* Path=Relative[0]?xrtPathJoin(Root,Relative):xrtStrDup(Root);xfileinfo Info;
    if(!Path||!xrtPathStat(Path,false,&Info)){xrtFree(Path);return false;}bool Ok=true;
    if(Info.Type==XFILE_TYPE_DIRECTORY){xdir D=xrtDirOpen(Path,XDIR_STAT);xdirentry Entry;if(!D)Ok=false;
        xdirnext Next=XDIR_NEXT_END;
        while(Ok&&(Next=xrtDirNext(D,&Entry))==XDIR_NEXT_ITEM){char* Name=xrtStrDupN(Entry.Name.Data,Entry.Name.Size);char* Child=Name?(Relative[0]?xrtPathJoin(Relative,Name):xrtStrDup(Name)):NULL;xrtFree(Name);
            if(Child){for(char* C=Child;*C;C++)if(*C=='\\')*C='/';}Ok=Child&&MdoPackagePath(Child)&&CliCollectFiles(Files,Root,Child,Depth+1,Cancel,Total);xrtFree(Child);}
        if(Next==XDIR_NEXT_ERROR)Ok=false;
        xrtDirClose(D);
    }else if(Info.Type==XFILE_TYPE_FILE&&Relative[0]&&MdoPackagePath(Relative)&&xrtValueCount(Files)<MDO_CLI_FILES){
        char Hash[65];uint64 Size=0;Ok=CliFileHash(Path,Hash,&Size,Cancel)&&*Total+Size<=MDO_CLI_BYTES;*Total+=Size;
        if(Ok){xvalue* F=xrtValueObject();CliSet(F,"path",Relative);CliSet(F,"sha256",Hash);xrtValueObjectSetNew(F,XRT_STR_LITERAL("size"),xrtValueUInt(Size));
            /* Preserve executable bits without exposing write or special bits. */
            uint32 Mode=(Info.Mode&0111)?0755:0644;
#if defined(_WIN32) || defined(_WIN64)
            Mode=0755;
#endif
            xrtValueObjectSetNew(F,XRT_STR_LITERAL("mode"),xrtValueUInt(Mode));Ok=xrtValueArrayAppend(Files,F);xrtValueRelease(F);}
    }else Ok=false;xrtFree(Path);return Ok;
}
static bool CliBuildArchive(MdoCliJob* Job,const xvalue* Tool,char Relative[160],char Hash[65],uint64* Size,char* Error,size_t Capacity)
{
    cstr Entry=CliText(Tool,"path",4096),Selected=CliText(Tool,"package_root",4096);char* Root=NULL,*EntryRelative=NULL,*Managed=NULL;bool Ok=false;uint64 Total=0;
    if(!Selected[0]&&CliText(Tool,"managed_root",160)[0]){Managed=MdoHomeExternalPath(CliText(Tool,"managed_root",160));if(Managed)Selected=Managed;}
    const char* Base=Entry;for(const char* C=Entry;*C;C++)if(*C=='/'||*C=='\\')Base=C+1;
    if(Selected[0]){Root=xrtStrDup(Selected);EntryRelative=xrtPathRelative(xrtStrView(Root),xrtStrView(Entry),XPATH_NATIVE);if(EntryRelative)for(char* C=EntryRelative;*C;C++)if(*C=='\\')*C='/';}
    else{Root=xrtStrDupN(Entry,(size_t)(Base-Entry));EntryRelative=xrtStrDup(Base);}
    xvalue* Manifest=xrtValueObject(),*Files=xrtValueArray();xfile Output=NULL;
    if(!Root||!EntryRelative||!MdoPackagePath(EntryRelative))goto done;
    if(Selected[0])Ok=CliCollectFiles(Files,Root,"",0,Job->Cancel,&Total);
    else Ok=CliCollectFiles(Files,Root,EntryRelative,0,Job->Cancel,&Total);
    const cstr Keys[]={"id","name","version","platform","prompt","license"};
    for(size_t I=0;I<6;I++)CliSet(Manifest,Keys[I],CliText(Tool,Keys[I],I==4?16384:512));
    CliSet(Manifest,"description",CliText(Tool,"description",2048));
    CliSet(Manifest,"copyright",CliText(Tool,"copyright",4096));
    CliSet(Manifest,"entry",EntryRelative);const xvalue* Links=xrtValueObjectGet(Tool,XRT_STR_LITERAL("links"));if(Links)xrtValueObjectSet(Manifest,XRT_STR_LITERAL("links"),Links);
    xrtValueObjectSet(Manifest,XRT_STR_LITERAL("files"),Files);
    const xvalue* ExistingCommands=xrtValueObjectGet(Tool,XRT_STR_LITERAL("commands"));xvalue* Commands=ExistingCommands?xrtValueDeepClone(ExistingCommands):xrtValueArray();
    if(Commands&&!xrtValueCount(Commands)){char* Base=xrtPathName(EntryRelative);char* Name=Base;
#if defined(_WIN32) || defined(_WIN64)
        size_t L=Base?strlen(Base):0;if(L>4&&MdoCliAliasEqual(Base+L-4,".exe"))Base[L-4]=0;
#endif
        cstr Selected=CliText(Tool,"command",64);if(Selected[0])Name=(char*)Selected;xvalue* C=xrtValueObject();CliSet(C,"name",Name?Name:"");CliSet(C,"entry",EntryRelative);xrtValueObjectSetNew(C,XRT_STR_LITERAL("args"),xrtValueArray());xrtValueArrayAppendNew(Commands,C);xrtFree(Base);}
    const xvalue* Aliases=xrtValueObjectGet(Tool,XRT_STR_LITERAL("aliases"));
    for(size_t I=0;Commands&&I<xrtValueCount(Aliases);I++){xstrview Name={0};if(!xrtValueGetString(xrtValueArrayGet(Aliases,I),&Name))continue;bool Duplicate=false;for(size_t J=0;J<xrtValueCount(Commands);J++)if(MdoCliAliasEqual(Name.Data,CliText(xrtValueArrayGet(Commands,J),"name",64)))Duplicate=true;
        if(!Duplicate){xvalue* C=xrtValueObject();CliSet(C,"name",Name.Data);CliSet(C,"entry",EntryRelative);xrtValueObjectSetNew(C,XRT_STR_LITERAL("args"),xrtValueArray());xrtValueArrayAppendNew(Commands,C);}}
    if(Commands)xrtValueObjectSetNew(Manifest,XRT_STR_LITERAL("commands"),Commands);
    if(!Ok||!MdoCliManifestValid(Manifest,Error,Capacity))goto done;
    size_t N=0;char* Json=xrtJsonStringify(Manifest,false,&N);if(!Json||!N||N>MDO_CLI_JSON){xrtFree(Json);Ok=false;goto done;}
    snprintf(Relative,160,"data/cli-tools/uploads/%s.mdo-cli",Job->Id);Output=MdoHomeOpenWrite(Relative,XFILE_WRITE|XFILE_CREATE|XFILE_TRUNCATE);
    uint8 Header[12];memcpy(Header,MDO_CLI_MAGIC,8);for(size_t I=0;I<4;I++)Header[8+I]=(uint8)(N>>(8*I));
    Ok=Output&&CliWriteExact(Output,Header,sizeof(Header))&&CliWriteExact(Output,Json,N);xrtFree(Json);
    for(size_t I=0;Ok&&I<xrtValueCount(Files);I++){const xvalue* F=xrtValueArrayGet(Files,I);char* Native=xrtPathJoin(Root,CliText(F,"path",220));xfile Input=Native?xrtOpen(Native,XFILE_READ):NULL;xrtFree(Native);
        uint64 Expected=MdoDistNumber(F,"size"),Copied=0;xsha256 Sha;uint8 Buffer[65536],Digest[32];char Actual[65];xrtSha256Init(&Sha);Ok=Input!=NULL;
        while(Ok&&Copied<Expected){size_t Want=(size_t)((Expected-Copied)<sizeof(Buffer)?Expected-Copied:sizeof(Buffer)),Got=0;Ok=!xrtCancelRequested(Job->Cancel)&&xrtRead(Input,Buffer,Want,&Got)&&Got==Want&&xrtSha256Update(&Sha,Buffer,Got)&&CliWriteExact(Output,Buffer,Got);Copied+=Got;}
        uint64 Current=0;Ok=Ok&&xrtFileSize(Input,&Current)&&Current==Expected&&xrtSha256Final(&Sha,Digest);if(Ok){CliHex(Digest,Actual);Ok=!strcmp(Actual,CliText(F,"sha256",64));}xrtClose(Input);
    }
    Ok=Ok&&xrtFlush(Output);xrtClose(Output);Output=NULL;
    if(Ok){char* Native=MdoHomeExternalPath(Relative);Ok=Native&&CliFileHash(Native,Hash,Size,Job->Cancel);xrtFree(Native);}
done:xrtClose(Output);xrtFree(Managed);xrtFree(Root);xrtFree(EntryRelative);xrtValueRelease(Files);xrtValueRelease(Manifest);
    if(!Ok){if(Relative[0])MdoHomeRemove(Relative,false);if(!Error[0])snprintf(Error,Capacity,"Cannot package selected program files; paths, permissions or files changed");}return Ok;
}
static bool CliExtract(MdoCliJob* Job,const xvalue* Product,cstr Download,char Root[160],xvalue** Out,char* Error,size_t Capacity)
{
    xfile Input=MdoHomeOpenRead(Download);uint8 Header[12];uint64 ArchiveSize=0;char* Text=NULL;xvalue* M=NULL;bool Ok=false;
    if(!Input||!xrtFileSize(Input,&ArchiveSize)||ArchiveSize>MDO_CLI_BYTES||!CliReadExact(Input,Header,12)||memcmp(Header,MDO_CLI_MAGIC,8))goto done;
    uint32 Length=(uint32)Header[8]|((uint32)Header[9]<<8)|((uint32)Header[10]<<16)|((uint32)Header[11]<<24);
    if(!Length||Length>MDO_CLI_JSON||Length+12u>ArchiveSize)goto done;
    Text=xrtMalloc(Length);if(!Text||!CliReadExact(Input,Text,Length))goto done;
    xjsonreadconfig Config;xrtJsonReadConfigInit(&Config);Config.MaxInputBytes=MDO_CLI_JSON;Config.MaxValues=50000;Config.MaxStringBytes=32768;
    M=xrtJsonRead(xrtStrViewN(Text,Length),&Config);
    if(!MdoCliManifestValid(M,Error,Capacity)||strcmp(CliText(M,"platform",32),MdoToolPlatform())||strcmp(CliText(M,"id",64),CliText(Product,"slug",64))||strcmp(CliText(M,"version",64),CliText(Product,"version",64)))goto done;
    const xvalue* Files=xrtValueObjectGet(M,XRT_STR_LITERAL("files"));uint64 ExpectedArchive=12u+Length;
    for(size_t I=0;I<xrtValueCount(Files);I++)ExpectedArchive+=MdoDistNumber(xrtValueArrayGet(Files,I),"size");
    if(ExpectedArchive!=ArchiveSize)goto done;
    snprintf(Root,160,"data/cli-tools/installed/%s",Job->Id);if(!MdoHomeCreateDirectory(Root))goto done;Ok=true;
    for(size_t I=0;Ok&&I<xrtValueCount(Files);I++){const xvalue* F=xrtValueArrayGet(Files,I);char Relative[384];snprintf(Relative,sizeof(Relative),"%s/%s",Root,CliText(F,"path",220));
        xfile Output=MdoHomeOpenWrite(Relative,XFILE_WRITE|XFILE_CREATE|XFILE_TRUNCATE);uint64 Expected=MdoDistNumber(F,"size"),Copied=0;uint8 Buffer[65536],Digest[32];char Hash[65];xsha256 Sha;xrtSha256Init(&Sha);Ok=Output!=NULL;
        while(Ok&&Copied<Expected){size_t N=(size_t)((Expected-Copied)<sizeof(Buffer)?Expected-Copied:sizeof(Buffer));Ok=!xrtCancelRequested(Job->Cancel)&&CliReadExact(Input,Buffer,N)&&CliWriteExact(Output,Buffer,N)&&xrtSha256Update(&Sha,Buffer,N);Copied+=N;}
        Ok=Ok&&xrtSha256Final(&Sha,Digest)&&xrtFlush(Output);xrtClose(Output);if(Ok){CliHex(Digest,Hash);Ok=!strcmp(Hash,CliText(F,"sha256",64));}
#if defined(__linux__) && !defined(__ANDROID__)
        if(Ok)Ok=MdoHomeSetMode(Relative,(uint32)MdoDistNumber(F,"mode"));
#endif
    }
    if(Ok&&CliText(M,"description",2048)[0]&&xrtValueObjectGet(Product,XRT_STR_LITERAL("description")))Ok=!strcmp(CliText(M,"description",2048),CliText(Product,"description",2048));
    if(Ok&&CliText(M,"copyright",4096)[0]&&xrtValueObjectGet(Product,XRT_STR_LITERAL("copyright")))Ok=!strcmp(CliText(M,"copyright",4096),CliText(Product,"copyright",4096));
    if(Ok){*Out=xrtValueObject();const cstr Keys[]={"name","version","platform","prompt","license","links","commands","description","copyright"};for(size_t I=0;I<9;I++){const xvalue* V=xrtValueObjectGet(M,xrtStrView(Keys[I]));if(V)xrtValueObjectSet(*Out,xrtStrView(Keys[I]),V);}
        if(!CliText(M,"description",2048)[0])CliSet(*Out,"description",CliText(Product,"description",2048));
        if(!CliText(M,"copyright",4096)[0])CliSet(*Out,"copyright",CliText(Product,"copyright",4096));
        /* Publisher identity is supplied by the catalogue, never by an
         * archive's self-declared author or copyright holder. */
        CliSet(*Out,"submitter",CliText(Product,"submitter",100));CliSet(*Out,"author",CliText(Product,"author",100));
        char Relative[384];snprintf(Relative,sizeof(Relative),"%s/%s",Root,CliText(M,"entry",220));char* Native=MdoHomeExternalPath(Relative);
        Ok=Native&&CliPresent(Native)&&CliSet(*Out,"path",Native)&&CliSet(*Out,"managed_root",Root)&&CliSet(*Out,"source","store")&&CliSet(*Out,"entry",CliText(M,"entry",220))&&CliSet(*Out,"id",CliText(Product,"tool_id",64));xrtFree(Native);
        xrtValueObjectSet(*Out,XRT_STR_LITERAL("store_id"),xrtValueObjectGet(Product,XRT_STR_LITERAL("id")));xrtValueObjectSetNew(*Out,XRT_STR_LITERAL("enabled"),xrtValueBool(true));}
done:xrtClose(Input);xrtFree(Text);xrtValueRelease(M);
    if(!Ok){if(Root[0]){size_t Count=0;(void)MdoDistRemoveTree(Root,0,&Count);}xrtValueRelease(*Out);*Out=NULL;if(!Error[0])snprintf(Error,Capacity,"Downloaded tool files do not match the approved manifest");}return Ok;
}
