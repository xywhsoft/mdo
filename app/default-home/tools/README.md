# Local C tools

Place portable tool sources at `mdo-home/tools/<id>.c`. The Settings → Extended
capabilities → Tools page can create, import, edit, export and enable them.

Each C file implements `mdoModuleEntry()` from `mdo/module.h`, then registers one
or more tool descriptors with `Registrar->AddTool`. The Execute callback receives
JSON arguments and a result writer. mdo compiles, validates and registers the
source through TCC; no external compiler is needed. Built-in IDs are reserved.

C extensions run in the host process. Review their source before importing.
See `docs/local-tools.md` in the project for the callback and permission contract.
