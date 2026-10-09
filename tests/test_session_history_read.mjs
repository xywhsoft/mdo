import assert from "node:assert/strict";
import test from "node:test";
import { createRequestRecovery } from "../app/web/js/api/request-recovery.js";
import { loadSessionHistory } from "../app/web/js/state/sessions.js";
import { createSessionHistoryRead } from "../app/web/js/features/sessions/session-history-read.js";

const session = { project_id: "default", id: "one", revision: 1 };
const history = { project_id: "default", session_id: "one", revision: 2, last_sequence: 3 };
const etag = '"mdo-session-one-2"';
const reply = (data = history, tag = etag) => Response.json({ ok: true, data }, { headers: { ETag: tag } });
const failure = (status = 503) => Response.json({ ok: false, error: { code:
  status === 403 ? "permission_denied" : "session_service_unavailable" } }, { status });
const flush = () => new Promise(resolve => setImmediate(resolve));
function environment() {
  let time = 0, serial = 0, route = { view: "workspace", projectId: "default", sessionId: "one" };
  const timers = new Map(), waits = [], page = new EventTarget(), listeners = new Set();
  return { timers, waits, page, listeners,
    createRecovery: () => createRequestRecovery({ now: () => time, random: () => 0, eventTarget: page,
      setTimer(fn, delay) { timers.set(++serial, { fn, at: time + delay, delay }); return serial; },
      clearTimer(id) { timers.delete(id); } }),
    navigation: { get: () => route, subscribe(fn) { listeners.add(fn); fn(route); return () => listeners.delete(fn); } },
    navigate(changes) { route = { ...route, ...changes }; for (const fn of listeners) fn(route); },
    elapsed: () => time,
    async settle(promise) {
      let done = false, value, error;
      promise.then(result => { value = result; done = true; }, reason => { error = reason; done = true; });
      for (let i = 0; i < 100 && !done; i++) {
        await flush(); if (done) break;
        assert.ok(timers.size, "a pending history read must own a finite deadline");
        const [id, timer] = [...timers].sort((a, b) => a[1].at - b[1].at)[0];
        timers.delete(id); time = timer.at; waits.push(timer.delay); timer.fn();
      }
      assert.equal(done, true); assert.equal(timers.size, 0);
      if (error) throw error;
      return value;
    },
  };
}

test("standalone history reads quietly recover and validate the exact source", async context => {
  const env = environment(); let calls = 0;
  context.mock.method(globalThis, "fetch", async (path, options) => {
    assert.equal(path, "/api/v1/projects/default/sessions/one/history");
    assert.ok(options.signal instanceof AbortSignal);
    return ++calls < 3 ? failure() : reply();
  });
  assert.deepEqual(await env.settle(loadSessionHistory(session, env)), { ...history, etag });
  assert.equal(calls, 3); assert.deepEqual(env.waits, [500, 1000]);
});

test("an abort-ignoring history body has a shared one-minute limit and an accurate final error", async context => {
  const env = environment(), signals = [];
  context.mock.method(globalThis, "fetch", async (_path, options) => {
    signals.push(options.signal);
    return { ok: true, status: 200, headers: new Headers({ ETag: etag }), json: () => new Promise(() => {}) };
  });
  await assert.rejects(env.settle(loadSessionHistory(session, env)), { code: "session_history_unavailable" });
  assert.equal(signals.length, 6); assert.equal(env.elapsed(), 60000);
  assert.ok(signals.every(signal => signal.aborted));
});

test("a caller-owned deadline makes only one history attempt", async context => {
  let calls = 0;
  context.mock.method(globalThis, "fetch", async () => { calls++; return failure(); });
  const controller = new AbortController();
  await assert.rejects(loadSessionHistory(session, { signal: controller.signal,
    createRecovery: () => { throw new Error("nested retry budget"); } }), { status: 503 });
  assert.equal(calls, 1);
});

test("mismatched boundaries and revisions cannot prepare a fork or cut", async context => {
  for (const [data, tag] of [[{ ...history, session_id: "other" }, etag],
    [{ ...history, project_id: "other" }, etag], [{ ...history, last_sequence: -1 }, etag],
    [{ ...history, revision: 0 }, etag], [{ ...history, last_sequence: 3.5 }, etag],
    [history, '"mdo-session-other-2"'], [history, null]]) {
    const env = environment(); let calls = 0;
    context.mock.method(globalThis, "fetch", async () => { calls++; return reply(data, tag); });
    await assert.rejects(env.settle(loadSessionHistory(session, env)), { code: "session_history_unavailable" });
    assert.equal(calls, 1);
  }
});

test("permanent access refusal is accurate and not retried", async context => {
  const env = environment(); let calls = 0;
  context.mock.method(globalThis, "fetch", async () => { calls++; return failure(403); });
  await assert.rejects(env.settle(loadSessionHistory(session, env)), { code: "permission_denied" });
  assert.equal(calls, 1); assert.equal(env.waits.length, 0);
});

test("switching sessions or opening a utility page cancels preparation without applying the late boundary", async () => {
  for (const changes of [{ sessionId: "two" }, { view: "settings", settingsSection: "general" }, { view: "schedules" }]) {
    const env = environment(); let resolve, signal;
    const reader = createSessionHistoryRead({ ...env, readHistory: (_session, options) => {
      signal = options.signal; return new Promise(done => { resolve = done; });
    } });
    const pending = reader.read(session); await flush();
    assert.doesNotThrow(() => env.navigate(changes), "navigation listeners must not throw cancellation errors");
    await assert.rejects(env.settle(pending), { name: "AbortError" });
    assert.equal(signal.aborted, true); assert.equal(env.listeners.size, 0);
    resolve(history); await flush(); assert.equal(env.listeners.size, 0);
  }
});

test("a newer fork/range intent supersedes an older read", async () => {
  const env = environment(); let reads = 0, firstSignal, finishFirst;
  const reader = createSessionHistoryRead({ ...env, readHistory: (_session, { signal }) => {
    if (++reads === 1) { firstSignal = signal; return new Promise(resolve => { finishFirst = resolve; }); }
    return Promise.resolve(history);
  } });
  const first = reader.read(session); await flush();
  const second = reader.read(session);
  await assert.rejects(first, { name: "AbortError" });
  assert.deepEqual(await env.settle(second), history);
  assert.equal(firstSignal.aborted, true); assert.equal(env.listeners.size, 0);
  finishFirst({ ...history, last_sequence: 999 }); await flush();
});

test("history preparation owns one recovery budget and final reading error", async context => {
  const env = environment(); let calls = 0;
  context.mock.method(globalThis, "fetch", async () => { calls++; return failure(); });
  const reader = createSessionHistoryRead({ ...env, readHistory: loadSessionHistory });
  await assert.rejects(env.settle(reader.read(session)), { code: "session_history_unavailable" });
  assert.equal(calls, 6); assert.deepEqual(env.waits, [500, 1000, 2000, 4000, 8000]);
  assert.equal(env.listeners.size, 0);
});

test("leaving the document or explicitly cancelling releases a hanging history read", async () => {
  for (const stop of [env => env.page.dispatchEvent(new Event("pagehide")),
    (_env, reader) => reader.cancel()]) {
    const env = environment(); let signal;
    const reader = createSessionHistoryRead({ ...env, readHistory: (_session, options) => {
      signal = options.signal; return new Promise(() => {});
    } });
    const pending = reader.read(session); await flush(); stop(env, reader);
    await assert.rejects(env.settle(pending), { name: "AbortError" });
    assert.equal(signal.aborted, true); assert.equal(env.listeners.size, 0);
  }
});
