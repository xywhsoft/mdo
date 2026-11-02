# Built-in modules

Bundled Agent, Subagent, and Tool module sources live in the corresponding
subdirectories. Every `.c` file is one independently compiled module and uses
`#include "mdo/module.h"`. External sources with the same logical path override
the bundled source through `mdo-home`.

All 22 bundled tools, including `todo`, are ordinary C extensions under
`tools/<name>/` in the default Home. They use the same directory installation,
identity, trust and dependency-plan lifecycle as other extensions. Explicit
disable/uninstall choices survive restarts and upgrades.

`todo/tool.c` demonstrates a self-contained `AddTool` implementation. The other
tools use `mdo/builtin_tool.h` to bind existing host implementations, retaining
their real schemas and permission checks. A disabled or removed directory has
no hidden host-tool fallback. This applies to main Agents, SubAgents and model
verification. Legacy `mdo.todo` event records remain readable.
