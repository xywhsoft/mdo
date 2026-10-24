// Per-run UI choices survive WebView reloads, but never a native app restart.
// The public lifetime ID is independent of API write and account credentials.
export function createNoticePreferences(storage) {
  if (storage === undefined) { try { storage = globalThis.sessionStorage; } catch {} }
  const records = new Map(), fallback = new Map();
  try {
    for (const [scope, row] of Object.entries(JSON.parse(storage?.getItem("mdo.notices.run.v1") || "{}")).slice(-16))
      if (row && /^[0-9a-f]{32}$/.test(row.runtime) && row.values && typeof row.values === "object")
        records.set(scope, { runtime: row.runtime, values: Object.fromEntries(Object.entries(row.values).slice(-100)) });
  } catch { /* Private browsing and damaged UI history must not block notices. */ }
  function save() {
    try { storage?.setItem("mdo.notices.run.v1", JSON.stringify(Object.fromEntries([...records].filter(([, row]) => row.runtime).slice(-16)))); } catch {}
  }
  return {
    forRun(scope, runtime) {
      const id = /^[0-9a-f]{32}$/.test(runtime || "") ? runtime : "";
      // Startup renders before the native snapshot arrives. It must not erase
      // a stored choice for that still-unknown run.
      if (!id) { if (!fallback.has(scope)) fallback.set(scope, {}); return fallback.get(scope); }
      if (records.get(scope)?.runtime !== id) { records.set(scope, { runtime: id, values: {} }); save(); }
      return records.get(scope).values;
    },
    record(scope, runtime, notice, hidden = false) {
      const values = this.forRun(scope, runtime);
      Object.defineProperty(values, notice.id, { value: { read: notice.revision, ...(hidden && !notice.persistent ? { hidden: notice.revision } : {}) }, enumerable: true, configurable: true, writable: true });
      const keys = Object.keys(values); for (const key of keys.slice(0, Math.max(0, keys.length - 100))) delete values[key];
      save();
    },
  };
}
