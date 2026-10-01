typedef struct CaptureChildGate {
    xsem* Ready;
    xsem* Release;
} CaptureChildGate;

static xllm_result CaptureChildComplete(void* data, const xllm_request* request,
    const xllm_stream_callbacks* callbacks, xllm_response** response, xllm_error* error)
{
    CaptureChildGate* gate = (CaptureChildGate*)data;
    (void)request; (void)callbacks; (void)error;
    xrtSemPost(gate->Ready);
    if (xrtSemWaitFor(gate->Release, UINT64_C(5000000)) != XWAIT_OK)
        return XLLM_RESULT_ERROR;
    *response = Response("capture child complete");
    return *response != NULL ? XLLM_RESULT_OK : XLLM_RESULT_ERROR;
}

static bool QuiescentCaptureProbe(MdoAgentSession* session)
{
    xwork_error error;
    CheckpointReaderProbe reader = {0};
    xwork_scheduled_task_config scheduled;
    xwork_subagent_definition_config child;
    xllm_executor executor = {0};
    xllm_tool_call call = {0};
    xllm_executor_result result = {0};
    CaptureChildGate gate = {0};
    xwork_model_complete_fn old_complete = session->Owner->ExternalComplete;
    void* old_data = session->Owner->ExternalModelData;
    MdoAgentOwner* retained = NULL;
    uint64 task_id = 0u;
    uint64 child_task_id = 0u;
    uint64 completed = 0u;
    unsigned long long parsed_task_id = 0u;
    const char* task_text;
    bool claim = false, bound = false, ok = false;
    bool pending, running, terminal, child_blocked, retained_blocked, retry;
    const char* stage = "scheduled-create";
    reader.Session = session;
    xworkScheduledTaskConfigInit(&scheduled);
    scheduled.sOwnerSession = session->SnapshotPath;
    scheduled.sLabel = "bounded capture pending task";
    if (!xworkRuntimeCreateScheduledTask(session->Owner->Runtime, &scheduled, &task_id, &error)) goto done;
    pending = !MdoAgentSessionWithQuiescentCheckpoint(session, ReadCheckpoint, &reader, &error) &&
        error.eCode == XWORK_ERROR_CONTEXT && reader.Calls == 0u;
    if (!xworkRuntimeStartScheduledTask(session->Owner->Runtime, task_id, &error)) goto done;
    running = !MdoAgentSessionWithQuiescentCheckpoint(session, ReadCheckpoint, &reader, &error) &&
        error.eCode == XWORK_ERROR_CONTEXT && reader.Calls == 0u;
    if (!xworkRuntimeFinishScheduledTask(session->Owner->Runtime, task_id,
            XWORK_RESULT_OK, "finished", &error)) goto done;
    terminal = MdoAgentSessionWithQuiescentCheckpoint(session, ReadCheckpoint, &reader, &error);
    if (!xworkRuntimeReleaseTask(session->Owner->Runtime, task_id, &error)) goto done;
    task_id = 0u;
    stage = "child-publish";
    gate.Ready = xrtSemCreate(0u, 1u); gate.Release = xrtSemCreate(0u, 1u);
    if (!gate.Ready || !gate.Release) goto done;
    xworkSubagentDefinitionConfigInit(&child);
    child.sName = "capture.child"; child.sDescription = "Capture lifetime fixture";
    child.sSystemPrompt = "Return a bounded answer";
    child.bReadOnly = true; child.bAllowBackground = true;
    child.uAllowedEffects = XWORK_TOOL_EFFECT_READ;
    child.uMaxTurns = 1u; child.uMaxDepth = 1u;
    if (!xworkAgentReplaceSubagentDefinitions(session->Agent, &child, 1u, &error)) goto done;
    session->Owner->ExternalComplete = CaptureChildComplete;
    session->Owner->ExternalModelData = &gate;
    bound = xworkExecutorBind(&executor, session->Agent, &error);
    claim = bound && xworkAgentRunBegin(session->Agent, &error);
    if (!claim) goto done;
    stage = "child-submit";
    call.sId = "capture-child-call"; call.sName = "agent";
    call.sArgumentsJson = "{\"name\":\"capture.child\",\"prompt\":\"capture\",\"background\":true}";
    if (!executor.pExecute(executor.pUserData, &call, NULL, &result)) goto done;
    task_text = result.sContent != NULL ? strstr(result.sContent, "task_id: ") : NULL;
    if (!result.bSuccess || !task_text || sscanf(task_text, "task_id: %llu", &parsed_task_id) != 1) {
        printf("quiescent_child_submit=success:%d content:%s\n", result.bSuccess,
            result.sContent != NULL ? result.sContent : "none");
        goto done;
    }
    child_task_id = (uint64)parsed_task_id;
    xworkAgentRunEnd(session->Agent); claim = false;
    stage = "child-ready";
    if (xrtSemWaitFor(gate.Ready, UINT64_C(5000000)) != XWAIT_OK) goto done;
    reader.Calls = 0u;
    child_blocked = !MdoAgentSessionWithQuiescentCheckpoint(session, ReadCheckpoint, &reader, &error) &&
        error.eCode == XWORK_ERROR_CONTEXT && reader.Calls == 0u;
    xrtSemPost(gate.Release);
    stage = "child-finish";
    if (xworkRuntimeWaitTasks(session->Owner->Runtime, &child_task_id, 1u,
            XWORK_TASK_WAIT_ALL, xrtDeadlineAfter(UINT64_C(5000000)), NULL,
            &completed, &error) != XWORK_RESULT_OK) goto done;
    /* An already-terminal root task cannot justify capturing a retained
     * descendant owner. This also covers a nested queued-child handoff. */
    retained = MdoAgentOwnerRef(session->Owner);
    retained_blocked = retained &&
        !MdoAgentSessionWithQuiescentCheckpoint(session, ReadCheckpoint, &reader, &error) &&
        error.eCode == XWORK_ERROR_CONTEXT && reader.Calls == 0u;
    MdoAgentOwnerRelease(retained); retained = NULL;
    retry = MdoAgentSessionWithQuiescentCheckpoint(session, ReadCheckpoint, &reader, &error) &&
        reader.Calls == 1u && reader.Busy && reader.SnapshotReady;
    ok = pending && running && terminal && child_blocked && retained_blocked && retry;
    stage = "result";
    printf("quiescent_capture=pending:%d running:%d terminal:%d child:%d retained:%d retry:%d\n",
        pending, running, terminal, child_blocked, retained_blocked, retry);
done:
    if (claim) xworkAgentRunEnd(session->Agent);
    if (gate.Release) xrtSemPost(gate.Release);
    if (child_task_id) {
        (void)xworkRuntimeWaitTasks(session->Owner->Runtime, &child_task_id, 1u,
            XWORK_TASK_WAIT_ALL, xrtDeadlineAfter(UINT64_C(5000000)), NULL,
            &completed, &error);
        (void)xworkRuntimeReleaseTask(session->Owner->Runtime, child_task_id, &error);
    }
    if (task_id) {
        (void)xworkRuntimeCancelTask(session->Owner->Runtime, task_id, &error);
        (void)xworkRuntimeReleaseTask(session->Owner->Runtime, task_id, &error);
    }
    MdoAgentOwnerRelease(retained);
    if (bound) xworkExecutorUnbind(&executor);
    session->Owner->ExternalComplete = old_complete;
    session->Owner->ExternalModelData = old_data;
    xrtSemDestroy(gate.Ready); xrtSemDestroy(gate.Release);
    if (!ok) {
        printf("quiescent_capture_stage=%s\n", stage);
        PrintRuntimeError("quiescent_capture_error", &error);
    }
    return ok;
}
