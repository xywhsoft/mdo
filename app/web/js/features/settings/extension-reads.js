import { api } from "../../api/client.js";
import { createRequestRecovery } from "../../api/request-recovery.js";

// Independent read intents share the existing bounded recovery policy. A newer
// read of the same intent supersedes its predecessor; writes never enter here.
export function createExtensionReads({ get = api.get,
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
