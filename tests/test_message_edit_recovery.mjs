import assert from "node:assert/strict";
import test from "node:test";
import { createMessageEditRecovery, messageEditId } from
  "../app/web/js/features/chat/message-edit-recovery.js";

const lost = () => Object.assign(new Error("lost reply"), { code: "network_error" });
function clock() {
  let time = 0, serial = 0;
  const timers = new Map(), delays = [], page = new EventTarget();
  const recovery = createMessageEditRecovery({ now: () => time, random: () => 0,
    eventTarget: page,
    setTimer(fn, delay) { timers.set(++serial, { fn, at: time + delay, delay }); return serial; },
    clearTimer(id) { timers.delete(id); },
  });
  return { recovery, page, timers, delays, get elapsed() { return time; },
    async run(operation) {
      let result, error, done = false;
      operation().then(value => { result = value; done = true; },
        value => { error = value; done = true; });
      for (let step = 0; !done && step < 100; ++step) {
        await new Promise(resolve => setImmediate(resolve));
        if (done) break;
        assert.ok(timers.size, "every pending operation has a deadline");
        const [id, timer] = [...timers].sort((a, b) => a[1].at - b[1].at)[0];
        timers.delete(id); time = timer.at; delays.push(timer.delay); timer.fn();
      }
      assert.equal(done, true);
      assert.equal(timers.size, 0);
      if (error) throw error;
      return result;
    },
  };
}

test("temporary edit reads recover silently with exponential waits", async () => {
  const env = clock(); let calls = 0;
  assert.equal(await env.run(() => env.recovery.request(async () => {
    if (++calls < 3) throw lost(); return "history";
  })), "history");
  assert.equal(calls, 3); assert.deepEqual(env.delays, [500, 1000]);
  env.recovery.dispose();
});

test("six failures terminate, and permission refusals do not retry", async () => {
  const env = clock(); let calls = 0;
  await assert.rejects(env.run(() => env.recovery.request(async () => {
    ++calls; throw lost();
  })), { code: "network_error" });
  assert.equal(calls, 6); assert.equal(env.elapsed, 15500);
  let refused = 0;
  await assert.rejects(env.run(() => env.recovery.request(async () => {
    ++refused; throw Object.assign(new Error("changed"), { status: 412, code: "revision_conflict" });
  })), { code: "revision_conflict" });
  assert.equal(refused, 1); env.recovery.dispose();
});

test("abort-aware and noncooperative I/O share the one-minute deadline", async () => {
  for (const honorsAbort of [true, false]) {
    const env = clock(); let calls = 0;
    const hung = signal => { ++calls; return new Promise((_resolve, reject) => {
      if (honorsAbort) signal.addEventListener("abort", () =>
        reject(new DOMException("fetch abort", "AbortError")), { once: true });
    }); };
    await assert.rejects(env.run(() => env.recovery.request(hung)), { code: "network_error" });
    assert.equal(calls, 6); assert.equal(env.elapsed, 60000);
    env.recovery.dispose();
  }
});

test("leaving the page cancels both an active request and backoff", async () => {
  for (const backoff of [false, true]) {
    const env = clock(); let calls = 0;
    const pending = env.recovery.request(async () => {
      ++calls; if (backoff) throw lost(); return new Promise(() => {});
    });
    await new Promise(resolve => setImmediate(resolve));
    env.page.dispatchEvent(new Event("pagehide"));
    await assert.rejects(pending, { name: "AbortError" });
    assert.equal(calls, 1); assert.equal(env.timers.size, 0);
    assert.throws(() => env.recovery.assertActive(), { name: "AbortError" });
  }
});

test("draft writes remain single attempts; only keyed edits accept uncertain replies", async () => {
  const env = clock(); let writes = 0;
  await assert.rejects(env.run(() => env.recovery.request(async () => {
    ++writes; throw lost();
  }, { retry: false })), { code: "network_error" });
  assert.equal(writes, 1);
  let reads = 0;
  await assert.rejects(env.run(() => env.recovery.request(async () => {
    ++reads; throw Object.assign(new Error("invalid JSON"), { code: "invalid_response" });
  })), { code: "invalid_response" });
  assert.equal(reads, 1);
  let edits = 0;
  assert.equal(await env.run(() => env.recovery.request(async () => {
    if (++edits === 1) throw Object.assign(new Error("lost result"), { code: "remote_result_unconfirmed" });
    return "committed";
  }, { mutation: true })), "committed");
  assert.equal(edits, 2); env.recovery.dispose();
});

test("history edits get independent 128-bit identities", () => {
  const first = messageEditId(), second = messageEditId();
  assert.match(first, /^[0-9a-f]{32}$/); assert.match(second, /^[0-9a-f]{32}$/);
  assert.notEqual(first, second);
});
