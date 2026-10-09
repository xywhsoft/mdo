import assert from "node:assert/strict";
import test from "node:test";
import { ApiError } from "../app/web/js/api/client.js";
import { createRequestRecovery } from "../app/web/js/api/request-recovery.js";
import { createProjectPurgeRecovery } from "../app/web/js/features/settings/project-purge-recovery.js";
import { EMPTY_PURGE_ETAG } from "../app/web/js/features/settings/project-purge-contract.js";

const empty = { data: { intent: null }, etag: EMPTY_PURGE_ETAG };
const offline = () => new ApiError("offline", { code: "network_error" });
const turn = () => new Promise(resolve => setImmediate(resolve));

function fixture(context, respond, background = false) {
  let time = 0, serial = 0, owners = 0;
  const timers = new Map(), events = new EventTarget(), calls = [], errors = new Set();
  const setTimer = (fn, delay) => { timers.set(++serial, { fn, at: time + delay }); return serial; };
  const clearTimer = id => timers.delete(id);
  // Baseline request() owns global timers rather than the injected recovery.
  context.mock.method(globalThis, "setTimeout", setTimer);
  context.mock.method(globalThis, "clearTimeout", clearTimer);
  const transport = { get(path, options) {
    calls.push({ path, signal: options.signal, at: time }); return respond(path, options);
  } };
  const recovery = createProjectPurgeRecovery({ transport, eventTarget: background ? events : null,
    createRecovery: limits => { owners++; return createRequestRecovery({ ...limits,
      now: () => time, random: () => 0, setTimer, clearTimer, eventTarget: events }); } });
  recovery.subscribe(state => { if (state.error) errors.add(state.error); });
  return { recovery, transport, events, calls, errors, timers, owners: () => owners, elapsed: () => time,
    async retry() {
      assert.equal(timers.size, 1);
      const [id, timer] = [...timers][0]; assert.equal(timer.at - time, 30000);
      timers.delete(id); time = timer.at; timer.fn(); await turn();
    },
    async finish(promise) {
      let done = false, value, failure;
      promise.then(result => { done = true; value = result; }, error => { done = true; failure = error; });
      for (let n = 0; !done && n < 100; n++) {
        await turn(); if (done) break;
        const [id, timer] = [...timers].sort((a, b) => a[1].at - b[1].at)[0] ?? [];
        assert(timer, "service continuity check stayed pending without a deadline");
        timers.delete(id); time = timer.at; timer.fn();
      }
      assert(done, "service continuity check never settled");
      assert(timers.size === 0 || (background && timers.size === 1 &&
        [...timers.values()][0].at - time === 30000));
      if (failure) throw failure; return value;
    } };
}

test("service continuity checks recover quietly from two transient read failures", async context => {
  let reads = 0;
  const f = fixture(context, async () => { if (++reads < 3) throw offline(); return empty; });
  await f.finish(f.recovery.refresh());
  assert.equal(f.recovery.isPaused(), false);
  assert.equal(f.errors.size, 0);
  assert.equal(f.calls.length, 3);
  assert.equal(f.elapsed(), 1500);
  assert.equal(f.owners(), 1);
});

test("ignored aborts end service checks within one minute without inventing a purge", async context => {
  const f = fixture(context, () => new Promise(() => {}));
  assert.equal(await f.finish(f.recovery.refresh()), false);
  assert.equal(f.elapsed(), 60000);
  assert.equal(f.calls.length, 6);
  assert(f.calls.every(call => call.signal.aborted));
  assert.equal(f.errors.size, 1);
  assert.equal(f.recovery.get().busy, false);
  assert.equal(f.recovery.get().error.code, "network_error");
  assert.equal(f.recovery.requiresReview(), false);
  assert.equal(f.recovery.isPaused(), true);
  assert.equal(f.recovery.allowsWrite({ method: "POST", path: "/sessions" }), false);
});

test("a reachable connection resumes a failed check once without replaying a mutation", async context => {
  const f = fixture(context, async () => { throw offline(); });
  await f.finish(f.recovery.refresh());
  assert.equal(f.errors.size, 1);
  f.transport.get = async () => { f.calls.push({ path: "/project-purge-intent" }); return empty; };
  const first = f.recovery.resumeReads(), second = f.recovery.resumeReads();
  assert.equal(first, second);
  assert.equal(f.recovery.isPaused(), true);
  await f.finish(first);
  assert.equal(f.recovery.isPaused(), false);
  assert.equal(f.recovery.requiresReview(), false);
  assert.equal(f.recovery.allowsWrite({ method: "POST", path: "/sessions" }), true);
  const count = f.calls.length;
  await f.finish(f.recovery.resumeReads());
  assert.equal(f.calls.length, count);
});

test("permanent denial is final and connection events do not retry it", async context => {
  const f = fixture(context, async () => { throw new ApiError("denied", { status: 403, code: "access_denied" }); });
  await f.finish(f.recovery.refresh());
  await f.finish(f.recovery.resumeReads());
  assert.equal(f.calls.length, 1);
  assert.equal(f.errors.size, 1);
  assert.equal(f.recovery.requiresReview(), false);
  assert.equal(f.recovery.isPaused(), true);
});

test("a verified service restart keeps review and never automatically releases stale writes", async context => {
  const f = fixture(context, async () => empty);
  await f.finish(f.recovery.refresh());
  f.recovery.markWriteConflict(new ApiError("restart", { code: "service_restarted" }));
  assert.equal(f.recovery.requiresReview(), true);
  await f.finish(f.recovery.resumeReads());
  assert.equal(f.calls.length, 1);
  assert.equal(f.recovery.isPaused(), true);
  assert.equal(f.recovery.get().writeConflictReason, "restart");
});

test("page departure cancels a pending read and a late empty result cannot unlock it", async context => {
  let finish;
  const f = fixture(context, () => new Promise(resolve => { finish = resolve; }));
  const pending = f.recovery.refresh(); await turn();
  f.events.dispatchEvent(new Event("pagehide"));
  assert.equal(await f.finish(pending), false);
  assert.equal(f.elapsed(), 0);
  assert.equal(f.calls.length, 1);
  assert.equal(f.calls[0].signal.aborted, true);
  finish(empty); await turn();
  assert.equal(f.recovery.isPaused(), true);
  assert.equal(f.recovery.get().checked, false);
});

test("a cold exhausted check recovers in the background without another click", async context => {
  const f = fixture(context, async () => { throw offline(); }, true);
  await f.finish(f.recovery.refresh());
  assert.equal(f.calls.length, 6); assert.equal(f.errors.size, 1);
  f.transport.get = async () => { f.calls.push({ path: "/project-purge-intent" }); return empty; };
  await f.retry();
  assert.equal(f.recovery.isPaused(), false);
  assert.equal(f.calls.length, 7);
  assert.equal(f.timers.size, 0);
});

test("hiding the page cancels its cooldown and showing it resumes only the read", async context => {
  const f = fixture(context, async () => { throw offline(); }, true);
  await f.finish(f.recovery.refresh());
  f.events.dispatchEvent(new Event("pagehide"));
  assert.equal(f.timers.size, 0);
  assert.equal(await f.finish(f.recovery.resumeReads()), false);
  assert.equal(f.calls.length, 6);
  f.transport.get = async () => { f.calls.push({ path: "/project-purge-intent" }); return empty; };
  f.events.dispatchEvent(new Event("pageshow")); await turn();
  assert.equal(f.recovery.isPaused(), false);
  assert.equal(f.calls.length, 7);
});

for (const immediately of [false, true])
  test(`returning to the page resumes a cancelled read${immediately ? " before cancellation settles" : ""}`, async context => {
    let calls = 0;
    const f = fixture(context, () => ++calls === 1 ? new Promise(() => {}) : Promise.resolve(empty), true);
    const pending = f.recovery.refresh();
    await turn();
    f.events.dispatchEvent(new Event("pagehide"));
    if (immediately) f.events.dispatchEvent(new Event("pageshow"));
    await pending;
    await turn();
    if (!immediately) f.events.dispatchEvent(new Event("pageshow"));
    await turn();
    assert.equal(f.calls.length, 2);
    assert.equal(f.owners(), 2);
    assert.equal(f.recovery.isPaused(), false);
    assert.equal(f.recovery.requiresReview(), false);
    assert.equal(f.timers.size, 0);
  });

test("a hung cancellation stays single attempt and retains its original request", async context => {
  const intent = { purge_request_id: "a".repeat(32), project_id: "demo",
    revision: 3, created_at: 123, name: "Demo" };
  const f = fixture(context, async path => {
    if (path === "/project-purge-intent") return { data: { intent },
      etag: `"mdo-purge-intent-${intent.purge_request_id}"` };
    throw new ApiError("missing", { status: 404, code: "purge_request_not_found" });
  });
  await f.finish(f.recovery.refresh());
  assert.equal(f.recovery.requiresReview(), true);
  const posts = [];
  f.transport.post = (path, body, options) => {
    posts.push({ path, body, signal: options.signal }); return new Promise(() => {});
  };
  assert.equal(await f.finish(f.recovery.cancel()), false);
  assert.equal(f.elapsed(), 8000);
  assert.equal(posts.length, 1); assert(posts[0].signal.aborted);
  assert.equal(posts[0].body.purge_request_id, intent.purge_request_id);
  assert.deepEqual(f.recovery.get().intent, intent);
  assert.equal(f.recovery.isPaused(), true);
});
