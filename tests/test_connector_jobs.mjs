import test from "node:test";
import assert from "node:assert/strict";
import {connectorJob,connectorWatchJob,connectorRequest} from "../app/web/js/api/connector.js";
const id="a".repeat(32),token="b".repeat(32)+"-0";
function setup(t, read){
 const posts=[];t.mock.method(globalThis,"fetch",async(url,options={})=>{
  if(url==="/api/v1/connector/devices"){posts.push(JSON.parse(options.body));return Response.json({ok:true,data:{job:{id,state:"queued"}}});}
  if(url==="/api/v1/connector/state")return read(options);
  return Response.json({ok:true,data:{}},{headers:{"X-Mdo-Write-Token":token}});
 });return posts;
}
const state=(phase,fields={})=>Response.json({ok:true,data:{job:{id,state:phase,...fields}}});

test("an admitted write exposes its receipt and resumes through reads only",async t=>{
 let broken=true;const posts=setup(t,()=>{if(broken)throw TypeError("read lost");return state("done");});let admitted;
 await assert.rejects(connectorJob({action:"revoke",device_id:"c".repeat(32)},{onQueued:value=>admitted=value}),e=>e.jobId===id&&e.outcomeUnknown);
 assert.equal(admitted,id);broken=false;assert.equal((await connectorWatchJob(id)).job.state,"done");assert.equal(posts.length,1);
});
test("a failed receipt preserves the action's own HTTP status",async t=>{
 setup(t,()=>state("failed",{status:403,error:"denied",transport:{phase:"transport"}}));
 await assert.rejects(connectorJob({action:"remove"}),e=>e.jobId===id&&e.status===403&&!e.outcomeUnknown);
});
test("HTTP failure while reading an admitted write is still an unknown outcome",async t=>{
 setup(t,()=>Response.json({ok:false,error:{code:"unavailable",message:"temporarily unavailable"}},{status:503}));
 await assert.rejects(connectorJob({action:"revoke"}),e=>e.jobId===id&&e.status===503&&e.outcomeUnknown);
});
test("superseded receipts cannot be mistaken for the original operation",async t=>{
 setup(t,()=>Response.json({ok:true,data:{job:{id:"c".repeat(32),state:"done"}}}));let received=false;
 await assert.rejects(connectorWatchJob(id,{onState:()=>received=true}),e=>e.code==="connector_job_changed"&&e.jobId===id&&e.outcomeUnknown);assert(!received);
});
test("a deadline preserves receipt identity and does not resubmit",async t=>{
 const posts=setup(t,()=>state("running"));await assert.rejects(connectorJob({action:"remove"},{timeoutMs:1}),e=>e.code==="connector_job_timeout"&&e.jobId===id&&e.outcomeUnknown);assert.equal(posts.length,1);
});
test("cancelling receipt polling releases its wait without another write",async t=>{
 const controller=new AbortController(),posts=setup(t,()=>state("running"));
 const job=connectorJob({action:"revoke"},{signal:controller.signal,onState:()=>queueMicrotask(()=>controller.abort())});
 await assert.rejects(job,e=>e.name==="AbortError"&&e.jobId===id);assert.equal(posts.length,1);
});
test("a stalled receipt HTTP read is aborted by the total deadline",async t=>{
 const posts=setup(t,options=>new Promise((resolve,reject)=>options.signal.addEventListener("abort",()=>reject(new DOMException("aborted","AbortError")),{once:true})));
 await assert.rejects(connectorJob({action:"revoke"},{timeoutMs:10}),e=>e.code==="connector_job_timeout"&&e.jobId===id&&e.outcomeUnknown);assert.equal(posts.length,1);
});
test("a stalled write admission has a bounded unknown outcome without replay",async t=>{
 let posts=0;t.mock.method(globalThis,"fetch",async(url,options={})=>{
  if(url==="/api/v1/connector/devices"){posts++;return new Promise((resolve,reject)=>options.signal.addEventListener("abort",()=>reject(new DOMException("aborted","AbortError")),{once:true}));}
  return Response.json({ok:true,data:{}},{headers:{"X-Mdo-Write-Token":token}});
 });
 await assert.rejects(connectorJob({action:"remove"},{timeoutMs:10}),e=>e.code==="connector_job_timeout"&&e.outcomeUnknown);assert.equal(posts,1);
});
test("account verification reads also have an explicit deadline",async t=>{
 t.mock.method(globalThis,"fetch",async(url,options)=>new Promise((resolve,reject)=>options.signal.addEventListener("abort",()=>reject(new DOMException("aborted","AbortError")),{once:true})));
 await assert.rejects(connectorRequest("/account",undefined,{timeoutMs:10}),e=>e.code==="connector_job_timeout");
});
