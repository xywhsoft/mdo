# mdo application modules

Each directory owns one product boundary. Implementations use ordinary `.c`
and `.h` files; `tools/build_mdo.py` generates the single TCC translation unit
from `app/sources.json`.

Reserved module roots are `bootstrap`, `config`, `storage`, `model`, `runtime`,
`module`, `skill`, `mcp`, `session`, `schedule`, `api`, and `diagnostics`.
Add a root when its first implementation lands instead of keeping empty
placeholder directories.
