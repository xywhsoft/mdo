import assert from "node:assert/strict";
import test from "node:test";
import { createScheduleWriter } from "../app/web/js/state/schedule-write.js";
import { createRequestRecovery } from "../app/web/js/api/request-recovery.js";

const flush = () => new Promise(resolve => setImmediate(resolve));
const failure = (code = "network_error", status = 0) => Object.assign(new Error("fixture failure"), { code, status });
const body = { id: "sc-c-one", label: "One", input: "Work", start_at: 1700000000000000 };
const tag = revision => `"mdo-schedule-${body.id}-${revision}"`;
const reply = (revision = 1, fields = {}) => ({ writeToken: "first-token", etag: tag(revision),
  data: { ...body, revision, enabled: true, ...fields } });

function environment(client) {
  let time = 0, serial = 0, epoch = "first";
  const timers = new Map(), waits = [], events = new EventTarget();
  const writer = createScheduleWriter({ client, epoch: () => epoch, token: () => "first-token",
    createRecovery: () => createRequestRecovery({ now: () => time, random: () => 0, eventTarget: events,
      setTimer(fn, delay) { timers.set(++serial, { fn, at: time + delay, delay }); return serial; },
      clearTimer(id) { timers.delete(id); } }) });
  return { writer, events, timers, waits, elapsed: () => time, changeOwner() { epoch = "second"; },
    async settle(work) {
      let done = false, value, error;
      work.then(result => { done = true; value = result; }, reason => { done = true; error = reason; });
      for (let count = 0; count < 80 && !done; ++count) {
        await flush(); if (done) break;
        assert.ok(timers.size, "every pending save owns a finite deadline");
        const [id, timer] = [...timers].sort((a, b) => a[1].at - b[1].at)[0];
        timers.delete(id); time = timer.at; waits.push(timer.delay); timer.fn();
      }
      assert.equal(done, true); assert.equal(timers.size, 0);
      if (error) throw error; return value;
    },
  };
}

test("lost create acknowledgement reads the original identity without another POST", async () => {
  const calls = [];
  const env = environment({ post: async (_path, sent) => {
    calls.push("POST"); assert.deepEqual(sent, body); throw failure();
  }, get: async path => { calls.push(path); return reply(); } });
  assert.equal((await env.settle(env.writer.create(body))).data.id, body.id);
  assert.deepEqual(calls, ["POST", `/schedules/${body.id}`]);
});

test("browser duplicate conflict after creation is confirmed against the complete definition", async () => {
  let posts = 0, gets = 0;
  const env = environment({ post: async () => { posts++; throw failure("schedule_conflict", 409); },
    get: async () => { gets++; return reply(); } });
  assert.equal((await env.settle(env.writer.create(body))).data.revision, 1);
  assert.equal(posts, 1); assert.equal(gets, 1);
});

test("an original ID not yet visible recovers quietly, never submitting a substitute", async () => {
  let posts = 0, gets = 0;
  const env = environment({ post: async () => { posts++; throw failure(); },
    get: async () => { if (++gets < 3) throw failure("schedule_not_found", 404); return reply(); } });
  await env.settle(env.writer.create(body));
  assert.equal(posts, 1); assert.equal(gets, 3); assert.deepEqual(env.waits, [500, 1000]);
});

test("native read service failures recover even when that code is a definite write refusal", async () => {
  let gets = 0;
  const env = environment({ post: async () => { throw failure(); }, get: async () => {
    if (++gets < 3) throw failure("schedule_service_unavailable", 503); return reply();
  } });
  await env.settle(env.writer.create(body)); assert.equal(gets, 3);
});

test("unconfirmed save retry remains read-only and retains the frozen original body", async () => {
  let posts = 0, gets = 0, unavailable = true;
  const env = environment({ post: async () => { posts++; throw failure(); }, get: async () => {
    gets++; if (unavailable) throw failure(); return reply();
  } });
  await assert.rejects(env.settle(env.writer.create(body)), { code: "schedule_save_unconfirmed" });
  assert.equal(posts, 1); assert.equal(gets, 6);
  await assert.rejects(env.settle(env.writer.create({ ...body, label: "Changed" })),
    { code: "schedule_save_pending_changed" });
  assert.equal(posts, 1); assert.equal(gets, 6);
  unavailable = false;
  await env.settle(env.writer.create(body)); assert.equal(posts, 1); assert.equal(gets, 7);
});

test("revisioned replace and pause recover matching state, while a different later edit is not overwritten", async () => {
  for (const kind of ["replace", "enabled"]) {
    let puts = 0, gets = 0;
    const env = environment({ put: async (_path, _body, options) => {
      puts++; assert.equal(options.ifMatch, tag(1)); throw failure();
    }, get: async () => {
      gets++; return gets === 1 ? reply(1) : reply(2, { label: "Changed", enabled: false });
    } });
    const operation = kind === "replace"
      ? env.writer.replace(body.id, tag(1), { ...body, label: "Changed" })
      : env.writer.enabled(body.id, 1, false);
    assert.equal((await env.settle(operation)).data.revision, 2);
    assert.equal(puts, 1); assert.equal(gets, 2);
  }
  let puts = 0;
  const env = environment({ put: async () => { puts++; throw failure(); },
    get: async () => reply(3, { label: "Another edit" }) });
  await assert.rejects(env.settle(env.writer.replace(body.id, tag(1), body)),
    { code: "revision_conflict", status: 412 });
  assert.equal(puts, 1);
});

test("a no-op pause acknowledgement at the original revision remains successful", async () => {
  const env = environment({ put: async () => reply(1, { enabled: false }) });
  assert.equal((await env.settle(env.writer.enabled(body.id, 1, false))).data.enabled, false);
});

test("lost deletion confirmation verifies absence in the same native generation", async () => {
  let deletes = 0; const reads = [];
  const env = environment({ delete: async () => { deletes++; throw failure(); }, get: async path => {
    reads.push(path);
    if (path !== "/schedules") throw failure("schedule_not_found", 404);
    return { writeToken: "first-token", data: { items: [] } };
  } });
  assert.deepEqual((await env.settle(env.writer.remove(body.id, 1))).data,
    { id: body.id, removed: true });
  assert.equal(deletes, 1); assert.deepEqual(reads, [`/schedules/${body.id}`, "/schedules"]);
  const changed = environment({ delete: async () => { throw failure(); }, get: async path => {
    if (path !== "/schedules") throw failure("schedule_not_found", 404);
    return { writeToken: "new-token", data: { items: [] } };
  } });
  await assert.rejects(changed.settle(changed.writer.remove(body.id, 1)), { code: "write_token_conflict" });
});

test("declared persistence, capacity and permission refusals are immediate and do not inspect", async () => {
  for (const [code, status] of [["schedule_persistence_failed", 500], ["schedule_limit_reached", 429],
      ["schedule_service_unavailable", 503], ["permission_denied", 403]]) {
    let posts = 0;
    const env = environment({ post: async () => { posts++; throw failure(code, status); },
      get: async () => assert.fail("definite refusal does not become uncertain") });
    await assert.rejects(env.settle(env.writer.create(body)), { code, status });
    assert.equal(posts, 1); assert.deepEqual(env.waits, []);
  }
});

test("hanging acknowledgement and confirmations share one deadline and release their signals", async () => {
  const signals = [];
  const hang = (_path, _body, options) => {
    signals.push((options || _body).signal); return new Promise(() => {});
  };
  const env = environment({ post: hang, get: hang });
  await assert.rejects(env.settle(env.writer.create(body)), { code: "schedule_save_unconfirmed" });
  assert.equal(env.elapsed(), 55500); assert.equal(signals.length, 6);
  assert.ok(signals.every(signal => signal.aborted));
});

test("view cancellation releases a late write and later explicit retry only reads its result", async () => {
  let resolve, signal, posts = 0;
  const env = environment({ post: (_path, _body, options) => {
    posts++; signal = options.signal; return new Promise(done => { resolve = done; });
  }, get: async () => reply() });
  const controller = new AbortController();
  const work = env.writer.create(body, { signal: controller.signal });
  const cancelled = assert.rejects(work, { name: "AbortError" });
  await flush(); controller.abort(); await cancelled;
  assert.equal(signal.aborted, true); assert.equal(env.timers.size, 0);
  resolve(reply()); await flush();
  await env.settle(env.writer.create(body)); assert.equal(posts, 1);
});

test("changed target, malformed identity and wrong snapshot cannot acknowledge another schedule", async () => {
  const env = environment({ post: async () => { env.changeOwner(); throw failure(); } });
  await assert.rejects(env.settle(env.writer.create(body)), { name: "AbortError" });
  await assert.rejects(env.writer.replace(body.id, '"mdo-schedule-other-1"', body), TypeError);
  const wrong = environment({ post: async () => ({ ...reply(), data: { ...reply().data, id: "other" } }),
    get: async () => ({ ...reply(), data: { ...reply().data, id: "other" } }) });
  await assert.rejects(wrong.settle(wrong.writer.create(body)), { code: "schedule_save_unconfirmed" });
});

test("manual run accepts one valid acknowledgement and never replays an uncertain start", async () => {
  const result = { writeToken: "first-token", data: { schedule_id: body.id, task_id: 7, agent_run_id: 8 } };
  let calls = 0;
  const normal = environment({ post: async (path, sent, options) => {
    calls++; assert.equal(path, `/schedules/${body.id}/run`);
    assert.equal(sent, undefined); assert.equal(options.ifMatch, tag(1)); return result;
  } });
  assert.deepEqual(await normal.settle(normal.writer.run(body.id, 1)), result);
  assert.equal(calls, 1);
  for (const reason of [failure(), failure("revision_conflict", 412), failure("schedule_result_unavailable", 500)]) {
    let posts = 0;
    const env = environment({ post: async () => { posts++; throw reason; },
      get: async () => assert.fail("unrelated history cannot acknowledge this run") });
    await assert.rejects(env.settle(env.writer.run(body.id, 1)), { code: "schedule_run_unconfirmed" });
    assert.equal(posts, 1); assert.deepEqual(env.waits, []);
  }
  const malformed = environment({ post: async () => ({ ...result, data: { ...result.data, task_id: 0 } }) });
  await assert.rejects(malformed.settle(malformed.writer.run(body.id, 1)), { code: "schedule_run_unconfirmed" });
});

test("manual run has a finite ACK wait, cancels quietly and preserves definite refusals", async () => {
  let nativeSignal, posts = 0;
  const env = environment({ post: (_path, _body, options) => {
    posts++; nativeSignal = options.signal; return new Promise(() => {});
  } });
  await assert.rejects(env.settle(env.writer.run(body.id, 1)), { code: "schedule_run_unconfirmed" });
  assert.equal(env.elapsed(), 8000); assert.equal(posts, 1); assert.equal(nativeSignal.aborted, true);
  const controller = new AbortController();
  const work = env.writer.run(body.id, 1, { signal: controller.signal });
  const cancelled = assert.rejects(work, { name: "AbortError" });
  await flush(); controller.abort(); await cancelled;
  assert.equal(env.timers.size, 0); assert.equal(nativeSignal.aborted, true);
  for (const [code, status] of [["schedule_run_limit", 409], ["schedule_run_unavailable", 503],
      ["schedule_persistence_failed", 500], ["permission_denied", 403], ["write_token_conflict", 409]]) {
    let refused = 0;
    const definite = environment({ post: async () => { refused++; throw failure(code, status); } });
    await assert.rejects(definite.settle(definite.writer.run(body.id, 1)), { code, status });
    assert.equal(refused, 1); assert.deepEqual(definite.waits, []);
  }
});
