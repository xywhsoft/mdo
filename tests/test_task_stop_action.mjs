import assert from "node:assert/strict";
import test from "node:test";
import { runTaskStopAction } from "../app/web/js/state/task-stop-action.js";

const task = { id: 4, kind: "process", state: "running", terminal: false,
  stop_requested: false, owner_agent_id: 7, owner_run_id: 9, parent_task_id: 0,
  owner_session: "default/session", created_at: 100, schedule_id: "", schedule_generation: 0 };
const stopped = { ...task, state: "cancelled", terminal: true, stop_requested: true };
const lost = (code = "network_error") => Object.assign(new Error("lost reply"), { code });
const reply = data => ({ data, writeToken: "same-host" });

function environment() {
  let time = 0, serial = 0;
  const timers = new Map(), waits = [], page = new EventTarget();
  const recoveryOptions = { now: () => time, random: () => 0, eventTarget: page,
    setTimer(fn, delay) { timers.set(++serial, { fn, at: time + delay, delay }); return serial; },
    clearTimer(key) { timers.delete(key); } };
  return { page, timers, waits, recoveryOptions, elapsed: () => time,
    async settle(promise) {
      let result, error, done = false;
      promise.then(value => { result = value; done = true; }, value => { error = value; done = true; });
      for (let step = 0; !done && step < 100; ++step) {
        await new Promise(resolve => setImmediate(resolve));
        if (done) break;
        assert.ok(timers.size, "a pending stop has a deadline");
        const [key, timer] = [...timers].sort((a, b) => a[1].at - b[1].at)[0];
        timers.delete(key); time = timer.at; waits.push(timer.delay); timer.fn();
      }
      assert.equal(done, true); assert.equal(timers.size, 0);
      if (error) throw error;
      return result;
    },
  };
}
const action = (env, options) => runTaskStopAction({ id: 4, task, token: "same-host",
  epoch: () => "original-target", recoveryOptions: env.recoveryOptions, ...options });

test("a successful stop returns its acknowledgement without another read", async () => {
  const env = environment(); let deletes = 0, reads = 0;
  assert.equal(await env.settle(action(env, {
    cancel: async (id, { signal }) => { ++deletes; assert.equal(id, "4"); assert.ok(signal); return reply(stopped); },
    inspect: async () => { ++reads; },
  })), stopped);
  assert.equal(deletes, 1); assert.equal(reads, 0);
});

test("lost acknowledgement is confirmed from the original stopped task", async () => {
  const env = environment(); let deletes = 0, reads = 0;
  assert.equal(await env.settle(action(env, {
    cancel: async () => { ++deletes; throw lost("remote_result_unconfirmed"); },
    inspect: async () => { ++reads; return reply(stopped); },
  })), stopped);
  assert.equal(deletes, 1); assert.equal(reads, 1); assert.deepEqual(env.waits, [500]);
});

test("an unaccepted stop repeats only after inspecting the same active task", async () => {
  const env = environment(); let deletes = 0, reads = 0;
  assert.equal(await env.settle(action(env, {
    cancel: async () => { if (++deletes === 1) throw lost(); return reply(stopped); },
    inspect: async () => { if (++reads === 1) throw lost(); return reply(task); },
  })), stopped);
  assert.equal(deletes, 2); assert.equal(reads, 2); assert.deepEqual(env.waits, [500, 1000]);
});

test("unreadable confirmations exhaust once without replaying cancellation", async () => {
  for (const malformed of [false, true]) {
    const env = environment(); let deletes = 0, reads = 0;
    await assert.rejects(env.settle(action(env, {
      cancel: async () => { ++deletes; throw lost(); },
      inspect: async () => { ++reads; if (malformed) return reply({}); throw lost(); },
    })), { code: "task_stop_unconfirmed" });
    assert.equal(deletes, 1); assert.equal(reads, 5); assert.equal(env.elapsed(), 15500);
  }
});

test("a malformed cancellation acknowledgement is inspected before any repeat", async () => {
  const env = environment(); let deletes = 0;
  assert.equal(await env.settle(action(env, {
    cancel: async () => { ++deletes; return reply({}); },
    inspect: async () => reply(stopped),
  })), stopped);
  assert.equal(deletes, 1);
});

test("permanent refusal remains precise with no automatic repeat", async () => {
  for (const [code, status] of [["task_cancel_failed", 409], ["remote_read_only", 403],
    ["task_not_found", 404], ["purge_review_required", 0]]) {
    const env = environment(); let deletes = 0, reads = 0;
    await assert.rejects(env.settle(action(env, {
      cancel: async () => { ++deletes; throw Object.assign(lost(code), { status }); },
      inspect: async () => { ++reads; },
    })), { code });
    assert.equal(deletes, 1); assert.equal(reads, 0);
  }
});

test("reused IDs with different immutable ownership cannot receive a second stop", async () => {
  for (const patch of [{ owner_agent_id: 8 }, { owner_run_id: 10 }, { parent_task_id: 5 },
    { owner_session: "other/session" }, { created_at: 101 }, { kind: "agent" },
    { schedule_id: "other" }, { schedule_generation: 1 }]) {
    const env = environment(); let deletes = 0;
    await assert.rejects(env.settle(action(env, {
      cancel: async () => { ++deletes; throw lost(); },
      inspect: async () => reply({ ...task, ...patch }),
    })), { code: "task_stop_context_changed" });
    assert.equal(deletes, 1);
  }
});

test("unknown tasks are read first and naturally completed tasks need no stop", async () => {
  const env = environment(); let deletes = 0, reads = 0;
  const completed = { ...task, terminal: true, state: "succeeded" };
  assert.equal(await env.settle(action(env, { task: null,
    cancel: async () => { ++deletes; },
    inspect: async () => { ++reads; return reply(completed); },
  })), completed);
  assert.equal(deletes, 0); assert.equal(reads, 1);
});

test("host or target changes prevent late acknowledgement and further cancellation", async () => {
  for (const switched of [false, true]) {
    const env = environment(); let generation = "old", reads = 0, deletes = 0;
    await assert.rejects(env.settle(action(env, { epoch: () => generation,
      cancel: async () => { ++deletes; if (switched) generation = "new"; throw lost(); },
      inspect: async () => { ++reads; return { data: stopped, writeToken: "new-host" }; },
    })), error => switched ? error.name === "AbortError" : error.code === "write_token_conflict");
    assert.equal(deletes, 1); assert.equal(reads, switched ? 0 : 1);
  }
});

test("all request and waiting phases share one minute even for uncooperative transport", async () => {
  const env = environment(); let deletes = 0, reads = 0;
  await assert.rejects(env.settle(action(env, {
    cancel: async () => { ++deletes; return new Promise(() => {}); },
    inspect: async () => { ++reads; return new Promise(() => {}); },
  })), { code: "task_stop_unconfirmed" });
  assert.equal(deletes, 1); assert.equal(reads, 5); assert.equal(env.elapsed(), 60000);
});

test("page exit cancels the request or its backoff without a late read or stop", async () => {
  for (const waiting of [false, true]) {
    const env = environment(); let deletes = 0, reads = 0;
    const pending = action(env, {
      cancel: async () => { ++deletes; if (waiting) throw lost(); return new Promise(() => {}); },
      inspect: async () => { ++reads; return reply(task); },
    });
    await new Promise(resolve => setImmediate(resolve));
    env.page.dispatchEvent(new Event("pagehide"));
    await assert.rejects(pending, { name: "AbortError" });
    assert.equal(deletes, 1); assert.equal(reads, 0); assert.equal(env.timers.size, 0);
  }
});
