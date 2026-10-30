import assert from "node:assert/strict";
import test from "node:test";
import { warningNotice } from "../app/web/js/features/notifications/warnings.js";
import { buildNotices } from "../app/web/js/features/notifications/model.js";
const a={domain:"knowledge",code:"root_not_directory",stage:"check",subject_id:"",path:"knowledge",message:"Root conflict"};
const b={domain:"mcp",stage:"parse",subject_id:"server",path:"mcp/server.json",message:"Invalid configuration"};
const notice=(items,extra={})=>warningNotice({diagnostics:{items},locale:"zh-CN",...extra});

test("warnings deduplicate across checks, preserve a stable revision and collect every source into one indicator",()=>{
  const n=notice([a,b,a]),reordered=notice([b,a]);assert.equal(n.items.length,2);assert.equal(n.revision,reordered.revision);
  assert.notEqual(notice([{...a,message:"Changed"},b]).revision,n.revision);
  assert.equal(notice([]),null);assert(n.persistent);assert.equal(n.items.find(x=>x.path==="knowledge").section,"knowledge");
  assert(n.items.find(x=>x.path==="knowledge").body.includes("原文件已保留"));
  assert.equal(notice([b,{...b,message:"A different failure in the same file"}]).items.length,2);
});
test("online warnings join local checks while ordinary messages and updates keep their direct destinations",()=>{
  const args={distribution:{cli:{},notices:[{id:"admin",revision:2,title:"Admin",body:"Notice",icon:"warning"},{id:"info",revision:1,title:"Info",body:"Message",icon:"message"}]},diagnostics:{items:[a]},update:{available:true,sha256:"update"},locale:"zh-CN",copy:{update:"Update"}};
  const list=buildNotices(args),warning=list.find(n=>n.icon==="warning");assert.equal(list.filter(n=>n.icon==="warning").length,1);assert.equal(warning.items.length,2);assert(list.some(n=>n.id==="info"));assert(list.some(n=>n.action==="update"));
  const hidden=buildNotices({...args,preferences:{admin:{hidden:2},"system-warnings":{hidden:warning.revision}}});assert.equal(hidden.find(n=>n.id==="system-warnings").items.length,1);
  assert(buildNotices({...args,preferences:{admin:{hidden:1}}}).find(n=>n.id==="system-warnings").items.length===2);
});
test("an unavailable check preserves known issues and never pretends they have been resolved",()=>{
  const n=notice([a],{failed:true});assert.equal(n.items.length,2);assert(n.items.some(n=>n.id==="diagnostic:unavailable"));
  assert.equal(notice([a],{diagnostics:{items:[a],truncated:true}}).items.length,2);
  assert.equal(notice([],{diagnostics:{items:{bad:true}}}),null);
});

test("all current diagnostic domains share one warning and never link to the removed diagnostics page",()=>{
  const rows=["storage","sessions","schedules","modules","skills","mcp","plugins","extensions","knowledge"]
    .map(domain=>({...b,domain,settings_section:"diagnostics"}));
  const n=notice(rows);
  assert.equal(n.id,"system-warnings");assert.equal(n.items.length,9);
  for(const row of n.items)assert.notEqual(row.section,"diagnostics");
  for(const domain of ["storage","sessions","schedules"])
    assert.equal(n.items.find(row=>row.id.includes(`"${domain}"`)).section,undefined);
  assert.equal(notice([{...b,settings_section:"tools"}]).items[0].section,"tools");
  assert.equal(notice([{...b,settings_section:"missing-page"}]).items[0].section,"mcp");
});
