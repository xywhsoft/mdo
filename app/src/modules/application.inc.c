/* Generation-owned providers and hooks. A catalog reference pins callback code;
 * external callbacks always run after the module manager lock was released. */
static mdo_result MdoModulesApplicationReject(MdoModuleRegistrarContext* R,char* Error,size_t Cap,cstr Message){
    MdoModulesCopyError(Error,Cap,Message);if(R){R->Failed=true;MdoModulesCopyError(R->Message,sizeof(R->Message),Message);}return MDO_RESULT_ERROR;
}
static mdo_result MdoModulesRegistrarAddService(void* U,const mdo_service_v1* D,char* Error,size_t Cap){
    MdoModuleRegistrarContext* R=U;MdoModuleGeneration* M=R?R->Generation:NULL;
    if(!M||!D||D->Size<sizeof(*D)||D->Version!=MDO_SDK_VERSION||!SdkIdValid(D->Id)||
        !strchr(D->Id,'.')||!D->Execute||M->ServiceCount>=64||(D->Flags&~MDO_SERVICE_REPLACE_BUILTIN))
        return MdoModulesApplicationReject(R,Error,Cap,"Invalid service descriptor, flags or limit");
    if(g_MdoModuleApplication&&g_MdoModuleApplication->Builtin&&g_MdoModuleApplication->Builtin(D->Id)&&!(D->Flags&MDO_SERVICE_REPLACE_BUILTIN))return MdoModulesApplicationReject(R,Error,Cap,"Replacing a built-in service requires MDO_SERVICE_REPLACE_BUILTIN");
    for(size_t i=0;i<M->ServiceCount;i++)if(!strcmp(D->Id,M->Services[i].Id))return MdoModulesApplicationReject(R,Error,Cap,"Duplicate service ID");
    if((D->Description&&strlen(D->Description)>16384)||(D->InputSchema&&!MdoModulesSchemaValid(D->InputSchema))||(D->OutputSchema&&!MdoModulesSchemaValid(D->OutputSchema)))return MdoModulesApplicationReject(R,Error,Cap,"Service schema or description exceeds its contract");
    mdo_service_v1 Copy=*D;
    Copy.Id=xrtStrDup(D->Id);Copy.Description=xrtStrDup(D->Description?D->Description:"");
    Copy.InputSchema=xrtStrDup(D->InputSchema?D->InputSchema:"{}");Copy.OutputSchema=xrtStrDup(D->OutputSchema?D->OutputSchema:"{}");
    xvalue* In=SdkParse(Copy.InputSchema),*Out=SdkParse(Copy.OutputSchema);
    bool Ok=Copy.Id&&Copy.Description&&In&&Out&&xrtValueType(In)==XVALUE_OBJECT&&xrtValueType(Out)==XVALUE_OBJECT;
    xrtValueRelease(In);xrtValueRelease(Out);
    mdo_service_v1* Next=Ok?xrtRealloc(M->Services,(M->ServiceCount+1)*sizeof(*Next)):NULL;
    if(!Next){xrtFree((void*)Copy.Id);xrtFree((void*)Copy.Description);xrtFree((void*)Copy.InputSchema);xrtFree((void*)Copy.OutputSchema);return MdoModulesApplicationReject(R,Error,Cap,"Service schema or allocation is invalid");}
    M->Services=Next;Next[M->ServiceCount++]=Copy;return MDO_RESULT_OK;
}
static mdo_result MdoModulesRegistrarAddHook(void* U,const mdo_hook_v1* D,char* Error,size_t Cap){
    MdoModuleRegistrarContext* R=U;MdoModuleGeneration* M=R?R->Generation:NULL;
    if(!M||!D||D->Size<sizeof(*D)||D->Version!=MDO_SDK_VERSION||!SdkIdValid(D->Id)||!SdkIdValid(D->Topic)||!D->Execute||M->HookCount>=64){
        return MdoModulesApplicationReject(R,Error,Cap,"Invalid hook descriptor or limit");}
    for(size_t i=0;i<M->HookCount;i++)if(!strcmp(D->Id,M->Hooks[i].Id))return MdoModulesApplicationReject(R,Error,Cap,"Duplicate hook ID");
    mdo_hook_v1 Copy=*D;Copy.Id=xrtStrDup(D->Id);Copy.Topic=xrtStrDup(D->Topic);
    mdo_hook_v1* Next=Copy.Id&&Copy.Topic?xrtRealloc(M->Hooks,(M->HookCount+1)*sizeof(*Next)):NULL;
    if(!Next){xrtFree((void*)Copy.Id);xrtFree((void*)Copy.Topic);return MdoModulesApplicationReject(R,Error,Cap,"Cannot allocate hook descriptor");}
    M->Hooks=Next;Next[M->HookCount++]=Copy;return MDO_RESULT_OK;
}
static mdo_result MdoModulesRegistrarAddLifecycle(void* U,const mdo_lifecycle_v1* D,char* Error,size_t Cap){
    MdoModuleRegistrarContext* R=U;MdoModuleGeneration* M=R?R->Generation:NULL;
    if(!M||!D||D->Size<sizeof(*D)||D->AbiVersion!=MDO_MODULE_ABI_VERSION||!SdkIdValid(D->Id)||!D->OnEvent||
        !D->Phases||(D->Phases&~MDO_AGENT_PHASES_ALL)||M->LifecycleCount>=64)
        return MdoModulesApplicationReject(R,Error,Cap,"Invalid lifecycle descriptor, phases or limit");
    if(D->AgentPattern){size_t N=strlen(D->AgentPattern);if(!N||N>128)return MdoModulesApplicationReject(R,Error,Cap,"Invalid lifecycle role pattern");
        for(size_t I=0;I<N;I++){char C=D->AgentPattern[I];if(!((C>='a'&&C<='z')||(C>='A'&&C<='Z')||(C>='0'&&C<='9')||C=='.'||C=='_'||C=='-'||(C=='*'&&I==N-1)))return MdoModulesApplicationReject(R,Error,Cap,"Invalid lifecycle role pattern");}}
    for(size_t I=0;I<M->LifecycleCount;I++)if(!strcmp(D->Id,M->Lifecycles[I].Id))return MdoModulesApplicationReject(R,Error,Cap,"Duplicate lifecycle hook ID");
    mdo_lifecycle_v1 Copy=*D;Copy.Id=xrtStrDup(D->Id);Copy.AgentPattern=D->AgentPattern?xrtStrDup(D->AgentPattern):NULL;
    mdo_lifecycle_v1* Next=Copy.Id&&(!D->AgentPattern||Copy.AgentPattern)?xrtRealloc(M->Lifecycles,(M->LifecycleCount+1)*sizeof(*Next)):NULL;
    if(!Next){xrtFree((void*)Copy.Id);xrtFree((void*)Copy.AgentPattern);return MdoModulesApplicationReject(R,Error,Cap,"Cannot allocate lifecycle descriptor");}
    M->Lifecycles=Next;Next[M->LifecycleCount++]=Copy;return MDO_RESULT_OK;
}
static void MdoModulesLifecycleWarning(const mdo_sdk_context_v1* C,cstr Id,unsigned Phase,cstr Message){
    if(!C||!C->Api||!C->Api->Call)return;
    xvalue* Warning=xrtValueObject();SdkPut(Warning,"extension",C->ExtensionId);SdkPut(Warning,"hook",Id);SdkPutUInt(Warning,"phase",Phase);SdkPut(Warning,"message",Message);
    xvalue* Params=xrtValueObject(),*Result=NULL;SdkPut(Params,"topic","extension.warning");xrtValueObjectSet(Params,XRT_STR_LITERAL("data"),Warning);
    mdo_error_v1 Error={0};(void)mdoCall(C,"events.emit",Params,&Result,&Error);xrtValueRelease(Warning);xrtValueRelease(Params);xrtValueRelease(Result);
}
typedef struct SdkLifecycleBinding {const mdo_lifecycle_v1* Hook;cstr Module;MdoModuleGeneration* Owner;} SdkLifecycleBinding;
static bool MdoModulesCallbackRecover(mdo_agent_context_v1* Frame,mdo_result Code,
    uint32 Attempt,xwork_error* Error,cstr Message)
{
    if(Code!=MDO_RESULT_RETRY_SAFE&&Code!=MDO_RESULT_RESOURCE)return false;
    xwork_agent_extension_context* Native=Frame?Frame->Native:NULL;
    if(!Native||!Native->pAgent)return false;
    xworkErrorInit(Error);if(Error){Error->eCode=Code==MDO_RESULT_RESOURCE?XWORK_ERROR_OUT_OF_MEMORY:XWORK_ERROR_CONTEXT;
        snprintf(Error->sMessage,sizeof(Error->sMessage),"%s",Message&&Message[0]?Message:"Extension requested safe recovery");}
    bool Quiet=Native->ePhase==XWORK_AGENT_EXTENSION_BEFORE_TOOL||Native->ePhase==XWORK_AGENT_EXTENSION_AFTER_TOOL;
    cstr Stage=Code==MDO_RESULT_RESOURCE?(Quiet?"callback_resource_quiet":"resource"):(Quiet?"callback_quiet":"callback");
    return xworkAgentWaitRecovery(Native->pAgent,Stage,Error,Attempt);
}
static bool MdoModulesCallbackStopped(mdo_result Code,const xwork_error* Error){
    return (Code==MDO_RESULT_RETRY_SAFE||Code==MDO_RESULT_RESOURCE)&&Error&&
        (Error->eCode==XWORK_ERROR_CANCELLED||Error->eCode==XWORK_ERROR_TIMEOUT);
}
static int SdkLifecycleCompare(const void* L,const void* R){
    const SdkLifecycleBinding *A=L,*B=R;if(A->Hook->Priority!=B->Hook->Priority)return A->Hook->Priority<B->Hook->Priority?-1:1;
    int N=strcmp(A->Module,B->Module);return N?N:strcmp(A->Hook->Id,B->Hook->Id);
}
bool MdoModuleCatalogLifecycle(const MdoModuleCatalog* Source,mdo_agent_phase Phase,mdo_agent_context_v1* Frame,bool Notification,xwork_error* Error){
    if(!Source||!Frame||Phase<1||Phase>=MDO_AGENT_PHASE_COUNT)return false;
    MdoModuleCatalog* Catalog=MdoModuleCatalogRef((MdoModuleCatalog*)Source);SdkLifecycleBinding* List=NULL;size_t Count=0;bool Ok=true;
    for(size_t I=0;Ok&&I<Catalog->ModuleCount;I++){
        MdoModuleGeneration* M=Catalog->Modules[I];
        for(size_t J=0;J<M->LifecycleCount;J++){
            const mdo_lifecycle_v1* H=&M->Lifecycles[J];if(!(H->Phases&MDO_AGENT_PHASE_BIT(Phase)))continue;
            cstr Pattern=H->AgentPattern;size_t N=Pattern?strlen(Pattern):0;
            if(N&&(Pattern[N-1]=='*'?strncmp(Frame->AgentId,Pattern,N-1)!=0:strcmp(Frame->AgentId,Pattern)!=0))continue;
            SdkLifecycleBinding* Next=xrtRealloc(List,(Count+1)*sizeof(*Next));if(!Next){Ok=false;if(Error){xworkErrorInit(Error);Error->eCode=XWORK_ERROR_OUT_OF_MEMORY;snprintf(Error->sMessage,sizeof(Error->sMessage),"Cannot collect lifecycle callbacks");}break;}List=Next;List[Count++]=(SdkLifecycleBinding){H,M->Id,M};
        }
    }
    if(Count>1)qsort(List,Count,sizeof(*List),SdkLifecycleCompare);
    const mdo_sdk_context_v1* Previous=Frame->Application;const void* PreviousSettings=Frame->ExtensionSettings;
    MdoApplicationInvocation Invocation={0};Invocation.Cancel=(xcancel*)Frame->CancelToken;Invocation.Catalog=Catalog;
    mdo_sdk_context_v1 Application={0};Application.Size=sizeof(Application);Application.Version=MDO_SDK_VERSION;Application.Api=g_MdoModuleApplication;
    Application.Opaque=&Invocation;Application.ExtensionId="lifecycle";Application.InvocationId=Frame->AgentId;Application.DeviceId="local";
    Application.ProjectId=Frame->ProjectId;Application.SessionId=Frame->SessionId;Application.Workspace=Frame->WorkspaceRoot;Application.Deadline=Frame->Deadline;
    Application.Namespace=Frame->Namespace;Application.MemberId=Frame->MemberId;
    Application.Origin=MDO_ORIGIN_AGENT;
    Frame->Application=&Application;
    for(size_t I=0;Ok&&I<Count;I++){
        const mdo_lifecycle_v1* H=List[I].Hook;Application.ExtensionId=List[I].Module;
        char Message[1024]={0};mdo_result Code;uint32 Attempt=0;
        MdoModuleGeneration* G=List[I].Owner;xwork_agent_extension_context* N=Frame->Native;
        cstr Ref=MdoExtensionText(G->Directory,"id",192);
        xvalue* Settings=MdoModuleAgentSettings(N?N->pAgent:NULL,0,0,Ref,Message);
        Application.ExtensionKind=G->Application.ExtensionKind;Application.DirectoryId=G->Application.DirectoryId;
        Application.ExtensionRoot=G->Application.ExtensionRoot;Application.ExtensionSettings=Settings;Frame->ExtensionSettings=Settings;
        do{Code=Settings?H->OnEvent(H->UserData,Phase,Frame,Message,sizeof(Message)):MDO_RESULT_ERROR;
            if(H->ObserveOnly||Notification||Phase==MDO_AGENT_INSTANCE_RELEASE)break;
            if(!MdoModulesCallbackRecover(Frame,Code,++Attempt,Error,Message))break;
        }while(true);
        xrtValueRelease(Settings);Application.ExtensionSettings=NULL;Frame->ExtensionSettings=NULL;
        if(Code==MDO_RESULT_OK){xworkErrorInit(Error);continue;}
        bool Observer=H->ObserveOnly||Notification||Phase==MDO_AGENT_INSTANCE_RELEASE;
        if(!Observer){Ok=false;if(Error&&!MdoModulesCallbackStopped(Code,Error)){xworkErrorInit(Error);Error->eCode=Code==MDO_RESULT_CANCELLED?XWORK_ERROR_CANCELLED:Code==MDO_RESULT_TIMEOUT?XWORK_ERROR_TIMEOUT:Code==MDO_RESULT_LIMIT?XWORK_ERROR_LIMIT:XWORK_ERROR_CONTEXT;snprintf(Error->sMessage,sizeof(Error->sMessage),"%s",Message[0]?Message:"Lifecycle control hook rejected execution");}}
        else MdoModulesLifecycleWarning(&Application,H->Id,Phase,Message[0]?Message:"Lifecycle observer failed");
    }
    Frame->Application=Previous;Frame->ExtensionSettings=PreviousSettings;xrtFree(List);MdoModuleCatalogRelease(Catalog);
    if(!Ok&&Error&&Error->eCode==XWORK_ERROR_NONE){xworkErrorInit(Error);Error->eCode=XWORK_ERROR_OUT_OF_MEMORY;snprintf(Error->sMessage,sizeof(Error->sMessage),"Cannot collect lifecycle callbacks");}
    return Ok;
}
xvalue* MdoApplicationCustomCatalogFor(const mdo_sdk_context_v1* C){
    MdoApplicationInvocation* Invocation=C?C->Opaque:NULL;
    xvalue* A=xrtValueArray();MdoModuleCatalog* Catalog=Invocation&&Invocation->Catalog?MdoModuleCatalogRef(Invocation->Catalog):MdoModuleCatalogSnapshot();
    for(size_t i=0;Catalog&&i<Catalog->ModuleCount;i++){
        MdoModuleGeneration* M=Catalog->Modules[i];
        for(size_t j=0;M&&j<M->ServiceCount;j++){
            const mdo_service_v1* S=&M->Services[j];xvalue* V=xrtValueObject();
            SdkPut(V,"id",S->Id);SdkPut(V,"domain",M->Id);SdkPut(V,"description",S->Description);
            xrtValueObjectSetNew(V,XRT_STR_LITERAL("input_schema"),SdkParse(S->InputSchema));
            xrtValueObjectSetNew(V,XRT_STR_LITERAL("output_schema"),SdkParse(S->OutputSchema));SdkPutUInt(V,"generation",M->Generation);
            SdkPutUInt(V,"effects",S->Effects);xrtValueObjectSetNew(V,XRT_STR_LITERAL("replaces_builtin"),xrtValueBool((S->Flags&MDO_SERVICE_REPLACE_BUILTIN)!=0));
            xrtValueArrayAppendNew(A,V);
        }
    }MdoModuleCatalogRelease(Catalog);return A;
}
xvalue* MdoApplicationCustomCatalog(void){return MdoApplicationCustomCatalogFor(NULL);}
static xvalue* MdoModulesApplicationOwner(mdo_sdk_context_v1* Out,const mdo_sdk_context_v1* Caller,MdoModuleGeneration* Owner){
    *Out=*Caller;Out->ExtensionId=Owner->Id;Out->ExtensionKind=Owner->Application.ExtensionKind;
    Out->DirectoryId=Owner->Application.DirectoryId;Out->ExtensionRoot=Owner->Application.ExtensionRoot;
    char Why[512]={0};cstr Ref=MdoExtensionText(Owner->Directory,"id",192);
    xvalue* Settings=Owner->ExtensionSettings?xrtValueClone(Owner->ExtensionSettings):Ref?MdoExtensionSettingsSnapshot(Ref,true,Why):xrtValueObject();
    Out->ExtensionSettings=Settings;return Settings;
}
bool MdoApplicationCustomCall(const mdo_sdk_context_v1* C,cstr Id,const xvalue* P,xvalue** R,mdo_error_v1* E,bool* Found){
    MdoApplicationInvocation* Invocation=C->Opaque;
    *Found=false;MdoModuleCatalog* Catalog=Invocation&&Invocation->Catalog?MdoModuleCatalogRef(Invocation->Catalog):MdoModuleCatalogSnapshot();mdo_service_v1* Service=NULL;MdoModuleGeneration* Owner=NULL;
    for(size_t i=0;Catalog&&i<Catalog->ModuleCount;i++){
        MdoModuleGeneration* M=Catalog->Modules[i];
        for(size_t j=0;M&&j<M->ServiceCount;j++)if(!strcmp(M->Services[j].Id,Id)){
            if(Service){*Found=true;MdoModuleCatalogRelease(Catalog);return MdoApplicationError(E,"sdk_service_conflict","Multiple providers registered this service","dispatch",409,false);}
            Service=&M->Services[j];Owner=M;
        }
    }
    bool Ok=true;if(Service){*Found=true;mdo_sdk_context_v1 Own;xvalue* Settings=MdoModulesApplicationOwner(&Own,C,Owner);
        Ok=Settings?Service->Execute(Service->UserData,&Own,P,R,E)==MDO_RESULT_OK:MdoApplicationError(E,"extension_settings_required","Complete this extension's configuration","extension",422,false);xrtValueRelease(Settings);}
    MdoModuleCatalogRelease(Catalog);return Ok;
}
typedef struct SdkHookBinding {const mdo_hook_v1* Hook;cstr Module;MdoModuleGeneration* Owner;} SdkHookBinding;
static int SdkHookCompare(const void* L,const void* R){
    const SdkHookBinding *A=L,*B=R;
    if(A->Hook->Priority!=B->Hook->Priority)return A->Hook->Priority<B->Hook->Priority?-1:1;
    int N=strcmp(A->Module,B->Module);return N?N:strcmp(A->Hook->Id,B->Hook->Id);
}
bool MdoApplicationHooks(const mdo_sdk_context_v1* C,cstr Topic,xvalue** Value,mdo_error_v1* Error){
    MdoApplicationInvocation* Invocation=C->Opaque;
    MdoModuleCatalog* Catalog=Invocation&&Invocation->Catalog?MdoModuleCatalogRef(Invocation->Catalog):MdoModuleCatalogSnapshot();SdkHookBinding* List=NULL;size_t Count=0;bool Ok=true;
    for(size_t i=0;Ok&&Catalog&&i<Catalog->ModuleCount;i++){
        MdoModuleGeneration* M=Catalog->Modules[i];
        for(size_t j=0;M&&j<M->HookCount;j++)if(!strcmp(M->Hooks[j].Topic,Topic)){
            SdkHookBinding* Next=xrtRealloc(List,(Count+1)*sizeof(*Next));if(!Next){Ok=false;break;}
            List=Next;List[Count++]=(SdkHookBinding){&M->Hooks[j],M->Id,M};
        }
    }
    if(Count>1)qsort(List,Count,sizeof(*List),SdkHookCompare);
    for(size_t i=0;Ok&&i<Count;i++){
        const mdo_hook_v1* H=List[i].Hook;xvalue* Replacement=NULL;mdo_error_v1 E={0};
        mdo_sdk_context_v1 Own;xvalue* Settings=MdoModulesApplicationOwner(&Own,C,List[i].Owner);
        mdo_result Result=Settings?H->Execute(H->UserData,&Own,Topic,*Value,&Replacement,&E):MDO_RESULT_ERROR;
        if(!Settings)MdoApplicationError(&E,"extension_settings_required","Complete this extension's configuration","extension",422,false);
        xrtValueRelease(Settings);
        if(H->Transform&&Result!=MDO_RESULT_OK){if(Error)*Error=E;Ok=false;}
        else if(H->Transform&&Replacement){xrtValueRelease(*Value);*Value=Replacement;Replacement=NULL;}
        else if(Result!=MDO_RESULT_OK)fprintf(stderr,"[mdo] observer %s:%s failed (%s)\n",List[i].Module,H->Id,E.Code);
        xrtValueRelease(Replacement);
    }
    xrtFree(List);MdoModuleCatalogRelease(Catalog);return Ok;
}
