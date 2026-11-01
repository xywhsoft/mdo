import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { TestElement,testDocument,descendants } from "./helpers/extension-dom.mjs";
import { extensionInstaller } from "../app/web/js/state/extension-installs.js";
import { downloadRegistry } from "../app/web/js/state/downloads.js";
const flush=async()=>{for(let i=0;i<10;i++)await new Promise(r=>setImmediate(r));},button=(root,name)=>descendants(root).find(n=>n.tagName==="BUTTON"&&n.textContent===name);
async function load(){const url=new URL("../app/web/js/features/notifications/external-directory-tools.js",import.meta.url);return import(url.href+`?qa=${Math.random()}`);}
function env(t){const before={document:globalThis.document,window:globalThis.window,confirm:globalThis.confirm};globalThis.document=testDocument();globalThis.window=new EventTarget();globalThis.confirm=()=>true;t.after(()=>Object.assign(globalThis,before));}
test("external store uses work/version identity and shares the numbered, non-nested paging component",async t=>{
 env(t);const calls=[],rows=Array.from({length:35},(_,i)=>({kind:"external-tools",xid:`work-${i}`,version_id:`version-${i}`,name:`Tool ${i}`,owner:0}));
 t.mock.method(globalThis,"fetch",async(url,options)=>{const data=options.method==="POST"?JSON.parse(options.body):null;if(data){calls.push(data);assert.equal(data.action,"catalog");return Response.json({ok:true,data:{items:rows.slice(data.offset,data.offset+20),total:35}});}return Response.json({ok:true,data:{}});});
 const {createExternalToolsPanel,externalToolAction}=await load();assert.equal(externalToolAction(rows[0],[{id:"custom",installation:{version_id:rows[0].version_id}}]),"install");assert.equal(externalToolAction(rows[0],[{id:rows[0].xid,enabled:false,installation:{version_id:"older"}}]),"update");assert.equal(externalToolAction(rows[0],[{id:rows[0].xid,enabled:false,installation:{version_id:rows[0].version_id}}]),null);
 const panel=createExternalToolsPanel(),root=new TestElement("div");document.body.append(root);t.after(()=>panel.destroy());panel.render(root,{platform:"windows-x86_64",guide:"accepted",items:[]});button(root,"工具商店").click();await flush();
 const pager=()=>root.querySelector(".eb-pagination"),next=()=>button(pager(),"下一页");next().click();next().click();await flush();assert.equal(calls.at(-1).offset,20);assert.equal(root.querySelector(".eb-name").textContent,"Tool 20");button(pager(),"上一页").click();await flush();assert.equal(root.querySelector(".eb-name").textContent,"Tool 10");assert.equal(root.querySelectorAll(".cli-tasks").length,0);assert(!descendants(root).some(n=>n.textContent==="创作中心"));
});
test("official recommendation asks once then queues independent tools and enables reused installed tools",async t=>{
 env(t);const calls=[],jobs=[],aliases=["busybox","curl","jq","openssh","aria2","ripgrep","7zip"],rows=aliases.map((alias,i)=>({id:`external-tool:${alias}`,kind:"external-tools",xid:`work-${i}`,version_id:`version-${i}`,alias,owner:0}));let guide="new";
 const snapshot=()=>({platform:"windows-x86_64",writable:true,guide,items:[{id:rows[1].id,directory_id:rows[1].xid,name:"curl",enabled:false,source:"store",valid:true,installation:{version_id:rows[1].version_id}}]});
 t.mock.method(extensionInstaller,"enqueue",async(plan,meta)=>{jobs.push({plan,meta});return {state:"queued"};});
 t.mock.method(globalThis,"fetch",async(url,options)=>{const body=options.method==="POST"?JSON.parse(options.body):null;if(body){calls.push({url,body});if(url==="/api/v1/cli-tools"){guide="accepted";return Response.json({ok:true,data:snapshot()});}if(body.action==="catalog"){assert.equal(body.limit,100);return Response.json({ok:true,data:{items:rows,total:7}});}if(body.action==="install_plan")return Response.json({ok:true,data:{id:body.version_id,revision:"rev",ready:true,items:[{kind:"external-tools",name:body.version_id,operation:"install",version_id:body.version_id}]}});return Response.json({ok:true,data:{}});}return Response.json({ok:true,data:snapshot()});});
 const panel=(await load()).createExternalToolsPanel(),root=document.createElement("div");t.after(()=>panel.destroy());panel.render(root,snapshot());const guideNode=document.body.querySelector(".cli-first-use");assert(!guideNode.hidden);button(guideNode,"安装推荐工具").click();await flush();
 assert.equal(calls.filter(c=>c.body.action==="install_plan").length,6);assert.equal(jobs.length,0);const popup=document.body.querySelector("dialog");assert(popup.textContent.includes("信任"));assert(!popup.querySelector('input[type="checkbox"]'));button(popup,"安装").click();await flush();
 assert.equal(jobs.length,6);assert.equal(new Set(jobs.map(j=>j.meta.versionId)).size,6);assert(jobs.every(j=>j.meta.kind==="external-tools"));assert(calls.some(c=>c.url===`/api/v1/extensions/external-tools/${rows[1].xid}/enabled`&&c.body.enabled));assert(guideNode.hidden);assert(!JSON.stringify(calls).includes("base64"));assert(!calls.some(c=>c.body.action==="download"||c.body.action==="apply_plan"));
});

test("dismissing recommended-tool trust never installs or enables and discards inspected plans",async t=>{
 env(t);const calls=[],rows=["busybox","curl","jq","openssh","aria2","ripgrep","7zip"].map((alias,i)=>({kind:"external-tools",xid:`work-${i}`,version_id:`version-${i}`,alias,owner:0}));
 t.mock.method(extensionInstaller,"enqueue",async()=>assert.fail("Unapproved installation"));
 t.mock.method(globalThis,"fetch",async(url,options)=>{const body=JSON.parse(options.body);calls.push(body);return Response.json({ok:true,data:body.action==="catalog"?{items:rows}:body.action==="install_plan"?{id:body.version_id,revision:"rev",ready:true,items:[]}: {}});});
 const panel=(await load()).createExternalToolsPanel(),root=document.createElement("div");t.after(()=>panel.destroy());panel.render(root,{platform:"windows-x86_64",guide:"new",items:[]});button(document.body.querySelector(".cli-first-use"),"安装推荐工具").click();await flush();button(document.body.querySelector("dialog"),"取消").click();await flush();assert.equal(calls.filter(c=>c.action==="discard_plan").length,7);assert(!calls.some(c=>c.action==="accept"));assert(!document.body.querySelector("dialog"));
});

test("Android offers APK installation and cannot dynamically add external programs",async t=>{
 env(t);let full=0,requests=0;const snapshot={platform:"android-arm64-v8a",guide:"accepted",items:[]};t.mock.method(globalThis,"fetch",async url=>{requests++;assert.equal(url,"/api/v1/cli-tools");return Response.json({ok:true,data:snapshot});});const panel=(await load()).createExternalToolsPanel({openFull:async()=>full++}),root=document.createElement("div");t.after(()=>panel.destroy());panel.render(root,snapshot);assert(!button(root,"添加自定义工具"));button(root,"一键安装推荐工具").click();await flush();assert.equal(full,1);assert.equal(requests,1);button(root,"安装完整版").click();await flush();assert.equal(full,2);const picker=root.querySelectorAll('input').find(n=>n.type==='file');picker.files=[new Blob(["PK"] )];picker.dispatchEvent(new Event("change"));await flush();assert.equal(full,3);assert.equal(requests,1);
});


test("the tabs-row recommendation action appears only for missing original tools, independent of guide dismissal",async t=>{
 env(t);const aliases=["busybox","curl","jq","openssh","aria2","ripgrep","7zip"],installed=aliases.map(alias=>({id:`external-tool:mdo.${alias}`,alias,name:alias,valid:true,enabled:false}));
 t.mock.method(globalThis,"fetch",async()=>Response.json({ok:true,data:{items:[],total:0}}));
 const panel=(await load()).createExternalToolsPanel(),root=document.createElement("div");root.isConnected=false;
 try{
  panel.render(root,{platform:"windows-x86_64",guide:"dismissed",items:[]});let action=button(root,"一键安装推荐工具");assert(action);assert(root.querySelector(".cli-tabbar").contains(action));assert(!root.querySelector(".store-tabs").contains(action));assert.equal(document.body.querySelector(".cli-first-use").hidden,true);document.body.append(root);
  button(root,"工具商店").click();await flush();assert(button(root,"一键安装推荐工具"));
  panel.render(root,{platform:"windows-x86_64",guide:"dismissed",items:installed});assert(!button(root,"一键安装推荐工具"),"Disabled installed tools count as installed; Git and optional Python are not required");
  panel.render(root,{platform:"windows-x86_64",guide:"dismissed",items:installed.map(row=>row.alias==="jq"?{...row,valid:false}:row)});assert(button(root,"一键安装推荐工具"),"A missing or invalid program can be repaired");
  panel.render(root,{platform:"windows-x86_64",writable:false,guide:"dismissed",items:[]});assert(button(root,"一键安装推荐工具").disabled);
 }finally{panel.destroy();}
});

test("one-click recommendation queues only missing tools, preserves installed choices and disappears on completion",async t=>{
 env(t);const aliases=["busybox","curl","jq","openssh","aria2","ripgrep","7zip"],rows=[...aliases,"git","python"].map((alias,i)=>({id:`external-tool:mdo.${alias}`,alias,name:alias,kind:"external-tools",owner:0,xid:`work-${i}`,version_id:`version-${i}`})),calls=[],jobs=[];
 const items=rows.filter(row=>aliases.includes(row.alias)&&row.alias!=="aria2").map(row=>({...row,directory_id:row.xid,source:"store",valid:true,enabled:false,installation:{version_id:"retained-local-version"}}));
 const snapshot=()=>({platform:"windows-x86_64",guide:"dismissed",items});
 const id="sdk-"+"a".repeat(32);
 t.mock.method(extensionInstaller,"enqueue",async(plan,meta)=>{jobs.push(meta);return downloadRegistry.install("local",id,{...meta,versionIds:[meta.versionId],state:"installing"});});
 t.mock.method(globalThis,"fetch",async(url,options)=>{const body=options?.method==="POST"?JSON.parse(options.body):null;if(body)calls.push({url,body});return Response.json({ok:true,data:body?.action==="catalog"?{items:rows,total:rows.length}:body?.action==="install_plan"?{id:"p",revision:"r",ready:true,items:[{...rows.find(row=>row.alias==="aria2"),operation:"install"}]}:snapshot()});});
 const panel=(await load()).createExternalToolsPanel(),root=document.createElement("div");document.body.append(root);
 try{
  panel.render(root,snapshot());button(root,"一键安装推荐工具").click();await flush();assert.equal(jobs.length,0);assert.deepEqual(calls.filter(c=>c.body.action==="install_plan").map(c=>c.body.version_id),[rows.find(row=>row.alias==="aria2").version_id]);
  button(document.body.querySelector("dialog"),"安装").click();await flush();assert.equal(jobs.length,1);assert.equal(jobs[0].resourceId,"external-tool:mdo.aria2");assert(!calls.some(c=>c.url.endsWith("/enabled")));assert(items.every(row=>!row.enabled));
  const running=button(root,"正在安装…");assert(running?.disabled);running.click();await flush();assert.equal(jobs.length,1);
  const installed=rows.find(row=>row.alias==="aria2");items.push({...installed,directory_id:installed.xid,source:"store",valid:true,enabled:true,installation:{version_id:installed.version_id}});downloadRegistry.install("local",id,{state:"complete"});window.dispatchEvent(new CustomEvent("mdo-extensions-changed",{detail:{kind:"external-tools"}}));await flush();assert(!button(root,"一键安装推荐工具"));
 }finally{panel.destroy();downloadRegistry.install("local",id,{state:"complete"});const row=downloadRegistry.list("local").find(row=>row.operationId===id);if(row)downloadRegistry.hide(row.key);}
});
