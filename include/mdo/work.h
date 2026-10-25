#ifndef MDO_PUBLIC_WORK_H
#define MDO_PUBLIC_WORK_H
#include "sdk.h"
/* Shared tasks are an optional extension building block, never an injected
 * todo tool. Claims are durable and explicit; they do not expire and retry
 * side effects automatically. Reuse worker/claim_id to retrieve a lost claim. */
static inline mdo_result mdoWorkCreate(const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E)
{return mdoCall(C,"work.create",P,R,E);}
static inline mdo_result mdoWorkList(const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E)
{return mdoCall(C,"work.list",P,R,E);}
static inline mdo_result mdoWorkClaim(const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E)
{return mdoCall(C,"work.claim",P,R,E);}
static inline mdo_result mdoWorkComplete(const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E)
{return mdoCall(C,"work.complete",P,R,E);}
static inline mdo_result mdoWorkRelease(const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E)
{return mdoCall(C,"work.release",P,R,E);}
static inline mdo_result mdoWorkGet(const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E)
{return mdoCall(C,"work.get",P,R,E);}
static inline mdo_result mdoWorkCancel(const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E)
{return mdoCall(C,"work.cancel",P,R,E);}
#endif
