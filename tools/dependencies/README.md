# Locked xs host source

The current lock uses `xs-xrt-mainline.bundle`. Both the native XRT core and
the xllm, xllm-session and
xwork sources are mechanically exported from one committed XRT mainline
revision, recorded in each `UPSTREAM.txt` and in `deps.lock.libraries`.
`tools/export_agent_libraries.py --check` and the XS vendoring verifier check
the export. Production-tree hashes also cover the host-owned bridge headers.
xllm is 3.2.0; rebuild all consumers for the changed public request layout.
Session schema 4 and xwork ABI 9/event schema 3 retain the product contract.
The core implementation and declaration headers are pinned together. Existing
process-tree/ConPTY compatibility fixes stay in the reviewed native overlay;
the regex fix is already in mainline and needs no separate backport.
See `docs/build-records/2026-11-05-xrt-core-mainline.md` and the preceding
`docs/build-records/2026-11-05-xrt-agent-mainline.md`.

`xwork-mcp.patch` now contains only native host/core compatibility and host-owned
bridge changes. The agent-library implementations live in the bundled XS
snapshot. Keeping this real overlay preserves the native patch maintenance
workflow and its verified build receipts; it does not retain a dependency on
the deleted mdo branches. The preceding `xs-agent-mainline.bundle` retains the
earlier agent-only integration as historical source evidence.

## 上游维护约定

- XRT 的通用改动优先直接提交 `master`。确实需要临时分支时，按小阶段验证并尽快
  合并回主线，不长期保留 mdo 专用分支。
- 先把产品所需行为和回归纳入 XRT 的规范源码，再同步宿主；不要用旧快照覆盖上游
  新实现，也不要以仅修改版本号或空合并代替实际接线。
- mdo 使用完整主线提交、源码哈希和实际宿主收据锁定依赖。更换核心时，实现头、
  声明头、native 宿主和 TCC SDK 必须同步，并验证进程、模型与会话恢复。
- 宿主快照使用固定标签和可验证 bundle，不新增维护分支。已有完整且验证通过的 SDK
  缓存直接复用，避免为每次构建再创建 checkout；全新构建仍可从 bundle 重建。

mdo 的 native overlay 另包含浏览器面板与窗口位置记忆。窗口状态文件由
`window.state_file` 按与 `profile_dir` 相同的 Home 选择规则解析，保存在 Home 的
`data/ui`，与浏览器缓存分开。原生头文件在 `tools/native/`；更新后执行
`python tools/native/update_browser_patch.py` 再构建宿主。构建盘空间不足时，
可将 `MDO_SDK_OVERLAY_CACHE` 指向另一个磁盘的 SDK 缓存目录；仍校验锁定提交和补丁摘要。

The preceding lock used `xs-regex-acceptance.bundle` on `refs/heads/master`.
It retains XRT `3d55ec41` with the upstream DFA acceptance fix from
`xrt@a539c579` and records that backport in `deps.lock.xrt.upstream_fixes`.
The native host must be rebuilt: changing application sources alone does not
repair the regex engine. This fixes ordinary `grep` searches such as `match .*`
against `xrtahomatch Match;`, including retrying an interrupted persisted call.
No application/extension ABI or persistence schema is changed.

The preceding `xs-xrt-current.bundle` upgraded the product host while retaining the existing
product APIs and fixes from `4b0dddaa`. XRT is implemented exactly once; the
HTTP client compiles as native sources instead of embedding an old core.
Product Unix/monotonic microsecond records retain their meaning; native XRT
waits receive signed relative milliseconds. See
`docs/build-records/2026-10-18-xs-xrt-upgrade.md` for the migration and gates.

The preceding lock used `xs-context-governance.bundle`, extending the compatible
history described below. It preserves native process failure causes for
exec/spawn/stop (including system codes, without argv/environment or error Data),
and reports relevant bounded edit contexts and actual multiline match locations.
Exact matching, atomic batch writes, public ABI 8 and the locked xrt ABI are
unchanged. Rebuild native hosts to include these fixes.

Context governance now retains the last server input/output usage as a prefix
anchor and estimates the actual later messages, tool results and system prompt
changes. The checkpoint and journal preserve the anchor and its model identity;
reapplying the same model keeps it, and a different model or missing legacy
feedback falls back to the retained context estimate. Context events expose the
same statistics to the token meter. Structured text and image parts participate
in estimates. This compatibility extension preserves the public ABI and schema
4, with additive checkpoint fields and a usage journal record.

The continuation extension aligns UTF-8 cursors and separates invalid encoding
from binary data. Tool-local failures become failed results instead of aborting
the loop; cancellation, OOM, lost context and uncertain receipts retain their
fatal semantics. Successful repeated batches continue. All completed batch
results are saved before applying a consecutive failure guard. Unusable model
responses are rejected before ledger commit, and product recovery owns retries.

The memory-read extension distinguishes invalid path arguments, missing files,
filesystem permissions and mount/workspace boundary failures. Read diagnostics
include bounded escaped locators and the actually mounted memory index aliases.
Embedded NUL is rejected on every platform; Windows also rejects invalid file
name characters. Other platforms retain their legal file names, and the existing
workspace boundary remains the default. The new additive host opt-in allows
ordinary external file paths in full access; explicit mounts retain their own
boundaries. Subagents inherit the host-selected scope. The read handler verifies
unambiguous formatting/memory path recovery, declares the corrected resource to
permission callbacks, clamps page options and pages large text files. True
missing files and OS denial remain errors. The original tool-diagnostics bundle
remains available as historical source evidence; ABI 8 and the xrt lock are unchanged.

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

The current product source bundle continues to pin the compatible native host
while its agent libraries track the consolidated XRT source. Another machine
can reproduce the build without the historical mdo branch names. Moving the
whole native host to regular XS mainline remains a separate core/SDK migration.

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
