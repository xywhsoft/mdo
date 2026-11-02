# Agent selection and extension settings

The composer lists every installed Agent whose public flags include `MDO_AGENT_MAIN`, including roles that also support delegation. SubAgent-only definitions stay in the SubAgent catalog. A new conversation records the selected Agent when its first message or attachment creates it. Selection itself creates no conversation and makes no model request.

An idle existing conversation can change `agent_id` through the optional field on `PUT /api/v1/projects/{project}/sessions/{session}/profile`. The existing model, reasoning, permission, history and revision checks remain in force. The manager recreates and validates the Agent against the recovered ledger before committing metadata. Active runtimes refuse the change. UI pending submissions and member-main-speaker routes also block selection. A lost acknowledgement is confirmed against both the desired Agent and model profile.

Frontend extensions may register a companion UI for any installed main Agent:

```js
sdk.ui.registerAgent("agent.example", {
  async onSelected({ sdk, context, signal }) {
    if (!context.session || signal.aborted) return;
    const panel = await sdk.ui.openPanel("example.workspace");
    return () => panel?.close();
  },
  async beforeDeselected({ sdk, context, signal }) {
    // Optionally refuse changing away while extension work needs resolution.
  },
});
sdk.ui.registerSettingsPage("example-settings", {
  title: "Example",
  async mount(root, { sdk, signal }) {
    // Render settings using the context-bound public Application SDK.
    // Return an optional cleanup function; abort guards every asynchronous UI update.
  },
});
```

`onSelected` also runs when opening or restoring a conversation and when the plugin finishes loading after that conversation. Unchanged catalog polling does not rerun it. Changing conversation, Agent, device, or leaving the chat aborts its signal; an asynchronous returned cleanup still runs. This is a display callback, not authorization to send a message or start execution.

Settings registrations appear under Extension settings in both desktop navigation and the mobile selector. Page IDs must be unique lowercase URL segments. Settings mount lazily on entry, with their own saves and an abort signal; leaving or unloading the plugin releases the page. Global preference buttons do not apply to these extension-owned forms.

All registrations belong to the calling plugin scope and are removed on unload. No Agent ID is hardcoded into the host's selection or callback dispatch.
