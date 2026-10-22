#include "internal.h"
#include <libtcc.h>
#include "../../generated/application-native.inc.h"
static bool SdkBindNative(void* State){
    if(!State)return false;
    for(size_t i=0;i<sizeof(SdkNativeSymbols)/sizeof(*SdkNativeSymbols);i++)
        if(tcc_add_symbol(State,SdkNativeSymbols[i].Name,(const void*)SdkNativeSymbols[i].Function)<0)return false;
    return true;
}
static xvalue* SdkNativeCatalog(void){
    xvalue* A=xrtValueArray();for(size_t i=0;i<sizeof(SdkNativeSymbols)/sizeof(*SdkNativeSymbols);i++){
        xvalue* V=xrtValueObject();SdkPut(V,"name",SdkNativeSymbols[i].Name);SdkPut(V,"header",SdkNativeSymbols[i].Header);xrtValueArrayAppendNew(A,V);
    }return A;
}
