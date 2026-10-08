# xs conversation recovery source

`xs-conversation-recovery.bundle` carries the compatibility commits from
`bd7831704482cc7dd418260281a37c6bdb222b23` to the xs revision in `deps.lock`.
It contains the upstream xllm stream termination/output-limit fixes and xwork
draft restart markers, configured compaction summary formatting and accepted
task stop snapshots, preserving
mdo's currently locked xrt ABI. The compaction format fix is also committed in
the current xrt mainline and synced into current xs.

The task snapshot query is additive in the locked xwork 3.8.1 API (ABI 7).
It captures the accepted stop flag with task metadata without changing the
public task-info layout. Current xs mainline uses a different xwork API; this
bundle keeps the compatible source reproducible until that migration.

The bundle is source history, not an executable. Its exact SHA-256, branch ref
and destination commit are locked. When a normal xs clone has the base but is
on another revision, `build_mdo.py` imports the bundle and creates a detached
checkout under `.build/locked-xs`. The caller's branch and local files stay in
place. Explicit `--xserver` paths still require the exact locked revision.

After mdo moves to the latest xrt wait API, remove this compatibility bundle and
lock the regular upstream xs revision. Until then, the bundle lets another
machine reproduce the build without an unpublished local branch.
