import { api, resourceId } from "../../api/client.js";
import { t } from "../../i18n.js";

const SAVE_DELAY_MS = 300;
const MAX_DRAFT_BYTES = 65536;

function imageIds(value) {
  return Array.isArray(value) ? value.filter((id) =>
    typeof id === "string" && /^[0-9a-f]{32}$/.test(id)).slice(0, 4) : [];
}

function sameIds(a, b) {
  return a.length === b.length && a.every((id, index) => id === b[index]);
}

function endpoint(key) {
  if (!key) return "/draft";
  const [projectId, sessionId, extra] = key.split("/");
  if (extra !== undefined) throw new TypeError("Invalid draft owner");
  return `/projects/${resourceId(projectId, "project")}/sessions/${resourceId(sessionId, "session")}/draft`;
}

export function createDraftStore({ onRestore, onError, onSaved, onLoaded = () => {} }) {
  const entries = new Map();
  const encoder = new TextEncoder();
  let selected = "";

  function entry(key) {
    let value = entries.get(key);
    if (!value) {
      value = { text: "", attachments: [], revision: 0, loaded: false, dirty: false,
        uncertainRun: false, conflict: false, error: null, loading: null,
        saving: null, timer: 0 };
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
        // A local edit may precede the GET response. Never clear a persisted
        // review guard while saving that edit.
        current.uncertainRun ||= response.data.run_admission_uncertain === true;
        if (!current.dirty) {
          current.text = response.data.text ?? "";
          current.attachments = imageIds(response.data.attachments);
        } else schedule(key, true);
        if (selected === key) onRestore(current.text,
          [...current.attachments], current.uncertainRun);
        if (selected === key && !current.dirty) onSaved();
        if (selected === key) onLoaded();
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
    if (current.saving) {
      await current.saving;
      return current.loaded && !current.dirty && !current.conflict;
    }
    current.saving = (async () => {
      if (!current.loaded) await load(key);
      if (!current.loaded || current.conflict) return;
      while (current.dirty) {
        const text = current.text;
        const attachments = [...current.attachments];
        const uncertainRun = current.uncertainRun;
        if (encoder.encode(text).length > MAX_DRAFT_BYTES) {
          if (selected === key) onError(new Error(t("draft.tooLarge", {},
            "草稿超过 64 KiB 保存上限")));
          return;
        }
        current.dirty = false;
        try {
          const body = { revision: current.revision, text, attachments,
            run_admission_uncertain: uncertainRun };
          const keepalive = encoder.encode(JSON.stringify(body)).length <= 60 * 1024;
          const response = await api.put(endpoint(key), body, { keepalive });
          current.revision = Number(response.data.revision);
          current.error = null;
          if (current.text !== text || !sameIds(current.attachments, attachments) ||
              current.uncertainRun !== uncertainRun)
            current.dirty = true;
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
    return current.loaded && !current.dirty && !current.conflict;
  }

  function edit(key, text, attachments = entry(key).attachments, immediate = false) {
    const current = entry(key);
    const ids = key ? imageIds(attachments) : [];
    if (current.text === text && sameIds(current.attachments, ids) &&
        !current.dirty) return;
    current.text = text;
    current.attachments = ids;
    current.dirty = true;
    schedule(key, immediate);
  }

  function select(key) {
    selected = key;
    const current = entry(key);
    onRestore(current.text, [...current.attachments], current.uncertainRun);
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
    editAttachments(key, attachments, immediate = false) {
      edit(key, entry(key).text, attachments, immediate);
    },
    capture(key, text, attachments = []) {
      if (entry(key).text !== text ||
          !sameIds(entry(key).attachments, attachments))
        edit(key, text, attachments, true);
    },
    clear(key) { edit(key, "", [], true); },
    isLoaded(key) { return entry(key).loaded; },
    async ensureLoaded(key) {
      await load(key);
      return entry(key).loaded;
    },
    isRunUncertain(key) { return entry(key).uncertainRun; },
    setRunUncertain(key, uncertain) {
      const current = entry(key);
      if (current.uncertainRun === Boolean(uncertain)) return;
      current.uncertainRun = Boolean(uncertain);
      current.dirty = true;
      schedule(key, true);
    },
    clearIfMatches(key, text, attachments = []) {
      const current = entry(key);
      if (current.text !== text ||
          !sameIds(current.attachments, imageIds(attachments))) return false;
      edit(key, "", [], true);
      return true;
    },
    restoreUnsent(key, text, attachments = []) {
      const current = entry(key);
      const submitted = imageIds(attachments);
      if (current.text === text && sameIds(current.attachments, submitted))
        return { text, attachments: submitted, merged: false };
      const otherText = Boolean(current.text && current.text !== text);
      const merged = otherText || current.attachments.some((id) =>
        !submitted.includes(id));
      const nextText = otherText && text ? `${text}\n\n${current.text}`
        : current.text || text;
      const nextAttachments = [...submitted,
        ...current.attachments.filter((id) => !submitted.includes(id))].slice(0, 4);
      edit(key, nextText, nextAttachments, true);
      return { text: nextText, attachments: nextAttachments, merged };
    },
    flush,
  });
}
