import assert from "node:assert/strict";
import test from "node:test";
import { createResourceStore } from "../app/web/js/state/store.js";
import { isTransientReadError } from "../app/web/js/api/read-recovery.js";
import { modelsStore, agentsStore, projectsStore, recoverCatalogs } from
  "../app/web/js/state/catalogs.js";

const network = () => Object.assign(new Error("connection lost"), { code: "network_error" });
const flush = async () => { for (let i = 0; i < 12; ++i) await Promise.resolve(); };
function fixture(initial = { models: [] }) {
  let time = 1000, next = 1;
  const timers = new Map();
  const store = createResourceStore(initial, { recoverRead: isTransientReadError,
    now: () => time, random: () => 0,
    setTimer(callback, delay) { const id = next++; timers.set(id, { callback, at: time + delay }); return id; },
    clearTimer(id) { timers.delete(id); },
  });
  const states = []; store.subscribe(state => states.push(state.status));
  return { store, states, get elapsed() { return time - 1000; }, get pending() { return timers.size; },
    async step() {
      const [id, timer] = [...timers].sort((a, b) => a[1].at - b[1].at)[0] ?? [];
      if (!timer) return false;
      timers.delete(id); time = timer.at; timer.callback(); await flush(); return true;
    },
    async drain() {
      for (let i = 0; i < 40; ++i) if (!await this.step()) return;
      assert.fail("unbounded recovery timers");
    },
  };
}

test("the first lost catalog read settles startup, then restores in the background", async () => {
  const env = fixture(); let calls = 0;
  const first = await env.store.load(async () => {
    if (++calls === 1) throw network();
    return { models: [{ id: "restored" }] };
  });
  assert.equal(first.status, "refreshing"); assert.equal(first.error, null);
  assert.equal(calls, 1); assert.equal(env.pending, 1);
  await env.drain(); assert.equal(env.elapsed, 500);
  assert.equal(calls, 2); assert.equal(env.store.get().data.models[0].id, "restored");
  assert.equal(env.store.get().status, "ready"); assert.ok(!env.states.includes("error"));
});

test("exhaustion uses exponential waits and publishes one final error", async () => {
  const env = fixture(); let calls = 0; const error = network();
  await env.store.load(async () => { calls++; throw error; });
  await env.drain(); assert.equal(calls, 6); assert.equal(env.elapsed, 15500);
  assert.equal(env.states.filter(state => state === "error").length, 1);
  assert.equal(env.store.get().error, error); assert.equal(env.pending, 0);
});

test("six aborted reads and waits cannot exceed the shared minute deadline", async () => {
  const env = fixture(); let calls = 0;
  await env.store.load(async signal => { // Advance fake time while the first load waits.
    calls++;
    const waiting = new Promise((_resolve, reject) => signal.addEventListener("abort", () =>
      reject(new DOMException("timeout", "AbortError")), { once: true }));
    queueMicrotask(() => { void env.step(); }); return waiting;
  });
  await env.drain(); assert.equal(calls, 6); assert.equal(env.elapsed, 60000);
  assert.equal(env.store.get().status, "error");
  assert.equal(env.store.get().error.code, "network_error");
  assert.equal(env.states.filter(state => state === "error").length, 1);
});

test("permanent access failures stay visible and are never retried", async () => {
  const env = fixture(); let calls = 0;
  const error = Object.assign(new Error("permission denied"), { status: 403, code: "permission_denied" });
  await env.store.load(async () => { calls++; throw error; });
  await env.drain(); assert.equal(calls, 1); assert.equal(env.elapsed, 0);
  assert.equal(env.store.get().error, error);
});

test("an explicit newer read cancels the old retry and keeps the new result", async () => {
  const env = fixture(); let oldCalls = 0;
  await env.store.load(async () => { oldCalls++; throw network(); });
  await env.store.load(async () => ({ models: [{ id: "newer" }] }));
  await env.drain(); assert.equal(oldCalls, 1); assert.equal(env.pending, 0);
  assert.equal(env.store.get().data.models[0].id, "newer");
});

test("a stale in-flight response cannot replace a newer catalog", async () => {
  const env = fixture(); let oldSignal, finish;
  const old = env.store.load(signal => { oldSignal = signal; return new Promise(resolve => { finish = resolve; }); });
  await env.store.load(async () => ({ models: [{ id: "newer" }] }));
  assert.equal(oldSignal.aborted, true);
  finish({ models: [{ id: "obsolete" }] }); await old; await env.drain();
  assert.equal(env.store.get().data.models[0].id, "newer");
});

test("reset and authoritative data cancel any scheduled recovery", async () => {
  for (const operation of [store => store.reset(), store => store.setData({ models: [{ id: "accepted" }] })]) {
    const env = fixture(); let calls = 0;
    await env.store.load(async () => { calls++; throw network(); });
    operation(env.store); const state = env.store.get(); await env.drain();
    assert.equal(calls, 1); assert.equal(env.pending, 0); assert.equal(env.store.get(), state);
  }
});

test("a temporary refresh failure keeps the previously loaded catalog", async () => {
  const env = fixture(); const saved = { models: [{ id: "saved" }] };
  env.store.setData(saved); let calls = 0;
  await env.store.load(async () => { if (++calls === 1) throw network(); return saved; });
  assert.equal(env.store.get().data, saved); assert.equal(env.store.get().status, "refreshing");
  await env.drain(); assert.equal(env.store.get().data, saved);
});

test("ordinary stores keep their original single-read failure behavior", async () => {
  const store = createResourceStore(); let calls = 0; const error = network();
  const state = await store.load(async () => { calls++; throw error; });
  assert.equal(calls, 1); assert.equal(state.status, "error"); assert.equal(state.error, error);
});

test("foreground recovery reads only an exhausted transient catalog through the real API", async () => {
  const previousFetch = globalThis.fetch; const reads = [];
  const denied = Object.assign(new Error("denied"), { code: "permission_denied", status: 403 });
  modelsStore.setError(network()); agentsStore.setError(denied);
  projectsStore.setData({ items: [{ id: "saved-project" }] });
  globalThis.fetch = async (url, options) => {
    reads.push({ url, signal: options.signal, method: options.method });
    return new Response(JSON.stringify({ schema_version: 1, ok: true,
      data: { models: [{ id: "restored" }] } }), { status: 200,
      headers: { "Content-Type": "application/json" } });
  };
  try {
    await recoverCatalogs(); await recoverCatalogs();
    assert.equal(reads.length, 1); assert.equal(reads[0].url, "/api/v1/models");
    assert.equal(reads[0].method, "GET"); assert.ok(reads[0].signal instanceof AbortSignal);
    assert.equal(modelsStore.get().data.models[0].id, "restored");
    assert.equal(agentsStore.get().error, denied);
    assert.equal(projectsStore.get().data.items[0].id, "saved-project");
  } finally {
    globalThis.fetch = previousFetch;
    for (const store of [modelsStore, agentsStore, projectsStore]) store.reset();
  }
});
