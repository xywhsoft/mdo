import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { loadLocale } from "../app/web/js/i18n.js";
import { TestElement, testDocument, descendants } from "./helpers/extension-dom.mjs";
import { createDownloadRegistry } from "../app/web/js/state/downloads.js";
import { createDownloadCenter } from "../app/web/js/features/downloads/center.js";
const initialFetch = globalThis.fetch;
globalThis.fetch = async url => new Response(readFileSync(new URL(`../app/web${url}`, import.meta.url)), { status: 200 });
await loadLocale("en-US"); globalThis.fetch = initialFetch;
const flush = () => new Promise(r => setImmediate(r));

test("one global dialog combines source types and delegates cancellation, retry and update review", async t => {
  const previous = globalThis.document, entry = new TestElement("button"), doc = testDocument();
  for (const name of ["label", "count"]) { const child = new TestElement("span"); child.setAttribute(`data-downloads-${name}`, ""); entry.append(child); }
  doc.querySelector = name => name === "#open-downloads" ? entry : null; globalThis.document = doc;
  const registry = createDownloadRegistry(), calls = []; let updates = 0;
  registry.observe("/cli-tools", { tasks: [{ id: "active", name: "rg", action: "install", state: "downloading", done: 20, total: 100 }, { id: "retry", name: "curl", action: "install", state: "failed", error: "Checksum mismatch" }] });
  registry.observe("/update", { status: "ready", ready: true, build_id: 9 });
  let complete; const extension = registry.run("local", { id: 4, name: "My Skill" }, () => new Promise(r => { complete = r; }));
  const center = createDownloadCenter({ registry, updatePanel: { open: () => { updates++; } }, transport: { get: async () => ({ data: {} }), post: async (path, data) => { calls.push([path, data]); }, delete: async path => { calls.push([path]); } } });
  t.after(() => { center.destroy(); globalThis.document = previous; });
  center.open(); await flush();
  let dialog = doc.body.querySelector("dialog"); assert(dialog.open); assert.equal(doc.body.querySelectorAll("dialog").length, 1);
  assert(dialog.classList.contains("mdo-layer")); assert.equal(dialog.id,"downloads-dialog");
  assert.equal(dialog.querySelector(".mdo-layer-title").textContent,"Downloads");
  assert(dialog.querySelector(".mdo-layer-footer").textContent.includes("Clear finished"));
  center.open(); assert.equal(doc.body.querySelector("dialog"),dialog,"Repeated opening reuses the public window");
  assert(dialog.textContent.includes("My Skill") && dialog.textContent.includes("mdo") && dialog.textContent.includes("Checksum mismatch"));
  assert.equal(entry.querySelector("[data-downloads-count]").textContent, "2");
  const buttons = label => descendants(dialog).filter(n => n.tagName === "BUTTON" && n.textContent === label);
  buttons("Cancel")[0].click(); await flush(); assert.deepEqual(calls.at(-1), ["/cli-tools", { action: "cancel", id: "active" }]);
  buttons("Retry")[0].click(); await flush(); assert.deepEqual(calls.at(-1), ["/cli-tools", { action: "retry", id: "retry" }]);
  buttons("View update")[0].click(); assert.equal(updates, 1); assert(!dialog.open);
  complete(); await extension; center.open(); await flush(); dialog = doc.body.querySelector("dialog");
  const beforeClear=calls.length;buttons("Clear finished")[0].click(); await flush(); assert(!calls.slice(beforeClear).some(([p,b])=>p==="/cli-tools"&&b?.action==="clear"));
  assert(!registry.list("local").some(row => row.name === "My Skill"));
  assert(registry.list("local").some(row => row.state === "ready"));
});

test("clear finished releases native archive receipts and preserves failures and newly active tasks",async t=>{
 const previous=globalThis.document,doc=testDocument(),entry=new TestElement("button");
 for(const name of ["label","count"]){const span=new TestElement("span");span.setAttribute(`data-downloads-${name}`,"");entry.append(span);}doc.querySelector=name=>name==="#open-downloads"?entry:null;globalThis.document=doc;
 const registry=createDownloadRegistry(),calls=[],states=["complete","cancelled","failed","expired","ready","receiving"],ids=states.map((_,i)=>String.fromCharCode(65+i).repeat(32));
 registry.observe("/extension-tasks",states.map((state,i)=>({id:ids[i],name:`archive-${i}`,state,purpose:"file",bytes:2,received:2})));
 const center=createDownloadCenter({registry,transport:{post:async()=>assert.fail("no retired CLI clear"),get:async path=>{
  if(["/extension-tasks","/update","/distribution"].includes(path))return{data:{}};
  calls.push(["GET",path]);const i=ids.indexOf(path.split("/").at(-1));return{data:{state:i===3?"receiving":states[i],active:false}};
 },delete:async path=>{calls.push(["DELETE",path]);if(path.includes(ids[2]))throw Error("owned file is still locked");return{data:{}};}}});
 t.after(()=>{center.destroy();globalThis.document=previous;});center.open();await flush();const dialog=doc.body.querySelector("dialog");descendants(dialog).find(n=>n.tagName==="BUTTON"&&n.textContent==="Clear finished").click();for(let i=0;i<6;i++)await flush();
 assert(calls.some(([m,p])=>m==="DELETE"&&p===`/extension-tasks/${ids[0]}/forget`));assert(calls.some(([m,p])=>m==="DELETE"&&p===`/extension-tasks/${ids[1]}/forget`));
 assert(!calls.some(([m,p])=>m==="DELETE"&&p.includes(ids[3])));assert(!calls.some(([,p])=>p.includes(ids[4])||p.includes(ids[5])));
 assert(!registry.list("local").some(r=>r.taskId===ids[0]||r.taskId===ids[1]));assert(registry.list("local").some(r=>r.taskId===ids[2]));assert(registry.list("local").some(r=>r.taskId===ids[3]));assert(dialog.textContent.includes("owned file is still locked"));
});

test("closing the public window leaves transfers running and late actions cannot reopen it", async t => {
  const previous=globalThis.document,doc=testDocument(),entry=new TestElement("button");
  for(const name of ["label","count"]){const span=new TestElement("span");span.setAttribute(`data-downloads-${name}`,"");entry.append(span);}
  doc.querySelector=name=>name==="#open-downloads"?entry:null;globalThis.document=doc;
  const registry=createDownloadRegistry(),calls=[];let release;
  const queued=new Promise(resolve=>{release=resolve;});
  const task={id:"running",name:"ripgrep",action:"install",state:"downloading",done:10,total:100};
  registry.observe("/cli-tools",{tasks:[task]});
  const center=createDownloadCenter({registry,transport:{get:async()=>({data:{}}),post:async(path,body)=>{calls.push([path,body]);await queued;}}});
  t.after(()=>{center.destroy();globalThis.document=previous;});
  center.open();await flush();let dialog=doc.body.querySelector("dialog");
  const cancel=dialog.querySelector(".download-actions").querySelector("button");cancel.click();
  dialog.querySelector(".mdo-layer-controls").lastChild.click();assert(!doc.body.querySelector("dialog"));
  assert.equal(registry.list("local")[0].state,"downloading","Closing never cancels the transfer");
  registry.observe("/cli-tools",{tasks:[{...task,done:65}]});release();await flush();
  assert(!doc.body.querySelector("dialog"),"Late registry updates and write receipts cannot reopen the window");
  assert.deepEqual(calls,[["/cli-tools",{action:"cancel",id:"running"}]]);
  cancel.click();await flush();assert.equal(calls.length,1,"Detached controls cannot submit another cancellation");
  center.open();dialog=doc.body.querySelector("dialog");assert.equal(dialog.querySelector("progress").value,65);
  dialog.querySelector(".mdo-layer-body").scrollTop=80;
  const focused=dialog.querySelector(".download-actions").querySelector("button");focused.focus();
  registry.observe("/cli-tools",{tasks:[{...task,done:90}]});
  assert.equal(dialog.querySelector(".mdo-layer-body").scrollTop,80);assert.equal(document.activeElement.textContent,"Cancel");
  center.destroy();assert(!doc.body.querySelector("dialog"));center.open();assert(!doc.body.querySelector("dialog"));
});

test("same-device connection updates retain the window; switching device closes it and rejects old actions", async t => {
  const previous=globalThis.document,doc=testDocument(),entry=new TestElement("button");
  for(const name of ["label","count"]){const span=new TestElement("span");span.setAttribute(`data-downloads-${name}`,"");entry.append(span);}
  doc.querySelector=name=>name==="#open-downloads"?entry:null;globalThis.document=doc;
  let state={selected:{id:"first",mode:"control"},connected:true};const listeners=new Set(),calls=[];
  globalThis.__downloadTargets={targetState:()=>state,subscribeTarget:fn=>{listeners.add(fn);fn();return()=>listeners.delete(fn);}};
  const url=new URL("../app/web/js/features/downloads/center.js",import.meta.url);
  const source=readFileSync(url,"utf8").replace('import { targetState, subscribeTarget } from "../../api/target.js";',"const { targetState, subscribeTarget } = globalThis.__downloadTargets;")
    .replace(/from "(\.[^"]+)"/g,(_,path)=>`from ${JSON.stringify(new URL(path,url).href)}`);
  const {createDownloadCenter:make}=await import("data:text/javascript;base64,"+Buffer.from(source).toString("base64"));
  const registry=createDownloadRegistry();registry.observe("/cli-tools",{tasks:[{id:"old",name:"Old task",action:"install",state:"failed"}]},"first");
  registry.observe("/cli-tools",{tasks:[{id:"new",name:"New task",action:"install",state:"failed"}]},"second");
  const center=make({registry,transport:{get:async()=>({data:{}}),post:async(path,body)=>calls.push([path,body])}});
  t.after(()=>{center.destroy();globalThis.document=previous;delete globalThis.__downloadTargets;});
  center.open();const dialog=doc.body.querySelector("dialog"),old=dialog.querySelector(".download-actions").querySelector("button");
  state={...state,connected:false};for(const notify of listeners)notify();assert.equal(doc.body.querySelector("dialog"),dialog);
  assert(dialog.querySelector(".download-actions").querySelector("button").disabled);
  state={...state,connected:true};for(const notify of listeners)notify();assert(!dialog.querySelector(".download-actions").querySelector("button").disabled);
  state={selected:{id:"second",mode:"control"},connected:true};for(const notify of listeners)notify();
  assert(!doc.body.querySelector("dialog"));old.click();await flush();assert.equal(calls.length,0);
  center.open();assert(doc.body.querySelector("dialog").textContent.includes("New task"));assert(!doc.body.querySelector("dialog").textContent.includes("Old task"));
});
