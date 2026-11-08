# Browser Use JavaScript execution

Mainline base: `8c57034f9504c5c5540e22739893ede3000f89d4` (0.9.5).
Plugin base: `96f1955ccfcdcd95fabb1c10f2774088f1781333`.
Work remains on both master branches. No version IDs or online releases change.

## Implementation

- Add `execute_js` to the canonical browser command contract and guarded shared
  dispatcher. Preserve browser/task ownership, user takeover, cancellation,
  input/capture exclusion and native dialog/popup/download checkpoints.
- Select only a freshly observed root document and exact native frame/document.
  Windows routes same-origin, cross-process and nested frames through its private
  native session map. Default page realms use immutable unique context IDs;
  isolated scripts use a separate `scripts` world, never the trusted observer
  runtime. No numeric-ID fallback or browser-wide target discovery is introduced.
- Await async function bodies and return bounded JSON. Limits: 64 KiB source,
  10-second whole call, at most 3 seconds synchronous evaluation, default 16 KiB
  / maximum 64 KiB result. Exceptions and uncertain results are not replayed.
- Retain a provider-owned reservation for every outstanding native callback.
  A client timeout cannot release an unresolved Promise's reservation. Sixteen
  pending evaluations per physical browser are allowed; completion/destruction
  releases each reservation once.
- Register plugin tool `browser_evaluate`, shared `browseruse.evaluate` service,
  C convenience API and JS durable-operation client. Read-only tasks are denied.
  The plugin now registers 17 tools / 14 services. Update Skill and ZIP allowlist.
- Synchronize canonical native sources, locked compatibility patch and reviewable
  plugin host overlays. Standard builds verify their normalized source hashes.

## Verification

| Check | Result |
| --- | --- |
| Mainline sequential JavaScript suite | 1790 total: 1789 passed, 1 skipped, 0 failed |
| Plugin sequential JavaScript suite | 592 passed, 0 failed |
| Focused dispatcher / new script component integration | 52 passed, included above |
| Mainline Application SDK contract | 13 passed |
| Native source/patch consistency | 2 passed |
| Canonical native page-context and pending-reservation lifetime regression | Passed |
| Plugin native original-world regression | 80 assertions, zero wrong-realm effects |
| Existing native frame identity/navigation and routed CDP regressions | Passed; CDP 17 assertions |
| Standard mainline Windows compile/link/package | Passed |
| Actual mainline managed tool / shared service / WebView2 | 12 scoped cases passed |
| Plugin + Skill deterministic ZIP / installed-byte checks | 6 passed |

The twelve actual cases cover page globals/JSON, separate script/observer worlds,
async results, same-origin/cross-site OOPIF/nested frames, isolated cross-site
execution, exceptions/JSON limits followed by recovery, stale root/iframe
denial, timeout uncertainty without replay, and managed read-only denial.
The timeout test executes its original increment once and verifies it remains
one after recovery; it does not resend the timed-out script.

The actual workflow runs through the production managed tool executor and
plugin C service into the real mainline UI and native WebView2. It reuses the
one original Home/deployment; the fixture uses one loopback HTTP worker and
two sites. No model inference or external test website is used.

Windows test executable: `.build/browser-js-evaluation/mdo-browser-js.exe`,
7,702,066 bytes, SHA-256
`7815ef32ab2aeea5620ce1ec87c3964cbdcfbfeabc7f519c0921a465db4363a4`.
Its build receipt identifies the dirty tested integration tree at base `8c57034f`;
it is not a published release or a separate final packed-executable acceptance.

Early probe failures are retained: incorrect SDK session-create body, a missing
post-restart model-catalog wait, and assumptions that managed error text was
always JSON. These harness issues were corrected without resubmitting uncertain
website effects. Existing dynamic test import peers also required the new module
path; no production guard or outcome assertion was removed to pass the suites.
The SDK retains structured uncertainty fields; managed errors retain their
ordinary text format and are checked against observed page effects.

## Cleanup and limits

Close only the synthetic owned browser records and HTTP fixture. Restore the
original development config/Home/port in headless mode; keep the updated owned
plugin/Skill for further development. Retain private logs and receipts under
`.build/browser-js-evaluation/` and the plugin's `.mdo/qa/`.

The pre-existing `app/include/mdo/config.h` modification is retained outside the
commit (SHA-256 `418005fc8373174e2ab08d2ef95a6edb61106034133fb4b6610a78f0dd4f12b1`).
Android execution remains unsupported, with no page-world fallback, phone test,
provider change or Android automation claim. This feature does not qualify a new
two-device, Doze, Linux or online-publication release.
