#ifndef MDO_SESSIONS_INTERNAL_H
#define MDO_SESSIONS_INTERNAL_H

#include "../../include/mdo/sessions.h"

typedef struct MdoSessionEventBridge MdoSessionEventBridge;

MdoSessionEventBridge* MdoSessionEventBridgeCreate(
    const char* ProjectId, const char* SessionId,
    xwork_event_fn UserEvent, void* UserEventData,
    void* UserOwnerData, xwork_agent_owner_retain_fn UserOwnerRetain,
    xwork_agent_owner_release_fn UserOwnerRelease, xwork_error* Error);
bool MdoSessionEventBridgeRef(void* Value);
void MdoSessionEventBridgeRelease(void* Value);
bool MdoSessionEventBridgeOnEvent(void* Value, const xwork_event* Event);
void MdoSessionEventBridgeSetRegistered(MdoSessionEventBridge* Bridge);

/* Implemented by manager.c. Called only when the final Agent callback owner
 * releases its bridge. */
void MdoSessionsInternalActiveRelease(const char* ProjectId,
    const char* SessionId);

#endif
