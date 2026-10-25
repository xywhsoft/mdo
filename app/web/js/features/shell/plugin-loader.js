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
    if(!record.cleaned){
      try{await record.scope.dispose();record.cleaned=true;}
      catch{record.failed=true;await warning(record.plugin,"cleanup");return false;}
    }
    try{
      await api().call("plugin.frontend.leave",identity(record.plugin));
      active.delete(record.plugin.id);
      if(record.activated)attempted.delete(`${record.plugin.id}:${record.plugin.revision}`);
      return true;
    }catch{loaded=false;return false;} // Retry the acknowledgement, never cleanup.

  }
  async function sync() {
    if (disposed || stopped() || !canCall()) return;
    if (!hello) { await api().call("plugin.frontend.hello", { client, page }); hello = true; }
    let result = await api().call("plugin.list");
    if (disposed || stopped() || result.recovery_mode) return;
    loaded = true; let retired = false;
    const outgoing=new Set();
    for(const record of active.values()){
      const next=result.items.find(item=>item.id===record.plugin.id);
      if((record.cleaned&&!record.activated)||!next?.enabled||next.error||next.revision!==record.plugin.revision)outgoing.add(record.plugin.id);
    }
    for(let pass=0;pass<active.size;pass++)for(const record of active.values())
      if(record.plugin.manifest?.requires?.some(dep=>outgoing.has(dep.id)))outgoing.add(record.plugin.id);
    // Entries were inserted in dependency order; retire dependents first.
    for(const record of [...active.values()].reverse())if(outgoing.has(record.plugin.id)){
      const dependent=[...active.values()].some(other=>other!==record&&other.plugin.manifest?.requires?.some(dep=>dep.id===record.plugin.id));
      if(!dependent)retired=await retire(record)||retired;
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
          scope = createScope(plugin); const record = { plugin, scope, failed: false, activated: false }; active.set(plugin.id, record);
          const cleanup = await module.activate(scope.sdk); if (typeof cleanup === "function") scope.own(cleanup); record.activated=true;
          if (disposed || stopped()) await retire(record);
        } catch {
          await warning(plugin, "activation");
          if (scope) { const record = active.get(plugin.id); if (record) await retire(record); }
          else { const record={plugin,scope:{dispose:async()=>{}},cleaned:true,activated:false,failed:false};active.set(plugin.id,record);await retire(record); }
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
