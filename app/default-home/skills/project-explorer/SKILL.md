---
name: Project Explorer
description: Inspect a repository and report its structure before making changes.
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

Inspect the repository before editing it. Identify the build entry points,
project-owned source, generated or vendored boundaries, and the narrowest useful
validation commands. Report concrete paths and distinguish verified facts from
inferences.

Use `templates/report.md` when a durable project map is requested.
