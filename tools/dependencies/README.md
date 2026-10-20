# Locked xs host source

The current lock uses `xs-xrt-current.bundle` on `refs/heads/master`.
It upgrades the product host to XRT `ac059267` while retaining the existing
product APIs and fixes from `4b0dddaa`. XRT is implemented exactly once; the
HTTP client compiles as native sources instead of embedding an old core.
Product Unix/monotonic microsecond records retain their meaning; native XRT
waits receive signed relative milliseconds. See
`docs/build-records/2026-10-18-xs-xrt-upgrade.md` for the migration and gates.

`xs-tool-image-text.bundle` carries the compatibility commits from
`bd7831704482cc7dd418260281a37c6bdb222b23` to the xs revision in `deps.lock`.
It contains the upstream xllm stream termination/output-limit fixes and xwork
draft restart markers, configured compaction summary formatting and accepted
task stop snapshots, preserving
mdo's currently locked xrt ABI. The compaction format fix is also committed in
the current xrt mainline and synced into current xs.

The task snapshot query is additive in the locked xwork API (ABI 7).
It captures the accepted stop flag with task metadata without changing the
public task-info layout. Current xs mainline uses a different xwork API; this
bundle keeps the compatible source reproducible until that migration.

It also includes the Linux desktop/server hosts, target-matched TCC CRT,
streamed tool-result file handling, the glibc 2.28 release build guard and
runtime selection of WebKitGTK 4.1 or 4.0 (>= 2.30). Linux release compilation
uses the isolated baseline described in `docs/linux.md`.

The bundle is source history, not an executable. Its exact SHA-256, branch ref
and destination commit are locked. When a normal xs clone has the base but is
on another revision, `build_mdo.py` imports the bundle and creates a detached
checkout under `.build/locked-xs`. The caller's branch and local files stay in
place. Explicit `--xserver` paths still require the exact locked revision.

After mdo moves to the latest xrt wait API, remove this compatibility bundle and
lock the regular upstream xs revision. Until then, the bundle lets another
machine reproduce the build without an unpublished local branch.

The locked fetch request also supports a v3 final-response-header callback.
The response layout is unchanged; genuine v1/v2 request prefixes remain valid.
The callback exposes borrowed views only after a complete fetch, preserving
Retry-After without making xhttp response types part of the SDK contract.
Premature response EOF has a distinct SDK error code, so mdo can recover safe
page reads without retrying malformed HTTP or replaying billed search POSTs.

The xllm snapshot also backports `xrt@7449e55e`: string assistant history in
Responses uses EasyInputMessage without an explicit message discriminator.
Strict servers otherwise parse it as an output message and reject follow-ups
with HTTP 400. Structured content and function call/output pairing are retained;
existing session snapshots need no migration.

The Android shell now defers saved-page navigation until the host is ready.
When the host port changes, the last local path/query/fragment follows the new
origin, retaining the selected conversation. An unchanged live origin can
restore WebView history. Lost-renderer callbacks wait for Activity recreation.
The source bundle includes the route resolver's JVM checks and compilation
test; actual WebView input and lifecycle verification still require a device.

The locked xwork command tools also preserve empty `argv` entries after the
program name, including `ssh-keygen -N ""`. Approval and command parsing reject
an empty program or embedded NUL. Descriptor failures retain their diagnostic
through batch execution instead of being reported as approval denials; actual
permission refusals retain their existing behavior. Empty arguments are shown
as `""` in command previews. This compatibility fix keeps ABI 8 unchanged and
requires rebuilding the Windows and Android native hosts.

The session library also retains the tool result's text/path as a text part
alongside image bytes (upstream `xrt@54676d1d`). This fixes the loss of text
when a message switches from plain content to structured parts. The session
schema and public ABI are unchanged; rebuild native hosts to use the fix.

The Android window shell now accepts document-picker `ClipData` as well as a
single URI. It keeps selection order, removes duplicates, honors single-input
mode and completes canceled selections once. Returned URIs must use the granted
`content` provider scheme; private `file` paths and other URL schemes are not
passed to WebView. Bounded JVM cases and the full Android shell compile are in
`tools/test_android_file_chooser.py`; actual ARM64 picker/send evidence is in
`tests/acceptance/android-file-chooser` in mdo. This changes Java only, not the
native ABI. Repackage Android to include it.

The compatible xwork source bundle also provides a retained `xwork_file_scope`.
Hosts attach it before executor binding and can change its external-path policy
atomically during execution. Existing and future child agents retain the same
scope; mounted aliases remain confined and read-only effect ceilings still apply.
mdo pairs this scope with its synchronized approval state, so a live permission
change updates both approval and native-path access without restarting an Agent.
The additional opaque APIs leave ABI 8 and persisted session schemas unchanged.
