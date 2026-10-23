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
    xrtMutexLock(g_MdoCli.Lock);xrtValueRelease(g_MdoCli.Catalog);g_MdoCli.Catalog=Catalog;xrtMutexUnlock(g_MdoCli.Lock);return true;
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
    xrtMutexLock(g_MdoCli.Lock);xvalue* Previous=xrtValueClone(g_MdoCli.State),*Old=CliFind(Key);bool Enable=CliBool(Job->Input,"enable",Old?CliBool(Old,"enabled",true):true);
    if(Old){/* A managed update never silently replaces a user-selected program. */
        if(strcmp(CliText(Old,"source",16),"store")&&strcmp(CliText(Old,"source",16),"legacy")){Ok=false;snprintf(Error,Capacity,"Tool identity belongs to a user-defined program");}
        else {xrtValueObjectSetNew(Tool,XRT_STR_LITERAL("enabled"),xrtValueBool(Enable));for(size_t I=0;I<xrtValueCount(CliItems());I++)if(xrtValueArrayGet(CliItems(),I)==Old){CliRetire(Old);xrtValueArraySet(CliItems(),I,Tool);break;}}
    }else if(xrtValueCount(CliItems())>=256){Ok=false;snprintf(Error,Capacity,"External-tool list is full");}else xrtValueArrayAppend(CliItems(),Tool);
    if(Ok){xrtValueObjectSetNew(g_MdoCli.State,XRT_STR_LITERAL("revision"),xrtValueUInt(MdoDistNumber(g_MdoCli.State,"revision")+1));Ok=!xrtCancelRequested(Job->Cancel)&&CliSave();}
    if(!Ok){xrtValueRelease(g_MdoCli.State);g_MdoCli.State=Previous;Previous=NULL;}xrtValueRelease(Previous);xrtMutexUnlock(g_MdoCli.Lock);
    if(!Ok&&Root[0]){size_t Count=0;(void)MdoDistRemoveTree(Root,0,&Count);}
done:xrtValueRelease(Product);xrtValueRelease(Tool);return Ok;
#endif
}
static bool CliPublish(MdoCliJob* Job,char* Error,size_t Capacity)
{
#if defined(__ANDROID__)
    (void)Job;snprintf(Error,Capacity,"Publishing executable payloads requires a desktop device");return false;
#else
    const xvalue* Tool=xrtValueObjectGet(Job->Input,XRT_STR_LITERAL("tool"));MdoAccountLease Lease={0};
    if(!MdoAccountAcquireServiceWait(Job->Cancel,10000,&Lease)){snprintf(Error,Capacity,"Sign in before sharing a tool");return false;}
    char Relative[160]={0},Hash[65],Path[256];uint64 Size=0;bool Ok=CliBuildArchive(Job,Tool,Relative,Hash,&Size,Error,Capacity);xvalue* Result=NULL;char Upload[33]={0};
    if(!Ok)goto done;
    xvalue* Body=xrtValueObject();CliSet(Body,"action","begin");CliSet(Body,"sha256",Hash);xrtValueObjectSetNew(Body,XRT_STR_LITERAL("size"),xrtValueUInt(Size));
    Result=CliService("/api/v1/mdo/cli/upload","POST",Body,&Lease,Job->Cancel,Error,Capacity);xrtValueRelease(Body);if(!Result){Ok=false;goto done;}
    snprintf(Upload,sizeof(Upload),"%s",CliText(Result,"id",32));uint64 Chunk=MdoDistNumber(Result,"chunk_bytes");xrtValueRelease(Result);Result=NULL;
    if(strlen(Upload)!=32||!Chunk||Chunk>65536){Ok=false;goto done;}
    xfile Input=MdoHomeOpenRead(Relative);uint8 Buffer[65536];uint64 Offset=0;Ok=Input!=NULL;CliTaskStatus(Job->Id,"uploading",NULL);
    while(Ok&&Offset<Size){size_t N=(size_t)((Size-Offset)<Chunk?Size-Offset:Chunk);Ok=!xrtCancelRequested(Job->Cancel)&&CliReadExact(Input,Buffer,N);if(!Ok)break;
        char* Base64=xrtBase64EncodeNew(Buffer,N,NULL);Body=xrtValueObject();CliSet(Body,"action","chunk");CliSet(Body,"id",Upload);CliSet(Body,"data",Base64?Base64:"");xrtFree(Base64);xrtValueObjectSetNew(Body,XRT_STR_LITERAL("offset"),xrtValueUInt(Offset));
        Result=CliService("/api/v1/mdo/cli/upload","POST",Body,&Lease,Job->Cancel,Error,Capacity);xrtValueRelease(Body);Ok=Result&&MdoDistNumber(Result,"offset")==Offset+N;xrtValueRelease(Result);Result=NULL;if(Ok){Offset+=N;CliProgress(Offset,Size,Job);}}
    xrtClose(Input);if(!Ok)goto done;
    Body=xrtValueObject();CliSet(Body,"action","finish");CliSet(Body,"id",Upload);Result=CliService("/api/v1/mdo/cli/upload","POST",Body,&Lease,Job->Cancel,Error,Capacity);xrtValueRelease(Body);Ok=Result!=NULL;xrtValueRelease(Result);Result=NULL;if(!Ok)goto done;
    Body=xrtValueObject();CliSet(Body,"upload_id",Upload);snprintf(Path,sizeof(Path),"/api/v1/mdo/cli/submit");Result=CliService(Path,"POST",Body,&Lease,Job->Cancel,Error,Capacity);xrtValueRelease(Body);Ok=Result!=NULL;
    if(Ok){xrtMutexLock(g_MdoCli.Lock);xvalue* T=CliTask(Job->Id);if(T)xrtValueObjectSet(T,XRT_STR_LITERAL("submission"),Result);xrtMutexUnlock(g_MdoCli.Lock);}
done:if(Relative[0])MdoHomeRemove(Relative,false);xrtValueRelease(Result);MdoAccountRelease(&Lease);return Ok;
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
        xrtMutexLock(g_MdoCli.Lock);MdoCliJob* J=g_MdoCli.First;
        if(J){g_MdoCli.First=J->Next;if(!g_MdoCli.First)g_MdoCli.Last=NULL;J->Next=NULL;g_MdoCli.Running=J;}xrtMutexUnlock(g_MdoCli.Lock);
        if(!J){xrtSleep(100);continue;}char Error[256]={0};bool Ok=false;
        if(!xrtCancelRequested(J->Cancel)){CliTaskStatus(J->Id,"running",NULL);cstr Action=CliText(J->Input,"action",16);
            if(!strcmp(Action,"catalog"))Ok=CliCatalog(J,Error,sizeof(Error));else if(!strcmp(Action,"install"))Ok=CliInstall(J,Error,sizeof(Error));else if(!strcmp(Action,"publish"))Ok=CliPublish(J,Error,sizeof(Error));else if(!strcmp(Action,"recommend"))Ok=CliRecommend(J,Error,sizeof(Error));else if(!strcmp(Action,"mine"))Ok=CliMine(J,Error,sizeof(Error));else if(!strcmp(Action,"withdraw"))Ok=CliWithdraw(J,Error,sizeof(Error));}
        CliTaskStatus(J->Id,Ok?"complete":xrtCancelRequested(J->Cancel)?"cancelled":"failed",Ok?NULL:Error[0]?Error:"External-tool operation failed");
        xrtMutexLock(g_MdoCli.Lock);g_MdoCli.Running=NULL;
        cstr Action=CliText(J->Input,"action",16);if(Ok&&(!strcmp(Action,"catalog")||!strcmp(Action,"mine"))){for(size_t I=0;I<xrtValueCount(g_MdoCli.Tasks);I++)if(!strcmp(CliText(xrtValueArrayGet(g_MdoCli.Tasks,I),"id",32),J->Id)){xrtValueArrayRemove(g_MdoCli.Tasks,I,1);break;}(void)CliSaveTasks();}
        xrtMutexUnlock(g_MdoCli.Lock);if(J->WriteOwned)MdoApiWriteReleaseBackground();xrtValueRelease(J->Input);xrtCancelDestroy(J->Cancel);xrtFree(J);
    }return 0;
}
