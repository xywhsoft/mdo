import { t as uiErrorText, t as uiText } from "../i18n.js";

import { api } from "./client.js";
import { workbenchServices } from "./workbench-services.js";

export function createApplicationClient(context = () => ({}), transport = api) {
  const call = async (method, params = {}, options = {}) =>
    (await transport.post("/application/call", { method, params, context: context() }, options)).data;
  return Object.freeze({
    version: 1, call, ...workbenchServices(call),
    bind: scope => { const fixed = Object.freeze({ ...context(), ...scope }); return createApplicationClient(() => fixed, transport); },
    services: async options => (await transport.get("/application/services", options)).data,
    describe: (id, options) => call("sdk.describe", { id }, options),
    events: (cursor = {}, options) => call("events.read", cursor, options),
    start: (method, params = {}, id, options) => call("operation.start", { method, params, ...(id ? { id } : {}) }, options),
    wait: (id, timeout_ms = 1000, options) => call("operation.wait", { id, timeout_ms }, options),
    cancel: (id, options) => call("operation.cancel", { id }, options),
    result: async (id, options = {}) => {
      const result = await call("operation.result", { id }, options);
      if (result?.encoding !== "utf-8-json" || !Number.isSafeInteger(result.bytes)) return result;
      if (result.bytes > 16 * 1024 * 1024) throw new Error(uiErrorText("error.detail.sdkResultExceedsTheClientLimit", {}, "SDK result exceeds the client limit"));
      const bytes = new Uint8Array(result.bytes); let offset = 0;
      while (offset < bytes.length) {
        const chunk = await call("operation.result", { id, offset, limit: 16384 }, options);
        const decoded = Uint8Array.from(atob(chunk.base64), c => c.charCodeAt(0));
        if (!decoded.length || chunk.next !== offset + decoded.length || chunk.next > bytes.length)
          throw new Error(uiErrorText("error.detail.sdkResultReturnedAnInvalidByteRange", {}, "SDK result returned an invalid byte range"));
        bytes.set(decoded, offset); offset = chunk.next;
      }
      return JSON.parse(new TextDecoder("utf-8", { fatal: true }).decode(bytes));
    },
  });
}

// UI code and window geometry belong to the displaying host even while its
// ordinary SDK calls address another device. Keep its token out of the selected
// target adapter; an uncertain local write is never automatically repeated.
export function createLocalApplicationClient(context = () => ({})) {
  const read = async response => {
    const envelope = await response.json();
    if (!response.ok || !envelope.ok) throw Object.assign(new Error(envelope.error?.message || uiText("ui.application.localApplicationServiceUnavailable", {}, "本机应用服务不可用")), { code: envelope.error?.code, status: response.status });
    return envelope;
  };
  return createApplicationClient(context, {
    async post(path, body, options = {}) {
      const bootstrap = await fetch("/api/v1/bootstrap", { signal: options.signal });
      const token = bootstrap.headers.get("X-Mdo-Write-Token");
      if (!bootstrap.ok || !token) throw new Error(uiText("ui.application.localApplicationServiceIsNotReady", {}, "本机应用服务尚未就绪"));
      return read(await fetch(`/api/v1${path}`, { method: "POST", headers: { "Content-Type": "application/json", "X-Mdo-Write-Token": token }, body: JSON.stringify(body), signal: options.signal }));
    },
    async get(path, options = {}) { return read(await fetch(`/api/v1${path}`, { signal: options.signal })); },
  });
}

/* Record intent before an effect. A reload between effect and acknowledgement
 * is uncertain and must be reviewed, never silently replayed. */
export async function applyApplicationAction(action, execute, storage, key) {
  // A missing journal is empty; an unreadable/corrupt journal is uncertain.
  // Treating corruption as empty could repeat an already completed effect.
  const saved = storage.getItem(key);
  let records;
  try { records = saved === null ? {} : JSON.parse(saved); }
  catch { throw new Error(uiErrorText("error.detail.uiIntentJournalIsCorruptInspectTheUiBeforeRepeating", {}, "UI intent journal is corrupt; inspect the UI before repeating effects")); }
  if (!records || typeof records !== "object" || Array.isArray(records))
    throw new Error(uiErrorText("error.detail.uiIntentJournalIsInvalidInspectTheUiBeforeRepeating", {}, "UI intent journal is invalid; inspect the UI before repeating effects"));
  if (Object.hasOwn(records, action.id)) {
    const saved = records[action.id];
    return saved.state === "done" ? saved.ack : { success: false, error: "The page restarted while this action was executing; inspect the UI before repeating it" };
  }
  // Refuse effects if the at-most-once journal cannot be persisted.
  records[action.id] = { state: "executing" };
  const ids = Object.keys(records); for (const id of ids.slice(0, Math.max(0, ids.length - 128))) delete records[id];
  storage.setItem(key, JSON.stringify(records));
  let ack;
  try { const result = await execute(action); ack = { success: true, ...(result === undefined ? {} : { result }) }; }
  catch (error) { ack = { success: false, error: String(error?.message || error).slice(0, 512) }; }
  // Keep screenshots and large snapshots out of the durable intent journal.
  // The server retains the actual ACK; after a page crash, report an explicit
  // ephemeral result instead of repeating the action to recreate its bytes.
  const journalAck = JSON.stringify(ack).length > 16384
    ? { success: ack.success, result: { ephemeral: true, message: "Read the server receipt; this result is not stored in the page journal" } } : ack;
  records[action.id] = { state: "done", ack: journalAck };
  storage.setItem(key, JSON.stringify(records));
  return ack;
}
