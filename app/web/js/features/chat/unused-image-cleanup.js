// Queue removal can happen while an unrelated run keeps the session open.
// The attachment API protects all images in an open runtime. Run snapshots
// trigger immediate retries; a slow timer also covers missed state changes
// and transient storage errors. The server checks every reference on delete.
export function createUnusedImageCleanup({ deleteImage, isRunActive,
  setTimer = setTimeout, clearTimer = clearTimeout }) {
  const pending = new Map();
  const flushing = new Map();
  const timers = new Map();
  const retryDelay = new Map();

  function stopRetry(key) {
    if (timers.has(key)) clearTimer(timers.get(key));
    timers.delete(key);
    retryDelay.delete(key);
  }

  function retryLater(key) {
    if (timers.has(key) || !pending.get(key)?.size) return;
    const delay = retryDelay.get(key) ?? 15000;
    retryDelay.set(key, Math.min(delay * 2, 60000));
    timers.set(key, setTimer(() => {
      timers.delete(key);
      void flushKey(key);
    }, delay));
  }

  async function flushKey(key) {
    if (flushing.has(key)) return flushing.get(key);
    if (!pending.get(key)?.size) { stopRetry(key); return; }
    if (isRunActive(key)) { retryLater(key); return; }
    const task = (async () => {
      const [projectId, sessionId] = key.split("/");
      const images = pending.get(key);
      const batch = [...images];
      let retryable = false;
      for (const id of batch) {
        if (isRunActive(key)) { retryable = true; break; }
        try {
          await deleteImage(projectId, sessionId, id);
          images.delete(id);
        } catch (error) {
          if (error?.code === "attachment_not_found") images.delete(id);
          else if (error?.code !== "attachment_in_use") retryable = true;
          // A lasting reference waits for a draft, queue, or run change.
        }
      }
      if (!images.size) { pending.delete(key); stopRetry(key); }
      else if (retryable || isRunActive(key)) retryLater(key);
      // A new image can arrive during this request. Schedule one pass for it.
      else if (images.size > 0 && [...images].some((id) =>
          !batch.includes(id))) retryLater(key);
      else stopRetry(key);
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
