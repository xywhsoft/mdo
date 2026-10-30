import { t as uiText } from "../../i18n.js";
import { api } from "../../api/client.js";
import { subscribeTarget, targetState } from "../../api/target.js";
const listeners = new Set(), catalogs = new Map(), recoveries = new Map();
let generation = 0;
const owner = () => targetState().selected?.id || "local";
function invalidate() {
  ++generation; catalogs.clear();
  for (const listener of listeners) listener([]);
}
window.addEventListener("mdo-extensions-changed", event => { if (event.detail?.kind === "commands") invalidate(); });
subscribeTarget(invalidate);
export function subscribeCommands(listener) { listeners.add(listener); return () => listeners.delete(listener); }
export async function loadCommands(project = "default", { refresh = false } = {}) {
  const key = `${generation}:${project}`;
  if (refresh) catalogs.delete(key);
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
  const target = owner(), recoveryKey = JSON.stringify([target,scope.projectId,scope.sessionId,command.id,raw]);
  const recovered = recoveries.get(recoveryKey), id = recovered || commandInvocationId();
  const input = { invocation_id: id, command_id: command.id, raw_arguments: raw,
    session_id: scope.sessionId || "", run_id: scope.runId || "", revision: command.revision || "" };
  const path = `/command-invocations/${id}`;
  const current = () => owner() === target && !signal?.aborted;
  let result, admitted = false, terminal = false;
  if(!recovered&&recoveries.size>=64)throw new Error(uiText("ui.extensionCommands.tooManyUnconfirmedCommandsReadExistingCommandStatusFirst", {}, "未确认的命令过多，请先读取已有命令的执行状态。"));
  recoveries.set(recoveryKey,id);
  onPending?.(id, target);
  try {
    try { result = recovered ? (await api.get(path,{signal})).data : (await api.post(`/command-invocations?project=${encodeURIComponent(scope.projectId || "default")}`, input, { signal })).data;admitted = true; }
    catch (error) {
      if (!recovered && error.status >= 400) { recoveries.delete(recoveryKey);throw error; }
      if (!current()) throw error;
      try { result = (await api.get(path, { signal })).data; }
      catch { throw new Error(uiText("ui.extensionCommands.commandSubmissionIsUnconfirmedValue1ReadItsStatusBefore", { value1: id }, "命令提交结果不确定（{value1}）。请先重新读取状态，避免重复执行。"), { cause: error }); }
    }
    const checkReceipt = () => {
      if(!result||result.id!==id||typeof result.terminal!=="boolean"||typeof result.state!=="string")
        throw new Error(uiText("ui.extensionCommands.invalidCommandStateValue1ReadTheStatusAgainDo", { value1: id }, "命令状态无效（{value1}），请重新读取；不要重复提交。"));
    };
    checkReceipt();let readFailures = 0;
    const until = Date.now() + 610000;
    while (!result.terminal) {
      if (!current()) throw new DOMException("Command target changed", "AbortError");
      if (Date.now() > until) throw new Error(uiText("ui.extensionCommands.timedOutWaitingForCommandStatusValue1ReadThe", { value1: id }, "命令状态等待超时（{value1}）；请重新读取状态。"));
      await new Promise(resolve => setTimeout(resolve, Math.min(150 * 2 ** readFailures, 2000)));
      try { result = (await api.get(path, { signal })).data;checkReceipt(); readFailures = 0; }
      catch (error) { if (++readFailures >= 10 || error.status >= 400) throw error; }
    }
    terminal = result.terminal && !["interrupted", "uncertain"].includes(result.state);
    if(!current())throw new DOMException("Command target changed","AbortError");
    if (result.state !== "succeeded") throw Object.assign(new Error(result.error || uiText("ui.extensionCommands.commandFailedValue1", { value1: result.state }, "命令执行失败：{value1}")),{code:result.error_code,invocationId:id});
    return { ...result, raw_arguments: raw };
  } finally {
    if (terminal) recoveries.delete(recoveryKey);
    /* Keep uncertain identities, including cancellation after admission.
     * Re-entering the same command then reads its receipt rather than POSTs. */
    if (!admitted && recovered) recoveries.set(recoveryKey,id);
    onPending?.(id, target, true);
  }
}
export async function cancelCommand(id, target) {
  if (owner() !== target) return false;
  await api.delete(`/command-invocations/${encodeURIComponent(id)}`); return true;
}
