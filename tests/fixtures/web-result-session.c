/* Real session, xwork batch executor, model ledger and persistent UI events. */
static unsigned g_WebSessionCalls;
static bool g_WebSessionBudget = true;

static char* WebSessionCopy(const char* Text)
{
    size_t Size = strlen(Text) + 1u;
    char* Copy = (char*)malloc(Size);
    if (Copy) memcpy(Copy, Text, Size);
    return Copy;
}

static xllm_result WebSessionModel(void* Data, const xllm_request* Request,
    const xllm_stream_callbacks* Callbacks, xllm_response** Response, xllm_error* Error)
{
    xllm_response* Reply = NULL;
    char Arguments[512];
    const char* Name = "web_search";
    (void)Data; (void)Callbacks; (void)Error;
    *Response = NULL;
    if (++g_WebSessionCalls > 3u) return XLLM_RESULT_ERROR;
    for (size_t i = 0u; i < Request->iMessageCount; ++i) {
        const xllm_message* Message = &Request->pMessages[i];
        if (Message->eRole == XLLM_ROLE_TOOL && Message->sContent &&
            (strlen(Message->sContent) > 4256u || !xrtUtf8Valid(xrtStrView(Message->sContent), NULL)))
            g_WebSessionBudget = false;
    }
    strcpy(Arguments, "{\"query\":\"fixture-0\",\"count\":10}");
    if (g_WebSessionCalls == 2u) {
        xwork_artifact_info Info; char Path[300];
        xworkArtifactInfoInit(&Info);
        if (!xworkRuntimeArtifactAt(MdoBootstrapRuntime(), 0u, &Info) || !MdoWebResultLocator(&Info, Path))
            return XLLM_RESULT_ERROR;
        Name = "read";
        snprintf(Arguments, sizeof(Arguments), "{\"path\":\"%s\",\"start_line\":10,\"max_lines\":4,\"max_bytes\":1024}", Path);
    }
    Reply = (xllm_response*)calloc(1u, sizeof(*Reply));
    if (!Reply) return XLLM_RESULT_ERROR;
    Reply->sContent = WebSessionCopy(g_WebSessionCalls == 3u ? "Search and file read complete" : "");
    Reply->sModel = WebSessionCopy("local-web-session");
    Reply->sFinishReason = WebSessionCopy(g_WebSessionCalls == 3u ? "stop" : "tool_calls");
    Reply->eFinish = g_WebSessionCalls == 3u ? XLLM_FINISH_STOP : XLLM_FINISH_TOOL_CALLS;
    if (!Reply->sContent || !Reply->sModel || !Reply->sFinishReason) goto failed;
    if (g_WebSessionCalls != 3u) {
        Reply->pToolCalls = (xllm_tool_call*)calloc(1u, sizeof(xllm_tool_call));
        if (!Reply->pToolCalls) goto failed;
        Reply->iToolCallCount = 1u;
        Reply->pToolCalls[0].sName = WebSessionCopy(Name);
        Reply->pToolCalls[0].sId = WebSessionCopy(g_WebSessionCalls == 1u ? "session-search" : "session-read");
        Reply->pToolCalls[0].sArgumentsJson = WebSessionCopy(Arguments);
        if (!Reply->pToolCalls[0].sName || !Reply->pToolCalls[0].sId || !Reply->pToolCalls[0].sArgumentsJson) goto failed;
    }
    *Response = Reply; return XLLM_RESULT_OK;
failed:
    xllmResponseDestroy(Reply); return XLLM_RESULT_ERROR;
}

void ServiceInit(XS_HostInfo* Host)
{
    MdoSessionCreateOptions Create;
    MdoSession* Session = NULL; MdoAgentSession* Agent = NULL;
    MdoAgentRunOptions Options; MdoAgentRun* Run = NULL;
    xwork_run_result Result = {0}; xwork_error Error = {0};
    bool Ok = false;
    WebSessionProductInit(Host);
    /* Test-only token injection stays within this disposable fixture TU. */
    xrtMutexLock(g_MdoAccount.Lock);
    strcpy(g_MdoAccount.Tokens.Access, "probe-secret");
    g_MdoAccount.Tokens.Expires = xrtDeadlineAfter(600000000u);
    g_MdoAccount.Tokens.MemberId = 1;
    xrtMutexUnlock(g_MdoAccount.Lock);
    if (!MdoWebManagerReload()) goto done;
    MdoSessionCreateOptionsInit(&Create);
    Create.ProjectId = "default"; Create.RequestedId = "web-result-session";
    Create.Title = "Search file pipeline";
    Create.Agent.WorkspaceRoot = WEB_SESSION_WORKSPACE;
    Create.Agent.AgentId = "mdo.default"; Create.Agent.ModelId = "ornith-1.5-35b";
    Create.Agent.Protocol = MDO_MODEL_PROTOCOL_OPENAI_RESPONSES;
    Create.Agent.ReasoningEffort = "medium"; Create.Agent.PermissionProfile = "full-access";
    Create.Agent.OnModelComplete = WebSessionModel;
    Session = MdoSessionCreate(&Create, &Error);
    Agent = Session ? MdoSessionAgentRef(Session) : NULL;
    MdoAgentRunOptionsInit(&Options); Options.Prompt = "Search and read the relevant result range";
    Run = Agent ? MdoAgentRunCreate(Agent, &Options, &Error) : NULL;
    if (!Run || !MdoAgentRunStart(Run, &Error) ||
        MdoAgentRunWait(Run, xrtDeadlineAfter(5000000u), &Result, &Error) != XWORK_RESULT_OK ||
        g_WebSessionCalls != 3u || !g_WebSessionBudget) goto done;
    Ok = true;
done:
    printf("web_session_ok=%d calls=%u error=%d:%s\n", Ok, g_WebSessionCalls, Error.eCode, Error.sMessage);
    xworkRunResultUnit(&Result); MdoAgentRunDestroy(Run);
    MdoAgentSessionRelease(Agent); MdoSessionRelease(Session); fflush(stdout);
}
