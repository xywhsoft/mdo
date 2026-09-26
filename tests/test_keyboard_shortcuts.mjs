import assert from "node:assert/strict";
import test from "node:test";

import { createKeyboardShortcuts } from
  "../app/web/js/features/shell/keyboard-shortcuts.js";
import { isImeKey } from "../app/web/js/utils/dom.js";

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
