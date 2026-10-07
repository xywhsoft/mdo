// A working read-only template using the same ABI as production C modules.
// No external compiler, dependency injection or second plugin protocol.
import { portableId } from "./extension-formats.js";
export function newTool(id = "my-tool") {
  if (!portableId(id)) throw new Error("Invalid tool file ID");
  return `#include "mdo/module.h"

/* mdo owns argument/result storage. Write a bounded result before returning.
 * Declare effects and permissions before adding file/process/network access. */
static mdo_result Execute(void* UserData, const mdo_tool_context_v1* Context,
    const char* ArgumentsJson, mdo_result_writer_v1* Writer,
    char* Error, size_t ErrorCapacity)
{
    (void)UserData; (void)Context; (void)Error; (void)ErrorCapacity;
    /* Replace this echo with your implementation. ArgumentsJson follows Schema. */
    if (!Writer->WriteText(Writer->Context, ArgumentsJson) ||
        !Writer->SetSuccess(Writer->Context, true)) return MDO_RESULT_ERROR;
    return MDO_RESULT_OK;
}

static const mdo_tool_v1 Tool = {
    MDO_V1_HEADER(mdo_tool_v1),
    "user.${id}", "${id}", "Return the input JSON as text.",
    "{\\"type\\":\\"object\\",\\"properties\\":{\\"text\\":{\\"type\\":\\"string\\",\\"maxLength\\":2048}},\\"required\\":[\\"text\\"],\\"additionalProperties\\":false}",
    MDO_TOOL_EFFECT_READ, MDO_TOOL_STRICT,
    NULL, NULL, 8192u, NULL, NULL, Execute, NULL
};

static mdo_result Register(const mdo_host_services_v1* Host,
    const mdo_registrar_v1* Registrar, void** ModuleData,
    char* Error, size_t ErrorCapacity)
{
    (void)Host; (void)ModuleData;
    return Registrar->AddTool(Registrar->Context, &Tool, Error, ErrorCapacity);
}

static const mdo_module_v1 Module = {
    MDO_V1_HEADER(mdo_module_v1),
    "user.tools.${id}", "${id}", "Custom C tool module.", "1.0.0",
    0u, NULL, 0u, Register, NULL
};

MDO_EXPORT const mdo_module_v1* mdoModuleEntry(void) { return &Module; }
`;
}
