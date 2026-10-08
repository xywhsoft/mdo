import { ApiError } from "../../api/client.js";
import { isTransientReadError } from "../../api/read-recovery.js";

// Only reads and an explicitly keyed, guarded history edit may be repeated.
// New runs and draft writes are never retried by this helper. All phases share
// one minute; leaving the page cancels both the request and its backoff wait.
export function createMessageEditRecovery({ now = Date.now, random = Math.random,
  setTimer = setTimeout, clearTimer = clearTimeout, eventTarget = globalThis.window } = {}) {
  const deadline = now() + 60000;
  let stopped = false, cancelPending = null;
  const aborted = () => new DOMException("Message edit cancelled", "AbortError");
  const timeout = () => new ApiError("Message edit request timed out", { code: "network_error" });
  function assertActive() { if (stopped) throw aborted(); }
  function dispose() { stopped = true; cancelPending?.(); eventTarget?.removeEventListener("pagehide", dispose); }
  eventTarget?.addEventListener("pagehide", dispose, { once: true });
  async function attempt(operation) {
    assertActive();
    if (now() >= deadline) throw timeout();
    const controller = new AbortController();
    let timer;
    const limit = new Promise((_, reject) => {
      // Settle our own cause before aborting fetch: a synchronous abort
      // listener must not turn a retryable timeout into user cancellation.
      cancelPending = () => { reject(aborted()); controller.abort(); };
      timer = setTimer(() => { reject(timeout()); controller.abort(); },
        Math.min(8000, deadline - now()));
    });
    try {
      const result = await Promise.race([Promise.resolve().then(() => operation(controller.signal)), limit]);
      assertActive();
      return result;
    }
    finally { clearTimer(timer); cancelPending = null; }
  }
  function wait(delay) {
    return new Promise((resolve, reject) => {
      const timer = setTimer(() => { cancelPending = null; resolve(); }, delay);
      cancelPending = () => { clearTimer(timer); cancelPending = null; reject(aborted()); };
    });
  }
  return Object.freeze({
    dispose,
    assertActive,
    async request(operation, { retry = true, mutation = false } = {}) {
      for (let count = 1; ; ++count) {
        try { return await attempt(operation); }
        catch (error) {
          if (stopped || error?.name === "AbortError") throw error;
          const transient = isTransientReadError(error) || (mutation &&
            ["remote_result_unconfirmed", "invalid_response"].includes(error?.code));
          const base = Math.min(500 * 2 ** (count - 1), 8000);
          const delay = base + Math.floor(base * .2 * random());
          if (!retry || !transient || count >= 6 || now() + delay >= deadline) throw error;
          await wait(delay);
        }
      }
    },
  });
}

export function messageEditId() {
  return [...crypto.getRandomValues(new Uint8Array(16))]
    .map(byte => byte.toString(16).padStart(2, "0")).join("");
}
