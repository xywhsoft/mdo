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

test("an explicit session URL focuses after loading without stealing a newer focus", async () => {
  const originalFetch = globalThis.fetch;
  const originalLocation = globalThis.location;
  const originalDocument = globalThis.document;
  const session = { project_id: "default", id: "S1", title: "Linked task" };
  const body = { isConnected: true };
  globalThis.document = { body, activeElement: body };
  globalThis.location = { hash: "#/projects/default/sessions/S1" };
  globalThis.fetch = async () => Response.json({ ok: true, data: {
    project_id: "default", session_id: "S1",
  } });
  const base = { settingsStore: { get: () => ({ data: {} }) },
    sessionsStore: { get: () => ({ data: { items: [session] } }) },
    dialog: {}, title: {}, continueButton: {}, newButton: {},
    entryHash: globalThis.location.hash };
  try {
    const nav = fakeNavigation();
    nav.select("default", "S1");
    const detail = createResourceStore();
    let focused = 0;
    const prompt = { disabled: true, focus() { focused += 1; } };
    await startWorkspaceNavigation({ ...base, navigation: nav,
      sessionDetailStore: detail, prompt });
    assert.equal(focused, 0);
    prompt.disabled = false;
    detail.setData(session);
    assert.equal(focused, 1);

    const laterNav = fakeNavigation();
    laterNav.select("default", "S1");
    const laterDetail = createResourceStore();
    let stolen = 0;
    globalThis.document.activeElement = body;
    await startWorkspaceNavigation({ ...base, navigation: laterNav,
      sessionDetailStore: laterDetail,
      prompt: { disabled: false, focus() { stolen += 1; } } });
    globalThis.document.activeElement = { isConnected: true };
    laterDetail.setData(session);
    assert.equal(stolen, 0);

    const newNav = fakeNavigation();
    newNav.newTask("default");
    globalThis.location.hash = "#/projects/default/new";
    globalThis.document.activeElement = body;
    let newFocuses = 0;
    await startWorkspaceNavigation({ ...base, navigation: newNav,
      sessionDetailStore: createResourceStore(),
      prompt: { disabled: false, focus() { newFocuses += 1; } } });
    assert.equal(newFocuses, 1);
  } finally {
    globalThis.fetch = originalFetch;
    globalThis.location = originalLocation;
    globalThis.document = originalDocument;
  }
});

test("last startup resumes a live session before an idle saved session", async () => {
  const originalFetch = globalThis.fetch;
  const originalLocation = globalThis.location;
  const originalDocument = globalThis.document;
  const idle = { project_id: "default", id: "idle", title: "Idle", status: "active" };
  const running = { project_id: "other", id: "live", title: "Live", status: "active" };
  globalThis.location = { hash: "" };
  const body = { isConnected: true };
  globalThis.document = { body, activeElement: body };
  globalThis.fetch = async (url) => Response.json({ ok: true, data:
    String(url).endsWith("/workspace-state")
      ? { project_id: idle.project_id, session_id: idle.id } : idle });
  const sessionsStore = { get: () => ({ data: { items: [idle, running] } }) };
  const runsStore = { get: () => ({ data: { items: [
    { project_id: "other", session_id: "gone", terminal: false },
    { project_id: "default", session_id: "idle", terminal: true },
    { project_id: "other", session_id: "live", terminal: false },
  ] } }) };
  const base = { sessionsStore, runsStore, dialog: {}, title: {},
    continueButton: {}, newButton: {}, entryHash: "", prompt: {
      disabled: false, focus() {},
    } };
  try {
    const last = fakeNavigation();
    await startWorkspaceNavigation({ ...base, navigation: last,
      settingsStore: { get: () => ({ data: { workspace: { open_mode: "last" } } }) },
      sessionDetailStore: createResourceStore() });
    assert.deepEqual([last.get().projectId, last.get().sessionId],
      ["other", "live"]);

    const fresh = fakeNavigation();
    await startWorkspaceNavigation({ ...base, navigation: fresh,
      settingsStore: { get: () => ({ data: { workspace: { open_mode: "new" } } }) },
      sessionDetailStore: createResourceStore() });
    assert.equal(fresh.get().sessionId, "");

    const asking = fakeNavigation();
    let shown = false;
    await startWorkspaceNavigation({ ...base, navigation: asking,
      settingsStore: { get: () => ({ data: { workspace: { open_mode: "ask" } } }) },
      sessionDetailStore: createResourceStore(),
      dialog: { addEventListener() {}, showModal() { shown = true; } },
      continueButton: { addEventListener() {}, focus() {} },
      newButton: { addEventListener() {} } });
    assert.equal(shown, true);
    assert.equal(asking.get().sessionId, "");

    const explicit = fakeNavigation();
    explicit.select("default", "idle");
    globalThis.location.hash = "#/projects/default/sessions/idle";
    await startWorkspaceNavigation({ ...base, navigation: explicit,
      entryHash: globalThis.location.hash,
      settingsStore: { get: () => ({ data: { workspace: { open_mode: "last" } } }) },
      sessionDetailStore: createResourceStore() });
    assert.equal(explicit.get().sessionId, "idle");
  } finally {
    globalThis.fetch = originalFetch;
    globalThis.location = originalLocation;
    globalThis.document = originalDocument;
  }
});
