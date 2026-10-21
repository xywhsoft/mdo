# MCP ecosystem implementation

The accepted work is split into independently verified commits on master:

1. Reliability: bounded cancellable request admission, tool-change invalidation,
   malformed-tool isolation, capability-based initialization, and actionable diagnostics.
2. Configuration: project/user scopes with explicit precedence and external-agent
   migration previews. Project definitions must not silently launch processes.
3. Authorization: OAuth discovery, PKCE, explicit browser launch, persistent token
   references, refresh and disconnect/revoke. Cover local and Android callback paths.
4. Context: resources/templates, prompts integrated with commands, server instructions,
   and typed content results with bounded persistence.
5. Interaction: elicitation, roots and progress, enabled only when implemented;
   task/sampling extensions remain demand-driven as proposed in the audit.

Implementation baseline: mdo 9138f577, locked xs 216f5445163c, xwork 3.11.0.
The current upstream xrt/xs master has changed its public library layout and API.
Keep the existing production SDK and apply source-hash-verified MCP compatibility
changes during SDK preparation; do not replace unrelated runtime/platform code.

Validation must use deterministic local peers rather than production accounts.
Keep credentials out of logs, exports, git and test artifacts. Network failure must
not replay a possibly completed state-changing tool call.

## Progress

- Initial audit: existing four MCP contracts and native runtime probe pass.
- Reliability changes use a source-hash-verified SDK overlay, preserving xs/xrt,
  ABI 8, protocol codecs and the existing regex fix. The source checkout stays
  untouched. SDK overlays are generated artifacts, not a new source branch.
- Verified native concurrent tool calls both complete; an already-cancelled
  waiter returns cancellation and a short-deadline waiter times out. A
  resources-only server initializes successfully. Invalid/duplicate definitions
  are excluded while a valid tool remains available.
- Project scope implemented with native `.mdo/mcp/<id>.json` definitions,
  per-Agent server restrictions inherited by subagents, project editor/import,
  revision guards, project lifecycle leases and cross-process writer locks.
  Disabled/invalid project definitions shadow personal definitions.
- Real HTTP/model-loop regression passes: cold save, stale-write rejection,
  project-only discovery, rejected personal-server schema load, successful
  project tool call, and disabled project configuration without personal fallback.
- External configuration preview/migration implemented for fixed Claude,
  Codex TOML, ZCode, OpenCode and `.agents` paths. Preview is read-only and
  omits credentials; explicit migration checks source revisions, rejects
  existing targets, seals credentials and preserves the original files.
  Real HTTP regression covers four providers, project and personal targets,
  TOML timeouts, stale-source rejection, collisions and cold imports.
- OAuth implemented with the SDK's first-party xoauth2 extension, private pinned
  core clock/deadline compatibility adapter, PRM/AS discovery, S256 PKCE, explicit
  native browser opening, preregistered/CIMD/DCR clients, native sealed persistence,
  restart-safe pending state, refresh preservation, one authenticated 401 retry,
  logout and asynchronous best-effort provider revocation. UI supports personal
  and project servers; no production authorization or real browser was exercised.
- Real local OAuth regression passes PKCE verifier/resource/scope, restart during
  login, callback replay rejection, unknown token expiry, restart after login,
  forced refresh, omitted replacement refresh token, transient 503 retention,
  reconnection, logout/revocation and plaintext-at-rest scan. Existing extension
  HTTP suite and strict C compilation also pass. Android hardware is unavailable;
  its callback path shares native code but still requires device acceptance.
- Context implemented: scoped `mcp_read`, resources/list, templates/list, read,
  prompts/list/get, completion/complete, explicit pagination, project/personal
  resource browser, `/mcp server prompt {string arguments}` composer expansion,
  capability checks and cold diagnostics with instructions/schema rejection reasons.
  User context stays ordinary input; it never becomes a system message.
- Typed MCP tool results use the host writer/artifact API. The first supported
  image is forwarded as an image; complete typed content is kept in a bounded
  JSON artifact when persistence is available. Binary data is excluded from text
  rendering. Concurrent waiters recheck connection/enabled state before dispatch.
- Local regressions pass all six context methods, pagination, forbidden tool-call
  method rejection, diagnostics, typed PNG/structured content, binary exclusion,
  real model tool_search/load/call, scope inheritance and OAuth regression.
  Strict C compilation and JavaScript module graph pass.
- Authorization diagnostics now retain real HTTP status only (an unconfigured
  login has no synthetic HTTP 401). Cancel/deadline conditions keep their own
  classifications; successful refresh clears stale temporary errors. Replacing
  pending login state removes its previous index; public clients never transmit
  an optional client secret when their auth method is `none`.
