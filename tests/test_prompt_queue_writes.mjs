import assert from "node:assert/strict";
import test from "node:test";
import { createPromptQueue } from "../app/web/js/features/chat/prompt-queue.js";
import { createRequestRecovery } from "../app/web/js/api/request-recovery.js";
import { api } from "../app/web/js/api/client.js";

globalThis.document = { activeElement: null };
const flush = () => new Promise(resolve => setImmediate(resolve));
const id = "a".repeat(32);
const submission = { id, text: "saved input", attachments: [], interrupt: false,
  profile: { model_id: "fixture", reasoning_effort: "none", permission_profile: "balanced" } };
const item = state => ({ id, text: submission.text, attachments: [], priority: false,
  state, profile: submission.profile });
const ok = (items, headers) => Response.json({ ok: true, data: { items } }, { headers });
const receipt = (state, headers, receiptId = id) => Response.json({ ok: true,
  data: { id: receiptId, state, ...(state === "accepted" ? { run_id: "run-one" } : {}) } }, { headers });
const fail = (code, status = 503) => Response.json({ ok: false, error: { code, message: code } }, { status });

function fixture() {
  let time = 0, sequence = 0, epoch = "one";
  const timers = new Map(), events = new EventTarget();
  const queue = createPromptQueue({ stamp: () => epoch,
    createRecovery: () => createRequestRecovery({ now: () => time, random: () => 0, eventTarget: events,
      setTimer(fn, delay) { const key = ++sequence; timers.set(key, { fn, at: time + delay }); return key; },
      clearTimer(key) { timers.delete(key); } }),
    container: { dataset: {}, hidden: true, contains: () => false, replaceChildren() {}, querySelector: () => null },
    navigation: { get: () => ({}), subscribe() {} }, isRunActive: () => false,
  });
  async function settle(promise) {
    let done = false, value, failure;
    promise.then(result => { value = result; done = true; }, error => { failure = error; done = true; });
    for (let step = 0; step < 80 && !done; ++step) {
      await flush(); if (done) break;
      assert(timers.size, "queue write must settle even if its transport ignores abort");
      const [key, timer] = [...timers].sort((a, b) => a[1].at - b[1].at)[0];
      time = timer.at; timers.delete(key); timer.fn();
    }
    assert(done, "queue write must remain bounded");
    if (failure) throw failure;
    return value;
  }
  return { queue, settle, events, timers, elapsed: () => time, changeTarget() { epoch = "two"; } };
}

test("hung queue POST confirms its saved exact item after eight seconds", async context => {
  const env = fixture(); let posts = 0, rows = [], signal;
  context.mock.method(globalThis, "fetch", async (_path, options) => {
    if (options.method === "POST") { posts++; rows = [item("staged")]; signal = options.signal; return new Promise(() => {}); }
    return ok(rows);
  });
  assert.equal(await env.settle(env.queue.stage("default", "slow-post", submission)), id);
  assert.equal(env.elapsed(), 8000); assert.equal(posts, 1); assert(signal.aborted);
  assert.equal(env.queue.find("default", "slow-post", id).state, "staged");
});

test("lost queue POST waits out transient confirmation faults without replay", async context => {
  const env = fixture(); let posts = 0, confirmations = 0;
  context.mock.method(globalThis, "fetch", async (_path, options) => {
    if (options.method === "POST") { posts++; throw new TypeError("Lost response"); }
    if (!posts) return ok([]);
    return ++confirmations <= 2 ? fail("queue_unavailable") : ok([item("staged")]);
  });
  assert.equal(await env.settle(env.queue.stage("default", "lost-post", submission)), id);
  assert.equal(posts, 1); assert.equal(confirmations, 3); assert.equal(env.elapsed(), 1500);
});

test("absent queue data does not prove an unknown POST can be repeated", async context => {
  const env = fixture(); let posts = 0, confirmations = 0;
  context.mock.method(globalThis, "fetch", async (path, options) => {
    if (options.method === "POST") { posts++; throw new TypeError("Lost response"); }
    if (path.endsWith(`/queue/${id}`)) return fail("queue_receipt_not_found", 404);
    if (posts) confirmations++; return ok([]);
  });
  await assert.rejects(env.settle(env.queue.stage("default", "unknown-post", submission)), error =>
    error.code === "network_error" && error.queueAdmissionUncertain && error.queueItemId === id);
  assert.equal(posts, 1); assert.equal(confirmations, 6); assert.equal(env.elapsed(), 15500);
});

test("an exact consumed receipt confirms a queue POST; another ID cannot", async context => {
  for (const matches of [true, false]) {
    const env = fixture(); let posts = 0;
    context.mock.method(globalThis, "fetch", async (path, options) => {
      if (options.method === "POST") { posts++; throw new TypeError("Lost response"); }
      return path.endsWith(`/queue/${id}`) ? receipt("accepted", undefined, matches ? id : "b".repeat(32)) : ok([]);
    });
    const result = env.settle(env.queue.stage("default", "consumed-post", submission));
    if (matches) assert.equal(await result, id);
    else await assert.rejects(result, error => error.queueAdmissionUncertain && error.confirmationError.code === "invalid_response");
    assert.equal(posts, 1);
  }
});

test("confirmation cannot adopt the same ID with different text or model", async context => {
  for (const wrong of [{ ...item("staged"), text: "other text" },
    { ...item("staged"), profile: { ...submission.profile, model_id: "other" } }]) {
    const env = fixture(); let posts = 0;
    context.mock.method(globalThis, "fetch", async (_path, options) => {
      if (options.method === "POST") { posts++; throw new TypeError("Lost response"); }
      return ok(posts ? [wrong] : []);
    });
    await assert.rejects(env.settle(env.queue.stage("default", "mismatch-post", submission)), error =>
      error.queueAdmissionUncertain && error.confirmationError.code === "queue_id_conflict");
    assert.equal(posts, 1); assert.equal(env.queue.find("default", "mismatch-post", id), null);
  }
});

test("hung promotion and sending PUTs confirm the actual state, never repeat PUT", async context => {
  for (const [method, before, after] of [["promote", "staged", "pending"], ["markSending", "pending", "sending"]]) {
    const env = fixture(); let rows = [item(before)], writes = 0;
    context.mock.method(globalThis, "fetch", async (_path, options) => {
      if (options.method === "PUT") { writes++; rows = [item(after)]; return new Promise(() => {}); }
      return ok(rows);
    });
    await env.settle(env.queue.select("default", "slow-state"));
    await env.settle(env.queue[method]("default", "slow-state", id));
    assert.equal(env.elapsed(), 8000); assert.equal(writes, 1);
    assert.equal(env.queue.find("default", "slow-state", id).state, after);
  }
});

test("retry cannot mistake an accepted run for a successful reset to pending", async context => {
  const env = fixture(); let writes = 0, reads = 0;
  context.mock.method(globalThis, "fetch", async (path, options) => {
    if (options.method === "PUT") { writes++; throw new TypeError("Lost response"); }
    assert(!path.endsWith(`/queue/${id}`), "a run receipt is not proof of a reset");
    if (writes) reads++; return ok([item("sending")]);
  });
  await env.settle(env.queue.select("default", "reset"));
  await assert.rejects(env.settle(env.queue.retry("default", "reset", id)), error => error.queueAdmissionUncertain);
  assert.equal(writes, 1); assert.equal(reads, 6);
});

test("lost deletion quietly confirms absence after two temporary read failures", async context => {
  const env = fixture(); let deletes = 0, reads = 0;
  context.mock.method(globalThis, "fetch", async (_path, options) => {
    if (options.method === "DELETE") { deletes++; throw new TypeError("Lost response"); }
    if (!deletes) return ok([item("pending")]);
    return ++reads <= 2 ? fail("queue_unavailable") : ok([]);
  });
  await env.settle(env.queue.select("default", "delete"));
  await env.settle(env.queue.remove("default", "delete", id));
  assert.equal(deletes, 1); assert.equal(reads, 3);
  assert.equal(env.queue.find("default", "delete", id), null);
});

test("definite queue and account refusals retain their exact error without confirmation", async context => {
  for (const [code, status] of [["queue_full", 422], ["permission_denied", 403], ["quota_exceeded", 429]]) {
    const env = fixture(); let writes = 0, reads = 0;
    context.mock.method(globalThis, "fetch", async (_path, options) => {
      if (options.method === "POST") { writes++; return fail(code, status); }
      reads++; return ok([]);
    });
    await assert.rejects(env.settle(env.queue.stage("default", "refused", submission)), error =>
      error.code === code && error.status === status && !error.queueAdmissionUncertain);
    assert.equal(writes, 1); assert.equal(reads, 1);
  }
});

test("changing target while loading the queue cannot submit the old input to the new target", async context => {
  const env = fixture(); let finish, writes = 0;
  context.mock.method(globalThis, "fetch", async (_path, options) => {
    if (options.method === "POST") { writes++; return ok([item("staged")]); }
    return new Promise(resolve => { finish = resolve; });
  });
  const result = env.queue.stage("default", "moved", submission); await flush();
  env.changeTarget(); finish(ok([]));
  await assert.rejects(env.settle(result), error => error.name === "AbortError" && !error.queueAdmissionUncertain);
  assert.equal(writes, 0);
});

test("page exit cancels the wait, preserves write uncertainty, and ignores a late acknowledgement", async context => {
  const env = fixture(); let finish, writes = 0;
  context.mock.method(globalThis, "fetch", async (_path, options) => {
    if (options.method === "POST") { writes++; return new Promise(resolve => { finish = resolve; }); }
    return ok([]);
  });
  const result = env.queue.stage("default", "left", submission); await flush();
  env.events.dispatchEvent(new Event("pagehide"));
  await assert.rejects(env.settle(result), error => error.name === "AbortError" && error.queueAdmissionUncertain);
  finish(ok([item("staged")])); await flush();
  assert.equal(writes, 1); assert.equal(env.queue.find("default", "left", id), null);
});

test("a receipt from a restarted service cannot confirm the old write", async context => {
  const env = fixture(); let posts = 0;
  const token = "c".repeat(32) + "-1", next = "c".repeat(32) + "-2";
  context.mock.method(globalThis, "fetch", async (path, options) => {
    if (path.endsWith("/project-purge-intent")) return Response.json({ ok: true, data: {} }, { headers: { "X-Mdo-Write-Token": token } });
    if (options.method === "POST") { posts++; throw new TypeError("Lost response"); }
    return path.endsWith(`/queue/${id}`) ? receipt("accepted", { "X-Mdo-Write-Token": next }) :
      ok([], { "X-Mdo-Write-Token": token });
  });
  await api.get("/project-purge-intent");
  await assert.rejects(env.settle(env.queue.stage("default", "restarted", submission)), error =>
    error.queueAdmissionUncertain && error.confirmationError.code === "write_token_conflict");
  assert.equal(posts, 1);
});
