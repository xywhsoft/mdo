static xvalue* CliService(cstr Path,cstr Method,const xvalue* Body,MdoAccountLease* Lease,xcancel* Cancel,char* Error,size_t Capacity)
{
    XS_FetchRequest Request={0};XS_FetchResponse Response={0};char Url[1200],Auth[4200]={0};size_t N=0;char* Json=Body?xrtJsonStringify(Body,false,&N):NULL;xvalue* Envelope=NULL,*Result=NULL;
    XS_FetchHeader Headers[3]={{"Accept","application/json"},{"Content-Type","application/json"}};size_t Count=2;
    if(Lease&&Lease->Managed&&Lease->AccessToken){snprintf(Auth,sizeof(Auth),"Bearer %s",Lease->AccessToken);Headers[Count++]=(XS_FetchHeader){"Authorization",Auth};}
    snprintf(Url,sizeof(Url),MDO_ACCOUNT_SERVICE_ORIGIN "%s",Path);Request.Size=sizeof(Request);Request.Version=XS_FETCH_REQUEST_VERSION;Request.Url=Url;Request.Method=Method;Request.Headers=Headers;Request.HeaderCount=Count;Request.Body=Json;Request.BodySize=N;
    Request.MaxBodyBytes=MDO_CLI_JSON+65536;Request.Timeout=30000000;Request.IdleTimeout=15000000;Request.Cancel=Lease&&Lease->Managed?Lease->Cancel:Cancel;Response.Size=sizeof(Response);
    if(!xsFetch(&Request,&Response)){snprintf(Error,Capacity,"External-tool service connection failed");goto done;}
    xjsonreadconfig Config;xrtJsonReadConfigInit(&Config);Config.MaxInputBytes=MDO_CLI_JSON+65536;Config.MaxStringBytes=32768;Config.MaxValues=50000;
    Envelope=xrtJsonRead(xrtStrViewN((cstr)Response.Body,Response.BodySize),&Config);int64 Code=-1;
    if(Response.Status<200||Response.Status>=300||!MdoPackageNumber(Envelope,"code",&Code)||Code){
        snprintf(Error,Capacity,"HTTP %u: %.180s",Response.Status,CliText(Envelope,"message",512));if(Response.Status==401&&Lease&&Lease->Managed)(void)MdoAccountRejectAccess(Lease);goto done;}
    Result=xrtValueObjectTake(Envelope,XRT_STR_LITERAL("data"));
done:xrtSecureZero(Auth,sizeof(Auth));xrtFree(Json);xsFetchResponseUnit(&Response);xrtValueRelease(Envelope);return Result;
}
static bool CliCatalog(MdoCliJob* Job,char* Error,size_t Capacity)
{
    char Path[1000];cstr Query=CliText(Job->Input,"query",100);size_t N=0;char* Encoded=xrtPercentEncodeNew(Query,strlen(Query),xrtStrView(""),&N);
    snprintf(Path,sizeof(Path),"/api/v1/mdo/cli/catalog?platform=%s&q=%s&before=%llu",MdoToolPlatform(),Encoded?Encoded:"",(unsigned long long)(MdoDistNumber(Job->Input,"before")?MdoDistNumber(Job->Input,"before"):INT64_MAX));xrtFree(Encoded);
    xvalue* Catalog=CliService(Path,"GET",NULL,NULL,Job->Cancel,Error,Capacity);if(!Catalog)return false;
    xrtMutexLock(g_MdoCli.Lock);xrtValueRelease(g_MdoCli.Catalog);(void)CliFillDescriptions(xrtValueObjectGet(Catalog,XRT_STR_LITERAL("items")));g_MdoCli.Catalog=Catalog;xrtMutexUnlock(g_MdoCli.Lock);return true;
}
static bool CliInstall(MdoCliJob* Job,char* Error,size_t Capacity)
{
#if defined(__ANDROID__)
    (void)Job;snprintf(Error,Capacity,"Android programs are installed and updated through the full APK");return false;
#else
    uint64 Id=MdoDistNumber(Job->Input,"store_id");if(!Id){snprintf(Error,Capacity,"Store version is required");return false;}
    char Path[256],Download[160],Root[160]={0};snprintf(Path,sizeof(Path),"/api/v1/mdo/cli/detail?id=%llu",(unsigned long long)Id);
    xvalue* Product=CliService(Path,"GET",NULL,NULL,Job->Cancel,Error,Capacity),*Tool=NULL;if(!Product)return false;
    cstr Key=CliText(Product,"tool_id",64),Hash=CliText(Product,"sha256",64);uint64 Size=MdoDistNumber(Product,"size");
    bool Ok=MdoPackageId(Key)&&MdoCliHashValid(Hash)&&Size>12&&Size<=MDO_CLI_BYTES&&!strcmp(CliText(Product,"platform",32),MdoToolPlatform())&&!strcmp(CliText(Product,"state",16),"published");
    if(!Ok){snprintf(Error,Capacity,"Store version is unpublished, invalid or incompatible with this device");goto done;}
    snprintf(Download,sizeof(Download),"data/cli-tools/downloads/%s.mdo-cli",Job->Id);snprintf(Path,sizeof(Path),"/api/v1/mdo/cli/blob?hash=%s",Hash);
    MdoTransferReport Report={0};Ok=MdoTransferDownloadProgress(g_MdoCli.Server->Engine,Path,Download,Size,Hash,Job->Cancel,CliProgress,Job);
    if(!Ok){snprintf(Error,Capacity,"Tool download failed or content SHA-256 did not match");MdoHomeRemove(Download,false);goto done;}
    (void)Report;CliTaskStatus(Job->Id,"installing",NULL);Ok=CliExtract(Job,Product,Download,Root,&Tool,Error,Capacity);MdoHomeRemove(Download,false);
    if(!Ok)goto done;
    xvalue* DescriptionItems=xrtValueArray();xrtValueArrayAppend(DescriptionItems,Tool);(void)CliFillDescriptions(DescriptionItems);xrtValueRelease(DescriptionItems);
    xrtMutexLock(g_MdoCli.Lock);xvalue* Previous=xrtValueDeepClone(g_MdoCli.State),*Old=CliFind(Key);bool Enable=CliBool(Job->Input,"enable",Old?CliBool(Old,"enabled",true):true);
    if(Old){/* A managed update never silently replaces a user-selected program. */
        if(strcmp(CliText(Old,"source",16),"store")&&strcmp(CliText(Old,"source",16),"legacy")){Ok=false;snprintf(Error,Capacity,"Tool identity belongs to a user-defined program");}
        else {xrtValueObjectSetNew(Tool,XRT_STR_LITERAL("enabled"),xrtValueBool(Enable));const xvalue* Aliases=xrtValueObjectGet(Old,XRT_STR_LITERAL("aliases"));if(Aliases)xrtValueObjectSet(Tool,XRT_STR_LITERAL("aliases"),Aliases);if(!xrtValueCount(xrtValueObjectGet(Tool,XRT_STR_LITERAL("commands")))){const xvalue* Command=xrtValueObjectGet(Old,XRT_STR_LITERAL("command"));if(Command)xrtValueObjectSet(Tool,XRT_STR_LITERAL("command"),Command);}for(size_t I=0;I<xrtValueCount(CliItems());I++)if(xrtValueArrayGet(CliItems(),I)==Old){CliRetire(Old);xrtValueArraySet(CliItems(),I,Tool);break;}}
    }else if(xrtValueCount(CliItems())>=256){Ok=false;snprintf(Error,Capacity,"External-tool list is full");}else xrtValueArrayAppend(CliItems(),Tool);
    if(Ok){xrtValueObjectSetNew(g_MdoCli.State,XRT_STR_LITERAL("revision"),xrtValueUInt(MdoDistNumber(g_MdoCli.State,"revision")+1));Ok=!xrtCancelRequested(Job->Cancel)&&CliSave();}
    if(!Ok){xrtValueRelease(g_MdoCli.State);g_MdoCli.State=Previous;Previous=NULL;if(!Error[0])snprintf(Error,Capacity,"%s",g_MdoCli.BindingError);}xrtValueRelease(Previous);xrtMutexUnlock(g_MdoCli.Lock);
    if(!Ok&&Root[0]){size_t Count=0;(void)MdoDistRemoveTree(Root,0,&Count);}
done:xrtValueRelease(Product);xrtValueRelease(Tool);return Ok;
#endif
}
static bool CliImport(MdoCliJob* Job,char* Error,size_t Capacity)
{
#if defined(__ANDROID__)
    (void)Job;snprintf(Error,Capacity,"Android programs must be installed with the full APK");return false;
#else
    const xvalue* Source=xrtValueObjectGet(Job->Input,XRT_STR_LITERAL("tool"));char Archive[160]={0},Hash[65],Root[160]={0};uint64 Size=0;xvalue* Tool=NULL,*Product=xrtValueObject();
    bool Ok=CliMeta(Source)&&CliBuildArchive(Job,Source,Archive,Hash,&Size,Error,Capacity);
    CliSet(Product,"slug",CliText(Source,"id",64));CliSet(Product,"tool_id",CliText(Source,"id",64));CliSet(Product,"version",CliText(Source,"version",64));
    if(Ok)Ok=CliExtract(Job,Product,Archive,Root,&Tool,Error,Capacity);
    if(Ok){CliSet(Tool,"source","local");xrtValueObjectRemove(Tool,XRT_STR_LITERAL("store_id"));xrtValueObjectSetNew(Tool,XRT_STR_LITERAL("enabled"),xrtValueBool(CliBool(Source,"enabled",true)));
        const cstr Keys[]={"aliases","command"};for(size_t I=0;I<2;I++){const xvalue* V=xrtValueObjectGet(Source,xrtStrView(Keys[I]));if(V)xrtValueObjectSet(Tool,xrtStrView(Keys[I]),V);}
        xrtMutexLock(g_MdoCli.Lock);xvalue* Previous=xrtValueDeepClone(g_MdoCli.State),*Old=CliFind(CliText(Source,"id",64));
        Ok=Previous&&CliUserDefined(Old)&&!xrtCancelRequested(Job->Cancel)&&MdoDistNumber(Job->Input,"revision")==MdoDistNumber(g_MdoCli.State,"revision");
        if(Ok){for(size_t I=0;I<xrtValueCount(CliItems());I++)if(xrtValueArrayGet(CliItems(),I)==Old){CliRetire(Old);xrtValueArraySet(CliItems(),I,Tool);break;}
            xrtValueObjectSetNew(g_MdoCli.State,XRT_STR_LITERAL("revision"),xrtValueUInt(MdoDistNumber(g_MdoCli.State,"revision")+1));Ok=CliSave();}
        else snprintf(Error,Capacity,"Tool list changed while copying; reload and retry");
        if(!Ok&&Previous){xrtValueRelease(g_MdoCli.State);g_MdoCli.State=Previous;Previous=NULL;}if(!Ok&&!Error[0])snprintf(Error,Capacity,"%s",g_MdoCli.BindingError);xrtValueRelease(Previous);xrtMutexUnlock(g_MdoCli.Lock);}
    if(!Ok&&Root[0]){size_t Count=0;MdoDistRemoveTree(Root,0,&Count);}if(Archive[0])MdoHomeRemove(Archive,false);xrtValueRelease(Product);xrtValueRelease(Tool);return Ok;
#endif
}
/* Upload only explicit selections. Both passes hash/stream bounded buffers;
 * the service seals the exact immutable digest before any version is visible. */
static bool CliCreatorUpload(MdoCliJob* Job,cstr Path,cstr Hash,uint64 Size,cstr Purpose,MdoAccountLease* Lease,char Upload[33],char* Error,size_t Capacity)
{
    xvalue* B=xrtValueObject();CliSet(B,"action","begin");CliSet(B,"purpose",Purpose);CliSet(B,"sha256",Hash);xrtValueObjectSetNew(B,XRT_STR_LITERAL("size"),xrtValueUInt(Size));
    xvalue* R=CliService("/api/v1/mdo/cli/upload","POST",B,Lease,Job->Cancel,Error,Capacity);xrtValueRelease(B);if(!R)return false;
    snprintf(Upload,33,"%s",CliText(R,"id",32));uint64 Chunk=MdoDistNumber(R,"chunk_bytes");xrtValueRelease(R);if(strlen(Upload)!=32||!Chunk||Chunk>65536)return false;
    xfile F=xrtOpen(Path,XFILE_READ);uint8 Buffer[65536];uint64 Offset=0;bool Ok=F!=NULL;CliTaskStatus(Job->Id,"uploading",NULL);
    while(Ok&&Offset<Size){size_t N=(size_t)(Size-Offset<Chunk?Size-Offset:Chunk);Ok=!xrtCancelRequested(Job->Cancel)&&CliReadExact(F,Buffer,N);if(!Ok)break;
        char* Enc=xrtBase64EncodeNew(Buffer,N,NULL);B=xrtValueObject();CliSet(B,"action","chunk");CliSet(B,"id",Upload);CliSet(B,"data",Enc?Enc:"");xrtFree(Enc);xrtValueObjectSetNew(B,XRT_STR_LITERAL("offset"),xrtValueUInt(Offset));R=CliService("/api/v1/mdo/cli/upload","POST",B,Lease,Job->Cancel,Error,Capacity);xrtValueRelease(B);Ok=R&&MdoDistNumber(R,"offset")==Offset+N;xrtValueRelease(R);if(Ok){Offset+=N;CliProgress(Offset,Size,Job);}}
    xrtClose(F);if(!Ok)return false;B=xrtValueObject();CliSet(B,"action","finish");CliSet(B,"id",Upload);R=CliService("/api/v1/mdo/cli/upload","POST",B,Lease,Job->Cancel,Error,Capacity);xrtValueRelease(B);Ok=R!=NULL;xrtValueRelease(R);return Ok;
}
static bool CliPublish(MdoCliJob* Job,char* Error,size_t Capacity)
{
#if defined(__ANDROID__)
    (void)Job;snprintf(Error,Capacity,"Publishing executable payloads requires a desktop device");return false;
#else
    const xvalue* Tool=xrtValueObjectGet(Job->Input,XRT_STR_LITERAL("tool"));MdoAccountLease Lease={0};
    if(!MdoAccountAcquireServiceWait(Job->Cancel,10000,&Lease)){snprintf(Error,Capacity,"Sign in before sharing a tool");return false;}
    char Relative[160]={0},Hash[65],EvidenceHash[65],Upload[33]={0},EvidenceUpload[33]={0};uint64 Size=0,EvidenceSize=0;char* Absolute=NULL;bool Ok=false;xvalue* Result=NULL;
    cstr Evidence=CliText(Job->Input,"review_file",4096);bool Publish=CliBool(Job->Input,"publish",false);
    Result=CliService("/api/v1/mdo/creator/profile","GET",NULL,&Lease,Job->Cancel,Error,Capacity);uint64 Limit=MdoDistNumber(Result,"version_bytes");xrtValueRelease(Result);Result=NULL;if(!Limit)goto done;
    if(Publish&&(strlen(CliText(Job->Input,"review_notes",8000))<10||(!Evidence[0]&&!CliText(Job->Input,"source_url",2000)[0]))){snprintf(Error,Capacity,"Provide source/provenance and build/security instructions before requesting publication");goto done;}
    if(Evidence[0]){xfileinfo Info;if(!xrtPathStat(Evidence,false,&Info)||Info.Type!=XFILE_TYPE_FILE||!CliFileHash(Evidence,EvidenceHash,&EvidenceSize,Job->Cancel)||!EvidenceSize){snprintf(Error,Capacity,"Select a regular source/review file; directories and links are not uploaded");goto done;}}
    if(!CliBuildArchive(Job,Tool,Relative,Hash,&Size,Error,Capacity))goto done;
    if(Size>Limit||EvidenceSize>Limit-Size){snprintf(Error,Capacity,"Artifact plus review files exceed creator capacity (%llu MiB)",(unsigned long long)(Limit/1048576));goto done;}
    Absolute=MdoHomeExternalPath(Relative);Ok=Absolute&&CliCreatorUpload(Job,Absolute,Hash,Size,"artifact",&Lease,Upload,Error,Capacity);if(!Ok)goto done;
    if(Evidence[0]){Ok=CliCreatorUpload(Job,Evidence,EvidenceHash,EvidenceSize,"review",&Lease,EvidenceUpload,Error,Capacity);if(!Ok)goto done;}
    xvalue* B=xrtValueObject();CliSet(B,"upload_id",Upload);CliSet(B,"evidence_upload",EvidenceUpload);CliSet(B,"review_notes",CliText(Job->Input,"review_notes",8000));CliSet(B,"source_url",CliText(Job->Input,"source_url",2000));xrtValueObjectSetNew(B,XRT_STR_LITERAL("publish"),xrtValueBool(Publish));
    Result=CliService("/api/v1/mdo/cli/submit","POST",B,&Lease,Job->Cancel,Error,Capacity);xrtValueRelease(B);Ok=Result!=NULL;
    if(Ok){xrtMutexLock(g_MdoCli.Lock);xvalue* T=CliTask(Job->Id);if(T)xrtValueObjectSet(T,XRT_STR_LITERAL("submission"),Result);xrtMutexUnlock(g_MdoCli.Lock);}
done:
    if(!Ok){const char* Ids[]={Upload,EvidenceUpload};for(int I=0;I<2;I++)if(Ids[I][0]){char Ignored[256]={0};xvalue* B=xrtValueObject();CliSet(B,"action","cancel");CliSet(B,"id",Ids[I]);xvalue* R=CliService("/api/v1/mdo/cli/upload","POST",B,&Lease,NULL,Ignored,sizeof(Ignored));xrtValueRelease(B);xrtValueRelease(R);}}
    xrtFree(Absolute);if(Relative[0])MdoHomeRemove(Relative,false);xrtValueRelease(Result);MdoAccountRelease(&Lease);return Ok;
#endif
}
static bool CliMine(MdoCliJob* Job,char* Error,size_t Capacity)
{
    MdoAccountLease Lease={0};if(!MdoAccountAcquireServiceWait(Job->Cancel,10000,&Lease)){snprintf(Error,Capacity,"Sign in before viewing submissions");return false;}
    xvalue* Result=CliService("/api/v1/mdo/cli/catalog?mine=1","GET",NULL,&Lease,Job->Cancel,Error,Capacity);bool Ok=Result!=NULL;
    if(Ok){xrtMutexLock(g_MdoCli.Lock);xrtValueObjectSet(g_MdoCli.Catalog,XRT_STR_LITERAL("mine"),xrtValueObjectGet(Result,XRT_STR_LITERAL("items")));xrtMutexUnlock(g_MdoCli.Lock);}xrtValueRelease(Result);MdoAccountRelease(&Lease);return Ok;
}
static bool CliWithdraw(MdoCliJob* Job,char* Error,size_t Capacity)
{
    MdoAccountLease Lease={0};if(!MdoAccountAcquireServiceWait(Job->Cancel,10000,&Lease)){snprintf(Error,Capacity,"Sign in before withdrawing a submission");return false;}
    xvalue* Result=CliService("/api/v1/mdo/cli/withdraw","POST",Job->Input,&Lease,Job->Cancel,Error,Capacity);bool Ok=Result!=NULL;xrtValueRelease(Result);MdoAccountRelease(&Lease);return Ok;
}
static bool CliRecommend(MdoCliJob* Job,char* Error,size_t Capacity)
{
#if defined(__ANDROID__)
    (void)Job;snprintf(Error,Capacity,"Install or update the full APK to add bundled tools");return false;
#else
    char Path[128];snprintf(Path,sizeof(Path),"/api/v1/mdo/cli/recommend?platform=%s",MdoToolPlatform());xvalue* Result=CliService(Path,"GET",NULL,NULL,Job->Cancel,Error,Capacity);if(!Result)return false;
    const xvalue* Items=xrtValueObjectGet(Result,XRT_STR_LITERAL("items"));bool Ok=xrtValueCount(Items)>0;
    xrtMutexLock(g_MdoCli.Lock);
    size_t Needed=0;
    for(size_t I=0;I<xrtValueCount(Items);I++){const xvalue* R=xrtValueArrayGet(Items,I);xvalue* Old=CliFind(CliText(R,"tool_id",64));if(!Old||!CliPresent(CliText(Old,"path",4096))||strcmp(CliText(Old,"version",64),CliText(R,"version",64)))Needed++;}
    if(Needed>64u-xrtValueCount(g_MdoCli.Tasks)){Ok=false;snprintf(Error,Capacity,"Clear finished download tasks before installing these tools");}
    for(size_t I=0;Ok&&I<xrtValueCount(Items);I++){const xvalue* Item=xrtValueArrayGet(Items,I);cstr Key=CliText(Item,"tool_id",64);xvalue* Old=CliFind(Key);
        if(Old&&CliPresent(CliText(Old,"path",4096))&&!strcmp(CliText(Old,"version",64),CliText(Item,"version",64))){xrtValueObjectSetNew(Old,XRT_STR_LITERAL("enabled"),xrtValueBool(true));continue;}
        xvalue* Request=xrtValueObject();CliSet(Request,"action","install");CliSet(Request,"name",CliText(Item,"name",120));xrtValueObjectSetNew(Request,XRT_STR_LITERAL("enable"),xrtValueBool(true));xrtValueObjectSet(Request,XRT_STR_LITERAL("store_id"),xrtValueObjectGet(Item,XRT_STR_LITERAL("id")));Ok=CliEnqueue(Request,Error,Capacity);xrtValueRelease(Request);
    }
    if(Ok)Ok=CliSave();
    xrtMutexUnlock(g_MdoCli.Lock);xrtValueRelease(Result);if(!Ok&&!Error[0])snprintf(Error,Capacity,"No compatible recommended tools are currently published");return Ok;
#endif
}
static int32 CliWorker(void* Data)
{
    (void)Data;
    while(!xrtThreadStopping()&&!xrtCancelRequested(g_MdoCli.Cancel)){
        xrtMutexLock(g_MdoCli.Lock);MdoCliJob* J=g_MdoCli.First;char Error[256]={0};bool Allowed=false;
        if(J){g_MdoCli.First=J->Next;if(!g_MdoCli.First)g_MdoCli.Last=NULL;J->Next=NULL;g_MdoCli.Running=J;Allowed=CliUserRequest(J->Input,Error,sizeof(Error));}xrtMutexUnlock(g_MdoCli.Lock);
        if(!J){xrtSleep(100);continue;}bool Ok=false;
        if(Allowed&&!xrtCancelRequested(J->Cancel)){CliTaskStatus(J->Id,"running",NULL);cstr Action=CliText(J->Input,"action",16);
            if(!strcmp(Action,"catalog"))Ok=CliCatalog(J,Error,sizeof(Error));else if(!strcmp(Action,"install"))Ok=CliInstall(J,Error,sizeof(Error));else if(!strcmp(Action,"import"))Ok=CliImport(J,Error,sizeof(Error));else if(!strcmp(Action,"publish"))Ok=CliPublish(J,Error,sizeof(Error));else if(!strcmp(Action,"recommend"))Ok=CliRecommend(J,Error,sizeof(Error));else if(!strcmp(Action,"mine"))Ok=CliMine(J,Error,sizeof(Error));else if(!strcmp(Action,"withdraw"))Ok=CliWithdraw(J,Error,sizeof(Error));}
        CliTaskStatus(J->Id,Ok?"complete":xrtCancelRequested(J->Cancel)?"cancelled":"failed",Ok?NULL:Error[0]?Error:"External-tool operation failed");
        xrtMutexLock(g_MdoCli.Lock);g_MdoCli.Running=NULL;
        cstr Action=CliText(J->Input,"action",16);if(Ok&&(!strcmp(Action,"catalog")||!strcmp(Action,"mine"))){for(size_t I=0;I<xrtValueCount(g_MdoCli.Tasks);I++)if(!strcmp(CliText(xrtValueArrayGet(g_MdoCli.Tasks,I),"id",32),J->Id)){xrtValueArrayRemove(g_MdoCli.Tasks,I,1);break;}(void)CliSaveTasks();}
        xrtMutexUnlock(g_MdoCli.Lock);if(J->WriteOwned)MdoApiWriteReleaseBackground();xrtValueRelease(J->Input);xrtCancelDestroy(J->Cancel);xrtFree(J);
    }return 0;
}
