# mdo application modules

Each directory owns one product boundary. Implementations use ordinary `.c`
and `.h` files; `tools/build_mdo.py` generates the single TCC translation unit
from `app/sources.json`.

Add a module directory when its first implementation lands. Keep each `.c`
implementation in the source manifest or reachable through a quoted include;
the build rejects orphan implementations and private headers. Do not leave
empty placeholder directories or parallel copies of retired implementations.

Runtime-loaded agents and tools live in `default-home/modules/` and use the
versioned SDK in the repository's `include/mdo/`. Test-only modules belong in
`tests/fixtures/` and are injected into disposable test sites.
