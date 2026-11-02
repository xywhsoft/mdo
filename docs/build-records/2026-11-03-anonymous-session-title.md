# Anonymous session titles — 2026-11-03

Implemented native background naming for new conversations and deployed its anonymous server endpoint. Website installers and update channels were not republished; existing installed clients require a new build to opt into this behavior. Display version and installation serials were not changed by this feature.

## Behavior and ownership

The normal new-task client creates sessions with `auto_title: true`. After the first accepted main-user run starts, the native session manager captures at most 2,048 Unicode characters / 8,192 UTF-8 bytes of visible input. Attached images, file contents, contextual quotes and the appended `mdo-context` envelope are excluded. The ordinary Agent run proceeds independently.

The native manager has one bounded worker and at most 32 queued session references. It calls the fixed platform endpoint without account credentials or a login requirement, with an eight-second total timeout. A valid result replaces the provisional excerpt. Refusals, timeout, malformed output and service failures silently retain the excerpt; they never fail or retry the main conversation. Explicit/custom and legacy session titles are not automatically renamed.

Optional `title_state` metadata distinguishes manual/legacy, waiting, pending, generated and fallback states. A small Home-relative `title-request.json` retains a stable request ID and bounded seed for interrupted work, and is removed after processing. Shutdown cancels I/O and leaves unfinished work recoverable; pending requests older than one minute fall back. Manual rename, including entering the same text, cancels automatic replacement. Updates re-read current metadata under the session locks, retain last activation and live runtime state, and permit native handles to rebase over title-only changes while preserving ordinary profile conflicts. The bounded backup decoder accepts the optional metadata field.

## Platform endpoint

`POST https://ai.xywhsoft.com/api/v1/ai/session-title` accepts exactly `request_id`, `content` and optional `locale`. No login, Cookie, Bearer token or user model entitlement is required. It returns the standard platform envelope containing `data.title`.

The server fixes the model to the enabled `ornith-1.5-35b` configuration, uses the configured chat channel and server-owned upstream credentials, disables thinking, supplies no tools, and caps output at 96 tokens. It does not invoke user billing, user token pools or user model admission. Naming initialization failure does not disable the ordinary model gateway.

The endpoint enforces a 16 KiB HTTP body, 2,048-character / 8 KiB content limit, six-second upstream deadline, a 16 KiB response cap and two title requests in flight. Title calls also reserve slots in the existing shared upstream concurrency accounting. Default attempt limits are 12/minute and 200/UTC day per hashed client address, and 120/minute and 5,000/UTC day globally. Capacity/refusal failures use the client's normal fallback.

Request ID and content/locale fingerprint provide 24-hour durable deduplication, including cached failures and pending requests. Reusing an ID with different content is rejected. Only hashes, titles, status and timestamps are retained server-side; input text and upstream secrets are not stored in the title cache or returned to callers.

## Validation

- Server loopback integration passed anonymous access, strict/oversize input handling, fixed model and disabled thinking, successful/failing request deduplication, invalid output, shared capacity, nonblocking HTTP, timeout, restart cache, rate limit and absence of user billing rows. Ordinary model endpoints still reject unauthenticated inference.
- Eleven native contracts passed bounded credential-free requests, title/fallback transitions, manual rename protection, pending recovery, context exclusion, UTF-8 boundaries, title-only metadata rebasing and unrelated profile conflict preservation. Six session activation contracts passed.
- Actual native application integration passed first-turn-only naming, subsequent ordinary turns, anonymous HTTP, context exclusion, manual rename race, silent fallback and explicit title preservation. All five ordinary model runs succeeded using a local fixture; no vendor request was made by local tests.
- Six new-task tests, eleven Application SDK contracts, five session contracts and nineteen frontend contracts passed.
- Backup decoder passed both HTTP and TLS suites, each covering v1/v2 round trips, 247 malformed inputs, ownership, exact bytes, model/UI relations, real writers, cancellation, retry and no Home writes during inspection. Its source model fixture was changed to an offline custom provider so the test no longer depends on account login.

## Production deployment and verification

Only the model module's `main.c` and new `src/session_title.c` were deployed. The previous main entry and gateway were checked against their recorded SHA-256 values before replacement. The service was idle for model requests and exited normally via SIGTERM; systemd restarted it successfully. Account, conversation, billing and model configuration were not replaced. The native server executable, `xs.json` and existing proxy source hashes remained unchanged; unrelated working-tree edits were retained.

The live anonymous request returned HTTP 200 in 0.585 seconds. Repeating it returned the same cached result. An independent unauthenticated HTTPS call from Windows also returned the cached result in 0.125 seconds, without another inference. The public model catalogue and service health recovered normally.

Remote backup/receipts: `/home/xs/mdo-session-title-20261103`. Local deployment, native integration and live verification receipts: `.build/session-title-20261103`; native contract receipts: `.build/session-title-native`. Server implementation commit: `10ec609f` in `D:\GIT\home`; initial client implementation: `3c5101aa` in this repository. No Android device or installer publication is claimed for this feature.
