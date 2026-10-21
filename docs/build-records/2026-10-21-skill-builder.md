# Skill builder — 2026-10-21

## Reference and scope

ZCode's public Skill UI launches a chat-guided creator. Its built-in
`skill-creator` supplies intent capture, drafting, trial inputs, improvement and
existing-Skill updates. Reviewed the official documentation and source on
2026-10-21:

- https://zcode.z.ai/en/docs/skill
- https://github.com/zai-org/ZCode/blob/main/apps/zcode-cli/packages/skill-creator-plugin/skills/skill-creator/SKILL.md

mdo's implementation uses original instructions adapted to its actual tools,
permissions, directories, parameter semantics and resource limits. It adds native
read-only checks and keeps ordinary writes/approvals. There are two goals, each
committed separately on master: creator/validation, then guided UI integration.

## Goal 1: built-in creator and native checks

Committed as `9c9c183c`.

Added `$skill-creator`, a minimal template and on-demand authoring/test guidance.
It supports create, improve, review and extracting a workflow from the current
conversation. No extra dependency or external runtime is required.

`skill(action="validate", content=...)` checks a draft. `name` and `scope` check
the actual personal/project folder fresh from disk, including its UTF-8 body,
resource paths and limits. Invalid Skill content returns a normal report with
errors so the model can correct it and continue. An HTTP route exposes the same
checker to editors. Reports distinguish document-only checks, resource checks and
model behavior; validation never runs scripts, writes files or calls a model.

Native probes passed for valid/invalid drafts, missing resources, invalid UTF-8,
scope/path rejection and built-in resources. Offline HTTP regression loaded the
creator, received and corrected a format error, used the normal `write` tool to
save a new Skill and verified the resulting project folder. All six mock-model
steps completed without final model errors. Existing Skill runtime and feature
probes passed. UI integration is the next goal.

## Goal 2: guided builder UI

Settings > Skill exposes **Skill 构建器**, with create, improve and read-only review
modes, optional ID/requirements and the existing project/device/scope selection.
Personal/project lists can open improvements; project editors can validate an
unsaved document and project cards can check complete folders without changing
their enablement. Invalid/oversized creator expansion preserves all form input.
Stale or cancelled requests cannot launch a late draft on a different target.

The builder expands the installed `skill-creator` through the scoped invocation
API before creating a dedicated durable draft. It does not assume the bundled
copy is still enabled or silently bypass a user override. The user sends the
draft normally, after reviewing it. Review/improvement/new sessions have distinct
titles. Model calls, file writes and script runs retain existing permission rules.

The creator's own literal `$ARGUMENTS` explanation is escaped; the actual user
request is substituted once in its task section. This prevents a large task from
being substituted into instructional examples.

Final verification:

- Strict C11 unity syntax compilation (`-Wall -Wextra -Werror`): passed.
- Native builder checks, Skill catalog/resource/features and parameter probes: passed.
- Offline extension HTTP regression: passed; six builder model steps used normal
  Skill/write callbacks, corrected invalid content and verified the saved folder.
- Full API runtime probe: passed.
- Python unit/contract suite: 152 tests, 3 skipped, zero failures.
- Full frontend suite: 1,035 tests, 1,034 passed, 1 skipped, zero failures.
- Frontend graph: all 171 modules parsed and reachable.
- Builder form tests covered review/improvement prompts, actual creator expansion,
  disabled and oversized creators, retained edits, read-only checks and cancelled
  late responses. Existing drafts and attachments remained intact.
  A failed project-directory lookup clears old roots before reading, so a new
  project can never launch with the previous project's destination.

Evidence: `.build/skills-validation-20261021/builder-http.json` and
`builder-frontend.log`. Tests used disposable Homes, isolated model fixtures and
the pinned native host. No live model calls, online deployment, packaged-client
visual acceptance or Android device test was performed.
