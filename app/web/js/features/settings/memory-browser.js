import { api, resourceId } from "../../api/client.js";

export function memoryCollectionPath(project) {
  return project ? `/memory/projects/${resourceId(project.id, "project")}` : "/memory/global";
}

// Read-only file browsing owns its requests. Switching scopes or files must
// never let an earlier reply replace the current selection.
export function createMemoryBrowser({ read = (path, options) => api.get(path, options) } = {}) {
  let state = { project: null, collection: null, selectedId: "", entry: null, busy: false, error: null };
  let generation = 0;
  let request = null;
  const listeners = new Set();
  function publish(patch) {
    state = { ...state, ...patch };
    for (const listener of listeners) listener(state);
  }
  async function load(project, selectedId, refresh) {
    const path = memoryCollectionPath(project);
    const changed = path !== memoryCollectionPath(state.project);
    request?.abort();
    const owner = ++generation;
    request = new AbortController();
    const options = { signal: request.signal };
    publish({ project, collection: changed ? null : state.collection,
      selectedId, entry: null, busy: true, error: null });
    try {
      const collection = refresh || !state.collection ? (await read(path, options)).data : state.collection;
      if (owner !== generation) return;
      const items = collection.items ?? [];
      const id = items.some((item) => item.id === selectedId) ? selectedId : items[0]?.id ?? "";
      publish({ collection, selectedId: id });
      if (id) {
        const result = await read(`${path}/${resourceId(id, "memory")}`, options);
        if (owner !== generation) return;
        publish({ entry: result.data });
      }
    } catch (error) {
      if (owner === generation && error?.name !== "AbortError") publish({ error });
    } finally {
      if (owner === generation) { request = null; publish({ busy: false }); }
    }
  }
  return Object.freeze({
    get: () => state,
    subscribe(listener) { listeners.add(listener); listener(state); return () => listeners.delete(listener); },
    refresh(project = state.project, selectedId = state.selectedId) { return load(project, selectedId, true); },
    select(id) { return load(state.project, id, false); },
    cancel() { ++generation; request?.abort(); request = null; publish({ busy: false }); },
  });
}
