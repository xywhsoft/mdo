# Configured context compaction

The old packed runtime reproduced a conversation interruption on the second
turn: xwork's system instruction demanded the durable eight-heading summary,
while xllm-session's default coding policy required six different headings.
An obedient model therefore failed both quality attempts. The original native
tests explicitly selected durable and did not cover the default style.

The summarizer now follows the session's generated compaction prompt, including
custom format instructions outside serialized conversation/summary blocks.
Untrusted-data handling, tool exclusion, quality validation and transactional
checkpointing remain in place. This does not force a different session style
or bypass the quality gate.

Native regressions cover default coding, general and durable. The first two
failed before the fix; all three pass afterward. Existing functional tool,
deadline, cancellation and durable recovery cases also pass. The compatibility
suite uses the locked xs xhttp/XRT bridge: the historical standalone bridge
predates xrtRootRenameNoReplace and cannot link the current artifact tests.

`test_packed_compaction_recovery.py` uses an editable loopback model with a
16,384-token context. Five serial synthetic turns force four checkpoints. The
first summary receives one 429, then one deliberately invalid short summary,
then a valid coding summary. All turns succeed without final errors; the
runtime is restarted and a sixth turn still receives the original canvas,
palette and filename facts. No tools are offered to the summary request.

The real packed browser renders all six replies, then accepts one explicit
continuation through the composer. Seven replies remain visible, no runtime
error card or recovery banner appears, and the composer is empty after send.
Summary lifecycle details remain in the folded execution history.

- `packed.json`: bounded six-turn native run, request metadata and events.
- `browser.json` and `continued.png`: packed browser continuation.
- `packed-edit.json`: history edit/replay, idempotency and restart fences.
- `build.json`: clean-source candidate provenance and verification scope.

The candidate was built from `c1e42bf` plus the committed auth-renewal and
home-navigation fixes, with the new locked xs dependency. No unrelated working
tree edits were included. `xs-conversation-recovery.bundle` carries the source
repair for another checkout; its hash and ref are pinned in `deps.lock`.

This stage used synthetic local services, no paid model calls, stress or
high-load tests. The root executable, phone and public downloads were not
updated. Android device verification remains outstanding.
