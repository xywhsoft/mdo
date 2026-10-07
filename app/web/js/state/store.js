// Recovery is opt-in for idempotent reads. load() settles its first attempt;
// subscribers observe later recovery, within six attempts and a shared minute.
// Ordinary stores, including settings operations that can write, never retry.
export function createResourceStore(initialData = null, {
  recoverRead = null, now = Date.now, random = Math.random,
  setTimer = setTimeout, clearTimer = clearTimeout,
} = {}) {
  let state = Object.freeze({
    status: "idle",
    data: initialData,
    error: null,
    updatedAt: 0,
  });
  let requestGeneration = 0;
  let retryTimer = 0;
  let requestController = null;
  const listeners = new Set();

  function cancelRead() {
    clearTimer(retryTimer); retryTimer = 0;
    requestController?.abort(); requestController = null;
  }

  function publish(patch) {
    state = Object.freeze({ ...state, ...patch });
    for (const listener of listeners) listener(state);
    return state;
  }

  async function read(generation, loader, attempt, deadline) {
    const controller = recoverRead ? new AbortController() : null;
    requestController = controller;
    let expired = false;
    const timer = controller ? setTimer(() => {
      expired = true; controller.abort();
    }, Math.max(1, Math.min(8000, deadline - now()))) : 0;
    try {
      const data = await loader(controller?.signal);
      if (generation !== requestGeneration) return state;
      return publish({ status: "ready", data, error: null, updatedAt: now() });
    } catch (error) {
      if (generation !== requestGeneration) return state;
      if (expired) error = Object.assign(new Error("Resource read timed out"),
        { code: "network_error", cause: error });
      if (error?.name === "AbortError") return state;
      if (recoverRead?.(error) && attempt < 6) {
        const delay = Math.min(500 * 2 ** (attempt - 1), 8000);
        const wait = delay + Math.floor(delay * .2 * random());
        if (now() + wait < deadline) {
          // The first read settles promptly so catalog recovery cannot block
          // workspace startup. Keep old data, and publish only the final error.
          retryTimer = setTimer(() => {
            retryTimer = 0;
            if (generation === requestGeneration)
              void read(generation, loader, attempt + 1, deadline);
          }, wait);
          return state;
        }
      }
      return publish({ status: "error", error });
    } finally {
      clearTimer(timer);
      if (requestController === controller) requestController = null;
    }
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
      cancelRead();
      return publish({ status: "ready", data, error: null, updatedAt: now() });
    },
    setError(error) {
      requestGeneration += 1;
      cancelRead();
      return publish({ status: "error", error });
    },
    reset(data = initialData) {
      requestGeneration += 1;
      cancelRead();
      return publish({ status: "idle", data, error: null, updatedAt: 0 });
    },
    async load(loader) {
      const generation = ++requestGeneration;
      cancelRead();
      publish({ status: state.data === null ? "loading" : "refreshing", error: null });
      if (generation !== requestGeneration) return state;
      return read(generation, loader, 1, now() + 60000);
    },
  });
}
