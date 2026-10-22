#include "internal.h"
#include "../../include/mdo/extension_files.h"
#include "../../include/mdo/ecosystem_package.h"
#include "../../include/mdo/modules.h"
#include "../../include/mdo/time.h"

typedef struct SdkPluginMount {xvfsdisk Disk;xvfsmount Mount;} SdkPluginMount;
typedef struct SdkPlugin {xvalue* Manifest;SdkPluginMount Mounts[128];size_t Count;char Error[256];} SdkPlugin;
static struct {xmutex* Lock;xvalue* Config;SdkPlugin Plugins[32];bool Busy,Recovery;} SdkPlugins;
static const cstr SdkPluginMethods[]={"plugin.list","plugin.apply","plugin.enable","plugin.disable","plugin.remove","plugin.rollback","runtime.reload","runtime.reload.status"};
static void SdkPluginDrop(SdkPlugin* P){
    for(size_t i=0;i<P->Count;i++){if(P->Mounts[i].Mount){xrtVfsUnmount(P->Mounts[i].Mount);xrtVfsMountDestroy(P->Mounts[i].Mount);}xrtVfsDiskDestroy(P->Mounts[i].Disk);}
    xrtValueRelease(P->Manifest);memset(P,0,sizeof(*P));
}
static bool SdkPluginEnabled(const xvalue* M){bool Enabled=true;xrtValueGetBool(SdkGet(M,"enabled"),&Enabled);return Enabled;}
static bool SdkPluginVerifyFile(cstr Id,cstr Revision,const xvalue* Entry){
    cstr Name=SdkField(Entry,"path"),Hash=SdkField(Entry,"sha256");if(!MdoPackagePath(Name)||!Hash||strlen(Hash)!=64)return false;
    char Path[600];snprintf(Path,sizeof(Path),"plugins/%s/revisions/%s/files/%s",Id,Revision,Name);
    xfile File=MdoHomeOpenRead(Path);if(!File)return false;xfileinfo Info={0};bool Ok=xrtFileStat(File,&Info)&&Info.Type==XFILE_TYPE_FILE&&Info.Size<=1024u*1024u;
    unsigned char* Bytes=Ok?xrtMalloc((size_t)Info.Size+1):NULL;char Actual[65];
    Ok=Ok&&Bytes&&xrtReadFull(File,Bytes,(size_t)Info.Size,NULL)&&MdoExtensionHashBytes(Bytes,(size_t)Info.Size,Actual)&&!strcmp(Hash,Actual);
    if(Bytes)xrtSecureZero(Bytes,(size_t)Info.Size);
    xrtFree(Bytes);bool Closed=xrtClose(File);return Ok&&Closed;
}
static bool SdkPluginMountRevision(SdkPlugin* P,const xvalue* Manifest){
    P->Manifest=xrtValueClone(Manifest);if(!P->Manifest)return false;
    if(!SdkPluginEnabled(Manifest)||SdkPlugins.Recovery)return true;
    cstr Id=SdkField(Manifest,"id"),Revision=SdkField(Manifest,"revision");
    if(!MdoExtensionIdValid(Id)||!Revision||strlen(Revision)!=64)return false;
    for(size_t i=0;i<64;i++)if(!((Revision[i]>='0'&&Revision[i]<='9')||(Revision[i]>='a'&&Revision[i]<='f')))return false;
    char Path[256];snprintf(Path,sizeof(Path),"plugins/%s/revisions/%s/manifest.json",Id,Revision);
    bool Missing=false;char* Text=MdoExtensionRead(Path,true,1024u*1024u,&Missing);xvalue* M=SdkParse(Text);xrtFree(Text);
    const xvalue* Files=SdkGet(M,"files");bool Ok=M&&xrtValueType(Files)==XVALUE_ARRAY&&xrtValueCount(Files)<=128;
    for(size_t i=0;Ok&&i<xrtValueCount(Files);i++){
        const xvalue* Entry=xrtValueArrayGet(Files,i);cstr File=SdkField(Entry,"path");if(!SdkPluginVerifyFile(Id,Revision,Entry)){Ok=false;break;}
        char* Parent=xrtPathParent(File);if(!Parent){Ok=false;break;}
        bool Duplicate=false;for(size_t j=0;j<i;j++){char* Other=xrtPathParent(SdkField(xrtValueArrayGet(Files,j),"path"));if(Other&&!strcmp(Parent,Other))Duplicate=true;xrtFree(Other);}
        if(!Duplicate){
            char Relative[600],Prefix[300];snprintf(Relative,sizeof(Relative),"plugins/%s/revisions/%s/files/%s",Id,Revision,!strcmp(Parent,".")?"":Parent);
            snprintf(Prefix,sizeof(Prefix),"/app%s%s",!strcmp(Parent,".")?"":"/",!strcmp(Parent,".")?"":Parent);
            char* Native=MdoHomeExternalPath(Relative);SdkPluginMount* Mount=&P->Mounts[P->Count];
            Mount->Disk=Native?xrtVfsDiskCreate(Native,XVFS_DISK_READ):NULL;xrtFree(Native);
            if(Mount->Disk)Mount->Mount=xrtVfsDiskMount(MdoHomeApplicationVfs(),Prefix,2000,XVFS_CASE_SENSITIVE,Mount->Disk,0);
            P->Count++;Ok=Mount->Mount!=NULL;
        }xrtFree(Parent);
    }
    xrtValueRelease(M);return Ok;
}
static bool SdkPluginConfigSave(const xvalue* Config){char* Text=SdkJson(Config);bool Ok=Text&&MdoHomeAtomicWrite("config/plugins.json",Text,strlen(Text),true);xrtFree(Text);return Ok;}
bool MdoApplicationPluginsStoreBegin(xvalue** Config,char* Error,size_t Capacity){
    if(!Config||!SdkPlugins.Lock)return false;*Config=NULL;
    xrtMutexLock(SdkPlugins.Lock);
    bool Ok=!SdkPlugins.Busy&&!SdkPlugins.Recovery;
    if(Ok){*Config=xrtValueClone(SdkPlugins.Config);Ok=*Config!=NULL;if(Ok)SdkPlugins.Busy=true;}
    xrtMutexUnlock(SdkPlugins.Lock);
    if(!Ok&&Error&&Capacity)snprintf(Error,Capacity,"Plugin manager is busy or in recovery mode; retry after recovery");
    return Ok;
}
void MdoApplicationPluginsStoreEnd(void){
    if(SdkPlugins.Lock){xrtMutexLock(SdkPlugins.Lock);SdkPlugins.Busy=false;xrtMutexUnlock(SdkPlugins.Lock);}
}
bool MdoApplicationPluginsStorePublish(const xvalue* Config,char* Error,size_t Capacity){
    if(xrtValueType(Config)!=XVALUE_ARRAY||xrtValueCount(Config)>32||!SdkPlugins.Lock)return false;
    SdkPlugin* Candidates=xrtCalloc(32,sizeof(*Candidates));xvalue* Next=xrtValueClone(Config);bool Ok=Candidates&&Next;
    for(size_t i=0;Ok&&i<xrtValueCount(Config);i++)Ok=SdkPluginMountRevision(&Candidates[i],xrtValueArrayGet(Config,i));
    if(Ok){xrtMutexLock(SdkPlugins.Lock);
        if(!SdkPlugins.Busy)Ok=false;
        else{for(size_t i=0;i<32;i++){SdkPluginDrop(&SdkPlugins.Plugins[i]);SdkPlugins.Plugins[i]=Candidates[i];memset(&Candidates[i],0,sizeof(Candidates[i]));}
            xrtValueRelease(SdkPlugins.Config);SdkPlugins.Config=Next;Next=NULL;}
        xrtMutexUnlock(SdkPlugins.Lock);
    }
    if(Candidates){for(size_t i=0;i<32;i++)SdkPluginDrop(&Candidates[i]);xrtFree(Candidates);}xrtValueRelease(Next);
    if(!Ok&&Error&&Capacity)snprintf(Error,Capacity,"Plugin candidate could not be mounted; previous runtime is retained");
    return Ok;
}
bool MdoApplicationPluginsInit(void){
    SdkPlugins.Lock=xrtMutexCreate();if(!SdkPlugins.Lock)return false;
    char* Mode=xrtEnvGet("MDO_PLUGINS_DISABLED");SdkPlugins.Recovery=Mode&&strcmp(Mode,"0");xrtFree(Mode);
    bool Missing=false;char* Text=MdoExtensionRead("config/plugins.json",true,262144,&Missing);
    SdkPlugins.Config=Text?SdkParse(Text):xrtValueArray();xrtFree(Text);
    if(!SdkPlugins.Config||xrtValueType(SdkPlugins.Config)!=XVALUE_ARRAY||xrtValueCount(SdkPlugins.Config)>32){xrtValueRelease(SdkPlugins.Config);SdkPlugins.Config=xrtValueArray();SdkPlugins.Recovery=true;}
    for(size_t i=0;i<xrtValueCount(SdkPlugins.Config);i++){
        if(!SdkPluginMountRevision(&SdkPlugins.Plugins[i],xrtValueArrayGet(SdkPlugins.Config,i))){
            SdkPluginDrop(&SdkPlugins.Plugins[i]);SdkPlugins.Plugins[i].Manifest=xrtValueClone(xrtValueArrayGet(SdkPlugins.Config,i));
            snprintf(SdkPlugins.Plugins[i].Error,sizeof(SdkPlugins.Plugins[i].Error),"Plugin revision could not be mounted; started without this overlay");
        }
    }return true;
}
void MdoApplicationPluginsUnit(void){if(!SdkPlugins.Lock)return;for(size_t i=0;i<32;i++)SdkPluginDrop(&SdkPlugins.Plugins[i]);xrtValueRelease(SdkPlugins.Config);xrtMutexDestroy(SdkPlugins.Lock);memset(&SdkPlugins,0,sizeof(SdkPlugins));}
xvalue* MdoApplicationPluginsCatalog(void){xvalue* A=xrtValueArray();for(size_t i=0;i<sizeof(SdkPluginMethods)/sizeof(*SdkPluginMethods);i++){xvalue* V=xrtValueObject();SdkPut(V,"id",SdkPluginMethods[i]);SdkPut(V,"domain","plugin");SdkPut(V,"transport","native");xrtValueArrayAppendNew(A,V);}return A;}
static xvalue* SdkPluginList(void){
    xvalue* R=xrtValueObject(),*Items=xrtValueArray();xrtMutexLock(SdkPlugins.Lock);
    for(size_t i=0;i<32;i++)if(SdkPlugins.Plugins[i].Manifest){xvalue* M=xrtValueClone(SdkPlugins.Plugins[i].Manifest);SdkPut(M,"error",SdkPlugins.Plugins[i].Error);xrtValueArrayAppendNew(Items,M);}
    xrtValueObjectSetNew(R,XRT_STR_LITERAL("recovery_mode"),xrtValueBool(SdkPlugins.Recovery));xrtMutexUnlock(SdkPlugins.Lock);xrtValueObjectSetNew(R,XRT_STR_LITERAL("items"),Items);return R;
}
static bool SdkPluginStage(const xvalue* P,xvalue** Manifest,mdo_error_v1* E){
    cstr Id=SdkField(P,"id"),Frontend=SdkField(P,"frontend");const xvalue* Files=SdkGet(P,"files");
    if(!MdoExtensionIdValid(Id)||xrtValueType(Files)!=XVALUE_ARRAY||!xrtValueCount(Files)||xrtValueCount(Files)>128||(Frontend&&(!MdoPackagePath(Frontend)||strncmp(Frontend,"web/",4))))
        return MdoApplicationError(E,"sdk_arguments_invalid","Plugin needs an ID, 1..128 portable app-relative files and an optional web/ frontend","plugin",400,false);
    char* Text=SdkJson(P);char Hash[65];bool Ok=Text&&strlen(Text)<=8u*1024u*1024u&&MdoExtensionHash(Text,Hash);xrtFree(Text);if(!Ok)return MdoApplicationError(E,"sdk_plugin_limit","Plugin manifest exceeds 8 MiB","plugin",413,false);
    xvalue* Inventory=xrtValueArray();bool Core=false;size_t Total=0;
    for(size_t i=0;Ok&&i<xrtValueCount(Files);i++){
        const xvalue* File=xrtValueArrayGet(Files,i);cstr Name=SdkField(File,"path");unsigned char* Bytes=NULL;size_t Size=0;
        if(!MdoPackagePath(Name)){Ok=false;break;}
        for(size_t j=0;j<i;j++)if(MdoPackageSamePath(Name,SdkField(xrtValueArrayGet(Files,j),"path")))Ok=false;
        if(!Ok||!SdkInputBytes(File,&Bytes,&Size,E)){Ok=false;break;}
        Total+=Size;if(Total>4u*1024u*1024u)Ok=false;
        char Path[600],Digest[65];snprintf(Path,sizeof(Path),"plugins/%s/revisions/%s/files/%s",Id,Hash,Name);
        if(Ok)Ok=MdoExtensionHashBytes(Bytes,Size,Digest)&&MdoHomeAtomicWrite(Path,Bytes,Size,false);
        xrtSecureZero(Bytes,Size);xrtFree(Bytes);
        if(Ok){xvalue* Entry=xrtValueObject();SdkPut(Entry,"path",Name);SdkPut(Entry,"sha256",Digest);SdkPutUInt(Entry,"bytes",Size);xrtValueArrayAppendNew(Inventory,Entry);}
        if(!strncmp(Name,"src/",4)||!strncmp(Name,"include/",8)||!strncmp(Name,"generated/",10)||!strcmp(Name,"main.c")||!strcmp(Name,"sources.json"))Core=true;
    }
    if(!Ok){xrtValueRelease(Inventory);if(!E->Code[0])MdoApplicationError(E,"sdk_plugin_stage_failed","Plugin file validation or staging failed","plugin",422,false);return false;}
    *Manifest=xrtValueObject();SdkPut(*Manifest,"id",Id);SdkPut(*Manifest,"revision",Hash);SdkPut(*Manifest,"frontend",Frontend);xrtValueObjectSetNew(*Manifest,XRT_STR_LITERAL("enabled"),xrtValueBool(true));xrtValueObjectSetNew(*Manifest,XRT_STR_LITERAL("source_reload_required"),xrtValueBool(Core));
    xvalue* Disk=xrtValueClone(*Manifest);xrtValueObjectSetNew(Disk,XRT_STR_LITERAL("files"),Inventory);Text=SdkJson(Disk);char Path[256];snprintf(Path,sizeof(Path),"plugins/%s/revisions/%s/manifest.json",Id,Hash);
    Ok=Text&&MdoHomeAtomicWrite(Path,Text,strlen(Text),false);xrtFree(Text);xrtValueRelease(Disk);return Ok;
}
bool MdoApplicationPluginsCall(const mdo_sdk_context_v1* C,cstr Method,const xvalue* P,xvalue** R,mdo_error_v1* E,bool* Found){
    (void)C;*Found=false;for(size_t i=0;i<sizeof(SdkPluginMethods)/sizeof(*SdkPluginMethods);i++)if(!strcmp(Method,SdkPluginMethods[i]))*Found=true;if(!*Found)return true;
    if(!strcmp(Method,"runtime.reload")){cstr Domain=SdkField(P,"domain");bool Ok=false;
        if(Domain&&!strcmp(Domain,"modules"))Ok=MdoModuleManagerReload();
        else if(Domain&&!strcmp(Domain,"host")){XS_ServerInfo* Server=MdoApplicationServer();uint64 Id=Server?xsReloadServerSubmit(Server->Name):0;if(Id){*R=xrtValueObject();SdkPutUInt(*R,"id",Id);return true;}}
        else return MdoApplicationError(E,"sdk_arguments_invalid","Reload domain must be modules or host","reload",400,false);
        if(!Ok)return MdoApplicationError(E,"sdk_reload_failed","Candidate was rejected; inspect module diagnostics","reload",409,false);
        *R=xrtValueObject();return true;
    }
    if(!strcmp(Method,"runtime.reload.status")){uint64 Id=0;XS_ReloadResult Result={0};if(!SdkUInt(SdkGet(P,"id"),&Id)||!xsReloadQuery(Id,&Result))return MdoApplicationError(E,"sdk_reload_missing","Reload receipt is unavailable","reload",404,false);*R=xrtValueObject();SdkPutUInt(*R,"state",Result.State);SdkPut(*R,"message",Result.Message);return true;}
    if(!SdkPlugins.Lock)return MdoApplicationError(E,"sdk_unavailable","Plugin manager unavailable","plugin",503,false);
    if(!strcmp(Method,"plugin.list")){*R=SdkPluginList();return true;}
    cstr Id=SdkField(P,"id");if(!MdoExtensionIdValid(Id))return MdoApplicationError(E,"sdk_arguments_invalid","Invalid plugin ID","plugin",400,false);
    xrtMutexLock(SdkPlugins.Lock);if(SdkPlugins.Busy||SdkPlugins.Recovery){xrtMutexUnlock(SdkPlugins.Lock);return MdoApplicationError(E,"sdk_plugin_busy","Plugin manager is busy or in recovery mode","plugin",409,false);}SdkPlugins.Busy=true;
    size_t Index=xrtValueCount(SdkPlugins.Config);for(size_t i=0;i<xrtValueCount(SdkPlugins.Config);i++){cstr Existing=SdkField(xrtValueArrayGet(SdkPlugins.Config,i),"id");if(Existing&&!strcmp(Id,Existing))Index=i;}
    xvalue* Old=Index<xrtValueCount(SdkPlugins.Config)?xrtValueClone(xrtValueArrayGet(SdkPlugins.Config,Index)):NULL;
    xvalue* Config=xrtValueArray();for(size_t i=0;i<xrtValueCount(SdkPlugins.Config);i++)if(i!=Index)xrtValueArrayAppend(Config,xrtValueArrayGet(SdkPlugins.Config,i));xrtMutexUnlock(SdkPlugins.Lock);
    bool Remove=!strcmp(Method,"plugin.remove"),Ok=true;xvalue* Manifest=NULL;SdkPlugin Candidate={0};
    if(!strcmp(Method,"plugin.apply"))Ok=SdkPluginStage(P,&Manifest,E);
    else if(!Old)Ok=MdoApplicationError(E,"sdk_plugin_missing","Plugin is not installed","plugin",404,false);
    else {Manifest=xrtValueClone(Old);if(!strcmp(Method,"plugin.rollback")){cstr Previous=SdkField(Old,"previous");if(!Previous||strlen(Previous)!=64)Ok=MdoApplicationError(E,"sdk_plugin_no_previous","No previous revision is retained","plugin",409,false);else {
            char Path[256];snprintf(Path,sizeof(Path),"plugins/%s/revisions/%s/manifest.json",Id,Previous);bool Missing=false;char* Text=MdoExtensionRead(Path,true,1024u*1024u,&Missing);xvalue* Saved=SdkParse(Text);xrtFree(Text);
            if(!Saved)Ok=false;else{SdkPut(Manifest,"revision",Previous);SdkPut(Manifest,"frontend",SdkField(Saved,"frontend"));xrtValueObjectSet(Manifest,XRT_STR_LITERAL("source_reload_required"),SdkGet(Saved,"source_reload_required"));}xrtValueRelease(Saved);}}
        else xrtValueObjectSetNew(Manifest,XRT_STR_LITERAL("enabled"),xrtValueBool(!strcmp(Method,"plugin.enable")));}
    if(Ok&&!Remove){
        if(Old){cstr Revision=SdkField(Manifest,"revision"),Prior=SdkField(Old,"revision");
            cstr Previous=Revision&&Prior&&strcmp(Revision,Prior)?Prior:SdkField(Old,"previous");
            if(Previous)SdkPut(Manifest,"previous",Previous);
        }
        Ok=xrtValueCount(Config)<32&&SdkPluginMountRevision(&Candidate,Manifest);if(Ok)Ok=xrtValueArrayAppend(Config,Manifest);
    }
    if(Ok)Ok=SdkPluginConfigSave(Config);
    xrtMutexLock(SdkPlugins.Lock);
    if(Ok){for(size_t i=0;i<32;i++)if(SdkPlugins.Plugins[i].Manifest&&!strcmp(Id,SdkField(SdkPlugins.Plugins[i].Manifest,"id")))SdkPluginDrop(&SdkPlugins.Plugins[i]);
        if(!Remove){for(size_t i=0;i<32;i++)if(!SdkPlugins.Plugins[i].Manifest){SdkPlugins.Plugins[i]=Candidate;memset(&Candidate,0,sizeof(Candidate));break;}}
        xrtValueRelease(SdkPlugins.Config);SdkPlugins.Config=Config;Config=NULL;}
    SdkPlugins.Busy=false;xrtMutexUnlock(SdkPlugins.Lock);SdkPluginDrop(&Candidate);xrtValueRelease(Config);xrtValueRelease(Old);xrtValueRelease(Manifest);
    if(!Ok){if(!E->Code[0])MdoApplicationError(E,"sdk_plugin_publish_failed","Plugin was not published; previous configuration is retained","plugin",500,false);return false;}
    *R=SdkPluginList();return true;
}
