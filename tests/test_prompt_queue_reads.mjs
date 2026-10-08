import assert from "node:assert/strict";
import test from "node:test";
import { createPromptQueue } from "../app/web/js/features/chat/prompt-queue.js";
import { createRequestRecovery } from "../app/web/js/api/request-recovery.js";

globalThis.document = { activeElement: null };
const flush = () => new Promise(resolve => setImmediate(resolve));
const id = "a".repeat(32);
const item = (text = "saved") => ({ id, text, state: "pending", attachments: [], priority: false });
const ok = data => Response.json({ ok: true, data });
const fail = (code = "queue_unavailable", status = 503) => Response.json({ ok: false,
  error: { code, message: code } }, { status });

function fixture() {
  let time = 0, next = 0;
  const timers = new Map(), events = new EventTarget();
  const createRecovery = () => createRequestRecovery({ now: () => time, random: () => 0,
    setTimer(fn, wait) { const key = ++next; timers.set(key, { fn, at: time + wait }); return key; },
    clearTimer(key) { timers.delete(key); }, eventTarget: events });
  // Exercise real queue state and HTTP code for a background session. The
  // physical rendering paths are covered by separate real-browser fixtures.
  const queue = createPromptQueue({ createRecovery,
    container: { dataset: {}, hidden: true, contains: () => false,
      replaceChildren() {}, querySelector: () => null },
    navigation: { get: () => ({}), subscribe() {} }, isRunActive: () => false,
  });
  async function settle(promise) {
    let done = false, result, error;
    promise.then(value => { result = value; done = true; }, failure => { error = failure; done = true; });
    for (let step = 0; step < 80 && !done; ++step) {
      await flush();
      if (done) break;
      assert(timers.size, "read must have a deadline even if fetch ignores abort");
      const [key, timer] = [...timers].sort((a, b) => a[1].at - b[1].at)[0];
      timers.delete(key); time = timer.at; timer.fn();
    }
    assert(done, "bounded queue read must settle");
    if (error) throw error;
    return result;
  }
  return { queue, settle, events, timers, elapsed: () => time };
}

test("coalesced queue loads recover two temporary failures without writes", async context => {
  const env = fixture(); let reads = 0;
  context.mock.method(globalThis, "fetch", async (_path, options) => {
    assert.equal(options.method, "GET"); reads += 1;
    return reads <= 2 ? fail() : ok({ items: [item()] });
  });
  await env.settle(Promise.all([env.queue.select("default", "one"), env.queue.select("default", "one")]));
  assert.equal(reads, 3); assert.equal(env.elapsed(), 1500);
  assert.equal(env.queue.find("default", "one", id).text, "saved");
  assert.equal(env.queue.hasUnsettled("default", "one"), true);
  assert.equal(env.timers.size, 0);
});

test("failed refresh retains the saved queue and releases its load gate", async context => {
  const env = fixture(); let broken = false, reads = 0;
  context.mock.method(globalThis, "fetch", async () => {
    reads += 1; return broken ? fail() : ok({ items: [item()] });
  });
  await env.settle(env.queue.select("default", "one")); broken = true;
  await assert.rejects(env.settle(env.queue.select("default", "one")), { code: "queue_unavailable" });
  assert.equal(reads, 7); assert.equal(env.elapsed(), 15500);
  assert.equal(env.queue.peek("default", "one").text, "saved");
  broken = false; await env.settle(env.queue.select("default", "one"));
  assert.equal(reads, 8); assert.equal(env.timers.size, 0);
});

test("hung transport cannot permanently occupy a session queue load", async context => {
  const env = fixture(); const signals = []; let hung = true;
  context.mock.method(globalThis, "fetch", async (_path, options) => {
    signals.push(options.signal);
    return hung ? new Promise(() => {}) : ok({ items: [] });
  });
  await assert.rejects(env.settle(env.queue.select("default", "hung")), { code: "network_error" });
  assert.equal(signals.length, 6); assert(signals.every(signal => signal?.aborted));
  assert(env.elapsed() <= 60000); assert.equal(env.queue.hasUnsettled("default", "hung"), false);
  hung = false; await env.settle(env.queue.select("default", "hung"));
  assert.equal(env.timers.size, 0);
});

test("page exit cancels a hung read and ignores its late queue", async context => {
  const env = fixture(); let release;
  context.mock.method(globalThis, "fetch", () => new Promise(resolve => { release = resolve; }));
  const loading = env.queue.select("default", "left"); await flush();
  env.events.dispatchEvent(new Event("pagehide"));
  await assert.rejects(env.settle(loading), { name: "AbortError" });
  release(ok({ items: [item("late")] })); await flush();
  assert.equal(env.queue.find("default", "left", id), null);
  assert.equal(env.queue.hasUnsettled("default", "left"), false);
});

test("old refresh cannot overwrite a queue mutation that finished first", async context => {
  const env = fixture(); let hold = false, release;
  context.mock.method(globalThis, "fetch", async (_path, options) => {
    if (options.method === "PUT") return ok({ items: [item()] });
    return hold ? new Promise(resolve => { release = resolve; }) : ok({ items: [{ ...item(), state: "staged" }] });
  });
  await env.settle(env.queue.select("default", "one")); hold = true;
  const old = env.queue.select("default", "one"); await flush();
  await env.settle(env.queue.promote("default", "one", id));
  release(ok({ items: [{ ...item(), state: "staged" }] })); await env.settle(old);
  assert.equal(env.queue.find("default", "one", id).state, "pending");
});

test("malformed queue data cannot erase or replace the saved queue", async context => {
  const env = fixture(); let response = { items: [item()] };
  context.mock.method(globalThis, "fetch", async () => ok(response));
  await env.settle(env.queue.select("default", "one"));
  for (const invalid of [null, {}, { items: {} }, { items: [item(), item()] },
    { items: [{ ...item(), state: "unknown" }] }]) {
    response = invalid;
    await assert.rejects(env.settle(env.queue.select("default", "one")), { code: "invalid_response" });
    assert.equal(env.queue.find("default", "one", id).text, "saved");
  }
});

test("exact receipt reads quietly recover and never submit another queue item", async context => {
  const env = fixture(); let reads = 0;
  context.mock.method(globalThis, "fetch", async (path, options) => {
    assert(path.endsWith(`/queue/${id}`)); assert.equal(options.method, "GET");
    return ++reads <= 2 ? fail() : ok({ id, state: "accepted", run_id: "run-one" });
  });
  const receipt = await env.settle(env.queue.receipt("default", "one", id));
  assert.equal(receipt.run_id, "run-one"); assert.equal(reads, 3);
});

test("only a real missing receipt is absence; access and malformed responses fail", async context => {
  const env = fixture(); let mode = "missing", reads = 0;
  context.mock.method(globalThis, "fetch", async () => {
    reads += 1;
    if (mode === "missing") return fail("queue_receipt_not_found", 404);
    if (mode === "route") return fail("queue_item_not_found", 404);
    if (mode === "access") return fail("permission_denied", 403);
    if (mode === "starting") return ok({ id, state: "starting" });
    return ok({ id: "b".repeat(32), state: "accepted", run_id: "run-other" });
  });
  assert.equal(await env.settle(env.queue.receipt("default", "one", id)), null);
  mode = "starting"; assert.equal(await env.settle(env.queue.receipt("default", "one", id)), null);
  mode = "route"; await assert.rejects(env.settle(env.queue.receipt("default", "one", id)), { code: "queue_item_not_found" });
  mode = "access"; await assert.rejects(env.settle(env.queue.receipt("default", "one", id)), { code: "permission_denied" });
  mode = "malformed"; await assert.rejects(env.settle(env.queue.receipt("default", "one", id)), { code: "invalid_response" });
  assert.equal(reads, 5);
});
