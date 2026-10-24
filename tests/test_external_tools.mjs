import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
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
const flush=()=>new Promise(resolve=>setImmediate(resolve));

test("my submissions use one owned window, refresh asynchronous receipts and retain guarded withdrawal",async t=>{
 const saved=globalThis.document;globalThis.document=testDocument();
 let selected=null,signed=true,fail=false,hold;const calls=[];
 const item={id:7,name:"<sample submission>",version:"1.0.0",state:"pending",revision:3,reason:"Review notes"};
 let state={writable:true,platform:"windows-x86_64",guide:"accepted",items:[],tasks:[],store:{mine:[item]}};
 globalThis.__externalSubmissions={targetState:()=>({selected,connected:true}),api:{post:async(_,body)=>{
  calls.push(body);if(hold)await hold;if(fail)throw Error("Submission service unavailable");
  if(body.action==="withdraw")state={...state,store:{mine:[{...item,withdrawn:true,revision:4,state:"withdrawn"}]}};
  return {data:state};
 }}};
 const url=new URL("../app/web/js/features/notifications/external-tools.js",import.meta.url);
 const source=readFileSync(url,"utf8")
  .replace('import { api } from "../../api/client.js";',"const { api } = globalThis.__externalSubmissions;")
  .replace('import { targetState } from "../../api/target.js";',"const { targetState } = globalThis.__externalSubmissions;")
  .replace(/from "(\.[^"]+)"/g,(_,path)=>`from ${JSON.stringify(new URL(path,url).href)}`);
 const {createExternalToolsPanel:make}=await import("data:text/javascript;base64,"+Buffer.from(source).toString("base64"));
 const panel=make({refresh:async()=>{},openFull:async()=>{},signedIn:()=>signed}),body=new Element("div");
 t.after(()=>{panel.destroy();globalThis.document=saved;delete globalThis.__externalSubmissions;});
 const open=()=>all(body).find(node=>node.tagName==="BUTTON"&&node.textContent==="My submissions").click();
 const window=()=>document.body.querySelector(".mdo-layer");
 const footer=()=>window().querySelector(".mdo-layer-footer");
 panel.render(body,state);open();open();await flush();
 const first=window();assert(first.open);assert.equal(document.body.querySelectorAll(".mdo-layer").length,1);
 assert.equal(first.querySelector("h2").textContent,"My submissions");assert(first.textContent.includes(item.name)&&first.textContent.includes(item.reason));
 assert(!body.textContent.includes(item.name),"Submissions never append to the tool page");assert.equal(calls.length,1,"Repeated entry does not enqueue another read");
 const withdraw=first.querySelector(".cli-submission-row").querySelector("button");withdraw.click();withdraw.click();await flush();
 assert.deepEqual(calls.filter(row=>row.action==="withdraw"),[{action:"withdraw",store_id:7,revision:3,name:item.name}]);
 assert.equal(calls.at(-1).action,"mine");assert(first.querySelector(".cli-submission-row").querySelector("button").disabled);
 assert(first.textContent.includes("withdrawn"));assert.equal(window(),first,"Refresh retains the same window");
 fail=true;footer().firstChild.click();await flush();
 assert(first.textContent.includes("Submission service unavailable"));assert(first.textContent.includes(item.name),"A failed refresh retains cached submissions");
 fail=false;state={...state,store:{mine:[]}};footer().firstChild.click();await flush();assert(first.textContent.includes("No submissions yet"));
 // Closing during a delayed HTTP acknowledgement must not reopen the window.
 let release;hold=new Promise(resolve=>{release=resolve;});footer().firstChild.click();
 first.querySelector(".mdo-layer-controls").lastChild.click();assert(!window());assert.equal(document.activeElement,body.querySelector(".cli-create-menu").querySelector("summary"));
 const reads=calls.filter(row=>row.action==="mine").length;open();assert(window());
 assert.equal(calls.filter(row=>row.action==="mine").length,reads,"Reopening shares the pending read");
 fail=true;release();await flush();hold=null;
 assert(window().textContent.includes("Submission service unavailable"),"The new window sees failure of the shared read");
 fail=false;footer().firstChild.click();await flush();window().querySelector(".mdo-layer-controls").lastChild.click();assert(!window());
 // Queued native reads report failures through later task snapshots.
 state={...state,tasks:[{id:"read-1",action:"mine",state:"running"}]};panel.render(body,state);open();await flush();
 assert.equal(window().querySelector(".cli-submissions-list").getAttribute("aria-busy"),"true");
 state={...state,tasks:[{id:"read-1",action:"mine",state:"failed",error:"Queued read failed"}]};panel.render(body,state);
 assert(window().textContent.includes("Queued read failed"));assert(!footer().firstChild.disabled);
 state={...state,tasks:[],store:{mine:[item]}};panel.render(body,state);footer().firstChild.click();await flush();
 const stale=window().querySelector(".cli-submission-row").querySelector("button"),before=calls.length;
 selected={id:"other-device",mode:"control"};panel.render(body,{...state,store:{mine:[]}});assert(!window());stale.click();await flush();assert.equal(calls.length,before,"Old withdrawal controls cannot act on a new device");
 open();await flush();assert(window());signed=false;panel.render(body,state);assert(!window(),"Sign-out clears private submissions");
 signed=true;state={...state,tasks:[{id:"withdraw-1",action:"withdraw",state:"running",request:{store_id:7}}]};panel.render(body,state);open();await flush();
 assert(window().querySelector(".cli-submission-row").querySelector("button").disabled);
 state={...state,tasks:[{id:"withdraw-1",action:"withdraw",state:"failed",error:"Queued withdrawal failed"}]};panel.render(body,state);
 assert(window().textContent.includes("Queued withdrawal failed"));panel.reset();assert(!window());
 panel.render(body,state);open();await flush();panel.destroy();assert(!window());
});

test("external store page numbers retain earlier batches, deduplicate results and survive failed refresh", async () => {
 const saved=globalThis.document;globalThis.document=testDocument();
 let panel,selected=null,fail=false,serial=0;const calls=[];
 const tools=Array.from({length:35},(_,i)=>({id:i+1,tool_id:`tool.${i}`,name:`Tool ${i}`,version:"1.0.0",platform:"windows-x86_64"}));
 let state={writable:true,platform:"windows-x86_64",guide:"accepted",items:[],store:{items:[]},tasks:[]};
 globalThis.__externalPager={targetState:()=>({selected}),api:{post:async(path,body)=>{
  calls.push(body);if(fail)throw Error("catalog unavailable");
  state={...state,store:{request_id:`request-${++serial}`,query:body.query,before:body.before,items:body.before?tools.slice(19):tools.slice(0,20),next_before:body.before?0:16}};
  return {data:state};
 }}};
 try{
  const url=new URL("../app/web/js/features/notifications/external-tools.js",import.meta.url);
  const source=readFileSync(url,"utf8")
   .replace('import { api } from "../../api/client.js";',"const { api } = globalThis.__externalPager;")
   .replace('import { targetState } from "../../api/target.js";',"const { targetState } = globalThis.__externalPager;")
   .replace(/from "(\.[^"]+)"/g,(_,path)=>`from ${JSON.stringify(new URL(path,url).href)}`);
  const {createExternalToolsPanel:make}=await import("data:text/javascript;base64,"+Buffer.from(source).toString("base64"));
  panel=make({refresh:async()=>{},openFull:async()=>{},signedIn:()=>true});const body=new Element("div");panel.render(body,state);
  all(body).find(n=>n.attributes.role==="tab"&&n.textContent==="Tool store").click();await new Promise(r=>setImmediate(r));
  const pager=()=>body.querySelector(".eb-pagination");
  const page=n=>pager().querySelectorAll("button").find(b=>b.textContent===String(n));
  const next=()=>pager().querySelectorAll("button").find(b=>b.textContent==="下一页");
  assert.equal(pager().querySelector(".eb-page-status").textContent,"已加载 20 条");page(2).click();
  const search=body.querySelector("input");search.value="draft query";search.oninput();next().click();await new Promise(r=>setImmediate(r));
  assert.equal(calls.at(-1).before,16);assert.equal(calls.at(-1).query,"","unsubmitted search text cannot change a live cursor query");assert.equal(body.querySelector(".eb-name").textContent,"Tool 20");
  assert.equal(pager().querySelector(".eb-page-status").textContent,"共 35 条");page(1).click();assert.equal(body.querySelector(".eb-name").textContent,"Tool 0");
  page(4).click();assert.equal(body.querySelectorAll(".eb-row").length,5);assert(next().disabled);
  fail=true;all(body).find(n=>n.tagName==="BUTTON"&&n.textContent==="Refresh").click();await new Promise(r=>setImmediate(r));
  assert(body.textContent.includes("catalog unavailable"));assert.equal(body.querySelector(".eb-name").textContent,"Tool 30");
  selected={id:"another-device"};panel.render(body,{...state,store:{items:tools.slice(0,11)},items:[]});
  assert.equal(body.querySelectorAll(".eb-row").length,0,"changing devices clears the old local list");
 }finally{panel?.destroy();delete globalThis.__externalPager;globalThis.document=saved;}
});
test("local and store rows credit submitter usernames; details distinguish copyright holders and license",()=>{
 const saved=globalThis.document;globalThis.document=testDocument();
 try{
  const panel=createExternalToolsPanel({refresh:async()=>{},openFull:async()=>{},openTools:()=>{},signedIn:()=>true}),body=new Element("div");
  const tool={id:"community.fixture",name:"Fixture",source:"store",submitter:"<publisher>",author:"Publisher nickname",copyright:"Copyright 2026 Original Developer\nAdditional dependency owners",license:"MIT",version:"1.0.0",enabled:true,present:true};
  panel.render(body,{writable:true,platform:"windows-x86_64",guide:"accepted",items:[tool],store:{items:[{...tool,id:7,tool_id:tool.id,platform:"windows-x86_64"}]},tasks:[]});
  for(const view of ["Local tools","Tool store"]){
   all(body).find(n=>n.attributes.role==="tab"&&n.textContent===view).click();
   const row=body.querySelector(".eb-row");assert.equal(row.querySelector(".eb-name").textContent,tool.name);
   assert.equal(row.querySelector(".eb-byline").textContent,tool.submitter);assert(!row.textContent.includes(tool.author));
   row.querySelector(".eb-open").click();const detail=document.body.querySelector(".eb-detail");
   assert(detail.textContent.includes("Submitted by"));assert(detail.textContent.includes(tool.submitter));
   assert(detail.textContent.includes("Copyright notice"));assert(detail.textContent.includes(tool.copyright));assert(detail.textContent.includes("MIT"));
   assert.equal(body.querySelectorAll("script").length,0);
  }
  panel.render(body,{writable:true,platform:"windows-x86_64",guide:"accepted",items:[],store:{items:[{id:8,tool_id:"old",name:"Old",author:"Legacy publisher",version:"1.0.0",platform:"windows-x86_64"}]},tasks:[]});
  assert.equal(body.querySelector(".eb-byline").textContent,"Legacy publisher");body.querySelector(".eb-open").click();
  assert(document.body.querySelector(".eb-detail").textContent.includes("No copyright notice supplied"));
  panel.destroy();
 }finally{globalThis.document=saved;}
});
test("only user-added external tools offer edit, share and copy; store tools retain uninstall and toggle",()=>{
 const saved=globalThis.document;globalThis.document=testDocument();
 try{
  let signed=true;
  const panel=createExternalToolsPanel({refresh:async()=>{},openFull:async()=>{},openTools:()=>{},signedIn:()=>signed}),body=new Element("div");
  const tool={id:"fixture",name:"Fixture",source:"manual",path:"C:/fixture.exe",version:"1.0.0",enabled:true,present:true};
  const state={writable:true,platform:"windows-x86_64",guide:"accepted",items:[],tasks:[],store:{items:[]}};
  for(const source of ["manual","local","store","bundled","legacy","unknown",undefined])for(const managed of [false,true]){
   const custom=["manual","local"].includes(source);
   panel.render(body,{...state,items:[{...tool,source,managed_root:managed?"data/cli-tools/installed/fixture":undefined}]});
   const row=body.querySelector(".eb-row"),buttons=all(row).filter(n=>n.tagName==="BUTTON").map(n=>n.textContent);
   assert.equal(buttons.includes("Edit"),custom,`${source}: edit`);
   assert.equal(buttons.includes("Share to store"),custom,`${source}: share`);
   assert.equal(buttons.includes("Copy into mdo tools"),custom&&!managed,`${source}: copy`);
   assert.equal(buttons.includes("Uninstall"),source!=="bundled",`${source}: uninstall`);
   assert(all(row).some(n=>n.attributes.role==="switch"),`${source}: enable switch`);
   row.querySelector(".eb-open").click();
   assert(!document.body.querySelector(".eb-detail").textContent.includes("Share to store"));
  }
  signed=false;panel.render(body,{...state,items:[tool]});
  assert(all(body).find(n=>n.tagName==="BUTTON"&&n.textContent==="Share to store").disabled);
  panel.render(body,{...state,writable:false,items:[tool]});
  for(const label of ["Edit","Share to store","Copy into mdo tools"])
   assert(all(body).find(n=>n.tagName==="BUTTON"&&n.textContent===label).disabled);
  panel.render(body,{...state,platform:"android-arm64-v8a",items:[tool]});
  for(const label of ["Edit","Share to store","Copy into mdo tools"])
   assert(!all(body).some(n=>n.tagName==="BUTTON"&&n.textContent===label));
  panel.destroy();
 }finally{globalThis.document=saved;}
});
test("external manager has exactly two tabs, ordinary rows and a durable first-use prompt",()=>{
 const saved=globalThis.document;globalThis.document=testDocument();
 try{
  let main=true;const panel=createExternalToolsPanel({refresh:async()=>{},openFull:async()=>{},openTools:()=>{},signedIn:()=>false,isMain:()=>main}),body=new Element("div");
  const state={writable:true,guide:"new",platform:"windows-x86_64",edition:"desktop",items:[{id:"custom.one",name:"<script>literal</script>",path:"C:/cli/tool.exe",prompt:"use --example",version:"1.0.0",license:"MIT",enabled:true,present:true}],tasks:[],store:{items:[]}};
  panel.render(body,state);
  assert(!all(body).some(n=>n.tagName==="A"&&n.textContent==="Creator center"),"Creator navigation belongs to account/statistics settings");
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
  body.querySelector(".eb-open").click();assert(document.body.querySelector(".eb-detail").textContent.includes(tool.prompt),"model instructions remain available in the usage detail");
  all(body).find(n=>n.attributes.role==="tab"&&n.textContent==="Tool store").onclick();
  assert.deepEqual(body.querySelectorAll(".eb-description").map(n=>n.textContent),[tool.description]);
  assert(!body.textContent.includes(tool.prompt));panel.destroy();
 }finally{globalThis.document=saved;}
});
