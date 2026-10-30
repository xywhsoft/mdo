// Coalesce each resource separately: a slow task/recovery read must never
// hold back a later run or queue state. Callers own request deadlines and
// error presentation; this scheduler only repeats idempotent reads.
export function createResourceRefresh({ operations, enabled = () => true,
  setTimer = setTimeout, clearTimer = clearTimeout, delay = 120 }) {
  const lanes = new Map(Object.entries(operations).map(([name, read]) =>
    [name, { read, timer: 0, running: false, pending: false }]));
  let stopped = false, paused = false;
  function arm(lane) {
    if (stopped || paused || !enabled() || lane.timer || lane.running || !lane.pending) return;
    lane.timer = setTimer(async () => {
      lane.timer = 0;
      if (stopped || paused || !enabled()) return;
      lane.pending = false;
      lane.running = true;
      try { await lane.read(); }
      catch { /* Resource owners retain and present their own read errors. */ }
      finally { lane.running = false; arm(lane); }
    }, delay);
  }
  return Object.freeze({
    request(names = [...lanes.keys()]) {
      if (stopped) return;
      for (const name of names) {
        const lane = lanes.get(name);
        if (!lane) continue;
        lane.pending = true;
        arm(lane);
      }
    },
    pause() {
      paused = true;
      for (const lane of lanes.values()) {
        clearTimer(lane.timer); lane.timer = 0;
      }
    },
    resume() { paused = false; for (const lane of lanes.values()) arm(lane); },
  });
}
