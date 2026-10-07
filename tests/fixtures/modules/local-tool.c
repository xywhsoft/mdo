#include "mdo/module.h"

static mdo_result Execute(void* UserData, const mdo_tool_context_v1* Context,
    const char* ArgumentsJson, mdo_result_writer_v1* Writer,
    char* Error, size_t ErrorCapacity)
{
    (void)UserData; (void)Context; (void)ArgumentsJson;
    (void)Error; (void)ErrorCapacity;
    return Writer->WriteText(Writer->Context, "LOCAL TOOL CALLBACK RESULT") &&
        Writer->SetSuccess(Writer->Context, true) ? MDO_RESULT_OK : MDO_RESULT_ERROR;
}
static const mdo_tool_v1 Tool = {
    MDO_V1_HEADER(mdo_tool_v1), "user.fixture", "Local fixture", "Local C callback",
    "{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}",
    MDO_TOOL_EFFECT_READ, MDO_TOOL_STRICT, NULL, NULL, 4096u, NULL, NULL, Execute, NULL
};
static mdo_result Register(const mdo_host_services_v1* Host,
    const mdo_registrar_v1* Registrar, void** Data, char* Error, size_t Capacity)
{
    (void)Host; (void)Data;
    return Registrar->AddTool(Registrar->Context, &Tool, Error, Capacity);
}
static const mdo_module_v1 Module = {
    MDO_V1_HEADER(mdo_module_v1), "user.tools.fixture", "Fixture", "Custom tool source",
    "1.0.0", 0u, NULL, 0u, Register, NULL
};
MDO_EXPORT const mdo_module_v1* mdoModuleEntry(void) { return &Module; }
