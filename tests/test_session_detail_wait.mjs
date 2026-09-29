import assert from "node:assert/strict";
import test from "node:test";

import { waitForSelectedDetail } from
  "../app/web/js/features/shell/session-detail-wait.js";
import { createResourceStore } from "../app/web/js/state/store.js";

function navigation() {
  let route = { view: "workspace", projectId: "default", sessionId: "A" };
  const listeners = new Set();
  return {
    get: () => route,
    subscribe(listener) {
      listeners.add(listener);
      listener(route);
      return () => listeners.delete(listener);
    },
    select(next) {
      route = next;
      for (const listener of listeners) listener(route);
    },
    get listenerCount() { return listeners.size; },
  };
}

test("a retry unblocks the selected detail before an older read finishes", async () => {
  const nav = navigation();
  const store = createResourceStore();
  let finishOldRead;
  const oldRead = new Promise((resolve) => { finishOldRead = resolve; });
  const selected = () => nav.get().view === "workspace" &&
    nav.get().sessionId === "A";
  const waiting = waitForSelectedDetail({ navigation: nav, store,
    isSelected: selected });
  const stale = store.load(() => oldRead);
  await store.load(async () => ({ project_id: "default", id: "A" }));
  const result = await waiting;
  assert.equal(result.status, "ready");
  assert.equal(result.data.id, "A");
  assert.equal(nav.listenerCount, 0);
  finishOldRead({ project_id: "default", id: "obsolete" });
  await stale;
  assert.equal(store.get().data.id, "A");
});

test("a changed route releases a pending detail wait", async () => {
  const nav = navigation();
  const store = createResourceStore();
  const waiting = waitForSelectedDetail({ navigation: nav, store,
    isSelected: () => nav.get().view === "workspace" &&
      nav.get().sessionId === "A" });
  nav.select({ view: "settings", projectId: "default", sessionId: "A" });
  assert.equal(await waiting, null);
  assert.equal(nav.listenerCount, 0);
});
