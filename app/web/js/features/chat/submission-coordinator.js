import { ApiError } from "../../api/client.js";
import { createRequestRecovery } from "../../api/request-recovery.js";
import { isTransientReadError } from "../../api/read-recovery.js";

// All asynchronous results are scoped to the original device and message.
// A missing ACK causes reads; it is never permission to resend a user input.
export function createSubmissionCoordinator({ scope, rows, status, read, cancel,
  prepareCancel = async () => {},
  changed = () => {}, failure = () => {},
  setTimer = setTimeout, clearTimer = clearTimeout, now = Date.now,
  createRecovery = createRequestRecovery }) {
  const reading = new Set(), stopping = new Map(), checked = new Map(), recoveries = new Set();
  let owner = scope(), timer = 0, destroyed = false;
  function check() {
    if (owner === scope()) return;
    for (const recovery of recoveries) recovery.dispose();
    recoveries.clear(); reading.clear(); stopping.clear(); checked.clear(); owner = scope();
  }
  const identity = row => `${row.projectId}/${row.sessionId}/${row.id}`;
  function current(row, origin) { return !destroyed && scope() === origin && rows().some(item => identity(item) === identity(row)); }
  function observe(row, value, origin) {
    if (!current(row, origin) || !value) return;
    if (JSON.stringify(row.receipt) === JSON.stringify(value)) return;
    status(row.id, value); changed(row, value);
  }
  async function inspect(row) {
    const id = identity(row), origin = owner;
    reading.add(id); checked.set(id, now());
    try {
      const value = await read(row);
      if (value) {
        observe(row, value, origin);
      } else if (row.missing) observe(row, { state: "starting", phase: "unconfirmed" }, origin);
    } catch (error) {
      if (error?.name !== "AbortError" && current(row, origin))
        observe(row, { state: "starting", phase: "unconfirmed" }, origin);
    } finally { if (scope() === origin) { reading.delete(id); schedule(); } }
  }
  function schedule() {
    if (destroyed) return;
    check();
    if (timer) return;
    timer = setTimer(() => { timer = 0; tick(); }, 750);
  }
  function tick() {
    if (destroyed) return;
    check();
    const values = rows().filter(row => row.durable && row.sessionId && !row.error);
    for (const row of values) {
      const id = identity(row);
      if (reading.has(id) || stopping.has(id) || now() - (checked.get(id) || 0) < 2000 ||
          ["failed", "completed"].includes(row.displayPhase)) continue;
      // Unconfirmed reads remain recoverable, but never spin the UI or run.
      if (row.displayPhase === "unconfirmed" && now() - (checked.get(id) || 0) < 10000) continue;
      void inspect(row);
    }
    if (values.length) schedule();
  }
  async function stop(row) {
    check(); const id = identity(row), origin = owner;
    if (stopping.has(id)) return stopping.get(id);
    const recovery = createRecovery(); recoveries.add(recovery);
    // History may take ownership while Stop is in flight. That handoff must
    // not cancel the cancellation itself; only a device epoch change may.
    const assert = () => { recovery.assertActive(); if (destroyed || scope() !== origin) throw new DOMException("Message target changed", "AbortError"); };
    const operation = Promise.resolve().then(async () => {
      status(row.id, { state: "starting", phase: "stopping", cancel_requested: true });
      assert(); await prepareCancel(row, assert); assert();
      let value = await cancel(row); assert(); observe(row, value, origin);
      if (!value.terminal) value = await recovery.request(async () => {
        assert(); const next = await read(row); assert();
        if (!next?.terminal) throw new ApiError("Waiting for message cancellation", { code: "session_runtime_busy", status: 409 });
        return next;
      }, { retryable: error => isTransientReadError(error) || error?.code === "session_runtime_busy" });
      assert(); observe(row, value, origin);
      if (!current(row, origin)) changed(row, value);
      // Stop leaves the message in the conversation, just like a recorded
      // input. Editing is a separate user action, never an automatic restore.
      return value;
    }).catch(error => {
      if (error?.name !== "AbortError" && current(row, origin)) {
        observe(row, { state: "starting", phase: "unconfirmed", cancel_requested: true }, origin);
        failure(row, error);
      }
    }).finally(() => {
      recovery.dispose(); recoveries.delete(recovery);
      if (scope() === origin) { stopping.delete(id); checked.set(id, now()); schedule(); }
    });
    stopping.set(id, operation); return operation;
  }
  return {
    schedule, stop,
    busy: row => stopping.has(identity(row)),
    hasSession(projectId, sessionId) { check(); return [...stopping.keys()].some(id => id.startsWith(`${projectId}/${sessionId}/`)); },
    destroy() { destroyed = true; if (timer) clearTimer(timer);
      for (const recovery of recoveries) recovery.dispose(); recoveries.clear(); stopping.clear(); },
  };
}
