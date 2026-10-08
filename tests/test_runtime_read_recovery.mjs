import assert from "node:assert/strict";
import test, { after, beforeEach } from "node:test";

const original = { window: globalThis.window, setTimeout: globalThis.setTimeout,
  clearTimeout: globalThis.clearTimeout, now: Date.now, random: Math.random };
let time = 0, serial = 0; const timers = new Map();
globalThis.window = new EventTarget(); Date.now = () => time; Math.random = () => 0;
globalThis.setTimeout = (fn, delay) => { timers.set(++serial, { fn, delay }); return serial; };
globalThis.clearTimeout = id => timers.delete(id);
const { recoveryStore, selectRecovery, loadRecovery } = await import("../app/web/js/state/recovery.js");
const { needsRecoveryCard } = await import("../app/web/js/api/read-recovery.js");
const flush = () => new Promise(resolve => setImmediate(resolve));
const data = (id = "one", extra = {}) => ({ project_id: "qa", session_id: id,
  revision: 1, last_sequence: 0, total: 0, items: [], resume_required: false, ...extra });
const ok = body => Response.json({ ok: true, data: body });
const failure = (status = 503) => Response.json({ ok: false,
  error: { code: status === 403 ? "permission_denied" : "recovery_unavailable", message: "unavailable" } }, { status });
function tick(delay) {
  assert.equal(timers.size, 1);
  const [id, timer] = timers.entries().next().value;
  assert.equal(timer.delay, delay); timers.delete(id); time += delay; timer.fn();
}
beforeEach(() => { selectRecovery("", ""); recoveryStore.reset(); timers.clear(); time = 1; });
after(() => {
  selectRecovery("", ""); recoveryStore.reset();
  Date.now = original.now; Math.random = original.random;
  globalThis.window = original.window; globalThis.setTimeout = original.setTimeout;
  globalThis.clearTimeout = original.clearTimeout;
});

test("background inspections preserve known state and coalesce during quiet recovery", async context => {
  const saved = data("one", { resume_required: true }); recoveryStore.setData(saved);
  selectRecovery("qa", "one"); recoveryStore.setData(saved); let calls = 0;
  context.mock.method(globalThis, "fetch", async (_path, options) => {
    assert(options.signal instanceof AbortSignal); return ++calls === 1 ? failure() : ok(saved);
  });
  await loadRecovery(); assert.equal(recoveryStore.get().error, null);
  assert.equal(recoveryStore.get().data, saved); assert.equal(recoveryStore.isPending(), true);
  await loadRecovery(); assert.equal(calls, 1);
  tick(500); await flush(); assert.equal(calls, 2); assert.equal(recoveryStore.get().status, "ready");
});

test("exhaustion retains an actual interrupted response and only explicit retry bypasses cooldown", async context => {
  const saved = data("one", { resume_required: true }); selectRecovery("qa", "one"); recoveryStore.setData(saved);
  let calls = 0;
  context.mock.method(globalThis, "fetch", async () => { calls++; return failure(429); });
  await loadRecovery();
  for (const delay of [500, 1000, 2000, 4000, 8000]) { tick(delay); await flush(); }
  assert.equal(calls, 6); assert.equal(recoveryStore.get().data, saved);
  assert.equal(needsRecoveryCard(recoveryStore.get(), "qa", "one"), true);
  await loadRecovery(); assert.equal(calls, 6);
  context.mock.method(globalThis, "fetch", async () => { calls++; return ok(data()); });
  await loadRecovery({ retry: true }); assert.equal(calls, 7);
  assert.equal(recoveryStore.get().error, null); assert.equal(recoveryStore.get().data.resume_required, false);
});

test("switching inspection scope cancels an uncooperative transport and rejects its late data", async context => {
  let firstSignal, finishFirst; selectRecovery("qa", "one");
  context.mock.method(globalThis, "fetch", async path => ok(data(path.includes("/two/") ? "two" : "one")));
  context.mock.method(globalThis, "fetch", async (path, options) => {
    if (path.includes("/one/")) { firstSignal = options.signal; return new Promise(resolve => { finishFirst = resolve; }); }
    return ok(data("two"));
  });
  const first = loadRecovery(); await flush(); selectRecovery("qa", "two");
  assert.equal(firstSignal.aborted, true); await first; await loadRecovery();
  finishFirst(ok(data("one", { resume_required: true }))); await flush();
  assert.equal(recoveryStore.get().data.session_id, "two"); assert.equal(recoveryStore.get().data.resume_required, false);
  assert.equal(timers.size, 0);
});

test("an attempt deadline works even when fetch does not observe AbortSignal", async context => {
  selectRecovery("qa", "one"); let calls = 0;
  context.mock.method(globalThis, "fetch", async () => ++calls === 1 ? new Promise(() => {}) : ok(data()));
  const first = loadRecovery(); await flush(); tick(8000); await first;
  assert.equal(recoveryStore.get().error, null); assert.equal(recoveryStore.isPending(), true);
  tick(500); await flush(); assert.equal(calls, 2); assert.equal(recoveryStore.get().data.session_id, "one");
});

test("a busy runtime is unavailable for inspection and is not replayed as a transport failure", async context => {
  selectRecovery("qa", "one"); let calls = 0;
  context.mock.method(globalThis, "fetch", async () => {
    calls++; return Response.json({ ok: false, error: { code: "session_busy", message: "busy" } }, { status: 409 });
  });
  await loadRecovery(); assert.equal(calls, 1); assert.equal(timers.size, 0);
  assert.equal(recoveryStore.get().data.unavailable, true); assert.equal(recoveryStore.get().status, "ready");
});

test("permanent and malformed inspections discard stale decisions without automatic retry", async context => {
  for (const response of [failure(403), ok(data("other"))]) {
    selectRecovery("qa", "one"); recoveryStore.setData(data("one", { resume_required: true }));
    let calls = 0;
    context.mock.method(globalThis, "fetch", async () => { calls++; return response; });
    await loadRecovery({ retry: true });
    assert.equal(calls, 1); assert.equal(recoveryStore.get().status, "error");
    assert.equal(recoveryStore.get().data.resume_required, false); assert.equal(timers.size, 0);
  }
});
