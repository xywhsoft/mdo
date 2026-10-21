# Skill support completion

The ecosystem includes nine categories: Agent, SubAgent, tools, Skills, MCP,
commands, C modules, executable toolpacks, and client plugins. Client plugins
modify or add files in the application VFS; they are separate from source-resource
bundles. This change set completes the Skill layer in five independently committed goals.

- [x] Discovery and selection: bounded model metadata, paginated search, explicit chat selection.
- [ ] Reliable invocation: consistent limits, usable resource directories, bounded reads and clear errors.
- [ ] Format compatibility: invocation controls, argument/directory variables, diagnostics for unsupported behavior.
- [ ] Scope and isolation: project discovery and precedence, plugin-qualified names, independent visibility.
- [ ] Creation and migration: assisted authoring, existing-agent imports, source/scope management.

Skill instructions never authorize tools or install dependencies. Script execution
continues through the ordinary approved process tools. Each goal includes bounded
offline tests; no production account, model or user workspace is used for testing.
