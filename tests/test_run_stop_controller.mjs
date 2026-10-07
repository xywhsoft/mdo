import assert from "node:assert/strict";
import test from "node:test";
import { createRunStopController } from "../app/web/js/features/chat/run-stop-controller.js";

const active = { id: "run-a", project_id: "qa", session_id: "a",
  terminal: false, cancel_requested: false, state: "running" };
const stopped = { ...active, cancel_requested: true };
const failure = code => Object.assign(new Error(code), { code });
const flush = () => new Promise(resolve => setImmediate(resolve));
function fixture(options = {}) {
  const timers = new Map(), calls = [], accepted = [], errors = [];
  let serial = 0, writable = true;
  const controller = createRunStopController({
    cancel: async id => { calls.push(["DELETE", id]); return stopped; },
    read: async id => { calls.push(["GET", id]); return active; },
    canWrite: () => writable,
    setTimer: (fn, delay) => { timers.set(++serial, { fn, delay }); return serial; },
    clearTimer: id => timers.delete(id), random: () => 0,
    onAccepted: (run, owner) => accepted.push({ run, owner }),
    onError: (error, owner) => errors.push({ error, owner }),
    ...options,
  });
  return { controller, timers, calls, accepted, errors,
    writable: value => { writable = value; },
    async tick() { const [id, timer] = timers.entries().next().value;
      timers.delete(id); timer.fn(); await flush(); return timer.delay; } };
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
  f.writable(true); f.controller.resume(); f.controller.clear();
  resolve(active); await flush();
  assert.equal(deletes, 0); assert.equal(f.accepted.length, 0);
});

test("the write guard is checked again after reading the run", async () => {
  let resolve;
  const f = fixture({ read: () => new Promise(done => { resolve = done; }) });
  f.writable(false); f.controller.request(active);
  f.writable(true); f.controller.resume(); f.writable(false);
  resolve(active); await flush();
  assert.equal(f.calls.length, 0);
  assert.equal(f.controller.phase(active.id), "waiting");
  f.controller.dispose();
});

test("an authoritative acknowledgement settles a pending intent and ignores its late response", async () => {
  let resolve;
  const f = fixture({ cancel: () => new Promise(done => { resolve = done; }) });
  f.controller.request(active);
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
