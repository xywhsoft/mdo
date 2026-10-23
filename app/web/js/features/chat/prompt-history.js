import { api, currentPageWriteToken, resourceId } from "../../api/client.js";
import { targetState, subscribeTarget } from "../../api/target.js";
import { isImeKey } from "../../utils/dom.js";
import { MAX_PROMPT_BYTES } from "./prompt-context.js";

export function historyValues(items) {
  if (!Array.isArray(items) || items.length > 30 || items.some(item =>
    !/^[0-9a-f]{32}$/.test(item?.id) || typeof item.text !== "string" ||
    !item.text || new TextEncoder().encode(item.text).length > MAX_PROMPT_BYTES))
    throw new TypeError("Invalid input history");
  if (new Set(items.map(item => item.id)).size !== items.length) throw new TypeError("Invalid input history");
  return items.map(item => item.text).filter((value, index, all) => index === 0 || all[index - 1] !== value);
}

export function createPromptHistory({ input, navigation, restore, hasContexts = () => false,
  read = path => api.get(path), post = (path, body) => api.post(path, body) }) {
  const caches = new Map();
  const writes = new Map();
  let index = null, moving = false, route = "";
  const stamp = () => JSON.stringify([targetState().selected, targetState().runtimeChanged, currentPageWriteToken()]);
  const project = () => navigation.get().projectId || "default";
  const key = () => `${stamp()}/${project()}`;
  const path = id => `/projects/${resourceId(id, "project")}/input-history`;
  async function load() {
    const owner = key(), id = project();
    if (caches.has(owner)) return caches.get(owner);
    const value = { texts: [], pending: null }; caches.set(owner, value);
    value.pending = read(path(id)).then(reply => { value.texts = historyValues(reply.data.items); })
      .catch(() => { if (caches.get(owner) === value) caches.delete(owner); }).finally(() => { value.pending = null; });
    await value.pending; return value;
  }
  input.addEventListener("focus", () => { void load(); });
  input.addEventListener("input", () => { if (!moving) index = null; });
  const reset = () => { index = null; route = JSON.stringify(navigation.get()); };
  navigation.subscribe(reset); subscribeTarget(() => { caches.clear(); reset(); }); reset();
  return {
    record(projectId, entry) {
      if (!entry.text || !/^[0-9a-f]{32}$/.test(entry.id)) return;
      const origin = stamp(), owner = `${origin}/${projectId}`;
      const operation = (writes.get(owner) ?? Promise.resolve()).then(async () => {
        if (stamp() !== origin) return;
        try {
          const reply = await post(path(projectId), { id: entry.id, text: entry.text });
          if (stamp() !== origin) return;
          caches.set(owner, { texts: historyValues(reply.data.items), pending: null });
        } catch {
          if (stamp() !== origin) return;
          // History is secondary to the durable submission. A lost history ACK
          // is confirmed by reads; never submit another message or replay writes.
          try {
            const reply = await read(path(projectId));
            if (stamp() === origin) caches.set(owner, { texts: historyValues(reply.data.items), pending: null });
          } catch { caches.delete(owner); }
        }
      });
      writes.set(owner, operation);
      return operation.finally(() => { if (writes.get(owner) === operation) writes.delete(owner); });
    },
    onKeyDown(event) {
      if (event.defaultPrevented || isImeKey(event) || event.shiftKey || event.altKey || event.ctrlKey || event.metaKey ||
          input.disabled || input.readOnly || !["ArrowUp", "ArrowDown"].includes(event.key) ||
          input.getAttribute("aria-expanded") === "true") return false;
      if (route !== JSON.stringify(navigation.get())) reset();
      if (index === null && (input.value || hasContexts() || event.key === "ArrowDown")) return false;
      const texts = caches.get(key())?.texts;
      if (!texts?.length) { void load(); return false; }
      const next = event.key === "ArrowUp" ? Math.max(0, (index ?? texts.length) - 1) : Math.min(texts.length, index + 1);
      event.preventDefault(); index = next === texts.length ? null : next; moving = true;
      try { restore(index === null ? "" : texts[index]); input.dispatchEvent(new Event("input", { bubbles: true })); }
      finally { moving = false; }
      input.setSelectionRange(input.value.length, input.value.length); return true;
    }, load,
  };
}
