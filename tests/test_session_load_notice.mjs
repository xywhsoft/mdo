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

test("a final detail error stays visible with cached history and retries task metadata", () => {
  const originalWindow = globalThis.window;
  globalThis.window = { setTimeout: () => 1, clearTimeout() {} };
  const route = { view: "workspace", projectId: "default", sessionId: "A" };
  const detail = { project_id: "default", id: "A" };
  let state = { status: "ready", data: detail }, listener;
  let revalidations = 0, historyRetries = 0;
  const notice = node(), heading = node(), description = node(), retry = node();
  const store = { get: () => state, subscribe(callback) { listener = callback; callback(); } };
  const navigation = { get: () => route, subscribe(callback) { callback(); },
    revalidate() { revalidations++; state = { status: "refreshing", data: detail }; listener(); } };
  const history = { status: "ready", data: { projectId: "default", sessionId: "A",
    events: [{ text: "Saved reply" }], initializing: false, syncing: false } };
  try {
    createSessionLoadNotice({ navigation, store, notice, heading, description, retry,
      conversation: node(), sessionTitle: node(), sessionSubtitle: node(),
      mobileTitle: node(), mobileMeta: node(), prompt: node(),
      timeline: { get: () => history, subscribe(callback) { callback(); } },
      onRetryHistory() { historyRetries++; } });
    assert.equal(notice.hidden, true);
    state = { status: "error", data: detail, error: Object.assign(new Error("internal server message"), { status: 503 }) };
    listener(); assert.equal(notice.hidden, false); assert.equal(retry.hidden, false);
    assert.match(description.textContent, /任务信息/); assert.doesNotMatch(description.textContent, /internal/);
    retry.click(); assert.equal(revalidations, 1); assert.equal(historyRetries, 0);
    state = { status: "error", data: null,
      error: Object.assign(new Error("internal permission message"), { code: "permission_denied", status: 403 }) };
    listener(); assert.match(description.textContent, /无权/);
    assert.doesNotMatch(description.textContent, /internal/);
  } finally { globalThis.window = originalWindow; }
});

function historyNotice(error, { cached = true, detailError = null } = {}) {
  const route = { view: "workspace", projectId: "default", sessionId: "A" };
  const detail = { project_id: "default", id: "A" };
  let state = { status: detailError ? "error" : "ready", data: detail, error: detailError };
  let history = { status: "ready", data: { projectId: "default", sessionId: "A",
    initializing: !cached, syncing: false, events: cached ? [{ text: "Saved reply" }] : [], syncError: error } };
  let listener, historyRetries = 0, revalidations = 0;
  const notice = node(), description = node(), retry = node(), prompt = node();
  createSessionLoadNotice({ navigation: { get: () => route, subscribe(callback) { callback(); },
    revalidate() { revalidations++; } },
    store: { get: () => state, subscribe(callback) { callback(); } },
    timeline: { get: () => history, subscribe(callback) { listener = callback; callback(); } },
    conversation: node(), notice, description, retry, prompt, heading: node(), sessionTitle: node(),
    sessionSubtitle: node(), mobileTitle: node(), mobileMeta: node(),
    onRetryHistory() { historyRetries++; history = { status: "ready", data: { ...history.data,
      syncError: null, initializing: false, syncing: true } }; listener(); } });
  return { notice, description, retry, prompt, counts: () => ({ historyRetries, revalidations }),
    recover() { history = { status: "ready", data: { ...history.data, syncing: false, syncError: null } }; listener(); } };
}

test("exhausted history reads explain the failure instead of describing an ongoing check", () => {
  const oldWindow = globalThis.window;
  globalThis.window = { setTimeout: () => 1, clearTimeout() {} };
  try {
    const view = historyNotice(Object.assign(new Error("internal transport failure"), { code: "network_error" }));
    assert.equal(view.notice.hidden, false); assert.equal(view.notice.role, "alert");
    assert.match(view.description.textContent, /暂时无法读取.*对话/);
    assert.doesNotMatch(view.description.textContent, /正在检查|internal/);
    view.retry.click(); assert.deepEqual(view.counts(), { historyRetries: 1, revalidations: 0 });
    assert.equal(view.notice.role, "status");
    view.recover(); assert.equal(view.notice.hidden, true); assert.equal(view.prompt.focused, 1);
  } finally { globalThis.window = oldWindow; }
});

test("a cold history denial shows its precise cause when task metadata is already present", () => {
  const oldWindow = globalThis.window;
  globalThis.window = { setTimeout: () => 1, clearTimeout() {} };
  try {
    const view = historyNotice(Object.assign(new Error("internal denial"), { code: "permission_denied", status: 403 }), { cached: false });
    assert.equal(view.notice.hidden, false); assert.match(view.description.textContent, /无权/);
    assert.doesNotMatch(view.description.textContent, /正在读取|internal/);
  } finally { globalThis.window = oldWindow; }
});

test("task metadata errors retain priority over a simultaneous history failure", () => {
  const oldWindow = globalThis.window;
  globalThis.window = { setTimeout: () => 1, clearTimeout() {} };
  try {
    const view = historyNotice(Object.assign(new Error("history transport"), { code: "network_error" }),
      { detailError: Object.assign(new Error("detail denial"), { code: "permission_denied", status: 403 }) });
    assert.match(view.description.textContent, /无权/);
    view.retry.click(); assert.deepEqual(view.counts(), { historyRetries: 0, revalidations: 1 });
  } finally { globalThis.window = oldWindow; }
});
