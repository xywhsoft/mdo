// A page-local, bounded LRU. Records are immutable store snapshots, never a
// second persistent conversation database. Runtime and history validation are
// still required before using a cached view to issue commands.
export function createSessionCache({ maxBytes = 24 * 1024 * 1024,
  maxEntryBytes = 4 * 1024 * 1024, maxEntries = 8 } = {}) {
  const entries = new Map();
  let bytes = 0;
  function remove(key) {
    const entry = entries.get(key);
    if (entry) bytes -= entry.bytes;
    entries.delete(key);
  }
  return Object.freeze({
    get(key) {
      const entry = entries.get(key);
      if (!entry) return null;
      entries.delete(key); entries.set(key, entry);
      return entry.data;
    },
    put(key, data) {
      remove(key);
      if (!key || !data?.sessionId || data.initializing || data.syncing) return;
      // Count UTF-8 plus a conservative allowance for parsed objects. Only
      // measure on navigation, not for every token arriving on the live feed.
      const size = new TextEncoder().encode(JSON.stringify(data)).length * 2;
      if (size > maxEntryBytes || size > maxBytes) return;
      entries.set(key, { bytes: size, data }); bytes += size;
      while (bytes > maxBytes || entries.size > maxEntries) remove(entries.keys().next().value);
    },
    delete: remove,
    clear() { entries.clear(); bytes = 0; },
    usage: () => ({ bytes, entries: entries.size }),
  });
}
