import assert from "node:assert/strict";
import test from "node:test";

import { focusSessionComposerAfterNavigation } from
  "../app/web/js/features/shell/session-composer-focus.js";
import { createResourceStore } from "../app/web/js/state/store.js";

function navigation() {
  let route = { view: "workspace", projectId: "default", sessionId: "one" };
  const listeners = new Set();
  return {
    subscribe(listener) {
      listeners.add(listener);
      listener(route);
      return () => listeners.delete(listener);
    },
    select(projectId, sessionId) {
      route = { view: "workspace", projectId, sessionId };
      for (const listener of listeners) listener(route);
    },
  };
}

test("pointer-selected session focuses only after its detail is ready", () => {
  const originalDocument = globalThis.document;
  const origin = { isConnected: false, dataset: { sessionKey: "default/one" } };
  const currentButton = {
    isConnected: true, dataset: { sessionKey: "default/one" },
    classList: { contains: (name) => name === "session-item" },
  };
  const body = { isConnected: true };
  globalThis.document = { body, activeElement: currentButton };
  const nav = navigation();
  const detail = createResourceStore();
  let focused = 0;
  const prompt = { disabled: true, focus() { focused += 1; } };
  try {
    focusSessionComposerAfterNavigation({ navigation: nav,
      sessionDetailStore: detail, prompt, projectId: "default", sessionId: "one", origin });
    assert.equal(focused, 0);
    prompt.disabled = false;
    detail.setData({ project_id: "default", id: "one" });
    assert.equal(focused, 1);
    detail.setData({ project_id: "default", id: "one" });
    assert.equal(focused, 1);

    const second = createResourceStore();
    globalThis.document.activeElement = currentButton;
    focusSessionComposerAfterNavigation({ navigation: nav,
      sessionDetailStore: second, prompt, projectId: "default", sessionId: "one", origin });
    globalThis.document.activeElement = { isConnected: true };
    second.setData({ project_id: "default", id: "one" });
    assert.equal(focused, 1, "a newer focus choice must win");

    const third = createResourceStore();
    globalThis.document.activeElement = body;
    focusSessionComposerAfterNavigation({ navigation: nav,
      sessionDetailStore: third, prompt, projectId: "default", sessionId: "one", origin });
    nav.select("default", "other");
    third.setData({ project_id: "default", id: "one" });
    assert.equal(focused, 1, "leaving the session cancels pending focus");
  } finally {
    globalThis.document = originalDocument;
  }
});
