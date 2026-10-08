import assert from "node:assert/strict";
import test, { after, beforeEach } from "node:test";

const previous = { window: globalThis.window, setTimeout: globalThis.setTimeout,
  clearTimeout: globalThis.clearTimeout, now: Date.now, random: Math.random };
let clock = 0, timerId = 0;
const timers = new Map();
globalThis.window = new EventTarget();
globalThis.setTimeout = (callback, delay) => {
  const id = ++timerId; timers.set(id, { callback, delay }); return id;
};
globalThis.clearTimeout = id => timers.delete(id);
Date.now = () => clock; Math.random = () => 0;
const { sessionsStore, sessionDetailStore, loadSessions, loadSession, createSession,
  updateSessionProfile, patchSession, forkSession, trashSession, restoreSession,
  clearSession } = await import("../app/web/js/state/sessions.js");
const { waitForSelectedDetail } = await import("../app/web/js/features/shell/session-detail-wait.js");
const ok = data => Response.json({ ok: true, data });
const failure = (status = 503) => Response.json({ ok: false,
  error: { code: status === 403 ? "permission_denied" : "session_service_unavailable", message: "unavailable" } }, { status });
const session = (id = "one", revision = 1) => ({ id, project_id: "qa", revision,
  status: "active", model_id: "model", title: id, permission_profile: "balanced" });
const flush = () => new Promise(resolve => setImmediate(resolve));
function tick(delay) {
  assert.equal(timers.size, 1); const [id, item] = timers.entries().next().value;
  assert.equal(item.delay, delay); timers.delete(id); clock += delay; item.callback();
}
beforeEach(() => { sessionsStore.reset(); sessionDetailStore.reset(); timers.clear(); clock = 1; });
after(() => {
  sessionsStore.reset(); sessionDetailStore.reset(); Date.now = previous.now; Math.random = previous.random;
  const { now, random, ...globals } = previous; Object.assign(globalThis, globals);
});

test("a failed session-list poll stays quiet and preserves the visible sessions", async context => {
  const saved = { generation: 1, items: [session()] }; sessionsStore.setData(saved);
  let calls = 0;
  context.mock.method(globalThis, "fetch", async () => ++calls === 1 ? failure() : ok(saved));
  await loadSessions();
  assert.equal(sessionsStore.get().error, null); assert.equal(sessionsStore.get().data, saved);
  await loadSessions(); assert.equal(calls, 1);
  tick(500); await flush(); assert.equal(calls, 2); assert.equal(sessionsStore.get().status, "ready");
});

test("an accepted profile or session write finishes before a stalled sidebar read", async context => {
  const operations = [() => updateSessionProfile(session(), { model_id: "next", reasoning_effort: "none", permission_profile: "balanced" }),
    () => patchSession(session(), { title: "renamed" }),
    () => createSession({ project_id: "qa", title: "created" }),
    () => forkSession(session(), { through_sequence: 1 }),
    () => trashSession(session()), () => restoreSession(session()), () => clearSession(session())];
  for (const operation of operations) {
    let writes = 0, finishRead;
    context.mock.method(globalThis, "fetch", async (_path, options) => {
      if (options.method !== "GET") { writes++; return ok(session("one", 2)); }
      return new Promise(resolve => { finishRead = resolve; });
    });
    const pending = operation(); let settled = false; void pending.then(() => { settled = true; });
    await flush(); assert.equal(settled, true, "a confirmed write cannot wait for sidebar readback");
    assert.equal((await pending).revision, 2); assert.equal(writes, 1);
    finishRead(ok({ generation: 2, items: [] })); await flush(); sessionsStore.reset();
  }
});

test("session detail recovery never leaks a previous session into the new selection", async context => {
  sessionDetailStore.setData(session()); let calls = 0;
  context.mock.method(globalThis, "fetch", async (url, options) => {
    assert.equal(url, "/api/v1/projects/qa/sessions/two"); assert(options.signal instanceof AbortSignal);
    return ++calls === 1 ? failure() : ok(session("two"));
  });
  await loadSession("qa", "two");
  assert.equal(sessionDetailStore.get().error, null); assert.equal(sessionDetailStore.get().data, null);
  tick(500); await flush(); assert.equal(sessionDetailStore.get().data.id, "two");
});

test("a warm detail retries quietly but a permanent refusal discards its controls", async context => {
  sessionDetailStore.setData(session()); let calls = 0;
  context.mock.method(globalThis, "fetch", async () => ++calls === 1 ? failure() : failure(403));
  await loadSession("qa", "one"); assert.equal(sessionDetailStore.get().error, null);
  assert.equal(sessionDetailStore.get().data.id, "one"); tick(500); await flush();
  assert.equal(sessionDetailStore.get().status, "error"); assert.equal(sessionDetailStore.get().data, null);
  assert.equal(timers.size, 0);
});

test("a mismatched detail response cannot publish another task", async context => {
  context.mock.method(globalThis, "fetch", async () => ok(session("other")));
  await loadSession("qa", "one"); assert.equal(sessionDetailStore.get().status, "error");
  assert.equal(sessionDetailStore.get().data, null); assert.equal(timers.size, 0);
});

test("list exhaustion is finite and only an explicit retry bypasses its probe cooldown", async context => {
  const saved = { generation: 1, items: [session()] }; sessionsStore.setData(saved);
  let calls = 0;
  context.mock.method(globalThis, "fetch", async () => { calls++; return failure(429); });
  await loadSessions();
  for (const delay of [500, 1000, 2000, 4000, 8000]) {
    assert.equal(sessionsStore.get().error, null); tick(delay); await flush();
  }
  assert.equal(calls, 6); assert.equal(timers.size, 0);
  assert.equal(sessionsStore.get().status, "error"); assert.equal(sessionsStore.get().data, saved);
  await loadSessions(); assert.equal(calls, 6);
  context.mock.method(globalThis, "fetch", async () => { calls++; return ok(saved); });
  await loadSessions({ retry: true }); assert.equal(calls, 7);
  assert.equal(sessionsStore.get().status, "ready"); assert.equal(timers.size, 0);
});

test("an acknowledged write supersedes an older sidebar snapshot", async context => {
  let oldSignal, finishOld; const saved = { generation: 2, items: [session("one", 2)] };
  context.mock.method(globalThis, "fetch", async (_url, options) => {
    if (options.method !== "GET") return ok(session("one", 2));
    if (!oldSignal) {
      oldSignal = options.signal; return new Promise(resolve => { finishOld = resolve; });
    }
    return ok(saved);
  });
  const oldRead = loadSessions(); await flush();
  await patchSession(session(), { title: "renamed" }); await flush();
  assert.equal(oldSignal.aborted, true); assert.equal(sessionsStore.get().data.generation, 2);
  finishOld(ok({ generation: 1, items: [session()] })); await oldRead;
  assert.equal(sessionsStore.get().data.generation, 2);
});

test("returning to a warm task keeps its queue gate closed until recovery finishes", async context => {
  sessionDetailStore.setData(session()); let calls = 0;
  context.mock.method(globalThis, "fetch", async () => ++calls === 1 ? failure() : ok(session("one", 2)));
  const firstAttempt = loadSession("qa", "one");
  const waiting = waitForSelectedDetail({ store: sessionDetailStore, isSelected: () => true,
    navigation: { subscribe: () => () => {} } });
  let finished = false; void waiting.then(() => { finished = true; });
  await firstAttempt; await flush(); assert.equal(finished, false);
  tick(500); await flush(); assert.equal((await waiting).data.revision, 2);
});

test("permanent list refusal and malformed envelopes discard unavailable rows without retry", async context => {
  for (const response of [failure(403), ok({ generation: 2 })]) {
    sessionsStore.setData({ generation: 1, items: [session()] });
    context.mock.method(globalThis, "fetch", async () => response);
    await loadSessions(); assert.equal(sessionsStore.get().status, "error");
    assert.deepEqual(sessionsStore.get().data.items, []); assert.equal(timers.size, 0);
  }
});
