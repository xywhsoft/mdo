import assert from "node:assert/strict";
import test from "node:test";

import { createKeyboardShortcuts } from
  "../app/web/js/features/shell/keyboard-shortcuts.js";

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
    function escape(consumed) {
      const event = new Event("keydown", { cancelable: true });
      Object.defineProperty(event, "key", { value: "Escape" });
      if (consumed) event.preventDefault();
      document.dispatchEvent(event);
    }
    escape(true);
    assert.equal(stops, 0);
    assert.equal(closedDrawers, 0);
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
