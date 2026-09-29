import assert from "node:assert/strict";
import test from "node:test";

import { createSessionLoadNotice } from
  "../app/web/js/features/shell/session-load-notice.js";

function node() {
  const events = new Map();
  return {
    dataset: {}, hidden: true, textContent: "", focused: 0,
    setAttribute(name, value) { this[name] = value; },
    addEventListener(name, listener) { events.set(name, listener); },
    click() { events.get("click")?.(); },
    focus() { this.focused += 1; },
  };
}

test("a direct task has visible loading, retry, and focus recovery", () => {
  const originalWindow = globalThis.window;
  const timers = new Map();
  let nextTimer = 1;
  globalThis.window = {
    setTimeout(callback) {
      const id = nextTimer++;
      timers.set(id, () => { timers.delete(id); callback(); });
      return id;
    },
    clearTimeout(id) { timers.delete(id); },
  };
  let route = { view: "workspace", projectId: "default", sessionId: "A" };
  let state = { status: "idle", data: null };
  let revalidations = 0;
  const routeListeners = [];
  const stateListeners = [];
  const store = {
    get: () => state,
    subscribe(listener) { stateListeners.push(listener); listener(state); },
    set(next) { state = next; for (const listener of stateListeners) listener(state); },
  };
  const navigation = {
    get: () => route,
    subscribe(listener) { routeListeners.push(listener); listener(route); },
    revalidate() {
      revalidations += 1;
      for (const listener of routeListeners) listener(route);
      store.set({ status: "loading", data: null });
    },
    select(sessionId) {
      route = { view: "workspace", projectId: "default", sessionId };
      for (const listener of routeListeners) listener(route);
    },
  };
  const conversation = node();
  const notice = node();
  const heading = node();
  const description = node();
  const retry = node();
  const sessionTitle = node();
  const sessionSubtitle = node();
  const mobileTitle = node();
  const mobileMeta = node();
  const prompt = node();
  prompt.disabled = true;
  try {
    createSessionLoadNotice({ navigation, store, conversation, notice,
      heading, description, retry, sessionTitle, sessionSubtitle,
      mobileTitle, mobileMeta, prompt });
    assert.equal(conversation.dataset.sessionLoad, "loading");
    assert.equal(notice.hidden, false);
    assert.equal(retry.hidden, true);

    store.set({ status: "error", data: null, error: new Error("503") });
    assert.equal(conversation.dataset.sessionLoad, "error");
    assert.equal(retry.hidden, false);
    assert.equal(sessionTitle.textContent, heading.textContent);
    retry.click();
    assert.equal(conversation.dataset.sessionLoad, "loading");
    assert.equal(retry.hidden, false);
    assert.equal(retry["aria-disabled"], "true");
    assert.equal(retry.textContent.length > 0, true);
    retry.click();
    assert.equal(revalidations, 1);
    store.set({ status: "error", data: null, error: new Error("503 again") });
    assert.equal(retry["aria-disabled"], "false");
    assert.equal(retry.focused, 1);
    retry.click();
    assert.equal(revalidations, 2);
    prompt.disabled = false;
    // In production, the session subscriber enables the editor before the
    // notice subscriber runs; simulate that ordering for this isolated module.
    store.set({ status: "ready", data: { project_id: "default", id: "A" } });
    assert.equal(notice.hidden, true);
    assert.equal(conversation.dataset.sessionLoad, undefined);
    assert.equal(prompt.focused, 1);

    navigation.select("B");
    store.set({ status: "loading", data: null });
    assert.equal(conversation.dataset.sessionLoad, "loading");
    const pending = [...timers.values()][0];
    pending();
    assert.equal(conversation.dataset.sessionLoad, "delayed");
    assert.equal(retry.hidden, false);
    prompt.disabled = false;
    retry.click();
    store.set({ status: "ready", data: { project_id: "default", id: "B" } });
    assert.equal(notice.hidden, true);
    assert.equal(prompt.focused, 2);

    navigation.select("");
    assert.equal(notice.hidden, true);
    assert.equal(timers.size, 0);
  } finally {
    globalThis.window = originalWindow;
  }
});
