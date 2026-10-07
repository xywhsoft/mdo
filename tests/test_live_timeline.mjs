import assert from "node:assert/strict";
import test, { after, beforeEach } from "node:test";

const previous = { window: globalThis.window, document: globalThis.document };
const timers = new Map();
let timerId = 0;
globalThis.window = Object.assign(new EventTarget(), {
  setTimeout(fn, delay) { timers.set(++timerId, { fn, delay }); return timerId; },
  clearTimeout(id) { timers.delete(id); },
});
globalThis.document = Object.assign(new EventTarget(), { hidden: false });
const { timelineStore, applyLiveTimeline, refreshSelectedTimeline, clearTimeline,
  selectTimeline, isTransientTimelineError } =
  await import("../app/web/js/features/chat/timeline-store.js");
const event = (id, fields = {}) => ({ event_id: id, kind: "model_text_delta", text: `part ${id}`, ...fields });
function packet(items, fields = {}) {
  return { type: "events", project_id: "qa", session_id: "a", items,
    next_cursor: items.at(-1)?.event_id ?? 0, latest_event_id: items.at(-1)?.event_id ?? 0, ...fields };
}
beforeEach(() => {
  clearTimeline(); timers.clear();
  timelineStore.setData({ projectId: "qa", sessionId: "a", cursor: 1,
    latestEventId: 1, historyLost: false, events: [event(1)] });
});
after(() => { clearTimeline(); Object.assign(globalThis, previous); });

test("replay/live overlap is deduplicated without rewinding the cursor", () => {
  applyLiveTimeline(packet([event(1), event(2), event(3)]));
  applyLiveTimeline(packet([event(1), event(2)]));
  assert.deepEqual(timelineStore.get().data.events.map((item) => item.event_id), [1, 2, 3]);
  assert.equal(timelineStore.get().data.cursor, 3);
});

test("temporary background read failures retain messages and back off without error cards", async () => {
  const oldFetch = globalThis.fetch;
  globalThis.fetch = async () => { throw new TypeError("offline"); };
  try {
    await refreshSelectedTimeline();
    assert.equal(timelineStore.get().status, "ready");
    assert.equal(timelineStore.get().error, null);
    assert.equal(timelineStore.get().data.events[0].text, "part 1");
    const first = [...timers.values()].find(timer => timer.delay === 5000);
    assert.ok(first); await first.fn();
    assert.ok([...timers.values()].some(timer => timer.delay === 10000));
  } finally { globalThis.fetch = oldFetch; }
});

test("an unchanged successful replay clears a previous read error", async () => {
  const oldFetch = globalThis.fetch;
  timelineStore.setError(new Error("previous read failed"));
  globalThis.fetch = async () => Response.json({ ok: true,
    data: { items: [], next_cursor: 1, latest_event_id: 1 } });
  try {
    await refreshSelectedTimeline();
    assert.equal(timelineStore.get().status, "ready");
    assert.equal(timelineStore.get().error, null);
    assert.equal(timelineStore.get().data.events.length, 1);
  } finally { globalThis.fetch = oldFetch; }
});

test("cold timeline loading retries a temporary failure and then restores its history", async () => {
  const oldFetch = globalThis.fetch;
  let offline = true;
  globalThis.fetch = async path => {
    if (offline) throw new TypeError("offline");
    return Response.json({ ok: true, data: path.includes("/turns?")
      ? { items: [{ first_event_id: 1, end_event_id: 1 }], latest_event_id: 1, has_more: false }
      : { items: path.includes("after=0") ? [event(1)] : [], next_cursor: 1, latest_event_id: 1 } });
  };
  try {
    await selectTimeline("qa", "cold");
    assert.equal(timelineStore.get().status, "ready");
    assert.equal(timelineStore.get().data.initializing, true);
    const retry = [...timers.values()].find(timer => timer.delay === 5000);
    assert.ok(retry);
    offline = false; await retry.fn();
    assert.equal(timelineStore.get().data.initializing, false);
    assert.equal(timelineStore.get().data.cursor, 1);
    assert.equal(timelineStore.get().data.events[0].text, "part 1");
  } finally { globalThis.fetch = oldFetch; }
});

test("permanent permission failures remain visible instead of being retried silently", async () => {
  const oldFetch = globalThis.fetch;
  globalThis.fetch = async () => Response.json({ ok: false,
    error: { code: "access_denied", message: "No permission" } }, { status: 403 });
  try {
    await selectTimeline("qa", "denied");
    assert.equal(timelineStore.get().status, "error");
    assert.equal(timelineStore.get().error.code, "access_denied");
    assert.equal(timers.size, 0);
    assert.equal(isTransientTimelineError({ status: 429 }), true);
    assert.equal(isTransientTimelineError({ status: 403 }), false);
  } finally { globalThis.fetch = oldFetch; }
});

test("a newer push prevents an in-flight HTTP fallback overwriting it", async () => {
  let resolve;
  const oldFetch = globalThis.fetch;
  globalThis.fetch = () => new Promise((done) => { resolve = done; });
  try {
    const request = refreshSelectedTimeline();
    applyLiveTimeline(packet([event(2)]));
    resolve(Response.json({ ok: true, data: { items: [event(2, { text: "stale" })],
      next_cursor: 2, latest_event_id: 2 } }));
    await request;
    assert.equal(timelineStore.get().data.events.at(-1).text, "part 2");
  } finally { globalThis.fetch = oldFetch; }
});

test("intentional clear removes its range but additional streaming never evicts earlier messages", () => {
  applyLiveTimeline(packet([event(2, { kind: "history_truncated", source_event_id: 1 }), event(3)]));
  assert.deepEqual(timelineStore.get().data.events.map((item) => item.event_id), [2, 3]);
  applyLiveTimeline(packet(Array.from({ length: 650 }, (_, i) => event(i + 4))));
  assert.equal(timelineStore.get().data.events.length, 652);
  assert.equal(timelineStore.get().data.historyLost, false);
});

test("wrong session and malformed cursors cannot mutate the timeline", () => {
  applyLiveTimeline(packet([event(2)], { session_id: "b" }));
  applyLiveTimeline(packet([event(2)], { next_cursor: -1 }));
  applyLiveTimeline(packet([event(2)], { latest_event_id: 1 }));
  assert.equal(timelineStore.get().data.cursor, 1);
});
