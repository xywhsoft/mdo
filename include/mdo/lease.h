#ifndef MDO_PUBLIC_LEASE_H
#define MDO_PUBLIC_LEASE_H
#include "sdk.h"
/* Lease mutations require request_id. Use the returned id/fence/token as the
 * lease_guard of an atomic state write; validate alone cannot guard a later
 * external side effect. A process restart invalidates every old live lease. */
static inline mdo_result mdoLeaseAcquire(const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E)
{return mdoCall(C,"lease.acquire",P,R,E);}
static inline mdo_result mdoLeaseRenew(const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E)
{return mdoCall(C,"lease.renew",P,R,E);}
static inline mdo_result mdoLeaseRelease(const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E)
{return mdoCall(C,"lease.release",P,R,E);}
#endif
