import assert from "node:assert/strict";
import test from "node:test";

import { copyText } from "../app/web/js/utils/clipboard.js";

function browser({ writeText, copied = true } = {}) {
  const oldNavigator = Object.getOwnPropertyDescriptor(globalThis, "navigator");
  const oldDocument = Object.getOwnPropertyDescriptor(globalThis, "document");
  const calls = [];
  const focus = { isConnected: true,
    focus: () => calls.push(["focus"]) };
  const node = { style: {}, value: "", readOnly: false,
    select: () => calls.push(["select"]),
    remove: () => calls.push(["remove"]) };
  Object.defineProperty(globalThis, "navigator", { configurable: true,
    value: { clipboard: writeText ? { writeText } : undefined } });
  Object.defineProperty(globalThis, "document", { configurable: true,
    value: { activeElement: focus,
      createElement: () => node,
      body: { append: () => calls.push(["append"]) },
      execCommand: (command) => {
        calls.push([command, node.value]);
        return copied;
      },
    } });
  return { calls, node, restore() {
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
