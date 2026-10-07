import { isTransientReadError } from "../../api/read-recovery.js";

// Cancellation is idempotent for one exact run. Keep that intent through a
// connection loss, then inspect its acknowledgement before repeating DELETE.
// This deliberately does not retry new runs, tools or other mutations.
export function createRunStopController({ cancel, read, canWrite = () => true,
  onChange = () => {}, onAccepted = () => {}, onError = () => {},
  setTimer = setTimeout, clearTimer = clearTimeout, random = Math.random }) {
  const pending = new Map();
  let disposed = false;
  const current = entry => !disposed && pending.get(entry.owner.id) === entry;
  const matches = (run, owner) => run?.id === owner.id &&
    run.project_id === owner.project_id && run.session_id === owner.session_id &&
    typeof run.terminal === "boolean" && typeof run.cancel_requested === "boolean";
  function remove(entry) {
    clearTimer(entry.timer);
    pending.delete(entry.owner.id);
    onChange();
  }
  function accept(entry, run) {
    remove(entry);
    onAccepted(run, entry.owner);
  }
  function check(run, owner) {
    if (!matches(run, owner)) throw Object.assign(new Error("Invalid run stop response"),
      { code: "run_stop_invalid_response" });
  }
  async function attempt(entry) {
    if (!current(entry) || entry.busy) return;
    if (!canWrite()) {
      entry.waitingConnection = true; entry.phase = "waiting";
      onChange(); return;
    }
    clearTimer(entry.timer); entry.timer = 0;
    entry.busy = true; entry.waitingConnection = false;
    entry.phase = entry.inspect ? "checking" : "sending";
    onChange();
    try {
      if (entry.inspect) {
        const run = await read(entry.owner.id);
        if (!current(entry)) return;
        check(run, entry.owner);
        if (run.terminal || run.cancel_requested) { accept(entry, run); return; }
      }
      // A restart or target change can occur while the read is in flight.
      if (!canWrite()) { entry.waitingConnection = true; return; }
      const run = await cancel(entry.owner.id);
      if (!current(entry)) return;
      check(run, entry.owner);
      if (!run.terminal && !run.cancel_requested)
        throw Object.assign(new Error("Invalid run stop acknowledgement"),
          { code: "run_stop_invalid_response" });
      accept(entry, run);
    } catch (error) {
      if (!current(entry)) return;
      if (isTransientReadError(error) ||
          ["remote_result_unconfirmed", "invalid_response"].includes(error?.code)) {
        entry.inspect = true; entry.waitingConnection = !canWrite();
        const delay = Math.min(1000 * 2 ** Math.min(entry.retries++, 4), 15000);
        entry.timer = setTimer(() => { entry.timer = 0; void attempt(entry); },
          delay + Math.floor(delay * .2 * random()));
      } else { remove(entry); onError(error, entry.owner); }
    } finally {
      entry.busy = false;
      if (current(entry)) {
        entry.phase = entry.waitingConnection ? "waiting" : "retrying";
        onChange();
      }
    }
  }
  function clear() {
    for (const entry of pending.values()) clearTimer(entry.timer);
    pending.clear(); onChange();
  }
  return Object.freeze({
    request(run, returnFocus = false) {
      if (disposed || !run?.id || run.terminal || run.cancel_requested || pending.has(run.id)) return false;
      const owner = Object.freeze({ id: run.id, project_id: run.project_id,
        session_id: run.session_id, returnFocus });
      const entry = { owner, phase: "waiting", retries: 0, timer: 0,
        busy: false, inspect: !canWrite(), waitingConnection: !canWrite() };
      pending.set(owner.id, entry); onChange(); void attempt(entry); return true;
    },
    has: id => pending.has(id),
    phase: id => pending.get(id)?.phase ?? "",
    hasSession: (projectId, sessionId) => [...pending.values()].some(({ owner }) =>
      owner.project_id === projectId && owner.session_id === sessionId),
    observe(run) {
      const entry = pending.get(run?.id);
      if (entry && matches(run, entry.owner) && (run.terminal || run.cancel_requested)) accept(entry, run);
    },
    resume() {
      for (const entry of pending.values())
        if (entry.waitingConnection) void attempt(entry);
    },
    clear,
    dispose() { disposed = true; clear(); },
  });
}
