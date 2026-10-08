import assert from "node:assert/strict";
import test from "node:test";

import { createSessionMetadataSync } from
  "../app/web/js/features/shell/session-metadata-sync.js";
import { createResourceStore } from "../app/web/js/state/store.js";
import { createRequestRecovery } from "../app/web/js/api/request-recovery.js";

function session(id, revision, status = "active") {
  return { project_id: "default", id, revision, status,
    title: id, etag: `"mdo-session-${id}-${revision}"` };
}

function deferred() {
  let resolve;
  const promise = new Promise((done) => { resolve = done; });
  return { promise, resolve };
}

test("remote archive updates the selected session only once per revision", async () => {
  const route = { view: "workspace", projectId: "default", sessionId: "one" };
  const store = createResourceStore();
  store.setData(session("one", 1));
  let sidebarLoads = 0;
  let restores = 0;
  const sync = createSessionMetadataSync({ navigation: { get: () => route }, store,
    readSession: async () => session("one", 2, "archived"),
    loadSessions: async () => { sidebarLoads += 1; },
    onRestored: async () => { restores += 1; } });
  assert.equal(await sync.refresh(), true);
  assert.equal(store.get().data.status, "archived");
  assert.equal(await sync.refresh(), false);
  assert.equal(sidebarLoads, 1);
  assert.equal(restores, 0);
});

test("old responses cannot replace a newer local revision or another session", async () => {
  const route = { view: "workspace", projectId: "default", sessionId: "one" };
  const store = createResourceStore();
  store.setData(session("one", 1));
  const pending = deferred();
  const sync = createSessionMetadataSync({ navigation: { get: () => route }, store,
    readSession: () => pending.promise,
    loadSessions: async () => { throw new Error("unexpected sidebar load"); },
    onRestored: async () => { throw new Error("unexpected dispatch"); } });
  const first = sync.refresh();
  store.setData(session("one", 3));
  pending.resolve(session("one", 2, "archived"));
  assert.equal(await first, false);
  assert.equal(store.get().data.status, "active");
  const other = deferred();
  const secondSync = createSessionMetadataSync({ navigation: { get: () => route }, store,
    readSession: () => other.promise,
    loadSessions: async () => { throw new Error("unexpected sidebar load"); },
    onRestored: async () => { throw new Error("unexpected dispatch"); } });
  const second = secondSync.refresh();
  route.sessionId = "two";
  store.setData(session("two", 1));
  other.resolve(session("one", 4, "archived"));
  assert.equal(await second, false);
  assert.equal(store.get().data.id, "two");
});

test("remote restore rechecks the pending queue once", async () => {
  const route = { view: "workspace", projectId: "default", sessionId: "one" };
  const store = createResourceStore();
  store.setData(session("one", 1, "archived"));
  const pending = deferred();
  let reads = 0;
  let restores = 0;
  const sync = createSessionMetadataSync({ navigation: { get: () => route }, store,
    readSession: () => { reads += 1; return pending.promise; },
    loadSessions: async () => {},
    onRestored: async () => { restores += 1; } });
  const first = sync.refresh();
  const second = sync.refresh();
  pending.resolve(session("one", 2));
  assert.deepEqual(await Promise.all([first, second]), [true, true]);
  assert.equal(reads, 1);
  assert.equal(restores, 1);
});

test("background metadata does not compete with the selected task's pending read", async () => {
  const route = { view: "workspace", projectId: "default", sessionId: "one" };
  const store = createResourceStore(); store.setData(session("one", 1));
  const detail = deferred(); const pending = store.load(() => detail.promise);
  const sync = createSessionMetadataSync({ navigation: { get: () => route }, store,
    readSession: async () => { throw new Error("duplicate metadata request"); },
    loadSessions: async () => {}, onRestored: async () => {} });
  assert.equal(await sync.refresh(), false);
  detail.resolve(session("one", 2)); await pending;
});

test("an observed restore can release the queue before a stalled sidebar read", async () => {
  const route = { view: "workspace", projectId: "default", sessionId: "one" };
  const store = createResourceStore(); store.setData(session("one", 1, "archived"));
  const sidebar = deferred(); let restores = 0;
  const sync = createSessionMetadataSync({ navigation: { get: () => route }, store,
    readSession: async () => session("one", 2), loadSessions: () => sidebar.promise,
    onRestored: async () => { restores++; } });
  const pending = sync.refresh(); let finished = false;
  void pending.then(() => { finished = true; });
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(restores, 1); assert.equal(finished, true);
  sidebar.resolve(); assert.equal(await pending, true);
});

const flush = () => new Promise(resolve => setImmediate(resolve));
const unavailable = (status = 503) => Object.assign(new Error("unavailable"), { status });
function environment(readSession, status = "active") {
  let time = 0, serial = 0;
  let route = { view: "workspace", projectId: "default", sessionId: "one" };
  const listeners = new Set(), timers = new Map(), waits = [], page = new EventTarget();
  const store = createResourceStore(); store.setData(session("one", 1, status));
  let sidebarLoads = 0, restores = 0;
  const sync = createSessionMetadataSync({ store, readSession,
    navigation: { get: () => route, subscribe(listener) {
      listeners.add(listener); listener(route); return () => listeners.delete(listener);
    } },
    now: () => time,
    createRecovery: () => createRequestRecovery({ now: () => time, random: () => 0,
      eventTarget: page,
      setTimer(fn, delay) { timers.set(++serial, { fn, at: time + delay, delay }); return serial; },
      clearTimer(id) { timers.delete(id); },
    }),
    loadSessions: async () => { sidebarLoads++; }, onRestored: async () => { restores++; } });
  return { sync, store, page, timers, waits, listeners,
    effects: () => ({ sidebarLoads, restores }), elapsed: () => time,
    advance: delay => { time += delay; },
    navigate(next) { route = { ...route, ...next }; for (const listener of listeners) listener(route); },
    async settle(promise) {
      let done = false, value, error;
      promise.then(result => { value = result; done = true; }, failure => { error = failure; done = true; });
      for (let step = 0; !done && step < 100; ++step) {
        await flush(); if (done) break;
        assert.ok(timers.size, "background reads must have a finite deadline");
        const [id, timer] = [...timers].sort((a, b) => a[1].at - b[1].at)[0];
        timers.delete(id); time = timer.at; waits.push(timer.delay); timer.fn();
      }
      assert.equal(done, true); assert.equal(timers.size, 0);
      if (error) throw error;
      return value;
    },
  };
}

test("metadata retries transient reads quietly and coalesces the whole recovery", async () => {
  let reads = 0; const signals = [];
  const env = environment(async (_project, _id, options) => {
    signals.push(options?.signal); if (++reads < 3) throw unavailable();
    return session("one", 2, "archived");
  });
  const first = env.sync.refresh(), second = env.sync.refresh();
  assert.deepEqual(await env.settle(Promise.all([first, second])), [true, true]);
  assert.equal(reads, 3); assert.deepEqual(env.waits, [500, 1000]);
  assert.ok(signals.every(signal => signal instanceof AbortSignal));
  assert.equal(env.store.get().error, null); assert.equal(env.store.get().data.status, "archived");
  assert.deepEqual(env.effects(), { sidebarLoads: 1, restores: 0 });
  env.sync.destroy();
});

test("exhaustion keeps saved metadata and periodic polls do not restart six retries", async () => {
  let reads = 0, available = false; const failure = unavailable(429);
  const env = environment(async () => {
    reads++; if (!available) throw failure; return session("one", 2);
  });
  await assert.rejects(env.settle(env.sync.refresh()), error => error === failure);
  assert.equal(reads, 6); assert.deepEqual(env.waits, [500, 1000, 2000, 4000, 8000]);
  assert.equal(env.store.get().data.revision, 1); assert.equal(env.store.get().error, null);
  assert.equal(await env.sync.refresh(), false); assert.equal(reads, 6);
  env.advance(30000);
  await assert.rejects(env.settle(env.sync.refresh()), error => error === failure);
  assert.equal(reads, 7, "an exhausted background check gets only one cooldown probe");
  available = true; env.advance(30000);
  assert.equal(await env.settle(env.sync.refresh()), true); assert.equal(reads, 8);
  env.sync.destroy();
});

test("a hanging read cannot hold its session in flight forever even if it ignores abort", async () => {
  let hanging = true, reads = 0; const signals = [];
  const env = environment((_project, _id, options) => {
    reads++; signals.push(options?.signal);
    return hanging ? new Promise(() => {}) : Promise.resolve(session("one", 2));
  });
  await assert.rejects(env.settle(env.sync.refresh()), { code: "network_error" });
  assert.ok(env.elapsed() <= 60000); assert.equal(reads, 6);
  assert.ok(signals.every(signal => signal.aborted));
  hanging = false; env.advance(30000);
  assert.equal(await env.settle(env.sync.refresh()), true); assert.equal(reads, 7);
  env.sync.destroy();
});

test("leaving a session cancels its read and returning starts a fresh check", async () => {
  const pending = deferred(); let reads = 0, firstSignal;
  const env = environment((_project, _id, options) => {
    if (++reads === 1) { firstSignal = options?.signal; return pending.promise; }
    return Promise.resolve(session("one", 2, "archived"));
  });
  const first = env.sync.refresh(), coalesced = env.sync.refresh(); await flush();
  env.navigate({ view: "settings" });
  assert.deepEqual(await env.settle(Promise.all([first, coalesced])), [false, false]);
  assert.equal(firstSignal.aborted, true);
  env.navigate({ view: "workspace" });
  assert.equal(await env.settle(env.sync.refresh()), true); assert.equal(reads, 2);
  pending.resolve(session("one", 99)); await flush();
  assert.equal(env.store.get().data.revision, 2); assert.equal(env.store.get().data.status, "archived");
  env.sync.destroy();
});

test("a confirmed local revision cancels stale metadata recovery before another attempt", async () => {
  let reads = 0;
  const env = environment(async () => { reads++; throw unavailable(); });
  const first = env.sync.refresh(); await flush();
  assert.equal(env.timers.size, 1);
  env.store.setData(session("one", 3));
  assert.equal(await env.settle(first), false); assert.equal(reads, 1);
  assert.equal(env.store.get().data.revision, 3); env.sync.destroy();
});

test("a selected-detail load supersedes an already pending background check", async () => {
  let signal, reads = 0; const detail = deferred();
  const env = environment((_project, _id, options) => {
    reads++; signal = options?.signal; return new Promise(() => {});
  });
  const first = env.sync.refresh(); await flush();
  const foreground = env.store.load(() => detail.promise);
  assert.equal(await env.settle(first), false); assert.equal(signal.aborted, true);
  assert.equal(await env.sync.refresh(), false); assert.equal(reads, 1);
  detail.resolve(session("one", 2)); await foreground;
  env.sync.destroy();
});

test("page exit releases metadata reads and a later check can proceed", async () => {
  let reads = 0;
  const env = environment(async () => ++reads === 1 ? new Promise(() => {}) : session("one", 2));
  const first = env.sync.refresh(); await flush();
  env.page.dispatchEvent(new Event("pagehide"));
  assert.equal(await env.settle(first), false);
  env.page.dispatchEvent(new Event("pageshow"));
  assert.equal(await env.settle(env.sync.refresh()), true); assert.equal(reads, 2);
  env.sync.destroy();
});

test("definite refusals are not retried and do not overwrite saved metadata", async () => {
  let reads = 0; const failure = unavailable(403);
  const env = environment(async () => { reads++; throw failure; });
  await assert.rejects(env.settle(env.sync.refresh()), error => error === failure);
  assert.equal(reads, 1); assert.equal(env.store.get().data.revision, 1);
  assert.equal(env.store.get().error, null); env.sync.destroy();
});

test("destroy cancels active reads and unsubscribes from navigation", async () => {
  let reads = 0;
  const env = environment(async () => { reads++; return new Promise(() => {}); });
  const first = env.sync.refresh(); await flush(); env.sync.destroy();
  assert.equal(await env.settle(first), false); assert.equal(env.listeners.size, 0);
  assert.equal(await env.sync.refresh(), false); assert.equal(reads, 1);
});
