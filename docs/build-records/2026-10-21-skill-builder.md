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
