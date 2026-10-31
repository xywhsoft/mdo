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

test("a malformed terminal receipt stays uncertain and never declares installation successful",async t=>{
 const registry=createDownloadRegistry(),sdk={start:async()=>({terminal:false}),call:async()=>({terminal:true,state:"succeeded"}),result:async()=>({data:{}})};
 const installer=createExtensionInstaller({registry,sdk,target:()=>"local"});t.after(()=>installer.destroy());await installer.enqueue(plan,metadata);await installer.refresh();assert.equal(registry.list("local")[0].state,"unknown");
});
