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
- Remaining goals (OAuth, context/interaction features and expanded
  diagnostics) are still in progress. Unsupported migration forms are reported.
