import test from "node:test";
import assert from "node:assert/strict";
import { createExternalToolsPanel, externalToolAction } from "../app/web/js/features/notifications/external-tools.js";
import { buildNotices } from "../app/web/js/features/notifications/model.js";

test("store identities keep same-name manual tools independent and update disabled tools",()=>{
  const row={tool_id:"community.abc",version:"2.0.0"};
  assert.equal(externalToolAction(row,[{id:"custom.abc",name:"same",version:"2.0.0",present:true}]),"install");
  assert.equal(externalToolAction(row,[{id:row.tool_id,version:"1.0.0",present:true,enabled:false}]),"update");
  assert.equal(externalToolAction(row,[{id:row.tool_id,version:row.version,present:true,enabled:false}]),null);
  assert.equal(externalToolAction(row,[{id:row.tool_id,version:row.version,present:false}]),"install");
});

test("new registry does not recreate old missing-tool recommendation notifications",()=>{
  for(const guide of ["new","accepted","dismissed","deployed"]){
    const rows=buildNotices({distribution:{cli:{guide,items:[]},tools:[],toolpacks:[{id:"core",revision:10}],installed:{core:"old",core_revision:1}},locale:"en-US",copy:{}});
    assert(!rows.some(n=>n.id==="tools-guide"||n.id==="tools-update"));
  }
});

import { TestElement as Element, testDocument } from "./helpers/extension-dom.mjs";
const all=e=>[e,...e.children.flatMap(all)];
test("external manager has exactly two tabs, ordinary rows and a durable first-use prompt",()=>{
 const saved=globalThis.document;globalThis.document=testDocument();
 try{
  let main=true;const panel=createExternalToolsPanel({refresh:async()=>{},openFull:async()=>{},openTools:()=>{},signedIn:()=>false,isMain:()=>main}),body=new Element("div");
  const state={writable:true,guide:"new",platform:"windows-x86_64",edition:"desktop",items:[{id:"custom.one",name:"<script>literal</script>",path:"C:/cli/tool.exe",prompt:"use --example",version:"1.0.0",license:"MIT",enabled:true,present:true}],tasks:[],store:{items:[]}};
  panel.render(body,state);
  assert.equal(all(body).filter(n=>n.attributes.role==="tab").length,2);
  assert(all(body).some(n=>n.textContent==="<script>literal</script>"));
  const guide=document.body.children[0];assert.equal(guide.hidden,false);main=false;panel.render(body,state);assert.equal(guide.hidden,true);main=true;panel.render(body,state);assert.equal(guide.hidden,false);
  for(const decision of ["accepted","dismissed","deployed"]){panel.render(body,{...state,guide:decision,items:[]});assert.equal(guide.hidden,true);}
  panel.render(body,{...state,guide:"accepted",platform:"android-arm64-v8a",edition:"full"});
  assert(all(body).some(n=>n.textContent==="Install full edition"));
  assert(!all(body).some(n=>n.textContent==="Share to store"));
  panel.destroy();assert(guide.removed);
 }finally{globalThis.document=saved;}
});

test("Android APK failures remain visible and do not escape the tool manager",async()=>{
 const saved=globalThis.document;globalThis.document=testDocument();
 try{
  const panel=createExternalToolsPanel({refresh:async()=>{},openTools:()=>{},signedIn:()=>false,openFull:async()=>{throw Error("APK service unavailable");}}),body=new Element("div");
  panel.render(body,{writable:true,guide:"accepted",platform:"android-arm64-v8a",edition:"lite",items:[],tasks:[],store:{items:[]}});
  await all(body).find(n=>n.textContent==="Install full edition").onclick();
  assert(all(body).some(n=>n.textContent==="APK service unavailable"));panel.destroy();
 }finally{globalThis.document=saved;}
});

test("external filters and download badges use registered identities and keep active installs disabled",()=>{
 const saved=globalThis.document;globalThis.document=testDocument();
 try{
  const panel=createExternalToolsPanel({refresh:async()=>{},openFull:async()=>{},openTools:()=>{},signedIn:()=>true}),body=new Element("div");
  const items=[{id:"custom.one",name:"Manual tool",prompt:"Read files",version:"1.0.0",enabled:false,present:true}];
  const store={items:[{id:7,tool_id:"community.two",name:"Published tool",prompt:"Read files",platform:"windows-x86_64",version:"2.0.0"}]};
  panel.render(body,{writable:true,platform:"windows-x86_64",guide:"accepted",items,store,tasks:[{id:"task-one",state:"running",request:{store_id:7},done:5,total:10}]});
  const filter=all(body).find(n=>n.tagName==="SELECT");filter.value="enabled";filter.onchange();assert.equal(body.querySelectorAll(".eb-row").length,0);
  filter.value="disabled";filter.onchange();assert.equal(body.querySelectorAll(".eb-row").length,1);
  all(body).find(n=>n.attributes.role==="tab"&&n.textContent==="Tool store").onclick();
  const install=all(body).find(n=>n.tagName==="BUTTON"&&n.textContent==="Install");assert(install.disabled);
  assert(body.textContent.includes("50%"));assert.equal(body.querySelectorAll(".eb-row").length,1);
  panel.destroy();
 }finally{globalThis.document=saved;}
});

test("user descriptions drive local/store lists and search; model instructions stay separate",()=>{
 const saved=globalThis.document;globalThis.document=testDocument();
 try{
  const panel=createExternalToolsPanel({refresh:async()=>{},openFull:async()=>{},openTools:()=>{},signedIn:()=>true}),body=new Element("div");
  const tool={id:"custom.fixture",name:"Fixture",description:"Human-facing PDF conversion",prompt:"MODEL_PRIVATE_INSTRUCTIONS",version:"1.0.0",enabled:true,present:true};
  panel.render(body,{writable:true,platform:"windows-x86_64",guide:"accepted",items:[tool,{...tool,id:"legacy",name:"Legacy",description:undefined}],store:{items:[{...tool,id:9,tool_id:"community.fixture",platform:"windows-x86_64"}]},tasks:[]});
  assert.deepEqual(body.querySelectorAll(".eb-description").map(n=>n.textContent),[tool.description,"No description"]);
  assert(!body.textContent.includes(tool.prompt));
  const input=body.querySelector("input");input.value="MODEL_PRIVATE";input.oninput();assert.equal(body.querySelectorAll(".eb-row").length,0);
  input.value="PDF";input.oninput();assert.equal(body.querySelectorAll(".eb-row").length,1);
  body.querySelector(".eb-open").click();assert(body.querySelector(".eb-detail").textContent.includes(tool.prompt),"model instructions remain available in the usage detail");
  all(body).find(n=>n.attributes.role==="tab"&&n.textContent==="Tool store").onclick();
  assert.deepEqual(body.querySelectorAll(".eb-description").map(n=>n.textContent),[tool.description]);
  assert(!body.textContent.includes(tool.prompt));panel.destroy();
 }finally{globalThis.document=saved;}
});
