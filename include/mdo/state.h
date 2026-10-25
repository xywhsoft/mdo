#ifndef MDO_PUBLIC_STATE_H
#define MDO_PUBLIC_STATE_H
#include "sdk.h"
/* Namespace defaults to ExtensionId and is scoped to the current project.
 * Every write requires expected_revision. A deleted key retains its revision.
 * Values <=16 KiB; transactions <=64 writes; namespace <=1024 retained keys. */
static inline mdo_result mdoStateGet(const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E)
{return mdoCall(C,"state.get",P,R,E);}
static inline mdo_result mdoStateCompareExchange(const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E)
{return mdoCall(C,"state.compare_exchange",P,R,E);}
static inline mdo_result mdoStateTransaction(const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E)
{return mdoCall(C,"state.transaction",P,R,E);}
#endif
