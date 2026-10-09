/* Disposable loopback fixture. Model and event callbacks provide one bounded
 * interrupted read; real xwork, ledger, history maintenance and HTTP recovery
 * routes perform the work. No provider, account or remote service is called. */
static MdoSession* g_RecoveryHistorySession;
static bool g_RecoveryHistoryInterrupt = true;
static bool g_RecoveryHistoryIo = true;
static unsigned g_RecoveryHistoryReads;

static char* RecoveryHistoryCopy(const char* Text)
{
    size_t Length = strlen(Text) + 1u;
    char* Copy = (char*)malloc(Length);
    if (Copy) memcpy(Copy, Text, Length);
    return Copy;
}

static bool RecoveryHistoryEvent(void* User, const xwork_event* Event)
{
    (void)User;
    if (Event->eKind != XWORK_EVENT_TOOL_START) return true;
    if (!g_RecoveryHistoryInterrupt) { ++g_RecoveryHistoryReads; return true; }
    if (g_RecoveryHistoryIo) xrtSetErrorKind(XERR_IO);
    return false;
}

static xllm_result RecoveryHistoryModel(void* User, const xllm_request* Request,
    const xllm_stream_callbacks* Callbacks, xllm_response** Response, xllm_error* Error)
{
    xllm_response* Reply;
    bool Done = Request->iMessageCount &&
        Request->pMessages[Request->iMessageCount - 1u].eRole == XLLM_ROLE_TOOL;
    (void)User; (void)Callbacks; (void)Error;
    *Response = NULL;
    Reply = (xllm_response*)calloc(1u, sizeof(*Reply));
    if (!Reply) return XLLM_RESULT_ERROR;
    Reply->sContent = RecoveryHistoryCopy(Done ? "Recovered read complete" : "");
    Reply->sModel = RecoveryHistoryCopy("offline-recovery");
    Reply->sFinishReason = RecoveryHistoryCopy(Done ? "stop" : "tool_calls");
    Reply->eFinish = Done ? XLLM_FINISH_STOP : XLLM_FINISH_TOOL_CALLS;
    if (!Reply->sContent || !Reply->sModel || !Reply->sFinishReason) goto failed;
    if (!Done) {
        Reply->pToolCalls = (xllm_tool_call*)calloc(1u, sizeof(xllm_tool_call));
        if (!Reply->pToolCalls) goto failed;
        Reply->iToolCallCount = 1u;
        Reply->pToolCalls[0].sId = RecoveryHistoryCopy("interrupted-read");
        Reply->pToolCalls[0].sName = RecoveryHistoryCopy("read");
        Reply->pToolCalls[0].sArgumentsJson = RecoveryHistoryCopy("{\"path\":\"input.txt\"}");
        if (!Reply->pToolCalls[0].sId || !Reply->pToolCalls[0].sName ||
            !Reply->pToolCalls[0].sArgumentsJson) goto failed;
    }
    *Response = Reply;
    return XLLM_RESULT_OK;
failed:
    xllmResponseDestroy(Reply);
    return XLLM_RESULT_ERROR;
}

static bool RecoveryHistoryInterrupt(void)
{
    MdoAgentSession* Agent = MdoSessionAgentRef(g_RecoveryHistorySession);
    MdoAgentRunOptions Options;
    MdoAgentRun* Run = NULL;
    xwork_run_result Result = {0};
    xwork_error Error = {0};
    bool Ok = false;
    g_RecoveryHistoryInterrupt = true;
    MdoAgentRunOptionsInit(&Options);
    Options.Prompt = "Read the fixture file";
    if (Agent) Run = MdoAgentRunCreate(Agent, &Options, &Error);
    if (Run && MdoAgentRunStart(Run, &Error)) {
        xwork_result Outcome = MdoAgentRunWait(Run, xrtDeadlineAfter(5000000u), &Result, &Error);
        Ok = g_RecoveryHistoryIo ? Outcome == XWORK_RESULT_ERROR && Error.eCode == XWORK_ERROR_IO :
            Outcome == XWORK_RESULT_CANCELLED;
    }
    printf("recovery_history_seed=%d error=%d:%s\n", Ok, Error.eCode, Error.sMessage);
    xworkRunResultUnit(&Result);
    MdoAgentRunDestroy(Run);
    MdoAgentSessionRelease(Agent);
    g_RecoveryHistoryInterrupt = false;
    fflush(stdout);
    return Ok;
}

void ServiceInit(XS_HostInfo* Host)
{
    MdoSessionCreateOptions Create;
    xwork_error Error = {0};
    RecoveryHistoryProductInit(Host);
    MdoSessionCreateOptionsInit(&Create);
    Create.ProjectId = "default"; Create.RequestedId = "history-recovery";
    Create.Title = "Recovery after history editing";
    Create.Agent.WorkspaceRoot = RECOVERY_HISTORY_WORKSPACE;
    Create.Agent.AgentId = "mdo.default"; Create.Agent.ModelId = "ornith-1.5-35b";
    Create.Agent.Protocol = MDO_MODEL_PROTOCOL_OPENAI_RESPONSES;
    Create.Agent.ReasoningEffort = "medium"; Create.Agent.PermissionProfile = "full-access";
    Create.Agent.OnModelComplete = RecoveryHistoryModel;
    Create.Agent.OnEvent = RecoveryHistoryEvent;
    g_RecoveryHistorySession = MdoSessionCreate(&Create, &Error);
    if (g_RecoveryHistorySession) (void)RecoveryHistoryInterrupt();
    MdoSessionRelease(g_RecoveryHistorySession);
    g_RecoveryHistorySession = NULL;
    /* Runtime callbacks are not durable session identity. Normal HTTP resumes
     * reopen the session using the manager's fixture-only model callback. */
    g_MdoRuns.Options.OnModelComplete = RecoveryHistoryModel;
    g_MdoRuns.Options.OnEvent = RecoveryHistoryEvent;
}

void ServiceUnit(XS_HostInfo* Host)
{
    MdoSessionRelease(g_RecoveryHistorySession);
    g_RecoveryHistorySession = NULL;
    RecoveryHistoryProductUnit(Host);
}

XS_RequestResult RequestProc(XS_HttpReq* Request)
{
    xhttptarget Target;
    xwork_error Error = {0};
    MdoApiContext Context = {0};
    xvalue* Data;
    bool Clear, Ok;
    MdoSessionRuntimeOptions Runtime;
    if (!Request || !Request->head ||
        !xrtHttpTargetParse(Request->head->Method, Request->head->Target, &Target) ||
        !xrtStrEqual(Target.Path, XRT_STR_LITERAL("/__probe/history")))
        return RecoveryHistoryProductRequest(Request);
    /* Single worker and serial probe requests. Never exposed in product packs. */
    Context.Request = Request; Context.Target = Target;
    MdoApiRequestId(Context.RequestId);
    Clear = xrtStrEqual(Request->head->Method, XRT_STR_LITERAL("POST"));
    Ok = true;
    if (Clear || xrtStrEqual(Request->head->Method, XRT_STR_LITERAL("DELETE")) ||
        xrtStrEqual(Request->head->Method, XRT_STR_LITERAL("PATCH"))) {
        MdoSessionRuntimeOptionsInit(&Runtime);
        Runtime.OnModelComplete = RecoveryHistoryModel;
        Runtime.OnEvent = RecoveryHistoryEvent;
        g_RecoveryHistorySession = MdoSessionOpen("default", "history-recovery", &Runtime, &Error);
        Ok = g_RecoveryHistorySession != NULL;
    }
    if (Ok && (Clear || xrtStrEqual(Request->head->Method, XRT_STR_LITERAL("DELETE")))) {
        g_RecoveryHistoryIo = true;
        Ok = Clear ? MdoSessionClear(g_RecoveryHistorySession, &Error) :
            MdoSessionTruncateAfter(g_RecoveryHistorySession, 0u, &Error);
        if (Ok) Ok = RecoveryHistoryInterrupt();
    } else if (Ok && xrtStrEqual(Request->head->Method, XRT_STR_LITERAL("PATCH"))) {
        g_RecoveryHistoryIo = false;
        Ok = MdoSessionClear(g_RecoveryHistorySession, &Error) && RecoveryHistoryInterrupt();
    } else if (xrtStrEqual(Request->head->Method, XRT_STR_LITERAL("PUT"))) {
        g_RecoveryHistoryNoIdentity = true;
    }
    MdoSessionRelease(g_RecoveryHistorySession);
    g_RecoveryHistorySession = NULL;
    Data = xrtValueObject();
    if (Data && MdoApiValueSetBool(Data, "ok", Ok) &&
        MdoApiValueSetUInt(Data, "reads", g_RecoveryHistoryReads))
        (void)MdoApiReplySuccessTake(&Context, Ok ? 200u : 500u, Data, NULL);
    else xrtValueRelease(Data);
    return XS_OK;
}
