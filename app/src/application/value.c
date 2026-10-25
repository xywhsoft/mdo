#include "../../include/mdo/application.h"
#include "../../include/mdo/time.h"
#include <string.h>
#include "internal.h"
static bool SdkSet(xvalue* V,cstr K,const xvalue* X){return K&&xrtValueObjectSet(V,xrtStrView(K),X);}
static const xvalue* SdkAt(const xvalue* V,size_t I){return xrtValueArrayGet(V,I);}
static cstr SdkKey(const xvalue* V,size_t I){xstrview K={0};return xrtValueObjectAt(V,I,&K)&&!memchr(K.Data,0,K.Size)?K.Data:NULL;}
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
static bool SdkBuiltinService(cstr Method);
static const mdo_application_v1 SdkApi={sizeof(SdkApi),MDO_SDK_VERSION,&SdkValues,MdoApplicationCall,SdkCancelled,SdkClock,SdkBindNative,SdkBuiltinService};
const mdo_application_v1* MdoApplicationApi(void){return &SdkApi;}
void MdoApplicationContext(mdo_sdk_context_v1* C,MdoApplicationInvocation* I,cstr Extension,cstr Invocation,
    cstr Project,cstr Session,cstr Run,cstr Workspace,uint64 Deadline){
    memset(C,0,sizeof(*C));C->Size=sizeof(*C);C->Version=MDO_SDK_VERSION;C->Api=&SdkApi;C->Opaque=I;
    C->ExtensionId=Extension?Extension:"";C->InvocationId=Invocation?Invocation:"";C->DeviceId="local";
    C->ProjectId=Project?Project:"";C->SessionId=Session?Session:"";C->RunId=Run?Run:"";C->Workspace=Workspace?Workspace:"";C->Deadline=Deadline;
}

static bool SdkCallIdentity(char Id[33]){
    unsigned char Bytes[16];static const char Hex[]="0123456789abcdef";
    if(!xrtSecureRandom(Bytes,sizeof(Bytes)))return false;
    for(size_t i=0;i<16;i++){Id[i*2]=Hex[Bytes[i]>>4];Id[i*2+1]=Hex[Bytes[i]&15];}Id[32]=0;return true;
}
static mdo_result SdkErrorResult(const mdo_error_v1* Error){
    if(Error&&Error->MayHaveCompleted)return MDO_RESULT_ERROR;
    if(Error&&Error->Status==499)return MDO_RESULT_CANCELLED;
    if(Error&&Error->Status==504)return MDO_RESULT_TIMEOUT;
    if(Error&&(Error->Status==429||Error->Status==413))return MDO_RESULT_LIMIT;
    return MDO_RESULT_ERROR;
}
