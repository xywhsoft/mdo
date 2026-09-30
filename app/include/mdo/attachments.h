#ifndef MDO_ATTACHMENTS_H
#define MDO_ATTACHMENTS_H

#include <xsbase.h>

/* New records use the durable UI event ID. Runtime run IDs restart at one
 * after a process restart and are not suitable as persistent identities.
 * Direct write/prune/rollback calls take a shared project lifecycle lease. */
bool MdoSessionAttachmentEventWrite(const char* ProjectId,
    const char* SessionId, uint64 EventId, uint64 AgentRunId,
    const char Ids[4][33], size_t Count);
bool MdoSessionAttachmentEventRead(const char* ProjectId,
    const char* SessionId, uint64 EventId, uint64 AgentRunId,
    char Ids[4][33], size_t* Count);
/* Compatibility for records written before event-ID binding. */
bool MdoSessionAttachmentRunRead(const char* ProjectId,
    const char* SessionId, uint64 AgentRunId,
    char Ids[4][33], size_t* Count);

typedef struct MdoSession MdoSession;
bool MdoSessionAttachmentPendingSet(MdoSession* Session, uint64 AgentRunId,
    const char Ids[4][33], size_t Count, const char* QueueItemId);
void MdoSessionAttachmentPendingClear(MdoSession* Session, uint64 AgentRunId);

/* Copy retained event images into a new fork before it is published. Clone
 * reserves both projects for the operation. The enclosing fork transaction
 * must retain its own leases through publication or compensating rollback. */
bool MdoSessionAttachmentEventClone(const char* SourceProjectId,
    const char* SourceSessionId, uint64 SourceEventId,
    const char* TargetProjectId, const char* TargetSessionId,
    uint64 TargetEventId, uint64 AgentRunId);
bool MdoSessionAttachmentRecordReferenced(const char* ProjectId,
    const char* SessionId, const char* Id, bool* Referenced);
bool MdoSessionAttachmentPruneRemoved(const char* ProjectId,
    const char* SessionId);
void MdoSessionAttachmentForkRollback(const char* ProjectId,
    const char* SessionId);

#endif
