import assert from "node:assert/strict";
import test from "node:test";
import { ApiError } from "../app/web/js/api/client.js";
import { createRequestRecovery } from "../app/web/js/api/request-recovery.js";
import { createSessionProfileUpdater } from "../app/web/js/state/session-profile.js";
import { withSessionRuntime } from "../app/web/js/state/session-runtime.js";

const source = { project_id: "default", id: "source", revision: 1 };
const desired = { model_id: "code", reasoning_effort: "high", permission_profile: "balanced" };
const network = () => new ApiError("Lost response", { code: "network_error" });
const unavailable = () => new ApiError("Not applied", { code: "session_service_unavailable", status: 503 });
const stale = () => new ApiError("Stale revision", { code: "revision_conflict", status: 412 });
const reply = (data = {}, metadata = {}) => {
  const result = { ...source, ...desired, revision: 2, status: "active", ...data };
  return { data: result, etag: `"mdo-session-${result.id}-${result.revision}"`, writeToken: "epoch", ...metadata };
};
const flush = () => new Promise(resolve => setImmediate(resolve));
function environment(options = {}) {
  let time = 0, serial = 0, generation = "first";
  const timers = new Map(), waits = [], page = new EventTarget();
  const env = { timers, waits, page, epoch: () => generation, token: () => "epoch",
    changeEpoch: () => { generation = "next"; }, elapsed: () => time,
    createRecovery: () => createRequestRecovery({ now: () => time, random: () => 0, eventTarget: page,
      setTimer(fn, delay) { timers.set(++serial, { fn, at: time + delay, delay }); return serial; },
      clearTimer(id) { timers.delete(id); } }),
    async settle(promise) {
      let done = false, result, error;
      promise.then(value => { result = value; done = true; }, reason => { error = reason; done = true; });
      for (let i = 0; i < 100 && !done; i++) {
        await flush(); if (done) break;
        assert.ok(timers.size, "a pending profile must own a deadline");
        const [id, timer] = [...timers].sort((a, b) => a[1].at - b[1].at)[0];
        timers.delete(id); time = timer.at; waits.push(timer.delay); timer.fn();
      }
      assert.ok(done); assert.equal(timers.size, 0);
      if (error) throw error;
      return result;
    },
  };
  env.update = createSessionProfileUpdater({ ...env, ...options }); return env;
}

test("an exact profile acknowledgement completes without a confirmation read", async () => {
  let writes = 0;
  const env = environment({ send: async (_path, body, options) => {
    writes++; assert.deepEqual(body, desired); assert.ok(Object.isFrozen(body));
    assert.equal(options.ifMatch, '"mdo-session-source-1"'); return reply();
  }, inspect: () => { throw new Error("not needed"); } });
  assert.equal((await env.settle(env.update(source, desired))).model_id, "code");
  assert.equal(writes, 1);
});

test("a lost profile acknowledgement quietly reads the current desired state", async () => {
  let writes = 0, reads = 0;
  const env = environment({ send: async () => { writes++; throw network(); },
    inspect: async path => { reads++; assert.equal(path, "/projects/default/sessions/source"); return reply(); } });
  await env.settle(env.update(source, desired));
  assert.equal(writes, 1); assert.equal(reads, 1); assert.deepEqual(env.waits, [500]);
});

test("an unchanged read permits only the original ETag and frozen profile", async () => {
  const writes = []; let reads = 0;
  const env = environment({ send: async (_path, body, options) => {
    writes.push({ body, options }); if (writes.length === 1) throw network(); return reply();
  }, inspect: async () => { reads++; return reply({ revision: 1, model_id: "text" }); } });
  await env.settle(env.update(source, desired));
  assert.equal(reads, 1); assert.equal(writes.length, 2);
  assert.equal(writes[0].body, writes[1].body);
  assert.equal(writes[0].options.ifMatch, writes[1].options.ifMatch);
  assert.deepEqual(env.waits, [500, 1000]);
});

test("a stale PUT is confirmed only when its desired settings are current", async () => {
  for (const matching of [true, false]) {
    let writes = 0, reads = 0;
    const env = environment({ send: async () => { writes++; throw stale(); },
      inspect: async () => { reads++; return reply(matching ? {} : { model_id: "peer" }); } });
    const pending = env.settle(env.update(source, desired));
    if (matching) assert.equal((await pending).revision, 2);
    else await assert.rejects(pending, error => error.code === "session_profile_conflict" &&
      error.currentSession.model_id === "peer");
    assert.equal(writes, 1); assert.equal(reads, 1);
  }
});

test("a peer revision after lost delivery is never overwritten", async () => {
  let writes = 0;
  const env = environment({ send: async () => { writes++; throw network(); },
    inspect: async () => reply({ revision: 3, permission_profile: "read-only" }) });
  await assert.rejects(env.settle(env.update(source, desired)), { code: "session_profile_conflict" });
  assert.equal(writes, 1);
});

test("known refusal retries quietly while preserving any earlier uncertain delivery", async () => {
  let writes = 0, reads = 0;
  const env = environment({ send: async () => { writes++; if (writes === 1) throw network(); throw unavailable(); },
    inspect: async () => { reads++; return reads === 1 ? reply({ revision: 1, model_id: "text" }) : reply(); } });
  await env.settle(env.update(source, desired));
  assert.equal(writes, 2); assert.equal(reads, 2);
  assert.deepEqual(env.waits, [500, 1000, 2000]);
});

test("permanent refusals are not retried; known and unknown exhaustion are distinct", async () => {
  for (const [error, final, count] of [[new ApiError("Denied", { status: 403, code: "permission_denied" }),
      "permission_denied", 1], [new ApiError("Unsupported reasoning", {
        status: 422, code: "model_profile_reasoning_unsupported" }),
        "model_profile_reasoning_unsupported", 1], [unavailable(), "session_profile_unavailable", 6],
    [network(), "session_profile_unconfirmed", 6]]) {
    let attempts = 0;
    const env = environment({ send: async () => { attempts++; throw error; },
      inspect: async () => { attempts++; throw network(); } });
    await assert.rejects(env.settle(env.update(source, desired)), { code: final });
    assert.equal(attempts, count);
  }
});

test("invalid ownership, revision and ETag cannot confirm a profile", async () => {
  for (const [data, metadata] of [[{ id: "neighbour" }, {}], [{ project_id: "other" }, {}],
    [{ revision: 0 }, {}], [{ revision: 1 }, { etag: '"mdo-session-source-2"' }]]) {
    let writes = 0, reads = 0;
    const env = environment({ send: async () => { writes++; throw network(); },
      inspect: async () => { reads++; return reply(data, metadata); } });
    await assert.rejects(env.settle(env.update(source, desired)), { code: "session_profile_unconfirmed" });
    assert.equal(writes, 1); assert.equal(reads, 5);
  }
});

test("restart fencing and a changed device do not apply a late acknowledgement", async () => {
  const fenced = environment({ send: async () => reply({}, { writeToken: "restarted" }) });
  await assert.rejects(fenced.settle(fenced.update(source, desired)), { code: "write_token_conflict" });
  const moved = environment({ send: async () => { moved.changeEpoch(); return reply(); } });
  await assert.rejects(moved.settle(moved.update(source, desired)), { name: "AbortError" });
});

test("a hanging body shares one minute and six total attempts", async () => {
  let writes = 0, reads = 0;
  const env = environment({ send: () => { writes++; return new Promise(() => {}); },
    inspect: () => { reads++; return new Promise(() => {}); } });
  await assert.rejects(env.settle(env.update(source, desired)), { code: "session_profile_unconfirmed" });
  assert.equal(writes, 1); assert.equal(reads, 5); assert.equal(env.elapsed(), 60000);
});

test("cancellation and page exit release profile waiting and prevent late effects", async () => {
  for (const external of [true, false]) {
    let finish, writes = 0;
    const controller = new AbortController();
    const env = environment({ send: () => { writes++; return new Promise(resolve => { finish = resolve; }); } });
    const action = env.update(source, desired, { signal: controller.signal });
    await flush(); external ? controller.abort() : env.page.dispatchEvent(new Event("pagehide"));
    await assert.rejects(action, { name: "AbortError" });
    finish(reply()); await flush(); assert.equal(writes, 1); assert.equal(env.timers.size, 0);
  }
});

test("waiting for the runtime is bounded without claiming an unsubmitted PUT", async () => {
  let release, writes = 0, reads = 0;
  const owner = withSessionRuntime("default", "source", () => new Promise(resolve => { release = resolve; }));
  await flush();
  const env = environment({ send: () => { writes++; return reply(); }, inspect: () => { reads++; return reply(); } });
  try {
    await assert.rejects(env.settle(env.update(source, desired)), { code: "session_profile_unavailable" });
    assert.equal(writes, 0); assert.equal(reads, 0); assert.equal(env.elapsed(), 60000);
  } finally { release(); await owner; await flush(); }
});

test("invalid profile and source guards never issue a request", async () => {
  const env = environment({ send: () => { throw new Error("must not write"); } });
  for (const [session, profile] of [[{ ...source, revision: 0 }, desired],
    [{ ...source, etag: '"mdo-session-other-1"' }, desired], [source, { ...desired, model_id: "" }],
    [source, { ...desired, permission_profile: "unsafe" }], [source, { ...desired, reasoning_effort: null }]])
    await assert.rejects(env.update(session, profile), TypeError);
});
