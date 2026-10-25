#include "internal.h"
#include "../../include/mdo/extension_files.h"
#include "../../include/mdo/ecosystem_package.h"
#include "../../include/mdo/modules.h"
#include "../../include/mdo/time.h"

typedef struct SdkPlugin {xvalue* Manifest;char Error[256];} SdkPlugin;
static struct {xmutex* Lock;xvalue* Config;SdkPlugin Plugins[32];xvalue* Map;xvfsmount Mount;bool Busy,Recovery;} SdkPlugins;
static const cstr SdkPluginMethods[]={"plugin.list","plugin.apply","plugin.enable","plugin.disable","plugin.remove","plugin.rollback","plugin.plan","plugin.commit","runtime.reload","runtime.reload.status"};
static void SdkPluginDrop(SdkPlugin* P){
    xrtValueRelease(P->Manifest);memset(P,0,sizeof(*P));
}
static bool SdkPluginEnabled(const xvalue* M){bool Enabled=true;xrtValueGetBool(SdkGet(M,"enabled"),&Enabled);return Enabled;}
#include "plugins_vfs.inc.c"
static bool SdkPluginVerifyFile(cstr Id,cstr Revision,const xvalue* Entry){
    cstr Name=SdkField(Entry,"path"),Hash=SdkField(Entry,"sha256");if(!MdoPackagePath(Name)||!Hash||strlen(Hash)!=64)return false;
    char Path[600];snprintf(Path,sizeof(Path),"plugins/%s/revisions/%s/files/%s",Id,Revision,Name);
    xfile File=MdoHomeOpenRead(Path);if(!File)return false;xfileinfo Info={0};bool Ok=xrtFileStat(File,&Info)&&Info.Type==XFILE_TYPE_FILE&&Info.Size<=64u*1024u*1024u;
    unsigned char Bytes[65536],Digest[32];char Actual[65];uint64 Total=0;xsha256 Sha;xrtSha256Init(&Sha);
    while(Ok&&Total<Info.Size){size_t N=0;Ok=xrtRead(File,Bytes,sizeof(Bytes),&N)&&N&&xrtSha256Update(&Sha,Bytes,N);Total+=N;}
    uint64 Expected=0;Ok=Ok&&SdkUInt(SdkGet(Entry,"bytes"),&Expected)&&Expected==Info.Size&&Total==Info.Size&&xrtSha256Final(&Sha,Digest);if(Ok){for(size_t I=0;I<32;I++)snprintf(Actual+I*2,3,"%02x",Digest[I]);Ok=!strcmp(Hash,Actual);}
    xrtSecureZero(Bytes,sizeof(Bytes));bool Closed=xrtClose(File);return Ok&&Closed;
}
static bool SdkPluginMountRevision(SdkPlugin* P,const xvalue* Manifest){
    P->Manifest=xrtValueClone(Manifest);if(!P->Manifest)return false;
    cstr Id=SdkField(Manifest,"id"),Revision=SdkField(Manifest,"revision");
    if(!MdoExtensionIdValid(Id)||!Revision||strlen(Revision)!=64)return false;
    for(size_t i=0;i<64;i++)if(!((Revision[i]>='0'&&Revision[i]<='9')||(Revision[i]>='a'&&Revision[i]<='f')))return false;
    char Path[256];snprintf(Path,sizeof(Path),"plugins/%s/revisions/%s/manifest.json",Id,Revision);
    bool Missing=false;char* Text=MdoExtensionRead(Path,true,1024u*1024u,&Missing);xvalue* M=SdkParse(Text);xrtFree(Text);
    const xvalue* Files=SdkGet(M,"files");bool Ok=M&&xrtValueType(Files)==XVALUE_ARRAY&&xrtValueCount(Files)<=128;
    const xvalue* Doc=SdkGet(M,"manifest");char Why[256]={0};
    if(Ok&&Doc)Ok=MdoPluginValidate(Doc,Files,Id,Why,sizeof(Why));
    if(Ok)Ok=xrtValueObjectSet(P->Manifest,XRT_STR_LITERAL("files"),Files);
    if(Ok&&Doc)Ok=xrtValueObjectSet(P->Manifest,XRT_STR_LITERAL("manifest"),Doc);
    cstr Frontend=Doc?SdkField(SdkGet(Doc,"entry"),"frontend"):SdkField(M,"frontend");
    if(Ok&&Frontend&&*Frontend){char Virtual[600];snprintf(Virtual,sizeof(Virtual),"web/__mdo_plugins/%s/%s/%s",Id,Revision,Frontend);Ok=SdkPut(P->Manifest,"frontend",Virtual);}
    for(size_t i=0;Ok&&i<xrtValueCount(Files);i++){
        const xvalue* Entry=xrtValueArrayGet(Files,i);cstr File=SdkField(Entry,"path");if(!SdkPluginVerifyFile(Id,Revision,Entry)){Ok=false;break;}
        for(size_t J=0;Ok&&J<i;J++)if(MdoPluginPathOverlap(File,SdkField(xrtValueArrayGet(Files,J),"path")))Ok=false;
    }
    xrtValueRelease(M);return Ok;
}
#include "plugins_plan.inc.c"
static bool SdkPluginConfigSave(const xvalue* Config){char* Text=SdkJson(Config);bool Ok=Text&&MdoHomeAtomicWrite("config/plugins.json",Text,strlen(Text),true);xrtFree(Text);return Ok;}
bool MdoApplicationPluginsStoreBegin(xvalue** Config,char* Error,size_t Capacity){
    if(!Config||!SdkPlugins.Lock)return false;
    *Config=NULL;
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
    mdo_error_v1 E={0};if(Ok)Ok=SdkPluginSetCheck(Candidates,&E);
    xvalue* Map=Ok?SdkPluginMapBuild(Candidates):NULL;Ok=Ok&&Map;
    if(Ok){xrtMutexLock(SdkPlugins.Lock);
        if(!SdkPlugins.Busy)Ok=false;
        else{for(size_t i=0;i<32;i++){SdkPluginDrop(&SdkPlugins.Plugins[i]);SdkPlugins.Plugins[i]=Candidates[i];memset(&Candidates[i],0,sizeof(Candidates[i]));}
            xrtValueRelease(SdkPlugins.Config);SdkPlugins.Config=Next;Next=NULL;}
        if(Ok){xrtValueRelease(SdkPlugins.Map);SdkPlugins.Map=Map;Map=NULL;}
        xrtMutexUnlock(SdkPlugins.Lock);
    }
    if(Candidates){for(size_t i=0;i<32;i++)SdkPluginDrop(&Candidates[i]);xrtFree(Candidates);}xrtValueRelease(Next);xrtValueRelease(Map);
    if(!Ok&&Error&&Capacity)snprintf(Error,Capacity,"%s",E.Message[0]?E.Message:"Plugin candidate could not be mounted; previous runtime is retained");
    return Ok;
}
bool MdoApplicationPluginsInit(void){
    SdkPlugins.Lock=xrtMutexCreate();if(!SdkPlugins.Lock)return false;
    char* Mode=xrtEnvGet("MDO_PLUGINS_DISABLED");SdkPlugins.Recovery=Mode&&strcmp(Mode,"0");xrtFree(Mode);
    bool Missing=false;char* Text=MdoExtensionRead("config/plugins.json",true,262144,&Missing);
    SdkPlugins.Config=Text?SdkParse(Text):xrtValueArray();xrtFree(Text);
    if(!SdkPlugins.Config||xrtValueType(SdkPlugins.Config)!=XVALUE_ARRAY||xrtValueCount(SdkPlugins.Config)>32){xrtValueRelease(SdkPlugins.Config);SdkPlugins.Config=xrtValueArray();SdkPlugins.Recovery=true;}
    for(size_t i=0;!SdkPlugins.Recovery&&i<xrtValueCount(SdkPlugins.Config);i++){
        if(!SdkPluginMountRevision(&SdkPlugins.Plugins[i],xrtValueArrayGet(SdkPlugins.Config,i))){
            SdkPluginDrop(&SdkPlugins.Plugins[i]);SdkPlugins.Plugins[i].Manifest=xrtValueClone(xrtValueArrayGet(SdkPlugins.Config,i));
            snprintf(SdkPlugins.Plugins[i].Error,sizeof(SdkPlugins.Plugins[i].Error),"Plugin revision could not be mounted; started without this overlay");
        }
    }
    mdo_error_v1 E={0};SdkPlugins.Map=SdkPluginSetCheck(SdkPlugins.Plugins,&E)?SdkPluginMapBuild(SdkPlugins.Plugins):NULL;
    if(!SdkPlugins.Map){SdkPlugins.Recovery=true;SdkPlugins.Map=xrtValueArray();}
    SdkPlugins.Mount=xrtVfsMount(MdoHomeApplicationVfs(),"/app",2000,XVFS_CASE_SENSITIVE,&SdkPluginProvider,NULL,0);
    return SdkPlugins.Map&&SdkPlugins.Mount;
}
void MdoApplicationPluginsUnit(void){if(!SdkPlugins.Lock)return;if(SdkPlugins.Mount){xrtVfsUnmount(SdkPlugins.Mount);xrtVfsMountDestroy(SdkPlugins.Mount);}for(size_t i=0;i<32;i++)SdkPluginDrop(&SdkPlugins.Plugins[i]);xrtValueRelease(SdkPlugins.Config);xrtValueRelease(SdkPlugins.Map);xrtMutexDestroy(SdkPlugins.Lock);memset(&SdkPlugins,0,sizeof(SdkPlugins));}
xvalue* MdoApplicationPluginsCatalog(void){xvalue* A=xrtValueArray();for(size_t i=0;i<sizeof(SdkPluginMethods)/sizeof(*SdkPluginMethods);i++){xvalue* V=xrtValueObject();SdkPut(V,"id",SdkPluginMethods[i]);SdkPut(V,"domain","plugin");SdkPut(V,"transport","native");xrtValueArrayAppendNew(A,V);}return A;}
static xvalue* SdkPluginList(void){
    xvalue* R=xrtValueObject(),*Items=xrtValueArray();xrtMutexLock(SdkPlugins.Lock);
    for(size_t i=0;i<32;i++)if(SdkPlugins.Plugins[i].Manifest){xvalue* M=xrtValueClone(SdkPlugins.Plugins[i].Manifest);SdkPut(M,"error",SdkPlugins.Plugins[i].Error);xrtValueArrayAppendNew(Items,M);}
    xrtValueObjectSetNew(R,XRT_STR_LITERAL("recovery_mode"),xrtValueBool(SdkPlugins.Recovery));xrtMutexUnlock(SdkPlugins.Lock);xrtValueObjectSetNew(R,XRT_STR_LITERAL("items"),Items);return R;
}
static bool SdkPluginStage(const xvalue* P,xvalue** Manifest,mdo_error_v1* E){
    cstr Id=SdkField(P,"id"),Frontend=SdkField(P,"frontend");const xvalue* Files=SdkGet(P,"files"),*Doc=SdkGet(P,"manifest");
    char Why[256]={0};if(Doc&&!MdoPluginValidate(Doc,Files,Id,Why,sizeof(Why)))return MdoApplicationError(E,"sdk_plugin_manifest_invalid",Why,"plugin",422,false);
    if(Doc)Frontend=SdkField(SdkGet(Doc,"entry"),"frontend");
    if(!MdoExtensionIdValid(Id)||xrtValueType(Files)!=XVALUE_ARRAY||!xrtValueCount(Files)||xrtValueCount(Files)>128||(Frontend&&(!MdoPackagePath(Frontend)||(!Doc&&strncmp(Frontend,"web/",4)))))
        return MdoApplicationError(E,"sdk_arguments_invalid","Plugin needs an ID, 1..128 portable app-relative files and an optional web/ frontend","plugin",400,false);
    xvalue* Identity=xrtValueClone(P);if(Identity)xrtValueObjectRemove(Identity,XRT_STR_LITERAL("enabled"));char* Text=SdkJson(Identity);xrtValueRelease(Identity);char Hash[65];bool Ok=Text&&strlen(Text)<=MDO_PACKAGE_LIMIT&&MdoExtensionHash(Text,Hash);xrtFree(Text);if(!Ok)return MdoApplicationError(E,"sdk_plugin_limit","Plugin payload exceeds its limit","plugin",413,false);
    xvalue* Inventory=xrtValueArray();bool Core=false;size_t Total=0;
    for(size_t i=0;Ok&&i<xrtValueCount(Files);i++){
        const xvalue* File=xrtValueArrayGet(Files,i);cstr Name=SdkField(File,"path");unsigned char* Bytes=NULL;size_t Size=0;
        if(!MdoPackagePath(Name)){Ok=false;break;}
        for(size_t j=0;j<i;j++)if(MdoPluginPathOverlap(Name,SdkField(xrtValueArrayGet(Files,j),"path")))Ok=false;
        if(!Ok||!SdkInputBytes(File,&Bytes,&Size,E)){Ok=false;break;}
        Total+=Size;if(Total>MDO_PLUGIN_BYTES)Ok=false;
        char Path[600],Digest[65];snprintf(Path,sizeof(Path),"plugins/%s/revisions/%s/files/%s",Id,Hash,Name);
        if(Ok)Ok=MdoExtensionHashBytes(Bytes,Size,Digest)&&MdoHomeAtomicWrite(Path,Bytes,Size,false);
        xrtSecureZero(Bytes,Size);xrtFree(Bytes);
        if(Ok){xvalue* Entry=xrtValueObject();SdkPut(Entry,"path",Name);SdkPut(Entry,"sha256",Digest);SdkPutUInt(Entry,"bytes",Size);xrtValueArrayAppendNew(Inventory,Entry);}
        if(!strncmp(Name,"src/",4)||!strncmp(Name,"include/",8)||!strncmp(Name,"generated/",10)||!strcmp(Name,"main.c")||!strcmp(Name,"sources.json"))Core=true;
    }
    if(!Ok){xrtValueRelease(Inventory);if(!E->Code[0])MdoApplicationError(E,"sdk_plugin_stage_failed","Plugin file validation or staging failed","plugin",422,false);return false;}
    if(Doc){const xvalue* Overrides=SdkGet(Doc,"overrides");Core=false;for(size_t I=0;I<xrtValueCount(Overrides);I++){cstr T=SdkField(xrtValueArrayGet(Overrides,I),"target");if(!strncmp(T,"src/",4)||!strncmp(T,"include/",8)||!strncmp(T,"generated/",10)||!strcmp(T,"main.c")||!strcmp(T,"sources.json"))Core=true;}}
    bool Enabled=Doc?false:true;xrtValueGetBool(SdkGet(P,"enabled"),&Enabled);
    *Manifest=xrtValueObject();SdkPut(*Manifest,"id",Id);SdkPut(*Manifest,"revision",Hash);SdkPut(*Manifest,"frontend",Frontend);xrtValueObjectSetNew(*Manifest,XRT_STR_LITERAL("enabled"),xrtValueBool(Enabled));xrtValueObjectSetNew(*Manifest,XRT_STR_LITERAL("source_reload_required"),xrtValueBool(Core));
    if(Doc)xrtValueObjectSet(*Manifest,XRT_STR_LITERAL("manifest"),Doc);
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
    if(!strcmp(Method,"plugin.plan")){
        xrtMutexLock(SdkPlugins.Lock);xvalue* Config=xrtValueClone(SdkPlugins.Config);bool Recovery=SdkPlugins.Recovery;xrtMutexUnlock(SdkPlugins.Lock);
        if(Recovery){xrtValueRelease(Config);return MdoApplicationError(E,"sdk_plugin_recovery","Leave plugin recovery mode before changing plugins","plugin",409,false);}
        *R=Config?SdkPluginPlan(Config,SdkGet(P,"changes"),NULL,E):NULL;xrtValueRelease(Config);return *R!=NULL;
    }
    if(!strcmp(Method,"plugin.commit")){
        xvalue* Before=NULL,*Desired=NULL;if(!MdoApplicationPluginsStoreBegin(&Before,E->Message,sizeof(E->Message)))return MdoApplicationError(E,"sdk_plugin_busy","Plugin manager is busy or in recovery mode","plugin",409,false);
        xvalue* Plan=SdkPluginPlan(Before,SdkGet(P,"changes"),&Desired,E);bool Allowed=false;
        bool Ok=Plan&&xrtValueGetBool(SdkGet(Plan,"allowed"),&Allowed)&&Allowed;
        cstr Base=SdkField(P,"expected_revision"),Expected=SdkField(P,"expected_plan");
        if(Ok&&(!Base||!Expected||strcmp(Base,SdkField(Plan,"base_revision"))||strcmp(Expected,SdkField(Plan,"plan_revision"))))Ok=MdoApplicationError(E,"sdk_plugin_plan_changed","Plugin selection changed; review a fresh plan","plugin",412,false);
        if(Plan&&!Allowed)MdoApplicationError(E,"sdk_plugin_conflict","Plugin plan contains unresolved conflicts or dependencies","plugin",409,false);
        if(Ok)Ok=SdkPluginConfigSave(Desired);
        if(Ok&&!MdoApplicationPluginsStorePublish(Desired,E->Message,sizeof(E->Message))){bool Restored=SdkPluginConfigSave(Before);Ok=MdoApplicationError(E,"sdk_plugin_publish_failed",Restored?"Previous plugin selection retained":"Plugin selection needs restart recovery","plugin",500,!Restored);}
        MdoApplicationPluginsStoreEnd();xrtValueRelease(Before);xrtValueRelease(Desired);xrtValueRelease(Plan);
        if(Ok){MdoApplicationEvent("plugin.changed",C,NULL);*R=SdkPluginList();}return Ok;
    }
    cstr Id=SdkField(P,"id");if(!MdoExtensionIdValid(Id))return MdoApplicationError(E,"sdk_arguments_invalid","Invalid plugin ID","plugin",400,false);
    xrtMutexLock(SdkPlugins.Lock);if(SdkPlugins.Busy||SdkPlugins.Recovery){xrtMutexUnlock(SdkPlugins.Lock);return MdoApplicationError(E,"sdk_plugin_busy","Plugin manager is busy or in recovery mode","plugin",409,false);}SdkPlugins.Busy=true;
    size_t Index=xrtValueCount(SdkPlugins.Config);for(size_t i=0;i<xrtValueCount(SdkPlugins.Config);i++){cstr Existing=SdkField(xrtValueArrayGet(SdkPlugins.Config,i),"id");if(Existing&&!strcmp(Id,Existing))Index=i;}
    xvalue* Old=Index<xrtValueCount(SdkPlugins.Config)?xrtValueClone(xrtValueArrayGet(SdkPlugins.Config,Index)):NULL;
    xvalue* Config=xrtValueArray();for(size_t i=0;i<xrtValueCount(SdkPlugins.Config);i++)if(i!=Index)xrtValueArrayAppend(Config,xrtValueArrayGet(SdkPlugins.Config,i));xrtMutexUnlock(SdkPlugins.Lock);
    bool Remove=!strcmp(Method,"plugin.remove"),Ok=true;xvalue* Manifest=NULL;SdkPlugin Candidate={0};xvalue* Map=NULL;
    if(!strcmp(Method,"plugin.apply")){Ok=SdkPluginStage(P,&Manifest,E);if(Ok&&Old&&!SdkGet(P,"enabled"))xrtValueObjectSet(Manifest,XRT_STR_LITERAL("enabled"),SdkGet(Old,"enabled"));}
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
    if(Ok){SdkPlugin* All=xrtCalloc(32,sizeof(*All));Ok=All!=NULL;
        for(size_t I=0;Ok&&I<xrtValueCount(Config);I++)Ok=SdkPluginMountRevision(&All[I],xrtValueArrayGet(Config,I));
        if(Ok)Ok=SdkPluginSetCheck(All,E);
        if(Ok){Map=SdkPluginMapBuild(All);Ok=Map!=NULL;if(!Ok){cstr Why="Plugin file targets overlap";for(size_t I=0;I<32;I++)if(All[I].Error[0])Why=All[I].Error;MdoApplicationError(E,"sdk_plugin_conflict",Why,"plugin",409,false);}}
        if(All){for(size_t I=0;I<32;I++)SdkPluginDrop(&All[I]);xrtFree(All);}}
    if(Ok)Ok=SdkPluginConfigSave(Config);
    xrtMutexLock(SdkPlugins.Lock);
    if(Ok){for(size_t i=0;i<32;i++)if(SdkPlugins.Plugins[i].Manifest&&!strcmp(Id,SdkField(SdkPlugins.Plugins[i].Manifest,"id")))SdkPluginDrop(&SdkPlugins.Plugins[i]);
        if(!Remove){for(size_t i=0;i<32;i++)if(!SdkPlugins.Plugins[i].Manifest){SdkPlugins.Plugins[i]=Candidate;memset(&Candidate,0,sizeof(Candidate));break;}}
        xrtValueRelease(SdkPlugins.Config);SdkPlugins.Config=Config;Config=NULL;}
    if(Ok){xrtValueRelease(SdkPlugins.Map);SdkPlugins.Map=Map;Map=NULL;}
    SdkPlugins.Busy=false;xrtMutexUnlock(SdkPlugins.Lock);SdkPluginDrop(&Candidate);xrtValueRelease(Config);xrtValueRelease(Old);xrtValueRelease(Manifest);xrtValueRelease(Map);
    if(!Ok){if(!E->Code[0])MdoApplicationError(E,"sdk_plugin_publish_failed","Plugin was not published; previous configuration is retained","plugin",500,false);return false;}
    xvalue* Change=xrtValueObject();SdkPut(Change,"id",Id);SdkPut(Change,"method",Method);MdoApplicationEvent("plugin.changed",C,Change);xrtValueRelease(Change);
    *R=SdkPluginList();return true;
}
