import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import test, { beforeEach, after } from "node:test";

const previous = { window: globalThis.window, document: globalThis.document,
  fetch: globalThis.fetch, now: Date.now, random: Math.random };
let time = 0, serial = 0, fetcher;
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
const delta = (cursor, fields = {}) => reply(page([], { delta: true,
  next_cursor: cursor, latest_event_id: cursor, ...fields }));
async function step() {
  const [id, timer] = [...timers].sort((a, b) => a[1].at - b[1].at)[0] ?? [];
  assert(timer, "a hanging timeline read must have a deadline or retry timer");
  timers.delete(id); time = timer.at; timer.fn(); await flush();
}
beforeEach(() => {
  clearTimeline(); clearTimelineCache(); timers.clear(); time = 0;
  document.hidden = false;
  window.dispatchEvent(new Event("pageshow")); timers.clear();
});
after(() => { clearTimeline(); Object.assign(globalThis, {
  window: previous.window, document: previous.document, fetch: previous.fetch });
  Date.now = previous.now; Math.random = previous.random; });

test("cold conversation JSON ignoring abort recovers quietly and ignores its late body", async () => {
  let calls = 0, finish, signal, settled = false;
  fetcher = async (url, options) => {
    if (++calls === 1) { signal = options.signal;
      return { ok: true, status: 200, headers: new Headers(),
        json: () => new Promise(resolve => { finish = resolve; }) }; }
    return url.includes("after=") ? delta(2) : reply(page([2]));
  };
  void selectTimeline("qa", "a").then(() => { settled = true; }); await flush();
  await step(); assert.equal(time, 8000); assert.equal(signal.aborted, true);
  assert.equal(timelineStore.get().data.syncError ?? null, null);
  await step(); assert.equal(time, 8500); assert.equal(settled, true);
  assert.equal(timelineStore.get().data.initializing, false);
  assert.deepEqual(timelineStore.get().data.events.map(e => e.event_id), [2]);
  finish({ ok: true, data: page([1]) }); await flush();
  assert.deepEqual(timelineStore.get().data.events.map(e => e.event_id), [2]);
});

test("cached messages remain visible through a slow refresh and automatic recovery", async () => {
  fetcher = async url => url.includes("after=") ? delta(1) : reply(page([1]));
  await selectTimeline("qa", "a"); let calls = 0, finish, settled = false;
  fetcher = () => ++calls === 1 ? new Promise(resolve => { finish = resolve; }) : reply(page([2], { delta: true }));
  void refreshSelectedTimeline().then(() => { settled = true; }); await flush();
  await step(); assert.equal(time, 8000); assert.equal(settled, false);
  assert.equal(timelineStore.get().data.events[0].text, "text 1");
  assert.equal(timelineStore.get().data.syncError ?? null, null);
  await step(); assert.equal(settled, true);
  assert.deepEqual(timelineStore.get().data.events.map(e => e.event_id), [1, 2]);
  finish(reply(page([1]))); await flush();
  assert.deepEqual(timelineStore.get().data.events.map(e => e.event_id), [1, 2]);
});

test("six uncooperative history reads end one budget with a single final read error", async () => {
  let calls = 0, settled = false;
  const failures = [], signals = [];
  const off = timelineStore.subscribe(state => { if (state.data?.syncError) failures.push(state.data.syncError); });
  fetcher = (_url, options) => { calls++; signals.push(options.signal); return new Promise(() => {}); };
  void selectTimeline("qa", "a").then(() => { settled = true; }); await flush();
  for (let i = 0; i < 15 && !settled; ++i) await step();
  off(); assert.equal(settled, true); assert.equal(calls, 6); assert.equal(time, 60000);
  assert.equal(failures.length, 1); assert.equal(failures[0].code, "network_error");
  assert(signals.every(signal => signal.aborted));
});

test("switching tasks releases an ignored abort without waiting for its deadline", async () => {
  let finish, signal, settled = false;
  fetcher = (_url, options) => { signal = options.signal; return new Promise(resolve => { finish = resolve; }); };
  void selectTimeline("qa", "a").then(() => { settled = true; }); await flush();
  fetcher = async url => url.includes("after=") ? delta(3, { session_id: "b" }) : reply(page([3], { session_id: "b" }));
  await selectTimeline("qa", "b"); await flush();
  assert.equal(settled, true); assert.equal(signal.aborted, true); assert.equal(time, 0);
  finish(reply(page([1]))); await flush();
  assert.equal(timelineStore.get().data.sessionId, "b");
  assert.deepEqual(timelineStore.get().data.events.map(e => e.event_id), [3]);
});

test("clear releases a hanging refresh and leaves no recovery work", async () => {
  fetcher = async url => url.includes("after=") ? delta(1) : reply(page([1]));
  await selectTimeline("qa", "a"); let settled = false;
  fetcher = () => new Promise(() => {});
  void refreshSelectedTimeline().then(() => { settled = true; }); await flush(); clearTimeline(); await flush();
  assert.equal(settled, true); assert.equal(timers.size, 0);
  assert.equal(timelineStore.get().data.sessionId, "");
});

test("old history retry preserves a live push and releases its loading control", async () => {
  fetcher = async url => url.includes("after=") ? delta(8) : reply(page([7, 8], { has_more: true }));
  await selectTimeline("qa", "a"); let calls = 0;
  fetcher = async () => ++calls === 1 ? Response.json({ ok: false,
    error: { code: "temporary" } }, { status: 503 }) : reply(page([5, 6], { next_cursor: 8, latest_event_id: 8 }));
  const pending = loadOlderTimeline(); await flush();
  assert.equal(timelineStore.get().data.loadingHistory, true);
  assert.equal(timelineStore.get().data.historyError ?? null, null);
  applyLiveTimeline({ type: "events", project_id: "qa", session_id: "a", epoch,
    items: [{ event_id: 9, kind: "model_text_delta", text: "live" }], next_cursor: 9, latest_event_id: 9 });
  await step(); await pending;
  assert.equal(calls, 2); assert.equal(timelineStore.get().data.loadingHistory, false);
  assert.equal(timelineStore.get().data.cursor, 9);
  assert.deepEqual(timelineStore.get().data.events.map(e => e.event_id), [5, 6, 7, 8, 9]);
});

test("page exit cancels a hanging older read and returning allows one fresh retry", async () => {
  fetcher = async url => url.includes("after=") ? delta(8) : reply(page([7, 8], { has_more: true }));
  await selectTimeline("qa", "a"); let settled = false, signal;
  fetcher = (_url, options) => { signal = options.signal; return new Promise(() => {}); };
  void loadOlderTimeline().then(() => { settled = true; }); await flush();
  window.dispatchEvent(new Event("pagehide")); await flush();
  assert.equal(settled, true); assert.equal(signal.aborted, true);
  assert.equal(timers.size, 0); assert.equal(timelineStore.get().data.loadingHistory, false);
  fetcher = async url => url.includes("before=") ? reply(page([5, 6], { next_cursor: 8, latest_event_id: 8 })) : delta(8);
  window.dispatchEvent(new Event("pageshow")); await flush();
  await loadOlderTimeline();
  assert.deepEqual(timelineStore.get().data.events.map(e => e.event_id), [5, 6, 7, 8]);
});

test("permanent history denial reports the accurate error once without retry", async () => {
  let calls = 0;
  fetcher = async () => { calls++; return Response.json({ ok: false,
    error: { code: "access_denied", message: "No permission" } }, { status: 403 }); };
  await selectTimeline("qa", "a");
  assert.equal(calls, 1); assert.equal(timelineStore.get().status, "error");
  assert.equal(timelineStore.get().error.code, "access_denied"); assert.equal(timers.size, 0);
});

test("backgrounding a cold load settles it and foregrounding restores history once", async () => {
  let calls = 0, settled = false;
  fetcher = () => { calls++; return new Promise(() => {}); };
  void selectTimeline("qa", "a").then(() => { settled = true; }); await flush();
  document.hidden = true; document.dispatchEvent(new Event("visibilitychange")); await flush();
  assert.equal(settled, true); assert.equal(timers.size, 0);
  assert.equal(timelineStore.get().data.syncError ?? null, null);
  fetcher = async url => { calls++; return url.includes("after=") ? delta(2) : reply(page([2])); };
  document.hidden = false; document.dispatchEvent(new Event("visibilitychange")); await flush();
  assert.equal(calls, 3); assert.equal(timelineStore.get().data.initializing, false);
  assert.deepEqual(timelineStore.get().data.events.map(e => e.event_id), [2]);
});

test("legacy event replay has the same bounded wait after a missing snapshot route", async () => {
  let events = 0;
  fetcher = async url => {
    if (url.includes("/conversation?")) return Response.json({ ok: false,
      error: { code: "route_not_found" } }, { status: 404 });
    if (url.includes("/turns?")) return reply({ items: [{ first_event_id: 1, end_event_id: 1 }],
      latest_event_id: 1, has_more: false });
    if (++events === 1) return new Promise(() => {});
    return reply({ items: url.includes("after=0") ? [{ event_id: 1, kind: "model_text_delta", text: "legacy" }] : [],
      next_cursor: 1, latest_event_id: 1 });
  };
  let settled = false;
  void selectTimeline("qa", "a").then(() => { settled = true; }); await flush();
  await step(); assert.equal(time, 8000); assert.equal(settled, false);
  assert.equal(timelineStore.get().data.syncError ?? null, null);
  await step(); assert.equal(time, 8500); assert.equal(settled, true);
  assert.equal(timelineStore.get().data.events[0].text, "legacy");
});
