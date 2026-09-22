# Built-in modules

Bundled Agent, Subagent, and Tool module sources live in the corresponding
subdirectories. Every `.c` file is one independently compiled module and uses
`#include "mdo/module.h"`. External sources with the same logical path override
the bundled source through `mdo-home`.
