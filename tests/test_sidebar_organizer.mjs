import assert from "node:assert/strict";
import test from "node:test";
import { readFileSync } from "node:fs";
import { TestElement, testDocument } from "./helpers/extension-dom.mjs";
import { createSidebarOrganizer } from "../app/web/js/features/sessions/sidebar-organizer.js";
import { emptySidebarLayout } from "../app/web/js/features/sessions/sidebar-layout.js";
import { loadLocale, t as text } from "../app/web/js/i18n.js";

const originalFetch=globalThis.fetch;
globalThis.fetch=async path=>Response.json(JSON.parse(readFileSync(new URL(`../app/web${path}`,import.meta.url),"utf8")));
await loadLocale("en-US");globalThis.fetch=originalFetch;
const flush=()=>new Promise(resolve=>setImmediate(resolve));
function fixture(t) {
  const previous={document:globalThis.document,window:globalThis.window};
  const doc=Object.assign(new EventTarget(),testDocument());globalThis.document=doc;globalThis.window=new EventTarget();
  const container=new TestElement("div"),opener=new TestElement("button");opener.setAttribute("data-sidebar-focus","add-group");container.append(opener);doc.body.append(container);opener.focus();
  const model={value:emptySidebarLayout(),writable:true,writes:0,failure:null,gate:null,afterSave(){}};
  const store={get:()=>model.value,writable:()=>model.writable,update:async change=>{
    model.writes++;if(model.failure)throw model.failure;const next=change(structuredClone(model.value));
    if(model.gate)await model.gate;model.value={...next,revision:model.value.revision+1};model.afterSave();return true;
  }};
  const organizer=createSidebarOrganizer({container,store,render(){}});
  t.after(()=>{organizer.destroy();Object.assign(globalThis,previous);});
  const dialog=()=>doc.body.querySelector("dialog");
  const submit=()=>dialog().querySelector("form").dispatchEvent(new Event("submit",{cancelable:true}));
  return {doc,container,opener,model,organizer,dialog,submit};
}
test("new group uses shared chrome and footer form association; save restores the replaced sidebar entry",async t=>{
  const e=fixture(t);e.organizer.groupForm();const dialog=e.dialog(),form=dialog.querySelector("form"),input=form.querySelector("input");
  assert(dialog.classList.contains("mdo-layer"));assert(dialog.classList.contains("sidebar-organizer-dialog"));assert.equal(dialog.style.width,"28rem");assert.equal(document.activeElement,input);
  assert.equal(dialog.querySelector(".mdo-layer-footer").lastChild.getAttribute("form"),form.id);
  assert(!form.contains(dialog.querySelector(".mdo-layer-footer")));
  input.value="  Work  ";let replacement;
  e.model.afterSave=()=>{e.opener.remove();replacement=new TestElement("button");replacement.setAttribute("data-sidebar-focus","add-group");e.container.append(replacement);};
  e.submit();await flush();assert.equal(e.model.writes,1);assert.equal(e.model.value.groups[0].name,"Work");assert.equal(e.model.value.view,"workspace");assert(!e.dialog());assert.equal(document.activeElement,replacement);
});
test("cancel writes nothing; invalid names, failed writes and stale revisions preserve the form",async t=>{
  const e=fixture(t);e.organizer.groupForm();e.dialog().querySelector(".mdo-layer-footer").firstChild.click();await flush();assert(!e.dialog());assert.equal(e.model.writes,0);
  e.organizer.groupForm();const input=e.dialog().querySelector("input");input.value=" ";e.submit();await flush();assert(e.dialog());assert.equal(e.model.writes,0);assert.equal(e.dialog().querySelector("[role=alert]").textContent,text("sidebar.nameInvalid"));
  input.value="Retained name";e.model.failure=Error("Fixture storage failure");e.submit();await flush();assert.equal(input.value,"Retained name");assert.equal(e.dialog().querySelector("[role=alert]").textContent,"Fixture storage failure");
  e.model.failure=null;e.model.value.revision++;e.submit();await flush();assert.equal(e.model.writes,1);assert.equal(e.dialog().querySelector("[role=alert]").textContent,text("sidebar.changed"));
});
test("saving rejects duplicate submit and Escape; owner disposal prevents a late save from closing the next window",async t=>{
  const e=fixture(t);let release;e.model.gate=new Promise(resolve=>{release=resolve;});e.organizer.groupForm();const old=e.dialog();old.querySelector("input").value="Saving";e.submit();e.submit();
  assert.equal(e.model.writes,1);assert(old.querySelector(".mdo-layer-controls").lastChild.disabled);
  old.dispatchEvent(new Event("cancel",{cancelable:true}));await flush();assert.equal(e.dialog(),old);
  e.organizer.close();e.organizer.groupForm();const next=e.dialog();release();await flush();assert.equal(e.dialog(),next);
  old.querySelector("form").dispatchEvent(new Event("submit",{cancelable:true}));await flush();assert.equal(e.model.writes,1);
});
test("shared organizer retains rename, move, delete and reset behavior",async t=>{
  const e=fixture(t),id="a".repeat(32);e.model.value.groups=[{id,name:"Work"}];
  e.organizer.groupForm(e.model.value.groups[0]);e.dialog().querySelector("input").value="Renamed";e.submit();await flush();assert.equal(e.model.value.groups[0].name,"Renamed");
  e.organizer.move("p:alpha");e.dialog().querySelector("select").value=id;e.submit();await flush();assert.equal(e.model.value.placements[0].group,id);
  e.organizer.remove(e.model.value.groups[0]);e.submit();await flush();assert.equal(e.model.value.groups.length,0);assert.equal(e.model.value.placements[0].group,"");
  e.organizer.reset();e.submit();await flush();assert.equal(e.model.value.placements.length,0);assert(!e.dialog());
});
