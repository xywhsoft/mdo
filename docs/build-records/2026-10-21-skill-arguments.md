# Skill parameter support — 2026-10-21

## Goal 1: shared runtime semantics

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

Goal 2 covers model/UI guidance and end-to-end invocation regression.
