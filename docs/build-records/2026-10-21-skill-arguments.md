# Skill parameter support — 2026-10-21

## Goal 1: shared runtime semantics

Committed as `d1467412` on `master`.

Manual selection and model calls share the same bounded literal renderer. It
supports raw `$ARGUMENTS`, zero-based `$ARGUMENTS[N]` / `$N`, and optional named
positional aliases from `arguments`. Names are validated when the Skill is loaded;
no required/type/enum schema is imposed on task input. Missing named positions
become empty and missing indexed positions remain literal. Unfinished input quotes
consume the remainder rather than failing a conversation. Unused raw input is
appended when no placeholder receives it.

Input is limited to 16 KiB; rendered output stays below 2 MiB and retains the
existing downstream JSON/draft bounds. Substitution never executes shell syntax
or recursively interprets values. Catalog snapshots own argument names until the
snapshot is released, including across refresh/removal.

Verification: strict C11 unity syntax compilation with `-Wall -Wextra -Werror`
using the pinned SDK passed. Native xs/TCC parameter probes passed 24 expansion
cases, 16 declaration cases, maximum input/output bounds, large position indexes,
and snapshot ownership. Existing Skill generation, resource and feature probes
and the three Skill contract unit tests passed.

## Goal 2: discoverable parameters and invocation regression

Model discovery/load responses expose the parameter hint and named positions;
loaded results also preserve raw input. The bounded prompt index includes hints.
The existing chat picker displays the hint without requiring arguments. AI Skill
authoring now explains all supported substitutions, optional arguments, literal
handling and script/tool validation. Expansion failures retain their actionable
message in the model tool response.

HTTP regression passes identical quoted Unicode/Windows-path input through manual
and model entry points and compares the resulting instructions byte for byte.
It also checks optional/missing positions, literal dollar/directory text, invalid
input rejection, duplicate-name diagnostics, discovery hints, and rollback of
invalid Skill creation. The mock model completes without final model errors.
Frontend checks preserve raw quotes/empty positions through the API, retain
optional hints and keep oversized drafts protected. Native declaration regression
now additionally covers empty declarations and the maximum 128 names.
Malformed block mappings are rejected with an arguments-specific diagnostic,
instead of being silently treated as an empty declaration.

Final verification:

- Strict C11 unity syntax compilation with `-Wall -Wextra -Werror`: passed.
- Native parameter probe: 24 expansion cases, 20 declaration cases, boundary and
  snapshot lifetime checks passed.
- Existing native Skill generation/resource and feature probes: passed. One
  feature probe encountered a transient Windows file-sharing error during temp
  cleanup; its standalone repeat passed.
- Extension HTTP probe: passed, including identical manual/model instructions,
  metadata discovery, malformed declaration rollback and zero final model errors.
- Full API runtime probe: passed.
- Python unit/contract suite: 152 tests, 3 skipped, zero failures.
- Full frontend suite: 1,030 tests, 1,029 passed, 1 skipped, zero failures.

Local verification evidence is under `.build/skills-validation-20261021/` in
`arguments-http.json`, `arguments-unit.log` and `arguments-frontend.log`.

All tests use disposable Homes and local offline fixtures. No live model calls,
online deployment or Android device acceptance is part of this change.
