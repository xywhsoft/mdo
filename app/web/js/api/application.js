import { api } from "./client.js";

export function createApplicationClient(context = () => ({})) {
  const call = async (method, params = {}, options = {}) =>
    (await api.post("/application/call", { method, params, context: context() }, options)).data;
  return Object.freeze({
    version: 1, call,
    services: async options => (await api.get("/application/services", options)).data,
    describe: (id, options) => call("sdk.describe", { id }, options),
    events: (cursor = {}, options) => call("events.read", cursor, options),
    start: (method, params = {}, id, options) => call("operation.start", { method, params, ...(id ? { id } : {}) }, options),
    wait: (id, timeout_ms = 1000, options) => call("operation.wait", { id, timeout_ms }, options),
    cancel: (id, options) => call("operation.cancel", { id }, options),
    result: async (id, options = {}) => {
      const result = await call("operation.result", { id }, options);
      if (result?.encoding !== "utf-8-json" || !Number.isSafeInteger(result.bytes)) return result;
      if (result.bytes > 16 * 1024 * 1024) throw new Error("SDK result exceeds the client limit");
      const bytes = new Uint8Array(result.bytes); let offset = 0;
      while (offset < bytes.length) {
        const chunk = await call("operation.result", { id, offset, limit: 16384 }, options);
        const decoded = Uint8Array.from(atob(chunk.base64), c => c.charCodeAt(0));
        if (!decoded.length || chunk.next !== offset + decoded.length || chunk.next > bytes.length)
          throw new Error("SDK result returned an invalid byte range");
        bytes.set(decoded, offset); offset = chunk.next;
      }
      return JSON.parse(new TextDecoder("utf-8", { fatal: true }).decode(bytes));
    },
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
  catch { throw new Error("UI intent journal is corrupt; inspect the UI before repeating effects"); }
  if (!records || typeof records !== "object" || Array.isArray(records))
    throw new Error("UI intent journal is invalid; inspect the UI before repeating effects");
  if (Object.hasOwn(records, action.id)) {
    const saved = records[action.id];
    return saved.state === "done" ? saved.ack : { success: false, error: "The page restarted while this action was executing; inspect the UI before repeating it" };
  }
  // Refuse effects if the at-most-once journal cannot be persisted.
  records[action.id] = { state: "executing" };
  const ids = Object.keys(records); for (const id of ids.slice(0, Math.max(0, ids.length - 128))) delete records[id];
  storage.setItem(key, JSON.stringify(records));
  let ack;
  try { await execute(action); ack = { success: true }; }
  catch (error) { ack = { success: false, error: String(error?.message || error).slice(0, 512) }; }
  records[action.id] = { state: "done", ack };
  storage.setItem(key, JSON.stringify(records));
  return ack;
}
