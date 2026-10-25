#include "internal.h"
#include "../../include/mdo/extension_files.h"
#include "../../include/mdo/ecosystem_package.h"
#include "../../include/mdo/modules.h"
#include "../../include/mdo/time.h"

typedef struct SdkPlugin {xvalue* Manifest;char Error[256];} SdkPlugin;
static struct {xmutex* Lock;xvalue* Config;SdkPlugin Plugins[32];xvalue *Map,*Effective,*Frontends,*Failures;xvfsmount Mount,HomeMount;size_t Readers;bool Busy,Recovery,Pending,ConfigBroken;} SdkPlugins;
static const cstr SdkPluginMethods[]={"plugin.list","plugin.get","plugin.file.read","plugin.apply","plugin.enable","plugin.disable","plugin.remove","plugin.rollback","plugin.plan","plugin.commit","plugin.preset.list","plugin.preset.save","plugin.preset.remove","plugin.preset.plan","plugin.frontend.hello","plugin.frontend.enter","plugin.frontend.leave","plugin.frontend.fail","runtime.reload","runtime.reload.status"};
static void SdkPluginDrop(SdkPlugin* P){
    xrtValueRelease(P->Manifest);memset(P,0,sizeof(*P));
}
static bool SdkPluginEnabled(const xvalue* M){bool Enabled=true;xrtValueGetBool(SdkGet(M,"enabled"),&Enabled);return Enabled;}
#include "plugins_vfs.inc.c"
static bool SdkPluginVerifyFile(cstr Id,cstr Revision,const xvalue* Entry){
    cstr Name=SdkField(Entry,"path"),Hash=SdkField(Entry,"sha256");if(!MdoPluginPath(Name)||!Hash||strlen(Hash)!=64)return false;
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
    const xvalue* SelectedDoc=SdkGet(Manifest,"manifest");if(Ok&&(Doc||SelectedDoc))Ok=Doc&&SelectedDoc&&xrtValueEqual(Doc,SelectedDoc);
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
#include "plugins_runtime.inc.c"
#include "plugins_manage.inc.c"
static bool SdkPluginConfigSave(const xvalue* Config){char* Text=SdkJson(Config);bool Ok=Text&&MdoHomeAtomicWrite("config/plugins.json",Text,strlen(Text),true);xrtFree(Text);return Ok;}
bool MdoApplicationPluginsStoreBegin(xvalue** Config,char* Error,size_t Capacity){
    if(!Config||!SdkPlugins.Lock)return false;
    *Config=NULL;
    xrtMutexLock(SdkPlugins.Lock);
    bool Ok=!SdkPlugins.Busy&&!SdkPlugins.Readers&&!SdkPlugins.ConfigBroken;
    if(Ok){*Config=xrtValueClone(SdkPlugins.Config);Ok=*Config!=NULL;if(Ok)SdkPlugins.Busy=true;}
    xrtMutexUnlock(SdkPlugins.Lock);
    if(!Ok&&Error&&Capacity)snprintf(Error,Capacity,"Plugin manager is busy or in recovery mode; retry after recovery");
    return Ok;
}
void MdoApplicationPluginsStoreEnd(void){
    if(SdkPlugins.Lock){xrtMutexLock(SdkPlugins.Lock);SdkPlugins.Busy=false;xrtMutexUnlock(SdkPlugins.Lock);}
}
/* Inspect prepared immutable descriptors without publishing or writing files.
 * Both the SDK plan and store plan use the same final-set rules. */
xvalue* MdoApplicationPluginsStoreIssues(const xvalue* Config,const xvalue* Prepared){
    if(xrtValueType(Config)!=XVALUE_ARRAY||xrtValueCount(Config)>32)return NULL;
    SdkPlugin* All=xrtCalloc(32,sizeof(*All));xvalue* Issues=xrtValueArray();if(!All||!Issues){xrtFree(All);xrtValueRelease(Issues);return NULL;}
    bool Ok=true;
    for(size_t I=0;Ok&&I<xrtValueCount(Config);I++){
        const xvalue* R=xrtValueArrayGet(Config,I);cstr Id=SdkField(R,"id"),Rev=SdkField(R,"revision");char Path[256];snprintf(Path,sizeof(Path),"plugins/%s/revisions/%s/manifest.json",Id,Rev);xvalue* Disk=NULL;
        for(size_t J=0;J<xrtValueCount(Prepared);J++){
            const xvalue* F=xrtValueArrayGet(Prepared,J);cstr P=SdkField(F,"path");if(!P||strcmp(Path,P))continue;
            cstr B=SdkField(F,"new");size_t N=0;bytes Raw=B?xrtBase64DecodeNew(B,strlen(B),&N,NULL):NULL;
            Disk=Raw&&N<=1048576?xrtJsonParse(xrtStrViewN((cstr)Raw,N)):NULL;xrtFree(Raw);break;
        }
        bool Valid=false;
        if(Disk){
            const xvalue* Files=SdkGet(Disk,"files"),*Doc=SdkGet(Disk,"manifest");char Why[256];
            Valid=SdkField(Disk,"id")&&!strcmp(Id,SdkField(Disk,"id"))&&SdkField(Disk,"revision")&&!strcmp(Rev,SdkField(Disk,"revision"))&&xrtValueType(Files)==XVALUE_ARRAY;
            if(Valid&&Doc)Valid=MdoPluginValidate(Doc,Files,Id,Why,sizeof(Why));
            All[I].Manifest=xrtValueClone(R);Valid=Valid&&All[I].Manifest&&xrtValueObjectSet(All[I].Manifest,XRT_STR_LITERAL("files"),Files);
        }else Valid=SdkPluginMountRevision(&All[I],R);
        xrtValueRelease(Disk);if(!Valid)Ok=SdkPluginIssue(Issues,"invalid_revision",Id,"","Plugin revision could not be verified");
    }
    if(Ok)SdkPluginSetValidate(All,Issues);
    xvalue* Map=Ok&&!xrtValueCount(Issues)?SdkPluginMapBuild(All):NULL;
    if(Ok&&!Map&&!xrtValueCount(Issues)){cstr Why="Plugin file targets overlap";for(size_t I=0;I<32;I++)if(All[I].Error[0])Why=All[I].Error;Ok=SdkPluginIssue(Issues,"files","","",Why);}
    for(size_t I=0;I<32;I++)SdkPluginDrop(&All[I]);xrtFree(All);xrtValueRelease(Map);if(!Ok){xrtValueRelease(Issues);return NULL;}return Issues;
}
bool MdoApplicationPluginsStorePublish(const xvalue* Config,char* Error,size_t Capacity){
    if(xrtValueType(Config)!=XVALUE_ARRAY||xrtValueCount(Config)>32||!SdkPlugins.Lock)return false;
    SdkPlugin* Candidates=xrtCalloc(32,sizeof(*Candidates));xvalue* Next=xrtValueClone(Config),*Effective=xrtValueClone(Config);bool Ok=Candidates&&Next&&Effective;
    for(size_t i=0;Ok&&i<xrtValueCount(Config);i++)Ok=SdkPluginMountRevision(&Candidates[i],xrtValueArrayGet(Config,i));
    mdo_error_v1 E={0};if(Ok)Ok=SdkPluginSetCheck(Candidates,&E);
    xvalue* Map=Ok?SdkPluginMapBuild(Candidates):NULL;Ok=Ok&&Map;
    if(Ok){xrtMutexLock(SdkPlugins.Lock);
        if(!SdkPlugins.Busy||SdkPlugins.Readers)Ok=false;
        else{for(size_t i=0;i<32;i++){SdkPluginDrop(&SdkPlugins.Plugins[i]);SdkPlugins.Plugins[i]=Candidates[i];memset(&Candidates[i],0,sizeof(Candidates[i]));}
            xrtValueRelease(SdkPlugins.Config);SdkPlugins.Config=Next;Next=NULL;}
        if(Ok){SdkPlugins.Pending=!SdkPluginCanPublish(SdkPlugins.Config);if(!SdkPlugins.Pending){xrtValueRelease(SdkPlugins.Map);SdkPlugins.Map=Map;Map=NULL;xrtValueRelease(SdkPlugins.Effective);SdkPlugins.Effective=Effective;Effective=NULL;}}
        xrtMutexUnlock(SdkPlugins.Lock);
    }
    if(Candidates){for(size_t i=0;i<32;i++)SdkPluginDrop(&Candidates[i]);xrtFree(Candidates);}xrtValueRelease(Next);xrtValueRelease(Map);xrtValueRelease(Effective);
    if(!Ok&&Error&&Capacity)snprintf(Error,Capacity,"%s",E.Message[0]?E.Message:"Plugin candidate could not be mounted; previous runtime is retained");
    return Ok;
}
bool MdoApplicationPluginsInit(void){
    if(SdkPlugins.Lock)return true;
    SdkPlugins.Lock=xrtMutexCreate();if(!SdkPlugins.Lock)return false;
    SdkPlugins.Frontends=xrtValueArray();SdkPlugins.Failures=xrtValueArray();
    char* Mode=xrtEnvGet("MDO_PLUGINS_DISABLED");SdkPlugins.Recovery=Mode&&strcmp(Mode,"0");xrtFree(Mode);
    bool Missing=false;char* Text=MdoExtensionRead("config/plugins.json",true,16777216,&Missing);
    SdkPlugins.Config=Text?SdkParse(Text):xrtValueArray();xrtFree(Text);
    if(!SdkPlugins.Config||xrtValueType(SdkPlugins.Config)!=XVALUE_ARRAY||xrtValueCount(SdkPlugins.Config)>32){xrtValueRelease(SdkPlugins.Config);SdkPlugins.Config=xrtValueArray();SdkPlugins.Recovery=true;SdkPlugins.ConfigBroken=true;}
    for(size_t I=0;!SdkPlugins.Recovery&&I<xrtValueCount(SdkPlugins.Config);I++){
        const xvalue* R=xrtValueArrayGet(SdkPlugins.Config,I);cstr Id=SdkField(R,"id"),Rev=SdkField(R,"revision");bool Enabled;
        bool Valid=MdoPackageId(Id)&&Rev&&strlen(Rev)==64&&(!SdkGet(R,"enabled")||xrtValueGetBool(SdkGet(R,"enabled"),&Enabled));
        for(size_t J=0;Valid&&J<64;J++)if(!strchr("0123456789abcdef",Rev[J]))Valid=false;
        for(size_t J=0;Valid&&J<I;J++)if(!strcmp(Id,SdkField(xrtValueArrayGet(SdkPlugins.Config,J),"id")))Valid=false;
        if(!Valid){SdkPlugins.Recovery=true;SdkPlugins.ConfigBroken=true;}
    }
    for(size_t i=0;!SdkPlugins.Recovery&&i<xrtValueCount(SdkPlugins.Config);i++){
        if(!SdkPluginMountRevision(&SdkPlugins.Plugins[i],xrtValueArrayGet(SdkPlugins.Config,i))){
            SdkPluginDrop(&SdkPlugins.Plugins[i]);SdkPlugins.Plugins[i].Manifest=xrtValueClone(xrtValueArrayGet(SdkPlugins.Config,i));
            snprintf(SdkPlugins.Plugins[i].Error,sizeof(SdkPlugins.Plugins[i].Error),"Plugin revision could not be mounted; started without this overlay");
        }
    }
    mdo_error_v1 E={0};SdkPlugins.Map=SdkPluginSetCheck(SdkPlugins.Plugins,&E)?SdkPluginMapBuild(SdkPlugins.Plugins):NULL;
    if(!SdkPlugins.Map){SdkPlugins.Recovery=true;SdkPlugins.Map=xrtValueArray();}
    SdkPlugins.Effective=SdkPlugins.Recovery?xrtValueArray():xrtValueClone(SdkPlugins.Config);
    SdkPlugins.Mount=xrtVfsMount(MdoHomeApplicationVfs(),"/app",2000,XVFS_CASE_SENSITIVE,&SdkPluginProvider,NULL,0);
    SdkPlugins.HomeMount=xrtVfsMount(MdoHomeApplicationVfs(),"/app/default-home",2000,XVFS_CASE_SENSITIVE,&SdkPluginHomeProvider,NULL,0);
    return SdkPlugins.Map&&SdkPlugins.Mount&&SdkPlugins.HomeMount&&SdkPlugins.Effective&&SdkPlugins.Frontends&&SdkPlugins.Failures;
}
void MdoApplicationPluginsUnit(void){if(!SdkPlugins.Lock)return;if(SdkPlugins.HomeMount){xrtVfsUnmount(SdkPlugins.HomeMount);xrtVfsMountDestroy(SdkPlugins.HomeMount);}if(SdkPlugins.Mount){xrtVfsUnmount(SdkPlugins.Mount);xrtVfsMountDestroy(SdkPlugins.Mount);}for(size_t i=0;i<32;i++)SdkPluginDrop(&SdkPlugins.Plugins[i]);xrtValueRelease(SdkPlugins.Config);xrtValueRelease(SdkPlugins.Map);xrtValueRelease(SdkPlugins.Effective);xrtValueRelease(SdkPlugins.Frontends);xrtValueRelease(SdkPlugins.Failures);xrtMutexDestroy(SdkPlugins.Lock);memset(&SdkPlugins,0,sizeof(SdkPlugins));}
xvalue* MdoApplicationPluginsCatalog(void){xvalue* A=xrtValueArray();for(size_t i=0;i<sizeof(SdkPluginMethods)/sizeof(*SdkPluginMethods);i++){xvalue* V=xrtValueObject();SdkPut(V,"id",SdkPluginMethods[i]);SdkPut(V,"domain","plugin");SdkPut(V,"transport","native");xrtValueArrayAppendNew(A,V);}return A;}
static xvalue* SdkPluginList(void){
    xvalue* R=xrtValueObject(),*Items=xrtValueArray();xrtMutexLock(SdkPlugins.Lock);
    for(size_t i=0;i<32;i++)if(SdkPlugins.Plugins[i].Manifest){xvalue* M=xrtValueClone(SdkPlugins.Plugins[i].Manifest);SdkPut(M,"error",SdkPlugins.Plugins[i].Error);const xvalue* Active=SdkPluginFind(SdkPlugins.Effective,SdkField(M,"id"));bool Enabled=Active&&SdkPluginEnabled(Active);SdkPut(M,"effective_revision",Enabled?SdkField(Active,"revision"):"");xrtValueObjectSetNew(M,XRT_STR_LITERAL("effective_enabled"),xrtValueBool(Enabled));SdkPut(M,"state",SdkPlugins.Plugins[i].Error[0]?"failed":SdkPlugins.Recovery?"recovery":SdkPlugins.Pending?"pending":SdkPluginEnabled(M)?"enabled":"disabled");xrtValueArrayAppendNew(Items,M);}
    xvalue* Retiring=xrtValueArray();for(size_t I=0;I<xrtValueCount(SdkPlugins.Effective);I++){
        const xvalue* Old=xrtValueArrayGet(SdkPlugins.Effective,I),*New=SdkPluginFind(SdkPlugins.Config,SdkField(Old,"id"));
        if(SdkPluginEnabled(Old)&&(!New||!SdkPluginEnabled(New)||strcmp(SdkField(Old,"revision"),SdkField(New,"revision"))))xrtValueArrayAppend(Retiring,Old);
    }xrtValueObjectSetNew(R,XRT_STR_LITERAL("retiring"),Retiring);
    xrtValueObjectSetNew(R,XRT_STR_LITERAL("pending"),xrtValueBool(SdkPlugins.Pending));xrtValueObjectSetNew(R,XRT_STR_LITERAL("restart_required"),xrtValueBool(SdkPlugins.Recovery||SdkPluginNativeChanged(SdkPlugins.Effective,SdkPlugins.Config)));
    xrtValueObjectSetNew(R,XRT_STR_LITERAL("config_broken"),xrtValueBool(SdkPlugins.ConfigBroken));xrtValueObjectSetNew(R,XRT_STR_LITERAL("recovery_mode"),xrtValueBool(SdkPlugins.Recovery));xrtMutexUnlock(SdkPlugins.Lock);xrtValueObjectSetNew(R,XRT_STR_LITERAL("items"),Items);return R;
}
static bool SdkPluginInput(const mdo_sdk_context_v1* C,cstr Id,const xvalue* F,unsigned char** Data,size_t* Size,mdo_error_v1* E){
    cstr Storage=SdkField(F,"storage_path"),Revision=SdkField(F,"plugin_revision");
    if(!Storage&&!Revision)return SdkInputBytes(F,Data,Size,E);
    uint64 Expected=0;cstr Hash=SdkField(F,"sha256");char Path[600];bool Valid=SdkUInt(SdkGet(F,"bytes"),&Expected)&&Expected<=MDO_PLUGIN_BYTES&&Hash&&strlen(Hash)==64&&!SdkGet(F,"text")&&!SdkGet(F,"base64");
    if(Revision){
        Valid=Valid&&!Storage&&strlen(Revision)==64&&MdoPluginPath(SdkField(F,"path"));
        for(size_t I=0;Valid&&I<64;I++)if(!strchr("0123456789abcdef",Revision[I]))Valid=false;
        snprintf(Path,sizeof(Path),"plugins/%s/revisions/%s/manifest.json",Id,Valid?Revision:"invalid");bool Missing=false;
        char* Text=Valid?MdoExtensionRead(Path,true,1048576,&Missing):NULL;xvalue* M=SdkParse(Text);xrtFree(Text);
        const xvalue* Entry=MdoPluginFile(SdkGet(M,"files"),SdkField(F,"path"));uint64 N=0;
        Valid=Valid&&Entry&&SdkUInt(SdkGet(Entry,"bytes"),&N)&&N==Expected&&SdkField(Entry,"sha256")&&!strcmp(Hash,SdkField(Entry,"sha256"));xrtValueRelease(M);
        snprintf(Path,sizeof(Path),"plugins/%s/revisions/%s/files/%s",Id,Valid?Revision:"invalid",SdkField(F,"path"));
    }else{
        Valid=Valid&&MdoPackagePath(Storage)&&C->ExtensionId&&SdkIdValid(C->ExtensionId);
        snprintf(Path,sizeof(Path),"data/extensions/%s/%s",C->ExtensionId,Storage);
    }
    if(!Valid)return MdoApplicationError(E,"sdk_arguments_invalid","Choose an own immutable inventory file or bounded caller upload with size and digest","plugin",422,false);
    xfile File=MdoHomeOpenRead(Path);xfileinfo Info={0};bool Ok=File&&xrtFileStat(File,&Info)&&Info.Type==XFILE_TYPE_FILE&&Info.Size==Expected;
    *Data=Ok?xrtMalloc((size_t)Expected+1):NULL;*Size=(size_t)Expected;Ok=Ok&&*Data&&xrtReadFull(File,*Data,*Size,NULL);if(File)Ok=xrtClose(File)&&Ok;
    char Actual[65];Ok=Ok&&MdoExtensionHashBytes(*Data,*Size,Actual)&&!strcmp(Hash,Actual);
    if(!Ok){xrtFree(*Data);*Data=NULL;return MdoApplicationError(E,"sdk_plugin_upload_changed","Referenced file size or checksum changed; prepare it again","plugin",409,false);}return true;
}
static bool SdkPluginStage(const mdo_sdk_context_v1* C,const xvalue* P,xvalue** Manifest,mdo_error_v1* E){
    cstr Id=SdkField(P,"id"),Frontend=SdkField(P,"frontend");const xvalue* Files=SdkGet(P,"files"),*Doc=SdkGet(P,"manifest");
    char Why[256]={0};if(Doc&&!MdoPluginValidate(Doc,Files,Id,Why,sizeof(Why)))return MdoApplicationError(E,"sdk_plugin_manifest_invalid",Why,"plugin",422,false);
    if(Doc)Frontend=SdkField(SdkGet(Doc,"entry"),"frontend");
    if(!MdoExtensionIdValid(Id)||xrtValueType(Files)!=XVALUE_ARRAY||!xrtValueCount(Files)||xrtValueCount(Files)>128||(Frontend&&(!MdoPluginPath(Frontend)||(!Doc&&strncmp(Frontend,"web/",4)))))return MdoApplicationError(E,"sdk_arguments_invalid","Plugin needs portable inventory files and optional execution entries","plugin",422,false);
    struct {unsigned char* Bytes;size_t Size;} Decoded[128]={{0}};xvalue* Inventory=xrtValueArray();bool Ok=Inventory!=NULL,Core=false;size_t Total=0;
    for(size_t I=0;Ok&&I<xrtValueCount(Files);I++){
        const xvalue* File=xrtValueArrayGet(Files,I);cstr Name=SdkField(File,"path");
        if(!MdoPluginPath(Name)){Ok=false;break;}for(size_t J=0;J<I;J++)if(MdoPluginPathOverlap(Name,SdkField(xrtValueArrayGet(Files,J),"path")))Ok=false;
        if(!Ok||!SdkPluginInput(C,Id,File,&Decoded[I].Bytes,&Decoded[I].Size,E)){Ok=false;break;}
        size_t Size=Decoded[I].Size;if(Size>MDO_PLUGIN_BYTES-Total){Ok=false;break;}Total+=Size;char Digest[65];
        Ok=MdoExtensionHashBytes(Decoded[I].Bytes,Size,Digest);xvalue* F=xrtValueObject();Ok=Ok&&F&&SdkPut(F,"path",Name)&&SdkPut(F,"sha256",Digest)&&SdkPutUInt(F,"bytes",Size)&&xrtValueArrayAppend(Inventory,F);xrtValueRelease(F);
        if(!Doc&&(!strncmp(Name,"src/",4)||!strncmp(Name,"include/",8)||!strncmp(Name,"generated/",10)||!strcmp(Name,"main.c")||!strcmp(Name,"sources.json")))Core=true;
    }
    if(Doc){const xvalue* Overrides=SdkGet(Doc,"overrides");for(size_t I=0;I<xrtValueCount(Overrides);I++){cstr T=SdkField(xrtValueArrayGet(Overrides,I),"target");if(MdoPluginCoreTarget(T))Core=true;}}
    xvalue* Identity=xrtValueObject();Ok=Ok&&Identity&&SdkPut(Identity,"id",Id)&&xrtValueObjectSet(Identity,XRT_STR_LITERAL("files"),Inventory);
    if(Ok&&Doc)Ok=xrtValueObjectSet(Identity,XRT_STR_LITERAL("manifest"),Doc);else if(Ok)Ok=SdkPut(Identity,"frontend",Frontend);
    char Hash[65];Ok=Ok&&SdkPluginHash(Identity,Hash);xrtValueRelease(Identity);
    for(size_t I=0;Ok&&I<xrtValueCount(Files);I++){
        char Path[600];snprintf(Path,sizeof(Path),"plugins/%s/revisions/%s/files/%s",Id,Hash,SdkField(xrtValueArrayGet(Files,I),"path"));
        bool Exists=false;xfileinfo Info={0};Ok=MdoHomeExternalStat(Path,&Exists,&Info);
        if(Ok&&Exists)Ok=SdkPluginVerifyFile(Id,Hash,xrtValueArrayGet(Inventory,I));
        else if(Ok)Ok=MdoHomeAtomicWrite(Path,Decoded[I].Bytes,Decoded[I].Size,false);
    }
    bool Enabled=Doc?false:true;xrtValueGetBool(SdkGet(P,"enabled"),&Enabled);
    if(Ok){
        *Manifest=xrtValueObject();Ok=*Manifest&&SdkPut(*Manifest,"id",Id)&&SdkPut(*Manifest,"revision",Hash)&&SdkPut(*Manifest,"frontend",Frontend)&&xrtValueObjectSetNew(*Manifest,XRT_STR_LITERAL("enabled"),xrtValueBool(Enabled))&&xrtValueObjectSetNew(*Manifest,XRT_STR_LITERAL("source_reload_required"),xrtValueBool(Core));
        if(Ok&&Doc)Ok=xrtValueObjectSet(*Manifest,XRT_STR_LITERAL("manifest"),Doc);
        xvalue* Disk=Ok?xrtValueClone(*Manifest):NULL;if(Disk){xrtValueObjectRemove(Disk,XRT_STR_LITERAL("enabled"));Ok=xrtValueObjectSet(Disk,XRT_STR_LITERAL("files"),Inventory);}else Ok=false;
        char Path[256];snprintf(Path,sizeof(Path),"plugins/%s/revisions/%s/manifest.json",Id,Hash);bool Missing=false;char* Existing=Ok?MdoExtensionRead(Path,true,1048576,&Missing):NULL;
        if(Ok&&Existing){xvalue* Prior=SdkParse(Existing);Ok=Prior&&xrtValueEqual(Prior,Disk);xrtValueRelease(Prior);}
        else if(Ok){char* Text=SdkJson(Disk);Ok=Missing&&Text&&MdoHomeAtomicWrite(Path,Text,strlen(Text),false);xrtFree(Text);}
        xrtFree(Existing);xrtValueRelease(Disk);
    }
    for(size_t I=0;I<128;I++){xrtSecureZero(Decoded[I].Bytes,Decoded[I].Size);xrtFree(Decoded[I].Bytes);}xrtValueRelease(Inventory);
    if(!Ok&&!E->Code[0])MdoApplicationError(E,"sdk_plugin_stage_failed","Plugin files could not be staged or an immutable revision was changed","plugin",422,false);return Ok;
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
    if(!strncmp(Method,"plugin.preset.",14))return SdkPluginPresetCall(Method,P,R,E);
    if(!strcmp(Method,"plugin.file.read"))return SdkPluginFileCall(P,R,E);
    if(!strcmp(Method,"plugin.get")){SdkPlugin Plugin={0};bool Ok=SdkPluginLookup(SdkField(P,"id"),SdkField(P,"revision"),&Plugin,E);if(Ok){*R=Plugin.Manifest;Plugin.Manifest=NULL;}SdkPluginDrop(&Plugin);return Ok;}
    if(!strncmp(Method,"plugin.frontend.",16))return SdkPluginFrontendCall(Method,P,R,E);
    if(!strcmp(Method,"plugin.plan")){
        xrtMutexLock(SdkPlugins.Lock);xvalue* Config=xrtValueClone(SdkPlugins.Config);bool Recovery=SdkPlugins.Recovery;xrtMutexUnlock(SdkPlugins.Lock);
        (void)Recovery;
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
    xrtMutexLock(SdkPlugins.Lock);if(SdkPlugins.Busy||SdkPlugins.Readers||SdkPlugins.ConfigBroken){xrtMutexUnlock(SdkPlugins.Lock);return MdoApplicationError(E,"sdk_plugin_busy","Plugin manager is busy or in recovery mode","plugin",409,false);}SdkPlugins.Busy=true;
    size_t Index=xrtValueCount(SdkPlugins.Config);for(size_t i=0;i<xrtValueCount(SdkPlugins.Config);i++){cstr Existing=SdkField(xrtValueArrayGet(SdkPlugins.Config,i),"id");if(Existing&&!strcmp(Id,Existing))Index=i;}
    xvalue* Old=Index<xrtValueCount(SdkPlugins.Config)?xrtValueClone(xrtValueArrayGet(SdkPlugins.Config,Index)):NULL;
    xvalue* Config=xrtValueArray();for(size_t i=0;i<xrtValueCount(SdkPlugins.Config);i++)if(i!=Index)xrtValueArrayAppend(Config,xrtValueArrayGet(SdkPlugins.Config,i));xrtMutexUnlock(SdkPlugins.Lock);
    bool Remove=!strcmp(Method,"plugin.remove"),Ok=true;xvalue* Manifest=NULL;SdkPlugin Candidate={0};xvalue* Map=NULL;
    bool Managed=Old&&SdkField(Old,"source")&&!strcmp(SdkField(Old,"source"),"store");
    if(Managed&&(!strcmp(Method,"plugin.apply")||!strcmp(Method,"plugin.remove")||!strcmp(Method,"plugin.rollback")))Ok=MdoApplicationError(E,"store_resource_readonly","Use the store update or uninstall flow for managed plugins","plugin",403,false);
    else if(!strcmp(Method,"plugin.apply")){Ok=SdkPluginStage(C,P,&Manifest,E);if(Ok&&Old&&!SdkGet(P,"enabled"))xrtValueObjectSet(Manifest,XRT_STR_LITERAL("enabled"),SdkGet(Old,"enabled"));}
    else if(!Old)Ok=MdoApplicationError(E,"sdk_plugin_missing","Plugin is not installed","plugin",404,false);
    else {Manifest=xrtValueClone(Old);if(!strcmp(Method,"plugin.rollback")){cstr Previous=SdkField(Old,"previous");if(!Previous||strlen(Previous)!=64)Ok=MdoApplicationError(E,"sdk_plugin_no_previous","No previous revision is retained","plugin",409,false);else {
            char Path[256];snprintf(Path,sizeof(Path),"plugins/%s/revisions/%s/manifest.json",Id,Previous);bool Missing=false;char* Text=MdoExtensionRead(Path,true,1024u*1024u,&Missing);xvalue* Saved=SdkParse(Text);xrtFree(Text);
            if(!Saved)Ok=false;else{SdkPut(Manifest,"revision",Previous);SdkPut(Manifest,"frontend",SdkField(Saved,"frontend"));if(SdkGet(Saved,"manifest"))xrtValueObjectSet(Manifest,XRT_STR_LITERAL("manifest"),SdkGet(Saved,"manifest"));else xrtValueObjectRemove(Manifest,XRT_STR_LITERAL("manifest"));xrtValueObjectSet(Manifest,XRT_STR_LITERAL("source_reload_required"),SdkGet(Saved,"source_reload_required"));}xrtValueRelease(Saved);}}
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
    xvalue* Effective=Ok?xrtValueClone(Config):NULL;Ok=Ok&&Effective;
    if(Ok)Ok=SdkPluginConfigSave(Config);
    xrtMutexLock(SdkPlugins.Lock);
    if(Ok){for(size_t i=0;i<32;i++)if(SdkPlugins.Plugins[i].Manifest&&!strcmp(Id,SdkField(SdkPlugins.Plugins[i].Manifest,"id")))SdkPluginDrop(&SdkPlugins.Plugins[i]);
        if(!Remove){for(size_t i=0;i<32;i++)if(!SdkPlugins.Plugins[i].Manifest){SdkPlugins.Plugins[i]=Candidate;memset(&Candidate,0,sizeof(Candidate));break;}}
        xrtValueRelease(SdkPlugins.Config);SdkPlugins.Config=Config;Config=NULL;}
    if(Ok){SdkPlugins.Pending=!SdkPluginCanPublish(SdkPlugins.Config);if(!SdkPlugins.Pending){xrtValueRelease(SdkPlugins.Map);SdkPlugins.Map=Map;Map=NULL;xrtValueRelease(SdkPlugins.Effective);SdkPlugins.Effective=Effective;Effective=NULL;}}
    SdkPlugins.Busy=false;xrtMutexUnlock(SdkPlugins.Lock);SdkPluginDrop(&Candidate);xrtValueRelease(Config);xrtValueRelease(Old);xrtValueRelease(Manifest);xrtValueRelease(Map);xrtValueRelease(Effective);
    if(!Ok){if(!E->Code[0])MdoApplicationError(E,"sdk_plugin_publish_failed","Plugin was not published; previous configuration is retained","plugin",500,false);return false;}
    xvalue* Change=xrtValueObject();SdkPut(Change,"id",Id);SdkPut(Change,"method",Method);MdoApplicationEvent("plugin.changed",C,Change);xrtValueRelease(Change);
    *R=SdkPluginList();return true;
}
