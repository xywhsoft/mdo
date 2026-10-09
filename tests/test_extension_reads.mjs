import assert from "node:assert/strict";
import test from "node:test";
import { createRequestRecovery } from "../app/web/js/api/request-recovery.js";
import { createExtensionReads } from "../app/web/js/features/settings/extension-reads.js";

const flush = () => new Promise(resolve => setImmediate(resolve));
const response = data => Response.json({ ok: true, data });
const failure = status => Response.json({ ok: false, error: {
  code: status === 403 ? "permission_denied" : "service_unavailable", message: "Final read denial" } }, { status });
function environment() {
  let time = 0, sequence = 0;
  const timers = new Map(), waits = [], page = new EventTarget();
  const reads = createExtensionReads({ createRecovery: () => createRequestRecovery({
    now: () => time, random: () => 0, eventTarget: page,
    setTimer(fn, delay) { timers.set(++sequence, { fn, at: time + delay, delay }); return sequence; },
    clearTimer(id) { timers.delete(id); },
  }) });
  return { reads, page, timers, waits, elapsed: () => time,
    async settle(promise) {
      let done = false, result, error;
      promise.then(value => { done = true; result = value; }, reason => { done = true; error = reason; });
      for (let count = 0; count < 100 && !done; count++) {
        await flush(); if (done) break;
        assert.ok(timers.size, "every pending read owns a finite deadline");
        const [id, timer] = [...timers].sort((a, b) => a[1].at - b[1].at)[0];
        timers.delete(id); time = timer.at; waits.push(timer.delay); timer.fn();
      }
      assert.equal(done, true); assert.equal(timers.size, 0);
      if (error) throw error;
      return result;
    },
  };
}

test("extension GETs quietly recover twice with exponential waits", async context => {
  const env = environment(); let calls = 0;
  context.mock.method(globalThis, "fetch", async (url, options) => {
    assert.equal(url, "/api/v1/extensions/skills");
    assert.equal(options.method, "GET"); assert.ok(options.signal instanceof AbortSignal);
    return ++calls < 3 ? failure(503) : response({ items: [{ id: "one" }] });
  });
  assert.deepEqual((await env.settle(env.reads.read("catalog", "/extensions/skills"))).data,
    { items: [{ id: "one" }] });
  assert.equal(calls, 3); assert.deepEqual(env.waits, [500, 1000]);
});

test("permanent and optional failures are not retried", async context => {
  const env = environment(); let calls = 0, status = 403;
  context.mock.method(globalThis, "fetch", async () => { calls++; return failure(status); });
  await assert.rejects(env.settle(env.reads.read("editor", "/extensions/agents/default")),
    { status: 403, code: "permission_denied", message: "Final read denial" });
  status = 503;
  await assert.rejects(env.settle(env.reads.read("packages", "/ecosystem", { retry: false })), { status: 503 });
  assert.equal(calls, 2); assert.deepEqual(env.waits, []);
});

test("hanging bodies stop within the shared minute and six attempts", async context => {
  const env = environment(), signals = [];
  context.mock.method(globalThis, "fetch", async (_url, options) => {
    signals.push(options.signal);
    return { ok: true, status: 200, json: () => new Promise(() => {}) };
  });
  await assert.rejects(env.settle(env.reads.read("export", "/extensions/skills/one/bundle")), { code: "network_error" });
  assert.equal(signals.length, 6); assert.equal(env.elapsed(), 60000);
  assert.ok(signals.every(signal => signal.aborted));
});

test("a new edit intent releases an abort-ignoring old transport", async context => {
  const env = environment(); let resolveOld, oldSignal, calls = 0;
  context.mock.method(globalThis, "fetch", (url, options) => {
    calls++;
    if (url.endsWith("/old")) { oldSignal = options.signal; return new Promise(resolve => { resolveOld = resolve; }); }
    return Promise.resolve(response({ content: "new" }));
  });
  const old = env.reads.read("editor", "/extensions/commands/old");
  const cancelled = assert.rejects(old, { name: "AbortError" });
  await flush();
  assert.deepEqual((await env.settle(env.reads.read("editor", "/extensions/commands/new"))).data,
    { content: "new" });
  await cancelled; assert.equal(oldSignal.aborted, true);
  resolveOld(response({ content: "old" })); await flush();
  assert.equal(calls, 2); assert.equal(env.timers.size, 0);
});

test("an old read finishing cannot forget the replacement's cancellation", async context => {
  const env = environment(), signals = [];
  context.mock.method(globalThis, "fetch", (_url, options) => {
    signals.push(options.signal); return new Promise(() => {});
  });
  const old = env.reads.read("editor", "/extensions/commands/old");
  const oldCancelled = assert.rejects(old, { name: "AbortError" }); await flush();
  const newer = env.reads.read("editor", "/extensions/commands/new");
  const newCancelled = assert.rejects(newer, { name: "AbortError" });
  await oldCancelled; await flush(); env.reads.cancel("editor"); await newCancelled;
  assert.equal(signals.length, 2); assert.ok(signals.every(signal => signal.aborted));
  assert.equal(env.timers.size, 0);
});

test("leaving cancels every active read without another request", async context => {
  const env = environment(), signals = [];
  context.mock.method(globalThis, "fetch", (_url, options) => {
    signals.push(options.signal); return new Promise(() => {});
  });
  const outcomes = Promise.allSettled([env.reads.read("catalog", "/extensions/mcp"),
    env.reads.read("tool-picker", "/tools")]);
  await flush(); env.reads.cancel();
  assert.deepEqual((await outcomes).map(item => item.reason.name), ["AbortError", "AbortError"]);
  assert.equal(signals.length, 2); assert.ok(signals.every(signal => signal.aborted));
  assert.equal(env.timers.size, 0);
});

test("a changed owner prevents another retry after backoff", async context => {
  const env = environment(); let current = true, calls = 0;
  context.mock.method(globalThis, "fetch", async () => { calls++; current = false; return failure(503); });
  await assert.rejects(env.settle(env.reads.read("catalog", "/extensions/subagents", {
    isCurrent: () => current })), { name: "AbortError" });
  assert.equal(calls, 1); assert.deepEqual(env.waits, [500]);
});

test("a late successful result from a changed owner is discarded", async context => {
  const env = environment(); let current = true, resolve;
  context.mock.method(globalThis, "fetch", () => new Promise(done => { resolve = done; }));
  const work = env.reads.read("editor", "/extensions/commands/old", { isCurrent: () => current });
  const cancelled = assert.rejects(work, { name: "AbortError" }); await flush();
  current = false; resolve(response({ content: "old" })); await cancelled;
  assert.equal(env.timers.size, 0);
});

test("page exit cancels recovery, and an explicit later read can resume", async context => {
  const env = environment(); let calls = 0;
  context.mock.method(globalThis, "fetch", async () => ++calls === 1 ? failure(503) : response({ items: [] }));
  const work = env.reads.read("catalog", "/extensions/agents");
  const cancelled = assert.rejects(work, { name: "AbortError" }); await flush();
  env.page.dispatchEvent(new Event("pagehide")); await cancelled;
  assert.equal(calls, 1); assert.equal(env.timers.size, 0);
  assert.deepEqual((await env.settle(env.reads.read("catalog", "/extensions/agents"))).data, { items: [] });
  assert.equal(calls, 2);
});
