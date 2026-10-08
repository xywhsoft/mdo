import assert from "node:assert/strict";
import test, { after, beforeEach } from "node:test";

const previous = { document: globalThis.document, window: globalThis.window,
  setTimeout: globalThis.setTimeout, clearTimeout: globalThis.clearTimeout,
  now: Date.now, random: Math.random };
globalThis.document = Object.assign(new EventTarget(), { hidden: false });
let nextTimer = 0;
let clock = 0;
const timers = new Map();
globalThis.window = Object.assign(new EventTarget(), {
  clearTimeout(id) { timers.delete(id); },
  setTimeout(callback, delay) { const id = ++nextTimer; timers.set(id, { callback, delay }); return id; },
});
globalThis.setTimeout = window.setTimeout;
globalThis.clearTimeout = window.clearTimeout;
Date.now = () => clock;
Math.random = () => 0;
const { timelineStore, clearTimeline } = await import("../app/web/js/features/chat/timeline-store.js");
const { todoStore, selectTodo, clearTodo, selectedTodo, refreshSelectedTodo } = await import("../app/web/js/state/todo.js");
const empty = () => Response.json({ ok: true, data: { event_id: 0, items: [] } });
const plan = (eventId = 7) => Response.json({ ok: true,
  data: { event_id: eventId, items: [{ text: "Review the result", done: false }] } });
const failed = (status = 503) => Response.json({ ok: false,
  error: { code: "fixture_failure", message: "Temporary read failure" } }, { status });
const flush = () => new Promise((resolve) => setImmediate(resolve));
function tick() {
  assert.equal(timers.size, 1, "one retry timer is expected");
  const [id, timer] = timers.entries().next().value;
  timers.delete(id);
  clock += timer.delay;
  timer.callback();
  return timer.delay;
}
function observe(id) {
  timelineStore.setData({ projectId: "qa", sessionId: "a", cursor: id,
    latestEventId: id, historyLost: false, events: [{ event_id: id, kind: "tool_done",
      tool_name: "mdo.todo", success: true, agent_depth: 0 }] });
}
beforeEach(() => { clearTimeline(); clearTodo(); timers.clear(); clock = 0; });
after(() => {
  clearTimeline(); clearTodo(); Date.now = previous.now; Math.random = previous.random;
  const { now, random, ...globals } = previous;
  Object.assign(globalThis, globals);
});

test("a transient todo read recovers without a new tool event", async (context) => {
  let calls = 0;
  context.mock.method(globalThis, "fetch", async () => ++calls === 1 ? failed() : plan());
  await selectTodo("qa", "a");
  assert.equal(todoStore.get().error, null, "a recoverable read must stay quiet until its budget is exhausted");
  assert.equal(tick(), 500);
  await flush();
  assert.equal(calls, 2);
  assert.equal(todoStore.get().status, "ready");
  assert.equal(todoStore.get().error, null);
  assert.equal(todoStore.get().data.eventId, 7);
  assert.equal(timers.size, 0);
});

test("transport failures and projection lag share the same bounded retry budget", async (context) => {
  const responses = [empty(), failed(), empty(), failed(), plan()];
  let calls = 0;
  context.mock.method(globalThis, "fetch", async () => responses[calls++]);
  await selectTodo("qa", "a");
  observe(7);
  await flush();
  for (const delay of [500, 1000, 2000]) {
    assert.equal(todoStore.get().error, null);
    assert.equal(tick(), delay);
    await flush();
  }
  assert.equal(calls, 5);
  assert.equal(todoStore.get().status, "ready");
  assert.equal(todoStore.get().data.eventId, 7);
  assert.equal(timers.size, 0);
});

test("persistent failures stay quiet until six attempts and formal retry resets the budget", async (context) => {
  let calls = 0;
  let recover = false;
  context.mock.method(globalThis, "fetch", async () => { calls += 1; return recover ? plan() : failed(); });
  await selectTodo("qa", "a");
  for (const delay of [500, 1000, 2000, 4000, 8000]) {
    assert.equal(todoStore.get().error, null);
    assert.equal(tick(), delay);
    await flush();
  }
  assert.equal(calls, 6);
  assert.equal(todoStore.get().status, "error");
  assert.equal(timers.size, 0);
  observe(0);
  await flush();
  assert.equal(calls, 6);
  recover = true;
  await refreshSelectedTodo(); assert.equal(calls, 6, "ordinary refresh preserves the final notice during cooldown");
  await refreshSelectedTodo({ retry: true });
  assert.equal(todoStore.get().status, "ready");
  assert.equal(calls, 7);
});

test("network errors retry but rejected and malformed todo responses do not", async (context) => {
  context.mock.method(globalThis, "fetch", async () => { throw new TypeError("offline"); });
  await selectTodo("qa", "a");
  assert.equal(timers.size, 1);
  for (const response of [failed(400), failed(403), failed(404), failed(409),
    Response.json({ ok: true, data: null }),
    Response.json({ ok: true, data: { event_id: null, items: [] } }),
    Response.json({ ok: true, data: { event_id: -1, items: [] } }),
    Response.json({ ok: true, data: { event_id: 1, items: "invalid" } })]) {
    clearTodo();
    context.mock.method(globalThis, "fetch", async () => response);
    await selectTodo("qa", "a");
    assert.equal(todoStore.get().status, "error");
    assert.equal(timers.size, 0);
  }
});

test("an older failure cannot cover a newer tool event's healthy plan", async (context) => {
  let finishOld;
  let calls = 0;
  context.mock.method(globalThis, "fetch", async () => ++calls === 1
    ? new Promise((resolve) => { finishOld = resolve; }) : plan());
  const selection = selectTodo("qa", "a");
  observe(7);
  await flush();
  assert.equal(todoStore.get().status, "ready");
  finishOld(failed());
  await selection;
  assert.equal(todoStore.get().status, "ready");
  assert.equal(todoStore.get().error, null);
  assert.equal(todoStore.get().data.eventId, 7);
  assert.equal(timers.size, 0);
});

test("switching sessions cancels even a retry callback already queued to run", async (context) => {
  let calls = 0;
  context.mock.method(globalThis, "fetch", async () => ++calls < 3 ? failed() : plan(2));
  await selectTodo("qa", "a");
  assert.equal(timers.size, 1);
  const canceled = timers.values().next().value.callback;
  await selectTodo("qa", "b");
  assert.equal(timers.size, 1);
  const currentRetry = timers.values().next().value;
  canceled();
  await flush();
  assert.equal(calls, 2);
  assert.equal(todoStore.get().data.sessionId, "b");
  assert.equal(timers.values().next().value, currentRetry);
  assert.equal(tick(), 500);
  await flush();
  assert.equal(calls, 3);
  assert.equal(todoStore.get().status, "ready");
  assert.equal(timers.size, 0);
});

test("a forbidden read removes the old plan and can be retried without reselecting", async context => {
  let calls = 0;
  context.mock.method(globalThis, "fetch", async () => ++calls === 2 ? failed(403) : plan());
  await selectTodo("qa", "a");
  await refreshSelectedTodo();
  assert.equal(todoStore.get().status, "error");
  assert.deepEqual(todoStore.get().data.items, []);
  assert.equal(selectedTodo().sessionId, "a");
  assert.equal(timers.size, 0);
  await refreshSelectedTodo({ retry: true });
  assert.equal(calls, 3); assert.equal(todoStore.get().data.eventId, 7);
});

test("projection lag alone exhausts one budget and retains the previous plan", async context => {
  let calls = 0;
  context.mock.method(globalThis, "fetch", async () => { calls++; return plan(2); });
  await selectTodo("qa", "a"); const saved = todoStore.get().data;
  observe(7); await flush();
  for (const delay of [500, 1000, 2000, 4000, 8000]) {
    assert.equal(todoStore.get().error, null); assert.equal(todoStore.get().data, saved);
    assert.equal(tick(), delay); await flush();
  }
  assert.equal(calls, 7); assert.equal(todoStore.get().error.code, "todo_not_synced");
  assert.equal(timers.size, 0);
  observe(7); await flush(); assert.equal(calls, 7);
});

test("a restored history boundary cancels old reads and allows a zero-event plan", async context => {
  let calls = 0, finishOld;
  context.mock.method(globalThis, "fetch", async () => {
    calls++;
    return calls === 1 ? plan(2) : calls === 2 ? new Promise(resolve => { finishOld = resolve; }) : empty();
  });
  await selectTodo("qa", "a"); observe(7); await flush();
  timelineStore.setData({ projectId: "qa", sessionId: "a", events: [
    { event_id: 9, kind: "history_truncated", source_event_id: 1 }] });
  await flush();
  assert.equal(todoStore.get().data.eventId, 0); assert.deepEqual(todoStore.get().data.items, []);
  finishOld(plan(7)); await flush();
  assert.equal(todoStore.get().data.eventId, 0); assert.equal(timers.size, 0);
});

test("a hanging read times out quietly and clearing aborts pending recovery", async context => {
  let calls = 0, firstSignal;
  context.mock.method(globalThis, "fetch", async (_url, options) => {
    if (++calls !== 1) return plan();
    firstSignal = options.signal;
    return new Promise((_resolve, reject) => options.signal.addEventListener("abort",
      () => reject(new DOMException("cancelled", "AbortError")), { once: true }));
  });
  const selection = selectTodo("qa", "a");
  assert.equal(tick(), 8000); await selection; await flush();
  assert.equal(firstSignal.aborted, true); assert.equal(todoStore.get().error, null);
  assert.equal(timers.size, 1);
  const queued = timers.values().next().value.callback;
  clearTodo(); queued(); await flush();
  assert.equal(calls, 1); assert.equal(timers.size, 0); assert.equal(todoStore.isPending(), false);
});

test("identical background plan reads keep the same state and subscribers", async context => {
  context.mock.method(globalThis, "fetch", async () => plan());
  await selectTodo("qa", "a"); const state = todoStore.get(); let notifications = 0;
  const unsubscribe = todoStore.subscribe(() => notifications++);
  try { await refreshSelectedTodo(); assert.equal(todoStore.get(), state); assert.equal(notifications, 1); }
  finally { unsubscribe(); }
});
