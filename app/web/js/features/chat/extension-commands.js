import { api } from "../../api/client.js";
import { subscribeTarget, targetState } from "../../api/target.js";
const listeners = new Set(), catalogs = new Map();
let generation = 0;
const owner = () => targetState().selected?.id || "local";
function invalidate() {
  ++generation; catalogs.clear();
  for (const listener of listeners) listener([]);
}
window.addEventListener("mdo-extensions-changed", event => { if (event.detail?.kind === "commands") invalidate(); });
subscribeTarget(invalidate);
export function subscribeCommands(listener) { listeners.add(listener); return () => listeners.delete(listener); }
export async function loadCommands(project = "default") {
  const key = `${generation}:${project}`;
  if (catalogs.has(key)) return catalogs.get(key);
  const serial = generation;
  const read = api.get(`/commands?project=${encodeURIComponent(project)}`).then(response => {
    if (serial !== generation) return [];
    return Object.values(response.data || {}).filter(item => item.enabled && item.valid)
      .map(item => ({ ...item, name: `/${item.id}`, custom: true }));
  }).catch(error => { catalogs.delete(key); throw error; });
  catalogs.set(key, read); return read;
}
export function parseCommand(value) {
  const match = /^\/([a-z0-9_.-]+)(?:\s+([\s\S]*))?$/.exec(value);
  return match ? { id: match[1], raw: match[2] || "" } : null;
}
export function commandInvocationId() {
  const bytes = crypto.getRandomValues(new Uint8Array(16));
  return "cmd-" + [...bytes].map(byte => byte.toString(16).padStart(2, "0")).join("");
}
// A lost acknowledgement requires a status GET, never a repeated mutation.
export async function runCommand(command, raw, scope, { onPending, signal } = {}) {
  const target = owner(), id = commandInvocationId();
  const input = { invocation_id: id, command_id: command.id, raw_arguments: raw,
    session_id: scope.sessionId || "", run_id: scope.runId || "", revision: command.revision || "" };
  const path = `/command-invocations/${id}`;
  const current = () => owner() === target && !signal?.aborted;
  let result;
  onPending?.(id, target);
  try {
    try { result = (await api.post(`/command-invocations?project=${encodeURIComponent(scope.projectId || "default")}`, input, { signal })).data; }
    catch (error) {
      if (error.status >= 400 || !current()) throw error;
      try { result = (await api.get(path, { signal })).data; }
      catch { throw new Error(`命令提交结果不确定（${id}）。请先重新读取状态，避免重复执行。`, { cause: error }); }
    }
    let readFailures = 0;
    const until = Date.now() + 610000;
    while (!result.terminal) {
      if (!current()) throw new DOMException("Command target changed", "AbortError");
      if (Date.now() > until) throw new Error(`命令状态等待超时（${id}）；请重新读取状态。`);
      await new Promise(resolve => setTimeout(resolve, Math.min(150 * 2 ** readFailures, 2000)));
      try { result = (await api.get(path, { signal })).data; readFailures = 0; }
      catch (error) { if (++readFailures >= 10 || error.status >= 400) throw error; }
    }
    if (result.state !== "succeeded") throw new Error(result.error || `命令执行失败：${result.state}`);
    return { ...result, raw_arguments: raw };
  } finally { onPending?.(id, target, true); }
}
export async function cancelCommand(id, target) {
  if (owner() !== target) return false;
  await api.delete(`/command-invocations/${encodeURIComponent(id)}`); return true;
}
