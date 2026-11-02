# 2026-11-03 Agent selection and local Mohists delivery

This is a local Windows candidate for the user's test installation. The displayed version remains 0.8.5. No platform download, updater, or Mohists store publication was changed.

## Implemented behavior

- The main composer lists all main-capable Agents beside the permission control. Blank-task selection does not create a conversation or call a model. Idle existing conversations can switch Agent while retaining history, model, reasoning and permission; active runtimes and unresolved submissions cannot switch.
- The public Application SDK provides Agent selection callbacks and extension settings pages. Callbacks are conversation-bound, cancel on navigation, clean up late results, and refresh conditional conversation contributions after an Agent change. The host contains no Mohists-specific dispatch.
- The Mohists plugin opens its companion panel when selected or restored and registers Settings → Extension settings → Mohists. The separate sidebar task-creation entry was removed.
- The reported AVL goal had zero main-chat events and no model run: the old form saved a team goal only. Normal execution now uses the main composer. Manual group creation is collapsed under advanced operations, explicitly says it only saves a goal, and unassigned groups display Awaiting assignments.

Implementation commits: `dc0e7ee3`, `cc4198e5`, `e2c55cce` in mdo; `4834fa0`, `e4ab70a` in Mohists. Public UI contracts are documented in [agent-selection-ui.md](../agent-selection-ui.md).

## Validation

- 56 targeted host Node tests passed for composer selection/defaults, task creation, profile acknowledgement recovery, SDK callbacks and settings pages.
- Python discovery passed 19 frontend contract tests and 11 Application SDK contract tests.
- Seven Mohists frontend tests passed. Native Agent selection integration verified populated history preservation, unchanged model/permission, no request on switching, coordinator role on the next input, and rejection of SubAgent-only and busy-runtime changes. Its model endpoint was a deterministic loopback fixture.
- Coordinator integration passed a direct simple answer and the tools/member/multi-turn flow, including correction after a tool error. No vendor endpoint was used.
- Browser acceptance sent a normal composer message to the fixture, received the answer, automatically opened the companion panel, saved the settings form, and switched Agent both ways without losing history. Desktop 1366×900 and narrow 360×800 layouts passed; the narrow dialog filled the viewport without horizontal overflow. No browser console errors were observed.
- The packed application booted with four registered roles and passed native switch smoke tests in the existing project Home. Android hardware, real vendor and cross-device acceptance were not performed in this change.

## Installed candidate

`E:\software\mdo\mdo.exe` is 6,670,706 bytes, SHA-256 `f40b7205f7df68196962ce988bab96dc31ab0516ed3439fb510515e3a5272330`, PE GUI subsystem 2. The private source packer now uses the existing GUI host `xsw.exe`; the initial console-host candidate was replaced after the user exited the program. The final client was reopened and verified to have one native window and no extra console window.

`mohists-0.1.0.zip` is 74,918 bytes, SHA-256 `bc8ca3ba03635f1ff30e6c8454c2eb9d5c0dbf316099b4478c1468350b8adf68`. All 30 archive files exactly match `E:\software\mdo\mdo-home\plugins\mohists` after installation through the extension API.

The signed-in account and existing AVL goal were verified in the running native client. The existing conversation shows the Mohists selector, its companion panel and the waiting-for-assignment explanation. No model request was started in the user's Home. A 1,038-file pre-install preservation check found no missing files; the changed journal retained its complete original byte prefix, while account refresh and session metadata changed during normal runtime recovery/navigation. Backups remain under `mdo-home/data/backups/mohists-agent-selection-20261103`.

Private build, smoke, installation, preservation and final hash receipts are in `.build/mohists-agent-selection-20261103`. Browser proof from the installed service is saved at `D:\GIT\mohists\runtime\acceptance\agent-selection-installed-browser.png`; the native accessibility tree independently confirmed the same controls. The temporary source maintenance service was stopped; the user's GUI remains running. No extra Home, worktree or runtime was created.
