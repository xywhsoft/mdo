import assert from "node:assert/strict";
import test from "node:test";
import { runRecoveryAction } from "../app/web/js/features/chat/recovery-action.js";
import { withSessionRuntime } from "../app/web/js/state/session-runtime.js";

const owner = { project_id: "default", session_id: "original", revision: 3,
  last_sequence: 7, resume_required: true };
const id = "a".repeat(32);
const run = { id: "run-original", project_id: "default", session_id: "original",
  resume: true, client_resume_id: id, terminal: false };
const lost = code => Object.assign(new Error("lost acknowledgement"), { code: code ?? "network_error" });
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
        assert.ok(timers.size, "a pending confirmation has a deadline");
        const [key, timer] = [...timers].sort((a, b) => a[1].at - b[1].at)[0];
        timers.delete(key); time = timer.at; waits.push(timer.delay); timer.fn();
      }
      assert.equal(done, true); assert.equal(timers.size, 0);
      if (error) throw error;
      return result;
    },
  };
}
const action = (env, options) => runRecoveryAction({ data: owner, choices: new Map(),
  id, token: null, epoch: () => "original-target", recoveryOptions: env.recoveryOptions, ...options });

test("lost resume acknowledgement is confirmed by exact identity after quiet read backoff", async () => {
  const env = environment(); let posts = 0, reads = 0;
  const result = await env.settle(action(env, { kind: "resume",
    async submitResume(data, choices, options) {
      ++posts; assert.equal(data, owner); assert.equal(options.clientResumeId, id); throw lost();
    }, async inspectRuns() {
      ++reads;
      if (reads === 1) return { data: { items: [{ ...run, session_id: "other" }, { ...run, client_resume_id: "b".repeat(32) }] } };
      if (reads === 2) throw lost();
      return { data: { items: [{ ...run, terminal: true }] } };
    },
  }));
  assert.equal(result.id, run.id); assert.equal(result.terminal, true);
  assert.equal(posts, 1); assert.equal(reads, 3); assert.deepEqual(env.waits, [500, 1000]);
});

test("a successful resume and close need no confirmation reads", async () => {
  for (const kind of ["resume", "abandon"]) {
    const env = environment(); let posts = 0, reads = 0;
    const result = kind === "resume" ? run : { resume_required: false, revision: 4, last_sequence: 8 };
    assert.equal(await env.settle(action(env, { kind,
      submitResume: async () => { ++posts; return result; },
      submitAbandon: async () => { ++posts; return result; },
      inspectRuns: async () => { ++reads; }, inspectRecovery: async () => { ++reads; },
    })), result);
    assert.equal(posts, 1); assert.equal(reads, 0);
  }
});

test("six unreadable confirmations produce one accurate uncertainty without repeating the write", async () => {
  for (const kind of ["resume", "abandon"]) {
    const env = environment(); let posts = 0, reads = 0;
    const submit = async () => { ++posts; throw lost("remote_result_unconfirmed"); };
    const inspect = async () => { ++reads; throw lost(); };
    await assert.rejects(env.settle(action(env, { kind, submitResume: submit, submitAbandon: submit,
      inspectRuns: inspect, inspectRecovery: inspect })), error =>
      error.code === `recovery_${kind}_unconfirmed` && error.recoveryActionUncertain === true);
    assert.equal(posts, 1); assert.equal(reads, 6); assert.equal(env.elapsed(), 15500);
  }
});

test("an ending is confirmed only by a newer, closed boundary in its original session", async () => {
  const env = environment(); let posts = 0, reads = 0;
  const result = { ...owner, resume_required: false, revision: 4, last_sequence: 8 };
  const variants = [{ ...result, session_id: "other" }, { ...result, last_sequence: 7 },
    { ...result, revision: 3 }, { ...result, resume_required: true }, result];
  assert.equal(await env.settle(action(env, { kind: "abandon",
    submitAbandon: async () => { ++posts; throw lost(); },
    inspectRecovery: async () => ({ data: variants[reads++] }),
  })), result);
  assert.equal(posts, 1); assert.equal(reads, 5); assert.deepEqual(env.waits, [500, 1000, 2000, 4000]);
});

test("definite refusals keep their actual error and do not inspect or repeat", async () => {
  for (const [code, status] of [["recovery_state_conflict", 409], ["run_limit_reached", 429],
    ["recovery_resume_invalid", 422], ["remote_read_only", 403]]) {
    const env = environment(); let reads = 0, posts = 0;
    await assert.rejects(env.settle(action(env, { kind: "resume",
      submitResume: async () => { ++posts; throw Object.assign(lost(code), { status }); },
      inspectRuns: async () => { ++reads; },
    })), error => error.code === code && !error.recoveryActionUncertain);
    assert.equal(posts, 1); assert.equal(reads, 0);
  }
});

test("timeouts cap the whole confirmation at one minute, including uncooperative reads", async () => {
  const env = environment(); let posts = 0, reads = 0;
  await assert.rejects(env.settle(action(env, { kind: "resume",
    submitResume: async () => { ++posts; return new Promise(() => {}); },
    inspectRuns: async () => { ++reads; return new Promise(() => {}); },
  })), { code: "recovery_resume_unconfirmed" });
  assert.equal(posts, 1); assert.equal(reads, 5);
  assert.ok(env.elapsed() <= 60000, "submission, reads and backoff share the time cap");
});

test("page exit aborts both submission and confirmation without another request", async () => {
  for (const confirming of [false, true]) {
    const env = environment(); let posts = 0, reads = 0;
    const pending = action(env, { kind: "resume",
      submitResume: async () => { ++posts; if (confirming) throw lost(); return new Promise(() => {}); },
      inspectRuns: async () => { ++reads; return new Promise(() => {}); },
    });
    await new Promise(resolve => setImmediate(resolve));
    env.page.dispatchEvent(new Event("pagehide"));
    await assert.rejects(pending, error => error.name === "AbortError" && error.recoveryActionUncertain);
    assert.equal(posts, 1); assert.equal(reads, confirming ? 1 : 0); assert.equal(env.timers.size, 0);
  }
});

test("target changes and host epochs prevent an older acknowledgement from applying", async () => {
  for (const switched of [false, true]) {
    const env = environment(); let generation = "old", reads = 0;
    const pending = action(env, { kind: "resume", token: "old-token", epoch: () => generation,
      submitResume: async () => { if (switched) generation = "new"; throw lost(); },
      inspectRuns: async () => { ++reads; return { data: { items: [run] }, writeToken: "new-token" }; },
    });
    await assert.rejects(env.settle(pending), error => switched ?
      error.name === "AbortError" : error.code === "write_token_conflict");
    assert.equal(reads, switched ? 0 : 1);
  }
});

test("default ending confirmation releases a timed-out runtime before its next read", async context => {
  const env = environment(), data = { ...owner, session_id: "ending-read-deadline" };
  const closed = { ...data, resume_required: false, revision: 4, last_sequence: 8 };
  let release, reads = 0, posts = 0, firstSignal;
  const reply = () => Response.json({ ok: true, data: closed });
  context.after(async () => { release?.(reply()); await new Promise(resolve => setImmediate(resolve)); });
  context.mock.method(globalThis, "fetch", async (_path, options) => {
    if (++reads === 1) {
      firstSignal = options.signal;
      return new Promise(resolve => { release = resolve; });
    }
    return reply();
  });
  assert.deepEqual(await env.settle(action(env, { kind: "abandon", data,
    submitAbandon: async () => { ++posts; throw lost(); },
  })), closed);
  assert.equal(posts, 1); assert.equal(reads, 2); assert.equal(env.elapsed(), 8500);
  assert(firstSignal.aborted);
  assert.equal(await withSessionRuntime(data.project_id, data.session_id, async () => "available"), "available");
});

test("leaving during default ending confirmation releases its runtime and ignores late data", async context => {
  const env = environment(), data = { ...owner, session_id: "ending-read-pagehide" };
  let release, reads = 0, entered = false;
  context.after(async () => { release?.(Response.json({ ok: true, data: owner }));
    await new Promise(resolve => setImmediate(resolve)); });
  context.mock.method(globalThis, "fetch", async () => {
    ++reads; return new Promise(resolve => { release = resolve; });
  });
  const pending = action(env, { kind: "abandon", data, submitAbandon: async () => { throw lost(); } });
  await new Promise(resolve => setImmediate(resolve));
  env.page.dispatchEvent(new Event("pagehide"));
  await assert.rejects(pending, error => error.name === "AbortError" && error.recoveryActionUncertain);
  const follower = withSessionRuntime(data.project_id, data.session_id, async () => { entered = true; });
  await new Promise(resolve => setImmediate(resolve));
  assert(entered, "page exit must not leave the confirmation owning this runtime");
  await follower; assert.equal(reads, 1); assert.equal(env.timers.size, 0);
});

test("a cancelled ending confirmation waiting behind an owner never opens a ghost runtime", async context => {
  const env = environment(), data = { ...owner, session_id: "ending-wait-cancel" };
  let release, reads = 0;
  const active = withSessionRuntime(data.project_id, data.session_id,
    () => new Promise(resolve => { release = resolve; }));
  await new Promise(resolve => setImmediate(resolve));
  context.after(async () => { release(); await active; });
  context.mock.method(globalThis, "fetch", async () => {
    ++reads; return Response.json({ ok: true, data: { ...data, resume_required: false, revision: 4, last_sequence: 8 } });
  });
  const pending = action(env, { kind: "abandon", data, submitAbandon: async () => { throw lost(); } });
  await new Promise(resolve => setImmediate(resolve));
  env.page.dispatchEvent(new Event("pagehide"));
  await assert.rejects(pending, error => error.name === "AbortError" && error.recoveryActionUncertain);
  assert.equal(reads, 0);
  release(); await active;
  for (let step = 0; step < 3; ++step) await new Promise(resolve => setImmediate(resolve));
  assert.equal(reads, 0, "a cancelled waiter must not inspect after its predecessor exits");
  assert.equal(await withSessionRuntime(data.project_id, data.session_id, async () => "available"), "available");
});
