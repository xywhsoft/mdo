#ifndef MDO_PUBLIC_INSTANCE_H
#define MDO_PUBLIC_INSTANCE_H
#include "sdk.h"
/* create establishes identity, send durably queues a turn, step executes one
 * turn synchronously (operation.start can provide a one-shot async wrapper).
 * No model request is sent merely by creating, restoring or inspecting. */
static inline mdo_result mdoInstanceCreate(const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E)
{return mdoCall(C,"instance.create",P,R,E);}
static inline mdo_result mdoInstanceSend(const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E)
{return mdoCall(C,"instance.send",P,R,E);}
static inline mdo_result mdoInstanceStep(const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E)
{return mdoCall(C,"instance.step",P,R,E);}
static inline mdo_result mdoInstanceResult(const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E)
{return mdoCall(C,"instance.result",P,R,E);}
static inline mdo_result mdoInstanceWait(const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E)
{return mdoCall(C,"instance.wait",P,R,E);}
static inline mdo_result mdoInstanceResume(const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E)
{return mdoCall(C,"instance.resume",P,R,E);}
static inline mdo_result mdoInstanceAbandon(const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E)
{return mdoCall(C,"instance.abandon",P,R,E);}
#endif
