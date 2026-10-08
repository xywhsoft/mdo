// Recovery is opt-in for idempotent reads. load() settles its first attempt;
// subscribers observe later recovery, within six attempts and a shared minute.
// Ordinary stores, including settings operations that can write, never retry.
export function createResourceStore(initialData = null, {
  recoverRead = null, now = Date.now, random = Math.random,
  retainDataOnError = null,
  setTimer = setTimeout, clearTimer = clearTimeout,
  pageEvents = globalThis.window,
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
  let readPending = false;
  let recheckAt = 0;
  let readStopped = false;
  let readLoader = null;
  let readBackground = false;
  let suspendedLoader = null;
  const listeners = new Set();

  function cancelRead() {
    clearTimer(retryTimer); retryTimer = 0;
    requestController?.abort(); requestController = null;
    readPending = false;
  }

  if (recoverRead) pageEvents?.addEventListener?.("pagehide", () => {
    suspendedLoader = readPending ? { loader: readLoader, background: readBackground } : null;
    readStopped = true;
    requestGeneration += 1;
    cancelRead();
  });
  if (recoverRead) pageEvents?.addEventListener?.("pageshow", () => {
    readStopped = false;
    const loader = suspendedLoader;
    suspendedLoader = null;
    if (loader) void load(loader.loader, { background: loader.background });
  });

  function publish(patch) {
    state = Object.freeze({ ...state, ...patch });
    for (const listener of listeners) listener(state);
    return state;
  }

  async function load(loader, { background = false } = {}) {
    if (readStopped) return state;
    if (background && readPending) return state;
    // Polling must not erase a final error or restart another six-attempt
    // budget. Once exhausted, probe at most once per 30 seconds, retaining the
    // notice until a read actually succeeds. Explicit loads can retry now.
    const probing = Boolean(background && recoverRead && state.status === "error");
    if (probing && now() < recheckAt) return state;
    readLoader = loader;
    readBackground = background;
    const generation = ++requestGeneration;
    cancelRead();
    readPending = true;
    if (!probing && !(background && state.status === "ready"))
      publish({ status: state.data === null ? "loading" : "refreshing", error: null });
    if (generation !== requestGeneration) return state;
    return read(generation, loader, 1, now() + 60000, { probing, background });
  }

  async function read(generation, loader, attempt, deadline, { probing, background }) {
    const controller = recoverRead ? new AbortController() : null;
    requestController = controller;
    let expired = false;
    const timer = controller ? setTimer(() => {
      expired = true; controller.abort();
    }, Math.max(1, Math.min(8000, deadline - now()))) : 0;
    try {
      const data = await loader(controller?.signal);
      if (generation !== requestGeneration) return state;
      readPending = false; recheckAt = 0;
      if (background && state.status === "ready" && state.data === data) return state;
      return publish({ status: "ready", data, error: null, updatedAt: now() });
    } catch (error) {
      if (generation !== requestGeneration) return state;
      if (expired) error = Object.assign(new Error("Resource read timed out"),
        { code: "network_error", cause: error });
      if (error?.name === "AbortError") return state;
      if (!probing && recoverRead?.(error) && attempt < 6) {
        const delay = Math.min(500 * 2 ** (attempt - 1), 8000);
        const wait = delay + Math.floor(delay * .2 * random());
        if (now() + wait < deadline) {
          // The first read settles promptly so catalog recovery cannot block
          // workspace startup. Keep old data, and publish only the final error.
          retryTimer = setTimer(() => {
            retryTimer = 0;
            if (generation === requestGeneration)
              void read(generation, loader, attempt + 1, deadline, { probing, background });
          }, wait);
          return state;
        }
      }
      readPending = false;
      if (recoverRead) recheckAt = now() + 30000;
      const discard = retainDataOnError?.(error) === false;
      return publish({ status: "error", error,
        data: discard ? initialData : state.data,
        updatedAt: discard ? 0 : state.updatedAt });
    } finally {
      clearTimer(timer);
      // A parallel read may fail before its siblings settle. Abort only this
      // attempt's controller so those pure reads cannot outlive its retry.
      controller?.abort();
      if (requestController === controller) requestController = null;
      if (generation === requestGeneration && !retryTimer) readPending = false;
    }
  }

  return Object.freeze({
    get: () => state,
    isPending: () => readPending,
    subscribe(listener) {
      listeners.add(listener);
      listener(state);
      return () => listeners.delete(listener);
    },
    setData(data) {
      suspendedLoader = null;
      recheckAt = 0;
      requestGeneration += 1;
      cancelRead();
      return publish({ status: "ready", data, error: null, updatedAt: now() });
    },
    setError(error) {
      suspendedLoader = null;
      recheckAt = recoverRead ? now() + 30000 : 0;
      requestGeneration += 1;
      cancelRead();
      return publish({ status: "error", error });
    },
    reset(data = initialData) {
      suspendedLoader = null;
      recheckAt = 0;
      requestGeneration += 1;
      cancelRead();
      return publish({ status: "idle", data, error: null, updatedAt: 0 });
    },
    load,
  });
}
