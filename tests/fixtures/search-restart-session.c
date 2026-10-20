/* Offline model boundary; real product session, ledger, web callback and UI events. */
static bool g_RestartResume;
static unsigned g_RestartModelCalls;
static bool g_RestartToolSeen;
static bool g_RestartToolSuccess;

static char* RestartCopy(const char* Text)
{
    size_t Size=strlen(Text)+1u; char* Copy=(char*)malloc(Size);
    if (Copy) memcpy(Copy,Text,Size);
    return Copy;
}

static xllm_result RestartModel(void* Data, const xllm_request* Request,
    const xllm_stream_callbacks* Callbacks, xllm_response** Response, xllm_error* Error)
{
    (void)Data; (void)Callbacks; (void)Error;
    *Response=NULL; ++g_RestartModelCalls;
    unsigned UserMessages=0u;
    for (size_t i=0u;i<Request->iMessageCount;++i) {
        const xllm_message* Message=&Request->pMessages[i];
        if (Message->eRole==XLLM_ROLE_USER && Message->sContent &&
            !strcmp(Message->sContent,"Search once and continue after interruptions")) ++UserMessages;
        if (Message->eRole==XLLM_ROLE_TOOL && Message->sContent) {
            g_RestartToolSeen=true;
            g_RestartToolSuccess=strstr(Message->sContent,"web_search_results")!=NULL;
            printf("restart_tool_result=%s\n",Message->sContent); fflush(stdout);
        }
    }
    printf("restart_user_messages=%u\n",UserMessages);fflush(stdout);
    if (UserMessages!=1u) return XLLM_RESULT_ERROR;
    if ((g_RestartResume && !g_RestartToolSeen) || g_RestartModelCalls>2u) return XLLM_RESULT_ERROR;
    xllm_response* Reply=(xllm_response*)calloc(1u,sizeof(*Reply));
    if (!Reply) return XLLM_RESULT_ERROR;
    Reply->sModel=RestartCopy("offline-restart-fixture");
    Reply->sContent=RestartCopy(!g_RestartToolSeen ? "" : g_RestartToolSuccess ?
        "Recovered search; continue the conversation" : "Search could not be safely recovered; continue without repeating it");
    Reply->sFinishReason=RestartCopy(g_RestartToolSeen ? "stop" : "tool_calls");
    Reply->eFinish=g_RestartToolSeen ? XLLM_FINISH_STOP : XLLM_FINISH_TOOL_CALLS;
    if (!Reply->sModel || !Reply->sContent || !Reply->sFinishReason) goto failed;
    if (!g_RestartToolSeen) {
        Reply->pToolCalls=(xllm_tool_call*)calloc(1u,sizeof(xllm_tool_call));
        if (!Reply->pToolCalls) goto failed;
        Reply->iToolCallCount=1u;
        Reply->pToolCalls[0].sId=RestartCopy("same-unfinished-model-call");
        Reply->pToolCalls[0].sName=RestartCopy("web_search");
        Reply->pToolCalls[0].sArgumentsJson=RestartCopy("{\"query\":\"hello\",\"count\":2}");
        if (!Reply->pToolCalls[0].sId || !Reply->pToolCalls[0].sName || !Reply->pToolCalls[0].sArgumentsJson) goto failed;
    }
    *Response=Reply;return XLLM_RESULT_OK;
failed:
    xllmResponseDestroy(Reply);return XLLM_RESULT_ERROR;
}

void ServiceInit(XS_HostInfo* Host)
{
    MdoSessionCreateOptions Create; MdoSessionRuntimeOptions Open;
    MdoSession* Session=NULL; MdoAgentSession* Agent=NULL; MdoAgentRun* Run=NULL;
    MdoAgentRunOptions Options; xwork_run_result Result={0}; xwork_error Error={0};
    SearchRestartProductInit(Host);
    g_RestartResume=getenv("RESTART_FIXTURE_RESUME")!=NULL;
    const char* Token=getenv("RESTART_FIXTURE_TOKEN"); const char* Member=getenv("RESTART_FIXTURE_MEMBER");
    if (!Token || !Member || strlen(Token)>=sizeof(g_MdoAccount.Tokens.Access)) goto done;
    /* Only this disposable TU injects a JWT from the local website fixture. */
    xrtMutexLock(g_MdoAccount.Lock);
    strcpy(g_MdoAccount.Tokens.Access,Token);
    g_MdoAccount.Tokens.Expires=MdoDeadlineAfterUs(600000000u);
    g_MdoAccount.Tokens.MemberId=(uint64)atoi(Member);
    xrtMutexUnlock(g_MdoAccount.Lock);
    if (!MdoWebManagerReload()) goto done;
    if (g_RestartResume) {
        MdoSessionRuntimeOptionsInit(&Open);Open.OnModelComplete=RestartModel;
        Session=MdoSessionOpen("default","search-restart-session",&Open,&Error);
    } else {
        MdoSessionCreateOptionsInit(&Create);Create.ProjectId="default";
        Create.RequestedId="search-restart-session";Create.Title="Search process restart";
        Create.Agent.WorkspaceRoot=RESTART_FIXTURE_WORKSPACE;Create.Agent.AgentId="mdo.default";
        Create.Agent.ModelId="ornith-1.5-35b";Create.Agent.Protocol=MDO_MODEL_PROTOCOL_OPENAI_RESPONSES;
        Create.Agent.ReasoningEffort="medium";Create.Agent.PermissionProfile="full-access";
        Create.Agent.OnModelComplete=RestartModel;Session=MdoSessionCreate(&Create,&Error);
    }
    Agent=Session ? MdoSessionAgentRef(Session) : NULL;
    MdoAgentRunOptionsInit(&Options);Options.Resume=g_RestartResume;
    Options.Prompt=g_RestartResume ? NULL : "Search once and continue after interruptions";
    Run=Agent ? MdoAgentRunCreate(Agent,&Options,&Error) : NULL;
    if (Run && MdoAgentRunStart(Run,&Error)) {
        xwork_result Outcome=MdoAgentRunWait(Run,MdoDeadlineAfterUs(15000000u),&Result,&Error);
        printf("restart_outcome=%d tool_seen:%d tool_success:%d model_calls:%u error:%d:%s\n",
            Outcome,g_RestartToolSeen,g_RestartToolSuccess,g_RestartModelCalls,Error.eCode,Error.sMessage);
    }
done:
    printf("restart_fixture_done=1 error:%d:%s\n",Error.eCode,Error.sMessage);fflush(stdout);
    xworkRunResultUnit(&Result);MdoAgentRunDestroy(Run);
    MdoAgentSessionRelease(Agent);MdoSessionRelease(Session);
}
