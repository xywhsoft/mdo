import assert from "node:assert/strict";
import test from "node:test";
import { readFileSync } from "node:fs";
import { TestElement, testDocument, descendants } from "./helpers/extension-dom.mjs";

const flush=()=>new Promise(resolve=>setImmediate(resolve));
const button=(root,text)=>descendants(root).find(n=>n.tagName==="BUTTON"&&n.textContent===text);
const submit=form=>form.dispatchEvent(new Event("submit",{cancelable:true}));
function environment(t) {
  const old={document:globalThis.document,window:globalThis.window};
  globalThis.document=Object.assign(new EventTarget(),testDocument());globalThis.window=new EventTarget();window.confirm=()=>true;
  return ()=>Object.assign(globalThis,old);
}

test("custom external tool forms use shared fixed actions, retain failed input and reject duplicate/late saves",async t=>{
  const restore=environment(t);let target=null,gate,fail=false;const calls=[];
  let state={writable:true,platform:"windows-x86_64",revision:8,guide:"accepted",items:[],tasks:[],store:{items:[]}};
  globalThis.__authoringExternal={targetState:()=>({selected:target,connected:true}),api:{post:async(_,body)=>{
    calls.push(body);if(gate)await gate;if(fail)throw Error("Fixture save failed");return {data:state};
  }}};
  const url=new URL("../app/web/js/features/notifications/external-tools.js",import.meta.url);
  const source=readFileSync(url,"utf8")
    .replace('import { api } from "../../api/client.js";',"const {api}=globalThis.__authoringExternal;")
    .replace('import { targetState } from "../../api/target.js";',"const {targetState}=globalThis.__authoringExternal;")
    .replace(/from "(\.[^"]+)"/g,(_,path)=>`from ${JSON.stringify(new URL(path,url).href)}`);
  const {createExternalToolsPanel}=await import("data:text/javascript;base64,"+Buffer.from(source).toString("base64"));
  const panel=createExternalToolsPanel({refresh:async()=>{},signedIn:()=>true}),root=new TestElement("div");document.body.append(root);
  t.after(()=>{panel.destroy();delete globalThis.__authoringExternal;restore();});
  const open=()=>button(root,"Add custom tool").click(),dialog=()=>document.body.querySelector("dialog");
  panel.render(root,state);open();const first=dialog(),form=first.querySelector("form"),footer=first.querySelector("footer");
  assert(first.classList.contains("mdo-layer"));assert(!form.contains(footer));assert.equal(footer.firstChild.getAttribute("form"),form.id);
  for(const input of form.elements)if(input.name)input.value=({name:"My CLI",path:"D:/tools/cli.exe",description:"Human description",prompt:"Model instructions"})[input.name]||input.value;
  fail=true;submit(form);await flush();assert.equal(dialog(),first);assert.equal(form.querySelector("input").value,"My CLI");assert(first.textContent.includes("Fixture save failed"));
  fail=false;let release;gate=new Promise(resolve=>{release=resolve;});submit(form);submit(form);await flush();assert.equal(calls.length,2);
  first.dispatchEvent(new Event("cancel",{cancelable:true}));await flush();assert.equal(dialog(),first);assert(footer.firstChild.disabled);
  // Forced owner teardown can happen during a write. A late response cannot close a replacement form.
  panel.reset();target={id:"next",mode:"control"};panel.render(root,state);open();const replacement=dialog();release();await flush();assert.equal(dialog(),replacement);
  assert.equal(calls[1].revision,8);assert.equal(calls[1].tool.description,"Human description");assert.equal(calls[1].tool.prompt,"Model instructions");
  submit(form);await flush();assert.equal(calls.length,2);
  replacement.querySelector("footer").lastChild.click();await flush();assert(!dialog());
  target=null;gate=null;panel.reset();panel.render(root,state);open();submit(dialog().querySelector("form"));await flush();assert(!dialog(),"Successful save closes only its form");
});

test("Skill and C tool editors keep validation and save recovery inside shared body with bottom actions",async t=>{
  const restore=environment(t);const roots=new Map(["skills","tools","commands"].map(kind=>[kind,new TestElement("div")]));
  for(const root of roots.values())document.body.append(root);
  document.querySelector=selector=>roots.get(selector.match(/data-extension-manager="([^"]+)"/)?.[1])||null;
  let fail=true,gate;const writes=[];
  t.mock.method(globalThis,"fetch",async(url,options)=>{
    if(options.method==="PUT") { writes.push({url,body:JSON.parse(options.body)});if(gate)await gate;return fail?Response.json({ok:false,error:{message:"Fixture write failed"}},{status:403}):Response.json({ok:true,data:{}}); }
    return Response.json({ok:true,data:{items:[]}});
  });
  const {createExtensionPanels}=await import("../app/web/js/features/settings/extension-panels.js");
  const panel=createExtensionPanels();t.after(()=>{panel.destroy();restore();});
  for(const kind of ["skills","tools","commands"]) {
    panel.selectSection(kind);await flush();button(roots.get(kind),"新建").click();const dialog=document.body.querySelector("dialog");
    assert(dialog.classList.contains("mdo-layer"));assert(dialog.classList.contains("extension-authoring"));
    const body=dialog.querySelector(".mdo-layer-body"),footer=dialog.querySelector("footer");
    assert(!body.contains(footer));assert.deepEqual(footer.querySelectorAll("button").map(n=>n.textContent),["保存","取消"]);
    const id=body.querySelector("input");id.value=`fixture-${kind}`;
    if(kind==="skills")for(const input of body.querySelector("form").elements)if(input.name==="name"||input.name==="description")input.value="Fixture skill";
    fail=true;footer.firstChild.click();await flush();assert(dialog.textContent.includes("Fixture write failed"));assert.equal(id.value,`fixture-${kind}`);
    fail=false;let release;gate=new Promise(resolve=>{release=resolve;});footer.firstChild.click();footer.firstChild.click();await flush();
    const before=writes.length;dialog.dispatchEvent(new Event("cancel",{cancelable:true}));await flush();assert(dialog.open);assert(footer.firstChild.disabled);
    release();gate=null;await flush();assert(!document.body.querySelector("dialog"));assert.equal(writes.length,before);
    assert.equal(writes.at(-1).url,`/api/v1/extensions/${kind}/fixture-${kind}`);const payload=writes.at(-1).body;assert.equal(payload.manifest.kind,kind);assert.equal(payload.manifest.alias,`fixture-${kind}`);assert.equal(payload.expected_revision,"");assert(payload.edits[0].text.includes(kind==="tools"?"mdo/module.h":kind==="commands"?"mdo/command.h":"Fixture skill"));assert(!Object.hasOwn(payload,"content"));assert(!Object.hasOwn(payload,"files"));
  }
});
