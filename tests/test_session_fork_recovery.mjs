import assert from "node:assert/strict";
import test from "node:test";
import { ApiError } from "../app/web/js/api/client.js";
import { createRequestRecovery } from "../app/web/js/api/request-recovery.js";
import { createSessionForker } from "../app/web/js/state/session-fork.js";

const source = { project_id: "default", id: "source", revision: 1 };
const input = { title: "Branch", through_sequence: 3 };
const network = () => new ApiError("Disconnected", { code: "network_error" });
const unavailable = () => new ApiError("Not applied", { code: "session_service_unavailable", status: 503 });
const missing = () => new ApiError("Missing", { code: "session_not_found", status: 404 });
function reply(id = "a".repeat(32), data = {}, metadata = {}) {
  return { data: { id, project_id: "default", parent_session_id: "source",
    title: "Branch", forked_through_sequence: 3, revision: 1, status: "active", ...data },
    etag: `"mdo-session-${id}-1"`, writeToken: "epoch-token", ...metadata };
}
const flush = () => new Promise(resolve => setImmediate(resolve));
function environment(options = {}) {
  let time = 0, serial = 0, generation = "first", identities = 0;
  const timers = new Map(), waits = [], page = new EventTarget();
  const env = { timers, waits, page,
    newId: () => (++identities).toString(16).padStart(32, "0"),
    epoch: () => generation, token: () => "epoch-token",
    createRecovery: () => createRequestRecovery({ now: () => time, random: () => 0, eventTarget: page,
      setTimer(fn, delay) { timers.set(++serial, { fn, at: time + delay, delay }); return serial; },
      clearTimer(id) { timers.delete(id); } }),
    elapsed: () => time, identities: () => identities,
    changeEpoch: () => { generation = "next"; },
    async settle(promise) {
      let done = false, value, error;
      promise.then(result => { value = result; done = true; }, reason => { error = reason; done = true; });
      for (let i = 0; i < 100 && !done; i++) {
        await flush(); if (done) break;
        assert.ok(timers.size, "every pending fork must own a deadline");
        const [id, timer] = [...timers].sort((a, b) => a[1].at - b[1].at)[0];
        timers.delete(id); time = timer.at; waits.push(timer.delay); timer.fn();
      }
      assert.equal(done, true); assert.equal(timers.size, 0);
      if (error) throw error;
      return value;
    },
  };
  env.fork = createSessionForker({ ...env, ...options });
  return env;
}

test("lost acknowledgement is recovered by reading the exact reserved child", async () => {
  const writes = [], reads = [];
  const env = environment({ send: async (path, body, options) => { writes.push({ path, body, options }); throw network(); },
    inspect: async path => { reads.push(path); return reply(writes[0].body.client_session_id); } });
  const result = await env.settle(env.fork(source, input));
  assert.equal(writes.length, 1); assert.equal(reads.length, 1);
  assert.equal(writes[0].options.ifMatch, '"mdo-session-source-1"');
  assert.equal(reads[0], `/projects/default/sessions/${result.id}`);
  assert.equal(result.parent_session_id, source.id); assert.deepEqual(env.waits, [500]);
});

test("explicit pre-application refusals quietly retry the same frozen body", async () => {
  const writes = [];
  const env = environment({ send: async (_path, body) => {
    writes.push(body); if (writes.length < 3) throw unavailable(); return reply(body.client_session_id);
  }, inspect: () => { throw new Error("known refusal does not need confirmation"); } });
  await env.settle(env.fork(source, input));
  assert.equal(writes.length, 3); assert.ok(writes.every(body => body === writes[0] && Object.isFrozen(body)));
  assert.deepEqual(env.waits, [500, 1000]);
});

test("a definitely missing child allows only the same keyed POST", async () => {
  const writes = [];
  const env = environment({ send: async (_path, body) => {
    writes.push(body); if (writes.length === 1) throw network(); return reply(body.client_session_id);
  }, inspect: async () => { throw missing(); } });
  await env.settle(env.fork(source, input));
  assert.equal(writes.length, 2); assert.equal(writes[0], writes[1]); assert.equal(env.identities(), 1);
});

test("six exhausted attempts keep the intent for a read-only manual retry", async () => {
  let writes = 0, reads = 0, id, healthy = false;
  const env = environment({ send: async (_path, body) => { writes++; id = body.client_session_id; throw network(); },
    inspect: async () => { reads++; if (!healthy) throw unavailable(); return reply(id); } });
  await assert.rejects(env.settle(env.fork(source, input)), { code: "session_fork_unconfirmed" });
  assert.equal(writes, 1); assert.equal(reads, 5); assert.equal(env.identities(), 1);
  healthy = true;
  assert.equal((await env.settle(env.fork(source, input))).id, id);
  assert.equal(writes, 1); assert.equal(reads, 6); assert.equal(env.identities(), 1);
});

test("a confirmed fork releases the intent for a later deliberate fork", async () => {
  const env = environment({ send: async (_path, body) => reply(body.client_session_id) });
  const one = await env.settle(env.fork(source, input)), two = await env.settle(env.fork(source, input));
  assert.notEqual(one.id, two.id); assert.equal(env.identities(), 2);
});

test("permanent rejection and known refusal exhaustion retain accurate categories", async () => {
  for (const [error, expected, calls] of [[new ApiError("Forbidden", { status: 403, code: "permission_denied" }),
      "permission_denied", 1], [unavailable(), "session_fork_unavailable", 6]]) {
    let writes = 0;
    const env = environment({ send: async () => { writes++; throw error; } });
    await assert.rejects(env.settle(env.fork(source, input)), { code: expected });
    assert.equal(writes, calls);
  }
});

test("invalid acknowledgement is confirmed without accepting a neighbouring branch", async () => {
  let id, reads = 0;
  const env = environment({ send: async (_path, body) => { id = body.client_session_id; return reply("b".repeat(32)); },
    inspect: async () => { reads++; return reply(id, { title: "Renamed while confirming" }); } });
  const result = await env.settle(env.fork(source, input));
  assert.equal(reads, 1); assert.equal(result.id, id); assert.equal(result.title, "Renamed while confirming");
});

test("wrong project, parent, boundary and ETag never confirm the result", async () => {
  for (const [data, metadata] of [[{ project_id: "other" }, {}], [{ parent_session_id: "other" }, {}],
      [{ forked_through_sequence: 4 }, {}], [{ revision: 0 }, {}], [{}, { etag: '"wrong"' }]]) {
    let writes = 0, id;
    const env = environment({ send: async (_path, body) => { writes++; id = body.client_session_id; throw network(); },
      inspect: async () => reply(id, data, metadata) });
    await assert.rejects(env.settle(env.fork(source, input)), { code: "session_fork_unconfirmed" });
    assert.equal(writes, 1);
  }
});

test("a restarted service or changed target cannot reuse an acknowledgement", async () => {
  let writes = 0;
  const env = environment({ send: async (_path, body) => {
    writes++; return reply(body.client_session_id, {}, { writeToken: "another-host" });
  } });
  await assert.rejects(env.settle(env.fork(source, input)), { code: "write_token_conflict" });
  assert.equal(writes, 1);
  const changed = environment({ send: async (_path, body) => { changed.changeEpoch(); return reply(body.client_session_id); } });
  await assert.rejects(changed.settle(changed.fork(source, input)), { name: "AbortError" });
});

test("abort-ignoring bodies share one minute and six total attempts", async () => {
  const signals = [];
  const never = async (_path, _body, options) => { signals.push(options.signal); return new Promise(() => {}); };
  const env = environment({ send: never,
    inspect: async (_path, options) => { signals.push(options.signal); return new Promise(() => {}); } });
  await assert.rejects(env.settle(env.fork(source, input)), { code: "session_fork_unconfirmed" });
  assert.equal(env.elapsed(), 60000); assert.equal(signals.length, 6); assert.ok(signals.every(s => s.aborted));
});

test("route/dialog cancellation and pagehide stop waiting without a late result", async () => {
  for (const usePage of [false, true]) {
    let resolve, signal, reads = 0;
    const controller = new AbortController();
    const env = environment({ send: (_path, body, options) => {
      signal = options.signal; return new Promise(done => { resolve = () => done(reply(body.client_session_id)); });
    }, inspect: async () => { reads++; throw unavailable(); } });
    const action = env.fork(source, input, { signal: controller.signal });
    await flush();
    if (usePage) env.page.dispatchEvent(new Event("pagehide")); else controller.abort();
    await assert.rejects(env.settle(action), { name: "AbortError" });
    resolve(); await flush(); assert.equal(reads, 0); assert.equal(signal.aborted, true);
  }
});

test("changing source revision after uncertain delivery still reads the original child", async () => {
  let id, writes = 0;
  const mutable = { ...source };
  const env = environment({ send: async (_path, body, options) => {
    writes++; id = body.client_session_id; mutable.revision = 2;
    assert.equal(options.ifMatch, '"mdo-session-source-1"'); throw network();
  },
    inspect: async () => reply(id) });
  await env.settle(env.fork(mutable, input));
  assert.equal(writes, 1); assert.equal(mutable.revision, 2);
});

test("invalid inputs never submit a write", async () => {
  let writes = 0;
  const env = environment({ send: async () => { writes++; throw network(); } });
  for (const [session, options] of [[{ ...source, etag: '"other"' }, input],
      [{ ...source, revision: 0 }, input], [source, { ...input, through_sequence: -1 }],
      [source, { ...input, through_sequence: 1.1 }]])
    await assert.rejects(env.fork(session, options), TypeError);
  assert.equal(writes, 0);
});
