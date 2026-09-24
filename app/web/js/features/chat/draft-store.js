import { api, resourceId } from "../../api/client.js";

const SAVE_DELAY_MS = 300;
const MAX_DRAFT_BYTES = 65536;

function endpoint(key) {
  if (!key) return "/draft";
  const [projectId, sessionId, extra] = key.split("/");
  if (extra !== undefined) throw new TypeError("Invalid draft owner");
  return `/projects/${resourceId(projectId, "project")}/sessions/${resourceId(sessionId, "session")}/draft`;
}

export function createDraftStore({ onRestore, onError, onSaved }) {
  const entries = new Map();
  const encoder = new TextEncoder();
  let selected = "";

  function entry(key) {
    let value = entries.get(key);
    if (!value) {
      value = { text: "", revision: 0, loaded: false, dirty: false,
        conflict: false, error: null, loading: null, saving: null, timer: 0 };
      entries.set(key, value);
    }
    return value;
  }

  function schedule(key, immediate = false) {
    const current = entry(key);
    window.clearTimeout(current.timer);
    if (current.conflict) return;
    current.timer = window.setTimeout(() => { void flush(key); },
      immediate ? 0 : SAVE_DELAY_MS);
  }

  async function load(key) {
    const current = entry(key);
    if (current.loaded) return;
    if (current.loading) return current.loading;
    current.loading = (async () => {
      try {
        const response = await api.get(endpoint(key));
        current.revision = Number(response.data.revision);
        current.loaded = true;
        current.error = null;
        if (!current.dirty) {
          current.text = response.data.text ?? "";
          if (selected === key) onRestore(current.text);
        } else schedule(key, true);
        if (selected === key && !current.dirty) onSaved();
      } catch (error) {
        current.error = error;
        if (selected === key) onError(error);
      } finally { current.loading = null; }
    })();
    return current.loading;
  }

  async function flush(key) {
    const current = entry(key);
    window.clearTimeout(current.timer);
    current.timer = 0;
    if (current.saving) return current.saving;
    current.saving = (async () => {
      if (!current.loaded) await load(key);
      if (!current.loaded || current.conflict) return;
      while (current.dirty) {
        const text = current.text;
        if (encoder.encode(text).length > MAX_DRAFT_BYTES) {
          if (selected === key) onError(new Error("草稿超过 64 KiB 保存上限"));
          return;
        }
        current.dirty = false;
        try {
          const body = { revision: current.revision, text };
          const keepalive = encoder.encode(JSON.stringify(body)).length <= 60 * 1024;
          const response = await api.put(endpoint(key), body, { keepalive });
          current.revision = Number(response.data.revision);
          current.error = null;
          if (current.text !== text) current.dirty = true;
          if (selected === key && !current.dirty) onSaved();
        } catch (error) {
          current.dirty = true;
          current.error = error;
          if (error?.code === "draft_conflict") current.conflict = true;
          if (selected === key) onError(error);
          return;
        }
      }
    })();
    try { await current.saving; }
    finally {
      current.saving = null;
      if (current.dirty && !current.conflict && current.loaded)
        schedule(key);
    }
  }

  function edit(key, text, immediate = false) {
    const current = entry(key);
    if (current.text === text && !current.dirty) return;
    current.text = text;
    current.dirty = true;
    schedule(key, immediate);
  }

  function select(key) {
    selected = key;
    const current = entry(key);
    onRestore(current.text);
    if (current.error) onError(current.error);
    else onSaved();
    if (!current.loaded) void load(key);
    else if (current.dirty) schedule(key, true);
  }

  window.addEventListener("pagehide", () => {
    for (const key of entries.keys())
      if (entry(key).dirty) void flush(key);
  });

  return Object.freeze({
    select,
    edit,
    capture(key, text) { if (entry(key).text !== text) edit(key, text, true); },
    clear(key) { edit(key, "", true); },
    flush,
  });
}
