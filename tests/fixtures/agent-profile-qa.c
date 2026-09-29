#include "mdo/module.h"

static const mdo_agent_v1 QA_PROFILE = {
    .Size = sizeof(mdo_agent_v1),
    .AbiVersion = MDO_MODULE_ABI_VERSION,
    .Id = "qa.profile",
    .Name = "QA Profile",
    .Description = "Bounded profile selection fixture.",
    .Model = "ling-3.0-tiny-text-qa",
    .ReasoningEffort = "high",
    .SystemPrompt = "Answer bounded QA questions directly.",
    .PermissionProfile = "read-only",
    .MaxOutputTokens = 1024u,
    .MaxTurns = 4u,
    .TimeoutMilliseconds = 30000u,
    .MaxFinalBytes = 4096u,
    .AllowedEffects = MDO_TOOL_EFFECT_READ,
    .Flags = MDO_AGENT_MAIN | MDO_AGENT_READ_ONLY,
};

static mdo_result QaRegister(const mdo_host_services_v1* host,
    const mdo_registrar_v1* registrar, void** data,
    char* error, size_t error_capacity)
{
    (void)host;
    (void)data;
    return registrar->AddAgent(registrar->Context, &QA_PROFILE,
        error, error_capacity);
}

static const mdo_module_v1 QA_MODULE = {
    MDO_V1_HEADER(mdo_module_v1),
    "qa.profile-module", "QA Profile Module",
    "Bounded profile selection fixture.", "1.0.0",
    0u, NULL, 0u, QaRegister, NULL
};

MDO_EXPORT const mdo_module_v1* mdoModuleEntry(void)
{
    return &QA_MODULE;
}
