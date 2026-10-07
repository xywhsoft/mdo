#include <string.h>
#include "mdo/module.h"

static char Prompt[128];
static mdo_result Register(const mdo_host_services_v1* Host,
    const mdo_registrar_v1* Registrar, void** Data,char* Error,size_t Capacity)
{
    mdo_agent_v1 Agent={0}; (void)Host; (void)Data;
    strcpy(Prompt,"CODE GENERATED "); strcat(Prompt,"AGENT INSTRUCTIONS");
    Agent.Size=sizeof(Agent); Agent.AbiVersion=MDO_MODULE_ABI_VERSION;
    Agent.Id="agent.researcher"; Agent.Name="Generated"; Agent.Description="C generated base prompt";
    Agent.SystemPrompt=Prompt; Agent.AllowedEffects=MDO_TOOL_EFFECT_ALL;
    Agent.Flags=MDO_AGENT_MAIN; Agent.MaxTurns=64u; Agent.TimeoutMilliseconds=120000u;
    Agent.MaxFinalBytes=65536u; Agent.MaxDepth=1u;
    return Registrar->AddAgent(Registrar->Context,&Agent,Error,Capacity);
}
static const mdo_module_v1 Module={
    MDO_V1_HEADER(mdo_module_v1), "user.agents.generated", "Generated Agent", "C prompt fixture",
    "1.0.0",0u,NULL,0u,Register,NULL
};
MDO_EXPORT const mdo_module_v1* mdoModuleEntry(void) { return &Module; }
