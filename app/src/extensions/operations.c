#include "../../include/mdo/extensions.h"
#include "../../include/mdo/extension_manifest.h"
#include "../../include/mdo/home.h"
#include "../../include/mdo/modules.h"
#include "../../include/mdo/skills.h"
#include "../../include/mdo/mcp.h"
#include "../../include/mdo/distribution.h"
#include "../../include/mdo/application.h"
#include "../../include/mdo/diagnostics.h"
#include "../../include/mdo/android_tools.h"
#include "../../include/mdo/project_lifecycle.h"
#include "../../include/mdo/projects.h"

static bool MdoExtOpFailure(char Error[512],cstr Message){if(Error)snprintf(Error,512,"%s",Message);return false;}
static bool MdoExtOpBool(const xvalue* V,cstr Key,bool Default){bool B=Default;xrtValueGetBool(xrtValueObjectGet(V,xrtStrView(Key)),&B);return B;}
static bool MdoExtOpPut(xvalue* V,cstr K,cstr S){return S&&xrtValueObjectSetNew(V,xrtStrView(K),xrtValueString(xrtStrView(S)));}
static bool MdoExtOpReload(cstr Kind,char Error[512]){
 bool Ok=true;
 if(!strcmp(Kind,"agents")||!strcmp(Kind,"subagents")||!strcmp(Kind,"tools")){
  MdoModuleCatalog* C=MdoModuleCatalogSnapshot();bool Live=C!=NULL;MdoModuleCatalogRelease(C);if(Live)Ok=MdoModuleManagerReload();
 }else if(!strcmp(Kind,"skills")){MdoSkillCatalog* C=MdoSkillCatalogSnapshot();bool Live=C!=NULL;MdoSkillCatalogRelease(C);if(Live)Ok=MdoSkillManagerReload();}
 else if(!strcmp(Kind,"mcp")){MdoMcpCatalog* C=MdoMcpCatalogSnapshot();bool Live=C!=NULL;MdoMcpCatalogRelease(C);if(Live)Ok=MdoMcpManagerReload();}
 else if(!strcmp(Kind,"external-tools"))Ok=MdoCliReload();
 else if(!strcmp(Kind,"plugins"))Ok=MdoApplicationPluginsReload(Error,512);
 if(!Ok&&Error&&!Error[0])snprintf(Error,512,"The runtime could not retire or load this extension; finish its active calls or inspect its configuration");
 return Ok;
}
static xvalue* MdoExtOpItem(cstr Kind,cstr Id,char Error[512]){
 xvalue* M=MdoExtensionsRead(Kind,Id,Error);if(M)return M;
 xvalue* Rows=MdoExtensionsList(Kind,Error);for(size_t I=0;I<xrtValueCount(Rows);I++){const xvalue* Row=xrtValueArrayGet(Rows,I);cstr Found=MdoExtensionText(Row,"directory_id",96);if(Found&&!strcmp(Id,Found)){M=xrtValueClone(Row);break;}}xrtValueRelease(Rows);if(M&&Error)Error[0]=0;return M;
}
static bool MdoExtOpRestore(cstr Kind,cstr Id,bool Enabled,char Error[512]){
 return MdoExtensionsSetEnabled(Kind,Id,Enabled,Error)&&MdoExtOpReload(Kind,Error);
}
static bool MdoExtOpReloadGroup(const xvalue* Items,bool Retiring,void* Data,char Error[512]){
 (void)Data;const cstr Retire[]={"plugins","skills","tools","mcp","external-tools"},Resume[]={"external-tools","mcp","tools","skills","plugins"};const cstr* Kinds=Retiring?Retire:Resume;bool Ok=true;
 for(size_t K=0;K<5;K++){bool Needed=false;for(size_t I=0;I<xrtValueCount(Items);I++){cstr Kind=MdoExtensionText(xrtValueArrayGet(Items,I),"kind",32);if(Kind&&(!strcmp(Kind,Kinds[K])||(!strcmp(Kinds[K],"tools")&&(!strcmp(Kind,"agents")||!strcmp(Kind,"subagents")))))Needed=true;}
  char Why[512]={0};if(Needed&&!MdoExtOpReload(Kinds[K],Why)){Ok=false;if(Error&&!Error[0])snprintf(Error,512,"%s",Why);}}
 return Ok;
}
bool MdoExtensionsApplyPlan(const xvalue* Input,const xvalue* Downloads,xcancel* Cancel,xvalue** Result,char Error[512]){
 *Result=NULL;cstr Id=MdoExtensionText(Input,"plan_id",32),Revision=MdoExtensionText(Input,"revision",64);xvalue* Plan=MdoExtensionsPlanRead(Id,Revision,Error);if(!Plan)return false;
 bool Ok=MdoExtensionsMutationBegin();if(!Ok)MdoExtOpFailure(Error,"Another extension operation is still running");
 else{Ok=MdoExtensionsApplyGroup(Plan,Downloads,MdoExtOpBool(Input,"trust_code",false),Cancel,MdoExtOpReloadGroup,NULL,Result,Error);MdoExtensionsMutationEnd();}
 if(Ok&&*Result){bool Plugins=false;const xvalue* Items=xrtValueObjectGet(Plan,XRT_STR_LITERAL("items"));for(size_t I=0;I<xrtValueCount(Items);I++){cstr Kind=MdoExtensionText(xrtValueArrayGet(Items,I),"kind",32);if(Kind&&!strcmp(Kind,"plugins"))Plugins=true;}if(Plugins){bool Restart=false,Pending=MdoApplicationPluginsPending(&Restart);xrtValueObjectSetNew(*Result,XRT_STR_LITERAL("pending"),xrtValueBool(Pending));xrtValueObjectSetNew(*Result,XRT_STR_LITERAL("restart_required"),xrtValueBool(Restart));if(Pending)xrtValueObjectSetNew(*Result,XRT_STR_LITERAL("runtime_ready"),xrtValueBool(false));}}
 if(Ok&&*Result){char Why[512]={0};if(!MdoExtensionsPlanReceiptRefresh(Id,Revision,*Result,Why))MdoExtOpPut(*Result,"receipt_error",Why);}
 xrtValueRelease(Plan);return Ok;
}
bool MdoExtensionsManage(const xvalue* Input,const xvalue* Credits,xcancel* Cancel,xvalue** Result,char Error[512]){
 *Result=NULL;if(Error)Error[0]=0;cstr Kind=MdoExtensionText(Input,"kind",32),Id=MdoExtensionText(Input,"id",MDO_EXTENSION_REF_BYTES),Action=MdoExtensionText(Input,"action",32),Expected=MdoExtensionText(Input,"expected_revision",64);char Physical[97];
 if(!MdoExtensionDirectoryKind(Kind)||!Id||!Action||xrtValueType(Input)!=XVALUE_OBJECT)return MdoExtOpFailure(Error,"Choose an extension type, directory and operation");
 if(strchr(Id,':')){char* Resolved=MdoExtensionsDirectoryId(Kind,Id,Error);if(!Resolved)return false;snprintf(Physical,sizeof(Physical),"%s",Resolved);xrtFree(Resolved);Id=Physical;}
 if(!MdoExtensionDirectoryId(Id))return MdoExtOpFailure(Error,"Invalid extension directory identity");
 if(MdoExtensionsRecoveryBlocked(Kind,Id,Error))return false;
 const cstr Flags[]={"enabled","trust_code"};for(size_t I=0;I<2;I++){bool Flag;const xvalue* V=xrtValueObjectGet(Input,xrtStrView(Flags[I]));if(V&&!xrtValueGetBool(V,&Flag))return MdoExtOpFailure(Error,"Activation and code approval must be boolean values");}
 bool Create=!strcmp(Action,"create"),Save=!strcmp(Action,"save"),Import=!strcmp(Action,"import"),Install=!strcmp(Action,"install"),Remove=!strcmp(Action,"remove"),Enable=!strcmp(Action,"enable"),Edit=!strncmp(Action,"file.",5),CancelRemove=!strcmp(Action,"cancel_remove"),Bind=!strcmp(Action,"bind");
#if defined(__ANDROID__)
 if(!strcmp(Kind,"external-tools")&&(Create||Save||Import||Install||Edit))return MdoExtOpFailure(Error,"Android external programs are installed and updated through the full APK");
#endif
 if(!Create&&!Save&&!Import&&!Install&&!Remove&&!Enable&&!Edit&&!CancelRemove&&!Bind)return MdoExtOpFailure(Error,"Unknown directory operation");
 if(Cancel&&xrtCancelRequested(Cancel))return MdoExtOpFailure(Error,"Extension operation cancelled before changing files");
 if(!MdoExtensionsMutationBegin())return MdoExtOpFailure(Error,"Another extension operation is still running");
 bool Ok=false,Trust=MdoExtOpBool(Input,"trust_code",false),WasEnabled=false,Suspended=false;char RuntimeError[512]={0};xvalue* Before=MdoExtOpItem(Kind,Id,RuntimeError);
 if(Before)WasEnabled=MdoExtOpBool(Before,"desired_enabled",false);
 cstr Source=MdoExtensionText(Before,"source",16);
 if(Bind){
  cstr Ref=MdoExtensionText(Before,"id",MDO_EXTENSION_REF_BYTES),Dependency=MdoExtensionText(Input,"dependency_id",MDO_EXTENSION_REF_BYTES),Connection=MdoExtensionText(Input,"connection_id",96),Scope=MdoExtensionText(Input,"scope",64),Revision=MdoExtensionText(Input,"expected_binding_revision",64);char* Workspace=NULL;
  Ok=Ref&&Dependency&&Connection&&(!Scope||!Scope[0]||!strcmp(Scope,"global")||MdoExtensionDirectoryId(Scope));
  if(Ok&&Scope&&Scope[0]&&strcmp(Scope,"global")){
   if(!strcmp(Scope,"default"))Workspace=MdoHomeDefaultWorkspacePath(false);
   else{MdoProjectInfo Project={0};Project.Size=sizeof(Project);bool Found=false;xwork_error Why;if(MdoProjectGet(Scope,&Project,&Found,&Why)&&Found)Workspace=xrtStrDup(Project.WorkspaceRoot);}Ok=Workspace!=NULL;
  }
  if(Ok&&Connection[0]){char* Match=MdoMcpDependencyConnection(Dependency,Workspace,Connection,false,Error);Ok=Match!=NULL;xrtFree(Match);}
  if(Ok)Ok=MdoExtensionsBind(Ref,Dependency,Scope,Connection,Revision,Result,Error);
  xrtFree(Workspace);goto done;
 }
 if(Install&&Before&&Source&&strcmp(Source,"store")&&MdoExtensionText(Credits,"source",16)&&!strcmp(MdoExtensionText(Credits,"source",16),"store")){MdoExtOpFailure(Error,"A local extension already owns this directory identity; store installation will not overwrite it");goto done;}
 if((Edit||(Save&&Before))&&(!Before||!Source||strcmp(Source,"manual"))){MdoExtOpFailure(Error,"Only locally authored manual extensions can be edited");goto done;}
 if((Import||Install)&&Source&&!strcmp(Source,"store")){cstr NewSource=MdoExtensionText(Credits,"source",16);if(!NewSource||strcmp(NewSource,"store")){MdoExtOpFailure(Error,"Managed store installations can only be replaced with a verified store update");goto done;}}
 if(!Before&&!Create&&!Save&&!Import&&!Install){MdoExtOpFailure(Error,"Extension directory is not installed");goto done;}
 if(CancelRemove){Ok=MdoExtensionsCancelRemove(Kind,Id,Error);if(Ok)*Result=MdoExtOpItem(Kind,Id,Error);goto done;}
 if(Enable){bool Enabled=MdoExtOpBool(Input,"enabled",true);Ok=MdoExtOpRestore(Kind,Id,Enabled,Error);
  if(!Ok){char Rollback[512];if(!MdoExtOpRestore(Kind,Id,WasEnabled,Rollback)&&Error)snprintf(Error,512,"Activation failed and its runtime needs recovery: %.400s",Rollback);}else *Result=MdoExtOpItem(Kind,Id,Error);goto done;}
 if((Remove||Edit||(Before&&(Save||Import||Install)))&&(!Expected||strlen(Expected)!=64)){MdoExtOpFailure(Error,"Inspect the current directory and supply its fingerprint before replacing, editing or uninstalling it");goto done;}
 if((Remove||Edit||(Before&&(Save||Import||Install)))&&Expected){char Actual[65];if(!MdoExtensionsFingerprint(Kind,Id,Actual,Cancel,Error)||strcmp(Actual,Expected)){MdoExtOpFailure(Error,"Extension directory changed; refresh it before applying this operation");goto done;}}
 if(Before&&!Create){
  if(!MdoExtOpRestore(Kind,Id,false,Error)){char Ignored[512];(void)MdoExtOpRestore(Kind,Id,WasEnabled,Ignored);goto done;}Suspended=true;
  if(MdoExtensionsInUse(Kind,Id)){
   if(Remove&&!strcmp(Kind,"plugins")){Ok=MdoExtensionsScheduleRemove(Kind,Id,Expected,Result,Error);if(Ok)Suspended=false;}
   else MdoExtOpFailure(Error,!strcmp(Kind,"plugins")?"Stop and restart this native plugin before updating or editing its directory":"This extension still has active calls; finish them before changing its directory");
   goto done;
  }
 }
 if(Create){cstr Text=MdoExtensionText(Input,"entry_text",1048576);Ok=MdoExtensionsCreate(Kind,Id,xrtValueObjectGet(Input,XRT_STR_LITERAL("manifest")),Text,Text?strlen(Text):0,Trust,Result,Error);}
 else if(Save)Ok=MdoExtensionsSave(Kind,Id,xrtValueObjectGet(Input,XRT_STR_LITERAL("manifest")),xrtValueObjectGet(Input,XRT_STR_LITERAL("edits")),Expected?Expected:"",Trust,Cancel,Result,Error);
 else if(Import)Ok=MdoExtensionsImportDirectoryEntry(Kind,Id,MdoExtensionText(Input,"directory",4096),xrtValueObjectGet(Input,XRT_STR_LITERAL("manifest")),MdoExtensionText(Input,"entry_text",1048576),Expected?Expected:"",Trust,Cancel,Result,Error);
 else if(Install){xvalue* Provenance=Credits?xrtValueClone(Credits):xrtValueObject();Ok=Provenance!=NULL;if(!Credits)Ok=Ok&&MdoExtOpPut(Provenance,"source","manual");
  Ok=Ok&&MdoExtOpPut(Provenance,"expected_revision",Expected?Expected:"")&&MdoExtensionsInstall(Kind,Id,MdoExtensionText(Input,"archive",4096),Provenance,Trust,Cancel,Result,Error);xrtValueRelease(Provenance);}
 else if(Remove)Ok=MdoExtensionsRemove(Kind,Id,Result,Error);
 else if(Edit){cstr Text=MdoExtensionText(Input,"text",1048576);if(!strcmp(Action,"file.write")&&!Text){MdoExtOpFailure(Error,"Text writes require a text value; import binary resources through the complete directory or binary upload endpoint");goto done;}
  Ok=MdoExtensionsEdit(Kind,Id,Action+5,MdoExtensionText(Input,"path",4096),MdoExtensionText(Input,"destination",4096),Text,Text?strlen(Text):0,Expected,Result,Error);}
 if(Ok){Suspended=false;
  if(Remove&&!strcmp(Kind,"external-tools")&&MdoAndroidToolsActive()&&!MdoAndroidToolClean(Id)&&*Result){xrtValueObjectSetNew(*Result,XRT_STR_LITERAL("cleanup_pending"),xrtValueBool(true));MdoExtOpPut(*Result,"cleanup_error","Cannot remove derived Android command links");}
  if(Remove&&!strcmp(Kind,"mcp")&&!MdoMcpRemoveDirectoryConnections(Id,RuntimeError)&&*Result){xrtValueObjectSetNew(*Result,XRT_STR_LITERAL("cleanup_pending"),xrtValueBool(true));MdoExtOpPut(*Result,"cleanup_error",RuntimeError);RuntimeError[0]=0;}
  bool Wanted=!Remove&&!Create&&!(Save&&!Before)&&MdoExtOpBool(Input,"enabled",Before?WasEnabled:strcmp(Kind,"plugins")!=0);
  RuntimeError[0]=0;bool Live=Remove?MdoExtOpReload(Kind,RuntimeError):MdoExtOpRestore(Kind,Id,Wanted,RuntimeError);
  if(!Live){if(!Remove){char Why[512]={0};if(!MdoExtensionsRememberDesired(Kind,Id,Wanted,Why)&&*Result)MdoExtOpPut(*Result,"preference_error",Why);}if(*Result)MdoExtOpPut(*Result,"runtime_error",RuntimeError);}
  if(*Result){xrtValueObjectSetNew(*Result,XRT_STR_LITERAL("desired_enabled"),xrtValueBool(Wanted));xrtValueObjectSetNew(*Result,XRT_STR_LITERAL("enabled"),xrtValueBool(Live&&Wanted));xrtValueObjectSetNew(*Result,XRT_STR_LITERAL("runtime_ready"),xrtValueBool(Live));}
 }
done:
 if(Ok&&*Result&&!strcmp(Kind,"plugins")){
  bool Restart=false,Pending=MdoApplicationPluginsPending(&Restart);
  xrtValueObjectSetNew(*Result,XRT_STR_LITERAL("pending"),xrtValueBool(Pending));
  xrtValueObjectSetNew(*Result,XRT_STR_LITERAL("restart_required"),xrtValueBool(Restart));
  if(Pending)xrtValueObjectSetNew(*Result,XRT_STR_LITERAL("runtime_ready"),xrtValueBool(false));
 }
 if(Suspended){char Rollback[512]={0};if(!MdoExtOpRestore(Kind,Id,WasEnabled,Rollback)&&Error)snprintf(Error,512,"The operation did not complete; runtime recovery is required: %.380s",Rollback);}
 xrtValueRelease(Before);MdoExtensionsMutationEnd();return Ok&&*Result;
}
bool MdoExtensionsWarnings(xvalue* Items){
 if(!Items)return false;
 xvalue* Recovery=MdoExtensionsRecoveryWarnings();if(!Recovery)return false;
 for(size_t I=0;I<xrtValueCount(Recovery);I++){const xvalue* W=xrtValueArrayGet(Recovery,I);cstr Path=MdoExtensionText(W,"path",4096),Message=MdoExtensionText(W,"message",512);
  if(!MdoDiagnosticWarning(Items,"extensions","recovery_pending",Path?Path:"extensions",Path?Path:"data/extension-tasks",Message?Message:"Extension recovery needs attention","Preserved records were not deleted. Repair the indicated record or restore its backup, then restart mdo","extensions")){xrtValueRelease(Recovery);return false;}}
 xrtValueRelease(Recovery);
 cstr AndroidWarning=MdoAndroidToolsWarning();if(AndroidWarning&&!MdoDiagnosticWarning(Items,"extensions","android_tools_unavailable","external-tools","external-tools",AndroidWarning,"Install the full APK again or free storage, then restart","external-tools"))return false;
 for(size_t K=0;K<9;K++){cstr Kind=MdoExtensionKinds[K];char Error[512]={0};xvalue* Rows=MdoExtensionsList(Kind,Error);
  if(!Rows){if(!MdoDiagnosticWarning(Items,"extensions","catalog_unavailable",Kind,Kind,Error,"Check the extension directory and host installation state","extensions"))return false;continue;}
  for(size_t I=0;I<xrtValueCount(Rows);I++){
   if(xrtValueCount(Items)>=255){xrtValueRelease(Rows);return MdoDiagnosticWarning(Items,"extensions","warning_limit","","","Additional extension warnings were omitted","Resolve the listed issues and check again","extensions");}
   const xvalue* Row=xrtValueArrayGet(Rows,I);cstr Id=MdoExtensionText(Row,"directory_id",96),Root=MdoExtensionText(Row,"root",4096),Problem=MdoExtensionText(Row,"error",512),PendingError=MdoExtensionText(Row,"pending_error",512);
   bool Pending=MdoExtOpBool(Row,"pending_remove",false),Valid=MdoExtOpBool(Row,"valid",false),Builtin=MdoExtensionsBuiltinUpdatePending(Kind,Id,Row);
   if(Pending||!Valid||Builtin){char Subject[140];snprintf(Subject,sizeof(Subject),"%s/%s",Kind,Id?Id:"");bool Ok=MdoDiagnosticWarning(Items,"extensions",PendingError?"uninstall_changed":Pending?"uninstall_pending":Builtin?"builtin_update_preserved":"invalid_directory",Subject,Root?Root:Kind,
     PendingError?PendingError:Pending?"Uninstall is waiting for the active native plugin to retire or the application to restart":Builtin?"An updated builtin is available, but the installed files were modified or could not be verified; the original directory was preserved":Problem?Problem:"The extension directory is invalid",
     PendingError?"Inspect the changed files, then cancel or request uninstall again":Pending?"Restart the application to finish removing this complete directory":Builtin?"Preserve your changes, then restore or migrate the builtin in its extension settings":"Repair its local files or reinstall the store archive","extensions");if(!Ok){xrtValueRelease(Rows);return false;}}
  }xrtValueRelease(Rows);
 }return true;
}
