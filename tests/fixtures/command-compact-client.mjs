// Real frontend calls against the synthetic native service, including its
// asynchronous C command worker. Never use this on a user's real session.
import assert from "node:assert/strict";
const [origin, session] = process.argv.slice(2);
const nativeFetch = globalThis.fetch;
globalThis.window = new EventTarget();
globalThis.fetch = (url, options) => nativeFetch(new URL(url, origin), options);
const { api } = await import("../../app/web/js/api/client.js");
const { readRecovery } = await import("../../app/web/js/state/recovery.js");
const { loadCommands, runCommand } = await import("../../app/web/js/features/chat/extension-commands.js");
await api.get("/project-purge-intent"); // Adopt this synthetic service's write token.
const command = (await loadCommands("default")).find(item => item.id === "compact");
assert.equal(command?.requires_session, true);
const scope = { projectId: "default", sessionId: session };
const inspection = readRecovery("default", session);
const compact = runCommand(command, "", scope);
const followup = readRecovery("default", session);
const [before, result, after] = await Promise.all([inspection, compact, followup]);
assert.equal(before.resume_required, false);
assert.equal(after.resume_required, false);
assert.equal(result.state, "succeeded");
assert.equal(result.actions[0].type, "ui.refresh");
console.log(JSON.stringify({ state: result.state, action: result.actions[0].type, inspected: true }));
