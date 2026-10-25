#ifndef MDO_PUBLIC_EXECUTION_H
#define MDO_PUBLIC_EXECUTION_H
#include "sdk.h"

/* A service result describes this call, not completion of an entire workflow.
 * An external effect may already exist whenever MayHaveCompleted is true. */
static inline const char* mdoOutcomeName(mdo_result Result,const mdo_error_v1* Error)
{
    if(Result==MDO_RESULT_OK)return "succeeded";
    if(Error&&Error->MayHaveCompleted)return "uncertain";
    if(Result==MDO_RESULT_CANCELLED)return "cancelled";
    if(Result==MDO_RESULT_TIMEOUT)return "timed_out";
    if(Result==MDO_RESULT_LIMIT)return "limited";
    return Error&&Error->MayHaveCompleted?"uncertain":"failed";
}
static inline mdo_result mdoExecutionContext(const mdo_sdk_context_v1* Context,
    xvalue** Result,mdo_error_v1* Error)
{return mdoCall(Context,"sdk.context",NULL,Result,Error);}
#endif
