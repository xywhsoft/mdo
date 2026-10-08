import assert from "node:assert/strict";
import test from "node:test";
import { createResourceStore } from "../app/web/js/state/store.js";
import { isTransientReadError } from "../app/web/js/api/read-recovery.js";
import { api } from "../app/web/js/api/client.js";

const flush = () => new Promise(resolve => setImmediate(resolve));
function fixture(options = {}) {
  let time = 0, serial = 0;
  const timers = new Map(), events = new EventTarget(), states = [];
  const store = createResourceStore({ items: ["saved"] }, { recoverRead: isTransientReadError,
    now: () => time, random: () => 0, pageEvents: events,
    setTimer(fn, delay) { timers.set(++serial, { fn, at: time + delay }); return serial; },
    clearTimer(id) { timers.delete(id); }, ...options });
  store.subscribe(state => states.push(state));
  return { store, events, states, timers, elapsed: () => time,
    async step() {
      const [id, timer] = [...timers].sort((a, b) => a[1].at - b[1].at)[0] ?? [];
      if (!timer) return false;
      timers.delete(id); time = timer.at; timer.fn(); await flush(); return true;
    },
    async drain() {
      for (let i = 0; i < 30; ++i) if (!await this.step()) return;
      assert.fail("unbounded resource retry");
    },
  };
}

test("a read ignoring abort releases startup at eight seconds and quietly recovers", async () => {
  const f = fixture(); let calls = 0, firstSignal, finish, settled = false;
  const pending = f.store.load(signal => {
    if (++calls > 1) return { items: ["restored"] };
    firstSignal = signal; return new Promise(resolve => { finish = resolve; });
  }).then(() => { settled = true; });
  await f.step();
  assert.equal(settled, true); assert.equal(firstSignal.aborted, true);
  assert.equal(f.elapsed(), 8000); assert.equal(f.store.get().error, null);
  assert.equal(f.store.isPending(), true);
  await f.step(); await pending;
  assert.equal(f.elapsed(), 8500); assert.equal(calls, 2);
  assert.equal(f.store.get().status, "ready"); assert.deepEqual(f.store.get().data.items, ["restored"]);
  finish({ items: ["obsolete"] }); await flush();
  assert.deepEqual(f.store.get().data.items, ["restored"]);
  assert.equal(f.states.filter(state => state.status === "error").length, 0);
});

test("six uncooperative reads settle in one minute with one final error", async () => {
  const f = fixture(); let calls = 0, settled = false; const signals = [];
  void f.store.load(signal => { calls++; signals.push(signal); return new Promise(() => {}); })
    .then(() => { settled = true; });
  await f.drain();
  assert.equal(settled, true); assert.equal(calls, 6); assert.equal(f.elapsed(), 60000);
  assert.equal(f.store.get().status, "error"); assert.equal(f.store.get().error.code, "network_error");
  assert.equal(f.states.filter(state => state.status === "error").length, 1);
  assert.equal(f.store.isPending(), false); assert.equal(f.timers.size, 0);
  assert(signals.every(signal => signal.aborted));
});

test("page exit settles an ignored abort and resume reads once without old data", async () => {
  const f = fixture(); let calls = 0, finish, settled = false;
  void f.store.load(() => ++calls === 1 ? new Promise(resolve => { finish = resolve; }) : { items: ["resumed"] })
    .then(() => { settled = true; });
  f.events.dispatchEvent(new Event("pagehide")); await flush();
  assert.equal(settled, true); assert.equal(f.store.isPending(), false); assert.equal(f.timers.size, 0);
  f.events.dispatchEvent(new Event("pageshow")); await flush();
  assert.equal(calls, 2); assert.deepEqual(f.store.get().data.items, ["resumed"]);
  finish({ items: ["old"] }); await flush();
  assert.deepEqual(f.store.get().data.items, ["resumed"]);
  assert.equal(f.states.filter(state => state.status === "error").length, 0);
});

test("a replacement read settles its predecessor without clearing the new deadline", async () => {
  const f = fixture(); let oldSettled = false;
  void f.store.load(() => new Promise(() => {})).then(() => { oldSettled = true; });
  let finish;
  const next = f.store.load(() => new Promise(resolve => { finish = resolve; })); await flush();
  assert.equal(oldSettled, true); assert.equal(f.timers.size, 1); assert.equal(f.store.isPending(), true);
  finish({ items: ["new"] }); await next;
  assert.equal(f.store.isPending(), false); assert.equal(f.timers.size, 0);
});

test("reset, authoritative data and errors all release hanging loads without retries", async () => {
  for (const operation of [store => store.reset(), store => store.setData({ items: ["authoritative"] }),
    store => store.setError(Object.assign(new Error("denied"), { code: "permission_denied" }))]) {
    const f = fixture(); let settled = false;
    void f.store.load(() => new Promise(() => {})).then(() => { settled = true; });
    operation(f.store); const saved = f.store.get(); await flush();
    assert.equal(settled, true); assert.equal(f.store.get(), saved);
    assert.equal(f.store.isPending(), false); assert.equal(f.timers.size, 0);
  }
});

test("the real API response body is bounded even if JSON parsing ignores abort", async () => {
  const f = fixture(); const original = globalThis.fetch; let calls = 0, signal;
  globalThis.fetch = async (_url, options) => {
    calls++; signal = options.signal;
    if (calls === 1) return { ok: true, status: 200, headers: new Headers(), json: () => new Promise(() => {}) };
    return Response.json({ ok: true, data: { items: ["restored body"] } });
  };
  try {
    let settled = false;
    void f.store.load(async value => (await api.get("/models", { signal: value })).data).then(() => { settled = true; });
    await flush(); const firstSignal = signal; await f.step();
    assert.equal(settled, true); assert.equal(firstSignal.aborted, true);
    await f.step(); assert.equal(calls, 2);
    assert.deepEqual(f.store.get().data.items, ["restored body"]);
  } finally { f.store.reset(); globalThis.fetch = original; }
});

test("synchronous permanent failure keeps its exact error and clears all timers", async () => {
  const f = fixture(); const denied = Object.assign(new Error("denied"), { status: 403, code: "permission_denied" });
  await f.store.load(() => { throw denied; });
  assert.equal(f.store.get().error, denied); assert.equal(f.timers.size, 0);
  assert.equal(f.store.isPending(), false);
});
