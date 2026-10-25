/* Knowledge authoring uses the same durable file journal as store installation.
 * Only this internal path can replace a custom folder after checking its full
 * bundle revision. Store provenance is checked before entering this function. */
typedef struct KnowledgeFileContext {cstr Root;xvalue* Files;size_t Total;} KnowledgeFileContext;
static bool KnowledgeCollectFile(cstr Relative,const xfileinfo* Entry,void* Data)
{
    (void)Entry;KnowledgeFileContext* C=Data;char Full[512];int N=snprintf(Full,sizeof(Full),"%s/%s",C->Root,Relative);
    if(N<0||(size_t)N>=sizeof(Full))return false;
    xfile F=MdoHomeOpenRead(Full);xfileinfo Info;bool Read=F&&xrtFileStat(F,&Info)&&Info.Type==XFILE_TYPE_FILE&&Info.Size<=64u*1024u*1024u&&C->Total+Info.Size<=64u*1024u*1024u&&xrtValueCount(C->Files)<129;
    bytes B=Read?xrtMalloc((size_t)Info.Size+1):NULL;Read=Read&&B&&xrtReadFull(F,B,(size_t)Info.Size,NULL);if(F&&!xrtClose(F))Read=false;
    xvalue* Item=Read?EcoLocalFile(Full,B,(size_t)Info.Size):NULL;bool Ok=Item&&xrtValueArrayAppend(C->Files,Item);
    if(Ok)C->Total+=(size_t)Info.Size;xrtValueRelease(Item);xrtFree(B);return Ok;
}
static xvalue* KnowledgeCurrentFiles(cstr Id)
{
    char Root[96];snprintf(Root,sizeof(Root),"knowledge/%s",Id);bool Exists=false;xfileinfo Info;
    if(!MdoHomeExternalStat(Root,&Exists,&Info))return NULL;xvalue* Files=xrtValueArray();
    KnowledgeFileContext Context={Root,Files,0};
    if(Files&&Exists&&(Info.Type!=XFILE_TYPE_DIRECTORY||!MdoPortableTreeWalk(Root,KnowledgeCollectFile,&Context))){xrtValueRelease(Files);return NULL;}return Files;
}
static bool KnowledgeFilesRevision(const xvalue* Files,char Hash[65])
{
    const xvalue* Sorted[129];size_t Count=xrtValueCount(Files);if(Count>129)return false;
    for(size_t i=0;i<Count;i++){Sorted[i]=xrtValueArrayGet(Files,i);for(size_t j=i;j&&strcmp(MdoPackageText(Sorted[j-1],"path",255),MdoPackageText(Sorted[j],"path",255))>0;j--){const xvalue* Swap=Sorted[j];Sorted[j]=Sorted[j-1];Sorted[j-1]=Swap;}}
    xvalue* Values=xrtValueArray();bool Ok=Values!=NULL;
    for(size_t i=0;Ok&&i<Count;i++){xvalue* F=xrtValueObject();Ok=F&&xrtValueObjectSet(F,XRT_STR_LITERAL("path"),xrtValueObjectGet(Sorted[i],XRT_STR_LITERAL("path")))&&xrtValueObjectSet(F,XRT_STR_LITERAL("sha256"),xrtValueObjectGet(Sorted[i],XRT_STR_LITERAL("sha256")))&&xrtValueArrayAppend(Values,F);xrtValueRelease(F);}
    char* Text=Ok?xrtJsonStringify(Values,false,NULL):NULL;Ok=Text&&MdoExtensionHash(Text,Hash);xrtFree(Text);xrtValueRelease(Values);return Ok;
}
static bool KnowledgeExtras(cstr Id,const xvalue* Current,xvalue* Extras)
{
    char Base[96],Manifest[128];snprintf(Base,sizeof(Base),"knowledge/%s/",Id);snprintf(Manifest,sizeof(Manifest),"%sknowledge.json",Base);bool Ok=true;
    for(size_t i=0;Ok&&i<xrtValueCount(Current);i++){
        const xvalue* F=xrtValueArrayGet(Current,i);cstr Path=MdoPackageText(F,"path",255);if(!strcmp(Path,Manifest))continue;
        if(strncmp(Path,Base,strlen(Base)))return false;xvalue* Item=xrtValueObject();
        Ok=Item&&MdoApiValueSetString(Item,"path",Path+strlen(Base))&&xrtValueObjectSet(Item,XRT_STR_LITERAL("base64"),xrtValueObjectGet(F,XRT_STR_LITERAL("new")))&&xrtValueArrayAppend(Extras,Item);xrtValueRelease(Item);
    }return Ok;
}
static bool KnowledgeBundle(MdoApiContext* C,cstr Id)
{
    xvalue* Item=ExtensionItem("knowledge",Id,true),*Current=KnowledgeCurrentFiles(Id),*Extras=xrtValueArray();char Hash[65];
    bool Ok=Item&&Current&&Extras&&KnowledgeFilesRevision(Current,Hash)&&KnowledgeExtras(Id,Current,Extras)&&MdoApiValueSetString(Item,"bundle_revision",Hash)&&xrtValueObjectSet(Item,XRT_STR_LITERAL("files"),Extras);
    xrtValueRelease(Current);xrtValueRelease(Extras);
    if(!Ok){xrtValueRelease(Item);return MdoApiReplyError(C,413,"knowledge_bundle_unavailable","Knowledge folder is unreadable, contains links or exceeds 128 documents / 64 MiB",NULL);}return MdoApiReplySuccessTake(C,200,Item,NULL);
}
static bool KnowledgeInventoryFile(cstr Relative,const xfileinfo* Info,void* Data)
{
    if(!strcmp(Relative,"knowledge.json"))return true;
    xvalue* Files=Data; xvalue* F=xrtValueObject();
    bool Ok=xrtValueCount(Files)<128&&F&&MdoApiValueSetString(F,"path",Relative)&&MdoApiValueSetUInt(F,"bytes",Info->Size)&&xrtValueArrayAppend(Files,F);
    xrtValueRelease(F);return Ok;
}
static bool KnowledgeDocuments(MdoApiContext* C,cstr Id)
{
    char Root[96],Path[221]={0};snprintf(Root,sizeof(Root),"knowledge/%s",Id);xvalue* Data=xrtValueObject(),*Files=xrtValueArray();
    bool Ok=Data&&Files&&MdoPortableTreeWalk(Root,KnowledgeInventoryFile,Files)&&xrtValueObjectSet(Data,XRT_STR_LITERAL("files"),Files);xrtValueRelease(Files);
    if(C->Target.Query.Size){size_t N=0;xstrview Q=C->Target.Query;
        if(Q.Size<=5||memcmp(Q.Data,"path=",5)||memchr(Q.Data,'&',Q.Size)||!xrtPercentDecode(xrtStrViewN(Q.Data+5,Q.Size-5),Path,220,&N)||!N||memchr(Path,0,N)){xrtValueRelease(Data);return MdoApiReplyError(C,400,"knowledge_path_invalid","Provide one portable document path",NULL);}Path[N]=0;
        if(!MdoPackagePath(Path)||MdoPackageSamePath(Path,"knowledge.json")){xrtValueRelease(Data);return MdoApiReplyError(C,400,"knowledge_path_invalid","Only knowledge document paths are readable",NULL);}
        char Full[350];snprintf(Full,sizeof(Full),"%s/%s",Root,Path);xfile F=MdoHomeOpenRead(Full);xfileinfo Info;bool Read=F&&xrtFileStat(F,&Info)&&Info.Type==XFILE_TYPE_FILE;size_t NBytes=Read?(Info.Size>65536?65536:(size_t)Info.Size):0;
        char* Text=Read?xrtMalloc(NBytes+1):NULL;Read=Read&&Text&&xrtReadFull(F,Text,NBytes,NULL);if(F)xrtClose(F);
        if(Read){Text[NBytes]=0;size_t Bad=0;bool Utf=xrtUtf8Valid(xrtStrViewN(Text,NBytes),&Bad);if(!Utf&&NBytes==65536&&Bad>NBytes-4){NBytes=Bad;Text[NBytes]=0;Utf=true;}
            bool Binary=memchr(Text,0,NBytes)!=NULL||!Utf;Ok=Ok&&MdoApiValueSetString(Data,"path",Path)&&MdoApiValueSetBool(Data,"binary",Binary)&&MdoApiValueSetBool(Data,"truncated",Info.Size>NBytes)&&MdoApiValueSetStringView(Data,"text",xrtStrViewN(Binary?"":Text,Binary?0:NBytes));
        }else Ok=false;xrtFree(Text);
    }
    if(!Ok){xrtValueRelease(Data);return MdoApiReplyError(C,404,"knowledge_documents_unavailable","The document directory is missing, unreadable or contains unsupported paths",NULL);}return MdoApiReplySuccessTake(C,200,Data,NULL);
}
static bool KnowledgeWrite(MdoApiContext* C,cstr Id)
{
    char* Text=NULL;size_t Bytes=0;MdoApiBodyStatus Status=MdoApiBinaryBodyRead(C,MDO_PACKAGE_LIMIT,&Text,&Bytes);
    if(Status!=MDO_API_BODY_OK)return MdoApiReplyBodyError(C,Status);
    xjsonreadconfig Limits;xrtJsonReadConfigInit(&Limits);Limits.MaxInputBytes=MDO_PACKAGE_LIMIT;Limits.MaxStringBytes=90u*1024u*1024u;Limits.MaxValues=65536;
    xvalue* Body=xrtJsonRead(xrtStrViewN(Text,Bytes),&Limits);xrtFree(Text);char Error[1024]={0},Hash[65],Path[160];ExtensionPath("knowledge",Id,Path);
    bool Exists=false;xfileinfo Info;bool Ok=Body&&MdoHomeExternalStat(Path,&Exists,&Info);xvalue* Current=Ok?KnowledgeCurrentFiles(Id):NULL,*Extras=xrtValueArray();
    const xvalue* Supplied=xrtValueObjectGet(Body,XRT_STR_LITERAL("files"));cstr Revision=MdoPackageText(Body,"bundle_revision",64);
    if(Ok&&Exists&&Supplied&&(!Current||!KnowledgeFilesRevision(Current,Hash)||!Revision||strcmp(Hash,Revision))){xrtValueRelease(Current);xrtValueRelease(Extras);xrtValueRelease(Body);return MdoApiReplyError(C,412,"knowledge_bundle_conflict","Knowledge documents changed; reload the folder before saving. Your draft has not been written.",NULL);}
    if(Ok)Ok=Current&&Extras&&(Supplied?xrtValueType(Supplied)==XVALUE_ARRAY:KnowledgeExtras(Id,Current,Extras));
    xvalue *Resource=xrtValueObject(),*Package=xrtValueObject(),*Manifest=xrtValueObject(),*Resources=xrtValueArray(),*Entry=xrtValueObject(),*Platforms=xrtValueArray();
    cstr Content=MdoPackageText(Body,"content",131072);xvalue* Metadata=Content?xrtJsonParse(xrtStrView(Content)):NULL;
    Ok=Ok&&Metadata&&MdoKnowledgeMetadata(Metadata,Error,sizeof(Error))&&Resource&&Package&&Manifest&&Resources&&Entry&&Platforms&&
        MdoApiValueSetString(Resource,"kind","knowledge")&&MdoApiValueSetString(Resource,"id",Id)&&MdoApiValueSetString(Resource,"content",Content)&&xrtValueObjectSet(Resource,XRT_STR_LITERAL("files"),Supplied?Supplied:Extras)&&xrtValueArrayAppend(Resources,Resource);
    char Slug[65],Identity[65];MdoExtensionHash(Id,Identity);snprintf(Slug,sizeof(Slug),"kb-%.61s",Identity); // Receipt identity is local only.
    if(Ok)Ok=MdoApiValueSetString(Manifest,"slug",Slug)&&MdoApiValueSetString(Manifest,"name",MdoPackageText(Metadata,"name",100))&&MdoApiValueSetString(Manifest,"description",MdoPackageText(Metadata,"description",600))&&MdoApiValueSetString(Manifest,"version","1.0.0")&&MdoApiValueSetString(Manifest,"readme","Local knowledge documents")&&MdoApiValueSetString(Manifest,"license","UNLICENSED")&&MdoApiValueSetUInt(Manifest,"min_build_id",MDO_PACKAGE_LARGE_MIN_BUILD)&&xrtValueArrayAppendNew(Platforms,xrtValueString(xrtStrView(MdoToolPlatform())))&&xrtValueObjectSet(Manifest,XRT_STR_LITERAL("platforms"),Platforms)&&MdoApiValueSetString(Package,"format",MDO_PACKAGE_FORMAT_V2)&&xrtValueObjectSet(Package,XRT_STR_LITERAL("manifest"),Manifest)&&xrtValueObjectSet(Package,XRT_STR_LITERAL("resources"),Resources);
    if(Ok)Ok=MdoPackageValidate(Package,Error,sizeof(Error));
    if(Ok){const char* Credits[]={"author","copyright","license"};const size_t Bounds[]={100,4096,80};for(size_t i=0;Ok&&i<3;i++){cstr Credit=MdoPackageText(Metadata,Credits[i],Bounds[i]);if(Credit&&*Credit)Ok=MdoApiValueSetString(Manifest,Credits[i],Credit);}}
    char* Json=Ok?xrtJsonStringify(Package,false,NULL):NULL;
    if(Ok)Ok=Json&&MdoExtensionHash(Json,Hash)&&xrtValueObjectSet(Entry,XRT_STR_LITERAL("package"),Package)&&MdoApiValueSetString(Entry,"source","local")&&MdoApiValueSetString(Entry,"slug",Slug)&&MdoApiValueSetString(Entry,"name",MdoPackageText(Metadata,"name",100))&&MdoApiValueSetString(Entry,"version","1.0.0")&&MdoApiValueSetString(Entry,"author","")&&MdoApiValueSetUInt(Entry,"owner",0)&&MdoApiValueSetUInt(Entry,"id",0)&&MdoApiValueSetString(Entry,"sha256",Hash)&&EcoLocalApply(Entry,false,Error,NULL,NULL,Current);
    xrtFree(Json);xrtValueRelease(Body);xrtValueRelease(Current);xrtValueRelease(Extras);xrtValueRelease(Metadata);xrtValueRelease(Resource);xrtValueRelease(Package);xrtValueRelease(Manifest);xrtValueRelease(Resources);xrtValueRelease(Entry);xrtValueRelease(Platforms);
    return Ok?MdoApiReplySuccessTake(C,200,ExtensionItem("knowledge",Id,true),NULL):MdoApiReplyError(C,422,"knowledge_invalid",Error[0]?Error:"Invalid knowledge metadata or document folder; nothing was changed",NULL);
}
