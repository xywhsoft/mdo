#ifndef MDO_PUBLIC_CAPABILITIES_H
#define MDO_PUBLIC_CAPABILITIES_H
#include "sdk.h"
/* Preflight is read-only. A satisfied result does not grant permissions or
 * promise a live resource; inspect the normal service outcome when calling. */
static inline mdo_result mdoCapabilities(const mdo_sdk_context_v1* C,xvalue** R,mdo_error_v1* E)
{return mdoCall(C,"sdk.capabilities",NULL,R,E);}
static inline mdo_result mdoRequire(const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E)
{return mdoCall(C,"sdk.require",P,R,E);}
#endif
