static bool LmConfigValid(const xvalue* C){
 int64 Gpu=0;bool B=false;
 if(!LmFields(C,"context_tokens max_output_tokens output_tokens parallel gpu_layers threads reasoning_mode auto_start auto_start_on_use idle_seconds allow_uncertain_resources")||
 !LmNumber(C,"context_tokens",512,1048576)||!LmNumber(C,"max_output_tokens",1,1048575)||!LmNumber(C,"output_tokens",1,1048575)||!LmNumber(C,"parallel",1,64)||!LmNumber(C,"threads",0,256)||
 !MdoExtensionNumber(C,"gpu_layers",&Gpu)||Gpu< -1||Gpu>100000||!LmOneOf(LmStr(C,"reasoning_mode"),"default off on")||!LmNumber(C,"idle_seconds",0,604800))return false;
 if(LmUInt(C,"max_output_tokens")>=LmUInt(C,"context_tokens")||LmUInt(C,"output_tokens")>LmUInt(C,"max_output_tokens"))return false;
 if(LmGet(C,"allow_uncertain_resources")&&!xrtValueGetBool(LmGet(C,"allow_uncertain_resources"),&B))return false;
 return xrtValueGetBool(LmGet(C,"auto_start"),&B)&&xrtValueGetBool(LmGet(C,"auto_start_on_use"),&B);
}
static bool LmDeploymentValid(const xvalue* D){
 const xvalue* R=LmGet(D,"runtime");cstr Kind=MdoExtensionRefKind(LmStr(R,"ref"));
 return LmFields(D,"id name installation revision runtime config capabilities requirements profile_id fingerprint verification previous")&&LmXid(LmStr(D,"id"))&&LmText(D,"name",240)&&LmXid(LmStr(D,"installation"))&&LmUInt(D,"revision")&&LmHex(LmStr(D,"fingerprint"),64)&&
 LmFields(R,"ref version_id sha256 engine backend platform build min_os cpu_features min_driver")&&Kind&&!strcmp(Kind,"external-tools")&&LmXid(LmStr(R,"version_id"))&&LmHex(LmStr(R,"sha256"),64)&&LmOneOf(LmStr(R,"engine"),"llama.cpp strata")&&MdoExtensionPlatform(LmStr(R,"platform"))&&
 LmOneOf(LmStr(R,"backend"),"cpu cuda vulkan metal rocm sycl")&&LmConfigValid(LmGet(D,"config"))&&LmStrings(LmGet(D,"capabilities"),1,8,"text tools vision reasoning");
}
static xvalue* LmDeployment(cstr Id){if(!g_Lm.Lock||!LmXid(Id))return NULL;xrtMutexLock(g_Lm.Lock);const xvalue* V=LmGet(LmGet(g_Lm.State,"deployments"),Id);xvalue* D=V?xrtValueClone(V):NULL;xrtMutexUnlock(g_Lm.Lock);return D;}
static bool LmDeploymentFingerprint(xvalue* D,const xvalue* J){
 xvalue* Binding=xrtValueArray(),*Config=xrtValueClone(LmGet(D,"config"));
 if(Config){xrtValueObjectRemove(Config,XRT_STR_LITERAL("allow_uncertain_resources"));xrtValueObjectRemove(Config,XRT_STR_LITERAL("auto_start"));xrtValueObjectRemove(Config,XRT_STR_LITERAL("auto_start_on_use"));xrtValueObjectRemove(Config,XRT_STR_LITERAL("idle_seconds"));}
 bool Ok=Binding&&Config&&xrtValueArrayAppend(Binding,Config)&&xrtValueArrayAppend(Binding,LmGet(D,"runtime"))&&xrtValueArrayAppend(Binding,LmGet(J,"assets"))&&xrtValueArrayAppend(Binding,LmGet(D,"capabilities"));
 char Hash[65];char* Json=Ok?xrtJsonStringify(Binding,false,NULL):NULL;Ok=Json&&MdoExtensionHash(Json,Hash)&&LmPut(D,"fingerprint",Hash);xrtFree(Json);xrtValueRelease(Binding);xrtValueRelease(Config);return Ok;
}
static xvalue* LmDefaultConfig(const xvalue* Profile){
 xvalue* C=xrtValueObject();const cstr Fields[]={"context_tokens","max_output_tokens","output_tokens","parallel","gpu_layers"};
 for(size_t I=0;C&&I<5;I++){const xvalue* V=LmGet(Profile,Fields[I]);if(V)xrtValueObjectSet(C,xrtStrView(Fields[I]),V);}
 if(!Profile){xrtValueObjectSetNew(C,XRT_STR_LITERAL("context_tokens"),xrtValueUInt(4096));xrtValueObjectSetNew(C,XRT_STR_LITERAL("max_output_tokens"),xrtValueUInt(2048));xrtValueObjectSetNew(C,XRT_STR_LITERAL("output_tokens"),xrtValueUInt(1024));xrtValueObjectSetNew(C,XRT_STR_LITERAL("parallel"),xrtValueUInt(1));xrtValueObjectSetNew(C,XRT_STR_LITERAL("gpu_layers"),xrtValueInt(0));}
 xrtValueObjectSetNew(C,XRT_STR_LITERAL("threads"),xrtValueUInt(0));xrtValueObjectSetNew(C,XRT_STR_LITERAL("auto_start"),xrtValueBool(false));xrtValueObjectSetNew(C,XRT_STR_LITERAL("auto_start_on_use"),xrtValueBool(false));xrtValueObjectSetNew(C,XRT_STR_LITERAL("allow_uncertain_resources"),xrtValueBool(false));xrtValueObjectSetNew(C,XRT_STR_LITERAL("idle_seconds"),xrtValueUInt(0));LmPut(C,"reasoning_mode",Profile&&LmOneOf(LmStr(LmGet(Profile,"reasoning"),"kind"),"none toggle")?"off":"default");return C;
}
static bool LmDeploymentBusy(cstr Id);
static bool LmDeploymentClearViews(const xvalue* D,char E[512]);
static bool LmDeploymentCommand(const xvalue* Input,xvalue** Result,char E[512]){
 bool Create=!strcmp(LmStr(Input,"action"),"deployment_create"),Switch=!strcmp(LmStr(Input,"action"),"deployment_switch");cstr Id=LmStr(Input,"id");xvalue* D=Create?xrtValueObject():LmDeployment(Id);xvalue* J=NULL;char* NewId=Create?xrtXidMakeString():NULL;bool Ok=D!=NULL;
 if(Switch&&(!D||LmUInt(Input,"deployment_revision")!=LmUInt(D,"revision")||LmDeploymentBusy(Id)))Ok=LmFail(E,"Stop the deployment and reload its latest configuration before switching");
 uint64 Revision=Create?1:LmUInt(D,"revision");
 if(Create||Switch){if(Create)Id=NewId;J=LmJob(LmStr(Input,"installation"));const xvalue* Plan=LmGet(J,"plan"),*P=LmGet(Plan,"profile"),*Doc=LmGet(Plan,"document"),*Runtime=LmFind(LmGet(Doc,"runtimes"),LmStr(P,"runtime"));xvalue* R=Runtime?xrtValueClone(Runtime):NULL;
  if(!R){xvalue* Meta=MdoExtensionsResolve(LmStr(Input,"runtime_ref"),E);const xvalue* Receipt=LmGet(Meta,"installation");R=xrtValueObject();Ok=Ok&&Meta&&LmPut(R,"ref",LmStr(Input,"runtime_ref"))&&LmPut(R,"version_id",LmStr(Receipt,"version_id"))&&LmPut(R,"sha256",LmStr(Receipt,"sha256"))&&LmPut(R,"engine","llama.cpp")&&LmPut(R,"backend",LmStr(Input,"backend")[0]?LmStr(Input,"backend"):"cpu")&&LmPut(R,"platform",MdoExtensionRuntimePlatform())&&LmPut(R,"build",LmStr(Meta,"version"));xrtValueRelease(Meta);}
  if(R)xrtValueObjectRemove(R,XRT_STR_LITERAL("id"));
  xvalue* C=LmDefaultConfig(P),*Caps=P?xrtValueClone(LmGet(P,"capabilities")):xrtJsonParse(XRT_STR_LITERAL("[\"text\"]"));
  Ok=Ok&&Id&&J&&!strcmp(LmStr(J,"state"),"installed")&&LmPut(D,"id",Id)&&LmPut(D,"installation",LmStr(J,"id"))&&LmPut(D,"name",LmStr(Input,"name")[0]?LmStr(Input,"name"):LmStr(J,"name"))&&xrtValueObjectSetNew(D,XRT_STR_LITERAL("revision"),xrtValueUInt(Revision))&&xrtValueObjectSet(D,XRT_STR_LITERAL("runtime"),R)&&xrtValueObjectSet(D,XRT_STR_LITERAL("config"),C)&&xrtValueObjectSet(D,XRT_STR_LITERAL("capabilities"),Caps);
  if(Switch){xvalue* Old=LmDeployment(Id),*OldJob=Old?LmJob(LmStr(Old,"installation")):NULL;const xvalue* OldRecipe=LmGet(LmGet(OldJob,"plan"),"recipe"),*NewRecipe=LmGet(Plan,"recipe");Ok=Ok&&OldRecipe&&NewRecipe&&!strcmp(LmStr(OldRecipe,"id"),LmStr(NewRecipe,"id"));xrtValueRelease(Old);xrtValueRelease(OldJob);xrtValueObjectRemove(D,XRT_STR_LITERAL("verification"));}
  if(P){Ok=Ok&&xrtValueObjectSet(D,XRT_STR_LITERAL("requirements"),LmGet(P,"requirements"))&&LmPut(D,"profile_id",LmStr(P,"id"));}xrtValueRelease(R);xrtValueRelease(C);xrtValueRelease(Caps);
 }else{J=D?LmJob(LmStr(D,"installation")):NULL;if(!LmXid(Id)||!D||LmUInt(Input,"deployment_revision")!=LmUInt(D,"revision")||LmDeploymentBusy(Id))Ok=LmFail(E,"Stop the deployment and reload its latest configuration before editing it");}
 const xvalue* Config=LmGet(Input,"config"),*Caps=LmGet(Input,"capabilities");
 if(Ok&&Config)Ok=xrtValueObjectSet(D,XRT_STR_LITERAL("config"),Config);
 if(Ok&&Caps)Ok=xrtValueObjectSet(D,XRT_STR_LITERAL("capabilities"),Caps);
 if(Ok&&!Create){xvalue* Previous=LmDeployment(Id);if(Previous){Ok=LmDeploymentClearViews(Previous,E);xrtValueObjectRemove(Previous,XRT_STR_LITERAL("previous"));xrtValueObjectSet(D,XRT_STR_LITERAL("previous"),Previous);}xrtValueRelease(Previous);xrtValueObjectSetNew(D,XRT_STR_LITERAL("revision"),xrtValueUInt(LmUInt(D,"revision")+1));if(LmStr(Input,"name")[0])Ok=LmPut(D,"name",LmStr(Input,"name"));}
 if(Ok)Ok=J&&LmDeploymentFingerprint(D,J)&&LmDeploymentValid(D);
 if(Ok&&!Create){const xvalue* V=LmGet(D,"verification");if(V&&strcmp(LmStr(V,"fingerprint"),LmStr(D,"fingerprint")))xrtValueObjectRemove(D,XRT_STR_LITERAL("verification"));}
 if(Ok){xrtMutexLock(g_Lm.Lock);xvalue* Before=xrtValueClone(g_Lm.State),*Items=xrtValueObjectEdit(g_Lm.State,XRT_STR_LITERAL("deployments"));Ok=Before&&Items&&(!Create||xrtValueCount(Items)<LM_LIMIT)&&xrtValueObjectSet(Items,xrtStrView(Id),D);Ok=LmCommit(Before,Ok);xrtMutexUnlock(g_Lm.Lock);}
 if(Ok)*Result=xrtValueClone(D);else if(!E[0])LmFail(E,"The deployment needs an installed model, a pinned runtime and valid context/output settings");xrtValueRelease(D);xrtValueRelease(J);xrtFree(NewId);return Ok;
}
