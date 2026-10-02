/* Real mdo session/bridge fixture; every model response is local and bounded. */
static unsigned g_HomeArtifactCalls;

static char* HomeArtifactText(const char* Text)
{
    size_t Size = strlen(Text) + 1u;
    char* Copy = (char*)malloc(Size);
    if ( Copy ) memcpy(Copy, Text, Size);
    return Copy;
}

static xllm_result HomeArtifactModel(void* Data, const xllm_request* Request,
    const xllm_stream_callbacks* Callbacks, xllm_response** Response, xllm_error* Error)
{
    xllm_response* Reply;
    const char* Name = "read";
    const char* Id = "home-artifact-read";
    const char* Arguments = "{\"path\":\"input.txt\",\"max_lines\":200}";
    (void)Data; (void)Request; (void)Callbacks; (void)Error;
    *Response = NULL;
    if ( ++g_HomeArtifactCalls > 4u ) return XLLM_RESULT_ERROR;
    Reply = (xllm_response*)calloc(1u, sizeof(*Reply));
    if ( Reply == NULL ) return XLLM_RESULT_ERROR;
    Reply->sContent = HomeArtifactText(g_HomeArtifactCalls == 4u ? "done" : "");
    Reply->sModel = HomeArtifactText("local-home-artifact");
    Reply->sFinishReason = HomeArtifactText(g_HomeArtifactCalls == 4u ? "stop" : "tool_calls");
    Reply->eFinish = g_HomeArtifactCalls == 4u ? XLLM_FINISH_STOP : XLLM_FINISH_TOOL_CALLS;
    if ( !Reply->sContent || !Reply->sModel || !Reply->sFinishReason ) goto fail;
    if ( g_HomeArtifactCalls != 4u ) {
        if ( g_HomeArtifactCalls == 2u ) {
            Id = "home-artifact-outside-read";
            Arguments = "{\"path\":\"../outside.txt\"}";
        } else if ( g_HomeArtifactCalls == 3u ) {
            Id = "home-artifact-denied-write"; Name = "write";
            Arguments = "{\"path\":\"input.txt\",\"content\":\"forbidden\"}";
        }
        Reply->pToolCalls = (xllm_tool_call*)calloc(1u, sizeof(xllm_tool_call));
        if ( !Reply->pToolCalls ) goto fail;
        Reply->iToolCallCount = 1u;
        Reply->pToolCalls[0].sId = HomeArtifactText(Id);
        Reply->pToolCalls[0].sName = HomeArtifactText(Name);
        Reply->pToolCalls[0].sArgumentsJson = HomeArtifactText(Arguments);
        if ( !Reply->pToolCalls[0].sId || !Reply->pToolCalls[0].sName ||
             !Reply->pToolCalls[0].sArgumentsJson ) goto fail;
    }
    *Response = Reply;
    return XLLM_RESULT_OK;
fail:
    xllmResponseDestroy(Reply);
    return XLLM_RESULT_ERROR;
}

void ServiceInit(XS_HostInfo* Host)
{
    MdoSessionCreateOptions Create;
    MdoSessionRuntimeOptions Open;
    MdoSession* Session = NULL;
    MdoAgentSession* Agent = NULL;
    MdoAgentRunOptions RunOptions;
    MdoAgentRun* Run = NULL;
    xwork_run_result Result = {0};
    xwork_artifact_info Info;
    xwork_error Error = {0};
    xwork_artifact_store* UnexpectedStore = NULL;
    xroot UnexpectedRoot = NULL;
    bool Exists = false, Ok = false;
    HomeArtifactProductInit(Host);
    UnexpectedStore = MdoAgentsArtifactStore(HOME_ARTIFACT_WORKSPACE, &Error);
    if ( UnexpectedStore != NULL ) goto done;
    UnexpectedRoot = MdoHomeOpenStorageDirectory("../outside-storage");
    if ( UnexpectedRoot != NULL ) goto done;
    if ( !MdoHomeExternalStat("sessions/default/home-artifact-probe/meta.json", &Exists, NULL) ) goto done;
    if ( Exists ) {
        MdoSessionRuntimeOptionsInit(&Open);
        Open.OnModelComplete = HomeArtifactModel;
        Session = MdoSessionOpen("default", "home-artifact-probe", &Open, &Error);
    } else {
        MdoSessionCreateOptionsInit(&Create);
        Create.ProjectId = "default"; Create.RequestedId = "home-artifact-probe";
        Create.Title = "External Home tool output";
        Create.Agent.WorkspaceRoot = HOME_ARTIFACT_WORKSPACE;
        Create.Agent.AgentId = "mdo.default"; Create.Agent.ModelId = "ornith-1.5-35b";
        Create.Agent.Protocol = MDO_MODEL_PROTOCOL_OPENAI_RESPONSES;
        Create.Agent.ReasoningEffort = "medium";
        Create.Agent.PermissionProfile = "read-only";
        Create.Agent.OnModelComplete = HomeArtifactModel;
        Session = MdoSessionCreate(&Create, &Error);
    }
    Agent = Session ? MdoSessionAgentRef(Session) : NULL;
    MdoAgentRunOptionsInit(&RunOptions);
    RunOptions.Prompt = "Read input.txt, then verify workspace restrictions";
    Run = Agent ? MdoAgentRunCreate(Agent, &RunOptions, &Error) : NULL;
    if ( !Run || !MdoAgentRunStart(Run, &Error) ||
         MdoAgentRunWait(Run, xrtDeadlineAfter(UINT64_C(5000000)), &Result, &Error) != XWORK_RESULT_OK ||
         g_HomeArtifactCalls != 4u ) goto done;
    xworkArtifactInfoInit(&Info);
    if ( xworkRuntimeArtifactCount(MdoBootstrapRuntime()) != 1u ||
         !xworkRuntimeArtifactAt(MdoBootstrapRuntime(), 0u, &Info) ) goto done;
    UnexpectedRoot = MdoHomeOpenStorageDirectory("sessions/default/home-artifact-probe/meta.json");
    if ( UnexpectedRoot != NULL ) goto done;
    printf("home_artifact_id=%llu\nhome_artifact_path=%s\nhome_artifact_sha=%s\n",
        (unsigned long long)Info.uArtifactId, Info.sPath, Info.sSha256);
    Ok = true;
done:
    if ( !Ok ) printf("home_artifact_error=%d:%s calls:%u\n", Error.eCode, Error.sMessage,
        g_HomeArtifactCalls);
    xworkRunResultUnit(&Result);
    MdoAgentRunDestroy(Run);
    MdoAgentSessionRelease(Agent);
    MdoSessionRelease(Session);
    xworkArtifactStoreRelease(UnexpectedStore);
    if ( UnexpectedRoot ) (void)xrtRootClose(UnexpectedRoot);
    if ( Ok ) {
        if ( !MdoHomeRequireRestart("bounded artifact storage freeze fixture") ) Ok = false;
        UnexpectedRoot = MdoHomeOpenStorageDirectory("sessions/default/home-artifact-probe/artifacts");
        if ( UnexpectedRoot ) { (void)xrtRootClose(UnexpectedRoot); Ok = false; }
    }
    printf("home_artifact_ok=%d\n", Ok);
    fflush(stdout);
}
