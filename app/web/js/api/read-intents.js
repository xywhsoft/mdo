import { api } from "./client.js";
import { createRequestRecovery } from "./request-recovery.js";

// A newer read of the same intent cancels its predecessor. Different intents
// recover independently, within the existing bounded policy. Writes never
// enter here; callers also bind results to their current page or dialog.
export function createReadIntents({ get = api.get,
  createRecovery = createRequestRecovery } = {}) {
  const pending = new Map();
  function cancel(key) {
    if (key !== undefined) { pending.get(key)?.dispose(); pending.delete(key); return; }
    for (const recovery of pending.values()) recovery.dispose();
    pending.clear();
  }
  async function read(key, path, { isCurrent = () => true, retry = true } = {}) {
    cancel(key);
    const recovery = createRecovery();
    pending.set(key, recovery);
    function assertCurrent() {
      if (!isCurrent()) recovery.dispose();
      recovery.assertActive();
    }
    try {
      const reply = await recovery.request(signal => {
        assertCurrent(); return get(path, { signal });
      }, { retry });
      assertCurrent(); return reply;
    } finally {
      recovery.dispose();
      if (pending.get(key) === recovery) pending.delete(key);
    }
  }
  return Object.freeze({ read, cancel });
}
