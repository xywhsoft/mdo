import { api, attachmentFileName, resourceId } from "../../api/client.js";

// Names are immutable for a scoped attachment ID. Share reads between draft
// and history thumbnails; retain only a bounded number of session entries.
// A failed read preserves the generic label and retries after a short pause.
export function createImageNameCache({ limit = 128, retryMs = 5000,
  now = Date.now, read = (path) => api.get(path) } = {}) {
  if (!Number.isInteger(limit) || limit < 1 || !Number.isFinite(retryMs) || retryMs < 0)
    throw new TypeError("Invalid image name cache bounds");
  const entries = new Map();

  function key(owner, id) {
    if (!/^[0-9a-f]{32}$/.test(id)) throw new TypeError("Attachment ID is invalid");
    return `/projects/${resourceId(owner.projectId, "project")}` +
      `/sessions/${resourceId(owner.sessionId, "session")}/attachments/${id}/info`;
  }

  function store(path, entry) {
    entries.delete(path);
    entries.set(path, entry);
    while (entries.size > limit) entries.delete(entries.keys().next().value);
    return entry;
  }

  function get(owner, id) {
    const path = key(owner, id);
    const cached = entries.get(path);
    if (cached && cached.retryAt > now()) {
      store(path, cached);
      return cached.promise;
    }
    const entry = { retryAt: Infinity, promise: null };
    entry.promise = Promise.resolve().then(() => read(path)).then(({ data }) => {
      if (data?.id !== id || ![1, 2].includes(data.schema_version))
        throw new Error("Invalid image metadata");
      const name = data.schema_version === 2 ? attachmentFileName(data.file_name) : "";
      if (data.schema_version === 2 && !name) throw new Error("Invalid image name");
      entry.name = name;
      return name;
    }).catch(() => { entry.retryAt = now() + retryMs; return ""; });
    store(path, entry);
    return entry.promise;
  }

  function remember(owner, id, name) {
    const valid = attachmentFileName(name);
    if (!valid) return;
    store(key(owner, id), { name: valid, retryAt: Infinity, promise: Promise.resolve(valid) });
  }

  return { get, remember, peek: (owner, id) => entries.get(key(owner, id))?.name };
}

export const imageNames = createImageNameCache();
const previewNames = new WeakMap();
const unnamedPreview = Promise.resolve("");

// A preview owns the metadata promise captured when its source thumbnail was
// rendered. It can finish after that thumbnail is replaced; the dialog does
// not need to observe mutable DOM nodes or parse an attachment URL/reference.
export function previewImageName(preview) {
  return previewNames.get(preview) ?? unnamedPreview;
}

export function labelImageName({ preview, caption, remove, owner, id,
  viewLabel, removeLabel, cache = imageNames }) {
  const reference = preview.dataset.imageRef;
  function apply(name) {
    if (!name) return;
    caption.textContent = name;
    caption.title = name;
    preview.title = name;
    preview.setAttribute("aria-label", viewLabel(name));
    preview.querySelector("img").alt = name;
    if (remove) {
      remove.title = name;
      remove.setAttribute("aria-label", removeLabel(name));
    }
  }
  // Cached names are applied before reconciliation compares new and existing
  // cards. Otherwise each queue poll would replace a previously named card.
  apply(cache.peek(owner, id));
  const pendingName = cache.get(owner, id);
  previewNames.set(preview, pendingName);
  return pendingName.then((name) => {
    // Async reads must never label a detached/reused thumbnail after navigation
    // or a locale rerender. Updating text in place also preserves focus.
    if (!name || !preview.isConnected || preview.dataset.imageRef !== reference) return;
    apply(name);
  });
}
