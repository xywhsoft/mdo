import assert from "node:assert/strict";
import test from "node:test";

import { createSessionMetadataSync } from
  "../app/web/js/features/shell/session-metadata-sync.js";
import { createResourceStore } from "../app/web/js/state/store.js";

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
