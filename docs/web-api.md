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
  `/artifacts`, `/approvals`, `/permissions`, `/diagnostics`, `/storage`, and
  `/operations`;
- `/projects/{project}/sessions/{session}`, `/runs/{run}`,
  `/schedules/{schedule}`, `/tasks/{task}`, `/operations/{operation}`, and
  `/projects/{project}/sessions/{session}/recovery`.

Mutations:

- preview, replace, or restore a settings domain;
- reload model, Skill, module, and MCP catalogs;
- enable, disconnect, or refresh an MCP server;
- create, edit, archive, trash, and restore sessions;
- fork, truncate, clear, export, and resume durable sessions;
- start and cancel interactive Agent runs, and resolve one-shot approval
  requests;
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

## Approvals and interrupted-run recovery

`GET /approvals` returns the current bounded set of unresolved permission
requests. Each item contains its one-shot approval ID, tool and call identity,
risk, effects, resources, arguments, workspace, and deadline. A caller resolves
one item with `PUT /approvals/{approval}` and exactly one decision: `allow` or
`deny`. A decision is consumed once; a stale or already-resolved ID is not
silently applied to a later request.

`GET /projects/{project}/sessions/{session}/recovery` inspects a durable idle
session. It returns:

- `resume_required`, which is true for a pending tool batch or an interrupted
  model continuation;
- a stable `recovery_token` for the exact current recovery state;
- at most 32 pending calls, each with the original call ID, tool, arguments,
  effects, tool availability, and whether automatic retry would be safe;
- the session revision and the current diagnostic catalog generation.

The recovery view limits each argument document to 16 KiB and the whole batch
to 128 KiB. It fails closed when the durable state cannot be represented within
those limits. Catalog generation is diagnostic only: a reopened equivalent
catalog may receive a new process-local generation, while the token is derived
from the durable call identities, arguments, effects, availability, retry
safety, and continuation state.

`POST /projects/{project}/sessions/{session}/resume` accepts the current token
and one explicit decision for every pending call:

```json
{
  "recovery_token": "64 lowercase hexadecimal characters",
  "decisions": [
    {"tool_call_id": "call-id", "action": "record_uncertain"}
  ]
}
```

`retry` executes the call again through normal permission and hook processing
and therefore has at-least-once semantics. `record_uncertain` does not execute
the tool; it records an uncertainty result so the model can continue. An
unavailable tool cannot be retried. A model-only interruption has an empty
decision array. The server validates the token again on the exact reopened
Agent immediately before starting the run. A changed state returns
`409 recovery_state_conflict`; invalid, duplicate, missing, or extra decisions
return `422 recovery_resume_invalid`. Success returns `202` with the ordinary
run resource and `resume: true`, so clients monitor it through `/runs/{run}` and
the existing event replay routes.

## Secret boundary

Catalog and diagnostic responses expose whether credentials are configured,
but never return secret references, environment values, HTTP authorization
headers, raw keys, or module environment entries. Errors do not echo request
bodies or sensitive tool arguments.
