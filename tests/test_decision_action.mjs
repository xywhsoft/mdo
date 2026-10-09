import assert from "node:assert/strict";
import test from "node:test";
import { runDecisionAction } from "../app/web/js/state/decision-action.js";
import { hasPendingApiWrites } from "../app/web/js/api/client.js";

const lost = (code = "network_error", status = 0) => Object.assign(new Error("fixture failure"), { code, status });
const reply = (data = { id: 17, decision: "allow" }, writeToken = "host") => ({ data, writeToken });
const tick = () => new Promise(resolve => setImmediate(resolve));

function environment() {
  let time = 0, serial = 0;
  const timers = new Map(), waits = [], page = new EventTarget();
  return { page, timers, waits, elapsed: () => time, recoveryOptions: {
    now: () => time, random: () => 0, eventTarget: page,
    setTimer(fn, delay) { timers.set(++serial, { fn, at: time + delay, delay }); return serial; },
    clearTimer(key) { timers.delete(key); },
  }, async settle(promise) {
    let done = false, value, error;
    promise.then(result => { value = result; done = true; }, result => { error = result; done = true; });
    for (let step = 0; !done && step < 100; ++step) {
      await tick(); if (done) break;
      assert.ok(timers.size, "every pending decision has a deadline");
      const [key, timer] = [...timers].sort((a, b) => a[1].at - b[1].at)[0];
      timers.delete(key); time = timer.at; waits.push(timer.delay); timer.fn();
    }
    assert.equal(done, true); assert.equal(timers.size, 0);
    if (error) throw error;
    return value;
  } };
}
const action = (env, options) => runDecisionAction({ path: "/approvals/17", id: 17,
  field: "decision", value: "allow", epoch: () => "target", token: "host",
  recoveryOptions: env.recoveryOptions, ...options });

test("an exact acknowledgement completes immediately", async () => {
  const env = environment(); let count = 0;
  assert.deepEqual(await env.settle(action(env, { send: async (path, body, { signal }) => {
    ++count; assert.equal(path, "/approvals/17"); assert.deepEqual(body, { decision: "allow" });
    assert.ok(signal instanceof AbortSignal); return reply();
  } })), reply().data);
  assert.equal(count, 1); assert.deepEqual(env.waits, []);
});

test("lost acknowledgement retries only the identical decision", async () => {
  for (const code of ["network_error", "remote_result_unconfirmed", "invalid_response"]) {
    const env = environment(), bodies = []; let count = 0;
    await env.settle(action(env, { send: async (path, body) => {
      bodies.push([path, body]); if (++count === 1) throw lost(code); return reply();
    } }));
    assert.equal(count, 2); assert.deepEqual(bodies[0], bodies[1]); assert.deepEqual(env.waits, [500]);
  }
});

test("a stalled response body releases write tracking and recovers", async () => {
  const env = environment(), original = globalThis.fetch; let count = 0, firstSignal;
  globalThis.fetch = async (_path, options) => {
    if (++count === 1) {
      firstSignal = options.signal;
      return { ok: true, status: 200, headers: new Headers(), json: () => new Promise(() => {}) };
    }
    return Response.json({ ok: true, data: reply().data });
  };
  try {
    await env.settle(action(env, { token: null }));
    assert.equal(count, 2); assert.equal(firstSignal.aborted, true);
    assert.equal(hasPendingApiWrites(), false); assert.deepEqual(env.waits, [8000, 500]);
  } finally { globalThis.fetch = original; }
});

test("an unresolved submission has one accurate final failure and bounded attempts", async () => {
  for (const [field, path, id, value, code] of [
    ["decision", "/approvals/17", 17, "allow", "approval_decision_unconfirmed"],
    ["answer", "/projects/default/sessions/session/asks/17", 17, "回答", "ask_answer_unconfirmed"],
    ["answer", "/tasks/9/asks/17", 17, "回答", "ask_answer_unconfirmed"],
  ]) {
    const env = environment(); let count = 0;
    await assert.rejects(env.settle(action(env, { field, path, id, value, send: async () => {
      ++count; throw lost("network_error");
    } })), { code });
    assert.equal(count, 6); assert.equal(env.elapsed(), 15500);
  }
});

test("permanent refusals are not retried", async () => {
  for (const [code, status] of [["decision_conflict", 409], ["ask_not_found", 404],
    ["permission_denied", 403], ["home_restart_required", 503], ["write_token_invalid", 403]]) {
    const env = environment(); let count = 0;
    await assert.rejects(env.settle(action(env, { send: async () => { ++count; throw lost(code, status); } })), { code });
    assert.equal(count, 1); assert.deepEqual(env.waits, []);
  }
});

test("explicit temporary refusals retain their accurate final error", async () => {
  for (const code of ["decisions_unavailable", "asks_unavailable", "approvals_unavailable"]) {
    const env = environment(); let count = 0;
    await assert.rejects(env.settle(action(env, { send: async () => {
      ++count; throw lost(code, 503);
    } })), { code });
    assert.equal(count, 6);
  }
});

test("a later refusal cannot confirm an earlier lost submission", async () => {
  const env = environment(); let count = 0;
  await assert.rejects(env.settle(action(env, { send: async () => {
    if (++count === 1) throw lost();
    throw lost("decisions_unavailable", 503);
  } })), { code: "approval_decision_unconfirmed" });
  assert.equal(count, 6);
});

test("a response for a different value or ID cannot acknowledge this intent", async () => {
  for (const data of [{ id: 18, decision: "allow" }, { id: 17, decision: "deny" }, {}]) {
    const env = environment(); let count = 0;
    await assert.rejects(env.settle(action(env, { send: async () => { ++count; return reply(data); } })),
      { code: "approval_decision_unconfirmed" });
    assert.equal(count, 6);
  }
});

test("a restarted host or changed target does not receive a replay", async () => {
  let target = "old", count = 0;
  const env = environment();
  await assert.rejects(env.settle(action(env, { epoch: () => target, send: async () => {
    ++count; target = "new"; throw lost();
  } })), { name: "AbortError" });
  assert.equal(count, 1);
  const next = environment(); count = 0;
  await assert.rejects(next.settle(action(next, { send: async () => { ++count; return reply(reply().data, "new-host"); } })),
    { code: "write_token_conflict" });
  assert.equal(count, 1);
});

test("leaving the page cancels a hanging decision and its timers", async () => {
  const env = environment(); let signal, calls = 0;
  const pending = action(env, { send: async (_path, _body, options) => {
    ++calls; signal = options.signal; return new Promise(() => {});
  } });
  await tick(); env.page.dispatchEvent(new Event("pagehide"));
  await assert.rejects(pending, { name: "AbortError" });
  assert.equal(signal.aborted, true); assert.equal(calls, 1); assert.equal(env.timers.size, 0);
});

test("other mutations cannot accidentally opt into decision replay", async () => {
  for (const options of [{ path: "/runs" }, { path: "/approvals/18" }, { field: "prompt" },
    { path: "/tasks/9/asks/17", field: "decision" }]) {
    const env = environment(); let calls = 0;
    await assert.rejects(action(env, { ...options, send: async () => { ++calls; } }), TypeError);
    assert.equal(calls, 0); assert.equal(env.timers.size, 0);
  }
});
