/* Capture each synchronous model call's actual event sink and identity.
 * Descendants can overlap; never guess a target from the selected UI session.
 * All pointers borrowed by a captured sink are pinned by its owning Agent run. */
struct MdoAgentModelProgress {
    uint64 Thread;
    xwork_event Identity;
    xwork_event_fn Emit;
    void* UserData;
    struct MdoAgentModelProgress* Next;
};

static void MdoAgentsProgressTrack(MdoAgentOwner* Owner, const xwork_event* Event,
    xwork_event_fn Emit, void* UserData)
{
    if (!Owner->RouteLock || (Event->eKind != XWORK_EVENT_MODEL_START &&
        Event->eKind != XWORK_EVENT_MODEL_DONE && Event->eKind != XWORK_EVENT_ERROR &&
        Event->eKind != XWORK_EVENT_AGENT_DONE)) return;
    uint64 Thread = xrtThreadCurrentId();
    xrtMutexLock(Owner->RouteLock);
    MdoAgentModelProgress** Link = &Owner->ModelProgress;
    while (*Link) {
        MdoAgentModelProgress* Item = *Link;
        if (Item->Identity.uAgentId == Event->uAgentId &&
            Item->Identity.uRunId == Event->uRunId &&
            (Event->eKind != XWORK_EVENT_MODEL_DONE ||
             Item->Identity.uAgentTurn == Event->uAgentTurn)) {
            *Link = Item->Next;
            xrtFree(Item);
        } else Link = &Item->Next;
    }
    if (Event->eKind == XWORK_EVENT_MODEL_START) {
        MdoAgentModelProgress* Item = xrtCalloc(1u, sizeof(*Item));
        if (Item) {
            Item->Thread = Thread;
            Item->Emit = Emit;
            Item->UserData = UserData;
            Item->Identity.uSchemaVersion = Event->uSchemaVersion;
            Item->Identity.uAgentTurn = Event->uAgentTurn;
            Item->Identity.uAgentDepth = Event->uAgentDepth;
            Item->Identity.uAgentId = Event->uAgentId;
            Item->Identity.uRunId = Event->uRunId;
            Item->Identity.uParentRunId = Event->uParentRunId;
            Item->Identity.uTaskId = Event->uTaskId;
            Item->Next = Owner->ModelProgress;
            Owner->ModelProgress = Item;
        }
    }
    xrtMutexUnlock(Owner->RouteLock);
}

static void MdoAgentsProgressSnapshot(MdoAgentOwner* Owner, MdoAgentModelProgress* Out)
{
    if (!Owner->RouteLock) return;
    uint64 Thread = xrtThreadCurrentId();
    xrtMutexLock(Owner->RouteLock);
    for (MdoAgentModelProgress* Item = Owner->ModelProgress; Item; Item = Item->Next)
        if (Item->Thread == Thread) { *Out = *Item; Out->Next = NULL; break; }
    xrtMutexUnlock(Owner->RouteLock);
}

static bool MdoAgentsRetryProgress(void* User, const xllm_error* Error,
    uint32 NextAttempt, uint32 DelayMs)
{
    MdoAgentModelProgress* Progress = User;
    if (!Progress->Emit) return true;
    xwork_event Event = Progress->Identity;
    Event.eKind = MDO_AGENT_EVENT_MODEL_RETRY;
    Event.iOccurredAtUs = MdoUtcUs();
    /* Retry events carry a stable product classification, never provider prose. */
    Event.sProviderCode = MdoModelErrorKind(Error);
    Event.uHttpStatus = Error->iHttpStatus > 0 ? (uint32)Error->iHttpStatus : 0u;
    Event.tDiagnostics.uAttemptCount = NextAttempt ? NextAttempt - 1u : 0u;
    Event.tDiagnostics.uRetryAfterMs = DelayMs;
    Event.sText = "Model request recovery is in progress";
    Event.iTextLength = strlen(Event.sText);
    return Progress->Emit(Progress->UserData, &Event);
}

static bool MdoAgentsProgressOwnerEvent(void* User, const xwork_event* Event)
{
    MdoAgentOwner* Owner = User;
    MdoAgentsProgressTrack(Owner, Event, MdoAgentsOwnerEvent, User);
    return MdoAgentsOwnerEvent(User, Event);
}

static bool MdoAgentsProgressRunEvent(void* User, const xwork_event* Event)
{
    MdoAgentRun* Run = User;
    MdoAgentsProgressTrack(Run->Session->Owner, Event, MdoAgentsRunEvent, User);
    return MdoAgentsRunEvent(User, Event);
}

static void MdoAgentsProgressUnit(MdoAgentOwner* Owner)
{
    while (Owner->ModelProgress) {
        MdoAgentModelProgress* Item = Owner->ModelProgress;
        Owner->ModelProgress = Item->Next;
        xrtFree(Item);
    }
}
