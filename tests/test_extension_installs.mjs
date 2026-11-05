import test from "node:test";
import assert from "node:assert/strict";
import { createDownloadRegistry } from "../app/web/js/state/downloads.js";
import { createExtensionInstaller } from "../app/web/js/state/extension-installs.js";

const plan={id:"plan",revision:"revision",ready:true,items:[{operation:"install",version_id:"version"}]};
const metadata={kind:"tools",versionId:"version",name:"Fixture"};
test("accepted SDK installation is independent, deduplicated and checked through its final receipt",async t=>{
 const registry=createDownloadRegistry();let state={state:"running",terminal:false},released=0,started=0,notice=0;
 const sdk={async start(method,params,id){started++;assert.equal(method,"product.ecosystem.post");assert.deepEqual(params.body,{action:"apply_plan",plan_id:plan.id,revision:plan.revision,trust_code:true,queued:true});return {...state,id};},async call(method){if(method==="operation.release"){released++;return {};}return state;},async result(){return {status:200,data:{runtime_ready:true}};}};
 const installer=createExtensionInstaller({registry,sdk,target:()=>"local",notify(){notice++;}});t.after(()=>installer.destroy());
 const row=await installer.enqueue(plan,metadata);assert.equal(registry.list("local")[0].state,"installing");assert.equal((await installer.enqueue(plan,metadata)).operationId,row.operationId);assert.equal(started,1);
 state={state:"succeeded",terminal:true};await installer.refresh();assert.equal(registry.list("local")[0].state,"complete");assert.equal(notice,1);assert.equal(released,1);
});
test("install failures retain their causes and incomplete activation is not reported as success",async t=>{
 for(const [job,result,error] of [[{terminal:true,state:"failed",error:{message:"HTTP 503 from upstream"}},null,"HTTP 503 from upstream"],[{terminal:true,state:"succeeded"},{data:{runtime_ready:false,runtime_error:"Module load failed"}},"Module load failed"]]){
  const registry=createDownloadRegistry(),sdk={start:async()=>job,call:async()=>({}),result:async()=>result};
  const installer=createExtensionInstaller({registry,sdk,target:()=>"local"});try{await installer.enqueue(plan,metadata);assert.equal(registry.list("local")[0].state,"failed");assert.equal(registry.list("local")[0].error,error);}finally{installer.destroy();}
 }
});
test("page recovery reads the saved operation identity and never replays installation",async t=>{
 let saved="[]",starts=0,gets=0;
 const storage={getItem:()=>saved,setItem:(k,v)=>{saved=v;}},first=createDownloadRegistry(storage),id="sdk-"+"3".repeat(32);
 first.install("local",id,{...metadata,versionIds:["version"],state:"installing"});
 const registry=createDownloadRegistry(storage),sdk={start:async()=>{starts++;},call:async method=>{if(method==="operation.get"){gets++;return {terminal:true,state:"succeeded"};}return {};},result:async()=>({data:{runtime_ready:true}})};
 const installer=createExtensionInstaller({registry,sdk,target:()=>"local"});t.after(()=>installer.destroy());assert.equal(registry.list("local")[0].state,"unknown");await installer.refresh();assert.equal(starts,0);assert.equal(gets,1);assert.equal(registry.list("local")[0].state,"complete");
});
test("shared dependencies do not replace a different root installation",async t=>{
 const registry=createDownloadRegistry();let starts=0;
 const sdk={start:async()=>{starts++;return {terminal:false};},call:async()=>({})};const installer=createExtensionInstaller({registry,sdk,target:()=>"local"});t.after(()=>installer.destroy());
 await installer.enqueue({...plan,items:[...plan.items,{operation:"install",version_id:"shared"}]},metadata);
 await installer.enqueue({...plan,id:"another",items:[{operation:"install",version_id:"another"},{operation:"install",version_id:"shared"}]},{...metadata,versionId:"another"});assert.equal(starts,2);
});
test("a different device cannot consume a late installation response",async t=>{
 let current="first",release;const registry=createDownloadRegistry(),sdk={start:()=>new Promise(r=>{release=r;}),call:async()=>{throw Error("No read on the new device");}};
 const installer=createExtensionInstaller({registry,sdk,target:()=>current});t.after(()=>installer.destroy());const queued=installer.enqueue(plan,metadata);current="second";release({terminal:false});await queued;assert.equal(registry.list("second").length,0);assert.equal(registry.list("first")[0].state,"queued");
});
test("raw archive children share the parent installation task instead of duplicating it",()=>{
 const registry=createDownloadRegistry(),id="sdk-"+"4".repeat(32),task="a".repeat(32);
 registry.install("local",id,{...metadata,versionIds:["version"],state:"installing"});registry.observe("/extension-tasks",[{id:task,state:"receiving",purpose:"install",received:25,bytes:100,store:{version_id:"version"}}]);assert.equal(registry.list("local").length,1);assert.equal(registry.list("local")[0].done,25);
 registry.install("local",id,{state:"complete"});registry.hide(registry.list("local")[0].key);assert.equal(registry.list("local").length,0);
});

test("one installation absorbs its native transfer in either snapshot order and after page reload", () => {
 const id="sdk-"+"5".repeat(32),task="a".repeat(32),download="n".repeat(32);
 const archive={id:task,state:"receiving",purpose:"install",received:10,bytes:100,store:{version_id:"version"}};
 const native={id:download,source:"extension",identity:"version",archive_id:task,name:"Fixture",state:"downloading",active:true,received:40,bytes:100,speed:20,eta_seconds:3,engine:"aria2"};
 for(const nativeFirst of [true,false]){
  let saved="[]";
  const storage={getItem:()=>saved,setItem:(_,value)=>{saved=value;}},registry=createDownloadRegistry(storage);
  registry.install("local",id,{...metadata,versionIds:["version"],state:"installing",total:100,done:0});
  if(nativeFirst){registry.observe("/download-tasks",[native]);assert.equal(registry.list("local").length,1,"the version identity links the transfer before archive polling completes");}
  registry.observe("/extension-tasks",[archive]);
  if(!nativeFirst)registry.observe("/download-tasks",[native]);
  const rows=registry.list("local");assert.equal(rows.length,1);const row=rows[0];
  assert.equal(row.source,"install");assert.equal(row.operationId,id);assert.equal(row.state,"installing");assert.equal(row.phase,"downloading");
  assert.equal(row.done,40,"native bytes win over lagging archive staging");assert.equal(row.speed,20);assert.equal(row.eta,3);assert.equal(row.engine,"aria2");assert.equal(row.transfers[0].taskId,download);
  const restored=createDownloadRegistry(storage);assert.equal(restored.list("local").length,1);assert.equal(restored.list("local")[0].state,"unknown");
  registry.observe("/download-tasks",[{...native,state:"ready",active:false,received:100,speed:0}]);
  assert.equal(registry.list("local")[0].state,"installing","download completion is not installation completion");
  assert.equal(registry.list("local")[0].phase,"installing");
  registry.install("local",id,{state:"failed",error:"Activation failed"});
  assert.equal(registry.list("local")[0].state,"failed");assert.equal(registry.list("local")[0].error,"Activation failed");
  registry.install("local",id,{state:"complete",error:""});
  registry.hide(registry.list("local")[0].key);assert.equal(registry.list("local").length,0);
  registry.observe("/extension-tasks",[{...archive,state:"complete",received:100}]);registry.observe("/download-tasks",[{...native,state:"complete",active:false,received:100}]);
  assert.equal(registry.list("local").length,0,"clearing the total task must not expose its children on the next poll");
 }
});

test("dependency downloads aggregate once per version and do not consume unrelated device or version tasks", () => {
 const registry=createDownloadRegistry(),id="sdk-"+"6".repeat(32),a="a".repeat(32),b="b".repeat(32),c="c".repeat(32);
 registry.install("home",id,{...metadata,versionIds:["version","dependency"],state:"installing",total:300});
 const native=(task,version,bytes,done)=>({id:task,source:"extension",identity:version,archive_id:task.toUpperCase(),name:"Fixture",state:"downloading",active:true,bytes,received:done,speed:10});
 registry.observe("/download-tasks",[native(a,"version",100,40),native(b,"dependency",200,80),native(c,"unrelated",100,30)],"home");
 const rows=registry.list("home"),parent=rows.find(row=>row.source==="install");
 assert.equal(rows.length,2,"a different version with the same name is still a separate task");assert.equal(parent.done,120);assert.equal(parent.total,300);assert.equal(parent.speed,20);assert.equal(parent.eta,9);
 registry.observe("/download-tasks",[native(a,"version",100,90)],"company");assert.equal(registry.list("company").length,1);assert.equal(registry.list("company")[0].source,"native");assert.equal(registry.list("home").length,2);
 // Existing saved download histories have no versionId; their archive identity still links them.
 const legacy=createDownloadRegistry();legacy.install("local",id,{...metadata,versionIds:["version"],state:"installing"});
 legacy.observe("/download-tasks",[{...native(a,"version",100,40),identity:undefined}]);
 legacy.observe("/extension-tasks",[{id:a.toUpperCase(),purpose:"install",state:"receiving",received:20,bytes:100,store:{version_id:"version"}}]);
 assert.equal(legacy.list("local").length,1);assert.equal(legacy.list("local")[0].done,40);
});

test("a malformed terminal receipt stays uncertain and never declares installation successful",async t=>{
 const registry=createDownloadRegistry(),sdk={start:async()=>({terminal:false}),call:async()=>({terminal:true,state:"succeeded"}),result:async()=>({data:{}})};
 const installer=createExtensionInstaller({registry,sdk,target:()=>"local"});t.after(()=>installer.destroy());await installer.enqueue(plan,metadata);await installer.refresh();assert.equal(registry.list("local")[0].state,"unknown");
});
