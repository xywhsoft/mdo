import assert from "node:assert/strict";
import test from "node:test";
import { createMessageEditDialog } from "../app/web/js/features/chat/message-edit-dialog.js";

function setup() {
  const dialog = new EventTarget();
  const form = new EventTarget();
  const input = new EventTarget();
  const cancel = new EventTarget();
  const view = new EventTarget();
  const opener = { isConnected: true, focus() { state.focus = "opener"; } };
  const state = { focus: "", submits: 0, reports: 0, validity: "" };
  input.ownerDocument = { defaultView: view };
  input.setCustomValidity = (value) => { state.validity = value; };
  input.reportValidity = () => { state.reports += 1; };
  input.focus = () => { state.focus = "input"; };
  input.setSelectionRange = () => {};
  dialog.showModal = () => { dialog.open = true; };
  dialog.close = () => { dialog.open = false; dialog.dispatchEvent(new Event("close")); };
  form.reportValidity = () => !state.validity && (!input.required || Boolean(input.value));
  form.requestSubmit = () => {
    state.submits += 1;
    if (form.reportValidity()) form.dispatchEvent(new Event("submit", { cancelable: true }));
  };
  const editor = createMessageEditDialog({ dialog, form, input, cancel });
  const key = (fields = {}, consumed = false) => {
    const event = new Event("keydown", { cancelable: true });
    for (const [name, value] of Object.entries({ key: "Enter", ...fields }))
      Object.defineProperty(event, name, { value });
    if (consumed) event.preventDefault();
    input.dispatchEvent(event);
    return event;
  };
  return { editor, dialog, input, cancel, view, opener, state, key };
}

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
