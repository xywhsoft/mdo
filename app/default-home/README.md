# Built-in home

This tree is the read-only fallback mounted from `mdo.exe`. An external
`mdo-home` overlays it when present. Runtime writes never modify these files.

Only product agents, tools and skills belong here. Module ABI examples and
transport fixtures live under `tests/fixtures/`; test helpers inject them into
temporary sites without publishing them to production model contexts.
