import test from "node:test";
import assert from "node:assert/strict";
import { updateActions, updateAvailable, createUpdatePanel } from "../app/web/js/features/update/update-panel.js";
import { TestElement, testDocument } from "./helpers/extension-dom.mjs";

test("installation is offered only for an enabled, idle, verified package", () => {
  assert.equal(updateActions({ enabled: true, ready: false }).install, false);
  assert.equal(updateActions({ enabled: false, ready: true }).install, false);
  assert.equal(updateActions({ enabled: true, ready: true, busy: true }).install, false);
  assert.equal(updateActions({ enabled: true, ready: true, busy: false }).install, true);
});
test("shared update window has no snooze, retains the badge on close, and enforces mandatory updates", async () => {
  const previous = { document: globalThis.document, window: globalThis.window };
  const doc = Object.assign(new EventTarget(), testDocument(), { visibilityState: "hidden" });
  globalThis.document = doc; globalThis.window = new EventTarget();
  const root = new TestElement("section"), badge = new TestElement("button");
  for (const key of ["status", "notes"]) { const node = new TestElement("p"); node.setAttribute(`data-update-${key}`, ""); root.append(node); }
  doc.body.append(root, badge);
  const dialog = () => doc.body.querySelector("dialog");
  let data = { enabled:true, available:true, ready:false, required:false, blocked:false, status:"available", notes:"Test" };
  let reject = false;
  const panel = createUpdatePanel({ root, entries:[badge], transport:{ get:async () => {
    if (reject) throw Error("offline"); return {data};
  }}});
  const flush = () => new Promise(resolve => setImmediate(resolve));
  try {
    await flush();
    assert.equal(badge.hidden, false); assert.equal(dialog(), null);
    badge.click(); assert(dialog().classList.contains("mdo-layer"));
    assert.equal(dialog().querySelector("[data-update-later]"), null);
    dialog().querySelector(".mdo-layer-controls").lastChild.click(); await flush();
    assert.equal(dialog(), null); assert.equal(badge.hidden,false);
    data = { ...data, required:true, blocked:true };
    await panel.refresh();
    assert(dialog().open); assert(dialog().querySelector(".mdo-layer-controls").lastChild.hidden);
    const escape = new Event("cancel",{cancelable:true}); dialog().dispatchEvent(escape); await flush();
    assert.equal(escape.defaultPrevented,true); assert(dialog().open);
    const keyboardEscape = new Event("keydown", {cancelable:true});
    Object.defineProperty(keyboardEscape,"key",{value:"Escape"});
    document.dispatchEvent(keyboardEscape); assert.equal(keyboardEscape.defaultPrevented,true);
    dialog().querySelector(".mdo-layer-controls").lastChild.click(); await flush(); assert(dialog().open);
    reject = true; await panel.refresh(); assert(dialog().open);
    reject = false; data = { ...data, required:false, blocked:false };
    await panel.refresh(); await flush(); assert.equal(dialog(), null); assert.equal(badge.hidden,false);
    data = { ...data, available:false, status:"current" };
    await panel.refresh(); assert.equal(badge.hidden,true);
  } finally { panel.destroy(); Object.assign(globalThis, previous); }
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

test("remote updates never install or exit the physical controller", () => {
  const ready = { enabled:true, ready:true, available:true, blocked:true, status:"ready" };
  const target = { selected:{id:"target", mode:"control"}, connected:true, runtimeChanged:false };
  assert.equal(updateActions(ready,target).check,true);
  assert.equal(updateActions(ready,target).install,false);
  assert.equal(updateActions(ready,target).exit,false);
  assert.equal(updateActions({...ready, ready:false},target).download,true);
  for (const context of [
    {...target, connected:false},
    {...target, runtimeChanged:true},
    {...target, selected:{id:"target",mode:"view"}},
  ]) {
    assert.deepEqual(updateActions(ready,context),{check:false,download:false,install:false,cancel:false,exit:false});
    assert.equal(updateActions({enabled:true,busy:true,status:"downloading"},context).cancel,false);
  }
});

test("closing update details leaves a download running; explicit cancel and stale controls stay distinct", async () => {
  const previous={document:globalThis.document,window:globalThis.window};
  const doc=Object.assign(new EventTarget(),testDocument());globalThis.document=doc;globalThis.window=new EventTarget();
  const root=new TestElement("section");for(const key of ["status","notes"]){const n=new TestElement("p");n.setAttribute(`data-update-${key}`,"");root.append(n);}doc.body.append(root);
  const calls=[];let data={enabled:true,available:true,status:"available",busy:false,ready:false};
  const panel=createUpdatePanel({root,transport:{get:async()=>({data}),post:async path=>{calls.push(path);data={...data,status:"downloading",busy:true};},delete:async path=>{calls.push(path);data={...data,status:"available",busy:false};}}});
  const flush=()=>new Promise(resolve=>setImmediate(resolve));
  try {
    await flush();panel.open();const old=doc.body.querySelector("dialog");
    old.querySelector("[data-update-action=download]").click();await flush();assert.deepEqual(calls,["/update/download"]);
    old.querySelector(".mdo-layer-controls").lastChild.click();await flush();assert(!doc.body.querySelector("dialog"));assert(data.busy);
    old.querySelector("[data-update-action=check]").click();await flush();assert.equal(calls.length,1);
    panel.open();doc.body.querySelector("dialog").querySelector("[data-update-action=cancel]").click();await flush();assert.deepEqual(calls,["/update/download","/update/download"]);assert(!data.busy);
  } finally {panel.destroy();Object.assign(globalThis,previous);}
});
