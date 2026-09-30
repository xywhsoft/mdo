#ifndef MDO_API_PURGE_INTENT_H
#define MDO_API_PURGE_INTENT_H

#include "internal.h"
#include "../../include/mdo/home_purge.h"

/* The client reviews this binding. Names and paths never authorize execution. */
typedef struct MdoApiPurgeBinding {
    char RequestId[MDO_HOME_PURGE_REQUEST_CAPACITY];
    char ProjectId[65];
    uint64 Revision;
    int64 CreatedAt;
} MdoApiPurgeBinding;

bool MdoApiPurgeBindingValid(const MdoApiPurgeBinding* Binding);
/* False means validation has already replied; no mutation has occurred. */
bool MdoApiPurgeBindingRead(MdoApiContext* Context, MdoApiPurgeBinding* Binding);
bool MdoApiPurgeIntentInit(void);
void MdoApiPurgeIntentUnit(void);
bool MdoApiProjectPurgeIntentPrepareRoute(MdoApiContext* Context);
bool MdoApiProjectPurgeIntentRoute(MdoApiContext* Context);
/* Success holds the intent mutex through execution/cancellation and result
 * capture. Lock order is intent -> nonblocking project gate -> references ->
 * schedules -> Home. False replies and releases, including damaged records. */
bool MdoApiPurgeIntentActionBegin(MdoApiContext* Context, const MdoApiPurgeBinding* Binding);
void MdoApiPurgeIntentActionEnd(void);

#endif
