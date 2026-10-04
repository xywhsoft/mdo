import test from "node:test";
import assert from "node:assert/strict";
import { updateActions, updateAvailable, createUpdatePanel } from "../app/web/js/features/update/update-panel.js";

test("installation is offered only for an enabled, idle, verified package", () => {
  assert.equal(updateActions({ enabled: true, ready: false }).install, false);
  assert.equal(updateActions({ enabled: false, ready: true }).install, false);
  assert.equal(updateActions({ enabled: true, ready: true, busy: true }).install, false);
  assert.equal(updateActions({ enabled: true, ready: true, busy: false }).install, true);
});
test("panel consumes API envelopes, persists optional badge, and cannot dismiss mandatory updates", async () => {
  class Node extends EventTarget {
    constructor() { super(); this.dataset = {}; this.hidden = false; this.textContent = ""; this.classList = { contains: () => false }; }
    setAttribute() {}
    focus() {}
  }
  function surface(dialog = false) {
    const node = new Node(), nodes = new Map();
    for (const key of ["status", "notes", "title", "description", "later"])
      nodes.set(`[data-update-${key}]`, new Node());
    node.querySelector = key => nodes.get(key); node.contains = () => true;
    node.querySelectorAll = () => [];
    if (dialog) { node.open = false; node.showModal = () => { node.open = true; }; node.close = () => { node.open = false; }; }
    return node;
  }
  const previousDocument = globalThis.document;
  globalThis.document = Object.assign(new EventTarget(), {activeElement:null, visibilityState:"hidden"});
  const root = surface(), dialog = surface(true), badge = new Node();
  let data = { enabled:true, available:true, ready:false, required:false, blocked:false, status:"available", notes:"Test" };
  let reject = false;
  const panel = createUpdatePanel({ root, dialog, entries:[badge], transport:{ get:async () => {
    if (reject) throw Error("offline"); return {data};
  }}});
  const flush = () => new Promise(resolve => setImmediate(resolve));
  try {
    await flush();
    assert.equal(badge.hidden, false); assert.equal(dialog.open, false);
    badge.dispatchEvent(new Event("click")); assert.equal(dialog.open,true);
    dialog.querySelector("[data-update-later]").dispatchEvent(new Event("click"));
    assert.equal(dialog.open,false); assert.equal(badge.hidden,false);
    data = { ...data, required:true, blocked:true };
    await panel.refresh();
    assert.equal(dialog.open,true); assert.equal(dialog.querySelector("[data-update-later]").hidden,true);
    const escape = new Event("cancel",{cancelable:true}); dialog.dispatchEvent(escape);
    assert.equal(escape.defaultPrevented,true); assert.equal(dialog.open,true);
    const keyboardEscape = new Event("keydown", {cancelable:true});
    Object.defineProperty(keyboardEscape,"key",{value:"Escape"});
    document.dispatchEvent(keyboardEscape); assert.equal(keyboardEscape.defaultPrevented,true);
    dialog.querySelector("[data-update-later]").dispatchEvent(new Event("click"));
    assert.equal(dialog.open,true);
    reject = true; await panel.refresh(); assert.equal(dialog.open,true);
    reject = false; data = { ...data, required:false, blocked:false };
    await panel.refresh(); assert.equal(dialog.open,false); assert.equal(badge.hidden,false);
    data = { ...data, available:false, status:"current" };
    await panel.refresh(); assert.equal(badge.hidden,true);
  } finally { panel.destroy(); globalThis.document = previousDocument; }
});
test("transient operation errors preserve the required update actions and badge", () => {
  const status = { enabled: true, available: true, required: true, blocked: true, status: "error", busy: false };
  assert.equal(updateAvailable(status), true);
  assert.equal(updateActions(status).download, true);
  assert.equal(updateActions(status).exit, true);
  assert.equal(updateActions({ ...status, busy: true }).exit, false);
  assert.equal(updateActions({ ...status, ready: true }).download, false);
  assert.equal(updateActions({ ...status, ready: true }).install, true);
  assert.equal(updateAvailable({ ...status, available: false }), false);
  assert.equal(updateActions({ ...status, available: false }).download, false);
});
test("download and cancellation follow the current operation", () => {
  assert.equal(updateActions({ enabled: true, status: "current" }).download, false);
  assert.equal(updateActions({ enabled: true, status: "available" }).download, true);
  assert.equal(updateActions({ enabled: true, status: "available", busy: true }).download, false);
  assert.equal(updateActions({ enabled: true, status: "downloading", busy: true }).cancel, true);
  assert.equal(updateActions({ enabled: true, status: "installing", busy: true }).cancel, false);
});
