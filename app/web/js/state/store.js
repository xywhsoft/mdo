export function createResourceStore(initialData = null) {
  let state = Object.freeze({
    status: "idle",
    data: initialData,
    error: null,
    updatedAt: 0,
  });
  let requestGeneration = 0;
  const listeners = new Set();

  function publish(patch) {
    state = Object.freeze({ ...state, ...patch });
    for (const listener of listeners) listener(state);
    return state;
  }

  return Object.freeze({
    get: () => state,
    subscribe(listener) {
      listeners.add(listener);
      listener(state);
      return () => listeners.delete(listener);
    },
    setData(data) {
      requestGeneration += 1;
      return publish({ status: "ready", data, error: null, updatedAt: Date.now() });
    },
    setError(error) {
      requestGeneration += 1;
      return publish({ status: "error", error });
    },
    reset(data = initialData) {
      requestGeneration += 1;
      return publish({ status: "idle", data, error: null, updatedAt: 0 });
    },
    async load(loader) {
      const generation = ++requestGeneration;
      publish({ status: state.data === null ? "loading" : "refreshing", error: null });
      try {
        const data = await loader();
        if (generation !== requestGeneration) return state;
        return publish({ status: "ready", data, error: null, updatedAt: Date.now() });
      } catch (error) {
        if (error?.name === "AbortError" || generation !== requestGeneration) return state;
        return publish({ status: "error", error });
      }
    },
  });
}
