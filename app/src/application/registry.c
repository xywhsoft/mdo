#include "../../include/mdo/application.h"
#include "internal.h"
#include "../../generated/application-routes.inc.h"
#include "../../generated/application-contracts.inc.h"
static const struct {cstr Id,Description;} SdkBuiltins[]={
    {"sdk.services","Discover every product and extension service"},
    {"sdk.describe","Describe a service and its call contract"},
    {"sdk.context","Read the immutable caller context"},
    {"sdk.native","Discover typed product functions for in-process native modules"},
    {"model.complete","Call a configured model with managed credentials, tools, parts, usage and optional stream"},
    {"tool.call","Execute one tool with the selected session's live permission scope and result rules"},
    {"session.compact","Compact the bound managed session and retain its original transcript"},
    {"events.read","Read the bounded event journal with a resumable cursor"},
    {"events.emit","Publish an extension event for journal subscribers"}
};
static bool SdkBuiltinService(cstr Method){
    if(!Method)return false;
    for(size_t i=0;i<sizeof(SdkBuiltins)/sizeof(*SdkBuiltins);i++)if(!strcmp(Method,SdkBuiltins[i].Id))return true;
    for(size_t i=0;i<sizeof(SdkRoutes)/sizeof(*SdkRoutes);i++)if(!strcmp(Method,SdkRoutes[i].Id))return true;
    for(size_t i=0;i<sizeof(SdkRuntimeMethods)/sizeof(*SdkRuntimeMethods);i++)if(!strcmp(Method,SdkRuntimeMethods[i].Id))return true;
    const struct {const cstr* Names;size_t Count;} Groups[]={
        {SdkJobMethods,sizeof(SdkJobMethods)/sizeof(*SdkJobMethods)},
        {SdkStreamMethods,sizeof(SdkStreamMethods)/sizeof(*SdkStreamMethods)},
        {SdkPluginMethods,sizeof(SdkPluginMethods)/sizeof(*SdkPluginMethods)},
        {SdkUiMethods,sizeof(SdkUiMethods)/sizeof(*SdkUiMethods)}};
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
    }return true;
}
static xvalue* SdkDescription(const MdoApplicationRoute* R){
    xvalue* V=xrtValueObject();if(!V)return NULL;
    SdkPut(V,"id",R->Id);SdkPut(V,"domain",R->Domain);SdkPut(V,"method",R->Method);SdkPut(V,"resource",R->Path);
    xrtValueObjectSetNew(V,XRT_STR_LITERAL("input_schema"),SdkParse("{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"object\"},\"query\":{\"type\":\"object\"},\"body\":{},\"revision\":{\"type\":\"string\"}}}"));
    xrtValueObjectSetNew(V,XRT_STR_LITERAL("output_schema"),SdkParse("{\"type\":\"object\",\"properties\":{\"status\":{\"type\":\"integer\"},\"data\":{},\"revision\":{\"type\":\"string\"}}}"));
    SdkPut(V,"validation","The product service validates its body and revision; SDK calls retain the same lifecycle and mutation contracts.");
    SdkPut(V,"retry","Reads may be retried; mutations must use the product operation identity or inspect their receipt after an uncertain result.");
    xrtValueObjectSetNew(V,XRT_STR_LITERAL("mutation"),xrtValueBool(strcmp(R->Method,"GET")!=0));
    return V;
}
xvalue* MdoApplicationCatalog(void){
    xvalue* A=xrtValueArray();if(!A)return NULL;
    for(size_t i=0;i<sizeof(SdkBuiltins)/sizeof(*SdkBuiltins);i++){
        xvalue* V=xrtValueObject();SdkPut(V,"id",SdkBuiltins[i].Id);SdkPut(V,"description",SdkBuiltins[i].Description);
        SdkPut(V,"domain","sdk");SdkPut(V,"transport","native");xrtValueArrayAppendNew(A,V);
    }
    for(size_t i=0;i<sizeof(SdkRoutes)/sizeof(*SdkRoutes);i++){
        if(!xrtValueArrayAppendNew(A,SdkDescription(&SdkRoutes[i]))){xrtValueRelease(A);return NULL;}
    }
    xvalue* Runtime=MdoApplicationRuntimeCatalog();
    for(size_t i=0;i<xrtValueCount(Runtime);i++)xrtValueArrayAppend(A,xrtValueArrayGet(Runtime,i));
    xrtValueRelease(Runtime);
    xvalue* Jobs=MdoApplicationJobsCatalog();
    for(size_t i=0;i<xrtValueCount(Jobs);i++)xrtValueArrayAppend(A,xrtValueArrayGet(Jobs,i));
    xrtValueRelease(Jobs);
    xvalue* Streams=MdoApplicationStreamsCatalog();
    for(size_t i=0;i<xrtValueCount(Streams);i++)xrtValueArrayAppend(A,xrtValueArrayGet(Streams,i));
    xrtValueRelease(Streams);
    xvalue* Plugins=MdoApplicationPluginsCatalog();
    for(size_t i=0;i<xrtValueCount(Plugins);i++)xrtValueArrayAppend(A,xrtValueArrayGet(Plugins,i));
    xrtValueRelease(Plugins);
    xvalue* Ui=MdoApplicationUiCatalog();
    for(size_t i=0;i<xrtValueCount(Ui);i++)xrtValueArrayAppend(A,xrtValueArrayGet(Ui,i));
    xrtValueRelease(Ui);
    xvalue* Custom=MdoApplicationCustomCatalog(),*Merged=xrtValueArray();
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
static mdo_result SdkInvoke(const mdo_sdk_context_v1* C,cstr Method,const xvalue* P,xvalue** R,mdo_error_v1* E){
    bool Found=false;bool CustomOk=MdoApplicationCustomCall(C,Method,P,R,E,&Found);
    if(Found)return CustomOk?MDO_RESULT_OK:MDO_RESULT_ERROR;
    if(!SdkValidateContract(Method,P,E))return MDO_RESULT_ERROR;
    if(!strcmp(Method,"sdk.native")){*R=SdkNativeCatalog();return *R?MDO_RESULT_OK:MDO_RESULT_ERROR;}
    if(!strcmp(Method,"model.complete"))return SdkModelCall(C,P,R,E)?MDO_RESULT_OK:MDO_RESULT_ERROR;
    if(!strcmp(Method,"tool.call"))return SdkToolCall(C,P,R,E)?MDO_RESULT_OK:MDO_RESULT_ERROR;
    if(!strcmp(Method,"session.compact"))return SdkCompactCall(C,R,E)?MDO_RESULT_OK:MDO_RESULT_ERROR;
    if(!strcmp(Method,"sdk.services")){*R=MdoApplicationCatalog();return *R?MDO_RESULT_OK:MDO_RESULT_ERROR;}
    if(!strcmp(Method,"sdk.context")){
        *R=xrtValueObject();SdkPut(*R,"extension",C->ExtensionId);SdkPut(*R,"invocation",C->InvocationId);
        SdkPut(*R,"device",C->DeviceId);SdkPut(*R,"project",C->ProjectId);SdkPut(*R,"session",C->SessionId);
        SdkPut(*R,"run",C->RunId);SdkPut(*R,"workspace",C->Workspace);SdkPutUInt(*R,"deadline",C->Deadline);return MDO_RESULT_OK;
    }
    if(!strcmp(Method,"events.read")){*R=MdoApplicationEvents(P,E);return *R?MDO_RESULT_OK:MDO_RESULT_ERROR;}
    if(!strcmp(Method,"events.emit")){cstr Topic=SdkField(P,"topic");if(!SdkIdValid(Topic)){MdoApplicationError(E,"sdk_arguments_invalid","A valid event topic is required","events",400,false);return MDO_RESULT_ERROR;}MdoApplicationEvent(Topic,C,SdkGet(P,"data"));*R=xrtValueObject();return MDO_RESULT_OK;}
    if(!strcmp(Method,"sdk.describe")){
        cstr Id=SdkField(P,"id");
        xvalue* Catalog=MdoApplicationCatalog();
        for(size_t i=0;Id&&i<xrtValueCount(Catalog);i++){
            const xvalue* Item=xrtValueArrayGet(Catalog,i);
            if(!strcmp(Id,SdkField(Item,"id"))){*R=xrtValueClone(Item);xrtValueRelease(Catalog);return *R?MDO_RESULT_OK:MDO_RESULT_ERROR;}
        }xrtValueRelease(Catalog);
        MdoApplicationError(E,"sdk_method_missing","Service is not registered","discovery",404,false);return MDO_RESULT_ERROR;
    }
    bool RuntimeOk=MdoApplicationRuntimeCall(C,Method,P,R,E,&Found);
    if(Found)return RuntimeOk?MDO_RESULT_OK:MDO_RESULT_ERROR;
    bool JobOk=MdoApplicationJobsCall(C,Method,P,R,E,&Found);
    if(Found)return JobOk?MDO_RESULT_OK:MDO_RESULT_ERROR;
    bool StreamOk=MdoApplicationStreamsCall(C,Method,P,R,E,&Found);
    if(Found)return StreamOk?MDO_RESULT_OK:MDO_RESULT_ERROR;
    bool PluginOk=MdoApplicationPluginsCall(C,Method,P,R,E,&Found);
    if(Found)return PluginOk?MDO_RESULT_OK:MDO_RESULT_ERROR;
    bool UiOk=MdoApplicationUiCall(C,Method,P,R,E,&Found);
    if(Found)return UiOk?MDO_RESULT_OK:MDO_RESULT_ERROR;
    for(size_t i=0;i<sizeof(SdkRoutes)/sizeof(*SdkRoutes);i++)if(!strcmp(Method,SdkRoutes[i].Id))
        return MdoApplicationRouteInvoke(C,&SdkRoutes[i],P,R,E)?MDO_RESULT_OK:MDO_RESULT_ERROR;
    MdoApplicationError(E,"sdk_method_missing","Service is not registered","dispatch",404,false);return MDO_RESULT_ERROR;
}
mdo_result MdoApplicationCall(const mdo_sdk_context_v1* C,cstr Method,const xvalue* P,xvalue** R,mdo_error_v1* E){
    mdo_error_v1 LocalError;
    if(!E)E=&LocalError;
    if(R)*R=NULL;
    if(E){memset(E,0,sizeof(*E));E->Size=sizeof(*E);}
    if(!C||C->Size<sizeof(*C)||C->Version!=MDO_SDK_VERSION||!R||!SdkIdValid(Method)){
        MdoApplicationError(E,"sdk_arguments_invalid","A current SDK context, method and result pointer are required","dispatch",400,false);return MDO_RESULT_ERROR;}
    if(SdkCancelled(C)){MdoApplicationError(E,"sdk_cancelled","Call was cancelled or its deadline expired","dispatch",499,false);return MDO_RESULT_CANCELLED;}
    MdoApplicationInvocation Local={0};mdo_sdk_context_v1 Bound=*C;
    if(!Bound.Opaque)Bound.Opaque=&Local;
    Bound.Method=Method;C=&Bound;
    MdoApplicationInvocation* I=C->Opaque;
    if(I&&I->Depth>=32){MdoApplicationError(E,"sdk_recursion_limit","Service recursion exceeded 32 nested calls","dispatch",409,false);return MDO_RESULT_LIMIT;}
    if(I)I->Depth++;
    xvalue* Input=P&&xrtValueType(P)!=XVALUE_NULL?xrtValueClone(P):xrtValueObject();
    bool Hooked=strncmp(Method,"sdk.",4)&&strcmp(Method,"events.read");
    xvalue* Facts=xrtValueObject();SdkPut(Facts,"method",Method);
    if(Hooked)MdoApplicationEvent("service.before",C,Facts);
    mdo_result Code=(!Hooked||MdoApplicationHooks(C,"service.before",&Input,E))?SdkInvoke(C,Method,Input,R,E):MDO_RESULT_ERROR;
    if(Hooked&&Code==MDO_RESULT_OK&&!MdoApplicationHooks(C,"service.after",R,E)){Code=MDO_RESULT_ERROR;if(E)E->MayHaveCompleted=true;}
    if(Code!=MDO_RESULT_OK&&E&&!E->Code[0])MdoApplicationError(E,"sdk_extension_failed","Extension returned a failure without details","extension",500,E->MayHaveCompleted);
    if(Hooked){SdkPutUInt(Facts,"status",E?E->Status:0);MdoApplicationEvent("service.after",C,Facts);}
    xrtValueRelease(Input);xrtValueRelease(Facts);
    if(I)I->Depth--;
    return Code;
}
