import assert from "node:assert/strict";
import test from "node:test";
import { createMessageEditDialog } from "../app/web/js/features/chat/message-edit-dialog.js";

function setup({ queuedClose = false } = {}) {
  const dialog = new EventTarget();
  const form = new EventTarget();
  const input = new EventTarget();
  const cancel = new EventTarget();
  const submit = { disabled: false };
  const error = { hidden: true, textContent: "", focus() { state.focus = "error"; } };
  const view = new EventTarget();
  const opener = { isConnected: true, focus() { state.focus = "opener"; } };
  const state = { focus: "", submits: 0, reports: 0, validity: "" };
  input.ownerDocument = { defaultView: view };
  input.setCustomValidity = (value) => { state.validity = value; };
  input.reportValidity = () => { state.reports += 1; };
  input.focus = () => { state.focus = "input"; };
  input.setSelectionRange = () => {};
  dialog.showModal = () => { dialog.open = true; };
  const closeEvents = [];
  dialog.close = () => {
    dialog.open = false;
    const dispatch = () => dialog.dispatchEvent(new Event("close"));
    if (queuedClose) closeEvents.push(dispatch);
    else dispatch();
  };
  form.reportValidity = () => !state.validity && (!input.required || Boolean(input.value));
  form.requestSubmit = () => {
    state.submits += 1;
    if (form.reportValidity()) form.dispatchEvent(new Event("submit", { cancelable: true }));
  };
  const editor = createMessageEditDialog({ dialog, form, input, cancel, submit, error });
  const key = (fields = {}, consumed = false) => {
    const event = new Event("keydown", { cancelable: true });
    for (const [name, value] of Object.entries({ key: "Enter", ...fields }))
      Object.defineProperty(event, name, { value });
    if (consumed) event.preventDefault();
    input.dispatchEvent(event);
    return event;
  };
  return { editor, dialog, input, cancel, submit, error, view, opener, state, key,
    flushClose() { while (closeEvents.length) closeEvents.shift()(); } };
}

test("a queued close from a cancelled edit cannot cancel a reopened image-only edit", async () => {
  const context = setup({ queuedClose: true });
  const first = context.editor.open("old", [], context.opener);
  context.cancel.dispatchEvent(new Event("click"));
  assert.equal(await first, null);
  const nextOpener = { isConnected: true, focus() { context.state.focus = "next"; } };
  let settled = false;
  const second = context.editor.open("new", ["a".repeat(32)], nextOpener);
  second.then(() => { settled = true; });
  context.flushClose();
  await Promise.resolve();
  assert.equal(settled, false);
  assert.equal(context.dialog.open, true);
  assert.equal(context.input.value, "new");
  assert.equal(context.input.required, false);
  assert.equal(context.state.focus, "input");
  context.input.value = "";
  context.key();
  assert.equal(await second, "");
  context.flushClose();
  assert.equal(context.state.focus, "next");
});

test("an externally closed current dialog still cancels and restores its own opener", async () => {
  const context = setup({ queuedClose: true });
  const result = context.editor.open("original", [], context.opener);
  context.dialog.close();
  context.flushClose();
  assert.equal(await result, null);
  assert.equal(context.dialog.open, false);
  assert.equal(context.state.focus, "opener");
});

test("plain Enter saves one edit and returns focus to its message action", async () => {
  const context = setup();
  const result = context.editor.open("original", [], context.opener);
  context.input.value = "  edited\nsecond line  ";
  assert.equal(context.key().defaultPrevented, true);
  context.key();
  assert.equal(await result, "edited\nsecond line");
  assert.equal(context.state.submits, 1);
  assert.equal(context.dialog.open, false);
  assert.equal(context.state.focus, "opener");
});

test("Shift+Enter and modified Enter keep the editor's native input behavior", async () => {
  const context = setup();
  const result = context.editor.open("original");
  for (const modifier of ["shiftKey", "ctrlKey", "metaKey", "altKey"])
    assert.equal(context.key({ [modifier]: true }).defaultPrevented, false);
  assert.equal(context.state.submits, 0);
  assert.equal(context.dialog.open, true);
  context.cancel.dispatchEvent(new Event("click"));
  assert.equal(await result, null);
});

test("IME candidates never resend, and input or window blur releases composition", async () => {
  for (const release of ["compositionend", "blur", "window-blur"]) {
    const context = setup();
    const result = context.editor.open("候选内容");
    assert.equal(context.key({ isComposing: true }).defaultPrevented, false);
    assert.equal(context.key({ keyCode: 229 }).defaultPrevented, false);
    context.input.dispatchEvent(new Event("compositionstart"));
    assert.equal(context.key().defaultPrevented, false);
    assert.equal(context.state.submits, 0);
    (release === "window-blur" ? context.view : context.input)
      .dispatchEvent(new Event(release === "window-blur" ? "blur" : release));
    assert.equal(context.key().defaultPrevented, true);
    assert.equal(await result, "候选内容");
    assert.equal(context.state.submits, 1);
  }
});

test("Enter uses the button's validation and permits an image-only edit", async () => {
  const context = setup();
  const result = context.editor.open("original");
  context.input.value = "   ";
  context.key();
  assert.equal(context.dialog.open, true);
  assert.notEqual(context.state.validity, "");
  assert.equal(context.state.reports, 1);
  context.input.value = "corrected";
  context.input.dispatchEvent(new Event("input"));
  context.key();
  assert.equal(await result, "corrected");
  const imageOnly = setup();
  const imageResult = imageOnly.editor.open("", ["a".repeat(32)]);
  assert.equal(imageOnly.input.required, false);
  imageOnly.key();
  assert.equal(await imageResult, "");
});

test("disabled, readonly, and previously consumed Enter cannot submit", async () => {
  const context = setup();
  const result = context.editor.open("original");
  context.input.disabled = true;
  assert.equal(context.key().defaultPrevented, false);
  context.input.disabled = false;
  context.input.readOnly = true;
  assert.equal(context.key().defaultPrevented, false);
  context.input.readOnly = false;
  context.key({}, true);
  assert.equal(context.state.submits, 0);
  context.cancel.dispatchEvent(new Event("click"));
  assert.equal(await result, null);
});

test("a delayed commit keeps the editor open and rejects duplicate submission and cancellation", async () => {
  const context = setup();
  let complete;
  let calls = 0;
  const result = context.editor.open("original", [], context.opener, { onCommit: async (text) => {
    calls += 1;
    assert.equal(text, "edited");
    await new Promise((resolve) => { complete = resolve; });
  } });
  context.input.value = "edited";
  context.key();
  context.key();
  context.cancel.dispatchEvent(new Event("click"));
  const escape = new Event("cancel", { cancelable: true });
  context.dialog.dispatchEvent(escape);
  assert.equal(escape.defaultPrevented, true);
  assert.equal(context.dialog.open, true);
  assert.equal(context.input.readOnly, true);
  assert.equal(context.cancel.disabled, true);
  assert.equal(context.submit.disabled, true);
  assert.equal(calls, 1);
  complete();
  assert.equal(await result, "edited");
  assert.equal(context.dialog.open, false);
  assert.equal(context.input.readOnly, false);
  assert.equal(context.cancel.disabled, false);
  assert.equal(context.submit.disabled, false);
});

test("a refused message edit preserves text and attachments for correction or cancellation", async () => {
  const context = setup();
  let calls = 0;
  let settled = false;
  const result = context.editor.open("original", ["a".repeat(32)], context.opener, { onCommit: async () => {
    calls += 1;
    if (calls === 1) throw Object.assign(new Error("refused"), { code: "session_message_changed" });
  } });
  result.then(() => { settled = true; });
  context.input.value = "my unsent edit";
  context.key();
  await new Promise((resolve) => setImmediate(resolve));
  assert.equal(settled, false);
  assert.equal(context.dialog.open, true);
  assert.equal(context.input.value, "my unsent edit");
  assert.equal(context.input.required, false);
  assert.equal(context.input.readOnly, false);
  assert.equal(context.error.hidden, false);
  assert.match(context.error.textContent, /原消息已在其他窗口/);
  assert.equal(context.state.focus, "error");
  context.input.value = "corrected";
  context.key();
  assert.equal(await result, "corrected");
  assert.equal(calls, 2);
});

test("a late commit response cannot close or change a newer editor", async () => {
  const context = setup();
  let complete;
  const first = context.editor.open("first", [], context.opener, { onCommit: () =>
    new Promise(resolve => { complete = resolve; }) });
  context.key();
  context.dialog.close();
  assert.equal(await first, null);
  assert.equal(context.input.readOnly, false);
  assert.equal(context.cancel.disabled, false);
  assert.equal(context.submit.disabled, false);
  let settled = false;
  const next = context.editor.open("next", ["b".repeat(32)], context.opener);
  next.then(() => { settled = true; });
  complete();
  await new Promise((resolve) => setImmediate(resolve));
  assert.equal(settled, false);
  assert.equal(context.dialog.open, true);
  assert.equal(context.input.value, "next");
  assert.equal(context.input.readOnly, false);
  context.cancel.dispatchEvent(new Event("click"));
  assert.equal(await next, null);
});

test("closing an editor whose opener was removed returns focus through its owner-checked fallback", async () => {
  const context = setup();
  let current = true;
  const prompt = { isConnected: true, focus() { context.state.focus = "prompt"; } };
  const options = { fallbackFocus: () => current ? prompt : null };
  const first = context.editor.open("old source", [], context.opener, options);
  context.opener.isConnected = false;
  context.cancel.dispatchEvent(new Event("click"));
  assert.equal(await first, null);
  assert.equal(context.state.focus, "prompt");
  const second = context.editor.open("different route", [], context.opener, options);
  current = false;
  context.cancel.dispatchEvent(new Event("click"));
  assert.equal(await second, null);
  assert.equal(context.state.focus, "input");
});
