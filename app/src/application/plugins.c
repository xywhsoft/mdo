#include "internal.h"
#include "../../include/mdo/extension_files.h"
#include "../../include/mdo/extensions.h"
#include "../../include/mdo/plugin_format.h"
#include "../../include/mdo/modules.h"
#include "../../include/mdo/time.h"

typedef struct SdkPlugin {xvalue* Manifest;char Error[256];} SdkPlugin;
static struct {
 xmutex* Lock;xvalue* Config;SdkPlugin Plugins[32];MdoExtensionLease* Pins[32];
 xvalue *Map,*Effective,*Frontends,*Failures;xvfsmount Mount,HomeMount;
 size_t Readers;bool Busy,Recovery,Pending,ConfigBroken;
} SdkPlugins;
static const cstr SdkPluginMethods[]={"plugin.list","plugin.get","plugin.file.read","plugin.enable","plugin.disable","plugin.plan","plugin.commit","plugin.preset.list","plugin.preset.save","plugin.preset.remove","plugin.preset.plan","plugin.frontend.hello","plugin.frontend.enter","plugin.frontend.leave","plugin.frontend.fail","runtime.reload","runtime.reload.status"};
static void SdkPluginDrop(SdkPlugin* P){xrtValueRelease(P->Manifest);memset(P,0,sizeof(*P));}
static bool SdkPluginEnabled(const xvalue* M){bool Enabled=true;xrtValueGetBool(SdkGet(M,"enabled"),&Enabled);return Enabled;}
#include "plugins_vfs.inc.c"

static bool SdkPluginMountDirectory(SdkPlugin* P,const xvalue* Summary){
 cstr Id=SdkField(Summary,"id");char Error[512]={0},Hash[65];xvalue* Meta=MdoExtensionsRead("plugins",Id,Error);
 cstr Root=SdkField(Meta,"root"),Entry=SdkField(Meta,"entry");char* Path=Root&&Entry?xrtFormat("%s/%s",Root,Entry):NULL;
 char* Text=Path?MdoExtensionRead(Path,true,1048576,NULL):NULL;xvalue* Doc=Text?SdkParse(Text):NULL;xrtFree(Text);xrtFree(Path);
 xvalue* Files=Meta?MdoExtensionsFiles("plugins",Id,NULL,Error):NULL;
 bool Ok=Meta&&Doc&&Files&&MdoExtensionsVerify("plugins",Id,NULL,Error)&&MdoExtensionsFingerprint("plugins",Id,Hash,NULL,Error)&&MdoPluginValidate(Doc,Files,SdkField(Meta,"alias"),Error,sizeof(Error));
 if(Ok){P->Manifest=xrtValueClone(Meta);Ok=P->Manifest&&SdkPut(P->Manifest,"revision",Hash)&&xrtValueObjectSet(P->Manifest,XRT_STR_LITERAL("files"),Files)&&xrtValueObjectSet(P->Manifest,XRT_STR_LITERAL("manifest"),Doc);
  /* Display metadata remains in the root manifest. These are a runtime view. */
  const cstr Keys[]={"name","author","copyright","license","version"};for(size_t I=0;Ok&&I<5;I++)if(SdkField(Meta,Keys[I]))Ok=SdkPut(Doc,Keys[I],SdkField(Meta,Keys[I]));
  const xvalue* Flag=SdkGet(Summary,"enabled");if(Flag)Ok=Ok&&xrtValueObjectSet(P->Manifest,XRT_STR_LITERAL("enabled"),Flag);
  cstr Frontend=SdkField(SdkGet(Doc,"entry"),"frontend");char* Virtual=Frontend?xrtFormat("web/__mdo_plugins/%s/%s/%s",Id,Hash,Frontend):NULL;
  if(Frontend)Ok=Ok&&Virtual&&SdkPut(P->Manifest,"frontend",Virtual);xrtFree(Virtual);
 }
 if(!Ok){SdkPluginDrop(P);P->Manifest=xrtValueClone(Summary);snprintf(P->Error,sizeof(P->Error),"%.255s",Error[0]?Error:"Plugin recipe or directory is invalid");}
 xrtValueRelease(Meta);xrtValueRelease(Doc);xrtValueRelease(Files);return Ok;
}
/* Effective views pin their physical directories independently of the desired
 * selection. A pending native change never releases live code/resources. */
static bool SdkPluginPublishView(xvalue* Map,xvalue* Effective){
 MdoExtensionLease* Pins[32]={0};xvalue* NewMap=Map?xrtValueClone(Map):NULL,*NewEffective=Effective?xrtValueClone(Effective):NULL;bool Ok=NewMap&&NewEffective;
 for(size_t I=0;Ok&&I<xrtValueCount(Effective);I++)if(SdkPluginEnabled(xrtValueArrayGet(Effective,I))){
  char Error[512];Pins[I]=MdoExtensionsAcquire("plugins",SdkField(xrtValueArrayGet(Effective,I),"id"),Error);Ok=Pins[I]!=NULL;}
 if(Ok){for(size_t I=0;I<32;I++){MdoExtensionsRelease(SdkPlugins.Pins[I]);SdkPlugins.Pins[I]=Pins[I];Pins[I]=NULL;}
  xrtValueRelease(SdkPlugins.Map);SdkPlugins.Map=NewMap;NewMap=NULL;xrtValueRelease(SdkPlugins.Effective);SdkPlugins.Effective=NewEffective;NewEffective=NULL;}
 for(size_t I=0;I<32;I++)MdoExtensionsRelease(Pins[I]);xrtValueRelease(NewMap);xrtValueRelease(NewEffective);return Ok;
}
#include "plugins_plan.inc.c"
#include "plugins_runtime.inc.c"
#include "plugins_manage.inc.c"
static bool SdkPluginConfigSave(const xvalue* Config){
 xvalue* Flags=xrtValueArray();bool Ok=Flags!=NULL;
 for(size_t I=0;Ok&&I<xrtValueCount(Config);I++){const xvalue* P=xrtValueArrayGet(Config,I);xvalue* F=xrtValueObject();Ok=F&&SdkPut(F,"id",SdkField(P,"id"))&&xrtValueObjectSetNew(F,XRT_STR_LITERAL("enabled"),xrtValueBool(SdkPluginEnabled(P)))&&xrtValueArrayAppend(Flags,F);xrtValueRelease(F);}
 char Error[512];Ok=Ok&&MdoExtensionsSetActivation("plugins",Flags,Error);xrtValueRelease(Flags);return Ok;
}
bool MdoApplicationPluginsStoreBegin(xvalue** Config,char* Error,size_t Capacity){
 if(!Config||!SdkPlugins.Lock)return false;*Config=NULL;xrtMutexLock(SdkPlugins.Lock);
 bool Ok=!SdkPlugins.Busy&&!SdkPlugins.Readers&&!SdkPlugins.ConfigBroken;
 if(Ok){*Config=xrtValueClone(SdkPlugins.Config);Ok=*Config!=NULL;if(Ok)SdkPlugins.Busy=true;}xrtMutexUnlock(SdkPlugins.Lock);
 if(!Ok&&Error&&Capacity)snprintf(Error,Capacity,"Plugin manager is busy; wait for active readers");return Ok;
}
void MdoApplicationPluginsStoreEnd(void){if(SdkPlugins.Lock){xrtMutexLock(SdkPlugins.Lock);SdkPlugins.Busy=false;xrtMutexUnlock(SdkPlugins.Lock);}}
xvalue* MdoApplicationPluginsStoreIssues(const xvalue* Config,const xvalue* Prepared){
 xvalue* Issues=xrtValueArray();SdkPlugin All[32]={0};bool Ok=Issues&&xrtValueType(Config)==XVALUE_ARRAY&&xrtValueCount(Config)<=32;
 if(Ok&&xrtValueCount(Prepared))SdkPluginIssue(Issues,"directory_zip_required","","","Install a complete plugin directory ZIP with the common extension API");
 for(size_t I=0;Ok&&I<xrtValueCount(Config);I++)if(!SdkPluginMountDirectory(&All[I],xrtValueArrayGet(Config,I))&&SdkPluginEnabled(xrtValueArrayGet(Config,I)))SdkPluginIssue(Issues,"invalid_directory",SdkField(xrtValueArrayGet(Config,I),"id"),"",All[I].Error);
 if(Ok)SdkPluginSetValidate(All,Issues);xvalue* Map=Ok&&!xrtValueCount(Issues)?SdkPluginMapBuild(All):NULL;
 if(Ok&&!Map&&!xrtValueCount(Issues))SdkPluginIssue(Issues,"files","","","Plugin target mappings overlap");
 for(size_t I=0;I<32;I++)SdkPluginDrop(&All[I]);xrtValueRelease(Map);if(!Ok){xrtValueRelease(Issues);return NULL;}return Issues;
}
bool MdoApplicationPluginsStorePublish(const xvalue* Config,char* Error,size_t Capacity){
 if(!SdkPlugins.Lock||xrtValueType(Config)!=XVALUE_ARRAY||xrtValueCount(Config)>32)return false;
 SdkPlugin All[32]={0};bool Ok=true;for(size_t I=0;Ok&&I<xrtValueCount(Config);I++)if(!SdkPluginMountDirectory(&All[I],xrtValueArrayGet(Config,I))&&SdkPluginEnabled(xrtValueArrayGet(Config,I)))Ok=false;
 mdo_error_v1 E={0};if(Ok)Ok=SdkPluginSetCheck(All,&E);xvalue* Map=Ok?SdkPluginMapBuild(All):NULL,*Next=Ok?xrtValueArray():NULL;
 for(size_t I=0;Ok&&I<xrtValueCount(Config);I++)Ok=xrtValueArrayAppend(Next,All[I].Manifest);Ok=Ok&&Map&&Next;
 if(Ok){xrtMutexLock(SdkPlugins.Lock);Ok=SdkPlugins.Busy&&!SdkPlugins.Readers;
  bool Pending=Ok&&!SdkPluginCanPublish(Next);if(Ok&&!Pending)Ok=SdkPluginPublishView(Map,Next);
  if(Ok){for(size_t I=0;I<32;I++){SdkPluginDrop(&SdkPlugins.Plugins[I]);SdkPlugins.Plugins[I]=All[I];memset(&All[I],0,sizeof(All[I]));}
   xrtValueRelease(SdkPlugins.Config);SdkPlugins.Config=Next;Next=NULL;SdkPlugins.Pending=Pending;}
  xrtMutexUnlock(SdkPlugins.Lock);}
 for(size_t I=0;I<32;I++)SdkPluginDrop(&All[I]);xrtValueRelease(Map);xrtValueRelease(Next);
 if(!Ok&&Error&&Capacity)snprintf(Error,Capacity,"%s",E.Message[0]?E.Message:"Plugin directory could not be activated; previous runtime retained");return Ok;
}
bool MdoApplicationPluginsInit(void){
 if(SdkPlugins.Lock)return true;if(!MdoExtensionsInit())return false;SdkPlugins.Lock=xrtMutexCreate();if(!SdkPlugins.Lock)return false;
 SdkPlugins.Frontends=xrtValueArray();SdkPlugins.Failures=xrtValueArray();SdkPlugins.Config=xrtValueArray();SdkPlugins.Effective=xrtValueArray();SdkPlugins.Map=xrtValueArray();
 char* Mode=xrtEnvGet("MDO_PLUGINS_DISABLED");SdkPlugins.Recovery=Mode&&strcmp(Mode,"0");xrtFree(Mode);char Error[512]={0};xvalue* Rows=MdoExtensionsList("plugins",Error);
 if(!Rows||xrtValueCount(Rows)>32){SdkPlugins.Recovery=true;SdkPlugins.ConfigBroken=true;}
 for(size_t I=0;I<xrtValueCount(Rows)&&I<32;I++){SdkPluginMountDirectory(&SdkPlugins.Plugins[I],xrtValueArrayGet(Rows,I));xrtValueArrayAppend(SdkPlugins.Config,SdkPlugins.Plugins[I].Manifest);}
 mdo_error_v1 E={0};if(!SdkPlugins.Recovery&&!SdkPluginSetCheck(SdkPlugins.Plugins,&E))SdkPlugins.Recovery=true;
 xvalue* Effective=xrtValueArray();for(size_t I=0;!SdkPlugins.Recovery&&I<32;I++)if(SdkPlugins.Plugins[I].Manifest&&!SdkPlugins.Plugins[I].Error[0])xrtValueArrayAppend(Effective,SdkPlugins.Plugins[I].Manifest);
 xvalue* Map=SdkPlugins.Recovery?xrtValueArray():SdkPluginMapBuild(SdkPlugins.Plugins);
 if(!Map||!SdkPluginPublishView(Map,Effective)){SdkPlugins.Recovery=true;SdkPlugins.Pending=true;}
 xrtValueRelease(Map);xrtValueRelease(Effective);xrtValueRelease(Rows);
 SdkPlugins.Mount=xrtVfsMount(MdoHomeApplicationVfs(),"/app",2000,XVFS_CASE_SENSITIVE,&SdkPluginProvider,NULL,0);
 SdkPlugins.HomeMount=xrtVfsMount(MdoHomeApplicationVfs(),"/app/default-home",2000,XVFS_CASE_SENSITIVE,&SdkPluginHomeProvider,NULL,0);
 return SdkPlugins.Map&&SdkPlugins.Mount&&SdkPlugins.HomeMount&&SdkPlugins.Config&&SdkPlugins.Effective&&SdkPlugins.Frontends&&SdkPlugins.Failures;
}
bool MdoApplicationPluginsReload(char* Error,size_t Capacity){
 if(!SdkPlugins.Lock)return true;xvalue* Before=NULL;if(!MdoApplicationPluginsStoreBegin(&Before,Error,Capacity))return false;
 char Why[512]={0};xvalue* Rows=MdoExtensionsList("plugins",Why);bool Ok=Rows&&MdoApplicationPluginsStorePublish(Rows,Error,Capacity);xrtValueRelease(Rows);xrtValueRelease(Before);MdoApplicationPluginsStoreEnd();
 if(!Ok&&Error&&Capacity&&!Error[0])snprintf(Error,Capacity,"%s",Why[0]?Why:"Plugin catalogue could not be refreshed");return Ok;
}
void MdoApplicationPluginsUnit(void){
 if(!SdkPlugins.Lock)return;if(SdkPlugins.HomeMount){xrtVfsUnmount(SdkPlugins.HomeMount);xrtVfsMountDestroy(SdkPlugins.HomeMount);}if(SdkPlugins.Mount){xrtVfsUnmount(SdkPlugins.Mount);xrtVfsMountDestroy(SdkPlugins.Mount);}
 for(size_t I=0;I<32;I++){SdkPluginDrop(&SdkPlugins.Plugins[I]);MdoExtensionsRelease(SdkPlugins.Pins[I]);}
 xrtValueRelease(SdkPlugins.Config);xrtValueRelease(SdkPlugins.Map);xrtValueRelease(SdkPlugins.Effective);xrtValueRelease(SdkPlugins.Frontends);xrtValueRelease(SdkPlugins.Failures);xrtMutexDestroy(SdkPlugins.Lock);memset(&SdkPlugins,0,sizeof(SdkPlugins));
}
xvalue* MdoApplicationPluginsCatalog(void){xvalue* A=xrtValueArray();for(size_t I=0;I<sizeof(SdkPluginMethods)/sizeof(*SdkPluginMethods);I++){xvalue* V=xrtValueObject();SdkPut(V,"id",SdkPluginMethods[I]);SdkPut(V,"domain","plugin");SdkPut(V,"transport","native");xrtValueArrayAppendNew(A,V);}return A;}
static xvalue* SdkPluginList(void){
 xvalue* R=xrtValueObject(),*Items=xrtValueArray();xrtMutexLock(SdkPlugins.Lock);
 for(size_t I=0;I<32;I++)if(SdkPlugins.Plugins[I].Manifest){xvalue* M=xrtValueClone(SdkPlugins.Plugins[I].Manifest);SdkPut(M,"error",SdkPlugins.Plugins[I].Error);const xvalue* Active=SdkPluginFind(SdkPlugins.Effective,SdkField(M,"id"));
  SdkPutUInt(M,"file_count",xrtValueCount(SdkGet(M,"files")));xrtValueObjectRemove(M,XRT_STR_LITERAL("files"));
  xrtValueObjectSetNew(M,XRT_STR_LITERAL("effective_enabled"),xrtValueBool(Active&&SdkPluginEnabled(Active)));SdkPut(M,"effective_revision",Active?SdkField(Active,"revision"):"");SdkPut(M,"state",SdkPlugins.Plugins[I].Error[0]?"failed":SdkPlugins.Recovery?"recovery":SdkPlugins.Pending?"pending":SdkPluginEnabled(M)?"enabled":"disabled");xrtValueArrayAppendNew(Items,M);}
 xrtValueObjectSetNew(R,XRT_STR_LITERAL("items"),Items);xrtValueObjectSetNew(R,XRT_STR_LITERAL("pending"),xrtValueBool(SdkPlugins.Pending));xrtValueObjectSetNew(R,XRT_STR_LITERAL("restart_required"),xrtValueBool(SdkPlugins.Recovery||SdkPluginNativeChanged(SdkPlugins.Effective,SdkPlugins.Config)));xrtValueObjectSetNew(R,XRT_STR_LITERAL("recovery_mode"),xrtValueBool(SdkPlugins.Recovery));xrtMutexUnlock(SdkPlugins.Lock);return R;
}
bool MdoApplicationPluginsCall(const mdo_sdk_context_v1* C,cstr Method,const xvalue* P,xvalue** R,mdo_error_v1* E,bool* Found){
 *Found=false;for(size_t I=0;I<sizeof(SdkPluginMethods)/sizeof(*SdkPluginMethods);I++)if(!strcmp(Method,SdkPluginMethods[I]))*Found=true;if(!*Found)return true;
 if(!strcmp(Method,"runtime.reload")){cstr Domain=SdkField(P,"domain");if(Domain&&!strcmp(Domain,"modules")){if(!MdoModuleManagerReload())return MdoApplicationError(E,"sdk_reload_failed","Module candidate rejected","reload",409,false);*R=xrtValueObject();return true;}
  if(!Domain||strcmp(Domain,"host"))return MdoApplicationError(E,"sdk_arguments_invalid","Reload domain must be modules or host","reload",400,false);
  xrtMutexLock(SdkPlugins.Lock);bool Restart=SdkPluginNativeChanged(SdkPlugins.Effective,SdkPlugins.Config);xrtMutexUnlock(SdkPlugins.Lock);if(Restart)return MdoApplicationError(E,"sdk_plugin_restart_required","Pending native/code plugin activation requires a complete application restart","plugin",409,false);
  XS_ServerInfo* Server=MdoApplicationServer();uint64 Id=Server?xsReloadServerSubmit(Server->Name):0;if(!Id)return MdoApplicationError(E,"sdk_reload_failed","Host reload could not be submitted","reload",409,false);*R=xrtValueObject();SdkPutUInt(*R,"id",Id);return true;}
 if(!strcmp(Method,"runtime.reload.status")){uint64 Id=0;XS_ReloadResult Q={0};if(!SdkUInt(SdkGet(P,"id"),&Id)||!xsReloadQuery(Id,&Q))return MdoApplicationError(E,"sdk_reload_missing","Reload receipt unavailable","reload",404,false);*R=xrtValueObject();SdkPutUInt(*R,"state",Q.State);SdkPut(*R,"message",Q.Message);return true;}
 if(!SdkPlugins.Lock)return MdoApplicationError(E,"sdk_unavailable","Plugin manager unavailable","plugin",503,false);
 if(!strcmp(Method,"plugin.list")){*R=SdkPluginList();return true;}
 if(!strncmp(Method,"plugin.preset.",14))return SdkPluginPresetCall(Method,P,R,E);
 if(!strcmp(Method,"plugin.file.read"))return SdkPluginFileCall(P,R,E);
 if(!strcmp(Method,"plugin.get")){SdkPlugin V={0};bool Ok=SdkPluginLookup(SdkField(P,"id"),SdkField(P,"revision"),&V,E);if(Ok){*R=V.Manifest;V.Manifest=NULL;}SdkPluginDrop(&V);return Ok;}
 if(!strncmp(Method,"plugin.frontend.",16))return SdkPluginFrontendCall(Method,P,R,E);
 if(!strcmp(Method,"plugin.plan")){xrtMutexLock(SdkPlugins.Lock);xvalue* Config=xrtValueClone(SdkPlugins.Config);xrtMutexUnlock(SdkPlugins.Lock);*R=Config?SdkPluginPlan(Config,SdkGet(P,"changes"),NULL,E):NULL;xrtValueRelease(Config);return *R!=NULL;}
 xvalue* Before=NULL,*Desired=NULL,*Plan=NULL,*Changes=NULL;
 if(!MdoApplicationPluginsStoreBegin(&Before,E->Message,sizeof(E->Message)))return MdoApplicationError(E,"sdk_plugin_busy","Plugin manager is busy","plugin",409,false);
 if(!strcmp(Method,"plugin.commit"))Changes=xrtValueClone(SdkGet(P,"changes"));
 else{cstr Id=SdkField(P,"id");Changes=xrtValueArray();xvalue* F=xrtValueObject();SdkPut(F,"id",Id);xrtValueObjectSetNew(F,XRT_STR_LITERAL("enabled"),xrtValueBool(!strcmp(Method,"plugin.enable")));xrtValueArrayAppendNew(Changes,F);}
 Plan=SdkPluginPlan(Before,Changes,&Desired,E);bool Allowed=false;bool Ok=Plan&&xrtValueGetBool(SdkGet(Plan,"allowed"),&Allowed)&&Allowed;
 if(Ok&&!strcmp(Method,"plugin.commit")){cstr Base=SdkField(P,"expected_revision"),After=SdkField(P,"expected_plan");if(!Base||!After||strcmp(Base,SdkField(Plan,"base_revision"))||strcmp(After,SdkField(Plan,"plan_revision")))Ok=MdoApplicationError(E,"sdk_plugin_plan_changed","Review a fresh plugin combination plan","plugin",412,false);}
 if(Plan&&!Allowed)MdoApplicationError(E,"sdk_plugin_conflict","Resolve the plugin dependencies, exclusive markers or file targets first","plugin",409,false);
 if(Ok)Ok=SdkPluginConfigSave(Desired);
 if(Ok&&!MdoApplicationPluginsStorePublish(Desired,E->Message,sizeof(E->Message))){bool Restored=SdkPluginConfigSave(Before);Ok=MdoApplicationError(E,"sdk_plugin_publish_failed",Restored?"Previous activation retained":"Plugin activation needs recovery","plugin",500,!Restored);}
 MdoApplicationPluginsStoreEnd();xrtValueRelease(Before);xrtValueRelease(Desired);xrtValueRelease(Changes);xrtValueRelease(Plan);
 if(Ok){MdoApplicationEvent("plugin.changed",C,NULL);*R=SdkPluginList();}return Ok;
}
