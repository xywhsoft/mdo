# mdo Skill format v1

A Skill is a readable directory below `skills/<id>/`. The directory ID starts
with a lowercase ASCII letter and then uses lowercase letters, digits, `.`,
`_`, or `-`. Each directory has one `SKILL.md` and may contain explicitly
declared files below `scripts/`, `templates/`, and `assets/`.

`SKILL.md` starts with a bounded YAML front matter subset:

```markdown
---
name: Project Explorer
description: Inspect a repository before making changes.
version: 1.0.0
license: MIT
compatibility: mdo >= 0.1
tools:
  - ls
  - glob
  - grep
mcp: []
permissions:
  - workspace.read
scripts: []
templates:
  - templates/report.md
assets: []
---

# Project Explorer

The body is loaded only when an Agent selects this Skill.
```

The scalar keys `name` and `description` are required. `version`, `license`,
and `compatibility` are optional. Sequence values accept either the block form
shown above or an inline list such as `[ls, "grep"]`. Values may be plain,
single quoted, or JSON-style double quoted strings. Duplicate keys, duplicate
list entries, tabs, aliases, tags, mappings, multiline scalars, and unknown
keys are rejected. This deliberately small grammar keeps parsing deterministic
without embedding a second configuration language.

`tools`, `mcp`, and `permissions` declare dependencies for policy checks and
the settings UI. Resource lists are manifests, not search paths. Every entry
must use `/`, remain below its matching directory, and contain no empty, `.`,
or `..` segment. A resource not listed in front matter cannot be opened through
the Skill API.

Startup reads only the front matter and file metadata, then closes every file
so a Windows handle cannot block atomic replacement. On the first explicit
load, the catalog reopens the file, verifies size, identity, timestamps, and
the front matter hash, and stores an owned bounded cache. A mismatch reports a
stale catalog and requires reload instead of mixing generations. Once loaded,
an old catalog continues to return its cached generation after replacement.

The merged VFS discovers built-in and external directories by ID. An external
directory completely shadows the same built-in ID. If its `SKILL.md` or a
declared resource is missing or invalid, mdo publishes a diagnostic and omits
that Skill from the new catalog; it never falls back to the built-in copy.
Other valid Skills remain available.

Catalog records label built-in content as `BUILTIN` and external content as
`EXTERNAL_REFERENCE`. Prompt assembly must preserve that source label and treat
external Skill bodies, scripts, templates, and assets as user-controlled
reference material. The loader validates structure and paths but does not
claim that content is safe instructions or safe executable code.
