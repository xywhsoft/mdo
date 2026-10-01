#ifndef MDO_SESSIONS_INTERNAL_H
#define MDO_SESSIONS_INTERNAL_H

#include "../../include/mdo/sessions.h"
#include "../../include/mdo/project_lifecycle.h"

typedef struct MdoSessionEventBridge MdoSessionEventBridge;
typedef struct MdoSessionEventTrimPlan MdoSessionEventTrimPlan;

/* Publish a committed project bucket removal to catalog observers. The
 * exclusive owner proves no session/Agent object needs to be evicted. */
bool MdoSessionsProjectPurged(const char* ProjectId, const MdoProjectLease* Owner);

MdoSessionEventBridge* MdoSessionEventBridgeCreate(
    const char* ProjectId, const char* SessionId,
    MdoProjectLease* ProjectLease,
    xwork_event_fn UserEvent, void* UserEventData,
    void* UserOwnerData, xwork_agent_owner_retain_fn UserOwnerRetain,
    xwork_agent_owner_release_fn UserOwnerRelease, xwork_error* Error);
bool MdoSessionEventBridgeRef(void* Value);
void MdoSessionEventBridgeRelease(void* Value);
bool MdoSessionEventBridgeOnEvent(void* Value, const xwork_event* Event);
bool MdoSessionEventBridgePendingSet(MdoSessionEventBridge* Bridge,
    uint64 RunId, const char Ids[4][33], size_t Count,
    bool EmptyPrompt, const char* QueueItemId);
void MdoSessionEventBridgePendingClear(MdoSessionEventBridge* Bridge,
    uint64 RunId);
MdoSessionEventTrimPlan* MdoSessionEventTrimPrepare(
    MdoSessionEventBridge* Bridge, uint64 ThroughSequence, bool Clear,
    uint64 SourceEventId, bool* MessageChanged, xwork_error* Error);
bool MdoSessionEventTrimApply(MdoSessionEventTrimPlan* Plan,
    xwork_error* Error);
bool MdoSessionEventTrimReconcileTodo(MdoSessionEventTrimPlan* Plan,
    xwork_error* Error);
void MdoSessionEventTrimPlanRelease(MdoSessionEventTrimPlan* Plan);
void MdoSessionEventBridgeSetRegistered(MdoSessionEventBridge* Bridge);
bool MdoSessionEventBridgeSetProfile(MdoSessionEventBridge* Bridge,
    const char* ModelId, uint64 ContextWindowTokens);
bool MdoSessionEventBridgeClonePrefix(MdoSessionEventBridge* Bridge,
    const char* SourceProjectId, const char* SourceSessionId,
    uint64 ThroughSequence, xwork_error* Error);

/* Implemented by manager.c. Called only when the final Agent callback owner
 * releases its bridge. */
void MdoSessionsInternalActiveRelease(const char* ProjectId,
    const char* SessionId);

/* Schema bridge used by the offline legacy migration staging pipeline. */
char* MdoSessionsInternalMetaJson(const MdoSessionInfo* Info, size_t* Size);
bool MdoSessionsInternalMetaParse(const char* ExpectedProject,
    const char* ExpectedId, xstrview Json, MdoSessionInfo* Info);

#endif
