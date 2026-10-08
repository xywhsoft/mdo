// Recovery inspection and run creation both open an exclusive Agent runtime on
// the server. Keep those requests ordered within this page for each session.
const pending = new Map();

const cancelled = () => new DOMException("Runtime request cancelled", "AbortError");
function assertActive(signal) { if (signal?.aborted) throw cancelled(); }
function waitFor(promise, signal) {
  if (!signal) return promise;
  assertActive(signal);
  return new Promise((resolve, reject) => {
    const finish = (settle, value) => {
      signal.removeEventListener("abort", abort); settle(value);
    };
    const abort = () => finish(reject, cancelled());
    signal.addEventListener("abort", abort, { once: true });
    Promise.resolve(promise).then(value => finish(resolve, value), error => finish(reject, error));
  });
}

export async function withSessionRuntime(projectId, sessionId, operation, { signal } = {}) {
  assertActive(signal);
  const key = `${projectId}/${sessionId}`;
  const previous = pending.get(key);
  let release;
  const current = new Promise((resolve) => { release = resolve; });
  // A cancelled waiter must retain the previous owner's place in the chain;
  // otherwise its follower could open a runtime before that owner finishes.
  const tail = previous ? previous.then(() => current) : current;
  pending.set(key, tail);
  try {
    if (previous) await waitFor(previous, signal);
    return await waitFor(Promise.resolve().then(() => {
      assertActive(signal); return operation();
    }), signal);
  } finally {
    release();
    void tail.then(() => { if (pending.get(key) === tail) pending.delete(key); });
  }
}
