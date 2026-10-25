// Window-local execution. The host reserves compatibility slots before import
// and releases them only after every owned cleanup has actually completed.
export function createPluginLoader({ client, api, createScope, canCall = () => true,
  stopped = () => false, importModule = url => import(url) }) {
  const page = `page-${crypto.randomUUID()}`, active = new Map(), attempted = new Set();
  let hello = false, loaded = false, cursor = {}, running = null, disposed = false;
  const identity = plugin => ({ client, page, id: plugin.id, revision: plugin.revision });
  const warning = async (plugin, phase) => {
    try { await api().call("plugin.frontend.fail", { ...identity(plugin), phase }); } catch { /* Keep the local attempt fence. */ }
  };
  async function retire(record) {
    if (record.failed) return false;
    try {
      await record.scope.dispose();
      await api().call("plugin.frontend.leave", identity(record.plugin));
      active.delete(record.plugin.id); return true;
    } catch {
      record.failed = true; await warning(record.plugin, "cleanup"); return false;
    }
  }
  async function sync() {
    if (disposed || stopped() || !canCall()) return;
    if (!hello) { await api().call("plugin.frontend.hello", { client, page }); hello = true; }
    let result = await api().call("plugin.list");
    if (disposed || stopped() || result.recovery_mode) return;
    loaded = true; let retired = false;
    for (const record of active.values()) {
      const next = result.items.find(item => item.id === record.plugin.id);
      if (!next?.enabled || next.error || next.revision !== record.plugin.revision)
        retired = await retire(record) || retired;
    }
    if (retired) result = await api().call("plugin.list");
    if (disposed || stopped() || result.pending) return;
    // Dependency order is local to the same window. Native dependencies are
    // already ready in the host before it makes this selection effective.
    const remaining = result.items.filter(p => p.enabled && !p.error && p.frontend?.startsWith("web/"));
    for (let pass = 0; pass < remaining.length + 1; pass++) {
      let progress = false;
      for (const plugin of remaining) {
        const key = `${plugin.id}:${plugin.revision}`;
        if (active.has(plugin.id) || attempted.has(key)) continue;
        const dependencies = plugin.manifest?.requires || [];
        if (dependencies.some(dep => remaining.some(p => p.id === dep.id) && !active.has(dep.id))) continue;
        try { await api().call("plugin.frontend.enter", identity(plugin)); }
        catch (error) { if (error.status === 409) { loaded = false; continue; } throw error; }
        attempted.add(key); progress = true; let scope;
        try {
          const module = await importModule(`/${plugin.frontend.slice(4)}?revision=${encodeURIComponent(plugin.revision)}`);
          if (typeof module.activate !== "function") throw Error("Missing activate(sdk)");
          scope = createScope(plugin); const record = { plugin, scope, failed: false }; active.set(plugin.id, record);
          const cleanup = await module.activate(scope.sdk); if (typeof cleanup === "function") scope.own(cleanup);
          if (disposed || stopped()) await retire(record);
        } catch {
          await warning(plugin, "activation");
          if (scope) { const record = active.get(plugin.id); if (record) await retire(record); }
          else { try { await api().call("plugin.frontend.leave", identity(plugin)); } catch { /* Hold the reservation on uncertainty. */ } }
        }
      }
      if (!progress) break;
    }
  }
  async function tick() {
    if (disposed || stopped() || !canCall()) return;
    if (loaded) {
      const changes = await api().events({ ...cursor, topic: "plugin.changed" });
      cursor = { after: changes.cursor, epoch: changes.epoch };
      if (!changes.resync_required && !changes.items?.length) return;
    }
    await sync();
  }
  return Object.freeze({
    poll() {
      if (!running) running = tick().catch(() => { loaded = false; }).finally(() => { running = null; });
      return running;
    },
    invalidate() { loaded = false; },
    destroy() {
      disposed = true;
      return Promise.allSettled([...active.values()].map(retire));
    },
  });
}
