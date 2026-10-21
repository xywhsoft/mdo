/* Full agent/session probe; the model callback is deterministic and offline. */
static unsigned ProbeMode, ProbeModelCalls, ProbeToolDone, ProbeToolFailed;
static xcancel* ProbeCancel;
static xthread* ProbeStopThread;
static const char* ProbeNames[] = {"page-recover", "page-exhausted", "exec-timeout",
    "mcp-timeout", "nine-failures", "ten-failures", "failure-reset", "task-deadline", "user-stop", "mcp-http-timeout"};

static char* ProbeCopy(const char* Text)
{
    size_t Size=strlen(Text)+1u; char* Copy=(char*)malloc(Size);
    if (Copy) memcpy(Copy,Text,Size); return Copy;
}
static int32 ProbeStop(void* Data) { xrtSleep(50u); xrtCancelRequest((xcancel*)Data); return 0; }
static bool ProbeEvent(void* Data, const xwork_event* Event)
{
    (void)Data;
    if (Event->eKind == XWORK_EVENT_TOOL_DONE) { ++ProbeToolDone; if (!Event->bSuccess) ++ProbeToolFailed; }
    if (ProbeMode==9u && Event->eKind==XWORK_EVENT_TOOL_DONE)
        printf("http_tool=%s success:%d text:%.500s\n",Event->sToolName,Event->bSuccess,Event->sText?Event->sText:"");
    if (Event->eKind == XWORK_EVENT_TOOL_START && ProbeMode == 8u)
        ProbeStopThread = xrtThreadCreate(ProbeStop, ProbeCancel, 0u);
    return true;
}
static xllm_result ProbeModel(void* Data, const xllm_request* Request,
    const xllm_stream_callbacks* Callbacks, xllm_response** Output, xllm_error* Error)
{
    unsigned Step = ProbeModelCalls++, Repeats = (ProbeMode == 3u || ProbeMode == 9u) ? 2u : ProbeMode == 4u ? 9u :
        ProbeMode == 5u ? 11u : ProbeMode == 6u ? 19u : 1u;
    const char* Tool = "web_open"; char Arguments[2048], Id[64];
    xllm_response* Reply = (xllm_response*)calloc(1u, sizeof(*Reply));
    (void)Data; (void)Request; (void)Callbacks; (void)Error;
    if (!Reply || Step > 24u) { free(Reply); return XLLM_RESULT_ERROR; }
    bool Final = Step >= Repeats;
    if (ProbeMode >= 4u && ProbeMode <= 6u) {
        Tool = "read";
        snprintf(Arguments,sizeof(Arguments),"{\"path\":\"%s\"}",ProbeMode == 6u && Step == 9u ? "input.txt" : "missing.txt");
    } else if (ProbeMode == 2u) {
        Tool = "exec"; snprintf(Arguments,sizeof(Arguments),"{\"argv\":[%s,\"-c\",\"import time; time.sleep(1)\"],\"timeout_ms\":100}", PROBE_PYTHON_JSON);
    } else if (ProbeMode == 3u || ProbeMode == 9u) {
        Tool = Step ? (ProbeMode == 9u ? "mcp__httptimeout__echo" : "mcp__timeout__echo") : "tool_load";
        snprintf(Arguments,sizeof(Arguments),"%s",Step ? "{\"text\":\"probe\"}" :
            ProbeMode == 9u ? "{\"server\":\"httptimeout\",\"tool\":\"echo\"}" : "{\"server\":\"timeout\",\"tool\":\"echo\"}");
    } else {
        snprintf(Arguments,sizeof(Arguments),"{\"url\":\"%s/%s\"}",PROBE_PAGE_ORIGIN,ProbeMode == 0u ? "recover" : "slow");
    }
    Reply->sContent = ProbeCopy(Final ? "Tool failure recovered" : "");
    Reply->sModel = ProbeCopy("offline-timeout-probe");
    Reply->sFinishReason = ProbeCopy(Final ? "stop" : "tool_calls");
    Reply->eFinish = Final ? XLLM_FINISH_STOP : XLLM_FINISH_TOOL_CALLS;
    if (!Reply->sContent || !Reply->sModel || !Reply->sFinishReason) goto failed;
    if (!Final) {
        Reply->pToolCalls = (xllm_tool_call*)calloc(1u,sizeof(xllm_tool_call));
        if (!Reply->pToolCalls) goto failed;
        Reply->iToolCallCount = 1u;
        snprintf(Id,sizeof(Id),"call-%u-%u",ProbeMode,Step);
        Reply->pToolCalls[0].sId = ProbeCopy(Id);
        Reply->pToolCalls[0].sName = ProbeCopy(Tool);
        Reply->pToolCalls[0].sArgumentsJson = ProbeCopy(Arguments);
        if (!Reply->pToolCalls[0].sId || !Reply->pToolCalls[0].sName || !Reply->pToolCalls[0].sArgumentsJson) goto failed;
    }
    *Output=Reply; return XLLM_RESULT_OK;
failed:
    xllmResponseDestroy(Reply); return XLLM_RESULT_ERROR;
}
void ServiceInit(XS_HostInfo* Host)
{
    bool Ok = true;
    ProbeProductInit(Host);
    /* Test-only authorization in this disposable translation unit. */
    xrtMutexLock(g_MdoAccount.Lock);
    strcpy(g_MdoAccount.Tokens.Access,"probe-secret");
    g_MdoAccount.Tokens.Expires=MdoDeadlineAfterUs(600000000u);
    g_MdoAccount.Tokens.MemberId=1;
    xrtMutexUnlock(g_MdoAccount.Lock);
    if (!MdoWebManagerReload()) { printf("probe_web_init_failed=1\n"); fflush(stdout); exit(1); }
    for (ProbeMode=PROBE_FIRST_MODE;ProbeMode<10u;++ProbeMode) {
        MdoSessionCreateOptions Create; MdoAgentRunOptions Options;
        MdoSession* Session=NULL; MdoAgentSession* Agent=NULL; MdoAgentRun* Run=NULL;
        xwork_run_result Result={0}; xwork_error Error={0}; xwork_result Outcome=XWORK_RESULT_ERROR;
        ProbeModelCalls=ProbeToolDone=ProbeToolFailed=0u;
        ProbeCancel=xrtCancelCreate(); ProbeStopThread=NULL;
        MdoSessionCreateOptionsInit(&Create);
        Create.ProjectId="default"; Create.RequestedId=ProbeNames[ProbeMode]; Create.Title=ProbeNames[ProbeMode];
        Create.Agent.WorkspaceRoot=PROBE_WORKSPACE; Create.Agent.AgentId="mdo.default";
        Create.Agent.ModelId="ornith-1.5-35b"; Create.Agent.Protocol=MDO_MODEL_PROTOCOL_OPENAI_RESPONSES;
        Create.Agent.ReasoningEffort="medium"; Create.Agent.PermissionProfile="full-access";
        Create.Agent.OnModelComplete=ProbeModel; Create.Agent.OnEvent=ProbeEvent;
        Session=MdoSessionCreate(&Create,&Error); Agent=Session?MdoSessionAgentRef(Session):NULL;
        MdoAgentRunOptionsInit(&Options); Options.Prompt=ProbeNames[ProbeMode]; Options.Cancel=ProbeCancel;
        if (ProbeMode==7u) Options.Deadline=MdoDeadlineAfterUs(100000u);
        Run=Agent?MdoAgentRunCreate(Agent,&Options,&Error):NULL;
        if (Run && MdoAgentRunStart(Run,&Error))
            Outcome=MdoAgentRunWait(Run,MdoDeadlineAfterUs(35000000u),&Result,&Error);
        if (ProbeStopThread) { xrtThreadWait(ProbeStopThread); xrtThreadDestroy(ProbeStopThread); }
        bool Pass=ProbeMode==5u ? Outcome==XWORK_RESULT_LIMIT && ProbeToolFailed==10u :
            ProbeMode==7u ? (Outcome==XWORK_RESULT_TIMEOUT || Outcome==XWORK_RESULT_ERROR) && Error.eCode==XWORK_ERROR_TIMEOUT :
            ProbeMode==8u ? Outcome==XWORK_RESULT_CANCELLED && xrtCancelRequested(ProbeCancel) :
            Outcome==XWORK_RESULT_OK && Result.sFinalText && !strcmp(Result.sFinalText,"Tool failure recovered");
        if (ProbeMode==0u) Pass=Pass && ProbeToolDone==1u && ProbeToolFailed==0u;
        if (ProbeMode==1u || ProbeMode==2u || ProbeMode==3u || ProbeMode==9u) Pass=Pass && ProbeToolFailed==1u;
        if (ProbeMode==4u) Pass=Pass && ProbeToolFailed==9u;
        if (ProbeMode==6u) Pass=Pass && ProbeToolFailed==18u;
        printf("session_case=%s pass:%d outcome:%d error:%d models:%u tools:%u failures:%u message:%s\n",
            ProbeNames[ProbeMode],Pass,Outcome,Error.eCode,ProbeModelCalls,ProbeToolDone,ProbeToolFailed,Error.sMessage);
        Ok=Ok && Pass;
        xworkRunResultUnit(&Result); MdoAgentRunDestroy(Run); MdoAgentSessionRelease(Agent);
        MdoSessionRelease(Session); xrtCancelDestroy(ProbeCancel); fflush(stdout);
    }
    ServiceUnit(Host);
    printf("timeout_sessions_ok=%d\n",Ok); fflush(stdout); exit(Ok?0:1);
}
