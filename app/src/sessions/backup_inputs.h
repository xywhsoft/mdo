#ifndef MDO_BACKUP_INPUTS_H
#define MDO_BACKUP_INPUTS_H

#include "sidecars/draft.h"
#include "sidecars/queue.h"

/* Same-ID intents use the live browser's text trim/profile/image semantics. */
bool MdoBackupInputPayloadEqual(const MdoQueueItem* Queue,
    const MdoDraftSubmission* Draft);

#endif
