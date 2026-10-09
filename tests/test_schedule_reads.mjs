import assert from "node:assert/strict";
import test from "node:test";

const flush = () => new Promise(resolve => setImmediate(resolve));
let serial = 0;
const snapshot = { enabled: true, items: [{ id: "one", label: "Preserved" }], total: 1 };
const reply = data => Response.json({ ok: true, data });
const denied = status => Response.json({ ok: false, error: {
  code: status === 403 ? "permission_denied" : "service_unavailable", message: "Read refused" } }, { status });

async function environment(context, fetch) {
  let time = 0, sequence = 0;
  const timers = new Map(), waits = [];
  context.mock.method(Date, "now", () => time);
  context.mock.method(Math, "random", () => 0);
  context.mock.method(globalThis, "setTimeout", (fn, delay) => {
    timers.set(++sequence, { fn, at: time + delay, delay }); return sequence;
  });
  context.mock.method(globalThis, "clearTimeout", id => timers.delete(id));
  context.mock.method(globalThis, "fetch", fetch);
  const state = await import(`../app/web/js/state/schedules.js?case=${++serial}`);
  context.after(() => state.schedulesStore.reset());
  return { ...state, timers, waits, elapsed: () => time,
    async advance() {
      await flush();
      assert.ok(timers.size, "pending schedule reads own a finite deadline");
      const [id, timer] = [...timers].sort((a, b) => a[1].at - b[1].at)[0];
      timers.delete(id); time = timer.at; waits.push(timer.delay); timer.fn(); await flush();
    },
    async drain() {
      for (let count = 0; count < 50 && state.schedulesStore.isPending(); count++) await this.advance();
      assert.equal(state.schedulesStore.isPending(), false);
      assert.equal(timers.size, 0);
    },
  };
}

test("schedule list quietly recovers two temporary failures", async context => {
  let calls = 0;
  const env = await environment(context, async (path, options) => {
    assert.equal(path, "/api/v1/schedules"); assert.equal(options.method, "GET");
    assert.ok(options.signal instanceof AbortSignal);
    return ++calls < 3 ? denied(503) : reply(snapshot);
  });
  const states = [], unsubscribe = env.schedulesStore.subscribe(state => states.push(state.status));
  await env.loadSchedules();
  assert.equal(env.schedulesStore.get().status, "refreshing");
  await env.drain(); unsubscribe();
  assert.equal(calls, 3); assert.deepEqual(env.waits, [500, 1000]);
  assert.deepEqual(env.schedulesStore.get().data, snapshot);
  assert.equal(states.includes("error"), false);
});

test("background polls preserve old cards and share ongoing recovery", async context => {
  let calls = 0;
  const env = await environment(context, async () => ++calls < 3 ? denied(503) : reply(snapshot));
  env.schedulesStore.setData(snapshot);
  await env.loadSchedules({ background: true });
  await env.loadSchedules({ background: true });
  assert.equal(calls, 1); assert.equal(env.schedulesStore.get().status, "ready");
  assert.equal(env.schedulesStore.get().data, snapshot);
  await env.drain(); assert.equal(calls, 3);
});

test("final temporary failures do not let polling restart six attempts", async context => {
  let calls = 0;
  const env = await environment(context, async () => { calls++; return denied(503); });
  env.schedulesStore.setData(snapshot);
  await env.loadSchedules(); await env.drain();
  assert.equal(calls, 6); assert.equal(env.schedulesStore.get().status, "error");
  assert.equal(env.schedulesStore.get().data, snapshot);
  await env.loadSchedules({ background: true }); assert.equal(calls, 6);
  await env.loadSchedules(); await env.drain(); assert.equal(calls, 12);
});

test("a hanging schedule response releases within the shared minute", async context => {
  const signals = [];
  const env = await environment(context, async (_path, options) => {
    signals.push(options.signal); return { ok: true, status: 200, json: () => new Promise(() => {}) };
  });
  let settled = false;
  const first = env.loadSchedules().then(() => { settled = true; });
  await flush(); await env.advance(); await first;
  assert.equal(settled, true); await env.drain();
  assert.equal(env.elapsed(), 60000); assert.equal(signals.length, 6);
  assert.ok(signals.every(signal => signal.aborted));
  assert.equal(env.schedulesStore.get().error.code, "network_error");
});

test("leaving cancels a held list, preserves its snapshot and rejects late replacement", async context => {
  let resolve, signal;
  const env = await environment(context, (_path, options) => {
    signal = options.signal; return new Promise(done => { resolve = done; });
  });
  env.schedulesStore.setData(snapshot);
  const work = env.loadSchedules(); await flush();
  env.cancelScheduleReads(); await work;
  assert.equal(signal.aborted, true); assert.equal(env.timers.size, 0);
  resolve(reply({ enabled: true, items: [], total: 0 })); await flush();
  assert.equal(env.schedulesStore.get().data, snapshot);
  assert.equal(env.schedulesStore.get().error, null);
});

test("permanent read denials and run writes are never retried", async context => {
  const calls = [];
  const env = await environment(context, async (path, options) => {
    calls.push({ path, method: options.method });
    return denied(options.method === "GET" ? 403 : 503);
  });
  await env.loadSchedules(); await env.drain();
  assert.equal(env.schedulesStore.get().error.status, 403);
  await assert.rejects(env.runSchedule("one", 1), { status: 503 });
  assert.deepEqual(calls, [{ path: "/api/v1/schedules", method: "GET" },
    { path: "/api/v1/schedules/one/run", method: "POST" }]);
});
