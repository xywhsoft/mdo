import test from "node:test";
import assert from "node:assert/strict";
import { updateActions, createUpdatePanel } from "../app/web/js/features/update/update-panel.js";

test("installation is offered only for an enabled, idle, verified package", () => {
  assert.equal(updateActions({ enabled: true, ready: false }).install, false);
  assert.equal(updateActions({ enabled: false, ready: true }).install, false);
  assert.equal(updateActions({ enabled: true, ready: true, busy: true }).install, false);
  assert.equal(updateActions({ enabled: true, ready: true, busy: false }).install, true);
});
test("download and cancellation follow the current operation", () => {
  assert.equal(updateActions({ enabled: true, status: "current" }).download, false);
  assert.equal(updateActions({ enabled: true, status: "available" }).download, true);
  assert.equal(updateActions({ enabled: true, status: "available", busy: true }).download, false);
  assert.equal(updateActions({ enabled: true, status: "downloading", busy: true }).cancel, true);
  assert.equal(updateActions({ enabled: true, status: "installing", busy: true }).cancel, false);
});

test("remote update actions never install locally and disable writes in view or offline mode", () => {
  const status = { enabled:true, ready:true, status:"available" };
  const context = { selected:{mode:"control"}, connected:true };
  assert.equal(updateActions(status,context).download,true);
  assert.equal(updateActions(status,context).install,false);
  assert.equal(updateActions(status,{...context,connected:false}).check,false);
  assert.equal(updateActions(status,{...context,runtimeChanged:true}).download,false);
  assert.equal(updateActions(status,{...context,selected:{mode:"view"}}).download,false);
});

test("the update panel reads the API envelope's data and restores available actions", async () => {
  const node = (dataset={}) => ({ dataset, textContent:"", hidden:false, disabled:false,
    addEventListener() {}, removeEventListener() {} });
  const statusNode=node(),notes=node(),buttons=["check","download","install","cancel"].map(action=>node({updateAction:action}));
  const root={querySelector(selector){return selector==='[data-update-status]'?statusNode:notes;},
    querySelectorAll(selector){return selector==='[data-update-action]'?buttons:[];},
    addEventListener(){},removeEventListener(){}};
  const noticeNodes=new Map(), notice={hidden:false,querySelector(selector){
    if(!noticeNodes.has(selector))noticeNodes.set(selector,node());return noticeNodes.get(selector);}};
  const panel=createUpdatePanel({root,notice,navigation:{},transport:{async get(){
    return {data:{enabled:true,status:"available",notes:"target release",sha256:"test-digest"}};}}});
  try {
    await panel.refresh();
    await new Promise(resolve=>setImmediate(resolve));
    assert.equal(buttons[0].disabled,false);
    assert.equal(buttons[1].disabled,false);assert.equal(buttons[1].hidden,false);
    assert.equal(notes.textContent,"target release");assert.equal(notice.hidden,false);
  } finally {panel.destroy();}
});
