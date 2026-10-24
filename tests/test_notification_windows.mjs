import assert from "node:assert/strict";
import test from "node:test";
import { readFileSync } from "node:fs";
import { TestElement, testDocument } from "./helpers/extension-dom.mjs";
import { createNoticePreferences } from "../app/web/js/features/notifications/preferences.js";

const flush = () => new Promise(resolve => setImmediate(resolve));
const runtime = "a".repeat(32), nextRuntime = "b".repeat(32);
const storage = () => { const values = new Map(); return {getItem:key=>values.get(key),setItem:(key,value)=>values.set(key,value)}; };
test("later lasts for the native run across reloads, resets on restart/revision, and is isolated by target", () => {
  const store = storage(), notice = { id:"message", revision:1 }, choices = createNoticePreferences(store);
  choices.record("local", runtime, notice, true);
  const reloaded = createNoticePreferences(store);
  reloaded.forRun("local", undefined);
  assert.equal(reloaded.forRun("local", runtime).message.hidden, 1, "Initial empty render must not discard the saved runtime");
  assert.equal(choices.forRun("remote", runtime).message, undefined);
  assert.equal(createNoticePreferences(store).forRun("local", nextRuntime).message, undefined);
  choices.record("local", runtime, {...notice,persistent:true}, true);
  assert.equal(choices.forRun("local", runtime).message.hidden, undefined);
  choices.record("local", runtime, {id:"__proto__",revision:2}, true);
  assert.equal(Object.getPrototypeOf(choices.forRun("local", runtime)), Object.prototype);
  const fallback = createNoticePreferences({getItem(){throw Error("denied");},setItem(){throw Error("denied");}});
  fallback.record("local", undefined, notice, true); assert.equal(fallback.forRun("local").message.hidden,1);
});

let serial=0;
async function fixture(t) {
  const previous = { document:globalThis.document, window:globalThis.window, sessionStorage:globalThis.sessionStorage };
  const doc=Object.assign(new EventTarget(),testDocument()),win=new EventTarget();
  Object.assign(globalThis,{document:doc,window:win,sessionStorage:storage()});win.innerWidth=1280;win.innerHeight=720;
  doc.createElementNS=(_,tag)=>new TestElement(tag);
  const host=new TestElement("div"),tools=new TestElement("div");doc.body.append(host,tools);
  doc.querySelectorAll=selector=>selector==="[data-notifications-open]"?[host]:doc.body.querySelectorAll(selector);
  doc.querySelector=selector=>selector==="[data-settings-toolpacks]"?tools:selector==="dialog[open]"?doc.body.querySelectorAll("dialog").find(d=>d.open):doc.body.querySelector(selector);
  let state={selected:null,connected:true}; const listeners=new Set(),calls=[];
  const model={distribution:{notification_session:runtime,platform:"windows-x86_64",build_id:30000142,notices:[
    {id:"admin",revision:1,title:"Admin notice",body:"<script>literal</script>\nSecond line",icon:"warning",persistent:true,action:"update"},
    ...Array.from({length:4},(_,i)=>({id:`extra-${i}`,revision:1,title:`Notice ${i}`,body:`Body ${i}`}))]},
    update:{enabled:true,available:true,status:"available",sha256:"hash",notes:"Update notes"},account:{state:"signed_out"}};
  const stubs={api:{get:async path=>({data:path==="/distribution"?model.distribution:path==="/update"?model.update:path==="/account"?model.account:{platform:"windows-x86_64",tasks:[]}})},
    targetState:()=>state,subscribeTarget:fn=>{listeners.add(fn);fn(state);return()=>listeners.delete(fn);},
    createExternalToolsPanel:()=>({render(){},reset(){},destroy(){}}),createToolPanel:()=>({render(){}})};
  globalThis.__noticeWindows=stubs;
  const url=new URL("../app/web/js/features/notifications/center.js",import.meta.url);
  let source=readFileSync(url,"utf8");
  for(const [line,replacement] of [
    ['import { api } from "../../api/client.js";',"const {api}=globalThis.__noticeWindows;"],
    ['import { targetState, subscribeTarget } from "../../api/target.js";',"const {targetState,subscribeTarget}=globalThis.__noticeWindows;"],
    ['import { createExternalToolsPanel } from "./external-tools.js";',"const {createExternalToolsPanel}=globalThis.__noticeWindows;"],
    ['import { createToolPanel } from "./tool-panel.js";',"const {createToolPanel}=globalThis.__noticeWindows;"]])source=source.replace(line,replacement);
  source=source.replace(/from "(\.[^"]+)"/g,(_,relative)=>`from ${JSON.stringify(new URL(relative,url).href)}`);
  const module=await import("data:text/javascript;base64,"+Buffer.from(source+`\n// ${++serial}`).toString("base64"));
  const center=module.createNotificationCenter({updatePanel:{open(){calls.push("update");}},openAccount(){calls.push("account");},navigation:{get:()=>({view:"workspace"}),subscribe:()=>()=>{},openSettings:page=>calls.push(page)}});
  t.after(()=>{center.destroy();Object.assign(globalThis,previous);delete globalThis.__noticeWindows;});await flush();
  return {doc,host,model,calls,center,stubs,switchTarget(selected){state={selected,connected:true};for(const fn of listeners)fn(state);}};
}
test("all indicators directly open their target, with no notification list or intermediate update page",async t=>{
  const env=await fixture(t),buttons=env.host.children;
  assert.equal(buttons.length,7);assert(!buttons.some(b=>b.dataset.noticeId==="all-notifications"));
  buttons.find(b=>b.dataset.noticeId==="app-update").click();assert.deepEqual(env.calls,["update"]);assert(!env.doc.body.querySelector("dialog"));
  buttons.find(b=>b.dataset.noticeId==="account-guide").click();assert.deepEqual(env.calls,["update","account"]);
  buttons.find(b=>b.dataset.noticeId==="admin").click();const dialog=env.doc.body.querySelector("dialog");
  assert.equal(dialog.id,"notice-content-dialog");assert(dialog.classList.contains("mdo-layer"));assert(dialog.textContent.includes("<script>literal</script>"));assert(!dialog.querySelector("script"));
  assert.equal(env.calls.length,2,"Server-controlled action must not redirect away from the notice content");
  assert.equal(dialog.querySelector(".mdo-layer-footer").children.length,2);
  dialog.querySelector(".mdo-layer-footer").firstChild.click();await flush();assert(!env.doc.body.querySelector("dialog"));
  await env.center.refresh();assert(!env.host.children.some(b=>b.dataset.noticeId==="admin"));
  env.model.distribution={...env.model.distribution,notification_session:nextRuntime};await env.center.refresh();
  assert(env.host.children.some(b=>b.dataset.noticeId==="admin"));assert(env.host.children.some(b=>b.dataset.noticeId==="app-update"));
});
test("closing a notice preserves its indicator; target/run changes dispose the window and invalidate old buttons",async t=>{
  const env=await fixture(t);const open=()=>env.host.children.find(b=>b.dataset.noticeId==="admin").click();
  open();env.doc.body.querySelector("dialog").querySelector(".mdo-layer-controls").lastChild.click();assert(env.host.children.some(b=>b.dataset.noticeId==="admin"));
  open();const old=env.doc.body.querySelector("dialog").querySelector(".mdo-layer-footer").firstChild;
  env.switchTarget({id:"remote",owner:"7"});await flush();assert(!env.doc.body.querySelector("dialog"));old.click();await env.center.refresh();
  assert(env.host.children.some(b=>b.dataset.noticeId==="admin"));
  open();env.model.distribution={...env.model.distribution,notification_session:nextRuntime};await env.center.refresh();assert(!env.doc.body.querySelector("dialog"));
});

test("late update policy from the previous target cannot block the new target or revive its window",async t=>{
  const env=await fixture(t),url=new URL("../app/web/js/features/update/update-panel.js",import.meta.url);
  let source=readFileSync(url,"utf8").replace('import { subscribeTarget, targetState } from "../../api/target.js";',"const {subscribeTarget,targetState}=globalThis.__noticeWindows;");
  source=source.replace(/from "(\.[^"]+)"/g,(_,relative)=>`from ${JSON.stringify(new URL(relative,url).href)}`);
  const module=await import("data:text/javascript;base64,"+Buffer.from(source+`\n// ${++serial}`).toString("base64"));
  const root=new TestElement("section");for(const key of ["status","notes"]){const n=new TestElement("p");n.setAttribute(`data-update-${key}`,"");root.append(n);}env.doc.body.append(root);
  let first=true,resolve;const old=new Promise(done=>{resolve=done;});
  const panel=module.createUpdatePanel({root,transport:{get:()=>first?(first=false,old):Promise.resolve({data:{enabled:true,status:"current",available:false,blocked:false}})}});
  try {
    panel.open();env.switchTarget({id:"remote",owner:"7"});assert(!env.doc.body.querySelector("dialog"));
    resolve({data:{enabled:true,status:"available",available:true,blocked:true}});await flush();await flush();
    assert(!env.doc.body.querySelector("dialog"));panel.open();const dialog=env.doc.body.querySelector("dialog");
    assert.equal(dialog.dataset.required,"false");assert(dialog.querySelector("[data-update-action=download]").hidden);
  } finally {panel.destroy();}
});
