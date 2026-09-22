#include "mdo/module.h"

static mdo_result MdoEchoExecute(void* UserData,
    const mdo_tool_context_v1* Context, const char* ArgumentsJson,
    mdo_result_writer_v1* Writer, char* ErrorMessage,
    size_t ErrorCapacity)
{
    const char* Text = ArgumentsJson != NULL ? ArgumentsJson : "{}";
    (void)UserData;
    (void)Context;
    (void)ErrorMessage;
    (void)ErrorCapacity;
    if ( Writer == NULL || Writer->WriteText == NULL ||
         Writer->SetSuccess == NULL ||
         !Writer->WriteText(Writer->Context, Text) ||
         !Writer->SetSuccess(Writer->Context, true) )
        return MDO_RESULT_LIMIT;
    return MDO_RESULT_OK;
}

static const mdo_tool_v1 g_MdoEchoTool = {
    MDO_V1_HEADER(mdo_tool_v1),
    "mdo.echo",
    "Echo",
    "Return the JSON arguments through the host-owned result writer.",
    "{\"type\":\"object\",\"properties\":{\"text\":{\"type\":\"string\"}},\"additionalProperties\":false}",
    MDO_TOOL_EFFECT_READ,
    MDO_TOOL_STRICT | MDO_TOOL_PARALLEL_SAFE,
    NULL,
    NULL,
    16384u,
    NULL,
    NULL,
    MdoEchoExecute,
    NULL
};

static mdo_result MdoEchoRegister(const mdo_host_services_v1* Host,
    const mdo_registrar_v1* Registrar, void** ModuleData,
    char* ErrorMessage, size_t ErrorCapacity)
{
    (void)Host;
    (void)ModuleData;
    if ( Registrar == NULL || Registrar->AddTool == NULL )
        return MDO_RESULT_ERROR;
    return Registrar->AddTool(Registrar->Context, &g_MdoEchoTool,
        ErrorMessage, ErrorCapacity);
}

static const mdo_module_v1 g_MdoEchoModule = {
    MDO_V1_HEADER(mdo_module_v1),
    "mdo.core.echo",
    "Built-in Echo Tool",
    "A minimal built-in module used to verify the complete module ABI path.",
    "1.0.0",
    0u,
    NULL,
    0u,
    MdoEchoRegister,
    NULL
};

MDO_EXPORT const mdo_module_v1* mdoModuleEntry(void)
{
    return &g_MdoEchoModule;
}
