#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xsbase.h>
#include <xwork.h>
#include <xllm-executor.h>

static char Payload[40001];
static xwork_result Large(void* Data, const xwork_tool_context* Context,
    const char* Json, xwork_tool_result_writer* Writer, xwork_error* Error) {
    xwork_artifact_info Info; (void)Json;
    xworkArtifactInfoInit(&Info);
    if (!xworkToolResultWriterSetSuccess(Writer, true) ||
        !xworkToolResultWriterWriteText(Writer, Payload) ||
        !xworkToolResultWriterSaveArtifact(Writer, Context, Payload,
            strlen(Payload), "text/plain", &Info, Error)) return XWORK_RESULT_ERROR;
    if (Data) {
        const unsigned char Image[]={0x89,0x50,0x4e,0x47};
        if (!xworkToolResultWriterSetImage(Writer,Image,sizeof(Image),"image/png"))
            return XWORK_RESULT_ERROR;
    }
    return XWORK_RESULT_OK;
}
static void Run(bool Image) {
    xllm_session_config SC; xwork_agent_config AC; xwork_error Error;
    xwork_tool_definition Tool={0};xllm_executor Executor={0};
    xllm_executor_ctx Context={0};xllm_executor_result Result={0};
    xllm_tool_call Call={0};xllm_message Assistant;
    xllmSessionConfigInit(&SC);SC.uToolResultCapBytes=16u*1024u;
    xllm_session* Session=xllmSessionCreate(&SC,NULL);
    xworkAgentConfigInit(&AC);AC.pSession=Session;AC.sWorkspaceRoot=".";
    AC.sArtifactDirectory=".build/computer-use/result-cap-artifacts";
    AC.iMaxInlineToolBytes=12u*1024u;AC.bRegisterBuiltinTools=false;
    xwork_agent* Agent=xworkAgentCreate(&AC,&Error);
    Tool.sName="large_result";Tool.sDescription="Large typed result";
    Tool.sParametersJson="{\"type\":\"object\"}";Tool.uEffects=XWORK_TOOL_EFFECT_READ;
    Tool.OnExecuteV2=Large;Tool.pUserData=Image?(void*)1:NULL;
    if(!Agent||!xworkAgentRegisterTool(Agent,&Tool,&Error)||
        !xworkExecutorBind(&Executor,Agent,&Error))exit(2);
    Context.uTurn=xllmSessionBeginTurn(Session);
    Call.sId="large-result";Call.sName="large_result";Call.sArgumentsJson="{}";
    xllmMessageInit(&Assistant,XLLM_ROLE_ASSISTANT);
    if(!xllmMessageAddToolCall(&Assistant,Call.sId,Call.sName,"{}")||
       !xllmSessionAddMessage(Session,Context.uTurn,&Assistant,0))exit(3);
    xllmMessageUnit(&Assistant);
    if(!Executor.pExecute(Executor.pUserData,&Call,&Context,&Result)||!Result.bSuccess)exit(4);
    printf("inline_bytes=%zu image=%d\n",strlen(Result.sContent),(int)Image);
    if(strlen(Result.sContent)>SC.uToolResultCapBytes){puts("typed_result_exceeded_session_cap");exit(17);}
    bool Added=Image?xllmSessionAddToolResultWithImage(Session,Context.uTurn,Call.sId,
        Result.sContent,Result.pImageBytes,Result.iImageSize,Result.sImageMime):
        xllmSessionAddToolResult(Session,Context.uTurn,Call.sId,Result.sContent);
    if(!Added||!strstr(Result.sContent,"full output saved as artifact"))exit(5);
    xworkExecutorUnbind(&Executor);xworkAgentDestroy(Agent);xllmSessionDestroy(Session);
}
void ServiceInit(XS_HostInfo* Host){
    (void)Host;memset(Payload,'a',sizeof(Payload)-1);Payload[sizeof(Payload)-1]=0;
    Run(false);Run(true);puts("typed_result_cap=PASS");fflush(stdout);exit(0);
}
void ServiceUnit(XS_HostInfo* Host){(void)Host;}
