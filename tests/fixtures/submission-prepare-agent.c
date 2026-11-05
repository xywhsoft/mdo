#include "mdo/agent.h"
#include <stdio.h>
static mdo_result Prepare(void* Data, mdo_agent_phase Phase,
    const mdo_agent_context_v1* Context, char* Error, size_t Capacity)
{
    (void)Data; (void)Error; (void)Capacity;
    if (Phase == MDO_AGENT_RUN_PREPARE) {
        printf("PREP_BARRIER_ENTERED=1\n"); fflush(stdout);
        for (unsigned n = 0; n < 5000 && !xrtCancelRequested((const xcancel*)Context->CancelToken); ++n) xrtSleep(5);
        return MDO_RESULT_CANCELLED;
    }
    return MDO_RESULT_OK;
}
static const mdo_agent_program_v1 Program = {MDO_V1_HEADER(mdo_agent_program_v1), Prepare, NULL};
static const mdo_agent_v1 Agent = {.Size=sizeof(Agent), .AbiVersion=1, .Id="agent.prepare-barrier",
    .Name="Prepare barrier", .Description="Offline cancellation fixture", .SystemPrompt="Prepare fixture",
    .Flags=MDO_AGENT_MAIN, .AllowedEffects=MDO_TOOL_EFFECT_ALL, .Program=&Program};
MDO_AGENT_MODULE(Agent)
