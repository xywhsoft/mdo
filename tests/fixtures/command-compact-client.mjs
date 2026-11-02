// Real frontend calls against the synthetic native service, including its
// asynchronous C command worker. Never use this on a user's real session.
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
const [origin, session] = process.argv.slice(2);
const nativeFetch = globalThis.fetch;
globalThis.window = new EventTarget();
globalThis.fetch = (url, options) => nativeFetch(new URL(url, origin), options);
const clientSource = new URL("../../app/web/js/features/chat/extension-commands.js", import.meta.url);
const served = await fetch("/js/features/chat/extension-commands.js");
assert.equal(served.status, 200);
assert.equal((await served.text()).replaceAll("\r\n", "\n"), (await readFile(clientSource, "utf8")).replaceAll("\r\n", "\n"),
  "native application must serve the same command client used by this probe");
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
