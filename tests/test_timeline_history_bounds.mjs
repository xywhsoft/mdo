import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import test, { beforeEach, after } from "node:test";

const previous = { window: globalThis.window, document: globalThis.document, fetch: globalThis.fetch };
const timers = new Map(); let serial = 0, fetcher;
globalThis.window = Object.assign(new EventTarget(), {
  setTimeout(fn) { timers.set(++serial, fn); return serial; },
  clearTimeout(id) { timers.delete(id); },
});
globalThis.document = Object.assign(new EventTarget(), { hidden: false });
globalThis.fetch = (url, options) => fetcher(url, options);
const { timelineStore, selectTimeline, clearTimeline, clearTimelineCache,
  loadOlderTimeline, loadOlderConversationIndex, revealConversationTurn, refreshSelectedTimeline } =
  await import("../app/web/js/features/chat/timeline-store.js");
const { liveConnection } = await import("../app/web/js/api/live.js");
const oldConnected = liveConnection.isConnected;
let connected = true;
liveConnection.isConnected = () => connected;
const epoch = "a".repeat(64), replacement = "b".repeat(64);
const sha = text => createHash("sha256").update(text).digest("hex");
const flush = () => new Promise(resolve => setImmediate(resolve));
const event = (id, kind = "model_text_delta", fields = {}) => ({ event_id: id,
  kind, text: `text ${id}`, schema_version: 6, agent_depth: 0, run_id: 1, ...fields });
function page(events, fields = {}) {
  const e = fields.epoch ?? epoch;
  const items_json = JSON.stringify(events.map(item => ({ ...item, aggregate_end_id: 0,
    projection_epoch: e, node_id: `a:${e}:${item.event_id}`, content_hash: sha(item.text) })));
  return { project_id: "qa", session_id: "a", epoch: e, items_json,
    items_hash: sha(items_json), next_cursor: events.at(-1)?.event_id ?? 0,
    latest_event_id: events.at(-1)?.event_id ?? 0, next_before: events[0]?.event_id ?? 0,
    has_more: false, delta: false, history_lost: false, ...fields };
}
const reply = data => Response.json({ ok: true, data });
async function load() {
  fetcher = async () => reply(page([event(7, "agent_start", { user_message_sequence: 7 }),
    event(8)], { has_more: true }));
  await selectTimeline("qa", "a");
}
beforeEach(() => { clearTimeline(); clearTimelineCache(); timers.clear(); connected = true; });
after(() => { clearTimeline(); liveConnection.isConnected = oldConnected; Object.assign(globalThis, previous); });

test("reaching the first transcript group also completes the summary prefix without an extra GET", async () => {
  await load(); let calls = 0;
  fetcher = async url => { calls++; assert(url.includes("/conversation?before=7"));
    return reply(page([event(1, "agent_start", { user_message_sequence: 1 }), event(2)],
      { next_cursor: 8, latest_event_id: 8 })); };
  await loadOlderTimeline();
  const state = timelineStore.get().data;
  assert.equal(state.hasOlder, false);
  assert.equal(state.indexHasMore, false);
  assert.equal(state.indexBefore, 1);
  await loadOlderConversationIndex();
  assert.equal(calls, 1);
  assert.deepEqual(state.turns.map(t => t.first_event_id), [1, 7]);
});

test("late summary pages cannot rewind a stronger prefix proof from transcript loading", async () => {
  await load(); let finish;
  fetcher = url => url.includes("/turns?") ? new Promise(resolve => { finish = resolve; })
    : Promise.resolve(reply(page([event(1, "agent_start", { user_message_sequence: 1 })],
      { next_cursor: 8, latest_event_id: 8 })));
  const index = loadOlderConversationIndex(); await flush();
  await loadOlderTimeline();
  finish(reply({ epoch, items: [{ first_event_id: 4, end_event_id: 6, question: "indexed" }],
    next_before: 4, has_more: true })); await index;
  assert.equal(timelineStore.get().data.indexHasMore, false);
  assert.equal(timelineStore.get().data.indexBefore, 1);
});

test("a summary prefix ahead of the text stays intact until an overlapping page advances it", async () => {
  await load();
  fetcher = async () => reply({ epoch, items: [{ first_event_id: 2, end_event_id: 3, question: "older" }],
    next_before: 2, has_more: true });
  await loadOlderConversationIndex();
  fetcher = async () => reply(page([event(5)], { has_more: true, next_cursor: 8, latest_event_id: 8 }));
  await loadOlderTimeline();
  assert.equal(timelineStore.get().data.indexBefore, 2);
  assert.equal(timelineStore.get().data.indexHasMore, true);
  fetcher = async () => reply(page([event(1, "agent_start", { user_message_sequence: 1 })],
    { next_cursor: 8, latest_event_id: 8 }));
  await loadOlderTimeline();
  assert.equal(timelineStore.get().data.indexBefore, 1);
  assert.equal(timelineStore.get().data.indexHasMore, false);
});

test("a jump during an older page read cannot move either oldest boundary forwards", async () => {
  await load();
  fetcher = async () => reply({ epoch, items: [{ first_event_id: 2, end_event_id: 3, question: "jump target" }],
    next_before: 2, has_more: true });
  await loadOlderConversationIndex();
  let finish;
  fetcher = url => url.includes("/conversation?") ? new Promise(resolve => { finish = resolve; })
    : Promise.resolve(reply({ items: [event(2, "agent_start", { user_message_sequence: 2 }), event(3)], next_cursor: 3 }));
  const older = loadOlderTimeline(); await flush();
  await revealConversationTurn(2);
  finish(reply(page([event(5)], { has_more: true, next_cursor: 8, latest_event_id: 8 }))); await older;
  assert.equal(timelineStore.get().data.firstLoadedTurn, 2);
  assert.equal(timelineStore.get().data.indexBefore, 2);
  assert.equal(timelineStore.get().data.hasOlder, true);
  assert.deepEqual(timelineStore.get().data.events.map(e => e.event_id), [2, 3, 5, 7, 8]);
});

test("history replacement releases old transcript, index and jump reads despite ignored transport cancellation", async () => {
  await load();
  fetcher = async () => reply({ epoch, items: [{ first_event_id: 5, end_event_id: 6, question: "old jump" }],
    next_before: 5, has_more: true });
  await loadOlderConversationIndex();
  const pending = []; const signals = []; let settled = 0;
  fetcher = (url, options) => {
    if (url.includes("after=8&epoch=")) return Promise.resolve(reply(page([
      event(1, "agent_start", { user_message_sequence: 1, text: "New history" }), event(2),
    ], { epoch: replacement })));
    signals.push(options.signal);
    return new Promise(resolve => { pending.push(() => resolve(reply(url.includes("/turns?")
      ? { epoch, items: [{ first_event_id: 1, end_event_id: 4, question: "Old index" }], next_before: 1, has_more: false }
      : url.includes("/events?") ? { items: [event(5), event(6)], next_cursor: 6 }
        : page([event(1, "agent_start", { user_message_sequence: 1, text: "Old history" })],
          { next_cursor: 8, latest_event_id: 8 })))); });
  };
  const operations = [loadOlderTimeline(), loadOlderConversationIndex(), revealConversationTurn(5)]
    .map(operation => Promise.resolve(operation).then(() => { settled++; }, () => { settled++; }));
  await flush(); connected = false;
  await refreshSelectedTimeline(); await flush();
  const immediate = { settled, aborted: signals.map(signal => signal.aborted),
    loadingHistory: timelineStore.get().data.loadingHistory };
  pending.forEach(finish => finish()); await Promise.allSettled(operations);
  assert.deepEqual(immediate, { settled: 3, aborted: [true, true, true], loadingHistory: false });
  const state = timelineStore.get().data;
  assert.equal(state.epoch, replacement);
  assert.deepEqual(state.events.map(e => e.event_id), [1, 2]);
  assert.equal(state.events[0].text, "New history");
  assert.deepEqual(state.turns.map(t => t.question), ["New history"]);
  assert.equal(state.indexHasMore, false);
  assert.equal(state.historyError ?? null, null);
});
