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
Native/mainline integration and full final regression are recorded below when
completed; these component results do not qualify the whole change.
