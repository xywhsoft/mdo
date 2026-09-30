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
  `/schedules/{schedule}`, `/schedules/{schedule}/history`,
  `/tasks/{task}`, `/operations/{operation}`, and
  `/projects/{project}/sessions/{session}/recovery` and
  `/projects/{project}/sessions/{session}/todo` and
  `/projects/{project}/sessions/{session}/asks` and
  `/projects/{project}/sessions/{session}/workspace/files?q={query}`.

Mutations:

- preview, replace, or restore a settings domain;
- reload model, Skill, module, and MCP catalogs;
- enable, disconnect, or refresh an MCP server;
- create, edit, archive, trash, and restore sessions;
- add a persistent project with its workspace and optional default model;
- fork, truncate, clear, export, and resume durable sessions;
- start and cancel interactive Agent runs, resolve one-shot approval requests,
  and answer session-bound user questions;
- create, replace, enable, disable, remove, and explicitly run schedules;
- cancel a process, Subagent, or scheduled task through its unified task ID.

Module reload and MCP refresh use retained operation IDs. The caller polls the
operation resource and can request cancellation without depending on an HTTP
connection remaining open.

`GET /projects` merges persisted project definitions with projects inferred
from session and schedule catalogs. Every item has `id`, `name`,
`workspace_root`, `default_model_id`, `managed`, `revision`, and session and
schedule counts. Definitions without sessions remain visible. The list also
reports invalid definition count and truncation. `POST /projects` accepts
`{"id":"work","name":"Work","workspace_root":"D:\\work",
"default_model_id":"ling-3.0-tiny"}`; the last two fields are optional. It
returns `201` and a project ETag. Duplicate IDs return `409`; unknown fields,
invalid IDs, and empty supplied fields return `422`. Definitions are stored as
readable `projects/<id>.json` in the portable Home. Reads never materialize
Home. Session creation inherits a managed project's workspace and default
model when the request does not provide them.
Relative project workspace paths resolve from the executable's directory,
so a project placed beside `mdo.exe` remains portable when the folder moves.
`GET /projects/{id}` returns one managed definition and its strong ETag.
`PUT /projects/{id}` replaces `name`, `workspace_root`, and
`default_model_id` (all required; the model may be empty). `DELETE` unregisters
the definition only; existing sessions and schedules are retained and still
make the project visible as a derived entry. Both mutations require the
current `If-Match` ETag, returning `428` when missing and `412` when stale.
Writes preserve a readable `.bak` of the prior definition.

`GET /schedules/{schedule}/history` reads the retained completion sidecar
without creating Home. It returns the newest 32 records first, with task and
Agent run IDs, scheduled and finished times, result state, and a UTF-8-safe
text preview of at most 1024 bytes. `has_more` indicates earlier retained
records beyond this page; the portable JSONL sidecar keeps the full bounded
result text. A damaged record returns an error instead of a partial success.

`POST /schedules/{schedule}/run` accepts no body and requires the current
schedule ETag in `If-Match`. It returns `202` with the unified task ID and Agent
run ID. A stale revision returns `412`; a concurrent-run limit returns `409`.
This action can run a paused or exhausted definition, but requires the global
schedule execution setting to be enabled. It records a `run-now` audit entry
and leaves the definition's recurrence cursor unchanged.

The workspace file route lists relative file names for composer `@` completion;
it never reads file content. `q` is required, UTF-8, and at most 128 bytes.
Matching is case insensitive for ASCII and ranks basename prefixes first.
The server follows the session's workspace root, skips child symlinks and
hidden/build directories, scans at most 5000 entries in 128 directories and
five levels, and returns at most 12 names. `truncated` indicates that a scan
limit or result limit was reached; `scanned` reports entries visited. The UI
debounces requests and shows the first eight candidates. A selected name is
inserted into the prompt as a text reference, with quotes for spaces.

## Project lifecycle exclusion

Project definition endpoints, project/session drafts and submission intents,
queues, feedback, attachments, todos, project memory, and related session operations hold a
shared project lease for the entire handler. An exclusive project operation
causes these requests to return `409 project_busy` before running the handler.
Definition creation also checks the lease in the direct C writer. A busy project
is distinct from `project_exists`, stale revisions, and invalid fields.
`OPTIONS` and `405` responses do not take a lease; invalid identifiers retain
their endpoint's validation response. Global drafts and other projects keep
their own scope.

The global feedback listing can repair stored feedback; that repair obtains its
own project lease. Direct C memory writers and sidecar writers (todos, image
records/pruning/rollback, queue receipts/claims, and expired upload cleanup) also
obtain their own leases. Image copying retains both source and destination
projects. Memory directory import retains every associated project before any
publication and through completion or rollback; retained Agent memory tool
catalogs keep their project lease until the last tool reference is released.
Global memory remains independent of project exclusion.

Schedule definition writers reserve their associated project. Replacement
reserves both the current and destination project, then rechecks ownership
under the mutation lock. Global schedule enable changes reserve every catalog
project before updating runtime state and through rollback; a changed catalog
is rejected before mutation. Reloading an unchanged switch is read-only.

Due and explicit schedule claims also reserve the complete catalog while
synchronizing runtime cursors, then retain the claimed project's lease through
history publication. At most 64 mdo claims may be outstanding. Agent callback
owners separately retain their project until final runtime reference release,
even when memory tools are disabled. Cursor publication failure isolates the
complete project set; history failure retains the claim until manager shutdown.
This is process-local isolation, not a new crash-recovery transaction.

The current exclusive primitive is infrastructure for the planned purge
transaction. There is no executable project-purge API yet, and `purge-preview`
remains advisory. Legacy migration boundaries, the purge
transaction, and startup recovery still need implementation before purge opens.

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
For a new `agent_start` event, schema 3 includes `user_message_sequence`,
the exact durable ledger entry written for that user prompt. Other events and
resumed runs report zero. Existing schema 1 and 2 journal records still replay
with zero, so clients must not infer an edit boundary for those records.
Forking at a ledger sequence copies the retained UI event prefix into the new
session before publishing it. The copied records receive the child's session
identity; a main-Agent `agent_start` beyond the requested user-message
sequence and subsequent records are excluded. The UI journal remains bounded
by the existing retention limit, and unavailable older UI records cannot be
reconstructed from the model ledger.

The v1 transport deliberately uses bounded pull replay instead of holding an
SSE connection. This gives desktop, mobile, reload, and suspended-webview
clients the same recovery rule and creates no per-client server queue. The UI
fetches a resource snapshot, replays after its last cursor, and backs off while
the cursor is unchanged. Unknown event kinds remain ordered records and must
not stop replay.

`GET /projects/{project}/sessions/{session}/artifacts/{event-id}` reads a
tool-output artifact from that session's durable UI journal. The event must be
`tool_done` or `artifact_created` with a valid artifact ID and file name. The
server reconstructs the path under the current portable Home, opens it without
following symlinks, and rejects files over 8 MiB. `offset` defaults to zero;
`limit` defaults to and cannot exceed 64 KiB. The response includes Base64
bytes, total size, `eof`, media type, and a SHA-256 digest. Unlike the
process-local `/artifacts/{artifact}` registry, this session-scoped route works
after an exe restart or Home move. Missing journal events and artifacts return
`404`; an offset past the file returns `416`.

`GET /projects/{project}/sessions/{session}/todo` returns the most recent
successful main-Agent `mdo.todo` tool snapshot as `schema_version`, `event_id`,
and an `items` array of `{text, done}` objects. A missing sidecar returns an
empty list without creating Home. The tool submits a complete snapshot on each
call, including an empty list to clear it; the event bridge validates and
stores at most 24 items, 1024 UTF-8 bytes per item, and 12 KiB per snapshot.
The journal records the original tool event, while the bounded `todo.json`
sidecar serves quick reads.
Pure read tools, including this plan emitter, follow the configured automatic
read permission; effectful tools still use one-shot approval.

## User questions

The built-in `ask_user` tool suspends its tool call while the user answers a
structured question. `GET /projects/{project}/sessions/{session}/asks` lists
only live questions for that session, with `id`, `run_id`, `created_at`,
`question`, and up to eight `options`. The UI also accepts free text. A page
reload reads the same pending question without restarting the Agent.

`PUT /projects/{project}/sessions/{session}/asks/{ask}` accepts exactly
`{"answer":"..."}` with 1–1024 UTF-8 bytes. The answer is consumed once and
becomes the tool result; a stale ID or another session's ID returns 404.
Cancellation, the tool or run deadline, and shutdown release the waiting call. A
question waits at most four hours if no shorter deadline applies. Pending
questions are in process memory: after a process interruption, xwork retains
the pending read-only tool call in the durable session. The existing recovery
flow retries it and presents the question again; it does not silently reuse an
answer from a stopped process.

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
its revision and ETag. For a scheduled task owned by the product executor it
requests cancellation of the actual Agent Run. The task stays `running` until
the Run exits; `stop_requested: true` lets clients display "Stopping" and
disable repeat activation. A Run already completed is harvested with its actual
result. The ETag includes the stop request bit even before the xwork task
revision changes. An acknowledged snapshot must be retained if a later refresh
fails. Cancelled terminal tasks also report `stop_requested: true`; other
generic task kinds retain their existing xwork cancellation semantics.

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
