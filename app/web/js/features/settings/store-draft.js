// One writer per editor. Edits made during a save are persisted in a subsequent
// revision, and a failed CAS never advances the saved snapshot.
export function createStoreDraft({ id, revision = "", snapshot, save, onStatus = () => {}, delay = 600, existing = false }) {
  let saved = existing ? JSON.stringify(snapshot()) : null, touched = false, timer = null, running = null, disposed = false, failure = null;
  async function write() {
    while (!disposed) {
      const draft = snapshot(), encoded = JSON.stringify(draft);
      if (saved === encoded) { failure = null; return; }
      onStatus("saving");
      try {
        const result = await save({ action: "draft_save", draft: { id, ...draft }, revision });
        revision = result.revision; saved = encoded; failure = null; onStatus("saved");
      } catch (error) { failure = error; onStatus("failed", error); throw error; }
    }
  }
  function flush() {
    clearTimeout(timer); timer = null;
    if (disposed) return Promise.resolve();
    if (!running) running = write().finally(() => { running = null; });
    return running;
  }
  return {
    id,
    get revision() { return revision; },
    get pending() { return Boolean(timer || running || failure || touched && saved !== JSON.stringify(snapshot())); },
    schedule() { if (disposed) return;touched = true;clearTimeout(timer);timer = setTimeout(() => { void flush().catch(() => {}); }, delay); },
    flush,
    dispose() { disposed = true; clearTimeout(timer); timer = null; },
  };
}
