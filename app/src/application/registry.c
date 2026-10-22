#include "../../include/mdo/application.h"
#include "internal.h"
#include "../../generated/application-routes.inc.h"
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
    for(size_t i=0;i<sizeof(SdkRoutes)/sizeof(*SdkRoutes);i++){
        if(!xrtValueArrayAppendNew(A,SdkDescription(&SdkRoutes[i]))){xrtValueRelease(A);return NULL;}
    }
    xvalue* Custom=MdoApplicationCustomCatalog();
    for(size_t i=0;i<xrtValueCount(Custom);i++)xrtValueArrayAppend(A,xrtValueArrayGet(Custom,i));
    xrtValueRelease(Custom);
    return A;
}
static mdo_result SdkInvoke(const mdo_sdk_context_v1* C,cstr Method,const xvalue* P,xvalue** R,mdo_error_v1* E){
    if(!strcmp(Method,"sdk.services")){*R=MdoApplicationCatalog();return *R?MDO_RESULT_OK:MDO_RESULT_ERROR;}
    if(!strcmp(Method,"sdk.context")){
        *R=xrtValueObject();SdkPut(*R,"extension",C->ExtensionId);SdkPut(*R,"invocation",C->InvocationId);
        SdkPut(*R,"device",C->DeviceId);SdkPut(*R,"project",C->ProjectId);SdkPut(*R,"session",C->SessionId);
        SdkPut(*R,"run",C->RunId);SdkPut(*R,"workspace",C->Workspace);SdkPutUInt(*R,"deadline",C->Deadline);return MDO_RESULT_OK;
    }
    if(!strcmp(Method,"events.read")){*R=MdoApplicationEvents(P,E);return *R?MDO_RESULT_OK:MDO_RESULT_ERROR;}
    if(!strcmp(Method,"sdk.describe")){
        cstr Id=SdkField(P,"id");
        for(size_t i=0;Id&&i<sizeof(SdkRoutes)/sizeof(*SdkRoutes);i++)if(!strcmp(Id,SdkRoutes[i].Id)){*R=SdkDescription(&SdkRoutes[i]);return *R?MDO_RESULT_OK:MDO_RESULT_ERROR;}
        MdoApplicationError(E,"sdk_method_missing","Service is not registered","discovery",404,false);return MDO_RESULT_ERROR;
    }
    bool Found=false;bool CustomOk=MdoApplicationCustomCall(C,Method,P,R,E,&Found);
    if(Found)return CustomOk?MDO_RESULT_OK:MDO_RESULT_ERROR;
    for(size_t i=0;i<sizeof(SdkRoutes)/sizeof(*SdkRoutes);i++)if(!strcmp(Method,SdkRoutes[i].Id))
        return MdoApplicationRouteInvoke(C,&SdkRoutes[i],P,R,E)?MDO_RESULT_OK:MDO_RESULT_ERROR;
    MdoApplicationError(E,"sdk_method_missing","Service is not registered","dispatch",404,false);return MDO_RESULT_ERROR;
}
mdo_result MdoApplicationCall(const mdo_sdk_context_v1* C,cstr Method,const xvalue* P,xvalue** R,mdo_error_v1* E){
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
    xvalue* Input=P?xrtValueClone(P):xrtValueObject();
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
