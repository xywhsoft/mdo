#ifndef MDO_API_DECISION_CACHE_H
#define MDO_API_DECISION_CACHE_H

#include "internal.h"

typedef enum MdoApiDecisionStatus {
    MDO_API_DECISION_NEW,
    MDO_API_DECISION_REPLAY,
    MDO_API_DECISION_CONFLICT,
    MDO_API_DECISION_UNAVAILABLE
} MdoApiDecisionStatus;

typedef struct MdoApiDecisionAttempt { void* Entry; } MdoApiDecisionAttempt;

bool MdoApiDecisionsInit(void);
void MdoApiDecisionsUnit(void);
/* NEW holds the cache lock through the short, synchronous manager operation.
 * End is mandatory before replying. Replays have no manager side effects.
 * Keys include the admitted host/write generation, endpoint and exact value. */
MdoApiDecisionStatus MdoApiDecisionBegin(const MdoApiContext* Context,
    cstr Value, MdoApiDecisionAttempt* Attempt);
void MdoApiDecisionEnd(MdoApiDecisionAttempt* Attempt, bool Applied);
bool MdoApiDecisionReplyError(MdoApiContext* Context, MdoApiDecisionStatus Status);

#endif
