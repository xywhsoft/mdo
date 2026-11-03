import assert from "node:assert/strict";
import test from "node:test";
import { createExtensionUpdateState,isExtensionUpdate } from "../app/web/js/state/extension-updates.js";
import { extensionUpdateNeedsReview,consistentUpdatePlans,extensionUpdateActionable } from "../app/web/js/features/settings/extension-updates.js";
import { createExtensionInstaller,extensionInstallState } from "../app/web/js/state/extension-installs.js";
import { createDownloadRegistry,downloadRegistry } from "../app/web/js/state/downloads.js";
import { buildNotices } from "../app/web/js/features/notifications/model.js";
const flush=()=>new Promise(resolve=>setImmediate(resolve));

test("checks coalesce, preserve known updates offline, and never treat failure as up-to-date",async()=>{
 let calls=0,fail=false;const state=createExtensionUpdateState({target:()=>"local",transport:{async get(){calls++;if(fail)throw Error("offline");return {data:{items:[{id:"tool:qa.a",kind:"tools"}],checked_at:1}};}}});
 await Promise.all([state.refresh(),state.refresh()]);assert.equal(calls,1);assert.equal(state.get().items.length,1);
 fail=true;await state.refresh();assert.equal(state.get().items.length,1);assert.equal(state.get().stale,true);assert.equal(state.get().message,"offline");
 assert.equal(state.find({id:"tool:qa.a",kind:"tools",source:"manual"}),undefined);
});
test("a late response from another device cannot populate the current update list",async()=>{
 let target="home",release;const state=createExtensionUpdateState({target:()=>target,transport:{get:()=>new Promise(resolve=>release=resolve)}});
 const request=state.refresh();await flush();target="company";state.reset();release({data:{items:[{id:"home-tool"}]}});await request;assert.deepEqual(state.get().items,[]);
});
test("a requested recheck is not lost behind an outstanding cached read",async()=>{
 let release,checks=0;const state=createExtensionUpdateState({target:()=>"local",transport:{get:()=>new Promise(resolve=>release=resolve),async post(){checks++;return {data:{items:[],busy:true}};}}});
 const first=state.refresh();await flush();void state.refresh({check:true});release({data:{items:[]}});await first;await flush();assert.equal(checks,1);
});
test("publication serial and content, rather than display version text, decide updates",()=>{
 const local={id:"external-tool:qa.python",source:"store",installation:{xid:"work",serial:10,sha256:"old",version:"9.0"}};
 const remote={id:local.id,xid:"work",serial:11,sha256:"new",version:"1.0+pip"};assert(isExtensionUpdate(local,remote));
 assert(!isExtensionUpdate(local,{...remote,serial:9}));assert(!isExtensionUpdate(local,{...remote,sha256:"old"}));assert(!isExtensionUpdate(local,{...remote,xid:"another-author"}));assert(!isExtensionUpdate({...local,source:"manual"},remote));
});
test("routine updates need one click; new dependencies and declared permission changes need review",()=>{
 const plan={ready:true,root:"tool:a",items:[{id:"tool:a",operation:"install",old_exists:true,manifest:{}}]};assert.equal(extensionUpdateNeedsReview(plan,[{id:"tool:a"}]),false);
 assert(extensionUpdateNeedsReview({...plan,items:[...plan.items,{id:"tool:b",operation:"install",old_exists:false}]},[]));
 assert(extensionUpdateNeedsReview({...plan,items:[{...plan.items[0],manifest:{effects:["process"]}}]},[{id:"tool:a"}]));assert(extensionUpdateNeedsReview({...plan,ready:false},[]));
});
test("all extension types share one persistent update indicator",()=>{
 const items=[{version_id:"v1",kind:"tools"},{version_id:"v2",kind:"skills"},{version_id:"v3",kind:"external-tools"}];
 const notices=buildNotices({extensionUpdates:{items},preferences:{"extension-updates":{hidden:"v1,v2,v3"}},copy:{extensionUpdates:"扩展更新"}});
 assert.equal(notices.length,1);assert.equal(notices[0].count,3);assert.equal(notices[0].action,"extension_updates");assert.equal(notices[0].persistent,true);
 assert.equal(buildNotices({extensionUpdates:{items:[]},copy:{}}).length,0);
});
test("a publication changing a shared dependency during batch inspection requires a new check",()=>{
 const row={id:"tool:shared",manifest:{},version_id:"v1",sha256:"a"};assert(consistentUpdatePlans([{items:[row]},{items:[row]}]));assert(!consistentUpdatePlans([{items:[row]},{items:[{...row,version_id:"v2",sha256:"b"}]}]));
});
test("completed and uncertain native receipts cannot be submitted again while the catalogue refreshes",()=>{
 const row={id:"tool:receipt",version_id:"receipt-version"},id="receipt-ui-test";downloadRegistry.install("local",id,{versionIds:[row.version_id],roots:[row.id],name:"Tool",state:"complete",results:[{id:row.id,state:"complete"}]});
 try{assert.equal(extensionUpdateActionable(row),false);downloadRegistry.install("local",id,{state:"unknown",results:[]});assert.equal(extensionUpdateActionable(row),false);downloadRegistry.install("local",id,{state:"failed"});assert.equal(extensionUpdateActionable(row),true);}finally{for(const entry of downloadRegistry.list("local"))if(entry.operationId===id)downloadRegistry.remove(entry.key);}
});
test("one durable batch job deduplicates shared downloads and retains per-root failures",async()=>{
 const registry=createDownloadRegistry(),plans=[{id:"p1",revision:"r1",ready:true,root:"tool:a",items:[{id:"tool:shared",version_id:"shared",operation:"install",bytes:100},{id:"tool:a",version_id:"a",operation:"install",bytes:20}]},{id:"p2",revision:"r2",ready:true,root:"tool:b",items:[{id:"tool:shared",version_id:"shared",operation:"install",bytes:100},{id:"tool:b",version_id:"b",operation:"install",bytes:30}]}];let starts=0;
 const installer=createExtensionInstaller({registry,target:()=>"local",sdk:{async start(method,args,id){starts++;assert.equal(args.body.action,"update_batch");assert.equal(args.body.plans.length,2);return {id,terminal:true,state:"succeeded"};},async result(){return {data:{runtime_ready:false,runtime_error:"One update failed",results:[{id:"tool:a",state:"complete"},{id:"tool:b",state:"failed"}],items:[]}};},async call(){return {};}}});
 try{const job=await installer.enqueueBatch(plans,{name:"Updates"});assert.equal(starts,1);assert.equal(job.total,150);assert.deepEqual(job.versionIds,["shared","a","b"]);assert.equal(job.state,"failed");assert.equal(extensionInstallState(job,"tool:a"),"complete");assert.equal(extensionInstallState(job,"tool:b"),"failed");}finally{installer.destroy();}
});
