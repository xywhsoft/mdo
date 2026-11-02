# Built-in modules

Bundled Agent, Subagent, and Tool module sources live in the corresponding
subdirectories. Every `.c` file is one independently compiled module and uses
`#include "mdo/module.h"`. External sources with the same logical path override
the bundled source through `mdo-home`.

The conversation plan tool is bundled as the independent C Agent tool
`tools/todo/` in the default Home. Its model-call ID is `todo`. It is seeded
through the same extension-directory installation mechanism as bundled roles
and commands; explicit disable/uninstall choices survive upgrades. Legacy
`mdo.todo` event records remain readable. Other built-in tools keep their
existing implementations.
