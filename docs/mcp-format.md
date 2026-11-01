# MCP runtime configuration format

MCP extensions are ordinary directory ZIPs. The public recipe and bundled files
live in `mdo-home/mcp/<directory-id>/`; `extension.json` selects `mcp.json` in
`mdo.mcp-template.v1` format. This document describes the rendered internal
runtime config, not a second installation or publication format. See
[MCP service recipes](mcp-service-templates.md).

Trusted, enabled recipes derive one stable default connection. User overrides,
project scope and credential references live separately in
`mdo-home/data/mcp/connections/<connection-id>.json` and never enter the ZIP.
Existing private connections suppress automatic creation, including disabled
connections. Changing an approved directory never falls back to an unapproved
program or silently selects an advanced grant.

Directory discovery, read-only listing and preview do not start a process or make a network request.
After a successful catalogue publication, a host worker connects enabled,
approved services and discovers their tools. Missing inputs and authorization
are actionable states rather than installation failures.

## Schema version 1

```json
{
  "schema_version": 1,
  "id": "example",
  "name": "Example tools",
  "description": "Tools provided by the Example MCP server.",
  "enabled": false,
  "transport": {
    "type": "stdio",
    "program": "example-mcp",
    "arguments": ["--stdio"],
    "working_directory": null,
    "inherit_environment": true,
    "environment": [
      {"name": "EXAMPLE_TOKEN", "secret_ref": "env:EXAMPLE_TOKEN"}
    ]
  },
  "protocol_version": "2026-07-28",
  "startup_timeout_ms": 10000,
  "request_timeout_ms": 30000,
  "limits": {"message_bytes": 4194304, "tools": 256},
  "tools": {"allow": [], "deny": []},
  "security": {
    "default_effects": ["external-service"],
    "permission_profile": "balanced",
    "trust_read_only_annotations": false
  },
  "auto_reconnect": true
}
```

Unknown keys, duplicate entries, embedded NUL bytes, invalid UTF-8, unsafe
sizes, and protocol revisions other than `2026-07-28` or `2025-11-25` are
rejected. `allow` is evaluated before `deny`; an empty `allow` list permits all
remote names that are not denied. Effects are selected from `read`,
`workspace-write`, `process`, `network`, `external-service`, `secrets`,
`schedule`, and `agent-delegation`.

The private connection editor renders the selected recipe variant with device
values and credential references. Default variants use a platform-specific
`default: true` marker, or the first matching unmarked declaration. A variant
marked `default: false` requires an explicit advanced choice. The public recipe
is never copied into private connection JSON.

Schema v1 implements `stdio` and `streamable-http`; `streamable-http` is never silently treated as stdio,
and stdio is never treated as HTTP. The HTTP shape is
`{"type":"streamable-http","endpoint":"https://...","headers":[{"name":"Authorization","secret_ref":"env:API_TOKEN"}]}`.
Endpoints must use HTTPS. Header names use the HTTP token grammar, are unique
without regard to ASCII case, and header values are secret references only;
transport-owned headers (`Host`, framing headers, `Accept`, `Content-Type`, and
the `Mcp-*` namespace) are rejected. Header values containing control bytes are
rejected after secret resolution. xwork synchronously deep-copies accepted
headers, then MDO clears the resolved values from its candidate catalog.

`working_directory` is either null, an absolute platform path, or a portable
`mdo-home:<relative-path>` reference. Relative components use `/`, remain below
the selected root, and contain no empty, `.` or `..` segment.

Environment entries contain a name and exactly one of `secret_ref` or
`remove:true`. Private credential references use the supported `env:`, `file:`
and device `vault:` resolvers. Public recipes only declare `input:<id>` references;
they cannot contain device credential references or secret defaults.
The manager resolves them only for runtime registration, never returns their
values through catalog or status APIs, and never writes resolved values back to
the configuration. Missing or unavailable secrets produce a diagnostic and do
not start the server.

## Project scope

Connections have an optional private `project_id`. Global connections are
available in allowed projects, while project-specific connections match only
that workspace. Roles can further restrict the MCP selection; subagent
selections and live tool authorization retain their scope checks.

An explicit dependency binding wins. Otherwise a matching project connection
wins over the global default, followed by the unique eligible connection.
Ambiguous accounts require an explicit choice. A disabled private connection
never gets replaced with a fresh automatic connection.

## Runtime behavior

Catalogue publication is atomic and does not wait for network initialization.
A single cancellable host worker initializes enabled services and caches tool
discovery. Transient IO, timeout and HTTP 408/429/5xx failures receive at most
five attempts within a shared deadline of at most 60 seconds. It does not
replay tool calls. Missing inputs and OAuth stop at a user-action state; the
worker never opens a login browser. Completing OAuth schedules discovery again.

HTTP headers are resolved only for runtime requests and are absent from public
catalogue/status data. The manager never returns their resolved
values through catalog or status APIs. Ordinary chat does not require optional
MCP services to be ready. The next send or new delegation in an existing chat
uses the current catalogue without a new chat or application restart.

Agent prompts initially receive the compact `tools`, `load` and `fetch`
discovery/read tools. A selected remote tool publishes its schema in the next
Agent catalogue generation. See [automatic activation](mcp-automatic-activation.md).

Reload builds a complete candidate first. Runtime registration and the public
catalog change as one transaction; failure preserves the previous generation
and publishes diagnostics. Disconnect keeps the definition registered and
enabled while closing its current transport. Disable also closes the transport.
Schema generation, expiry, negotiated protocol, connection state, discovered
tool count, and request count are live status rather than immutable catalog
metadata.

The parser accepts at most 128 server files. Each document is at most 256 KiB;
arguments, environment entries, HTTP headers, allow entries, and deny entries
are each limited to 256. General strings are at most 4096 bytes and descriptions
are single-line text of at most 2048 bytes. A server can advertise at most 4096
tools, messages are limited to 16 MiB, and startup/request timeouts are 100 to
300000 milliseconds. Diagnostics are bounded to 256 entries.

`MdoMcpManagerInit` and `MdoMcpManagerUnit` run in the serialized process
lifecycle. Reload and operational calls may run concurrently after init.
Catalog and diagnostic snapshots are immutable, reference counted, and remain
valid until released even when a reload publishes a later generation.

MDO accepts `2026-07-28` and `2025-11-25` as separate wire codecs. It does not
silently downgrade one to the other. The modern codec uses server-provided
cache metadata; cache records are bounded runtime state and contain no secret
material.

## Browser authorization

Streamable HTTP servers can set `oauth: true` or an object with `enabled`,
`client_id`, `client_metadata_url`, `client_secret_ref`, `scope`,
`resource_metadata_url`, `authorization_server`, `redirect_uri`, and `auth_method`.
Only `none`, `client_secret_basic`, and `client_secret_post` are supported.
A static Authorization header cannot be combined with OAuth. Stdio credentials
continue to use environment references.

Settings > MCP > Authorize discovers protected-resource/authorization metadata,
requires S256 PKCE, and prepares an explicit browser action on the owning device.
Pre-registered clients and metadata URLs are supported, with dynamic registration
as fallback. Default callbacks use the current loopback app port; a configured
redirect URI must have been registered with the provider. Android uses its local
server callback. Pending PKCE material survives restart for ten minutes.

OAuth requires the host's native credential protection. The pinned Windows and
Android hosts provide it; the current Linux hosts do not. On unsupported hosts,
status reports `available: false` and `state: unsupported`, and starting login
returns `credential_protection_unavailable` before discovery or token exchange.
The UI disables login and browser actions. No plaintext fallback is used.

Credentials/tokens are sealed by the native platform credential service under
`data/mcp/oauth`; they never enter configuration exports, browser storage, or
status responses. A missing token expiry remains unknown. Refresh preserves an
omitted refresh token and retains saved credentials on transient errors. An
authenticated HTTP 401 permits one refresh/retry; transport errors never replay a
tool request. Forget removes local credentials first, disconnects the MCP session,
and attempts advertised provider revocation in the background. Revocation failure
does not restore local credentials.

Reference: https://modelcontextprotocol.io/specification/2025-11-25/basic/authorization

## Resources and prompts

`mcp_read` exposes only resources/list, resources/templates/list, resources/read,
prompts/list, prompts/get and completion/complete. It inherits the Agent's MCP
scope and external-service permission. Lists return nextCursor without automatically
walking unbounded pages. Server capabilities are checked before dispatch.

Settings > MCP > Resources, prompts and diagnostics opens the user-driven browser.
Templates display the URI template and accept a concrete URI. Prompt fields accept
string arguments. `/mcp server prompt {"argument":"value"}` expands a prompt into
the composer for review; a matching project definition shadows the personal server.
The full result is bounded, asynchronous, cancellable and kept in recent operation
memory. Binary blocks are not automatically fetched by URI. Agent tool results
persist full typed JSON through the normal artifact store when enabled, forward
one supported image to the model, and exclude binary data from text previews.

## Client requests and progress

The 2025-11-25 client advertises roots and form elicitation. Both stdio and
Streamable HTTP dispatch ping, roots/list and elicitation/create while an outgoing
request is active. HTTP SSE bytes are consumed on the ordinary request thread;
network callbacks only copy bounded chunks. A server request may reuse the
numeric ID of the outgoing client request without being mistaken for its result.

Roots contain only the active operation's project directory. With no associated
Agent context (for example Settings connection tests), the list is empty. Roots
are advisory context, not a filesystem permission grant. Form requests use the
current conversation's ask entry: users explicitly answer a JSON object or
decline/cancel. Primitive types, required keys, enum and numeric/string bounds
are checked; unsupported input or no active user-question tool produces cancel.
URL elicitation, sampling and task execution are not advertised or executed.
Unknown client methods receive -32601 without failing the original call.

Legacy requests include an operation-local progress token. Only matching,
nondecreasing, finite progress is accepted; stale tokens are ignored. The
cold diagnostics endpoint can read progress even while the server is busy.
Resource-browser reads display it while waiting. Request bytes, SSE frames,
client requests, waiters and deadlines remain bounded. HTTP cancellation/timeout
closes the active call; a completed write is never replayed as a network retry.

## Directory programs and writable data

Directory MCP programs use `data/mcp/runtime/<connection-id>` as their default
working directory (including `working_directory: "."`). Read distributed assets
relative to `MDO_EXTENSION_ROOT`, and write connection state relative to
`MDO_EXTENSION_DATA`. An explicit directory such as `{$path}` can still be used
when a program requires it; the program must then keep generated files out of
its published directory.

The host supplies and reserves `MDO_EXTENSION_ROOT`, `MDO_EXTENSION_ID`,
`MDO_EXTENSION_DIGEST`, `MDO_EXTENSION_DATA`, `PYTHONPYCACHEPREFIX`,
`XDG_CACHE_HOME`, `XDG_DATA_HOME`, `TMPDIR`, `TMP` and `TEMP`. Python caches are
scoped to the approved directory digest, outside the distributed files. The
entire published directory remains subject to integrity/approval checks; cache
files are not simply exempted from verification. Preview does not create a
runtime directory or start a program. Private connection data remains local and
is not included in ZIP exports or removed with the published extension files.
