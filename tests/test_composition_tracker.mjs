import assert from "node:assert/strict";
import test from "node:test";
import { createCompositionTracker } from "../app/web/js/utils/composition.js";

function send(scope, type, target) {
  const event = new Event(type);
  Object.defineProperty(event, "target", { value: target });
  scope.dispatchEvent(event);
}

test("composition belongs to its editor and another field's end cannot release it", () => {
  const scope = new EventTarget();
  const tracker = createCompositionTracker(scope);
  const first = {};
  const second = {};
  try {
    send(scope, "compositionstart", first);
    assert.equal(tracker.isComposing(first), true);
    assert.equal(tracker.isComposing(second), false);
    send(scope, "compositionend", second);
    assert.equal(tracker.isComposing(first), true);
    send(scope, "compositionend", first);
    assert.equal(tracker.isComposing(first), false);
  } finally { tracker.dispose(); }
});

test("editor blur clears only its own unfinished composition", () => {
  const scope = new EventTarget();
  const tracker = createCompositionTracker(scope);
  const first = {};
  const second = {};
  try {
    send(scope, "compositionstart", first);
    send(scope, "compositionstart", second);
    send(scope, "blur", first);
    assert.equal(tracker.isComposing(first), false);
    assert.equal(tracker.isComposing(second), true);
    assert.equal(tracker.isComposing(null), false);
  } finally { tracker.dispose(); }
});

test("losing the owner window clears composition when the same field is revisited", () => {
  const scope = new EventTarget();
  const view = new EventTarget();
  scope.ownerDocument = { defaultView: view };
  const tracker = createCompositionTracker(scope);
  const input = {};
  try {
    send(scope, "compositionstart", input);
    view.dispatchEvent(new Event("blur"));
    assert.equal(tracker.isComposing(input), false);
    send(scope, "compositionstart", input);
    assert.equal(tracker.isComposing(input), true);
  } finally { tracker.dispose(); }
});

test("disposing a component clears its state and removes composition listeners", () => {
  const scope = new EventTarget();
  const tracker = createCompositionTracker(scope);
  const input = {};
  send(scope, "compositionstart", input);
  tracker.dispose();
  assert.equal(tracker.isComposing(input), false);
  send(scope, "compositionstart", input);
  assert.equal(tracker.isComposing(input), false);
  tracker.dispose();
});
