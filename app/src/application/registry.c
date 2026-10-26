#include "../../include/mdo/application.h"
#include "internal.h"
#include "../../generated/application-routes.inc.h"
#include "../../generated/application-contracts.inc.h"
bool MdoApplicationProductService(cstr Method){
    for(size_t i=0;Method&&i<sizeof(SdkRoutes)/sizeof(*SdkRoutes);i++)if(!strcmp(Method,SdkRoutes[i].Id))return true;
    return false;
}
static const struct {cstr Id,Description;} SdkBuiltins[]={
    {"sdk.services","Discover every product and extension service"},
    {"sdk.describe","Describe a service and its call contract"},
    {"sdk.context","Read the immutable caller context"},
    {"sdk.native","Discover typed product functions for in-process native modules"},
    {"session.execution","Read the current main speaker, immutable model binding and native context checkpoint"},
    {"model.complete","Call a configured model with managed credentials, tools, parts, usage and optional stream"},
    {"tool.call","Execute one tool with the selected session's live permission scope and result rules"},
    {"session.compact","Compact the bound managed session and retain its original transcript"},
    {"events.read","Read the bounded event journal with a resumable cursor"},
    {"events.wait","Wait for a filtered event without losing a notification between reading and sleeping"},
    {"events.emit","Publish an extension event for journal subscribers"}
};
static bool SdkBuiltinService(cstr Method){
    if(!Method)return false;
    for(size_t i=0;i<sizeof(SdkBuiltins)/sizeof(*SdkBuiltins);i++)if(!strcmp(Method,SdkBuiltins[i].Id))return true;
    for(size_t i=0;i<sizeof(SdkRoutes)/sizeof(*SdkRoutes);i++)if(!strcmp(Method,SdkRoutes[i].Id))return true;
    for(size_t i=0;i<sizeof(SdkRuntimeMethods)/sizeof(*SdkRuntimeMethods);i++)if(!strcmp(Method,SdkRuntimeMethods[i].Id))return true;
    const struct {const cstr* Names;size_t Count;} Groups[]={
        {SdkJobMethods,sizeof(SdkJobMethods)/sizeof(*SdkJobMethods)},
        {SdkStateMethods,sizeof(SdkStateMethods)/sizeof(*SdkStateMethods)},
        {SdkStreamMethods,sizeof(SdkStreamMethods)/sizeof(*SdkStreamMethods)},
        {SdkTerminalMethods,sizeof(SdkTerminalMethods)/sizeof(*SdkTerminalMethods)},
        {SdkPluginMethods,sizeof(SdkPluginMethods)/sizeof(*SdkPluginMethods)},
        {SdkUiMethods,sizeof(SdkUiMethods)/sizeof(*SdkUiMethods)},
        {SdkWorkbenchMethods,sizeof(SdkWorkbenchMethods)/sizeof(*SdkWorkbenchMethods)}};
    for(size_t i=0;i<sizeof(Groups)/sizeof(*Groups);i++)for(size_t j=0;j<Groups[i].Count;j++)if(!strcmp(Method,Groups[i].Names[j]))return true;
    return false;
}
static bool SdkSchemaCheck(const xvalue* V,const xvalue* S,unsigned Depth,cstr Path,char Message[256]){
    if(Depth>32){snprintf(Message,256,"%s exceeds the schema nesting limit",Path);return false;}
    cstr Type=SdkField(S,"type");xvaluetype Kind=xrtValueType(V);bool Valid=true;
    if(Type){Valid=!strcmp(Type,"object")?Kind==XVALUE_OBJECT:!strcmp(Type,"array")?Kind==XVALUE_ARRAY:!strcmp(Type,"string")?SdkText(V)!=NULL:!strcmp(Type,"boolean")?Kind==XVALUE_BOOL:!strcmp(Type,"integer")?Kind==XVALUE_INT||Kind==XVALUE_UINT:!strcmp(Type,"number")?Kind==XVALUE_INT||Kind==XVALUE_UINT||Kind==XVALUE_FLOAT:!strcmp(Type,"null")?Kind==XVALUE_NULL:false;
        if(!Valid){snprintf(Message,256,"%s must be %s",Path,Type);return false;}}
    const xvalue* Enum=SdkGet(S,"enum");if(Enum){Valid=false;cstr Text=SdkText(V);for(size_t i=0;i<xrtValueCount(Enum);i++){cstr Choice=SdkText(xrtValueArrayGet(Enum,i));if(Text&&Choice&&!strcmp(Text,Choice))Valid=true;}if(!Valid){snprintf(Message,256,"%s is not an allowed value",Path);return false;}}
    if(Kind==XVALUE_INT||Kind==XVALUE_UINT){uint64 U=0;int64 I=0;double Number=xrtValueGetInt(V,&I)?(double)I:SdkUInt(V,&U)?(double)U:0;
        const cstr Keys[]={"minimum","maximum"};for(size_t i=0;i<2;i++)if(SdkGet(S,Keys[i])){uint64 Bound=0;if(SdkUInt(SdkGet(S,Keys[i]),&Bound)&&((i==0&&Number<(double)Bound)||(i==1&&Number>(double)Bound))){snprintf(Message,256,"%s is outside the allowed range",Path);return false;}}}
    if(Kind==XVALUE_OBJECT){
        const xvalue* Required=SdkGet(S,"required"),*Properties=SdkGet(S,"properties"),*Additional=SdkGet(S,"additionalProperties");
        for(size_t i=0;i<xrtValueCount(Required);i++){cstr Name=SdkText(xrtValueArrayGet(Required,i));if(Name&&!SdkGet(V,Name)){snprintf(Message,256,"%s.%s is required",Path,Name);return false;}}
        for(size_t i=0;i<xrtValueCount(V);i++){cstr Key=SdkKey(V,i);if(!Key){snprintf(Message,256,"%s contains a property name with an embedded NUL",Path);return false;}const xvalue* Rule=SdkGet(Properties,Key);if(!Rule&&xrtValueType(Additional)==XVALUE_OBJECT)Rule=Additional;
            if(Rule){char Child[256];snprintf(Child,sizeof(Child),"%s.%s",Path,Key);if(!SdkSchemaCheck(SdkGet(V,Key),Rule,Depth+1,Child,Message))return false;}}
    }else if(Kind==XVALUE_ARRAY){
        uint64 Min=0,Max=UINT64_MAX;(void)SdkUInt(SdkGet(S,"minItems"),&Min);(void)SdkUInt(SdkGet(S,"maxItems"),&Max);
        if(xrtValueCount(V)<Min||xrtValueCount(V)>Max){snprintf(Message,256,"%s has an invalid item count",Path);return false;}
        const xvalue* Items=SdkGet(S,"items");for(size_t i=0;Items&&i<xrtValueCount(V);i++){char Child[256];snprintf(Child,sizeof(Child),"%s[%zu]",Path,i);if(!SdkSchemaCheck(xrtValueArrayGet(V,i),Items,Depth+1,Child,Message))return false;}
    }return true;
}
static bool SdkValidateContract(cstr Method,const xvalue* Params,mdo_error_v1* E){
    for(size_t i=0;i<sizeof(SdkContracts)/sizeof(*SdkContracts);i++)if(!strcmp(Method,SdkContracts[i].Id)){
        xvalue* Schema=SdkParse(SdkContracts[i].Input);char Message[256]="Cannot load the service parameter contract";bool Ok=Schema&&SdkSchemaCheck(Params,Schema,0,"params",Message);xrtValueRelease(Schema);
        return Ok||MdoApplicationError(E,"sdk_arguments_invalid",Message,"validation",400,false);
    }
    return true;
}
static xvalue* SdkDescription(const MdoApplicationRoute* R,bool Detailed){
    xvalue* V=xrtValueObject();if(!V)return NULL;
    SdkPut(V,"id",R->Id);SdkPut(V,"domain",R->Domain);SdkPut(V,"method",R->Method);SdkPut(V,"resource",R->Path);
    if(Detailed){
        xrtValueObjectSetNew(V,XRT_STR_LITERAL("input_schema"),SdkParse(R->InputSchema));
        xrtValueObjectSetNew(V,XRT_STR_LITERAL("output_schema"),SdkParse(R->OutputSchema));
    }
    xrtValueObjectSetNew(V,XRT_STR_LITERAL("schema_available"),xrtValueBool(true));
    SdkPut(V,"validation","The product service validates its body and revision; SDK calls retain the same lifecycle and mutation contracts.");
    SdkPut(V,"retry","Reads may be retried; mutations must use the product operation identity or inspect their receipt after an uncertain result.");
    xrtValueObjectSetNew(V,XRT_STR_LITERAL("mutation"),xrtValueBool(strcmp(R->Method,"GET")!=0));
    return V;
}
xvalue* MdoApplicationCatalogFor(const mdo_sdk_context_v1* C){
    xvalue* A=xrtValueArray();if(!A)return NULL;
    for(size_t i=0;i<sizeof(SdkBuiltins)/sizeof(*SdkBuiltins);i++){
        xvalue* V=xrtValueObject();SdkPut(V,"id",SdkBuiltins[i].Id);SdkPut(V,"description",SdkBuiltins[i].Description);
        SdkPut(V,"domain","sdk");SdkPut(V,"transport","native");xrtValueArrayAppendNew(A,V);
    }
    for(size_t i=0;i<sizeof(SdkRoutes)/sizeof(*SdkRoutes);i++){
        if(!xrtValueArrayAppendNew(A,SdkDescription(&SdkRoutes[i],false))){xrtValueRelease(A);return NULL;}
    }
    xvalue* Runtime=MdoApplicationRuntimeCatalog();
    for(size_t i=0;i<xrtValueCount(Runtime);i++)xrtValueArrayAppend(A,xrtValueArrayGet(Runtime,i));
    xrtValueRelease(Runtime);
    xvalue* Jobs=MdoApplicationJobsCatalog();
    for(size_t i=0;i<xrtValueCount(Jobs);i++)xrtValueArrayAppend(A,xrtValueArrayGet(Jobs,i));
    xrtValueRelease(Jobs);
    xvalue* State=MdoApplicationStateCatalog();
    for(size_t i=0;i<xrtValueCount(State);i++)xrtValueArrayAppend(A,xrtValueArrayGet(State,i));
    xrtValueRelease(State);
    xvalue* Streams=MdoApplicationStreamsCatalog();
    for(size_t i=0;i<xrtValueCount(Streams);i++)xrtValueArrayAppend(A,xrtValueArrayGet(Streams,i));
    xrtValueRelease(Streams);
    xvalue* Terminals=MdoApplicationTerminalsCatalog();
    for(size_t i=0;i<xrtValueCount(Terminals);i++)xrtValueArrayAppend(A,xrtValueArrayGet(Terminals,i));
    xrtValueRelease(Terminals);
    xvalue* Plugins=MdoApplicationPluginsCatalog();
    for(size_t i=0;i<xrtValueCount(Plugins);i++)xrtValueArrayAppend(A,xrtValueArrayGet(Plugins,i));
    xrtValueRelease(Plugins);
    xvalue* Ui=MdoApplicationUiCatalog();
    for(size_t i=0;i<xrtValueCount(Ui);i++)xrtValueArrayAppend(A,xrtValueArrayGet(Ui,i));
    xrtValueRelease(Ui);
    xvalue* WorkbenchCatalog=MdoApplicationWorkbenchCatalog();
    for(size_t i=0;i<xrtValueCount(WorkbenchCatalog);i++)xrtValueArrayAppend(A,xrtValueArrayGet(WorkbenchCatalog,i));
    xrtValueRelease(WorkbenchCatalog);
    xvalue* Custom=MdoApplicationCustomCatalogFor(C),*Merged=xrtValueArray();
    for(size_t i=0;i<xrtValueCount(A);i++){const xvalue* Item=xrtValueArrayGet(A,i);bool Replaced=false;
        for(size_t k=0;k<sizeof(SdkContracts)/sizeof(*SdkContracts);k++)if(!strcmp(SdkField(Item,"id"),SdkContracts[k].Id)){
            xvalue* Description=(xvalue*)Item;SdkPut(Description,"description",SdkContracts[k].Description);
            xrtValueObjectSetNew(Description,XRT_STR_LITERAL("input_schema"),SdkParse(SdkContracts[k].Input));
            xrtValueObjectSetNew(Description,XRT_STR_LITERAL("output_schema"),SdkParse(SdkContracts[k].Output));break;
        }
        for(size_t j=0;j<xrtValueCount(Custom);j++)if(!strcmp(SdkField(Item,"id"),SdkField(xrtValueArrayGet(Custom,j),"id")))Replaced=true;
        if(!Replaced)xrtValueArrayAppend(Merged,Item);}
    for(size_t i=0;i<xrtValueCount(Custom);i++)xrtValueArrayAppend(Merged,xrtValueArrayGet(Custom,i));
    xrtValueRelease(Custom);xrtValueRelease(A);return Merged;
}
xvalue* MdoApplicationCatalog(void){return MdoApplicationCatalogFor(NULL);}
static mdo_result SdkInvoke(const mdo_sdk_context_v1* C,cstr Method,const xvalue* P,xvalue** R,mdo_error_v1* E){
    bool Found=false;bool CustomOk=MdoApplicationCustomCall(C,Method,P,R,E,&Found);
    if(Found)return CustomOk?MDO_RESULT_OK:SdkErrorResult(E);
    if(!SdkValidateContract(Method,P,E))return MDO_RESULT_ERROR;
    if(!strcmp(Method,"sdk.native")){*R=SdkNativeCatalog();return *R?MDO_RESULT_OK:MDO_RESULT_ERROR;}
    if(!strcmp(Method,"model.complete"))return SdkModelCall(C,P,R,E)?MDO_RESULT_OK:MDO_RESULT_ERROR;
    if(!strcmp(Method,"tool.call"))return SdkToolCall(C,P,R,E)?MDO_RESULT_OK:MDO_RESULT_ERROR;
    if(!strcmp(Method,"session.compact"))return SdkCompactCall(C,R,E)?MDO_RESULT_OK:MDO_RESULT_ERROR;
    if(!strcmp(Method,"session.execution"))return MdoApplicationSessionExecution(C,NULL,R,E)?MDO_RESULT_OK:MDO_RESULT_ERROR;
    if(!strcmp(Method,"sdk.services")){*R=MdoApplicationCatalogFor(C);return *R?MDO_RESULT_OK:MDO_RESULT_ERROR;}
    if(!strcmp(Method,"sdk.context")){
        *R=xrtValueObject();SdkPut(*R,"extension",C->ExtensionId);SdkPut(*R,"invocation",C->InvocationId);
        SdkPut(*R,"device",C->DeviceId);SdkPut(*R,"project",C->ProjectId);SdkPut(*R,"session",C->SessionId);
        SdkPut(*R,"run",C->RunId);SdkPut(*R,"workspace",C->Workspace);SdkPutUInt(*R,"deadline",C->Deadline);
        SdkPut(*R,"namespace",C->Namespace);SdkPut(*R,"member",C->MemberId);
        SdkPut(*R,"origin",mdoCallOrigin(C)==MDO_ORIGIN_USER?"user":mdoCallOrigin(C)==MDO_ORIGIN_AGENT?"agent":mdoCallOrigin(C)==MDO_ORIGIN_EXTENSION?"extension":"unknown");
        SdkPut(*R,"deadline_unit","monotonic_microseconds");return MDO_RESULT_OK;
    }
    if(!strcmp(Method,"events.read")){*R=MdoApplicationEvents(P,E);return *R?MDO_RESULT_OK:MDO_RESULT_ERROR;}
    if(!strcmp(Method,"events.wait")){*R=MdoApplicationEventsWait(C,P,E);return *R?MDO_RESULT_OK:SdkErrorResult(E);}
    if(!strcmp(Method,"events.emit")){cstr Topic=SdkField(P,"topic");if(!SdkIdValid(Topic)){MdoApplicationError(E,"sdk_arguments_invalid","A valid event topic is required","events",400,false);return MDO_RESULT_ERROR;}char* Payload=SdkGet(P,"data")?SdkJson(SdkGet(P,"data")):NULL;size_t Bytes=Payload?strlen(Payload):0;xrtFree(Payload);
        if(Bytes>8192){MdoApplicationError(E,"sdk_event_limit","Event payload exceeds 8 KiB; store the resource and emit its identity","events",413,false);return MDO_RESULT_LIMIT;}
        MdoApplicationEvent(Topic,C,SdkGet(P,"data"));*R=xrtValueObject();return MDO_RESULT_OK;}
    if(!strcmp(Method,"sdk.describe")){
        cstr Id=SdkField(P,"id");
        xvalue* Catalog=MdoApplicationCatalogFor(C);
        for(size_t i=0;Id&&i<xrtValueCount(Catalog);i++){
            const xvalue* Item=xrtValueArrayGet(Catalog,i);
            if(!strcmp(Id,SdkField(Item,"id"))){
                *R=xrtValueClone(Item);
                if(!SdkGet(Item,"input_schema"))for(size_t k=0;k<sizeof(SdkRoutes)/sizeof(*SdkRoutes);k++)if(!strcmp(Id,SdkRoutes[k].Id)){xrtValueRelease(*R);*R=SdkDescription(&SdkRoutes[k],true);break;}
                xrtValueRelease(Catalog);return *R?MDO_RESULT_OK:MDO_RESULT_ERROR;
            }
        }xrtValueRelease(Catalog);
        MdoApplicationError(E,"sdk_method_missing","Service is not registered","discovery",404,false);return MDO_RESULT_ERROR;
    }
    bool RuntimeOk=MdoApplicationRuntimeCall(C,Method,P,R,E,&Found);
    if(Found)return RuntimeOk?MDO_RESULT_OK:MDO_RESULT_ERROR;
    bool JobOk=MdoApplicationJobsCall(C,Method,P,R,E,&Found);
    if(Found)return JobOk?MDO_RESULT_OK:MDO_RESULT_ERROR;
    bool StateOk=MdoApplicationStateCall(C,Method,P,R,E,&Found);
    if(Found)return StateOk?MDO_RESULT_OK:MDO_RESULT_ERROR;
    bool StreamOk=MdoApplicationStreamsCall(C,Method,P,R,E,&Found);
    if(Found)return StreamOk?MDO_RESULT_OK:MDO_RESULT_ERROR;
    bool TerminalOk=MdoApplicationTerminalsCall(C,Method,P,R,E,&Found);
    if(Found)return TerminalOk?MDO_RESULT_OK:MDO_RESULT_ERROR;
    bool PluginOk=MdoApplicationPluginsCall(C,Method,P,R,E,&Found);
    if(Found)return PluginOk?MDO_RESULT_OK:MDO_RESULT_ERROR;
    bool UiOk=MdoApplicationUiCall(C,Method,P,R,E,&Found);
    if(Found)return UiOk?MDO_RESULT_OK:MDO_RESULT_ERROR;
    bool WorkbenchOk=MdoApplicationWorkbenchCall(C,Method,P,R,E,&Found);
    if(Found)return WorkbenchOk?MDO_RESULT_OK:MDO_RESULT_ERROR;
    for(size_t i=0;i<sizeof(SdkRoutes)/sizeof(*SdkRoutes);i++)if(!strcmp(Method,SdkRoutes[i].Id))
        return MdoApplicationRouteInvoke(C,&SdkRoutes[i],P,R,E)?MDO_RESULT_OK:MDO_RESULT_ERROR;
    MdoApplicationError(E,"sdk_method_missing","Service is not registered","dispatch",404,false);return MDO_RESULT_ERROR;
}
mdo_result MdoApplicationCall(const mdo_sdk_context_v1* C,cstr Method,const xvalue* P,xvalue** R,mdo_error_v1* E){
    mdo_error_v1 LocalError;
    if(!E)E=&LocalError;
    if(R)*R=NULL;
    if(E){memset(E,0,sizeof(*E));E->Size=sizeof(*E);}
    if(!C||C->Size<offsetof(mdo_sdk_context_v1,Origin)||C->Version!=MDO_SDK_VERSION||!R||!SdkIdValid(Method)){
        MdoApplicationError(E,"sdk_arguments_invalid","A current SDK context, method and result pointer are required","dispatch",400,false);return MDO_RESULT_ERROR;}
    if(SdkCancelled(C)){
        MdoApplicationInvocation* I=C->Opaque;bool Cancelled=I&&I->Cancel&&xrtCancelRequested(I->Cancel);
        MdoApplicationError(E,Cancelled?"sdk_cancelled":"sdk_deadline_expired",Cancelled?"Call was cancelled":"Call deadline expired","dispatch",Cancelled?499:504,false);
        return Cancelled?MDO_RESULT_CANCELLED:MDO_RESULT_TIMEOUT;
    }
    MdoApplicationInvocation* Parent=C->Opaque;
    MdoApplicationInvocation Frame=Parent?*Parent:(MdoApplicationInvocation){0};
    mdo_sdk_context_v1 Bound={0};memcpy(&Bound,C,C->Size<sizeof(Bound)?C->Size:sizeof(Bound));
    Bound.Origin=mdoCallOrigin(C);Bound.Size=sizeof(Bound);Bound.Opaque=&Frame;Bound.Method=Method;C=&Bound;
    MdoApplicationInvocation* I=&Frame;
    if(I->Depth>=32){MdoApplicationError(E,"sdk_recursion_limit","Service recursion exceeded 32 nested calls","dispatch",409,false);return MDO_RESULT_LIMIT;}
    I->Depth++;

    xvalue* Input=P&&xrtValueType(P)!=XVALUE_NULL?xrtValueClone(P):xrtValueObject();
    bool Hooked=strncmp(Method,"sdk.",4)&&strcmp(Method,"events.read")&&strcmp(Method,"events.wait");
    xvalue* Facts=xrtValueObject();SdkPut(Facts,"method",Method);
    if(Hooked)MdoApplicationEvent("service.before",C,Facts);
    mdo_result Code=(!Hooked||MdoApplicationHooks(C,"service.before",&Input,E))?SdkInvoke(C,Method,Input,R,E):MDO_RESULT_ERROR;
    if(Hooked&&Code==MDO_RESULT_OK&&!MdoApplicationHooks(C,"service.after",R,E)){Code=MDO_RESULT_ERROR;if(E)E->MayHaveCompleted=true;}
    if(Code!=MDO_RESULT_OK&&E&&!E->Code[0])MdoApplicationError(E,"sdk_extension_failed","Extension returned a failure without details","extension",500,E->MayHaveCompleted);
    if(Hooked){SdkPutUInt(Facts,"status",E?E->Status:0);MdoApplicationEvent("service.after",C,Facts);}
    xrtValueRelease(Input);xrtValueRelease(Facts);
    if(Code!=MDO_RESULT_OK)Code=SdkErrorResult(E);
    return Code;
}
