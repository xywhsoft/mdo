import { ApiError } from "./client.js";
import { isTransientReadError } from "./read-recovery.js";

// Only reads and an explicitly keyed, guarded mutation may be repeated.
// New runs and draft writes use retry:false. All phases share
// one minute by default; leaving the page cancels requests and backoff waits.
// Larger file transfers can keep their existing deadline and error category.
export function createRequestRecovery({ now = Date.now, random = Math.random,
  setTimer = setTimeout, clearTimer = clearTimeout, eventTarget = globalThis.window,
  requestMs = 8000, recoveryMs = 60000,
  timeoutError = () => new ApiError("Request timed out", { code: "network_error" }) } = {}) {
  if (!Number.isFinite(requestMs) || requestMs <= 0 || !Number.isFinite(recoveryMs) || recoveryMs <= 0 ||
      typeof timeoutError !== "function") throw new TypeError("Invalid request recovery limits");
  const deadline = now() + recoveryMs;
  let stopped = false, cancelPending = null;
  const aborted = () => new DOMException("Request cancelled", "AbortError");
  const timeout = timeoutError;
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
        Math.min(requestMs, deadline - now()));
    });
    try {
      const result = await Promise.race([Promise.resolve().then(() => {
        assertActive();
        if (controller.signal.aborted) throw timeout();
        return operation(controller.signal);
      }), limit]);
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
    async request(operation, { retry = true, mutation = false,
      retryable = isTransientReadError } = {}) {
      for (let count = 1; ; ++count) {
        try { return await attempt(operation); }
        catch (error) {
          if (stopped || error?.name === "AbortError") throw error;
          const transient = retryable(error) || (mutation &&
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

export function clientActionId() {
  return [...crypto.getRandomValues(new Uint8Array(16))]
    .map(byte => byte.toString(16).padStart(2, "0")).join("");
}
