# Browser host mainline integration

Base: mdo `4f1111b2e7bb68e138be817f48571a2c825908b5` (0.9.5).
Imported enhancement source: mdo-browser-use `96f1955ccfcdcd95fabb1c10f2774088f1781333`.
Work proceeds on master. The existing uncommitted config.h is preserved by hash;
version/build IDs and online releases are outside this integration.

## Image-export deadline fix

The first whole frontend regression exposes a final-budget timer boundary:
the export aborts its binary read, but a timer firing before the clock's exact
deadline permits the next image's metadata read. The failure retains three
requests where only the original metadata/body pair is allowed. That file is
unchanged by the Browser Use import, so this is a separately identified defect.

The export now retires its shared budget when its timeout owner settles. A
controlled early-timer regression proves no later metadata request is started,
without wall-clock sleeps or increasing production deadlines. All six image
export tests pass; the combined focused UI/export run passes eleven cases.
No account, session, attachment or Home data is changed by these component tests.

The initial broad Node command also includes two standalone browser drivers;
the authoritative suite is `tests/test_*.mjs`, as used by qa_release.py. Original
failures and subsequent logs are retained in `.build/browser-host-integration`.
Native/mainline integration and final regression are recorded below; these
component results alone do not qualify the whole change.

## Mainline integration

The reviewed generic host/application changes from the source revision above
now live in canonical `app/`, SDK contracts, Android adapters and native build
sources. Browser Use model tools, plugin and Skill retain their independent
distribution; this change does not register them as built-in tools.

Scope includes browser observation/actions/navigation/waits, frame routing and
capture, native dialogs/popups/uploads/downloads, frozen workspace identity,
verified file receiving/publication, operation/stream lifetime and recovery,
approval ownership/wakeup, UI delivery and private result acknowledgement.
An existing generic UI action without an `action` string must still ACK; the
new private-browser-result check now guards the string before inspecting it.
A targeted regression also proves private results do not enter replay storage.

The locked SDK remains based on `e9a3551a5a533de34e2ca4291a8def71612fbf35`.
Its standard compatibility patch includes the reviewed native enhancements,
current model-media transport compatibility, `.mjs` MIME handling and Android
private CDP JNI integration. Patch SHA-256:
`2b7a81a70b820db822144154b91adf79b22af2bf74f1c019679f5865e49643db`.
The build verifies 17 canonical/native source pairs against the lock before
reusing the existing SDK. It does not require the plugin development loader.

## Validation

| Check | Result |
| --- | --- |
| Frontend module graph | 296 modules parsed and reachable |
| Final sequential main + browser-host JavaScript suite | 1779 total: 1778 pass, 1 skipped, 0 fail |
| Imported generic host components (included above) | 25 modules, 340 cases pass |
| Application SDK contract | 13 pass |
| Native source/patch integrity and path escape rejection | 2 pass |
| Model SDK build constraints | 2 pass |
| Standard Windows compile/link/package | Pass |
| Actual mainline application SDK + WebView2 | 19 actions, 4 navigations, 5 wait conditions, 3 frames; pass |
| Actual managed Browser Use tools + WebView2 | 16 tools registered; permissions, recovery, display, dialogs, popup ownership, capture, gestures, selection and drag pass |
| Android production Java source preparation/compile | 78 sources, API 36, source/target 8; pass |
| Android ARM64 actual JNI entry + private CDP transport | NDK syntax compile pass |

The standard Windows test executable is
`.build/browser-host-integration/mdo-browser-host-integration.exe`, 7,688,174
bytes, SHA-256 `67aea87d4a71494f536cd84fbbcbd94b9d900d6665424ed0da1612c70255c97c`.
The build receipt identifies the tested dirty integration tree at parent
`f8cd4362`; it is a local test build, not a published production release.

Runtime checks ran the actual mainline application sources through the standard
native host, with the independently installed extension in the one existing
development Home. They are not a separate packed-executable acceptance run.
The first SDK harness exited successfully before chaining the managed harness;
the completed SDK receipt was retained and the managed test was run separately.
Fixture/pre-start harness failures are retained alongside the successful runs.
No model calls were sent. No app or Home clone was created.

Android automation remains disabled (`browser_use.available:false`). This turn
does not qualify a full Android APK build, phone acceptance, Doze, two-device
acceptance or Linux native build. No page-world automation fallback was enabled.

## Cleanup and retained evidence

The temporary mainline service configuration and three synthetic HTML fixtures
were removed only after identity/content checks. The original single deployment
configuration and Home were restored, retaining the account and user data.
Private reports, logs, build receipts and import hashes remain under
`.build/browser-host-integration/`; reusable component regressions now live in
`tests/browser_host/` and run in the normal frontend gate.

The pre-existing `app/include/mdo/config.h` modification is excluded from commits;
its raw SHA-256 remains
`418005fc8373174e2ab08d2ef95a6edb61106034133fb4b6610a78f0dd4f12b1`.
Version/build identifiers and online downloads/updates are unchanged.
