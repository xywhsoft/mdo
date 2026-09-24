#include <string.h>

#include "mdo/module.h"

/* The result is a complete, bounded plan snapshot. The session event bridge
 * validates it again before projecting it into the portable Home. */
static mdo_result MdoTodoExecute(void* UserData,
    const mdo_tool_context_v1* Context, const char* ArgumentsJson,
    mdo_result_writer_v1* Writer, char* ErrorMessage,
    size_t ErrorCapacity)
{
    size_t Size = 0u;
    (void)UserData;
    (void)Context;
    (void)ErrorMessage;
    (void)ErrorCapacity;
    if ( ArgumentsJson == NULL || Writer == NULL ||
         Writer->Write == NULL || Writer->SetSuccess == NULL )
        return MDO_RESULT_ERROR;
    while ( Size <= 12288u && ArgumentsJson[Size] != '\0' ) ++Size;
    if ( Size == 0u || Size > 12288u ) return MDO_RESULT_LIMIT;
    if ( !Writer->Write(Writer->Context, ArgumentsJson, Size) ||
         !Writer->SetSuccess(Writer->Context, true) )
        return MDO_RESULT_LIMIT;
    return MDO_RESULT_OK;
}

static const mdo_tool_v1 MDO_TODO_TOOL = {
    MDO_V1_HEADER(mdo_tool_v1),
    "mdo.todo",
    "Update plan",
    "Show a concise task plan. Send the complete list on each update; an empty list clears it.",
    "{\"type\":\"object\",\"properties\":{\"items\":{\"type\":\"array\",\"maxItems\":24,\"items\":{\"type\":\"object\",\"properties\":{\"text\":{\"type\":\"string\",\"minLength\":1,\"maxLength\":240},\"done\":{\"type\":\"boolean\"}},\"required\":[\"text\",\"done\"],\"additionalProperties\":false}}},\"required\":[\"items\"],\"additionalProperties\":false}",
    MDO_TOOL_EFFECT_READ,
    MDO_TOOL_STRICT,
    "mdo.todo",
    NULL,
    12288u,
    NULL,
    NULL,
    MdoTodoExecute,
    NULL
};

static mdo_result MdoTodoRegister(const mdo_host_services_v1* Host,
    const mdo_registrar_v1* Registrar, void** ModuleData,
    char* ErrorMessage, size_t ErrorCapacity)
{
    (void)Host;
    (void)ModuleData;
    if ( Registrar == NULL || Registrar->AddTool == NULL )
        return MDO_RESULT_ERROR;
    return Registrar->AddTool(Registrar->Context, &MDO_TODO_TOOL,
        ErrorMessage, ErrorCapacity);
}

static const mdo_module_v1 MDO_TODO_MODULE = {
    MDO_V1_HEADER(mdo_module_v1),
    "mdo.core.todo",
    "Built-in Plan Tool",
    "Publishes a compact plan snapshot for the session conversation dock.",
    "1.0.0",
    0u,
    NULL,
    0u,
    MdoTodoRegister,
    NULL
};

MDO_EXPORT const mdo_module_v1* mdoModuleEntry(void)
{
    return &MDO_TODO_MODULE;
}
