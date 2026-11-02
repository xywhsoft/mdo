# 2026-11-02 dead-code cleanup

## Scope and API boundary

Clean production-unreachable helpers and retired UI implementations without
changing release identity, publishing, or modifying user Home data.

The initial reference audit undercounted generated native SDK bindings.
`tools/application_sdk.py::native_symbols` exports declared product functions
from `app/include/mdo` to external C extensions. In particular, the legacy
JSON memory mutation/directory-transfer API and `MdoMcpValidateText` remain
public capabilities. Retain those implementations and their regression tests;
absence of an internal call is not sufficient evidence to remove an SDK API.
Legacy memory reading, first-use Markdown conversion, ZIP verification,
provider strict-schema checks and dynamic/native callbacks also remain.

## Stage 1: unreachable helpers and old frontend formats

- Remove unused private C helpers and three declarations for removed MCP API
  routes. Move the ZIP fixture's convenience iterator out of the shipped header.
- Reuse the strict-schema compatibility predicate in the real request-body hook,
  replacing its test-only JSON wrapper and duplicated root checks.
- Remove the unused single-JSON MCP importer, command-text expansion, Base64
  attachment renderer and unwired template/diff helpers. Keep directory ZIP
  distribution, source Markdown rendering and connection-input selection.
- Remove unused frontend exports/imports and tests for retired behavior. Keep
  coverage for the actual forms, connection inputs, store paging, URL safety,
  source rendering and request serialization.

Validation:

- Targeted Node suite: 21 passed, 1 optional website integration check skipped
  (`MDO_ECOSYSTEM_SITE_ROOT` not configured).
- Frontend module graph: 247 modules parsed and reachable.
- Native discovered-model request probe: Chat/Responses/Anthropic serialization,
  caller ownership, strict-schema policy and capability boundaries passed.
- Existing locked SDK overlay reused; no repository/runtime copy or deployment.

## Stage 2: remove unused Markdown editor metadata

- The HTTP editor now accepts exactly `id` and `content`. Remove unused
  `title`/`tags`/`pinned` request validation, old options construction, hidden
  form fields, tag validation, checkbox styling and their language strings.
  Markdown/frontmatter content, scope checks, byte limits and ETags are unchanged.
- Update API fixtures and the keyboard fixture to match the real file editor.
  The native structured-memory SDK contract is unchanged.

Validation:

- Real HTTP integration on the current source and existing XS host: global and
  project Unicode Markdown round trips, missing/stale preconditions, malformed
  fields, path/size rejection and deletion passed.
- Memory browser: 4 tests passed; memory native/API static contracts: 4 passed.
- Shared native ZIP codec: 44 adversarial fixtures and binary writer round trip
  passed, including its test-local validation iterator.
- Only an owned Home fixture under `.build/dead-code-audit-20261102` was used;
  the temporary service/configuration was stopped/removed after the checks.

## Stage 3: retired styles and final integration

- Remove 7,832 UTF-8 bytes of old tool-package rows, store cards/dialogs,
  editor layouts and model-candidate list CSS. Preserve the command-result
  dialog's still-used `.extension-editor` and dynamically generated
  `.supplier-view-*` rules, current shared layers, tabs and recommendation UI.
- Align generated Application SDK `MemoryInput` with the two-field HTTP
  editor. Verify both global/project writes through the actual SDK broker,
  while retaining the native legacy-memory API export coverage.
- Remove the obsolete optional assertion comparing the app renderer with the
  retired website `ecosystem/markdown.js` (the file and its imports no longer
  exist in the current website). Keep the local renderer's safety tests.

Final validation:

- Full front-end suite, 203 files: 1,286 passed, zero failures; two optional
  checks were skipped at that point. After removing the obsolete website
  assertion, the remaining three source-renderer tests passed separately.
- Static native/API contracts: 127 passed. Frontend graph: 247 modules reached.
- Real MCP directory/private-connection and automatic-activation probes passed:
  handshakes, reconnect, bounded background retry, credential boundaries,
  private overrides, approval changes, process retirement and uninstall.
- Current `.build/host` and SDK source caches disappeared during this run.
  Do not recreate large SDK/worktree copies: reuse the existing `mdo.exe`
  host, whose whole-file hash matches its prior product receipt. Load current
  source with `--no-vfs`, then repack the current app using that same host.
- Packed startup, current web assets, and the real HTTP/SDK memory round trips
  passed. The temporary headless configuration was restored byte-for-byte;
  all owned acceptance processes were stopped.
- Normal Windows acceptance package:
  `.build/dead-code-audit-20261102/mdo-cleanup.exe`, 6,625,223 bytes,
  SHA-256 `0df53a3992003850b3adbb15fc71b28dccfbc6ebc3715140ad5e10d5095a9c23`.
  Its host prefix still matches SHA-256
  `74bf58a745978d8780112792f77c3822a913e5eeeaa08a890c3ecaf416a39a8c`.
- Product code/resources net reduction: 405 lines relative to `87d5ebbc`.
  Release remains 0.8.4. Root `mdo.exe`, user data, other workspaces and online
  downloads/updates were not replaced or published.
