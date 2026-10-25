#include "mdo/agent.h"
#include <stdio.h>

static mdo_result OnEvent(void* Data,mdo_agent_phase Phase,
    const mdo_agent_context_v1* Context,char* Error,size_t Capacity)
{
    (void)Data;(void)Error;(void)Capacity;
    if(Phase==MDO_AGENT_CONTEXT){
        xvalue* State=mdoAgentState(Context);
        if(!State || !mdoAgentContextSource(Context,"system"))return MDO_RESULT_ERROR;
        /* State belongs to this runtime instance; module globals are shared.
         * Add/disable/reorder sources through the mutable context plan here. */
        if(!xrtValueObjectSetNew(State,xrtStrView("prepared"),xrtValueBool(true)))return MDO_RESULT_ERROR;
    }
    if(Phase==MDO_AGENT_BEFORE_REQUEST){
        xwork_agent_extension_context* Native=mdoAgentNative(Context);
        Native->pRequest->bHasTemperature=true;
        Native->pRequest->fTemperature=0.2;
    }
    return MDO_RESULT_OK;
}

/* Optional full driver. Reuse the managed loop to retain ordinary streaming,
 * tool permissions, retries, cancellation, persistence and recovery. */
static mdo_result Drive(void* Data,const mdo_agent_context_v1* Context,char* Error,size_t Capacity)
{
    (void)Data;xwork_error NativeError;xworkErrorInit(&NativeError);
    mdo_result Result=mdoAgentRunDefault(Context,&NativeError);
    if(Result!=MDO_RESULT_OK && Capacity)snprintf(Error,Capacity,"%s",NativeError.sMessage);
    return Result;
}
static const mdo_agent_program_v1 Program={MDO_V1_HEADER(mdo_agent_program_v1),OnEvent,Drive};
static const char* Tools[]={"read","ls","glob","grep","skill"};
static const mdo_agent_v1 Role={
    .Size=sizeof(mdo_agent_v1),.AbiVersion=MDO_MODULE_ABI_VERSION,
    .Id="agent.code-reviewer",.Name="C 审查员",.Description="使用 C 回调定制上下文及模型请求。",
    .SystemPrompt="Inspect relevant evidence and report actionable findings.",
    .Tools=Tools,.ToolCount=sizeof(Tools)/sizeof(*Tools),
    .AllowedEffects=MDO_TOOL_EFFECT_READ,
    .Flags=MDO_AGENT_MAIN|MDO_AGENT_SUBAGENT|MDO_AGENT_READ_ONLY|MDO_AGENT_EXPLICIT_TOOLS,
    .MaxTurns=64,.MaxDepth=4,.MaxFinalBytes=65536,.Program=&Program,
    .ConfigurationJson="{\"schema\":\"mdo.agent.v1\",\"context\":{\"environment\":true,\"memory.global\":false}}"
};
MDO_AGENT_MODULE(Role)
