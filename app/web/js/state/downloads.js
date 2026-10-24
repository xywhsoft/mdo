// Presentation history only. Native managers remain authoritative; requests
// interrupted by a page reload are never replayed or declared successful.
const activeStates = new Set(["queued", "running", "downloading", "installing", "extracting", "activating", "uploading", "cancelling"]);
export const downloadActive = row => activeStates.has(row.state);
export function createDownloadRegistry(storage = null) {
  const listeners = new Set(), records = new Map(); let serial = 0;
  try {
    for (const row of JSON.parse(storage?.getItem("mdo.downloads.v1") || "[]").slice(-200)) {
      if (!row || typeof row.key !== "string" || typeof row.target !== "string" || typeof row.name !== "string") continue;
      records.set(row.key, { ...row, ...(downloadActive(row) ? { state: "unknown", error: "" } : {}) });
    }
  } catch { /* Private modes and damaged history must not stop downloads. */ }
  function publish() {
    const old = [...records.values()].filter(r => !downloadActive(r));
    while (records.size > 200 && old.length) records.delete(old.shift().key);
    try { storage?.setItem("mdo.downloads.v1", JSON.stringify([...records.values()])); } catch {}
    for (const listener of listeners) { try { listener(); } catch (error) { console.error("Download view update failed", error); } }
  }
  function put(key, patch) {
    const previous = records.get(key);
    const next = { started: Date.now(), ...previous, ...patch, key };
    if (previous?.hidden && previous.state !== next.state) next.hidden = false;
    if (JSON.stringify(previous) === JSON.stringify(next)) return;
    records.set(key, next); publish();
  }
  const list = target => [...records.values()].filter(r => r.target === target && !r.hidden).sort((a, b) => b.started - a.started);
  function observe(path, data, target = "local") {
    if (!data || typeof data !== "object") return;
    if (path === "/cli-tools" && Array.isArray(data.tasks)) {
      const live = new Set();
      for (const task of data.tasks) {
        if (!task.id || !["install", "import", "publish", "recommend"].includes(task.action)) continue;
        const key = `${target}:cli:${task.id}`; live.add(key);
        put(key, { target, source: "cli", taskId: task.id, action: task.action, name: String(task.name || task.request?.id || ""),
          state: task.state, error: String(task.error || "").slice(0, 1000), done: task.done || 0, total: task.total || 0,
          submission: task.submission ? { id: task.submission.id, state: task.submission.state } : null });
      }
      for (const row of records.values()) if (row.target === target && row.source === "cli" && downloadActive(row) && !live.has(row.key))
        put(row.key, { state: "unknown", error: "" });
    }
    if (path.startsWith("/update") && typeof data.status === "string") {
      const samePackage = r => (!data.build_id || r.buildId === data.build_id) && (!data.sha256 || r.hash === data.sha256) && (!data.edition || r.edition === data.edition) && (!data.platform || r.platform === data.platform);
      for (const row of records.values()) if (row.target === target && row.source === "update" && row.state === "ready" && (!samePackage(row) || data.ready === false && !data.busy))
        put(row.key, { state: "expired", error: "" });
      let previous = [...records.values()].reverse().find(r => r.target === target && r.source === "update" && samePackage(r));
      if (["downloading", "ready", "installing"].includes(data.status)) {
        if (!previous || (["failed", "cancelled", "complete", "expired"].includes(previous.state) && data.status === "downloading")) previous = null;
        const key = previous?.key || `${target}:update:${data.build_id || 0}:${Date.now()}:${++serial}`;
        put(key, { target, source: "update", name: "mdo", state: data.status, buildId: data.build_id || previous?.buildId || 0,
          platform: data.platform || previous?.platform || "", edition: data.edition || previous?.edition || "", hash: data.sha256 || previous?.hash || "", total: data.size || previous?.total || 0, done: null, error: "" });
      } else if (previous && downloadActive(previous) && !data.busy) {
        put(previous.key, { state: data.message === "Update operation cancelled" ? "cancelled" : data.status === "error" || data.message ? "failed" : "cancelled", error: data.message || "" });
      }
    }
    if (path === "/distribution" && data.build_id) {
      const previous = [...records.values()].reverse().find(r => r.target === target && r.source === "legacy");
      if (data.busy && data.operation_id || previous && downloadActive(previous)) {
        const key = previous && downloadActive(previous) ? previous.key : `${target}:legacy:${Date.now()}:${++serial}`;
        put(key, { target, source: "legacy", resourceId: data.operation_id, name: data.operation_id || "", state: data.busy ? activeStates.has(data.stage) ? data.stage : "running" : data.error ? "failed" : data.stage === "cancelled" ? "cancelled" : "complete", error: data.error?.detail || "", done: data.done || 0, total: data.total || 0 });
      }
      for (const row of records.values()) if (row.target === target && row.source === "update" && ["installing", "unknown"].includes(row.state) && row.buildId && row.buildId <= data.build_id)
        put(row.key, { state: "complete", error: "" });
    }
    if (path === "/ecosystem") for (const row of records.values()) {
      if (row.target === target && row.source === "extension" && row.state === "unknown" && Object.hasOwn(data, String(row.resourceId)))
        put(row.key, { state: "complete", error: "" });
    }
  }
  async function run(target, metadata, operation) {
    const key = `${target}:extension:${Date.now()}:${++serial}`;
    put(key, { target, source: "extension", name: String(metadata.name || metadata.id || ""), resourceId: metadata.id, state: "running", done: null, total: 0 });
    try { const result = await operation(); put(key, { state: "complete" }); return result; }
    catch (error) { put(key, { state: !error?.status || error?.name === "AbortError" ? "unknown" : "failed", error: String(error?.message || "").slice(0, 1000) }); throw error; }
  }
  return { list, observe, run, subscribe(fn) { listeners.add(fn); fn(); return () => listeners.delete(fn); },
    clear(target) { for (const row of records.values()) if (row.target === target && !downloadActive(row) && row.state !== "ready") records.set(row.key, { ...row, hidden: true }); publish(); } };
}
let storage = null; try { storage = globalThis.sessionStorage; } catch {}
export const downloadRegistry = createDownloadRegistry(storage);
