import assert from "node:assert/strict";
import test from "node:test";

import { createTokenMeter } from "../app/web/js/features/chat/token-meter.js";
import { createKeyboardShortcuts } from
  "../app/web/js/features/shell/keyboard-shortcuts.js";

class Node extends EventTarget {
  constructor(owner) {
    super();
    this.owner = owner;
    this.children = [];
    this.attributes = new Map();
    this.style = { setProperty() {} };
    this.value = "";
    this.hidden = false;
  }

  append(...children) { this.children.push(...children); }
  replaceChildren(...children) { this.children = [...children]; }
  contains(target) {
    return this === target || this.children.some((child) => child.contains?.(target));
  }
  setAttribute(name, value) { this.attributes.set(name, String(value)); }
  getAttribute(name) { return this.attributes.get(name) ?? null; }
  focus() { this.owner.activeElement = this; }
  querySelector() { return null; }
}

function resource(data) {
  return { get: () => ({ data }), subscribe(listener) {
    listener({ data });
    return () => {};
  } };
}

test("Token meter Escape closes the panel before the global stop shortcut", () => {
  const previousDocument = globalThis.document;
  const document = new EventTarget();
  document.createElement = () => new Node(document);
  document.querySelector = () => null;
  document.body = new Node(document);
  document.activeElement = document.body;
  globalThis.document = document;
  const root = new Node(document);
  const trigger = new Node(document);
  const panel = new Node(document);
  const prompt = new Node(document);
  const estimate = new Node(document);
  const ring = new Node(document);
  const dialog = new Node(document);
  root.append(trigger, panel);
  let stops = 0;
  let meter;
  try {
    meter = createTokenMeter({ root, trigger, panel, prompt, estimate, ring,
      modelSelect: { value: "ling" },
      sessionStore: resource(null), timelineStore: resource({ events: [] }),
      modelsStore: resource({ models: [{ id: "ling", name: "Ling",
        context_window_tokens: 1024 }] }) });
    createKeyboardShortcuts({ dialog,
      navigation: { get: () => ({ view: "workspace", sessionId: "s" }) },
      search: { isOpen: () => false },
      onNew() {}, onExport() {}, onSettings() {}, onToggleTheme() {},
      onStop: () => { stops += 1; }, isRunning: () => true,
      isDrawerOpen: () => false, closeDrawers() {} });

    trigger.dispatchEvent(new Event("click"));
    assert.equal(panel.hidden, false);
    assert.equal(document.activeElement, panel);

    function escape(keyCode = 27) {
      const event = new Event("keydown", { cancelable: true });
      Object.defineProperties(event, { key: { value: "Escape" },
        keyCode: { value: keyCode } });
      document.dispatchEvent(event);
      return event;
    }
    assert.equal(escape(229).defaultPrevented, false);
    assert.equal(panel.hidden, false, "an IME candidate key must not close the panel");
    assert.equal(stops, 0);

    assert.equal(escape().defaultPrevented, true);
    assert.equal(panel.hidden, true);
    assert.equal(document.activeElement, trigger);
    assert.equal(stops, 0, "closing the panel must not stop the run");

    escape();
    assert.equal(stops, 1, "a subsequent bare Escape still stops the run");
  } finally {
    meter?.destroy();
    globalThis.document = previousDocument;
  }
});
