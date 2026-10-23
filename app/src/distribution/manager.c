#include "../../include/mdo/time.h"
#include "../../include/mdo/distribution.h"
#include "../../include/mdo/home.h"
#include "../../include/mdo/version.h"
#include "../../include/mdo/ecosystem_package.h"
#include "../remote/net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void MdoApiLiveChanged(void* Data);
static struct {
    xmutex* Lock; xthread* Thread; xcancel *Cancel,*Operation; XS_ServerInfo* Server;
    xvalue *Catalog,*Active,*Tools,*CleanupEligible,*Error; bool Busy; unsigned Command;
    char ProbeDetail[256];
    uint64 Done,Total; char Stage[24],Id[16],Message[256];
} g_MdoDistribution;
static cstr MdoDistText(const xvalue* Object,cstr Key)
{
    xstrview Text; if(!xrtValueGetString(xrtValueObjectGet(Object,xrtStrView(Key)),&Text)||
        !Text.Data || Text.Size>4096 || memchr(Text.Data,0,Text.Size))return "";
    return Text.Data;
}
static uint64 MdoDistNumber(const xvalue* Object,cstr Key)
{
    uint64 Value=0; int64 Signed=0; xvalue* Number=xrtValueObjectGet(Object,xrtStrView(Key));
    if(!xrtValueGetUInt(Number,&Value)&&xrtValueGetInt(Number,&Signed)&&Signed>=0)Value=(uint64)Signed;
    return Value;
}
static bool MdoDistSet(xvalue* Object,cstr Key,cstr Text)
{return xrtValueObjectSetNew(Object,xrtStrView(Key),xrtValueString(xrtStrView(Text)));}
uint64 MdoBuildId(void)
{
    str Text=xrtEnvGet("MDO_BUILD_ID"); uint64 Id=0; if(Text && strlen(Text)==8)for(size_t i=0;i<8;i++){if(Text[i]<'0'||Text[i]>'9'){Id=0;break;}Id=Id*10+(unsigned)(Text[i]-'0');} xrtFree(Text);
    return Id>=10000000 && Id<=99999999?Id:MDO_BUILD_ID;
}
cstr MdoEdition(void)
{
#if defined(__ANDROID__)
    str Text=xrtEnvGet("MDO_EDITION"); bool Full=Text&&!strcmp(Text,"full"); xrtFree(Text); return Full?"full":"lite";
#elif defined(MDO_PRODUCT_EDITION)
    return MDO_PRODUCT_EDITION;
#else
    return "desktop";
#endif
}
cstr MdoToolPlatform(void)
{
#if defined(__ANDROID__)
    return "android-arm64-v8a";
#elif defined(_WIN32) || defined(_WIN64)
    return "windows-x86_64";
#elif defined(__linux__) && defined(__x86_64__)
    return "linux-x86_64";
#elif defined(__linux__) && defined(__aarch64__)
    return "linux-arm64";
#else
    return "unsupported";
#endif
}
static xvalue* MdoDistRead(cstr Path,size_t Limit)
{
    xfile File=!strcmp(Path,"cli-tools/official.json")?MdoResourceOpenRead(Path):MdoHomeOpenRead(Path); uint64 Size=0; size_t Got=0; char* Text=NULL; xvalue* Root=NULL;
    if(File && xrtFileSize(File,&Size)&&Size&&Size<=Limit) {
        Text=xrtMalloc((size_t)Size);
        if(Text&&xrtRead(File,Text,(size_t)Size,&Got)&&Got==Size) {
            xjsonreadconfig Config; xrtJsonReadConfigInit(&Config); Config.MaxInputBytes=Limit;
            Root=xrtJsonRead(xrtStrViewN(Text,Got),&Config);
        }
    }
    xrtFree(Text); xrtClose(File); return Root;
}
static bool MdoDistSave(cstr Path,xvalue* Value)
{
    size_t Size=0; str Text=xrtJsonStringify(Value,false,&Size);
    bool Ok=Text&&Size<=1048576&&MdoHomeAtomicWrite(Path,Text,Size,false); xrtFree(Text); return Ok;
}
static void MdoDistFailure(cstr Stage,cstr Code,cstr Detail,cstr File)
{
    xvalue* Error=xrtValueObject();MdoDistSet(Error,"stage",Stage);MdoDistSet(Error,"code",Code);
    MdoDistSet(Error,"detail",Detail);if(File)MdoDistSet(Error,"file",File);
    xrtMutexLock(g_MdoDistribution.Lock);
    /* Keep the first failing tool/file, rather than overwriting it during cleanup. */
    if(!g_MdoDistribution.Error){g_MdoDistribution.Error=Error;Error=NULL;}
    xrtMutexUnlock(g_MdoDistribution.Lock);xrtValueRelease(Error);
}
/* Legacy deployment inventory: file presence only, never execute a tool. */
static xvalue* MdoDistTools(xvalue* Active,bool Probe)
{
    (void)Probe;
    static cstr Names[]={"busybox","curl","jq","ssh","python","scp","sftp","aria2c","rg","7zip"};
    static cstr Products[]={"busybox","curl","jq","openssh","python","openssh","openssh","aria2","ripgrep","7zip"};
#if defined(_WIN32) || defined(_WIN64)
    static cstr Files[]={"busybox/busybox.exe","curl/curl.exe","jq/jq.exe","openssh/ssh.exe","python/python.exe","openssh/scp.exe","openssh/sftp.exe","aria2/aria2c.exe","ripgrep/rg.exe","7zip/7z.exe"};
#else
    static cstr Files[]={"busybox/busybox","curl/curl","jq/jq","openssh/ssh","python/bin/python3","openssh/scp","openssh/sftp","aria2/aria2c","ripgrep/rg","7zip/7zz"};
#endif
#if defined(__ANDROID__)
    str Root=xrtEnvGet("MDO_TOOLS_ROOT");
#else
    str Root=NULL;
#endif
    xvalue* Tools=xrtValueArray();
    for(size_t I=0;I<10;I++){
        cstr Group=I==4?"python":"core";str Base=Root?xrtStrDup(Root):MdoHomeExternalPath(MdoDistText(Active,Group));
        str Path=Base?xrtPathJoin(Base,Files[I]):NULL;
        if(Path&&(Root||MdoDistText(Active,Group)[0])&&xrtFileExists(Path)&&!xrtDirExists(Path)){
            char Key[32];snprintf(Key,sizeof(Key),"%s_metadata",Group);const xvalue* Versions=xrtValueObjectGet(xrtValueObjectGet(Active,xrtStrView(Key)),XRT_STR_LITERAL("tool_versions"));
            cstr Version=MdoDistText(Versions,Products[I]);xvalue* Tool=xrtValueObject();MdoDistSet(Tool,"id",Names[I]);MdoDistSet(Tool,"path",Path);MdoDistSet(Tool,"version",Version[0]?Version:"0.0.0");MdoDistSet(Tool,"verification","file-present");xrtValueArrayAppendNew(Tools,Tool);
        }xrtFree(Path);xrtFree(Base);
    }xrtFree(Root);return Tools;
}
static xvalue* MdoDistDetect(xvalue* Active){return MdoDistTools(Active,false);}
static xvalue* MdoDistInventory(xvalue* Active){return MdoDistTools(Active,false);}
static bool MdoDistValidRelative(cstr Path)
{
    if(!Path||!Path[0]||Path[0]=='/'||strlen(Path)>220||strchr(Path,'\\')||strchr(Path,':'))return false;
    const char* Start=Path;
    for(const char* p=Path;;p++)if(*p=='/'||!*p) {
        size_t n=(size_t)(p-Start); if(!n||(n==1&&Start[0]=='.')||(n==2&&!memcmp(Start,"..",2)))return false;
        if(!*p)break;
        Start=p+1;
    }
    return true;
}
#include "lifecycle.inc.c"
static bool MdoDistExtract(cstr Id,xvalue* Package,char Target[128])
{
    xfile File=MdoHomeOpenRead("data/toolpacks/download.pending"); uint64 Size=0;
    xvfspack Pack=NULL; xvfs Vfs=NULL; xvfsmount Mount=NULL; bytes Text=NULL; xvalue* Manifest=NULL; bool Ok=false;
    cstr FailedFile="toolpack.json",Code="archive",Detail="Unable to open the tool package archive";
    xvfspackoptions Options; xrtVfsPackOptionsInit(&Options);
    if(!File||!xrtFileSize(File,&Size))goto done;
    Pack=xrtVfsPackCreate(File,0,Size,&Options); if(!Pack)goto done; File=NULL;
    Vfs=xrtVfsCreate(); Mount=xrtVfsPackMount(Vfs,"/",0,XVFS_CASE_SENSITIVE,Pack,0);
    size_t Length=0; Text=Mount?xrtVfsReadAllLimit(Vfs,"/toolpack.json",1024*1024,&Length):NULL;
    if(Text)Manifest=xrtJsonParse(xrtStrViewN((cstr)Text,Length));
    xvalue* Files=xrtValueObjectGet(Manifest,XRT_STR_LITERAL("files"));
    Code="manifest";Detail="Tool package manifest does not match the catalog";
    if(!Files||xrtValueType(Files)!=XVALUE_ARRAY || xrtValueCount(Files)>10000 ||
       strcmp(MdoDistText(Manifest,"id"),Id) || strcmp(MdoDistText(Manifest,"platform"),MdoToolPlatform()) ||
       MdoDistNumber(Manifest,"revision")!=MdoDistNumber(Package,"revision"))goto done;
    snprintf(Target,128,"data/toolpacks/%s/%llu-%.12s-%llu",Id,(unsigned long long)MdoDistNumber(Package,"revision"),MdoDistText(Package,"sha256"),(unsigned long long)MdoMonotonicUs());
    Code="write";Detail="Unable to create the tool package directory";
    if(!MdoHomeCreateDirectory(Target))goto done;
    MdoDistProgress(0,xrtValueCount(Files),"extracting");
    for(size_t i=0;i<xrtValueCount(Files);i++) {
        cstr Path=MdoDistText(xrtValueArrayGet(Files,i),"path"); uint64 Expected=MdoDistNumber(xrtValueArrayGet(Files,i),"size");
        cstr Hash=MdoDistText(xrtValueArrayGet(Files,i),"sha256");
        char Virtual[256],Relative[384],Hex[65]; xfile Input=NULL,Output=NULL; uint8 Buffer[65536],Digest[32]; uint64 Total=0; xsha256 Sha;
        FailedFile=Path;Code="manifest";Detail="Invalid file metadata in the tool package";
        if(!MdoDistValidRelative(Path)||(!Expected && strncmp(MdoToolPlatform(),"linux-",6))||Expected>268435456||strlen(Hash)!=64)goto done;
        snprintf(Virtual,sizeof(Virtual),"/%s",Path); snprintf(Relative,sizeof(Relative),"%s/%s",Target,Path);
        xfileoptions FileOptions; xrtFileOptionsInit(&FileOptions); FileOptions.Flags=XFILE_READ;
        Input=xrtVfsOpen(Vfs,Virtual,&FileOptions); Output=MdoHomeOpenWrite(Relative,XFILE_WRITE|XFILE_CREATE|XFILE_TRUNCATE);
        Code="write";Detail="Unable to extract or write the tool package file";
        bool Copied=Input&&Output; xrtSha256Init(&Sha);
        while(Copied) {size_t Got=0,Written=0; Copied=!xrtCancelRequested(g_MdoDistribution.Operation)&&xrtRead(Input,Buffer,sizeof(Buffer),&Got);
            if(!Copied||!Got)break;
            Total+=Got;
            Copied=Total<=Expected && xrtSha256Update(&Sha,Buffer,Got) && xrtWrite(Output,Buffer,Got,&Written)&&Written==Got;
        }
        Copied=Copied&&Total==Expected&&xrtSha256Final(&Sha,Digest)&&xrtFlush(Output);
        if(Copied) {for(size_t j=0;j<32;j++)snprintf(Hex+j*2,3,"%02x",Digest[j]); Copied=!strcmp(Hex,Hash);if(!Copied){Code="file_checksum";Detail="Extracted file SHA-256 does not match its manifest";}}
        xrtClose(Input); xrtClose(Output); if(!Copied)goto done;
#if defined(__linux__) && !defined(__ANDROID__)
        /* XRTPACK has no native mode metadata. Honor only the two permitted
         * manifest modes after byte verification, before any functional probe. */
        uint64 Mode=MdoDistNumber(xrtValueArrayGet(Files,i),"mode");
        bool ModeOk=(Mode==0644 || Mode==0755) && MdoHomeSetMode(Relative,(uint32)Mode);
        if(!ModeOk){Code="file_mode";Detail="Invalid or unwritable Linux file permissions";goto done;}
#endif
        MdoDistProgress(i+1,xrtValueCount(Files),"extracting");
    }
    char Receipt[160];snprintf(Receipt,sizeof(Receipt),"%s/toolpack.receipt.json",Target);
    FailedFile="toolpack.receipt.json";Code="write";Detail="Unable to save the tool package receipt";Ok=MdoDistSave(Receipt,Manifest);
done:
    if(!Ok)MdoDistFailure("extracting",Code,Detail,FailedFile);
    xrtValueRelease(Manifest); xrtFree(Text); xrtVfsMountDestroy(Mount); xrtVfsDestroy(Vfs); xrtVfsPackDestroy(Pack); xrtClose(File); return Ok;
}
static bool MdoDistFetch(void)
{
    XS_FetchRequest Request={0}; XS_FetchResponse Response={0}; char Url[256];
    snprintf(Url,sizeof(Url),MDO_DISTRIBUTION_ORIGIN "/mdo/catalog?platform=%s&edition=%s&build_id=%llu",MdoToolPlatform(),MdoEdition(),(unsigned long long)MdoBuildId());
    Request.Size=sizeof(Request); Request.Version=XS_FETCH_REQUEST_VERSION; Request.Url=Url;
    Request.MaxBodyBytes=1048576; Request.Timeout=15000000; Request.IdleTimeout=15000000; Request.Cancel=g_MdoDistribution.Cancel;
    Response.Size=sizeof(Response); bool Ok=xsFetch(&Request,&Response)&&Response.Status==200;
    xvalue* Root=Ok?xrtJsonParse(xrtStrViewN((cstr)Response.Body,Response.BodySize)):NULL;
    Ok=Root && xrtValueType(Root)==XVALUE_OBJECT && xrtValueType(xrtValueObjectGet(Root,XRT_STR_LITERAL("notices")))==XVALUE_ARRAY &&
        xrtValueCount(xrtValueObjectGet(Root,XRT_STR_LITERAL("notices")))<=100 &&
        xrtValueType(xrtValueObjectGet(Root,XRT_STR_LITERAL("toolpacks")))==XVALUE_ARRAY &&
        xrtValueCount(xrtValueObjectGet(Root,XRT_STR_LITERAL("toolpacks")))<=16;
    if(Ok) {
        bool ImportAvailable = false, PreserveCache = false;
        /* Passive notices must not create Home or make a cache-only Home
         * ineligible for legacy import. Keep the catalog in memory until
         * user data has already made Home persistent. */
        if (MdoHomeImportInspect(&ImportAvailable, &PreserveCache) && !ImportAvailable)
            MdoDistSave("data/notifications/catalog.json",Root);
        xrtMutexLock(g_MdoDistribution.Lock); xrtValueRelease(g_MdoDistribution.Catalog); g_MdoDistribution.Catalog=Root; Root=NULL; xrtMutexUnlock(g_MdoDistribution.Lock);
    }
    if(Ok)MdoApiLiveChanged(NULL);
    xrtValueRelease(Root); xsFetchResponseUnit(&Response); return Ok;
}
static bool MdoDistInstall(cstr Id)
{
#if defined(__ANDROID__)
    (void)Id; return false; /* Native updates go through the matching full APK. */
#else
    xrtMutexLock(g_MdoDistribution.Lock); xvalue* Catalog=xrtValueDeepClone(g_MdoDistribution.Catalog);
    xvalue* Active=xrtValueDeepClone(g_MdoDistribution.Active); xrtMutexUnlock(g_MdoDistribution.Lock);
    xvalue* Packages=xrtValueObjectGet(Catalog,XRT_STR_LITERAL("toolpacks")); xvalue* Package=NULL;
    for(size_t i=0;i<xrtValueCount(Packages);i++)if(!strcmp(MdoDistText(xrtValueArrayGet(Packages,i),"id"),Id))Package=xrtValueArrayGet(Packages,i);
    char Path[96],Target[128]={0}; cstr Hash=MdoDistText(Package,"sha256");
    snprintf(Path,sizeof(Path),"/mdo/blob/%s",Hash);
    MdoTransferReport Report={0};bool Ok=MdoDistCompatible(Package,Active) && strlen(Hash)==64;
    /* Remove only an abandoned partial attempt, never reuse unchecked bytes. */
    MdoHomeRemove("data/toolpacks/download.pending",false);
    if(!Ok)MdoDistFailure("queued","metadata","No compatible tool package is available",NULL);
    if(Ok) {
        Ok=MdoTransferDownloadReport(g_MdoDistribution.Server->Engine,Path,"data/toolpacks/download.pending",MdoDistNumber(Package,"size"),Hash,g_MdoDistribution.Operation,MdoDistDownloadProgress,NULL,&Report);
        if(!Ok){MdoDistFailure("downloading",Report.Code,Report.Detail,NULL);
            xrtMutexLock(g_MdoDistribution.Lock);xvalue* Error=g_MdoDistribution.Error;
            MdoDistSet(Error,"expected_sha256",Hash);MdoDistSet(Error,"actual_sha256",Report.ActualHash);
            xrtValueObjectSetNew(Error,XRT_STR_LITERAL("expected_bytes"),xrtValueUInt(MdoDistNumber(Package,"size")));
            xrtValueObjectSetNew(Error,XRT_STR_LITERAL("actual_bytes"),xrtValueUInt(Report.Bytes));xrtMutexUnlock(g_MdoDistribution.Lock);}
    }
    if(Ok)Ok=MdoDistExtract(Id,Package,Target);
    if(Ok) {
        char RevisionKey[32]; snprintf(RevisionKey,sizeof(RevisionKey),"%s_revision",Id);
        MdoDistRetire(Active,Id);
        xrtValueObjectSetNew(Active,xrtStrView(RevisionKey),xrtValueUInt(MdoDistNumber(Package,"revision")));
        snprintf(RevisionKey,sizeof(RevisionKey),"%s_metadata",Id);xrtValueObjectSetNew(Active,xrtStrView(RevisionKey),MdoDistMetadata(Package));
        MdoDistSet(Active,Id,Target);MdoDistProgress(0,1,"activating");xvalue* Tools=MdoDistInventory(Active);
        xrtValueObjectSetNew(Active,XRT_STR_LITERAL("capability_revision"),xrtValueUInt(MdoDistNumber(Active,"capability_revision")+1));
        Ok=Tools && MdoDistInstalledCompatible(Active) && !xrtCancelRequested(g_MdoDistribution.Operation) && MdoDistSave("data/toolpacks/active.json",Active);
        if(!Ok)MdoDistFailure("activating","activate","Unable to activate the verified tool package",NULL);
        if(Ok) {
            xrtMutexLock(g_MdoDistribution.Lock); xrtValueRelease(g_MdoDistribution.Active); g_MdoDistribution.Active=Active; Active=NULL;
                xrtValueRelease(g_MdoDistribution.Tools); g_MdoDistribution.Tools=Tools; Tools=NULL; xrtMutexUnlock(g_MdoDistribution.Lock);
        }
        xrtValueRelease(Tools);
    }
    if(!Ok&&Target[0]){size_t Count=0;(void)MdoDistRemoveTree(Target,0,&Count);}
    bool PendingKept=false;
    if(!Ok&&!xrtCancelRequested(g_MdoDistribution.Operation)) {
        xfile Download=MdoHomeOpenRead("data/toolpacks/download.pending");bool Retained=false;
        if(Download){xrtClose(Download);MdoHomeRemove("data/toolpacks/download.failed",false);Retained=MdoHomeRenameNoReplace("data/toolpacks/download.pending","data/toolpacks/download.failed");PendingKept=!Retained;}
        xrtMutexLock(g_MdoDistribution.Lock);xvalue* Error=xrtValueDeepClone(g_MdoDistribution.Error);xrtMutexUnlock(g_MdoDistribution.Lock);
        MdoDistSet(Error,"package",Id);MdoDistSet(Error,"expected_sha256",Hash);
        MdoDistSet(Error,"actual_sha256",Report.ActualHash);
        xrtValueObjectSetNew(Error,XRT_STR_LITERAL("expected_bytes"),xrtValueUInt(MdoDistNumber(Package,"size")));
        xrtValueObjectSetNew(Error,XRT_STR_LITERAL("actual_bytes"),xrtValueUInt(Report.Bytes));
        if(Retained)MdoDistSet(Error,"download","data/toolpacks/download.failed");
        else if(PendingKept)MdoDistSet(Error,"download","data/toolpacks/download.pending");
        MdoDistSave("data/toolpacks/last-error.json",Error);
        xrtMutexLock(g_MdoDistribution.Lock);xrtValueRelease(g_MdoDistribution.Error);g_MdoDistribution.Error=Error;xrtMutexUnlock(g_MdoDistribution.Lock);
    } else if(Ok) {MdoHomeRemove("data/toolpacks/download.failed",false);MdoHomeRemove("data/toolpacks/last-error.json",false);}
    if(!PendingKept) MdoHomeRemove("data/toolpacks/download.pending",false);
    xrtValueRelease(Catalog);
    xrtValueRelease(Active);
    return Ok;
#endif
}
static bool MdoDistInvalidate(bool Binary,xbytesview Message,void* Data)
{(void)Message; if(Binary)return false; *(bool*)Data=true; return true;}
static int32 MdoDistWorker(void* Data)
{
    (void)Data; MdoRemoteNet Net={0}; MdoRemoteSocket* Socket=NULL; uint64 Next=0,Retry=0;
    while(!xrtThreadStopping()&&!xrtCancelRequested(g_MdoDistribution.Cancel)) {
        unsigned Command; char Id[16]; bool Dirty=false;
        xrtMutexLock(g_MdoDistribution.Lock); Command=g_MdoDistribution.Command; g_MdoDistribution.Command=0;
        snprintf(Id,sizeof(Id),"%s",g_MdoDistribution.Id); xrtMutexUnlock(g_MdoDistribution.Lock);
        if(Socket&&!MdoRemoteSocketPoll(Socket,MdoDistInvalidate,&Dirty)) {MdoRemoteSocketDestroy(Socket); Socket=NULL; Retry=MdoMonotonicUs()+30000000;}
        if(Command || Dirty || MdoMonotonicUs()>=Next) {
            bool Ok=Command==2?MdoDistInstall(Id):Command>=3?MdoDistMaintain(Command,Id):MdoDistFetch(); Next=MdoMonotonicUs()+300000000;
            if(Ok&&Command>=2)MdoCliLegacyChanged();
            if(Ok&&Command>=3){MdoHomeRemove("data/toolpacks/download.failed",false);MdoHomeRemove("data/toolpacks/last-error.json",false);}
            xrtMutexLock(g_MdoDistribution.Lock); bool Cancelled=xrtCancelRequested(g_MdoDistribution.Operation);if(Command)g_MdoDistribution.Busy=false; if(Command>=2) {xrtCancelDestroy(g_MdoDistribution.Operation); g_MdoDistribution.Operation=NULL;}
            if(Cancelled){xrtValueRelease(g_MdoDistribution.Error);g_MdoDistribution.Error=NULL;}
            bool Preserve=Command<2 && g_MdoDistribution.Error;
            if(!Preserve)snprintf(g_MdoDistribution.Stage,sizeof(g_MdoDistribution.Stage),"%s",Ok?"complete":Cancelled?"cancelled":"failed");
            if(Ok&&Command>=2)g_MdoDistribution.Done=g_MdoDistribution.Total;
            if(!Preserve)snprintf(g_MdoDistribution.Message,sizeof(g_MdoDistribution.Message),"%s",Ok||Cancelled?"":g_MdoDistribution.Error?MdoDistText(g_MdoDistribution.Error,"detail"):Command>=2?"Tool package operation failed":"Online service unavailable; cached notices retained");
            xrtMutexUnlock(g_MdoDistribution.Lock);
        }
        if(!Socket&&MdoMonotonicUs()>=Retry) {
            if(!Net.Engine)MdoRemoteNetInit(&Net,g_MdoDistribution.Server->Engine,NULL);
            MdoRemoteSocketConfig Config={MDO_DISTRIBUTION_HOST,MDO_DISTRIBUTION_PORT,MDO_DISTRIBUTION_SECURE,
                "/mdo/push",MDO_DISTRIBUTION_ORIGIN,"mdo.notifications.v1","mdo.notifications.v1",4096};
            Socket=MdoRemoteSocketOpen(&Net,&Config,g_MdoDistribution.Cancel,NULL); Retry=MdoMonotonicUs()+60000000;
        }
        xrtSleep(100);
    }
    MdoRemoteSocketDestroy(Socket); MdoRemoteNetUnit(&Net); return 0;
}
bool MdoDistributionInit(XS_ServerInfo* Server)
{
    if(g_MdoDistribution.Lock)return true;
    g_MdoDistribution.Lock=xrtMutexCreate(); g_MdoDistribution.Cancel=xrtCancelCreate(); g_MdoDistribution.Server=Server;
    g_MdoDistribution.Active=MdoDistRead("data/toolpacks/active.json",1048576);
    if(xrtValueType(g_MdoDistribution.Active)!=XVALUE_OBJECT) {xrtValueRelease(g_MdoDistribution.Active);g_MdoDistribution.Active=xrtValueObject();}
    g_MdoDistribution.Catalog=MdoDistRead("data/notifications/catalog.json",1048576);
    if(xrtValueType(g_MdoDistribution.Catalog)!=XVALUE_OBJECT) {xrtValueRelease(g_MdoDistribution.Catalog);g_MdoDistribution.Catalog=xrtValueObject();}
    g_MdoDistribution.Tools=MdoDistDetect(g_MdoDistribution.Active);
    g_MdoDistribution.CleanupEligible=xrtValueDeepClone(xrtValueObjectGet(g_MdoDistribution.Active,XRT_STR_LITERAL("retired")));
    g_MdoDistribution.Error=MdoDistRead("data/toolpacks/last-error.json",16384);
    if(xrtValueType(g_MdoDistribution.Error)!=XVALUE_OBJECT){xrtValueRelease(g_MdoDistribution.Error);g_MdoDistribution.Error=NULL;}
    if(g_MdoDistribution.Error){snprintf(g_MdoDistribution.Stage,sizeof(g_MdoDistribution.Stage),"failed");snprintf(g_MdoDistribution.Message,sizeof(g_MdoDistribution.Message),"%s",MdoDistText(g_MdoDistribution.Error,"detail"));snprintf(g_MdoDistribution.Id,sizeof(g_MdoDistribution.Id),"%s",MdoDistText(g_MdoDistribution.Error,"package"));}
    if(!g_MdoDistribution.Lock||!g_MdoDistribution.Cancel||!g_MdoDistribution.Tools)return false;
    if(!MdoCliInit(Server,g_MdoDistribution.Tools,g_MdoDistribution.Active))return false;
    g_MdoDistribution.Thread=xrtThreadCreate(MdoDistWorker,NULL,0); return g_MdoDistribution.Thread!=NULL;
}
void MdoDistributionUnit(void)
{
    MdoCliUnit();
    xrtCancelRequest(g_MdoDistribution.Cancel); if(g_MdoDistribution.Thread) {xrtThreadStop(g_MdoDistribution.Thread); xrtThreadWait(g_MdoDistribution.Thread); xrtThreadDestroy(g_MdoDistribution.Thread);}
    xrtValueRelease(g_MdoDistribution.Catalog); xrtValueRelease(g_MdoDistribution.Active); xrtValueRelease(g_MdoDistribution.Tools);
    xrtValueRelease(g_MdoDistribution.CleanupEligible);
    xrtValueRelease(g_MdoDistribution.Error);
    xrtCancelDestroy(g_MdoDistribution.Operation); xrtCancelDestroy(g_MdoDistribution.Cancel); xrtMutexDestroy(g_MdoDistribution.Lock); memset(&g_MdoDistribution,0,sizeof(g_MdoDistribution));
}
bool MdoDistributionRequest(cstr Action,cstr Id)
{
    if(!g_MdoDistribution.Lock)return false;
    if(!strcmp(Action,"cancel")) {xrtMutexLock(g_MdoDistribution.Lock); xrtCancelRequest(g_MdoDistribution.Operation); xrtMutexUnlock(g_MdoDistribution.Lock); return true;}
    unsigned Command=!strcmp(Action,"check")?1:(!strcmp(Action,"install")||!strcmp(Action,"repair"))?2:!strcmp(Action,"uninstall")?3:!strcmp(Action,"rollback")?4:!strcmp(Action,"cleanup")?5:0;
    bool Installable=!strcmp(MdoToolPlatform(),"windows-x86_64")||!strncmp(MdoToolPlatform(),"linux-",6);
    if(!Command || (Command>=2 && (!Installable || (Command!=5&&strcmp(Id,"core")&&strcmp(Id,"python")))))return false;
    xrtMutexLock(g_MdoDistribution.Lock); bool Ok=!g_MdoDistribution.Busy&&!xrtCancelRequested(g_MdoDistribution.Cancel);
    if(Ok && Command>=2) {g_MdoDistribution.Operation=xrtCancelChild(g_MdoDistribution.Cancel); Ok=g_MdoDistribution.Operation!=NULL;}
    if(Ok) {if(Command>=2){xrtValueRelease(g_MdoDistribution.Error);g_MdoDistribution.Error=NULL;g_MdoDistribution.Message[0]=0;}g_MdoDistribution.Busy=true; g_MdoDistribution.Done=g_MdoDistribution.Total=0;if(Command>=2||!g_MdoDistribution.Error){snprintf(g_MdoDistribution.Stage,sizeof(g_MdoDistribution.Stage),"queued");snprintf(g_MdoDistribution.Id,sizeof(g_MdoDistribution.Id),"%s",Id);}g_MdoDistribution.Command=Command;}
    xrtMutexUnlock(g_MdoDistribution.Lock); return Ok;
}
xvalue* MdoDistributionSnapshot(void)
{
    if(!g_MdoDistribution.Lock)return NULL;
    xrtMutexLock(g_MdoDistribution.Lock); xvalue* Root=xrtValueDeepClone(g_MdoDistribution.Catalog);
    xvalue* Tools=xrtValueArray();
    for(size_t i=0;i<xrtValueCount(g_MdoDistribution.Tools);i++){xvalue* Tool=xrtValueArrayGet(g_MdoDistribution.Tools,i);if(xrtFileExists(MdoDistText(Tool,"path")))xrtValueArrayAppendNew(Tools,xrtValueDeepClone(Tool));}
    xrtValueObjectSetNew(Root,XRT_STR_LITERAL("tools"),Tools);
    xrtValueObjectSetNew(Root,XRT_STR_LITERAL("installed"),xrtValueDeepClone(g_MdoDistribution.Active));
    xrtValueObjectSetNew(Root,XRT_STR_LITERAL("busy"),xrtValueBool(g_MdoDistribution.Busy)); MdoDistSet(Root,"message",g_MdoDistribution.Message);
    if(g_MdoDistribution.Error)xrtValueObjectSetNew(Root,XRT_STR_LITERAL("error"),xrtValueDeepClone(g_MdoDistribution.Error));
    MdoDistSet(Root,"stage",g_MdoDistribution.Stage);MdoDistSet(Root,"operation_id",g_MdoDistribution.Id);
    xrtValueObjectSetNew(Root,XRT_STR_LITERAL("done"),xrtValueUInt(g_MdoDistribution.Done));
    xrtValueObjectSetNew(Root,XRT_STR_LITERAL("total"),xrtValueUInt(g_MdoDistribution.Total));
    xrtValueObjectSetNew(Root,XRT_STR_LITERAL("capability_revision"),xrtValueUInt(MdoDistNumber(g_MdoDistribution.Active,"capability_revision")));
    xrtValueObjectSetNew(Root,XRT_STR_LITERAL("cleanup_available"),xrtValueUInt(xrtValueCount(g_MdoDistribution.CleanupEligible)));
    xrtMutexUnlock(g_MdoDistribution.Lock);
    xrtValueObjectSetNew(Root,XRT_STR_LITERAL("build_id"),xrtValueUInt(MdoBuildId()));
    xrtValueObjectSetNew(Root,XRT_STR_LITERAL("bundled_toolpack_revision"),xrtValueUInt(!strcmp(MdoEdition(),"full")?MDO_TOOLPACK_REVISION:0));
    MdoDistSet(Root,"edition",MdoEdition()); MdoDistSet(Root,"platform",MdoToolPlatform()); return Root;
}
#include "ecosystem.inc.c"
#include "cli.inc.c"
bool MdoToolPrompt(char** Prompt)
{return MdoCliToolPrompt(Prompt);}
