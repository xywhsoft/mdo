import assert from "node:assert/strict";
import test from "node:test";
import { createSubmissionState } from "../app/web/js/features/chat/submission-state.js";
import { createSubmissionCoordinator } from "../app/web/js/features/chat/submission-coordinator.js";

const input = (id = "a") => ({ id, projectId: "p", sessionId: "s", text: "same text", attachments: [] });
const snapshot = () => ({ projectId: "p", sessionId: "s", events: [], entries: [], active: false });
test("pre-start cancellation has a terminal state and cannot be rolled back by stale queue reads", () => {
  const model = createSubmissionState(() => "device"), state = snapshot();
  model.begin(input()); state.entries = [{ ...input(), state: "sending" }]; model.reconcile(state);
  model.status("a", { state: "cancelled", phase: "cancelled", terminal: true, cancel_requested: true, executed: false });
  const row = model.reconcile(state).visible[0];
  assert.equal(row.displayPhase, "cancelled");
  model.status("a", { state: "starting", phase: "preparing" });
  assert.equal(model.reconcile(state).visible[0].displayPhase, "cancelled");
  assert.equal(model.begin(input("b")).id, "b", "a deliberate resend uses a new identity even for identical text");
});

test("missing queue input becomes a recoverable confirmation state instead of preparing forever", () => {
  const model = createSubmissionState(() => "local"), state = snapshot();
  model.begin(input()); state.entries = [{ ...input(), state: "sending" }]; model.reconcile(state);
  state.entries = [];
  assert.equal(model.reconcile(state).visible[0].displayPhase, "unconfirmed");
  state.events = [{ kind: "agent_start", queue_item_id: "b", event_id: 30 }]; state.active = true;
  assert.equal(model.reconcile(state).visible[0].displayPhase, "unconfirmed");
});

test("reply anchors and prior history knowledge prevent duplicate inputs after paging or reload", () => {
  const model = createSubmissionState(() => "local"), state = snapshot();
  state.entries = [{ ...input(), state: "sending" }]; model.reconcile(state);
  state.events = [{ kind: "model_text_delta", event_id: 12,
    action_prompt: { kind: "agent_start", agent_depth: 0, queue_item_id: "a", event_id: 10 } }];
  assert.equal(model.reconcile(state).visible.length, 0);
  state.events = [];
  assert.equal(model.reconcile(state).visible.length, 0, "a lagging queue cannot resurrect a previously handed-off input");
});

test("a stopped input retains its insertion anchor when a newer run starts", () => {
  const model = createSubmissionState(() => "local"), state = snapshot();
  state.events = [{ event_id: 9 }]; model.begin(input()); model.reconcile(state);
  state.entries = [{ ...input(), state: "sending" }]; model.reconcile(state);
  state.entries = []; state.events.push({ kind: "agent_start", event_id: 20, queue_item_id: "b" });
  assert.equal(model.reconcile(state).visible[0].afterEventId, 9);
  model.status("a", { state: "cancelled", phase: "cancelled", terminal: true, after_event_id: 20 });
  assert.equal(model.reconcile(state).visible[0].afterEventId, 9);
  const recovered = createSubmissionState(() => "local");
  state.entries = [{ ...input(), state: "cancelled" }]; recovered.reconcile(state);
  recovered.status("a", {state:"cancelled",phase:"cancelled",terminal:true,after_event_id:9});
  assert.equal(recovered.reconcile(state).visible[0].afterEventId,9,"a reloaded cancelled input uses its durable server anchor");
});

test("device changes clear both pending messages and old history ownership", () => {
  let scope = "one"; const model = createSubmissionState(() => scope), state = snapshot();
  model.begin(input()); model.reconcile(state); scope = "two";
  assert.equal(model.reconcile(state).visible.length, 0);
  model.status("a", { phase: "cancelled" }); assert.equal(model.get("a"), undefined);
});

function coordinatorFixture(overrides = {}) {
  let owner = "one", values = [{ ...input(), durable: true, displayPhase: "preparing" }];
  const observations = [], errors = [], calls = { cancel: 0, read: 0 };
  const stopped = { id: "a", state: "cancelled", phase: "cancelled", terminal: true, cancel_requested: true, executed: false };
  const timer = () => 1;
  const recovery = () => { let disposed = false; return {
    dispose() { disposed = true; }, assertActive() { if (disposed) throw new DOMException("Disposed", "AbortError"); },
    async request(fn, options = {}) { for (let n = 0; ; n++) try { return await fn(); }
      catch (error) { if (n > 3 || !options.retryable?.(error)) throw error; } },
  }; };
  const coordinator = createSubmissionCoordinator({ scope: () => owner, rows: () => values,
    status(id, value) { observations.push(value); const row = values.find(x => x.id === id);
      if (row) { row.receipt = value; row.displayPhase = value.phase; } },
    async cancel() { calls.cancel++; return stopped; },
    async read() { calls.read++; return stopped; },
    failure(_row, error) { errors.push(error); },
    setTimer: timer, clearTimer() {}, createRecovery: recovery, ...overrides,
  });
  return { coordinator, observations, errors, calls, row: values[0], stopped,
    changeScope() { owner = "two"; values = [{ ...input(), durable: true }]; },
    handoff() { values = []; } };
}

test("two Stop clicks cancel once and leave the original message for an explicit edit", async () => {
  const f = coordinatorFixture();
  await Promise.all([f.coordinator.stop(f.row), f.coordinator.stop(f.row)]);
  assert.equal(f.calls.cancel, 1); assert.equal(f.row.text,"same text");
  assert.equal(f.observations[0].phase, "stopping"); assert.equal(f.observations.at(-1).phase, "cancelled");
  f.coordinator.destroy();
});

test("late cancellation acknowledgements cannot restore text or mutate the next device", async () => {
  let resolve; const waiting = new Promise(done => resolve = done);
  const f = coordinatorFixture({ cancel: () => waiting });
  const operation = f.coordinator.stop(f.row); await new Promise(done => setImmediate(done));
  f.changeScope(); resolve(f.stopped); await operation;
  assert.equal(f.observations.length, 1); assert.deepEqual(f.errors, []);
  f.coordinator.destroy();
});

test("cancellation racing actual execution never duplicates an input into the composer", async () => {
  const f = coordinatorFixture({ cancel: async () => ({ id: "a", state: "accepted", phase: "cancelled",
    terminal: true, cancel_requested: true, executed: true }) });
  await f.coordinator.stop(f.row);
  assert.equal(f.observations.at(-1).executed, true);
  f.coordinator.destroy();
});

test("an unconfirmed stop retains identity without starting a replacement run", async () => {
  const f = coordinatorFixture({ cancel: async () => { throw new Error("connection lost"); } });
  await f.coordinator.stop(f.row);
  assert.equal(f.observations.at(-1).phase, "unconfirmed");
  assert.equal(f.errors.length, 1); f.coordinator.destroy();
});

test("history handoff does not abandon the cancellation of the original message", async () => {
  let release; const preparing = new Promise(resolve => release = resolve);
  const f = coordinatorFixture({ prepareCancel: () => preparing });
  const operation = f.coordinator.stop(f.row); await new Promise(resolve => setImmediate(resolve));
  f.handoff(); release(); await operation;
  assert.equal(f.calls.cancel,1,"the original ID is still cancelled after its provisional row disappears");
  f.coordinator.destroy();
});
