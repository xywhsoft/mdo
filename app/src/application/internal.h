#ifndef MDO_APPLICATION_INTERNAL_H
#define MDO_APPLICATION_INTERNAL_H
#include "../../include/mdo/application.h"
static inline xvalue* SdkString(cstr S){return xrtValueString(xrtStrView(S?S:""));}
static inline const xvalue* SdkGet(const xvalue* V,cstr K){return K?xrtValueObjectGet(V,xrtStrView(K)):NULL;}
static inline cstr SdkText(const xvalue* V){xstrview T={0};return xrtValueGetString(V,&T)&&!memchr(T.Data,0,T.Size)?T.Data:NULL;}
static inline cstr SdkField(const xvalue* V,cstr K){return SdkText(SdkGet(V,K));}
static inline bool SdkUInt(const xvalue* V,uint64* N){int64 I=0;return xrtValueGetUInt(V,N)||(xrtValueGetInt(V,&I)&&I>=0&&(*N=(uint64)I,true));}
static inline xvalue* SdkParse(cstr S){return S?xrtJsonParse(xrtStrView(S)):NULL;}
static inline char* SdkJson(const xvalue* V){return xrtJsonStringify(V,false,NULL);}
static inline bool SdkPut(xvalue* V,cstr K,cstr S){return xrtValueObjectSetNew(V,xrtStrView(K),SdkString(S));}
static inline bool SdkPutUInt(xvalue* V,cstr K,uint64 I){return xrtValueObjectSetNew(V,xrtStrView(K),xrtValueUInt(I));}
static inline bool SdkIdValid(cstr Id){
    if(!Id||!Id[0]||strlen(Id)>255)return false;
    for(size_t I=0;Id[I];I++)if(!((Id[I]>='a'&&Id[I]<='z')||(Id[I]>='A'&&Id[I]<='Z')||
        (Id[I]>='0'&&Id[I]<='9')||Id[I]=='.'||Id[I]=='_'||Id[I]=='-'))return false;
    return true;
}
#endif
