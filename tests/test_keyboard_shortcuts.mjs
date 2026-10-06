import assert from "node:assert/strict";
import test from "node:test";

import { createKeyboardShortcuts } from
  "../app/web/js/features/shell/keyboard-shortcuts.js";
import { isImeKey } from "../app/web/js/utils/dom.js";

function targetedEvent(type, target, fields = {}) {
  const event = new Event(type, { cancelable: true });
  Object.defineProperty(event, "target", { value: target });
  for (const [name, value] of Object.entries(fields))
    Object.defineProperty(event, name, { value });
  return event;
}

test("composition in a removed or different editor cannot block new-task shortcuts", () => {
  const originalDocument = globalThis.document;
  const document = new EventTarget();
  document.querySelector = () => null;
  globalThis.document = document;
  const first = {};
  const second = {};
  const dialog = new EventTarget();
  let created = 0;
  try {
    createKeyboardShortcuts({ dialog,
      navigation: { get: () => ({ view: "workspace" }) },
      search: { isOpen: () => false },
      onNew() { created += 1; }, onExport() {}, onSettings() {}, onToggleTheme() {},
      onStop() {}, isRunning: () => false,
      isDrawerOpen: () => false, closeDrawers() {} });
    document.dispatchEvent(targetedEvent("compositionstart", first));
    document.dispatchEvent(targetedEvent("keydown", first, { key: "k", ctrlKey: true }));
    assert.equal(created, 0);
    document.dispatchEvent(targetedEvent("keydown", second, { key: "k", ctrlKey: true }));
    assert.equal(created, 1);
    document.dispatchEvent(targetedEvent("blur", first));
    document.dispatchEvent(targetedEvent("keydown", first, { key: "k", ctrlKey: true }));
    assert.equal(created, 2);
  } finally { globalThis.document = originalDocument; }
});

test("window focus loss releases an unfinished composition before shortcuts resume", () => {
  const originalDocument = globalThis.document;
  const document = new EventTarget();
  document.querySelector = () => null;
  document.defaultView = new EventTarget();
  globalThis.document = document;
  const editor = {};
  let created = 0;
  try {
    createKeyboardShortcuts({ dialog: new EventTarget(),
      navigation: { get: () => ({ view: "workspace" }) },
      search: { isOpen: () => false },
      onNew() { created += 1; }, onExport() {}, onSettings() {}, onToggleTheme() {},
      onStop() {}, isRunning: () => false,
      isDrawerOpen: () => false, closeDrawers() {} });
    document.dispatchEvent(targetedEvent("compositionstart", editor));
    document.defaultView.dispatchEvent(new Event("blur"));
    document.dispatchEvent(targetedEvent("keydown", editor, { key: "k", ctrlKey: true }));
    assert.equal(created, 1);
  } finally { globalThis.document = originalDocument; }
});

test("IME candidate keys cannot submit a composer turn", () => {
  assert.equal(isImeKey({ isComposing: false, keyCode: 229 }), true);
  assert.equal(isImeKey({ isComposing: false, keyCode: 13 }, true), true);
  assert.equal(isImeKey({ isComposing: false, keyCode: 13 }), false);
});

test("Escape respects a composer menu before handling a run or drawer", () => {
  const originalDocument = globalThis.document;
  const document = new EventTarget();
  document.querySelector = () => null;
  globalThis.document = document;
  const dialog = new EventTarget();
  dialog.querySelector = () => null;
  let stops = 0;
  let closedDrawers = 0;
  let drawerOpen = false;
  try {
    createKeyboardShortcuts({
      dialog,
      navigation: { get: () => ({ view: "workspace", sessionId: "test" }) },
      search: { isOpen: () => false },
      onNew() {}, onExport() {}, onSettings() {}, onToggleTheme() {},
      onStop: () => { stops += 1; }, isRunning: () => true,
      isDrawerOpen: () => drawerOpen,
      closeDrawers: () => { closedDrawers += 1; },
    });
    function escape(consumed, ime = false) {
      const event = new Event("keydown", { cancelable: true });
      Object.defineProperty(event, "key", { value: "Escape" });
      Object.defineProperty(event, "keyCode", { value: ime ? 229 : 27 });
      if (consumed) event.preventDefault();
      document.dispatchEvent(event);
    }
    escape(true);
    assert.equal(stops, 0);
    assert.equal(closedDrawers, 0);
    document.dispatchEvent(new Event("compositionstart"));
    escape(false);
    assert.equal(stops, 0);
    document.dispatchEvent(new Event("compositionend"));
    escape(false, true);
    assert.equal(stops, 0);
    escape(false);
    assert.equal(stops, 1);
    drawerOpen = true;
    escape(false);
    assert.equal(stops, 1);
    assert.equal(closedDrawers, 1);
  } finally {
    globalThis.document = originalDocument;
  }
});

test("slash search only claims keys in the workspace outside composition", () => {
  const originalDocument = globalThis.document;
  const originalElement = globalThis.Element;
  const document = new EventTarget();
  document.querySelector = () => null;
  globalThis.document = document;
  globalThis.Element = class {};
  const dialog = new EventTarget();
  dialog.querySelector = () => null;
  let view = "workspace";
  let searches = 0;
  try {
    createKeyboardShortcuts({
      dialog, navigation: { get: () => ({ view, sessionId: "test" }) },
      search: { isOpen: () => false },
      onNew() {}, onExport() {}, onSettings() {}, onToggleTheme() {},
      onSessionSearch: () => { searches += 1; },
      onStop() {}, isRunning: () => false,
      isDrawerOpen: () => false, closeDrawers() {},
    });
    function slash() {
      const event = new Event("keydown", { cancelable: true });
      Object.defineProperty(event, "key", { value: "/" });
      document.dispatchEvent(event);
      return event.defaultPrevented;
    }
    assert.equal(slash(), true);
    assert.equal(searches, 1);
    view = "settings";
    assert.equal(slash(), false);
    assert.equal(searches, 1);
    view = "workspace";
    document.dispatchEvent(new Event("compositionstart"));
    assert.equal(slash(), false);
    assert.equal(searches, 1);
    document.dispatchEvent(new Event("compositionend"));
    assert.equal(slash(), true);
    assert.equal(searches, 2);
  } finally {
    globalThis.document = originalDocument;
    globalThis.Element = originalElement;
  }
});

test("Escape leaves scheduled tasks without stopping the background conversation", () => {
  const previous = globalThis.document;
  const document = new EventTarget();
  let modalOpen = true, drawerOpen = false;
  document.querySelector = () => modalOpen ? {} : null;
  globalThis.document = document;
  let returns = 0, stops = 0, drawerCloses = 0;
  try {
    createKeyboardShortcuts({ dialog: new EventTarget(),
      navigation: { get: () => ({ view: "schedules" }),
        backToWorkspace: () => { returns += 1; } },
      search: { isOpen: () => false }, onNew() {}, onExport() {},
      onSettings() {}, onToggleTheme() {}, onStop: () => { stops += 1; },
      isRunning: () => true, isDrawerOpen: () => drawerOpen,
      closeDrawers: () => { drawerCloses += 1; } });
    document.dispatchEvent(targetedEvent("keydown", {}, { key: "Escape" }));
    assert.equal(returns, 0);
    modalOpen = false; drawerOpen = true;
    document.dispatchEvent(targetedEvent("keydown", {}, { key: "Escape" }));
    assert.equal(drawerCloses, 1);
    assert.equal(returns, 0);
    drawerOpen = false;
    const event = targetedEvent("keydown", {}, { key: "Escape" });
    document.dispatchEvent(event);
    assert.equal(returns, 1);
    assert.equal(stops, 0);
    assert.equal(event.defaultPrevented, true);
  } finally { globalThis.document = previous; }
});
