# mdo Skill format

A Skill is a directory containing SKILL.md and optional scripts/, references/,
assets/ and templates/. The UTF-8 Markdown document starts with YAML frontmatter;
name and description are required. Unknown passive metadata is preserved.
YAML aliases, tags, duplicate fields and unsafe resource paths are rejected.

```markdown
---
name: review
description: Inspect changes before delivery
---
```

Use `name: review` and `description: Inspect changes before delivery` as top-level
fields. Directory IDs are portable lowercase names. Optional fields include
version, license, compatibility, when_to_use, argument-hint and metadata mappings.

- `disable-model-invocation: true` hides the Skill from model discovery and blocks
  model loads and Agent preloading. The user can select it with `$review`.
- `user-invocable: false` hides it from chat selection and rejects explicit user
  invocation; model discovery/loading still works.
- `metadata: {"opencode/autoinvoke":"false"}` also disables model invocation.
- `tools: [read, grep]` declares required tools. The compatibility spelling
  `allowed-tools: Read Grep` maps plain tool aliases; it never bypasses approval.
  Use one spelling. Command permission patterns are unsupported.
- `mcp` and `permissions` retain dependency metadata. They grant no permissions.
- Legacy scripts/templates/assets arrays restrict resource access to those files.
  Without these arrays, safe files are discovered automatically (128 files,
  depth 8, 8 MiB per resource, 16 MiB total).

`context`, `agent`, `model`, `effort`, `hooks`, `shell`, `paths`, `disallowed-tools`
and named `arguments` declarations require unsupported execution semantics and
are rejected with a field-specific diagnostic. Configure an Agent or SubAgent
for those workflows. Shell substitutions in Markdown are never executed.

The model receives a bounded metadata index. The skill tool searches with query,
offset and limit (default 20, maximum 50); responses include total/next_offset.
Loading name returns instructions and a real base_directory. `$ARGUMENTS` and
`${MDO_SKILL_DIR}`, `${CLAUDE_SKILL_DIR}`, `${ZCODE_SKILL_DIR}` are replaced
literally; replacement values are never evaluated or recursively expanded.
Script execution continues through normal process tools and permission checks.

SKILL.md is limited to 128 KiB in management and runtime. Resource text reads are
48 KiB pages with UTF-8-aligned cursor, next_cursor and eof. Binary resources are
not converted to text. XRTPACK resources are materialized on demand into a
content-addressed Home cache; listing does not write files. File changes require
catalog refresh; existing loaded generations retain their owned bytes.

Startup reads only the front matter and file metadata. External directories
completely shadow the corresponding built-in Skill; an invalid override never
falls back to the built-in definition. Undeclared legacy resources cannot be opened
through the Skill API. External content is labeled EXTERNAL_REFERENCE and remains
untrusted reference material, including after reload or cache materialization.

## Project scope and package names

Chat resolves Skills for its actual workspace. Personal Home overrides built-ins.
Within a project, precedence (lowest first) is `.opencode/skills`, `.zcode/skills`, `.codex/skills`,
`.claude/skills`, `.agents/skills`, `.mdo/skills`. Inside a Git worktree, ancestor directories up to
the nearest `.git` marker are eligible; the closest directory wins. Outside Git,
only the exact workspace is scanned. Symlinks are not followed. Invalid or disabled
project shadows do not reveal lower-priority instructions. Diagnostics include the
source path. Scope limits apply to the combined catalog (256 entries).

Project enable flags live in `.mdo/skills.json` as an ID-to-boolean object, separate
from personal enable flags. Project catalogs are independently owned; they never
mutate the global catalog. Current projects and devices select their own chat list.

Installed resource-package receipts expose Skills as `package-slug:skill-id`.
Bare IDs remain callable for old Agent bindings but are hidden from discovery; a
project override replaces the bare ID while the qualified package Skill remains
available. Namespace collisions fail explicitly. These are resource packages, not
the future client-plugin runtime.

## Creation, migration and management

In Settings > Skill, choose **来源、项目与 AI 创建**. The workbench shows the
selected project's `.mdo/skills` entries, their enabled/valid status and source
paths. Create or edit SKILL.md, enable/disable a Skill, or remove it into
`.mdo/skill-trash/<id>-<nonce>`. Removal preserves the entire folder for manual
recovery. Personal Skills continue to use the existing editor and package owner
controls. Editors require the last content revision; conflicting writes fail
instead of overwriting another edit. Project mutations hold a shared project
lifecycle lease and writer lock.

**用 AI 编写 Skill** creates a dedicated conversation with an unsent, saved draft.
It specifies the selected device's real target directory and supported format.
Existing conversations, drafts and attachments are preserved. The user sends the
draft before any model call; normal tool approvals govern subsequent writes.

Migration discovers personal mdo Skills plus `.agents`, `.claude`, `.codex`,
`.zcode`, `.opencode` and `.mdo` skill directories in the selected workspace and
the OS user profile. It does not search the disk recursively. Pick an individual
source and an unused destination ID in personal or project scope. The source is
unchanged. Imports validate the complete staged Skill before publishing the folder
with a no-replace rename. Source files that change during reading are rejected.
Scripts are copied without execution or dependency installation. Unsupported
behavior fields are reported in source diagnostics.

A migration copies up to 128 ordinary, non-hidden resource files, 8 MiB each and
16 MiB total including SKILL.md. Symlinks are excluded. Source discovery displays
up to 128 entries and 128 diagnostics and reports truncation. New managed IDs are
limited to 64 portable characters and must begin with a lowercase letter. Existing
compatible longer source IDs may be copied to a shorter destination ID.

Relevant local APIs: `GET /skills?project=<id>`, `POST /skills/invoke?project=<id>`,
`GET /skills/sources?project=<id>`, `POST /skills/migrate?project=<id>`, and
`GET/PUT/DELETE /skills/project/<skill-id>?project=<id>` with the `/enabled` POST
operation. All mutating editor operations require If-Match; import always refuses
an existing target. Skill text replies allow 1 MiB of encoded JSON so escaping
does not silently shrink the documented 128 KiB document limit. Excessive argument
expansion is rejected explicitly.

The chat composer has a separate 64 KiB durable-draft limit. Inline `$skill`
expansions that exceed it are rejected before replacing the current draft, with
a specific message. Large Skills remain available through the model's lazy Skill
tool (128 KiB instructions and paginated resources); move detailed material into
references when a Skill must also support inline manual selection.
