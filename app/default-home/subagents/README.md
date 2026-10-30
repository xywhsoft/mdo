# SubAgents

The bundled general-purpose assistant lives in `general/`. Its logical ID is
`subagent:general`; the `agent` tool calls it as `subagent.general`.
It follows the parent task's model and authorized capabilities, supports
background tasks, and returns a concise report. Nested delegation is disabled
by default. It adds no separate task timeout beyond the parent's deadline.

Manage SubAgents in Settings → Extension capabilities → SubAgent. Each extension
has its own directory containing `extension.json` and an `agent.md`, JSON or C
entry. Configurable roles use `mdo.agent.v1`; `tools: inherit` follows the
authorized tool catalogue. A v1 empty tool array means no tools.

The ordinary bundled-extension installer preserves explicit disable/uninstall
choices and local edits. Upgrading does not silently restore those choices.
