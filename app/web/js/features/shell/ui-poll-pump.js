// Serialize queue drains across hints, fallback timers and page lifecycle.
// A wakeup only requests another read; the caller still owns action validation.
export function createUiPollPump({ poll, delay, setTimer = setTimeout, clearTimer = clearTimeout, onError = () => {} }) {
  let stopped = false, running = false, wanted = false, timer = null, timerRevision = 0;
  const clear = () => { ++timerRevision; if (timer !== null) clearTimer(timer); timer = null; };
  async function run() {
    if (stopped) return;
    if (running) { wanted = true; return; }
    clear(); running = true; wanted = false;
    try { await poll(); }
    catch (error) { try { onError(error); } catch { /* Keep the fallback alive. */ } }
    finally {
      running = false;
      if (!stopped) {
        if (wanted) void run();
        else {
          const revision = ++timerRevision;
          timer = setTimer(() => { if (stopped || revision !== timerRevision) return; timer = null; void run(); }, delay());
        }
      }
    }
  }
  return Object.freeze({
    wake() { if (!stopped) { clear(); if (running) wanted = true; else void run(); } },
    stop() { stopped = true; wanted = false; clear(); },
  });
}
