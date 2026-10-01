import assert from "node:assert/strict";
import test from "node:test";

import { copyText } from "../app/web/js/utils/clipboard.js";

function browser({ writeText, copied = true, selection, onCopy } = {}) {
  const oldNavigator = Object.getOwnPropertyDescriptor(globalThis, "navigator");
  const oldDocument = Object.getOwnPropertyDescriptor(globalThis, "document");
  const calls = [];
  const focus = { isConnected: true,
    focus: () => calls.push(["focus"]) };
  const node = { style: {}, value: "", readOnly: false,
    select: () => { calls.push(["select"]); selection?.removeAllRanges(); },
    remove: () => calls.push(["remove"]) };
  Object.defineProperty(globalThis, "navigator", { configurable: true,
    value: { clipboard: writeText ? { writeText } : undefined } });
  const document = { activeElement: focus,
      getSelection: () => selection,
      createElement: () => node,
      body: { append: () => calls.push(["append"]) },
      execCommand: (command) => {
        calls.push([command, node.value]);
        onCopy?.();
        return copied;
      },
    };
  Object.defineProperty(globalThis, "document", { configurable: true, value: document });
  return { calls, node, focus, document, restore() {
    if (oldNavigator) Object.defineProperty(globalThis, "navigator", oldNavigator);
    else delete globalThis.navigator;
    if (oldDocument) Object.defineProperty(globalThis, "document", oldDocument);
    else delete globalThis.document;
  } };
}

test("copy uses the asynchronous API when available", async () => {
  const env = browser({ writeText: async (value) => env.calls.push(["native", value]) });
  try {
    await copyText("message body");
    assert.deepEqual(env.calls, [["native", "message body"]]);
  } finally { env.restore(); }
});

function readingSelection({ multi = false, directional = true, live = false } = {}) {
  const start = { isConnected: true };
  const end = { isConnected: true };
  const parent = { isConnected: true };
  const range = { startContainer: start, endContainer: end,
    cloneRange: () => ({
      get startContainer() { return live && !start.isConnected ? parent : start; },
      get endContainer() { return live && !end.isConnected ? parent : end; },
    }) };
  let ranges = multi ? [range, range] : [range];
  const selection = {
    anchorNode: end, anchorOffset: 12, focusNode: start, focusOffset: 2,
    get rangeCount() { return ranges.length; },
    getRangeAt: (index) => ranges[index],
    removeAllRanges: () => { ranges = []; },
    addRange: (value) => ranges.push(value),
  };
  if (directional) selection.setBaseAndExtent = (...points) => {
    [selection.anchorNode, selection.anchorOffset,
      selection.focusNode, selection.focusOffset] = points;
    ranges = [range];
  };
  return { selection, start, end, range };
}

test("fallback copy preserves a backwards reader selection and focus", async () => {
  const { selection, start, end } = readingSelection();
  const env = browser({ selection });
  try {
    await copyText("reply text");
    assert.equal(selection.rangeCount, 1);
    assert.deepEqual([selection.anchorNode, selection.anchorOffset,
      selection.focusNode, selection.focusOffset], [end, 12, start, 2]);
    assert.deepEqual(env.calls.at(-1), ["focus"]);
  } finally { env.restore(); }
});

test("fallback restores independent ranges on WebViews without directional selection", async () => {
  const { selection, range } = readingSelection({ multi: true, directional: false });
  const env = browser({ selection });
  try {
    await copyText("reply text");
    assert.equal(selection.rangeCount, 2);
    for (let index = 0; index < selection.rangeCount; index++) {
      assert.notEqual(selection.getRangeAt(index), range);
      assert.equal(selection.getRangeAt(index).startContainer, range.startContainer);
      assert.equal(selection.getRangeAt(index).endContainer, range.endContainer);
    }
  } finally { env.restore(); }
});

test("failed OS copying still restores the reader selection", async () => {
  const { selection } = readingSelection();
  const env = browser({ selection, onCopy() { throw new Error("OS copy failed"); } });
  try {
    await assert.rejects(copyText("reply text"), /OS copy failed/);
    assert.equal(selection.rangeCount, 1);
    assert.equal(selection.anchorOffset, 12);
    assert.deepEqual(env.calls.slice(-2), [["remove"], ["focus"]]);
  } finally { env.restore(); }
});

test("fallback skips a removed selection even when its live clone relocates to a connected parent", async () => {
  const { selection, start, end } = readingSelection({ live: true });
  const env = browser({ selection, onCopy() {
    start.isConnected = end.isConnected = env.focus.isConnected = false;
  } });
  try {
    await copyText("reply text");
    assert.equal(selection.rangeCount, 0);
    assert.deepEqual(env.calls.at(-1), ["remove"]);
  } finally { env.restore(); }
});

test("a delayed native refusal preserves the current draft caret and scroll", async () => {
  let rejectNative;
  const env = browser({ writeText: () => new Promise((_, reject) => {
    rejectNative = reject;
  }) });
  try {
    const result = copyText("reply text");
    const draft = { isConnected: true, selectionStart: 2, selectionEnd: 9,
      selectionDirection: "backward", scrollTop: 85, scrollLeft: 3,
      focus: () => { draft.scrollTop = 0; env.calls.push(["draft focus"]); },
      setSelectionRange: (...args) => env.calls.push(["draft selection", ...args]) };
    // The user moved away from the copy button while the native call waited.
    env.document.activeElement = draft;
    rejectNative(new Error("native clipboard denied"));
    await result;
    assert.deepEqual(env.calls.slice(-2), [["draft focus"],
      ["draft selection", 2, 9, "backward"]]);
    assert.equal(draft.scrollTop, 85);
    assert.equal(draft.scrollLeft, 3);
    assert.ok(!env.calls.some(([name]) => name === "focus"));
  } finally { env.restore(); }
});

test("code copying falls back and restores button focus without the Clipboard API", async () => {
  const env = browser();
  try {
    await copyText("int answer(void) { return 42; }");
    assert.deepEqual(env.calls, [["append"], ["select"],
      ["copy", "int answer(void) { return 42; }"], ["remove"], ["focus"]]);
    assert.equal(env.node.readOnly, true);
  } finally { env.restore(); }
});

test("a rejected native copy tries the fallback and reports a definite failure", async () => {
  const env = browser({ writeText: async () => { throw new Error("denied"); },
    copied: false });
  try {
    await assert.rejects(copyText("unavailable"), /clipboard unavailable/);
    assert.deepEqual(env.calls, [["append"], ["select"],
      ["copy", "unavailable"], ["remove"], ["focus"]]);
  } finally { env.restore(); }
});
