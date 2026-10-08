import assert from "node:assert/strict";
import test, { after, beforeEach } from "node:test";

const previous = { document: globalThis.document, window: globalThis.window,
  setTimeout: globalThis.setTimeout, clearTimeout: globalThis.clearTimeout };
globalThis.document = Object.assign(new EventTarget(), { hidden: false });
let timer = 0;
const timers = new Map();
globalThis.window = Object.assign(new EventTarget(), { clearTimeout(id) { timers.delete(id); },
  setTimeout(fn) { timers.set(++timer, fn); return timer; } });
// Todo uses the shared resource store's default clock. Both clocks must be
// controlled before importing it, so this check observes its actual retries.
globalThis.setTimeout = window.setTimeout;
globalThis.clearTimeout = window.clearTimeout;
const { mergeTimelineEvents, timelineStore, refreshSelectedTimeline, clearTimeline } =
  await import("../app/web/js/features/chat/timeline-store.js");
const { todoStore, selectTodo, clearTodo } = await import("../app/web/js/state/todo.js");
const event = (id, fields = {}) => ({ event_id: id, kind: "agent_start", text: `turn ${id}`, ...fields });
const marker = (id, source) => event(id, { kind: "history_truncated", source_event_id: source });
const reply = (items, next = items.at(-1)?.event_id ?? 0, lost = false) =>
  Response.json({ ok: true, data: { items, next_cursor: next, latest_event_id: next, history_lost: lost } });
const flush = () => new Promise(resolve => setImmediate(resolve));
beforeEach(() => { clearTimeline(); clearTodo(); timers.clear(); });
after(() => { clearTimeline(); clearTodo(); Object.assign(globalThis, previous); });
function cached(events, historyLost = false) {
  timelineStore.setData({ projectId: "qa", sessionId: "a", cursor: events.at(-1)?.event_id ?? 0,
    latestEventId: events.at(-1)?.event_id ?? 0, historyLost, events });
}

test("truncate removes its cached ID range while retaining prefix, marker and reused-sequence replacement", () => {
  const original = [event(1), event(2), event(3, { user_message_sequence: 2 }), event(4)];
  const addition = [marker(5, 3), event(6, { user_message_sequence: 2 })];
  const frozen = structuredClone(original);
  const merged = mergeTimelineEvents(original, addition);
  assert.deepEqual(merged.events.map(e => e.event_id), [1, 2, 5, 6]);
  assert.equal(merged.cleared, false);
  assert.deepEqual(original, frozen);
  assert.deepEqual(mergeTimelineEvents(merged.events, [marker(7, 1), event(8)]).events
    .map(e => e.event_id), [7, 8]);
});

test("history notes without valid ID ranges remain literal and never remove cached records", () => {
  for (const source of [undefined, 0, -1, 1.5, 99, NaN]) {
    assert.deepEqual(mergeTimelineEvents([event(1)], [marker(2, source)]).events
      .map(e => e.event_id), [1, 2]);
  }
});

test("polling honors an intentional clear without reporting an unexplained history gap", async context => {
  cached([event(1), event(2)], true);
  context.mock.method(globalThis, "fetch", async () => reply([marker(3, 1), event(4)]));
  await refreshSelectedTimeline();
  assert.deepEqual(timelineStore.get().data.events.map(e => e.event_id), [3, 4]);
  assert.equal(timelineStore.get().data.historyLost, false);
});

test("a delayed older replay cannot resurrect discarded events or rewind the cursor", async context => {
  cached([event(1), event(2)]);
  let finishOld;
  let calls = 0;
  context.mock.method(globalThis, "fetch", () => ++calls === 1
    ? new Promise(resolve => { finishOld = resolve; }) : Promise.resolve(reply([marker(4, 1), event(5)])));
  const older = refreshSelectedTimeline();
  await flush();
  await refreshSelectedTimeline();
  finishOld(reply([event(3)]));
  await older;
  assert.deepEqual(timelineStore.get().data.events.map(e => e.event_id), [4, 5]);
  assert.equal(timelineStore.get().data.cursor, 5);
  assert.equal(timers.size, 1);
});

test("history clear drops an obsolete plan and invalidates its pending read", async context => {
  cached([event(1)]);
  let finishOld;
  let calls = 0;
  context.mock.method(globalThis, "fetch", () => ++calls === 1
    ? new Promise(resolve => { finishOld = resolve; })
    : Promise.resolve(Response.json({ ok: true, data: { event_id: 0, items: [] } })));
  const oldSelection = selectTodo("qa", "a");
  cached([marker(6, 1)]);
  await flush();
  finishOld(Response.json({ ok: true, data: { event_id: 3, items: [{ text: "obsolete", done: false }] } }));
  await oldSelection;
  await flush();
  assert.deepEqual(todoStore.get().data.items, []);
  assert.equal(todoStore.get().data.eventId, 0);
  assert.equal(calls, 2);
});

test("truncate may restore an earlier plan, and later todo calls still advance it", async context => {
  cached([event(1)]);
  let response = { event_id: 8, items: [{ text: "removed plan", done: true }] };
  context.mock.method(globalThis, "fetch", async () => Response.json({ ok: true, data: response }));
  await selectTodo("qa", "a");
  response = { event_id: 2, items: [{ text: "retained plan", done: false }] };
  cached([event(1), marker(10, 5)]);
  await flush();
  assert.equal(todoStore.get().data.eventId, 2);
  assert.equal(todoStore.get().data.items[0].text, "retained plan");
  response = { event_id: 12, items: [{ text: "new plan", done: false }] };
  cached([event(1), marker(10, 5), event(12, { kind: "tool_done", tool_name: "mdo.todo", success: true, agent_depth: 0 })]);
  await flush();
  assert.equal(todoStore.get().data.eventId, 12);
  assert.equal(todoStore.get().data.items[0].text, "new plan");
});

test("a post-boundary todo response inside the discarded range is retried rather than displayed", async context => {
  cached([event(1)]);
  let response = { event_id: 3, items: [{ text: "removed plan", done: false }] };
  context.mock.method(globalThis, "fetch", async () => Response.json({ ok: true, data: response }));
  await selectTodo("qa", "a");
  cached([marker(6, 1)]);
  await flush();
  assert.deepEqual(todoStore.get().data.items, []);
  response = { event_id: 0, items: [] };
  const retry = [...timers.values()].at(-1);
  assert.equal(typeof retry, "function");
  retry();
  await flush();
  assert.equal(todoStore.get().data.eventId, 0);
  assert.equal(todoStore.get().status, "ready");
});
