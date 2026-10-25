#ifndef MDO_PUBLIC_MAILBOX_H
#define MDO_PUBLIC_MAILBOX_H
#include "sdk.h"
/* Reads do not consume messages. Stable message_id deduplicates send; ack can
 * atomically commit state writes/checks plus the acknowledgement. */
static inline mdo_result mdoMailboxSend(const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E)
{return mdoCall(C,"mailbox.send",P,R,E);}
static inline mdo_result mdoMailboxRead(const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E)
{return mdoCall(C,"mailbox.read",P,R,E);}
static inline mdo_result mdoMailboxAck(const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E)
{return mdoCall(C,"mailbox.ack",P,R,E);}
#endif
