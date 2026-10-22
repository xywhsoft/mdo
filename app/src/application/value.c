#include "../../include/mdo/application.h"
#include "../../include/mdo/time.h"
#include <string.h>
#include "internal.h"
static bool SdkSet(xvalue* V,cstr K,const xvalue* X){return K&&xrtValueObjectSet(V,xrtStrView(K),X);}
static const xvalue* SdkAt(const xvalue* V,size_t I){return xrtValueArrayGet(V,I);}
static cstr SdkKey(const xvalue* V,size_t I){xstrview K={0};return xrtValueObjectAt(V,I,&K)?K.Data:NULL;}
static mdo_value_kind SdkKind(const xvalue* V){
    switch(xrtValueType(V)){
    case XVALUE_NULL:return MDO_VALUE_NULL;case XVALUE_BOOL:return MDO_VALUE_BOOL;
    case XVALUE_INT:return MDO_VALUE_INT;case XVALUE_UINT:return MDO_VALUE_UINT;
    case XVALUE_FLOAT:return MDO_VALUE_NUMBER;case XVALUE_STRING:return MDO_VALUE_STRING;
    case XVALUE_ARRAY:return MDO_VALUE_ARRAY;case XVALUE_OBJECT:return MDO_VALUE_OBJECT;
    default:return MDO_VALUE_OTHER;}
}
static bool SdkCancelled(const mdo_sdk_context_v1* C){
    MdoApplicationInvocation* I=C?C->Opaque:NULL;
    return !C||(I&&I->Cancel&&xrtCancelRequested(I->Cancel))||(C->Deadline&&MdoDeadlineExpired(C->Deadline));
}
static uint64 SdkClock(void){return MdoMonotonicUs();}
static const mdo_values_v1 SdkValues={sizeof(SdkValues),xrtValueNull,xrtValueBool,xrtValueInt,xrtValueUInt,xrtValueFloat,
    SdkString,xrtValueObject,xrtValueArray,xrtValueClone,SdkSet,xrtValueArrayAppend,SdkGet,SdkAt,SdkKey,xrtValueCount,SdkKind,
    SdkText,xrtValueGetBool,xrtValueGetInt,SdkUInt,xrtValueGetFloat,SdkParse,SdkJson,xrtValueRelease,xrtFree};
static const mdo_application_v1 SdkApi={sizeof(SdkApi),MDO_SDK_VERSION,&SdkValues,MdoApplicationCall,SdkCancelled,SdkClock,SdkBindNative};
const mdo_application_v1* MdoApplicationApi(void){return &SdkApi;}
void MdoApplicationContext(mdo_sdk_context_v1* C,MdoApplicationInvocation* I,cstr Extension,cstr Invocation,
    cstr Project,cstr Session,cstr Run,cstr Workspace,uint64 Deadline){
    memset(C,0,sizeof(*C));C->Size=sizeof(*C);C->Version=MDO_SDK_VERSION;C->Api=&SdkApi;C->Opaque=I;
    C->ExtensionId=Extension?Extension:"";C->InvocationId=Invocation?Invocation:"";C->DeviceId="local";
    C->ProjectId=Project?Project:"";C->SessionId=Session?Session:"";C->RunId=Run?Run:"";C->Workspace=Workspace?Workspace:"";C->Deadline=Deadline;
}
