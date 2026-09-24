#ifndef MDO_ATTACHMENTS_H
#define MDO_ATTACHMENTS_H

#include <xsbase.h>

/* Per-run image references are published before the run starts, so the
 * agent_start event can always be replayed with its session-local images. */
bool MdoSessionAttachmentRunWrite(const char* ProjectId,
    const char* SessionId, uint64 AgentRunId,
    const char Ids[4][33], size_t Count);
bool MdoSessionAttachmentRunRead(const char* ProjectId,
    const char* SessionId, uint64 AgentRunId,
    char Ids[4][33], size_t* Count);
bool MdoSessionAttachmentRunRemove(const char* ProjectId,
    const char* SessionId, uint64 AgentRunId);

#endif
