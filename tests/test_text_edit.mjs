import assert from "node:assert/strict";
import test from "node:test";
import { replaceInputText } from "../app/web/js/utils/text-edit.js";

function editor(command) {
  const previousDocument = Object.getOwnPropertyDescriptor(globalThis, "document");
  const input = new EventTarget();
  Object.assign(input, { value: "keep @QA tail", disabled: false, readOnly: false,
    selectionStart: 8, selectionEnd: 8, selectionDirection: "none" });
  let nativeCalls = 0;
  let fallbacks = 0;
  let notifications = 0;
  input.focus = () => {};
  input.setSelectionRange = (start, end, direction = "none") => {
    input.selectionStart = start;
    input.selectionEnd = end;
    input.selectionDirection = direction;
  };
  input.setRangeText = (text, start, end) => {
    fallbacks += 1;
    input.value = input.value.slice(0, start) + text + input.value.slice(end);
    input.setSelectionRange(start + text.length, start + text.length);
  };
  input.addEventListener("input", () => { notifications += 1; });
  Object.defineProperty(globalThis, "document", { configurable: true, value: {
    execCommand: command ? (name, ui, text) => {
      nativeCalls += 1;
      assert.equal(name, "insertText");
      assert.equal(ui, false);
      return command(input, text);
    } : undefined,
  } });
  return { input, stats: () => ({ nativeCalls, fallbacks, notifications }), restore() {
    if (previousDocument) Object.defineProperty(globalThis, "document", previousDocument);
    else delete globalThis.document;
  } };
}

const completion = '@"notes/QA notes.txt"';
const expected = `keep ${completion} tail`;
function nativeEdit(input, text, notify = true) {
  const start = input.selectionStart;
  input.value = input.value.slice(0, start) + text + input.value.slice(input.selectionEnd);
  input.setSelectionRange(start + text.length, start + text.length);
  if (notify) input.dispatchEvent(new Event("input"));
}

test("native replacement preserves surrounding draft and emits only one input notification", () => {
  const env = editor((input, text) => { nativeEdit(input, text); return true; });
  try {
    assert.equal(replaceInputText(env.input, 5, 8, completion), true);
    assert.equal(env.input.value, expected);
    assert.deepEqual(env.stats(), { nativeCalls: 1, fallbacks: 0, notifications: 1 });
  } finally { env.restore(); }
});

test("a native command reporting failure after editing is never applied twice", () => {
  const env = editor((input, text) => { nativeEdit(input, text, false); return false; });
  try {
    replaceInputText(env.input, 5, 8, completion);
    assert.equal(env.input.value, expected);
    assert.deepEqual(env.stats(), { nativeCalls: 1, fallbacks: 0, notifications: 1 });
  } finally { env.restore(); }
});

test("unavailable and rejected native commands retain a single legacy edit and notification", () => {
  for (const command of [undefined, () => false, () => { throw new Error("unsupported"); }]) {
    const env = editor(command);
    try {
      replaceInputText(env.input, 5, 8, completion);
      assert.equal(env.input.value, expected);
      assert.deepEqual(env.stats(), { nativeCalls: command ? 1 : 0,
        fallbacks: 1, notifications: 1 });
    } finally { env.restore(); }
  }
});

test("cancelled beforeinput keeps the draft and original caret without bypassing its handler", () => {
  const env = editor((input) => {
    const event = new Event("beforeinput", { cancelable: true });
    input.dispatchEvent(event);
    return !event.defaultPrevented;
  });
  env.input.addEventListener("beforeinput", (event) => event.preventDefault());
  try {
    assert.equal(replaceInputText(env.input, 5, 8, completion), false);
    assert.equal(env.input.value, "keep @QA tail");
    assert.deepEqual([env.input.selectionStart, env.input.selectionEnd], [8, 8]);
    assert.deepEqual(env.stats(), { nativeCalls: 1, fallbacks: 0, notifications: 0 });
  } finally { env.restore(); }
});

test("an unchanged completion moves the caret without resetting the editor's history", () => {
  const env = editor(() => { throw new Error("must not edit"); });
  try {
    assert.equal(replaceInputText(env.input, 5, 8, "@QA"), true);
    assert.equal(env.input.value, "keep @QA tail");
    assert.deepEqual([env.input.selectionStart, env.input.selectionEnd], [8, 8]);
    assert.deepEqual(env.stats(), { nativeCalls: 0, fallbacks: 0, notifications: 0 });
  } finally { env.restore(); }
});

test("disabled or read-only drafts cannot be changed by an old completion option", () => {
  for (const field of ["disabled", "readOnly"]) {
    const env = editor(() => { throw new Error("must not edit"); });
    env.input[field] = true;
    try {
      assert.equal(replaceInputText(env.input, 5, 8, completion), false);
      assert.equal(env.input.value, "keep @QA tail");
      assert.deepEqual(env.stats(), { nativeCalls: 0, fallbacks: 0, notifications: 0 });
    } finally { env.restore(); }
  }
});
