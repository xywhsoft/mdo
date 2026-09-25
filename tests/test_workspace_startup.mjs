import assert from "node:assert/strict";
import test from "node:test";

import { startWorkspaceNavigation } from
  "../app/web/js/features/shell/workspace-startup.js";
import { createResourceStore } from "../app/web/js/state/store.js";

function fakeNavigation() {
  let route = { view: "workspace", projectId: "", sessionId: "" };
  const listeners = new Set();
  function publish(next) {
    route = next;
    for (const listener of listeners) listener(route);
  }
  return {
    get: () => route,
    subscribe(listener) {
      listeners.add(listener);
      listener(route);
      return () => listeners.delete(listener);
    },
    select(projectId, sessionId) {
      publish({ view: "workspace", projectId, sessionId });
    },
    newTask(projectId) {
      publish({ view: "workspace", projectId, sessionId: "" });
    },
    clear() { publish({ view: "workspace", projectId: "", sessionId: "" }); },
  };
}

test("startup focuses a ready composer without stealing focus after navigation", async () => {
  const originalFetch = globalThis.fetch;
  const originalLocation = globalThis.location;
  const session = { project_id: "default", id: "S1", title: "Last task" };
  globalThis.location = { hash: "" };
  globalThis.fetch = async (url) => {
    const path = String(url);
    const data = path.endsWith("/workspace-state")
      ? { project_id: "default", session_id: "S1" } : session;
    return Response.json({ ok: true, data });
  };
  const sessionsStore = { get: () => ({ data: { items: [session] } }) };
  const base = { sessionsStore, dialog: {}, title: {}, continueButton: {},
    newButton: {}, entryHash: "" };
  try {
    const newNavigation = fakeNavigation();
    let newFocuses = 0;
    await startWorkspaceNavigation({ ...base, navigation: newNavigation,
      settingsStore: { get: () => ({ data: { workspace: { open_mode: "new" } } }) },
      sessionDetailStore: createResourceStore(),
      prompt: { disabled: false, focus() { newFocuses += 1; } } });
    assert.equal(newNavigation.get().sessionId, "");
    assert.equal(newFocuses, 1);

    const lastNavigation = fakeNavigation();
    const lastDetail = createResourceStore();
    let lastFocuses = 0;
    const lastPrompt = { disabled: true, focus() { lastFocuses += 1; } };
    await startWorkspaceNavigation({ ...base, navigation: lastNavigation,
      settingsStore: { get: () => ({ data: { workspace: { open_mode: "last" } } }) },
      sessionDetailStore: lastDetail, prompt: lastPrompt });
    assert.equal(lastNavigation.get().sessionId, "S1");
    assert.equal(lastFocuses, 0);
    lastPrompt.disabled = false;
    lastDetail.setData(session);
    assert.equal(lastFocuses, 1);
    lastDetail.setData(session);
    assert.equal(lastFocuses, 1);

    const awayNavigation = fakeNavigation();
    const awayDetail = createResourceStore();
    let awayFocuses = 0;
    await startWorkspaceNavigation({ ...base, navigation: awayNavigation,
      settingsStore: { get: () => ({ data: { workspace: { open_mode: "last" } } }) },
      sessionDetailStore: awayDetail,
      prompt: { disabled: false, focus() { awayFocuses += 1; } } });
    awayNavigation.newTask("default");
    awayDetail.setData(session);
    assert.equal(awayFocuses, 0);
  } finally {
    globalThis.fetch = originalFetch;
    globalThis.location = originalLocation;
  }
});
