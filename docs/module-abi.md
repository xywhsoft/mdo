# mdo Module ABI v1

Each C source below `tools`, `modules/tools`, `modules/agents`, or `modules/subagents` is
one independently compiled module. It includes `mdo/module.h` and exports the
fixed `mdoModuleEntry` function. The entry returns a static `mdo_module_v1`.

The user-facing local tool editor manages `tools/<id>.c`; legacy module paths
continue to work. Built-in tool IDs cannot be replaced by managed C tools.
Ordinary Agent/SubAgent Markdown profiles use the same descriptor catalog and
runtime. Main profiles can optionally use a same-ID C Agent for their base
prompt/lifecycle while retaining the UI tool scope. See [local tools](local-tools.md).

Every descriptor and host table starts with `Size` and `AbiVersion`. A module
sets them with `MDO_V1_HEADER(type)`. The host copies every descriptor string
and string array while `Register` is running. Module memory is never returned
for the host to free: tool text, images, permission resources, and errors cross
the boundary through host-owned writers or caller-owned buffers.

Modules are trusted in-process code and can include the public xs/xrt headers.
The Application SDK is supplied through `Host->Application`; include
`mdo/sdk.h` for typed values, service discovery and application calls.
`Registrar->AddService` and `AddHook` register generation-owned providers and
hooks. Registration can query application metadata without retaining the
module-manager mutex. Native code is not a sandbox; isolated command workers
use the same service contract through RPC.

The complete [Application SDK guide](application-sdk.md) covers typed native
product APIs, custom Agent drivers, operations, streams, UI/VFS plugins and
recovery. Native headers are generated as `mdo/native/*.h`. `Stop` stops a
module's own producers at retirement/shutdown; `Unregister` frees shared state
only after the final generation reference is released. Neither runs under the
module-manager mutex. Replacing an application service requires the explicit
`MDO_SERVICE_REPLACE_BUILTIN` flag; competing providers reject the candidate.

Tool effects are authoritative. A mutating tool either supplies a side-effect
free `DescribePermissions` callback, or declares one constant
`PermissionResource` when it has exactly one non-read effect. Multiple
non-read effects require the callback. Result writers copy input before they
return and are valid only during the callback.

Agent and Subagent definitions share `mdo_agent_v1`. The directory sets the
maximum role: `agents` may register main Agents and Subagents;
`subagents` may register only Subagents. Module unload waits until every
catalog, active call, and other owner reference to its generation is released.
