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
  loadOlderConversationIndex, applyLiveTimeline, revealConversationTurn } =
  await import("../app/web/js/features/chat/timeline-store.js");
const { liveConnection } = await import("../app/web/js/api/live.js");
const connected = liveConnection.isConnected;
liveConnection.isConnected = () => true;
const epoch = "a".repeat(64), sha = text => createHash("sha256").update(text).digest("hex");
const reply = data => Response.json({ ok: true, data });
const flush = () => new Promise(resolve => setImmediate(resolve));
const event = (id, kind = "model_text_delta", fields = {}) => ({ event_id: id,
  kind, text: `text ${id}`, schema_version: 6, agent_depth: 0, run_id: 1, ...fields });
function snapshot(events, fields = {}) {
  const session = fields.session_id ?? "a";
  const items_json = JSON.stringify(events.map(item => ({ ...item, aggregate_end_id: 0,
    projection_epoch: epoch, node_id: `${session}:${epoch}:${item.event_id}`, content_hash: sha(item.text) })));
  return { project_id: "qa", session_id: session, epoch, items_json,
    items_hash: sha(items_json), next_cursor: 40, latest_event_id: 40,
    next_before: events[0]?.event_id ?? 0, has_more: true, delta: false, history_lost: false, ...fields };
}
const summaries = [1, 11, 21, 31].map(id => ({ first_event_id: id, end_event_id: id + 9,
  question: `Question ${id}`, answer: `Complete answer ${id}`, state: "done" }));
const index = (items = summaries, fields = {}) => ({ epoch, items, next_before: items[0]?.first_event_id ?? 0,
  has_more: false, latest_event_id: 40, ...fields });
beforeEach(() => { clearTimeline(); clearTimelineCache(); timers.clear(); });
after(() => { clearTimeline(); liveConnection.isConnected = connected; Object.assign(globalThis, previous); });

test("an initial reply fragment loads the latest independent index including its missing user prompt", async () => {
  const calls = [];
  fetcher = async url => { calls.push(url); return reply(url.includes("/turns?")
    ? index() : snapshot([event(38), event(39), event(40)])); };
  await selectTimeline("qa", "a");
  assert.equal(timelineStore.get().data.turns.length, 0);
  const events = timelineStore.get().data.events;
  await loadOlderConversationIndex();
  const data = timelineStore.get().data;
  assert(calls.at(-1).endsWith("/turns?before=0&limit=64"));
  assert.equal(data.events, events, "summaries never download or replace transcript text");
  assert.deepEqual(data.turns.map(turn => turn.first_event_id), [1, 11, 21, 31]);
  assert.equal(data.turns.at(-1).answer, "Complete answer 31");
  assert.equal(data.indexHasMore, false);
  assert.equal(data.cursor, 40);
});

test("a fragment without a user marker still requests navigation when its text page has no older cursor", async () => {
  fetcher = async url => reply(url.includes("/turns?") ? index()
    : snapshot([event(40)], { has_more: false }));
  await selectTimeline("qa", "a");
  assert.equal(timelineStore.get().data.indexHasMore, true);
  await loadOlderConversationIndex();
  assert.equal(timelineStore.get().data.turns.length, 4);
});

test("a validated input anchor displays a cold reply's mark before the older index is available", async () => {
  fetcher = async () => reply(snapshot([event(38, "model_text_delta", { agent_turn: 4,
    action_prompt: { kind: "agent_start", agent_depth: 0, schema_version: 6, event_id: 31,
      user_message_sequence: 4, text: "Anchored question", text_truncated: false, projection_epoch: epoch } })]));
  await selectTimeline("qa", "a");
  const data = timelineStore.get().data;
  assert.deepEqual(data.turns.map(turn => turn.first_event_id), [31]);
  assert.equal(data.turns[0].question, "Anchored question");
  assert.equal(data.events[0].kind, "model_text_delta", "the anchor does not insert an extra input into the transcript");
  assert.equal(data.indexInitialized, false);
});

test("partial transcript refreshes retain complete indexed summaries and live input adds a new mark", async () => {
  fetcher = async url => reply(url.includes("/turns?") ? index()
    : snapshot([event(31, "agent_start", { user_message_sequence: 4, text: "Question 31" }), event(32)]));
  await selectTimeline("qa", "a"); await loadOlderConversationIndex();
  assert.equal(timelineStore.get().data.turns.at(-1).answer, "Complete answer 31");
  applyLiveTimeline({ project_id: "qa", session_id: "a", epoch, next_cursor: 40, latest_event_id: 40, items: [] });
  assert.equal(timelineStore.get().data.turns.at(-1).answer, "Complete answer 31");
  applyLiveTimeline({ project_id: "qa", session_id: "a", epoch, next_cursor: 42, latest_event_id: 42,
    items: [event(41, "agent_start", { user_message_sequence: 5, text: "Live question" }), event(42)] });
  assert.deepEqual(timelineStore.get().data.turns.map(turn => turn.first_event_id), [1, 11, 21, 31, 41]);
  assert.equal(timelineStore.get().data.turns.at(-1).question, "Live question");
});

test("jumping from the independent index fetches just that turn and preserves summaries across transcript gaps", async () => {
  const calls = [];
  fetcher = async url => { calls.push(url); return reply(url.includes("/turns?") ? index()
    : url.includes("/events?") ? { items: [event(1, "agent_start", { user_message_sequence: 1 }), event(2)], next_cursor: 10 }
      : snapshot([event(38), event(39), event(40)])); };
  await selectTimeline("qa", "a"); await loadOlderConversationIndex(); calls.length = 0;
  await revealConversationTurn(1);
  assert.equal(calls.length, 1); assert(calls[0].includes("/events?after=0&limit=32&epoch="));
  const data = timelineStore.get().data;
  assert.equal(data.cursor, 40);
  assert.deepEqual(data.events.map(item => item.event_id), [1, 2, 38, 39, 40]);
  assert.deepEqual(data.turns.map(turn => turn.answer), summaries.map(turn => turn.answer));
  assert.equal(data.turns[0].end_event_id, 10);
});

test("switching conversations releases an old index read and cannot populate the new navigation", async () => {
  fetcher = async () => reply(snapshot([event(40)]));
  await selectTimeline("qa", "a"); let finish, signal;
  fetcher = (_url, options) => { signal = options.signal; return new Promise(resolve => { finish = resolve; }); };
  const pending = assert.rejects(loadOlderConversationIndex(), { name: "AbortError" }); await flush();
  fetcher = async () => reply(snapshot([event(7, "agent_start", { user_message_sequence: 1, text: "Other conversation" })],
    { session_id: "b", has_more: false }));
  await selectTimeline("qa", "b"); await pending;
  assert(signal.aborted);
  finish(reply(index())); await flush();
  assert.equal(timelineStore.get().data.sessionId, "b");
  assert.deepEqual(timelineStore.get().data.turns.map(turn => turn.question), ["Other conversation"]);
});
