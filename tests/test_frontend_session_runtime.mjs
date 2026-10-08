import assert from "node:assert/strict";
import test from "node:test";

import { withSessionRuntime } from "../app/web/js/state/session-runtime.js";
import { selectRecovery, loadRecovery, readRecovery } from "../app/web/js/state/recovery.js";
import { startRun } from "../app/web/js/state/runs.js";
import { createRequestRecovery } from "../app/web/js/api/request-recovery.js";

function deferred() {
  let resolve;
  const promise = new Promise((done) => { resolve = done; });
  return { promise, resolve };
}

test("a new run waits for navigation's recovery inspection", async () => {
  const inspection = deferred();
  const entered = deferred();
  const calls = [];
  const originalFetch = globalThis.fetch;
  globalThis.fetch = async (path, options) => {
    const method = options.method;
    calls.push(method);
    if (method === "GET") {
      entered.resolve();
      await inspection.promise;
      return Response.json({ ok: true, data: inspectionData("new-session") });
    }
    assert.equal(method, "POST");
    return Response.json({ ok: true, data: runData("new-session") }, { status: 202 });
  };
  try {
    selectRecovery("default", "new-session");
    const loading = loadRecovery();
    await entered.promise;
    const run = startRun("default", "new-session", "hello");
    await Promise.resolve();
    assert.deepEqual(calls, ["GET"]);
    inspection.resolve();
    await Promise.all([loading, run]);
    assert.deepEqual(calls, ["GET", "POST"]);
    assert.equal((await run).id, "run-1");
  } finally {
    inspection.resolve();
    globalThis.fetch = originalFetch;
    selectRecovery("", "");
  }
});

test("a failed operation releases its session without blocking other sessions", async () => {
  const entered = deferred();
  const release = deferred();
  const first = withSessionRuntime("default", "a", async () => {
    entered.resolve();
    await release.promise;
    throw new Error("inspection failed");
  });
  await entered.promise;
  const second = withSessionRuntime("default", "a", async () => "next");
  const independent = withSessionRuntime("default", "b", async () => "other");
  assert.equal(await independent, "other");
  release.resolve();
  await assert.rejects(first, /inspection failed/);
  assert.equal(await second, "next");
});

test("a lost run response is reported as uncertain without replaying the POST", async () => {
  const originalFetch = globalThis.fetch;
  let posts = 0;
  globalThis.fetch = async (_path, options) => {
    if (options.method === "GET") return failure("permission_denied", 403);
    posts += 1;
    throw new TypeError("connection closed after acceptance");
  };
  try {
    await assert.rejects(startRun("default", "lost-response", "hello"),
      (error) => error.code === "network_error" &&
        error.runAdmissionUncertain === true);
    assert.equal(posts, 1);
  } finally { globalThis.fetch = originalFetch; }
});

test("an explicit run rejection keeps its ordinary failure status", async () => {
  const originalFetch = globalThis.fetch;
  globalThis.fetch = async () => Response.json({ ok: false,
    error: { code: "session_busy", message: "Session is busy" } },
  { status: 409 });
  try {
    await assert.rejects(startRun("default", "busy-session", "hello"),
      (error) => error.code === "session_busy" &&
        error.runAdmissionUncertain !== true);
  } finally { globalThis.fetch = originalFetch; }
});

test("a competing queue start requires review without replay", async () => {
  const originalFetch = globalThis.fetch;
  let posts = 0;
  globalThis.fetch = async (_path, options) => {
    if (options.method === "GET") return failure("permission_denied", 403);
    posts += 1;
    return Response.json({ ok: false, error: {
      code: "queue_run_starting", message: "Start already claimed",
    } }, { status: 409 });
  };
  try {
    await assert.rejects(startRun("default", "claimed-session", "hello", [],
      "a".repeat(32)), (error) => error.code === "queue_run_starting" &&
        error.runAdmissionUncertain === true);
    assert.equal(posts, 1);
  } finally { globalThis.fetch = originalFetch; }
});

test("a run error after the execution boundary requires review", async () => {
  const originalFetch = globalThis.fetch;
  let posts = 0;
  globalThis.fetch = async () => {
    posts += 1;
    return Response.json({ ok: false, error: {
      code: "run_start_uncertain", message: "Start outcome unknown",
    } }, { status: 503 });
  };
  try {
    await assert.rejects(startRun("default", "uncertain-start", "hello"),
      (error) => error.code === "run_start_uncertain" &&
        error.runAdmissionUncertain === true);
    assert.equal(posts, 1);
  } finally { globalThis.fetch = originalFetch; }
});

const flush = () => new Promise(resolve => setImmediate(resolve));
const ok = data => Response.json({ ok: true, data });
const failure = (code, status = 503) => Response.json({ ok: false,
  error: { code, message: "fixture unavailable" } }, { status });
const queueId = "a".repeat(32);
const receipt = (state = "accepted") => ({ id: queueId, state,
  ...(state === "accepted" ? { run_id: "run-1" } : {}) });
function runData(session, overrides = {}) {
  return { id: "run-1", project_id: "default", session_id: session,
    state: "succeeded", terminal: true, ...overrides };
}
function inspectionData(session, overrides = {}) {
  return { project_id: "default", session_id: session, revision: 1,
    last_sequence: 0, resume_required: false, total: 0, items: [], ...overrides };
}
function environment() {
  let time = 0, serial = 0; const timers = new Map(), waits = [], page = new EventTarget();
  const createRecovery = () => createRequestRecovery({ now: () => time, random: () => 0,
    eventTarget: page,
    setTimer(fn, delay) { timers.set(++serial, { fn, at: time + delay, delay }); return serial; },
    clearTimer(id) { timers.delete(id); } });
  return { createRecovery, timers, waits, page, elapsed: () => time,
    async settle(promise) {
      let value, error, done = false;
      promise.then(result => { value = result; done = true; }, failure => { error = failure; done = true; });
      for (let step = 0; !done && step < 100; ++step) {
        await flush(); if (done) break;
        assert.ok(timers.size, "runtime inspection/admission must have a finite deadline");
        const [id, timer] = [...timers].sort((a, b) => a[1].at - b[1].at)[0];
        timers.delete(id); time = timer.at; waits.push(timer.delay); timer.fn();
      }
      assert.equal(done, true); assert.equal(timers.size, 0);
      if (error) throw error;
      return value;
    },
  };
}

test("foreground runtime inspection quietly recovers transient failures before sending", async context => {
  const env = environment(); let reads = 0;
  context.mock.method(globalThis, "fetch", async (_path, options) => {
    assert(options.signal instanceof AbortSignal);
    return ++reads < 3 ? failure("recovery_unavailable") : ok(inspectionData("inspect-retry"));
  });
  const data = await env.settle(readRecovery("default", "inspect-retry", env));
  assert.equal(data.session_id, "inspect-retry"); assert(data.inspection_id > 0);
  assert.equal(reads, 3); assert.deepEqual(env.waits, [500, 1000]);
});

test("wrong-session and malformed inspection bodies never prove runtime availability", async context => {
  const env = environment(); let reads = 0;
  context.mock.method(globalThis, "fetch", async () => { reads++; return ok(inspectionData("other")); });
  await assert.rejects(env.settle(readRecovery("default", "inspect-wrong", env)), { code: "invalid_response" });
  assert.equal(reads, 1);
  context.mock.method(globalThis, "fetch", async () => ok(inspectionData("inspect-wrong", { resume_required: "false" })));
  await assert.rejects(env.settle(readRecovery("default", "inspect-wrong", env)), { code: "invalid_response" });
});

test("inspection timeouts release the session even when the transport ignores abort", async context => {
  const env = environment(); const signals = []; let hanging = true;
  context.mock.method(globalThis, "fetch", async (_path, options) => {
    signals.push(options.signal);
    return hanging ? new Promise(() => {}) : ok(inspectionData("inspect-hanging"));
  });
  await assert.rejects(env.settle(readRecovery("default", "inspect-hanging", env)), { code: "network_error" });
  assert.equal(signals.length, 6); assert(signals.every(signal => signal.aborted));
  assert(env.elapsed() <= 60000);
  hanging = false;
  assert.equal((await env.settle(readRecovery("default", "inspect-hanging", env))).session_id, "inspect-hanging");
});

test("cancelling a queued inspection cannot let its follower bypass the active runtime", { timeout: 1000 }, async () => {
  const gate = deferred(), controller = new AbortController(); const order = [];
  const first = withSessionRuntime("default", "cancel-middle", async () => { order.push("first"); await gate.promise; });
  await flush();
  const middle = withSessionRuntime("default", "cancel-middle", async () => { order.push("cancelled"); }, { signal: controller.signal });
  const aborted = assert.rejects(middle, { name: "AbortError" }); controller.abort(); await aborted;
  const last = withSessionRuntime("default", "cancel-middle", async () => { order.push("last"); });
  await flush(); assert.deepEqual(order, ["first"]);
  gate.resolve(); await Promise.all([first, last]); assert.deepEqual(order, ["first", "last"]);
});

test("a lost run response is confirmed by its exact queue receipt without replaying POST", async context => {
  const env = environment(); let posts = 0, receipts = 0, reads = 0;
  context.mock.method(globalThis, "fetch", async (path, options) => {
    assert(options.signal instanceof AbortSignal);
    if (options.method === "POST") { posts++; throw new TypeError("response lost"); }
    if (path.endsWith(`/queue/${queueId}`)) {
      return ++receipts === 1 ? ok(receipt("starting")) : ok(receipt());
    }
    assert.equal(path, "/api/v1/runs/run-1");
    return ++reads === 1 ? failure("run_unavailable") : ok(runData("confirm-start"));
  });
  const run = await env.settle(startRun("default", "confirm-start", "hello", [], queueId, env));
  assert.equal(run.session_id, "confirm-start"); assert.equal(posts, 1);
  assert.equal(receipts, 2); assert.equal(reads, 2); assert.deepEqual(env.waits, [500, 1000]);
});

test("a slow admission response times out and confirms the real accepted run", async context => {
  const env = environment(); let posts = 0; let postSignal;
  context.mock.method(globalThis, "fetch", async (path, options) => {
    if (options.method === "POST") { posts++; postSignal = options.signal; return new Promise(() => {}); }
    return ok(path.endsWith(`/queue/${queueId}`) ? receipt() : runData("slow-start"));
  });
  assert.equal((await env.settle(startRun("default", "slow-start", "hello", [], queueId, env))).id, "run-1");
  assert.equal(posts, 1); assert.equal(postSignal.aborted, true); assert.equal(env.elapsed(), 8000);
});

test("missing receipts exhaust quietly within the shared cap and leave only final uncertainty", async context => {
  const env = environment(); let posts = 0, reads = 0;
  context.mock.method(globalThis, "fetch", async (_path, options) => {
    if (options.method === "POST") { posts++; throw new TypeError("lost response"); }
    reads++; return failure("queue_receipt_not_found", 404);
  });
  await assert.rejects(env.settle(startRun("default", "missing-start", "hello", [], queueId, env)), error =>
    error.runAdmissionUncertain && error.code === "network_error" &&
    error.confirmationError?.code === "run_confirmation_pending");
  assert.equal(posts, 1); assert.equal(reads, 6); assert.equal(env.elapsed(), 15500);
});

test("confirmation can never adopt another queue item or another session's run", async context => {
  for (const wrongReceipt of [true, false]) {
    const env = environment(); let reads = 0, posts = 0;
    context.mock.method(globalThis, "fetch", async (path, options) => {
      if (options.method === "POST") { posts++; throw new TypeError("lost response"); }
      reads++;
      return ok(path.endsWith(`/queue/${queueId}`)
        ? { ...receipt(), ...(wrongReceipt ? { id: "b".repeat(32) } : {}) }
        : runData("other-session"));
    });
    await assert.rejects(env.settle(startRun("default", "scope-start", "hello", [], queueId, env)), error =>
      error.runAdmissionUncertain && error.confirmationError?.code === "invalid_response");
    assert.equal(posts, 1); assert.equal(reads, wrongReceipt ? 1 : 2);
  }
});

test("a definite quota refusal remains accurate and never performs confirmation or replay", async context => {
  const env = environment(); let calls = 0;
  context.mock.method(globalThis, "fetch", async () => { calls++; return failure("daily_token_limit", 429); });
  await assert.rejects(env.settle(startRun("default", "quota-start", "hello", [], queueId, env)), error =>
    error.code === "daily_token_limit" && !error.runAdmissionUncertain);
  assert.equal(calls, 1);
});

test("a start without a correlation ID times out once and never guesses an accepted run", async context => {
  const env = environment(); let posts = 0;
  context.mock.method(globalThis, "fetch", async () => { posts++; return new Promise(() => {}); });
  await assert.rejects(env.settle(startRun("default", "unkeyed-start", "hello", [], "", env)), error =>
    error.runAdmissionUncertain && error.code === "network_error");
  assert.equal(posts, 1); assert.equal(env.elapsed(), 8000);
  context.mock.method(globalThis, "fetch", async () => ok(runData("unkeyed-start")));
  assert.equal((await env.settle(startRun("default", "unkeyed-start", "next", [], "", env))).id, "run-1");
});

test("page exit cancels confirmation without forgetting that the POST may have succeeded", async context => {
  const env = environment(); let posts = 0, reads = 0;
  context.mock.method(globalThis, "fetch", async (_path, options) => {
    if (options.method === "POST") { posts++; throw new TypeError("lost response"); }
    reads++; return new Promise(() => {});
  });
  const pending = startRun("default", "exit-start", "hello", [], queueId, env); await flush();
  env.page.dispatchEvent(new Event("pagehide"));
  await assert.rejects(env.settle(pending), error => error.runAdmissionUncertain);
  assert.equal(posts, 1); assert.equal(reads, 1);
});

test("cancelling before acquiring the runtime sends no POST and preserves the active owner", async context => {
  const env = environment(), gate = deferred(); let posts = 0, followerEntered = false;
  context.mock.method(globalThis, "fetch", async () => { posts++; return ok(runData("waiting-start")); });
  const owner = withSessionRuntime("default", "waiting-start", () => gate.promise); await flush();
  const start = startRun("default", "waiting-start", "hello", [], queueId, env); await flush();
  env.page.dispatchEvent(new Event("pagehide"));
  await assert.rejects(env.settle(start), error => error.name === "AbortError" && !error.runAdmissionUncertain);
  const follower = withSessionRuntime("default", "waiting-start", async () => { followerEntered = true; });
  await flush(); assert.equal(posts, 0); assert.equal(followerEntered, false);
  gate.resolve(); await Promise.all([owner, follower]); assert.equal(followerEntered, true);
});
