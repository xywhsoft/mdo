#ifndef MDO_MODEL_POLICY_H
#define MDO_MODEL_POLICY_H
#include <xsbase.h>
#include <stdint.h>
#include <string.h>

/* Shared by configuration, draft validation and the runtime catalog. Operates
 * only on caller-owned values; provider credentials never enter this policy. */
static inline const xvalue* MdoModelPolicyGet(const xvalue* V,cstr K) {
    return xrtValueObjectGet(V,xrtStrView(K));
}
static inline uint32 MdoModelPolicyNumber(const xvalue* V) {
    uint64 U=0;int64 I=0;
    if(xrtValueGetUInt(V,&U)&&U&&U<=UINT32_MAX)return (uint32)U;
    if(xrtValueGetInt(V,&I)&&I>0&&(uint64)I<=UINT32_MAX)return (uint32)I;
    return 0;
}
static inline bool MdoModelPolicyText(const xvalue* V,cstr S) {
    xstrview T={0};return xrtValueGetString(V,&T)&&T.Size==strlen(S)&&!memcmp(T.Data,S,T.Size);
}
static inline bool MdoModelPolicyContains(const xvalue* V,cstr S) {
    for(size_t I=0;I<xrtValueCount(V);I++)if(MdoModelPolicyText(xrtValueArrayGet(V,I),S))return true;
    return false;
}
static inline bool MdoModelPolicyAppend(xvalue* V,cstr S) {
    if(MdoModelPolicyContains(V,S))return true;
    xvalue* T=xrtValueString(xrtStrView(S));bool Ok=T&&xrtValueArrayAppend(V,T);xrtValueRelease(T);return Ok;
}
static inline bool MdoModelPolicyNormalize(xvalue* Item) {
    xvalue* C=(xvalue*)MdoModelPolicyGet(Item,"capabilities");
    xvalue* A=(xvalue*)MdoModelPolicyGet(Item,"attachments");
    if(xrtValueType(C)!=XVALUE_ARRAY||xrtValueType(A)!=XVALUE_ARRAY)return true; /* validator owns malformed input */
    if(MdoModelPolicyContains(C,"media-input")||MdoModelPolicyContains(A,"image"))
        return MdoModelPolicyAppend(C,"media-input")&&MdoModelPolicyAppend(A,"image");
    return true;
}
static inline bool MdoModelPoliciesNormalize(xvalue* Models) {
    const xvalue* Items=MdoModelPolicyGet(Models,"items");
    for(size_t I=0;I<xrtValueCount(Items);I++)if(!MdoModelPolicyNormalize((xvalue*)xrtValueArrayGet(Items,I)))return false;
    return true;
}
static inline uint32 MdoModelPolicyLimit(const xvalue* Model) {
    if(MdoModelPolicyGet(Model,"output_budget"))return MdoModelPolicyNumber(MdoModelPolicyGet(MdoModelPolicyGet(Model,"window"),"max_output_tokens"));
    const xvalue* Fields=MdoModelPolicyGet(MdoModelPolicyGet(Model,"parameters"),"fields");
    uint32 Limit=MdoModelPolicyNumber(MdoModelPolicyGet(MdoModelPolicyGet(Fields,"max_output_tokens"),"value"));
    return Limit?Limit:MdoModelPolicyNumber(MdoModelPolicyGet(MdoModelPolicyGet(Model,"window"),"max_output_tokens"));
}
static inline uint32 MdoModelPolicyBudget(const xvalue* Model,cstr Effort) {
    const xvalue* W=MdoModelPolicyGet(Model,"window"),*P=MdoModelPolicyGet(Model,"output_budget");
    uint32 Limit=MdoModelPolicyLimit(Model),Budget=0;
    if(!P)return MdoModelPolicyNumber(MdoModelPolicyGet(W,"max_output_tokens")); /* legacy: never guess user intent */
    if(MdoModelPolicyText(MdoModelPolicyGet(P,"mode"),"recommended")) {
        const xvalue* R=MdoModelPolicyGet(Model,"parameters"),*D=MdoModelPolicyGet(R,"request_defaults");
        if(!Effort||!*Effort||!strcmp(Effort,"default")) {
            const xvalue* Reason=MdoModelPolicyGet(MdoModelPolicyGet(MdoModelPolicyGet(R,"fields"),"reasoning"),"value");
            xstrview T={0};char Name[16]="default";
            if(xrtValueGetString(MdoModelPolicyGet(Reason,"default"),&T)&&T.Size<sizeof(Name)){memcpy(Name,T.Data,T.Size);Name[T.Size]=0;}
            Budget=MdoModelPolicyNumber(MdoModelPolicyGet(MdoModelPolicyGet(D,"output_tokens_by_effort"),Name));
        } else Budget=MdoModelPolicyNumber(MdoModelPolicyGet(MdoModelPolicyGet(D,"output_tokens_by_effort"),Effort));
        if(!Budget)Budget=MdoModelPolicyNumber(MdoModelPolicyGet(D,"output_tokens"));
        if(!Budget)Budget=Limit;
    } else Budget=MdoModelPolicyNumber(MdoModelPolicyGet(P,"tokens"));
    if(Budget>Limit)Budget=Limit;
    uint32 Context=MdoModelPolicyNumber(MdoModelPolicyGet(W,"context_tokens"));
    if(MdoModelPolicyText(MdoModelPolicyGet(W,"mode"),"shared-context")&&Context&&Budget>=Context)Budget=Context-1;
    return Budget;
}
#endif
