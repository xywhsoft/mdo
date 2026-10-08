import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import test, { beforeEach, after } from "node:test";

const previous = { window: globalThis.window, document: globalThis.document,
  fetch: globalThis.fetch, now: Date.now, random: Math.random };
let time = 0, serial = 0, fetcher, connected = false;
const timers = new Map();
const flush = () => new Promise(resolve => setImmediate(resolve));
globalThis.window = Object.assign(new EventTarget(), {
  setTimeout(fn, delay) { timers.set(++serial, { fn, at: time + delay }); return serial; },
  clearTimeout(id) { timers.delete(id); },
});
globalThis.document = Object.assign(new EventTarget(), { hidden: false });
globalThis.fetch = (url, options) => fetcher(url, options);
Date.now = () => time;
Math.random = () => 0;
const { timelineStore, selectTimeline, clearTimeline, clearTimelineCache,
  refreshSelectedTimeline, loadOlderTimeline, applyLiveTimeline } =
  await import("../app/web/js/features/chat/timeline-store.js");
const { liveConnection } = await import("../app/web/js/api/live.js");
const oldConnected = liveConnection.isConnected;
liveConnection.isConnected = () => connected;
const sha = text => createHash("sha256").update(text).digest("hex");
const epoch = "a".repeat(64);
function page(ids, fields = {}) {
  const session = fields.session_id ?? "a";
  const items = ids.map(id => ({ event_id: id, aggregate_end_id: 0,
    kind: "model_text_delta", text: `text ${id}`, projection_epoch: epoch,
    node_id: `${session}:${epoch}:${id}`, content_hash: sha(`text ${id}`) }));
  const items_json = JSON.stringify(items);
  return { project_id: "qa", session_id: session, epoch, next_cursor: ids.at(-1) ?? 0,
    latest_event_id: ids.at(-1) ?? 0, next_before: ids[0] ?? 0, delta: false,
    has_more: false, history_lost: false, ...fields, items_json, items_hash: sha(items_json) };
}
const reply = data => Response.json({ ok: true, data });
const unavailable = () => Response.json({ ok: false, error: { code: "temporary" } }, { status: 503 });
const push = (fields = {}) => applyLiveTimeline({ project_id: "qa", session_id: "a", epoch,
  items: [{ event_id: 9, kind: "model_text_delta", text: "live" }],
  next_cursor: 9, latest_event_id: 9, ...fields });
async function load(fields = {}) {
  connected = true;
  fetcher = async () => reply(page([7, 8], fields));
  await selectTimeline("qa", "a");
  connected = false;
}
async function step() {
  const [id, timer] = [...timers].sort((a, b) => a[1].at - b[1].at)[0] ?? [];
  assert(timer, "missing expected timer"); timers.delete(id); time = timer.at;
  timer.fn(); await flush();
}
beforeEach(() => {
  connected = false; clearTimeline(); clearTimelineCache(); timers.clear(); time = 0;
});
after(() => { clearTimeline(); liveConnection.isConnected = oldConnected;
  Object.assign(globalThis, { window: previous.window, document: previous.document, fetch: previous.fetch });
  Date.now = previous.now; Math.random = previous.random; });

test("accepted live replay releases an ignored-abort refresh immediately without retry", async () => {
  await load(); let calls = 0, signal, finish, settled = false;
  fetcher = (_url, options) => { calls++; signal = options.signal;
    return new Promise(resolve => { finish = resolve; }); };
  void refreshSelectedTimeline().then(() => { settled = true; }); await flush();
  push(); await flush();
  assert.equal(signal.aborted, true); assert.equal(settled, true);
  assert.equal(timers.size, 0); assert.equal(calls, 1);
  finish(reply(page([9], { delta: true }))); await flush();
  assert.equal(timelineStore.get().data.events.at(-1).text, "live");
  assert.equal(timelineStore.get().data.syncError ?? null, null);
});

test("accepted live replay cancels the old refresh backoff without another HTTP read", async () => {
  await load(); let calls = 0, settled = false;
  fetcher = async () => { calls++; return unavailable(); };
  void refreshSelectedTimeline().then(() => { settled = true; }); await flush();
  assert.equal([...timers.values()][0].at, 500);
  push(); await flush();
  assert.equal(settled, true); assert.equal(timers.size, 0); assert.equal(calls, 1);
});

test("a newer refresh releases its predecessor and a late denial cannot affect the new result", async () => {
  await load(); let signal, finish, calls = 0, oldSettled = false;
  fetcher = (_url, options) => { if (++calls === 1) { signal = options.signal;
    return new Promise(resolve => { finish = resolve; }); }
    return Promise.resolve(reply(page([9], { delta: true }))); };
  void refreshSelectedTimeline().then(() => { oldSettled = true; }); await flush();
  await refreshSelectedTimeline(); await flush();
  assert.equal(oldSettled, true); assert.equal(signal.aborted, true); assert.equal(calls, 2);
  assert.deepEqual(timelineStore.get().data.events.map(e => e.event_id), [7, 8, 9]);
  finish(Response.json({ ok: false, error: { code: "access_denied" } }, { status: 403 })); await flush();
  assert.equal(timelineStore.get().data.syncError ?? null, null);
  assert.equal(timelineStore.get().status, "ready");
  assert.equal(timers.size, 1); assert.equal([...timers.values()][0].at, 2500);
});

test("resubscribing on a healthy live connection releases a stale HTTP fallback", async () => {
  await load(); let signal, settled = false;
  fetcher = (_url, options) => { signal = options.signal; return new Promise(() => {}); };
  void refreshSelectedTimeline().then(() => { settled = true; }); await flush();
  connected = true; await refreshSelectedTimeline(); await flush();
  assert.equal(settled, true); assert.equal(signal.aborted, true); assert.equal(timers.size, 0);
});

test("wrong-owner and malformed live packets cannot cancel a valid refresh", async () => {
  await load(); let signal, finish, settled = false;
  fetcher = (_url, options) => { signal = options.signal; return new Promise(resolve => { finish = resolve; }); };
  const pending = refreshSelectedTimeline().then(() => { settled = true; }); await flush();
  push({ session_id: "b" }); push({ next_cursor: -1 }); push({ latest_event_id: 8 }); await flush();
  assert.equal(signal.aborted, false); assert.equal(settled, false);
  finish(reply(page([9], { delta: true }))); await pending;
  assert.equal(timelineStore.get().data.events.at(-1).text, "text 9");
});

test("live replay cancels current refresh while preserving an independent older-page retry", async () => {
  await load({ has_more: true }); let currentSignal, olderCalls = 0, settled = false;
  fetcher = (url, options) => {
    if (url.includes("before=")) return Promise.resolve(++olderCalls === 1 ? unavailable()
      : reply(page([5, 6], { next_cursor: 8, latest_event_id: 8 })));
    currentSignal = options.signal; return new Promise(() => {});
  };
  void refreshSelectedTimeline().then(() => { settled = true; }); const older = loadOlderTimeline(); await flush();
  push(); await flush();
  assert.equal(settled, true); assert.equal(currentSignal.aborted, true);
  assert.equal(timelineStore.get().data.loadingHistory, true);
  await step(); await older;
  assert.equal(olderCalls, 2); assert.equal(timelineStore.get().data.cursor, 9);
  assert.deepEqual(timelineStore.get().data.events.map(e => e.event_id), [5, 6, 7, 8, 9]);
  assert.equal(timelineStore.get().data.loadingHistory, false); assert.equal(timers.size, 0);
});

test("cancel before the transport microtask starts performs no ghost HTTP read", async () => {
  await load(); let calls = 0, settled = false;
  fetcher = () => { calls++; return new Promise(() => {}); };
  void refreshSelectedTimeline().then(() => { settled = true; }); push(); await flush();
  assert.equal(settled, true); assert.equal(calls, 0); assert.equal(timers.size, 0);
});

test("legacy event fallback also releases immediately when superseded by live replay", async () => {
  timelineStore.setData({ projectId: "qa", sessionId: "a", cursor: 8,
    latestEventId: 8, historyLost: false, events: [{ event_id: 8, kind: "model_text_delta", text: "legacy" }], turns: [] });
  let signal, settled = false;
  fetcher = (url, options) => { assert(url.includes("/events?after=8")); signal = options.signal;
    return new Promise(() => {}); };
  void refreshSelectedTimeline().then(() => { settled = true; }); await flush(); push(); await flush();
  assert.equal(settled, true); assert.equal(signal.aborted, true); assert.equal(timers.size, 0);
  assert.deepEqual(timelineStore.get().data.events.map(e => e.event_id), [8, 9]);
});
