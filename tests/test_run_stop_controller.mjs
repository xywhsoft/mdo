import assert from "node:assert/strict";
import test from "node:test";
import { createRunStopController, resolvesRunStopError } from "../app/web/js/features/chat/run-stop-controller.js";
import { cancelRun, readRun } from "../app/web/js/state/runs.js";

const active = { id: "run-a", project_id: "qa", session_id: "a",
  terminal: false, cancel_requested: false, state: "running" };
const stopped = { ...active, cancel_requested: true };
const failure = code => Object.assign(new Error(code), { code });
const flush = () => new Promise(resolve => setImmediate(resolve));
function fixture(options = {}) {
  const timers = new Map(), calls = [], accepted = [], errors = [];
  let serial = 0, time = 0, writable = true;
  const controller = createRunStopController({
    cancel: async id => { calls.push(["DELETE", id]); return stopped; },
    read: async id => { calls.push(["GET", id]); return active; },
    canWrite: () => writable,
    setTimer: (fn, delay) => { timers.set(++serial, { fn, delay }); return serial; },
    clearTimer: id => timers.delete(id), random: () => 0, now: () => time,
    onAccepted: (run, owner) => accepted.push({ run, owner }),
    onError: (error, owner) => errors.push({ error, owner }),
    ...options,
  });
  return { controller, timers, calls, accepted, errors,
    writable: value => { writable = value; },
    elapsed: () => time, advance: delay => { time += delay; },
    async tick() { const [id, timer] = timers.entries().next().value;
      timers.delete(id); time += timer.delay; timer.fn(); await flush(); return timer.delay; } };
}

test("a normal stop is acknowledged once without waiting for secondary reads", async () => {
  const f = fixture();
  assert.equal(f.controller.request(active, true), true);
  assert.equal(f.controller.request(active), false);
  await flush();
  assert.deepEqual(f.calls, [["DELETE", active.id]]);
  assert.equal(f.accepted.length, 1);
  assert.equal(f.accepted[0].owner.returnFocus, true);
  assert.equal(f.controller.has(active.id), false);
  assert.equal(f.timers.size, 0);
});

test("an offline stop waits, then inspects and cancels the original run on reconnect", async () => {
  const f = fixture(); f.writable(false);
  f.controller.request(active);
  assert.equal(f.controller.phase(active.id), "waiting");
  assert.equal(f.controller.hasSession("qa", "a"), true);
  assert.equal(f.controller.hasSession("qa", "b"), false);
  f.controller.resume(); await flush();
  assert.equal(f.calls.length, 0);
  f.writable(true); f.controller.resume(); f.controller.resume(); await flush();
  assert.deepEqual(f.calls, [["GET", active.id], ["DELETE", active.id]]);
  assert.equal(f.accepted.length, 1);
});

test("a lost stop acknowledgement is verified before any repeat cancellation", async () => {
  let deletes = 0, reads = 0;
  const f = fixture({ cancel: async () => { deletes++; throw failure("remote_result_unconfirmed"); },
    read: async () => { reads++; return stopped; } });
  f.controller.request(active); await flush();
  assert.equal(f.errors.length, 0);
  assert.equal(f.controller.has(active.id), true);
  await f.tick();
  assert.equal(deletes, 1); assert.equal(reads, 1);
  assert.equal(f.accepted.length, 1);
});

test("an unaccepted transient stop retries only that exact run with bounded backoff", async () => {
  let deletes = 0;
  const f = fixture({ cancel: async id => {
    assert.equal(id, active.id); deletes++;
    if (deletes < 4) throw failure("network_error"); return stopped;
  } });
  f.controller.request(active); await flush();
  assert.equal(await f.tick(), 1000);
  assert.equal(await f.tick(), 2000);
  assert.equal(await f.tick(), 4000);
  assert.equal(deletes, 4);
  assert.equal(f.calls.filter(([method]) => method === "GET").length, 3);
  assert.equal(f.accepted.length, 1); assert.equal(f.errors.length, 0);
});

test("read failures keep the stop intent with a cap and jitter rather than replaying writes", async () => {
  const f = fixture({ cancel: async () => { throw failure("network_error"); },
    read: async () => { throw failure("remote_timeout"); }, random: () => 1 });
  f.controller.request(active); await flush();
  for (const delay of [1200, 2400, 4800, 9600, 18000, 18000])
    assert.equal(await f.tick(), delay);
  assert.equal(f.errors.length, 0);
  assert.equal(f.controller.has(active.id), true);
  f.controller.dispose(); assert.equal(f.timers.size, 0);
});

test("an already completed run satisfies the intent without falsely marking it cancelled", async () => {
  const done = { ...active, terminal: true, state: "succeeded" };
  const f = fixture({ read: async () => done }); f.writable(false);
  f.controller.request(active); f.writable(true); f.controller.resume(); await flush();
  assert.equal(f.calls.length, 0);
  assert.equal(f.accepted[0].run.state, "succeeded");
  assert.equal(f.accepted[0].run.cancel_requested, false);
});

test("a generation change during inspection prevents late cancellation", async () => {
  let resolve, deletes = 0;
  const f = fixture({ read: () => new Promise(done => { resolve = done; }),
    cancel: async () => { deletes++; return stopped; } });
  f.writable(false); f.controller.request(active);
  f.writable(true); f.controller.resume(); await flush(); f.controller.clear();
  resolve(active); await flush();
  assert.equal(deletes, 0); assert.equal(f.accepted.length, 0);
});

test("the write guard is checked again after reading the run", async () => {
  let resolve;
  const f = fixture({ read: () => new Promise(done => { resolve = done; }) });
  f.writable(false); f.controller.request(active);
  f.writable(true); f.controller.resume(); await flush(); f.writable(false);
  resolve(active); await flush();
  assert.equal(f.calls.length, 0);
  assert.equal(f.controller.phase(active.id), "waiting");
  f.controller.dispose();
});

test("an authoritative acknowledgement settles a pending intent and ignores its late response", async () => {
  let resolve;
  const f = fixture({ cancel: () => new Promise(done => { resolve = done; }) });
  f.controller.request(active); await flush();
  f.controller.observe({ ...stopped, project_id: "other" });
  assert.equal(f.controller.has(active.id), true);
  f.controller.observe(stopped); resolve(stopped); await flush();
  assert.equal(f.accepted.length, 1);
  assert.equal(f.controller.has(active.id), false);
});

test("permanent denial is shown once and remains available for an explicit retry", async () => {
  const f = fixture({ cancel: async () => { throw Object.assign(failure("access_denied"), { status: 403 }); } });
  f.controller.request(active); await flush(); f.controller.resume();
  assert.equal(f.errors.length, 1); assert.equal(f.timers.size, 0);
  assert.equal(f.controller.has(active.id), false);
  assert.equal(f.controller.request(active), true); await flush();
  assert.equal(f.errors.length, 2);
});

test("mismatched run responses never acknowledge or cancel another session", async () => {
  const f = fixture({ read: async () => ({ ...active, session_id: "b" }) });
  f.writable(false); f.controller.request(active);
  f.writable(true); f.controller.resume(); await flush();
  assert.equal(f.errors[0].error.code, "run_stop_invalid_response");
  assert.equal(f.accepted.length, 0); assert.equal(f.calls.length, 0);
});

test("a hung accepted stop times out and confirms the original run without another DELETE", async context => {
  let deletes = 0, reads = 0, signal;
  const f = fixture({ cancel: async (_id, options) => {
    ++deletes; signal = options?.signal; return new Promise(() => {});
  }, read: async () => { ++reads; return stopped; } });
  context.after(() => f.controller.dispose());
  f.controller.request(active); await flush();
  assert.equal(f.timers.size, 1, "a hanging stop must have a request deadline");
  assert.equal(await f.tick(), 8000); assert(signal.aborted);
  assert.equal(await f.tick(), 1000);
  assert.equal(deletes, 1); assert.equal(reads, 1);
  assert.equal(f.accepted.length, 1); assert.equal(f.errors.length, 0); assert.equal(f.timers.size, 0);
});

test("a hung stop inspection releases its attempt and leaves another session usable", async context => {
  let deletes = 0, reads = 0, signal;
  const other = { ...active, id: "run-b", session_id: "b" };
  const f = fixture({ cancel: async id => {
    if (id === other.id) return { ...other, cancel_requested: true };
    ++deletes; throw failure("network_error");
  }, read: async (_id, options) => {
    if (++reads === 1) { signal = options?.signal; return new Promise(() => {}); }
    return stopped;
  } });
  context.after(() => f.controller.dispose());
  f.controller.request(active); await flush(); assert.equal(await f.tick(), 1000);
  f.controller.request(other); await flush();
  assert.equal(f.accepted[0].run.id, other.id);
  assert.equal(f.timers.size, 1, "a hanging inspection must have a deadline");
  assert.equal(await f.tick(), 8000); assert(signal.aborted);
  assert.equal(await f.tick(), 2000);
  assert.equal(reads, 2); assert.equal(deletes, 1);
  assert.equal(f.accepted[1].run.id, active.id); assert.equal(f.errors.length, 0);
});

test("unconfirmable stops produce one final uncertainty within a minute and permit explicit retry", async context => {
  let deletes = 0;
  const f = fixture({ cancel: async () => { ++deletes; throw failure("network_error"); },
    read: async () => { throw failure("remote_timeout"); } });
  context.after(() => f.controller.dispose());
  f.controller.request(active); await flush();
  for (let step = 0; f.controller.has(active.id) && step < 20; ++step) {
    assert(f.timers.size, "confirmation is either scheduled or finished"); await f.tick();
  }
  assert.equal(f.controller.has(active.id), false, "a writable connection cannot retry forever");
  assert.equal(f.elapsed(), 60000); assert.equal(f.errors.length, 1);
  assert.equal(f.errors[0].error.code, "run_stop_unconfirmed");
  assert.equal(deletes, 1); assert.equal(f.timers.size, 0);
  f.controller.resume(); await flush(); assert.equal(f.errors.length, 1);
  assert.equal(f.controller.request(active), true); await flush(); assert.equal(deletes, 2);
});

test("a known offline interval keeps the stop intent without consuming its connected budget", async context => {
  let deletes = 0;
  const f = fixture({ cancel: async () => { ++deletes; throw failure("network_error"); },
    read: async () => stopped });
  context.after(() => f.controller.dispose());
  f.controller.request(active); await flush();
  f.writable(false); f.advance(600000); await f.tick();
  assert.equal(f.controller.phase(active.id), "waiting"); assert.equal(f.timers.size, 0);
  assert.equal(f.errors.length, 0); assert.equal(f.controller.has(active.id), true);
  f.writable(true); f.controller.resume(); await flush();
  assert.equal(f.accepted.length, 1); assert.equal(deletes, 1); assert.equal(f.errors.length, 0);
});

test("authoritative observation aborts a hanging stop and ignores its late acknowledgement", async context => {
  let signal, release;
  const f = fixture({ cancel: async (_id, options) => {
    signal = options?.signal; return new Promise(resolve => { release = resolve; });
  } });
  context.after(() => { release?.(stopped); f.controller.dispose(); });
  f.controller.request(active); await flush(); f.controller.observe(stopped);
  assert(signal?.aborted, "confirmed stops must release the outstanding HTTP request");
  release(stopped); await flush();
  assert.equal(f.accepted.length, 1); assert.equal(f.errors.length, 0); assert.equal(f.timers.size, 0);
});

test("clearing the target aborts a hanging inspection with no late write or error", async context => {
  let signal, release, deletes = 0;
  const f = fixture({ read: async (_id, options) => {
    signal = options?.signal; return new Promise(resolve => { release = resolve; });
  }, cancel: async () => { ++deletes; return stopped; } });
  context.after(() => { release?.(active); f.controller.dispose(); });
  f.writable(false); f.controller.request(active); f.writable(true); f.controller.resume(); await flush();
  f.controller.clear(); assert(signal?.aborted);
  release(active); await flush();
  assert.equal(deletes, 0); assert.equal(f.accepted.length, 0); assert.equal(f.errors.length, 0);
  assert.equal(f.timers.size, 0);
});

test("clearing before the first HTTP turn cannot send a queued stop", async () => {
  const f = fixture(); f.controller.request(active); f.controller.clear(); await flush();
  assert.equal(f.calls.length, 0); assert.equal(f.errors.length, 0); assert.equal(f.timers.size, 0);
});

test("connection loss before the first HTTP turn reconnects through an exact run inspection", async () => {
  const f = fixture(); f.controller.request(active); f.writable(false); await flush();
  assert.equal(f.calls.length, 0); assert.equal(f.controller.phase(active.id), "waiting");
  f.writable(true); f.controller.resume(); await flush();
  assert.deepEqual(f.calls, [["GET", active.id], ["DELETE", active.id]]);
  assert.equal(f.accepted.length, 1); assert.equal(f.errors.length, 0); assert.equal(f.timers.size, 0);
});

test("run API adapters cancel the real read and DELETE transports when their signal aborts", async context => {
  const releases = [];
  context.after(() => { for (const release of releases) release(); });
  for (const operation of [readRun, cancelRun]) {
    const controller = new AbortController(); let signal;
    context.mock.method(globalThis, "fetch", async (_path, options) => {
      signal = options.signal;
      return new Promise((resolve, reject) => {
        releases.push(() => resolve(Response.json({ ok: true, data: stopped })));
        signal?.addEventListener("abort", () => reject(new DOMException("Transport cancelled", "AbortError")), { once: true });
      });
    });
    const request = operation(active.id, { signal: controller.signal });
    await flush(); assert.equal(signal, controller.signal);
    controller.abort(); await assert.rejects(request, { name: "AbortError" });
    context.mock.restoreAll();
  }
});

test("authoritative completion clears only the error belonging to the same run", () => {
  const error = { code: "run_stop_unconfirmed", runStopOwner: active };
  assert(resolvesRunStopError(error, stopped));
  assert(resolvesRunStopError(error, { ...active, terminal: true, state: "succeeded" }));
  for (const candidate of [active, null, { ...stopped, id: "run-b" },
    { ...stopped, session_id: "b" }, { ...stopped, project_id: "other" },
    { ...active, cancel_requested: "true" }]) assert.equal(resolvesRunStopError(error, candidate), false);
  assert.equal(resolvesRunStopError({ code: "quota_exceeded" }, stopped), false);
});
