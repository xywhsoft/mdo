// Queue removal can happen while an unrelated run keeps the session open.
// The attachment API protects all images in an open runtime, so retry only
// after a run snapshot says the session is idle. The server still checks every
// draft, queue, and history reference before deleting an image.
export function createUnusedImageCleanup({ deleteImage, isRunActive }) {
  const pending = new Map();
  const flushing = new Map();

  async function flushKey(key) {
    if (flushing.has(key)) return flushing.get(key);
    if (isRunActive(key) || !pending.get(key)?.size) return;
    const task = (async () => {
      const [projectId, sessionId] = key.split("/");
      const images = pending.get(key);
      for (const id of [...images]) {
        if (isRunActive(key)) break;
        try {
          await deleteImage(projectId, sessionId, id);
          images.delete(id);
        } catch (error) {
          if (error?.code === "attachment_not_found") images.delete(id);
          // In-use and transient errors remain for the next run refresh.
        }
      }
      if (!images.size) pending.delete(key);
    })();
    flushing.set(key, task);
    try { await task; }
    finally { flushing.delete(key); }
  }

  return Object.freeze({
    remember(key, attachments = []) {
      if (!key || !attachments.length) return;
      let images = pending.get(key);
      if (!images) { images = new Set(); pending.set(key, images); }
      for (const id of attachments) {
        if (typeof id === "string" && /^[0-9a-f]{32}$/.test(id)) images.add(id);
      }
      void flushKey(key);
    },
    flush() { return Promise.all([...pending.keys()].map(flushKey)); },
  });
}
