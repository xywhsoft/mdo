import assert from "node:assert/strict";
import test, { after, beforeEach } from "node:test";

const previous = { window: globalThis.window, document: globalThis.document };
globalThis.window = Object.assign(new EventTarget(), { setTimeout: () => 1, clearTimeout() {} });
globalThis.document = Object.assign(new EventTarget(), { hidden: false });
const { timelineStore, applyLiveTimeline, refreshSelectedTimeline, clearTimeline } =
  await import("../app/web/js/features/chat/timeline-store.js");
const event = (id, fields = {}) => ({ event_id: id, kind: "model_text_delta", text: `part ${id}`, ...fields });
function packet(items, fields = {}) {
  return { type: "events", project_id: "qa", session_id: "a", items,
    next_cursor: items.at(-1)?.event_id ?? 0, latest_event_id: items.at(-1)?.event_id ?? 0, ...fields };
}
beforeEach(() => timelineStore.setData({ projectId: "qa", sessionId: "a", cursor: 1,
  latestEventId: 1, historyLost: false, events: [event(1)] }));
after(() => { clearTimeline(); Object.assign(globalThis, previous); });

test("replay/live overlap is deduplicated without rewinding the cursor", () => {
  applyLiveTimeline(packet([event(1), event(2), event(3)]));
  applyLiveTimeline(packet([event(1), event(2)]));
  assert.deepEqual(timelineStore.get().data.events.map((item) => item.event_id), [1, 2, 3]);
  assert.equal(timelineStore.get().data.cursor, 3);
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
