# Settings reads survive temporary connection failures

This stage fixes two packed-app failures that can change the conversation experience: a lost startup settings read left saved language, theme and input behavior at their fallbacks; an acknowledged save with a lost confirmation read immediately reported a failure. Recovery now rereads settings with bounded exponential waits and keeps the composer draft editable. It never retries the save or reset request.

## Packed UI verification

The local fixture is `tests/manual_conversation_retry_qa.py`, with a copied Windows executable, a separate Home and a loopback-only synthetic model. Browser actions used CUA and ordinary controls. CDP intercepted only the exact `GET /api/v1/settings` response, after the server had returned 200, and failed it with `ConnectionClosed`. Interception was removed in every case. These are functional fault checks, not load tests.

1. Save Russian, dark theme and guidance input behavior through the normal revisioned API. In the baseline executable, lose the startup settings response and restore connectivity: the page remains Chinese with the system theme. See `settings-saved-before.jpg` and `settings-lost-after-read.jpg`.
2. Repeat against the fixed executable. Type `SETTINGS_FINAL_RECOVERY_DRAFT` while recovering. The UI automatically returns to Russian and dark theme; the draft remains and sending becomes available. See `settings-restored-draft.jpg`.
3. Change density using the settings control and lose its first confirmation response. The observed request window contains one PATCH and two GETs, with no evicted network events. The footer stays busy, then reports Saved. See `settings-saved-after-echo-loss.jpg`.
4. Change font size and fail six confirmation responses. The footer stays busy through the retries, then accurately explains that the changes were saved but the latest settings could not be read. A subsequent connection recovery reads the snapshot again, clears the stale warning, and disables Save/Discard. The observed window contains one PATCH, six failed GET responses and a seventh recovery GET; no events were evicted. See `settings-final-confirmation-error.jpg` and `settings-reconfirmed-after-final-error.jpg`.
5. Return to the conversation. Language, theme and draft are retained; the persisted draft file has the same text. See `settings-confirmed-draft-preserved.jpg`.

The final fixture made zero model requests. Native files confirm Russian, dark theme, guidance mode, compact density, small font and the unchanged draft. The baseline and final executable hashes are in `acceptance.json`; the final fixture copy matches the rebuilt candidate. The candidate also includes pre-existing workspace changes outside this stage.

## Regression checks

54 Node tests passed across settings state/autosave/pages/resources, catalog recovery, workspace startup and composer defaults. They cover a six-attempt ceiling, shared deadline, read/write permission failures, reset cancellation, fresh ETags, obsolete responses, locale initialization attempted at most once per load, read-only recovery after an acknowledged PATCH/DELETE, and accurate messages in all three UI languages. The 20 frontend contract checks passed; all 138 reachable modules parsed successfully.

Each recovery episode uses at most six reads within 60 seconds, each read bounded by eight seconds. Waits start at 500 ms and double up to 8 seconds, with up to 20% jitter. A healthy snapshot remains available during refresh. An exhausted transient read can start a new read episode when the existing connection/foreground handling signals recovery; permanent access failures do not automatically retry.

Manual PATCH/DELETE requests whose own acknowledgements are lost are outside this change: they are not blindly resubmitted. The optional initial locale PATCH is attempted once per load; subsequent recovery reads are read-only. Reset confirmation recovery and permission denials were covered by deterministic tests, not by the packed UI sequence. Android APK build verification is recorded separately; this stage has no real-device Android result.
