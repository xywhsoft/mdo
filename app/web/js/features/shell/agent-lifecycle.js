// A display callback never changes the selected Agent or starts a model turn.
export function createAgentLifecycle(onError = () => {}) {
  const entries = new Map(); let selected = null, mounted = null, stopped = false;
  const release = () => {
    if (!mounted) return;
    const old = mounted; mounted = null; old.abort.abort();
    Promise.resolve(old.task).then(cleanup => typeof cleanup === "function" && cleanup(), () => {}).catch(onError);
  };
  const sync = () => {
    const row = entries.get(selected?.agent);
    const key = row && JSON.stringify(selected);
    if (mounted?.row === row && mounted?.key === key) return;
    release(); if (!row || stopped) return;
    const abort = new AbortController(), context = { ...selected };
    const task = Promise.resolve().then(() => !abort.signal.aborted && row(context, abort.signal));
    mounted = { row, key, abort, task }; task.catch(error => { if (!abort.signal.aborted) onError(error); });
  };
  return {
    select(context) { selected = context; sync(); },
    async beforeSelect(id, signal) {
      if (selected && selected.agent !== id) await entries.get(selected.agent)?.beforeDeselected?.({ ...selected }, signal);
    },
    register(id, callback) {
      if (!id || entries.has(id) || stopped) throw Error(`Duplicate or invalid Agent UI: ${id}`);
      entries.set(id, callback); sync();
      return () => { if (entries.get(id) === callback) { entries.delete(id); sync(); } };
    },
    destroy() { stopped = true; release(); entries.clear(); },
  };
}
