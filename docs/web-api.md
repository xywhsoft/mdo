# mdo Web API v1

The browser UI uses the same `/api/v1` interface in development and packed
executables. API routes are owned by mdo; xs provides the HTTP host and static
application files.

## Response contract

Every JSON response contains `schema_version`, `ok`, and `request_id`.
Successful responses place the resource under `data`. Failed responses place a
stable machine-readable `code` and a user-safe `message` under `error`.

Responses are limited to 256 KiB and use `Cache-Control: no-store`,
`X-Content-Type-Options: nosniff`, and `Referrer-Policy: no-referrer`. `HEAD`
returns the same status and headers as `GET` without a body. `OPTIONS` reports
the exact route methods through both `Allow` and the response document.

JSON mutations accept only `application/json`, reject duplicate or unknown
fields where the resource schema is closed, and limit decoded request bodies to
256 KiB. Revisioned resources use strong ETags. A write without `If-Match`
returns `428`; a well-formed stale ETag returns `412`.

## Resource routes

Read snapshots:

- `/bootstrap`, `/settings`, `/models`, `/agents`, `/modules`, `/skills`,
  `/mcp`, `/projects`, `/sessions`, `/runs`, `/schedules`, `/tasks`,
  `/artifacts`, `/permissions`, `/diagnostics`, `/storage`, and `/operations`;
- `/projects/{project}/sessions/{session}`, `/runs/{run}`,
  `/schedules/{schedule}`, `/tasks/{task}`, and `/operations/{operation}`.

Mutations:

- preview, replace, or restore a settings domain;
- reload model, Skill, module, and MCP catalogs;
- enable, disconnect, or refresh an MCP server;
- create, edit, archive, trash, and restore sessions;
- start and cancel interactive Agent runs;
- create, replace, enable, disable, and remove schedules;
- cancel a process, Subagent, or scheduled task through its unified task ID.

Module reload and MCP refresh use retained operation IDs. The caller polls the
operation resource and can request cancellation without depending on an HTTP
connection remaining open.

## Settings transactions

`GET /settings` returns the effective, typed UI settings and the current
configuration ETag. It exposes credential status as a boolean; secret
references and resolved values are omitted.

The configuration domains are `settings`, `models`, and `permissions`. Each
write document uses `{"schema_version":1,"patch":{...}}`:

- `POST /settings/{domain}/preview` validates a complete replacement patch;
- `PATCH /settings/{domain}/preview` recursively merges the supplied object
  into the current user patch, then validates the resulting document;
- `PUT /settings/{domain}` applies a complete replacement patch;
- `PATCH /settings/{domain}` applies the same server-side recursive merge;
- `DELETE /settings/{domain}` restores the built-in domain defaults and does
  not accept a request body.

Object members merge recursively. Arrays and scalar values replace the current
value. Preview is read-only and reports `changes`, `patch_bytes`, and
`current_revision`. PUT, PATCH, and DELETE require the current configuration
ETag in `If-Match`; successful mutations return the new revision and ETag.
Unknown patch fields remain round-trippable on the server, so a browser editing
safe typed fields cannot erase newer settings or secret references it did not
receive.

## Event replay

`/events?after={event-id}&limit={count}` replays the bounded process event
window. `/projects/{project}/sessions/{session}/events` replays the durable
session UI journal. Every event identifies its schema, time, kind, terminal
state, and available session/run/task lineage. Responses return a next cursor
and `history_lost` when the requested prefix is no longer retained.

The v1 transport deliberately uses bounded pull replay instead of holding an
SSE connection. This gives desktop, mobile, reload, and suspended-webview
clients the same recovery rule and creates no per-client server queue. The UI
fetches a resource snapshot, replays after its last cursor, and backs off while
the cursor is unchanged. Unknown event kinds remain ordered records and must
not stop replay.

## Unified task output

`/tasks/{task}/output` accepts independent `stdout`, `stderr`, and `result`
absolute byte offsets plus a shared `limit`. The default limit is 16 KiB and
the maximum is 64 KiB. Each stream reports `start`, `next`, `bytes`, and
`dropped`. `complete` describes the whole task.

Process output is arbitrary bytes, so all three channels use canonical Base64.
The UI decodes `data` only after checking the top-level `encoding` value. A
dropped cursor is explicit; clients never infer continuity from the returned
length.

`/tasks/{task}/events?after={revision}&limit={count}` replays the task-local
state window. The maximum page is 64 events. Cancellation through
`DELETE /tasks/{task}` is idempotent and returns the current task snapshot, including
its new revision and ETag.

## Secret boundary

Catalog and diagnostic responses expose whether credentials are configured,
but never return secret references, environment values, HTTP authorization
headers, raw keys, or module environment entries. Errors do not echo request
bodies or sensitive tool arguments.
