#include "../../include/mdo/extension_settings.h"
#include "../../include/mdo/extension_settings_form.h"

/* A private, per-Agent/run cache. Never attach this object to ContextPlan,
 * request diagnostics or tool arguments. First use pins each extension. */
xvalue* MdoModuleAgentSettings(xwork_agent* Agent,uint64 AgentId,uint64 RunId,cstr Ref,char Error[512]){
 (void)RunId;
 if(!Ref||!Ref[0])return xrtValueObject();
 if(!g_MdoModules.Lock)return MdoExtensionSettingsSnapshot(Ref,true,Error);
 xvalue* Result=NULL;xrtMutexLock(g_MdoModules.Lock);
 for(MdoModuleAgentScope* S=g_MdoModules.Scopes;S;S=S->Next)if((Agent&&S->Agent==Agent)||(AgentId&&S->AgentId==AgentId)){
  
  const xvalue* Found=MdoEsGet(S->Settings,Ref);if(Found)Result=xrtValueClone(Found);break;
 }xrtMutexUnlock(g_MdoModules.Lock);if(Result)return Result;
 xvalue* Loaded=MdoExtensionSettingsSnapshot(Ref,true,Error);if(!Loaded)return NULL;
 xrtMutexLock(g_MdoModules.Lock);
 for(MdoModuleAgentScope* S=g_MdoModules.Scopes;S;S=S->Next)if((Agent&&S->Agent==Agent)||(AgentId&&S->AgentId==AgentId)){
  
  if(!S->Settings)S->Settings=xrtValueObject();const xvalue* Found=MdoEsGet(S->Settings,Ref);
  if(Found)Result=xrtValueClone(Found);else if(S->Settings&&xrtValueObjectSet(S->Settings,xrtStrView(Ref),Loaded))Result=xrtValueClone(Loaded);break;
 }xrtMutexUnlock(g_MdoModules.Lock);
 if(!Result)Result=xrtValueClone(Loaded);xrtValueRelease(Loaded);return Result;
}

void MdoModuleAgentSettingsBegin(xwork_agent* Agent){
 if(!g_MdoModules.Lock||!Agent)return;
 xrtMutexLock(g_MdoModules.Lock);
 for(MdoModuleAgentScope* S=g_MdoModules.Scopes;S;S=S->Next)if(S->Agent==Agent){xrtValueRelease(S->Settings);S->Settings=NULL;break;}
 xrtMutexUnlock(g_MdoModules.Lock);
}
