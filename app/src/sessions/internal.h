#ifndef MDO_SESSIONS_INTERNAL_H
#define MDO_SESSIONS_INTERNAL_H

#include "../../include/mdo/sessions.h"
#include "../../include/mdo/project_lifecycle.h"
#include "data_gate.h"
#include "../../include/mdo/home_restore.h"

typedef struct MdoSessionEventBridge MdoSessionEventBridge;
typedef struct MdoSessionEventTrimPlan MdoSessionEventTrimPlan;
typedef struct MdoSessionRestoreReservation MdoSessionRestoreReservation;

/* One synchronous owner per reservation. Acquire a current shared project
 * lease first. Reserves a fresh 32-lowercase-hex target and owns its exclusive
 * session-data lease: pending targets cannot be created, opened or loaded,
 * including conservative native aliases. No Home write/runtime/catalog bump.
 * At most eight pending identities; only Home's one journal may begin storage.
 * Registry calls run before manager locks. Drain active calls/transactions
 * before manager/Home Unit; inactive old reservations remain safely releasable.
 * This is coordination, not project/workspace/semantic publication permission. */
MdoSessionRestoreReservation* MdoSessionsRestoreReserve(cstr ProjectId,
    cstr SessionId, MdoProjectLease* Owner, xwork_error* Error);
/* Parent must initially be NULL; caller owns the returned anchor. The opaque
 * reservation owns its Home transaction, never accepting another target's tx. */
bool MdoSessionsRestoreStorageBegin(MdoSessionRestoreReservation* Reservation,
    xroot* Parent, xwork_error* Error);
/* Same reserved storage boundary with a durable request identity. The request
 * must name this exact reservation; replay detection belongs to Home. */
bool MdoSessionsRestoreStorageBeginRequested(MdoSessionRestoreReservation* Reservation,
    const MdoHomeSessionRestoreRequest* Request, xroot* Parent, xwork_error* Error);
/* Call only inside the reviewed MdoProjectWithBinding publication callback,
 * after StageCheck and StageRelease and after closing caller anchors. On an
 * actual commit advances catalog exactly once under the same manager lock as
 * Home's atomic no-replace publication, including a false cleanup result.
 * Committed/Generation are independent of bool success. Generation exhaustion
 * prevents publication. The reservation stays pending until Release. */
bool MdoSessionsRestorePublish(MdoSessionRestoreReservation* Reservation,
    cstr DirectoryName, const xfileinfo* Identity, bool* Committed,
    uint64* Generation, xwork_error* Error);
/* Closes any uncommitted storage transaction and consumes *Reservation, then
 * releases the data/project pins. Stage/caller anchors must already be closed.
 * Cleanup failure freezes Home through its durable journal. Inactive objects
 * from a closed manager may be released but never authorize new storage.
 * Invalid stale active transactions retain the handle for diagnosis. */
bool MdoSessionsRestoreRelease(MdoSessionRestoreReservation** Reservation,
    xwork_error* Error);

/* Pure parsing seams. Event strings are borrowed only during Visitor; neither
 * function reads Home or projects state. TodoParse returns an owned value. */
typedef bool (*MdoSessionEventVisitor)(const MdoSessionEventInfo* Event, void* Data);
bool MdoSessionsInternalEventVisit(const char* ProjectId, const char* SessionId,
    xstrview Json, MdoSessionEventVisitor Visitor, void* Data);
xvalue* MdoSessionsInternalTodoParse(xstrview Json, bool Stored);

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
bool MdoSessionEventBridgeCaptureTryLock(MdoSessionEventBridge* Bridge);
void MdoSessionEventBridgeCaptureUnlock(MdoSessionEventBridge* Bridge);
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
/* Filesystem-free schema seams for offline backup inspection. They reuse the
 * live readers, release all parse allocations and never project/write state. */
bool MdoSessionsInternalEventValid(const char* ProjectId, const char* SessionId, xstrview Json);
bool MdoSessionsInternalTodoValid(xstrview Json);

/* Committed JSONL record, borrowed only during callback. The observer must
 * enqueue/copy and must not call any manager. Register before serving reads;
 * unregister before disposing its state. Publication holds the manager lock. */
typedef void (*MdoSessionEventObserver)(cstr Project, cstr Session, xstrview Record,
    bool StateChanged, void* Data);
void MdoSessionsObserve(MdoSessionEventObserver Observer, void* Data);
void MdoSessionsInternalPublish(cstr Project, cstr Session, xstrview Record,
    bool StateChanged);

#endif
