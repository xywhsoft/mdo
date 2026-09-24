# Built-in modules

Bundled Agent, Subagent, and Tool module sources live in the corresponding
subdirectories. Every `.c` file is one independently compiled module and uses
`#include "mdo/module.h"`. External sources with the same logical path override
the bundled source through `mdo-home`.

`tools/builtin_todo.c` publishes the compact `mdo.todo` plan tool. Its result
is a complete plan snapshot that the session event bridge validates and stores
for the conversation dock; it does not access workspace files.
