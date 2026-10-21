# Skill support completion — 2026-10-21

Five goals were implemented on `master`, with a separate commit for each goal:

1. `1e894818`: model metadata discovery, paginated search, explicit `$skill` and `/skill:` selection.
2. `402c224e`: consistent document/resource bounds, real native resource directories, UTF-8 paging.
3. `0d86e036`: invocation controls, literal argument/directory substitutions, unsupported-field diagnostics.
4. `c014f9a8`: isolated project discovery, precedence, enable flags and package-qualified names.
5. This change: assisted authoring, compatible-agent source migration and project Skill management.

The Skill manager exposes **来源、项目与 AI 创建**. AI authoring creates a new
conversation and saves an unsent draft. Migration copies one reviewed source to
an unused personal/project ID, validates all staged resources, and preserves the
source. Project editors use content revisions, lifecycle leases and writer locks;
removal preserves files in `.mdo/skill-trash`. Personal package ownership remains
with the existing ecosystem manager.

Supported compatibility is explicit: manual/model invocation controls,
`allowed-tools` declarations, metadata, argument hints and literal variables.
Foreign runtime behavior such as `context: fork`, hooks and model overrides is
diagnosed and rejected instead of silently ignored. Client plugins remain a
separate ecosystem category, outside this Skill implementation.

## Verification

- Strict C11 compilation: `-Wall -Wextra -Werror -fsyntax-only` passed using the pinned SDK.
- Python unit/contract suite: 152 tests, 3 skipped, zero failures.
- Full frontend suite: 1,029 tests, 1,028 passed, 1 skipped.
- Frontend graph: 171 modules parsed and reachable.
- Native xs/TCC probes passed: Skill generations, Skill feature behavior,
  extension HTTP, Agent ownership, C modules and full API runtime.
- Migration tests copied a 750 KB resource into both personal and project scope,
  preserved the source and rejected existing destinations and invalid IDs.
- Project tests covered revision conflicts, independent enablement, invalid-resource
  rollback, recoverable removal, invalid shadows and cross-project isolation.
- Boundary tests covered a near-128 KiB quote-heavy document and JSON response
  escaping, binary/UTF-8 resource handling, literal variables and oversized inline
  drafts. The composer retains its 64 KiB limit; oversized inline expansion fails
  before replacing an existing draft. Large instructions remain available through
  the model Skill tool.

Tests used disposable Homes, offline model fixtures and the local pinned host.
The UI workbench was exercised through a DOM adapter; Android hardware and packaged
desktop visual acceptance were not part of this verification. No live model calls,
production data changes or online deployment were performed.
