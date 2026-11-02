#ifndef MDO_BUILTIN_TOOL_H
#define MDO_BUILTIN_TOOL_H
#include "module.h"

/* A normal C extension entry which reuses a host-provided implementation.
 * Its directory, trust, dependencies and activation are managed like any
 * other tool extension; binding never elevates the caller's permissions. */
#define MDO_BUILTIN_TOOL(ToolId) \
static mdo_result MdoBuiltinRegister(const mdo_host_services_v1* Host, \
    const mdo_registrar_v1* R, void** Data, char* Error, size_t Capacity) { \
    (void)Host; (void)Data; \
    if (!R || R->Size < offsetof(mdo_registrar_v1, AddBuiltinTool) + sizeof(R->AddBuiltinTool) || !R->AddBuiltinTool) return MDO_RESULT_ERROR; \
    return R->AddBuiltinTool(R->Context, ToolId, Error, Capacity); \
} \
static const mdo_module_v1 MdoBuiltinModule = { \
    MDO_V1_HEADER(mdo_module_v1), .Id = "mdo.builtin." ToolId, .Name = ToolId, \
    .Description = "Host tool extension: " ToolId, .Version = "1.0.0", .Register = MdoBuiltinRegister \
}; \
MDO_EXPORT const mdo_module_v1* mdoModuleEntry(void) { return &MdoBuiltinModule; }

#endif
