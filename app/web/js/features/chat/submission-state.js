// One identity and lifecycle for the outbox, queue and conversation. This
// model contains no DOM and never submits/replays a run while reconciling.
export function submissionHistoryIds(events = []) {
  const ids = new Set();
  for (const event of events) {
    for (const input of [event, event.action_prompt])
      if (input?.kind === "agent_start" && !(input.agent_depth > 0) && input.queue_item_id)
        ids.add(input.queue_item_id);
  }
  return ids;
}

const terminal = new Set(["cancelled", "failed", "completed"]);
export function createSubmissionState(scope) {
  const rows = new Map(), consumed = new Map();
  let owner = scope(), serial = 0;
  const key = state => `${state.projectId}/${state.sessionId}`;
  function check() {
    if (owner === scope()) return;
    rows.clear(); consumed.clear(); owner = scope();
  }
  function put(value, projectId, sessionId) {
    let row = rows.get(value.id);
    if (row && (row.projectId !== projectId || row.sessionId && row.sessionId !== sessionId)) row = null;
    if (!row) { row = { ...value, projectId, sessionId, order: ++serial }; rows.set(value.id, row); }
    else Object.assign(row, value, { projectId, sessionId });
    return row;
  }
  function reconcile(state) {
    check();
    const sessionKey = key(state);
    let seen = consumed.get(sessionKey);
    if (!seen) { seen = new Set(); consumed.set(sessionKey, seen); }
    for (const id of submissionHistoryIds(state.events)) seen.add(id);
    // Bounded in-memory knowledge is independent of transcript pagination.
    while (consumed.size > 32) consumed.delete(consumed.keys().next().value);
    while (seen.size > 2048) seen.delete(seen.values().next().value);
    for (const id of seen) rows.delete(id);
    for (const item of state.entries ?? []) {
      if (seen.has(item.id)) continue;
      const row = put({ ...item, durable: true }, state.projectId, state.sessionId);
      if (row.phase === "unconfirmed" && !row.cancelRequested && !row.receipt?.executed) row.phase = "";
      if (item.state === "cancelled") {
        row.cancelRequested = true;
        if (!row.receipt?.terminal) row.phase = "stopping";
      }
    }
    const queued = new Set((state.entries ?? []).map(item => item.id));
    for (const [id, row] of rows) {
      if (row.projectId !== state.projectId || row.sessionId !== state.sessionId) continue;
      row.missing = row.durable && !queued.has(id);
      if (row.missing && !row.entered) rows.delete(id);
    }
    const current = [...rows.values()].filter(row => row.projectId === state.projectId &&
      (row.sessionId === state.sessionId || !row.sessionId && !state.sessionId)).sort((a, b) => a.order - b.order);
    const head = (state.entries ?? []).find(item => !seen.has(item.id) && item.state !== "cancelled")?.id ?? current[0]?.id;
    for (const row of current) {
      const entering = row.edit || row.error || row.state === "cancelled" || row.state === "sending" || row.start_claimed || row.run_id ||
        row.id === head && !state.active && !current.some(other => other !== row && other.entered);
      if (!row.entered && entering) {
        row.entered = true;
        row.afterEventId = (state.events ?? []).reduce((id, event) => Math.max(id, Number(event.event_id) || 0), 0);
      }
      row.displayPhase = row.phase || (row.error ? "failed" : !row.durable ? "saving" :
        row.missing ? "unconfirmed" : row.state === "sending" || row.id === head && !state.active ? "preparing" : "queued");
    }
    return { rows: current, visible: current.filter(row => row.entered), seen, head, owner };
  }
  return {
    reconcile, put, get: id => rows.get(id),
    values: () => [...rows.values()],
    begin(value) {
      check();
      const pending = [...rows.values()].find(row => !row.durable && !terminal.has(row.phase) &&
        row.projectId === value.projectId && row.sessionId === value.sessionId && Boolean(row.edit) === Boolean(value.edit) &&
        row.editSourceEventId === value.editSourceEventId && row.text === value.text &&
        JSON.stringify(row.attachments) === JSON.stringify(value.attachments));
      if (pending && !pending.error) return null;
      return put({ ...value, id: pending?.id || value.id, error: "", phase: value.phase || "" }, value.projectId, value.sessionId);
    },
    status(id, value) {
      const row = rows.get(id); if (!row) return;
      if (terminal.has(row.phase) && !terminal.has(value.phase)) return;
      if (row.cancelRequested && ["preparing", "running", "accepted"].includes(value.phase) && !value.terminal) return;
      row.receipt = value; row.phase = value.phase || value.state;
      if (Number.isSafeInteger(value.after_event_id) && value.after_event_id >= 0) row.afterEventId = value.after_event_id;
      if (value.cancel_requested) row.cancelRequested = true;
    },
    delete: id => rows.delete(id),
    clear() { rows.clear(); consumed.clear(); owner = scope(); },
  };
}
