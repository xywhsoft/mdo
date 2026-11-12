# Device picker operation feedback and synchronization

The picker waited for revoke/remove jobs, then reread the local connector cache.
Only a full list job replaced that cache, so successful mutations still appeared
unchanged. There was no per-operation progress or success feedback. The current
device label also hid its actual authorization state.

Changes:

- After an owner-bound successful service response, the remote manager publishes
  a new device cache: revoke updates that device's authorization; remove deletes
  its row. Deep cloning preserves snapshots already held by readers. Allocation
  failure invalidates the cache without declaring a confirmed write failed.
- Job snapshots include action and device ID. Admission and receipt polling
  share a 40-second overall deadline that also aborts stalled HTTP requests;
  account/state verification reads have their own 10-second bound.
  Admitted jobs can be watched by
  ID through reads only; polling errors retain that ID and an unknown-outcome
  classification. Definitive HTTP job failures keep their own error receipt.
- The picker shows row progress, disables conflicting actions, reports success
  immediately and performs a full list reconciliation automatically. Confirmed
  results survive stale cache reads and a failed subsequent list refresh.
- Closing the modal does not cancel or replay an admitted mutation. Reopening
  resumes its progress; a new picker can recognize an active backend mutation.
  Unknown outcomes offer inspection rather than automatic write retries.
  Background inspection is bounded; manual inspection remains available.
- Account generations fence old responses and detached buttons. Existing rows,
  list order, focus and scroll are retained across list updates. Polling runs
  while visible; focus/visibility restoration triggers synchronization.
- Current-device identity is shown alongside authorization state. Selected
  control/view/disconnected status tracks target events. Revoking the selected
  target stops reconnects and rejects late tickets while preserving the selected
  workspace and drafts; requests never fall back to local business operations.
- Chinese, English and Russian feedback strings are supplied.

Validation:

- 34 related JavaScript checks pass, including 12 actual-picker DOM tests,
  9 receipt/polling/timeout tests, existing connector/transport checks, locale coverage,
  and target revocation/account/late-ticket assertions.
- 19 existing frontend contract checks pass.
- 5 actual C cache checks pass in the existing mdo compiler runtime. The first
  run exposed shared children after a shallow clone; deep cloning makes original
  snapshots and unrelated rows remain unchanged.
- Full current product C source compiles in that same runtime; the native
  contract verifies action metadata, for 6 native checks in total. Reproduce with
  `python tests/test_remote_listing_native.py --url <local-url> --home <existing-home>`.
  Only owned command fixtures are created and removed by hash; normal invocation
  receipts remain. No new service, application/Home copy or model call is used.

Real account/device authorization was not revoked or removed during testing.
This is a source change, not a client installation or online publication; the
running packed client must be updated before its device picker changes.
