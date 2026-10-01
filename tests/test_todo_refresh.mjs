import assert from "node:assert/strict";
import test, { after, beforeEach } from "node:test";

const previous = { document: globalThis.document, window: globalThis.window };
globalThis.document = Object.assign(new EventTarget(), { hidden: false });
let nextTimer = 0;
const timers = new Map();
globalThis.window = {
  clearTimeout(id) { timers.delete(id); },
  setTimeout(callback, delay) { const id = ++nextTimer; timers.set(id, { callback, delay }); return id; },
};
const { timelineStore, clearTimeline } = await import("../app/web/js/features/chat/timeline-store.js");
const { todoStore, selectTodo, clearTodo } = await import("../app/web/js/state/todo.js");
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
  timer.callback();
  return timer.delay;
}
function observe(id) {
  timelineStore.setData({ projectId: "qa", sessionId: "a", cursor: id,
    latestEventId: id, historyLost: false, events: [{ event_id: id, kind: "tool_done",
      tool_name: "mdo.todo", success: true, agent_depth: 0 }] });
}
beforeEach(() => { clearTimeline(); clearTodo(); timers.clear(); });
after(() => { clearTimeline(); clearTodo(); Object.assign(globalThis, previous); });

test("a transient todo read recovers without a new tool event", async (context) => {
  let calls = 0;
  context.mock.method(globalThis, "fetch", async () => ++calls === 1 ? failed() : plan());
  await selectTodo("qa", "a");
  assert.equal(todoStore.get().status, "error");
  assert.equal(tick(), 120);
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
  for (const delay of [120, 240, 480]) {
    assert.equal(tick(), delay);
    await flush();
  }
  assert.equal(calls, 5);
  assert.equal(todoStore.get().status, "ready");
  assert.equal(todoStore.get().data.eventId, 7);
  assert.equal(timers.size, 0);
});

test("persistent failures stop after four retries and selection resets the budget", async (context) => {
  let calls = 0;
  let recover = false;
  context.mock.method(globalThis, "fetch", async () => { calls += 1; return recover ? plan() : failed(); });
  await selectTodo("qa", "a");
  for (const delay of [120, 240, 480, 960]) {
    assert.equal(tick(), delay);
    await flush();
  }
  assert.equal(calls, 5);
  assert.equal(todoStore.get().status, "error");
  assert.equal(timers.size, 0);
  observe(0);
  await flush();
  assert.equal(calls, 5);
  recover = true;
  await selectTodo("qa", "a");
  assert.equal(todoStore.get().status, "ready");
  assert.equal(calls, 6);
});

test("network errors retry but rejected and malformed todo responses do not", async (context) => {
  context.mock.method(globalThis, "fetch", async () => { throw new TypeError("offline"); });
  await selectTodo("qa", "a");
  assert.equal(timers.size, 1);
  for (const response of [failed(400), failed(403), failed(404), failed(409),
    Response.json({ ok: true, data: null }),
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
  assert.equal(tick(), 120);
  await flush();
  assert.equal(calls, 3);
  assert.equal(todoStore.get().status, "ready");
  assert.equal(timers.size, 0);
});
