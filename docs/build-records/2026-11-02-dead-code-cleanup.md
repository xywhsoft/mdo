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
