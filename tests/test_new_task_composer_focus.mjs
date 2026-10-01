import assert from "node:assert/strict";
import test from "node:test";
import { createNewTaskComposerFocus } from
  "../app/web/js/features/chat/new-task-composer-focus.js";

function fixture(check) {
  const previous = globalThis.document;
  const document = new EventTarget();
  document.body = { isConnected: true };
  document.defaultView = new EventTarget();
  document.hidden = false;
  globalThis.document = document;
  const listeners = new Set();
  let route = { view: "workspace", projectId: "default", sessionId: "" };
  const navigation = {
    get: () => route, preferredProject: () => "default",
    subscribe(listener) {
      listeners.add(listener);
      listener(route);
      return () => listeners.delete(listener);
    },
    select(projectId, sessionId, view = "workspace") {
      route = { view, projectId, sessionId };
      for (const listener of listeners) listener(route);
    },
  };
  let focusCount = 0;
  const prompt = { disabled: false, isConnected: true,
    focus(options) {
      assert.deepEqual(options, { preventScroll: true });
      focusCount += 1;
      document.activeElement = prompt;
      emit("focusin", prompt);
    } };
  document.activeElement = prompt;
  function emit(type, target) {
    const event = new Event(type);
    Object.defineProperty(event, "target", { value: target });
    document.dispatchEvent(event);
  }
  const focus = createNewTaskComposerFocus({ prompt, navigation });
  const state = { document, prompt, focus, navigation, emit,
    focused: () => focusCount,
    disable() { prompt.disabled = true; document.activeElement = document.body; },
    migrate() { focus.migrate("default", "created"); navigation.select("default", "created"); },
  };
  try { check(state); }
  finally { focus.destroy(); globalThis.document = previous; }
}

test("new-task focus survives disabling and returns once in the created session", () => {
  fixture(({ focus, prompt, disable, migrate, focused }) => {
    focus.capture();
    disable();
    prompt.disabled = false;
    assert.equal(focus.restore(), false, "the new-task journal is still copying");
    prompt.disabled = true;
    migrate();
    assert.equal(focus.restore(), false, "the target editor is not ready");
    prompt.disabled = false;
    assert.equal(focus.restore(), true);
    assert.equal(focus.restore(), false);
    assert.equal(focused(), 1);
  });
});

test("background recovery and existing sessions never acquire composer focus", () => {
  for (const origin of ["body", "existing", "settings"]) {
    fixture(({ document, prompt, focus, navigation, disable, migrate, focused }) => {
      if (origin === "body") document.activeElement = document.body;
      else navigation.select("default", origin === "existing" ? "old" : "",
        origin === "existing" ? "workspace" : origin);
      focus.capture();
      disable();
      navigation.select("default", "");
      migrate();
      prompt.disabled = false;
      assert.equal(focus.restore(), false);
      assert.equal(focused(), 0);
    });
  }
});

test("a newer focus or non-focusable click cannot be undone by migration", () => {
  for (const type of ["focusin", "pointerdown"]) {
    fixture(({ document, prompt, focus, disable, migrate, emit, focused }) => {
      focus.capture();
      disable();
      const other = { isConnected: true };
      document.activeElement = type === "focusin" ? other : document.body;
      emit(type, other);
      document.activeElement = document.body;
      migrate();
      prompt.disabled = false;
      assert.equal(focus.restore(), false);
      assert.equal(focused(), 0);
    });
  }
});

test("leaving the original project, task or workspace cancels pending focus", () => {
  for (const [projectId, sessionId, view] of [
    ["other", "", "workspace"], ["default", "other", "workspace"],
    ["", "", "settings"],
  ]) {
    fixture(({ prompt, focus, navigation, disable, migrate, focused }) => {
      focus.capture();
      disable();
      navigation.select(projectId, sessionId, view);
      navigation.select("default", "");
      migrate();
      prompt.disabled = false;
      assert.equal(focus.restore(), false);
      assert.equal(focused(), 0);
    });
  }
});

test("a hidden page or unfocused window never reopens its composer", () => {
  for (const type of ["visibilitychange", "blur"]) {
    fixture(({ document, prompt, focus, disable, migrate, focused }) => {
      focus.capture();
      disable();
      if (type === "blur") document.defaultView.dispatchEvent(new Event(type));
      else { document.hidden = true; document.dispatchEvent(new Event(type)); }
      document.hidden = false;
      migrate();
      prompt.disabled = false;
      assert.equal(focus.restore(), false);
      assert.equal(focused(), 0);
    });
  }
});

test("only the acknowledged migration target can receive saved focus", () => {
  fixture(({ prompt, focus, navigation, disable, focused }) => {
    focus.capture();
    disable();
    focus.migrate("other", "created");
    navigation.select("default", "created");
    prompt.disabled = false;
    assert.equal(focus.restore(), false);
    assert.equal(focused(), 0);
  });
});

test("an explicit new-task action or failed preparation can cancel saved focus", () => {
  fixture(({ prompt, focus, disable, migrate, focused }) => {
    focus.capture();
    disable();
    focus.cancel();
    migrate();
    prompt.disabled = false;
    assert.equal(focus.restore(), false);
    assert.equal(focused(), 0);
  });
});
