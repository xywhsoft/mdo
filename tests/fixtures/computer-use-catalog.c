#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xsbase.h>
#include <xwork.h>
#include <xllm-executor.h>

static int Owners;
static bool Retain(void* Data) { (void)Data; ++Owners; return true; }
static void Release(void* Data) { (void)Data; --Owners; }
static xwork_result Noop(void* Data, const xwork_tool_context* Context,
    const char* Json, xwork_tool_output* Output, xwork_error* Error) {
    (void)Data; (void)Context; (void)Json; (void)Error;
    return xworkToolOutputSet(Output, true, "ok") ? XWORK_RESULT_OK : XWORK_RESULT_ERROR;
}
static xwork_result Grow(void* Data, const xwork_tool_context* Context,
    const char* Json, xwork_tool_output* Output, xwork_error* Error) {
    xwork_tool_definition Tool = {0};
    (void)Data; (void)Json;
    Tool.sName = "added"; Tool.sDescription = "Added at execution time";
    Tool.sParametersJson = "{\"type\":\"object\"}";
    Tool.uEffects = XWORK_TOOL_EFFECT_READ; Tool.OnExecute = Noop;
    if (!xworkAgentRegisterTool(Context->pAgent, &Tool, Error)) return XWORK_RESULT_ERROR;
    /* The cached request and active callback still borrow the old entry.
     * An owner in both catalog generations must remain alive at this point.
     * Exit before dereferencing the retired entry on an unsafe host. */
    if (Owners != 2) {
        printf("catalog_retired_while_borrowed owners=%d\n", Owners);
        fflush(stdout); exit(17);
    }
    return xworkToolOutputSet(Output, true, "grown") ? XWORK_RESULT_OK : XWORK_RESULT_ERROR;
}
static void Run(bool ListFirst) {
    xllm_session_config SessionConfig; xwork_agent_config Config; xwork_error Error;
    xwork_tool_definition Tool = {0}; xllm_executor Executor = {0};
    xllm_request Request; xllm_tool_call Call = {0};
    xllm_executor_ctx Context = {0}; xllm_executor_result Result = {0};
    xllmSessionConfigInit(&SessionConfig);
    xllm_session* Session = xllmSessionCreate(&SessionConfig, NULL);
    xworkAgentConfigInit(&Config); Config.pSession = Session;
    Config.sWorkspaceRoot = "."; Config.bRegisterBuiltinTools = false;
    xwork_agent* Agent = xworkAgentCreate(&Config, &Error);
    Tool.sName = "grow"; Tool.sDescription = "Grow the tool catalog";
    Tool.sParametersJson = "{\"type\":\"object\"}";
    Tool.uEffects = XWORK_TOOL_EFFECT_READ; Tool.OnExecute = Grow;
    Tool.OnOwnerRetain = Retain; Tool.OnOwnerRelease = Release;
    if (!Agent || !xworkAgentRegisterTool(Agent, &Tool, &Error) ||
        !xworkExecutorBind(&Executor, Agent, &Error)) {
        printf("catalog_probe_init_error=%s\n", Error.sMessage); exit(2);
    }
    xllmRequestInit(&Request);
    if (ListFirst && !Executor.pListTools(Executor.pUserData, &Request)) exit(3);
    Call.sId = "catalog-growth"; Call.sName = "grow"; Call.sArgumentsJson = "{}";
    Context.uRound = 1;
    if (!Executor.pExecute(Executor.pUserData, &Call, &Context, &Result) || !Result.bSuccess) exit(4);
    if (ListFirst && Owners != 2) {
        puts("catalog_view_retired_after_callback"); fflush(stdout); exit(18);
    }
    if (ListFirst && (Request.iToolCount != 1 || strcmp(Request.pTools[0].sName, "grow"))) exit(5);
    xllmRequestUnit(&Request);
    xworkExecutorUnbind(&Executor);
    if (Owners != 1) exit(6);
    xworkAgentDestroy(Agent); xllmSessionDestroy(Session);
    if (Owners != 0) exit(7);
}
void ServiceInit(XS_HostInfo* Host) {
    (void)Host;
    Run(true);
    Run(false);
    puts("catalog_lifetime=PASS"); fflush(stdout); exit(0);
}
void ServiceUnit(XS_HostInfo* Host) { (void)Host; }
