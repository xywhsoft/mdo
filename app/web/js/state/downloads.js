// Presentation history only. Native managers remain authoritative; requests
// interrupted by a page reload are never replayed or declared successful.
const activeStates = new Set(["queued", "starting", "running", "downloading", "retrying", "verifying", "installing", "extracting", "activating", "uploading", "cancelling"]);
export const downloadActive = row => activeStates.has(row.state);
export function createDownloadRegistry(storage = null) {
  const listeners = new Set(), records = new Map(), updates = new Map(), installedBuilds = new Map(); let serial = 0;
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
  const list = target => {
    const all = [...records.values()].filter(r => r.target === target);
    const installs = all.filter(row => row.source === "install").sort((a, b) =>
      Number(downloadActive(b) || b.state === "unknown") - Number(downloadActive(a) || a.state === "unknown") || b.started - a.started);
    const archives = new Map(all.filter(row => row.source === "archive").map(row => [row.taskId, row]));
    const version = row => row.source === "archive" ? row.store?.version_id
      : row.source === "native" && row.nativeSource === "extension" ? row.versionId || archives.get(row.archiveId)?.store?.version_id : null;
    // Installation, archive staging and native transfer are three receipts for
    // one user action. Join by the pinned version/ archive identity, never name.
    const owners = new Map();
    for (const row of all) {
      const id = version(row), owner = id && installs.find(job => job.versionIds?.includes(id));
      if (owner) owners.set(row.key, owner.key);
    }
    function installation(row) {
      const children = all.filter(child => owners.get(child.key) === row.key);
      const transfers = children.filter(child => child.source === "native");
      const progress = new Map(Object.entries(row.transferProgress || {}));
      for (const child of children.filter(child => child.source === "archive")) progress.set(version(child), child.done || 0);
      // Native progress includes aria2 bytes that have not reached the archive
      // staging file yet. It wins regardless of API response ordering.
      for (const child of transfers) progress.set(version(child), child.done || 0);
      const current = transfers.find(child => downloadActive(child) || child.state === "paused");
      const phase = downloadActive(row) && row.state !== "cancelling" && current ? current.state : row.state;
      const speed = transfers.filter(downloadActive).reduce((sum, child) => sum + (child.speed || 0), 0);
      const done = progress.size ? [...progress.values()].reduce((sum, bytes) => sum + bytes, 0) : row.done;
      const transfer = current || transfers.at(-1);
      return { ...row, transfers, phase, done, speed,
        eta: speed > 0 && row.total > done ? (row.total - done) / speed : null,
        engine: transfer?.engine, fallbackReason: transfer?.fallbackReason };
    }
    return all.filter(r => !r.hidden &&
      !owners.has(r.key) &&
      !(r.source === "archive" && all.some(n => n.source === "native" && n.archiveId === r.taskId)) &&
      !(r.source === "update" && all.some(n => n.source === "native" && n.nativeSource === "update" && n.hash === r.hash)))
      .map(row => {
        if (row.source === "install") return installation(row);
        if(row.source === "native" && row.nativeSource === "transfer" && row.state === "ready") return { ...row, state: "complete" };
        if(row.source === "native" && row.nativeSource === "update" && row.state === "ready") {
          if(row.buildId && installedBuilds.get(target) >= row.buildId) return { ...row, state: "complete" };
          const update = updates.get(target);
          if(update && !update.busy && update.ready === false && ["current","available"].includes(update.status))
            return { ...row, state: update.status === "current" && update.sha256 === row.hash ? "complete" : "expired" };
        }
        if(row.source !== "native" || !row.archiveId) return row;
        const archive = records.get(`${target}:archive:${row.archiveId}`);
        return archive ? { ...row, store: archive.store, kind: archive.kind, directoryId: archive.directoryId,
          state: archive.state === "complete" ? "complete" : row.state } : row;
      }).sort((a,b) => b.started-a.started);
  };
  function install(target, operationId, patch = {}) {
    if (!/^sdk-[0-9a-f]{32}$/.test(operationId)) throw new Error("Invalid installation operation identity");
    const key = `${target}:install:${operationId}`;
    put(key, { target, source: "install", operationId, ...patch }); return records.get(key);
  }
  function observe(path, data, target = "local") {
    if (!data || typeof data !== "object") return;
    if(path === "/download-tasks" || path.startsWith("/download-tasks/")) {
      const live = new Set();
      for(const task of Array.isArray(data) ? data : []) {
        if(!/^[A-Za-z0-9_-]{32}$/.test(task.id || "") || typeof task.state !== "string") continue;
        const key = `${target}:native:${task.id}`;live.add(key);
        put(key,{target,source:"native",nativeSource:task.source,taskId:task.id,archiveId:task.archive_id || "",kind:task.kind,
          versionId:task.source === "extension" ? task.identity || "" : "",
          name:task.name,state:task.state,done:task.received || 0,total:task.bytes || 0,hash:task.sha256,buildId:task.build_id || 0,
          speed:task.speed || 0,eta:task.eta_seconds,error:task.error || "",errorCode:task.error_code || "",cleanupPending:task.cleanup_pending === true,engine:task.engine || "native",fallbackReason:task.fallback_reason || "",active:task.active === true});
      }
      for(const [key,row] of records) if(row.target===target && row.source==="native" && !live.has(key)) records.delete(key);
      publish();
    }
    if (path === "/extension-tasks" || path.startsWith("/extension-tasks/")) {
      const live = new Set();
      if(data.forgotten === true) records.delete(`${target}:archive:${path.split("/")[2]}`);
      for (const task of Array.isArray(data) ? data : [data]) {
        if (!/^[A-Za-z0-9_-]{32}$/.test(task.id || "") || !task.state) continue;
        live.add(`${target}:archive:${task.id}`);
        const state = task.state === "receiving" ? task.store ? "downloading" : "uploading" : task.state === "cancel_requested" ? "cancelling" : task.state === "ready" && task.active ? task.phase || "running" : task.state;
        put(`${target}:archive:${task.id}`, { target, source: "archive", taskId: task.id, action: task.purpose, kind: task.kind, directoryId: task.directory_id,
          name: task.name || task.alias || task.directory_id || task.id, state: state === "submitting" ? "uploading" : state,
          done: task.phase === "uploading" || task.phase === "submitting" ? task.sent || 0 : task.received || task.transferred_bytes || 0, total: task.bytes || 0,
          error: String(task.error || "").slice(0, 1000), store: task.store || null, publication: task.publication || null });
        if(task.store?.version_id)for(const parent of records.values())if(parent.target===target&&parent.source==="install"&&parent.versionIds?.includes(task.store.version_id)){
          const progress={...parent.transferProgress,[task.store.version_id]:task.received||task.transferred_bytes||0};
          put(parent.key,{transferProgress:progress,done:Object.values(progress).reduce((sum,n)=>sum+n,0)});
        }
      }
      if(path === "/extension-tasks" && Array.isArray(data)) for(const [key,row] of records)
        if(row.target === target && row.source === "archive" && !live.has(key)) records.delete(key);
      publish();
    }
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
      updates.set(target, data); publish();
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
      installedBuilds.set(target,data.build_id);publish();
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
  return { list, observe, run, install, subscribe(fn) { listeners.add(fn); fn(); return () => listeners.delete(fn); },
    hide(key) { const row=records.get(key);if(row){records.set(key,{...row,hidden:true});if(row.archiveId)records.delete(`${row.target}:archive:${row.archiveId}`);publish();} },
    clear(target) { for (const row of records.values()) if (row.target === target && !downloadActive(row) && row.state !== "ready") records.set(row.key, { ...row, hidden: true }); publish(); } };
}
let storage = null; try { storage = globalThis.sessionStorage; } catch {}
export const downloadRegistry = createDownloadRegistry(storage);
