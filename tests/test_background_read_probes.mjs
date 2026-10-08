import assert from "node:assert/strict";
import test from "node:test";
import { createResourceStore } from "../app/web/js/state/store.js";
import { isTransientReadError } from "../app/web/js/api/read-recovery.js";

const network = () => Object.assign(new Error("connection lost"), { code: "network_error" });
const flush = async () => { for (let i = 0; i < 12; ++i) await Promise.resolve(); };
function fixture(initial = { items: ["saved"] }) {
  let time = 1000, next = 1;
  const timers = new Map(), pageEvents = new EventTarget(), states = [];
  const store = createResourceStore(initial, {
    recoverRead: isTransientReadError, retainDataOnError: isTransientReadError, pageEvents,
    now: () => time, random: () => 0,
    setTimer(fn, delay) { const id = next++; timers.set(id, { fn, at: time + delay }); return id; },
    clearTimer(id) { timers.delete(id); },
  });
  store.subscribe(state => states.push(state));
  return { store, states, pageEvents, advance(ms) { time += ms; }, get pending() { return timers.size; },
    async step() {
      const [id, timer] = [...timers].sort((a, b) => a[1].at - b[1].at)[0] ?? [];
      if (!timer) return false;
      timers.delete(id); time = timer.at; timer.fn(); await flush(); return true;
    },
    async drain() {
      for (let i = 0; i < 40; ++i) if (!await this.step()) return;
      assert.fail("unbounded read recovery");
    },
  };
}

test("exhausted reads keep their final notice during rate-limited background probes", async () => {
  const env = fixture(); let calls = 0, finish;
  const saved = env.store.get().data;
  const failed = async () => { calls++; throw network(); };
  await env.store.load(failed, { background: true }); await env.drain();
  assert.equal(calls, 6); assert.equal(env.store.get().status, "error");
  const afterExhaustion = env.states.length;
  for (let i = 0; i < 10; ++i) await env.store.load(failed, { background: true });
  assert.equal(calls, 6, "polling/live events cannot reset an exhausted budget");
  assert.equal(env.states.length, afterExhaustion);
  env.advance(30000);
  const probe = env.store.load(() => { calls++; return new Promise((_resolve, reject) => { finish = reject; }); },
    { background: true });
  assert.equal(env.store.get().status, "error"); assert.equal(env.store.get().data, saved);
  assert.equal(env.store.isPending(), true);
  await env.store.load(failed, { background: true }); assert.equal(calls, 7);
  finish(network()); await probe; await env.drain();
  assert.equal(calls, 7, "an automatic probe makes one attempt, not another retry cycle");
  assert.equal(env.store.isPending(), false); assert.equal(env.pending, 0);
  env.advance(30000);
  await env.store.load(async () => { calls++; return { items: ["restored"] }; }, { background: true });
  assert.equal(calls, 8); assert.equal(env.store.get().status, "ready");
  assert.deepEqual(env.store.get().data.items, ["restored"]);
  assert(env.states.slice(afterExhaustion).every(state => ["error", "ready"].includes(state.status)));
});

test("explicit retry bypasses the probe delay and keeps the normal bounded recovery", async () => {
  const env = fixture(); env.store.setError(network()); let calls = 0;
  const loader = async () => { if (++calls === 1) throw network(); return { items: ["restored"] }; };
  await env.store.load(loader);
  assert.equal(env.store.get().status, "refreshing"); assert.equal(env.store.get().error, null);
  await env.store.load(loader, { background: true }); assert.equal(calls, 1);
  await env.drain(); assert.equal(calls, 2); assert.equal(env.store.get().status, "ready");
});

test("a permanent refusal stays visible without poll floods and a changed cause remains accurate", async () => {
  const env = fixture({ items: [] }); let calls = 0;
  env.store.setData({ items: ["saved"] });
  env.store.setError(network()); env.advance(30000);
  const denied = Object.assign(new Error("denied"), { status: 403, code: "permission_denied" });
  const loader = async () => { calls++; throw denied; };
  await env.store.load(loader, { background: true });
  assert.equal(env.store.get().error, denied); assert.deepEqual(env.store.get().data.items, []);
  assert.equal(env.store.get().updatedAt, 0);
  for (let i = 0; i < 5; ++i) await env.store.load(loader, { background: true });
  assert.equal(calls, 1); assert.equal(env.pending, 0);
  await env.store.load(async () => ({ items: ["allowed"] }));
  assert.deepEqual(env.store.get().data.items, ["allowed"]);
});

test("page cache return resumes an interrupted quiet probe without clearing its final notice", async () => {
  const env = fixture(); env.store.setError(network()); env.advance(30000);
  let calls = 0, firstSignal;
  const loader = signal => {
    if (++calls > 1) return { items: ["restored"] };
    firstSignal = signal;
    return new Promise((_resolve, reject) => signal.addEventListener("abort", () =>
      reject(new DOMException("cancelled", "AbortError")), { once: true }));
  };
  const afterError = env.states.length;
  const probe = env.store.load(loader, { background: true });
  env.pageEvents.dispatchEvent(new Event("pagehide")); await probe;
  assert.equal(firstSignal.aborted, true); assert.equal(env.pending, 0);
  env.pageEvents.dispatchEvent(new Event("pageshow")); await flush();
  assert.equal(calls, 2); assert.equal(env.store.get().status, "ready");
  assert(env.states.slice(afterError).every(state => ["error", "ready"].includes(state.status)));
});

test("authoritative context replacement cancels a quiet probe and its cooldown", async () => {
  const env = fixture(); env.store.setError(network()); env.advance(30000);
  let signal, finish;
  const probe = env.store.load(value => { signal = value; return new Promise(resolve => { finish = resolve; }); },
    { background: true });
  env.store.reset({ items: ["new context"] });
  assert.equal(signal.aborted, true); assert.equal(env.store.isPending(), false);
  await env.store.load(async () => ({ items: ["new read"] }), { background: true });
  finish({ items: ["obsolete"] }); await probe;
  assert.deepEqual(env.store.get().data.items, ["new read"]);
  assert.equal(env.pending, 0);
});

test("a stalled automatic probe has one eight-second attempt and preserves the final notice", async () => {
  const env = fixture(); env.store.setError(network()); env.advance(30000);
  let calls = 0;
  const afterError = env.states.length;
  const probe = env.store.load(signal => {
    calls++;
    return new Promise((_resolve, reject) => signal.addEventListener("abort", () =>
      reject(new DOMException("deadline", "AbortError")), { once: true }));
  }, { background: true });
  await env.step(); await probe; await env.drain();
  assert.equal(calls, 1); assert.equal(env.pending, 0);
  assert.equal(env.store.get().error.code, "network_error");
  assert(env.states.slice(afterError).every(state => state.status === "error"));
});
