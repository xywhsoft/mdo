#define ServiceInit EditQaProductInit
#include "generated/mdo_unity.c"
#undef ServiceInit
static unsigned ModelCalls;
static bool HoldFirst = true;
static bool HoldAll;
static xllm_result EditQaModel(void* User,const xllm_request* Request,const xllm_stream_callbacks* Stream,xllm_response** Out,xllm_error* Error){
    (void)User;(void)Stream;unsigned Call=++ModelCalls;printf("EDIT_MODEL_CALL=%u\n",Call);fflush(stdout);
    if(HoldFirst && (Call==1 || HoldAll)){
        uint64 End=MdoDeadlineAfterUs(30000000);
        while(!xrtCancelRequested(Request->pCancel)&&!MdoDeadlineExpired(End))xrtSleep(5);
        xrtSleep(500); /* Deliberately exercise the acknowledged-stop window. */
        xllmErrorInit(Error);Error->eCode=XLLM_ERROR_CANCELLED;return XLLM_RESULT_CANCELLED;
    }
    xllm_response* R=calloc(1,sizeof(*R));R->sContent=strdup("Edited reply accepted.");R->sModel=strdup("fixture");R->uHttpStatus=200;
    R->sFinishReason=strdup("stop");R->eFinish=XLLM_FINISH_STOP;*Out=R;xllmErrorInit(Error);return XLLM_RESULT_OK;
}
void ServiceInit(XS_HostInfo* Host){
    char* NoHold=xrtEnvGet("MDO_EDIT_NO_HOLD");HoldFirst=!(NoHold&&NoHold[0]);xrtFree(NoHold);
    char* All=xrtEnvGet("MDO_EDIT_HOLD_ALL");HoldAll=All&&All[0];xrtFree(All);
    EditQaProductInit(Host);if(!MdoBootstrapRuntime())return;
    (void)MdoConfigImport(MDO_CONFIG_SETTINGS,XRT_STR_LITERAL("{\"schema_version\":1,\"patch\":{\"agent\":{\"memory\":false,\"web_search\":false}}}"));
    void(*Observer)(void*)=g_MdoRuns.Observer;void* Data=g_MdoRuns.ObserverData;
    MdoRunManagerUnit();MdoRunManagerOptions Manager;MdoRunManagerOptionsInit(&Manager);Manager.OnModelComplete=EditQaModel;xwork_error E;
    if(!MdoRunManagerInit(MdoBootstrapRuntime(),&Manager,&E)){printf("EDIT_INIT_FAILED=%s\n",E.sMessage);return;}MdoRunObserve(Observer,Data);
    char* Workspace=xrtEnvGet("MDO_EDIT_WORKSPACE");MdoProjectCreateOptions P;MdoProjectCreateOptionsInit(&P);P.Id="edit-qa";P.Name="Edit QA";P.WorkspaceRoot=Workspace;
    if(!MdoProjectCreate(&P,NULL,&E)){printf("EDIT_INIT_FAILED=%s\n",E.sMessage);return;}
    MdoSessionCreateOptions O;MdoSessionCreateOptionsInit(&O);O.ProjectId=P.Id;O.RequestedId="stopped-edit";O.Title="Stopped edit fixture";
    O.Agent.WorkspaceRoot=Workspace;O.Agent.AgentId="mdo.default";O.Agent.ModelId="history-fixture";O.Agent.Protocol=MDO_MODEL_PROTOCOL_OPENAI_RESPONSES;O.Agent.PermissionProfile="full-access";
    MdoSession* Session=MdoSessionCreate(&O,&E);if(!Session)printf("EDIT_INIT_FAILED=%s\n",E.sMessage);MdoSessionRelease(Session);
    O.RequestedId="accepted-prep";O.Title="Accepted preparation";O.Agent.AgentId="agent.prepare-barrier";
    O.Agent.WorkspaceRoot=P.WorkspaceRoot;
    Session=MdoSessionCreate(&O,&E);if(!Session)printf("EDIT_INIT_FAILED=barrier:%s\n",E.sMessage);MdoSessionRelease(Session);xrtFree(Workspace);
    printf("EDIT_FIXTURE_READY=1\n");fflush(stdout);
}
