import test from "node:test";
import assert from "node:assert/strict";
import { testDocument, descendants } from "./helpers/extension-dom.mjs";
import { setTargetSwitchGuard } from "../app/web/js/api/target.js";

const flush = async () => { for (let i = 0; i < 30; i++) await new Promise(r => setImmediate(r)); };
const token = "a".repeat(32) + "-0", id = "d".repeat(32), other = "e".repeat(32);
const deferred = () => { let resolve, reject; const promise = new Promise((a,b) => { resolve=a;reject=b; }); return {promise,resolve,reject}; };
async function harness(t, options = {}) {
  const saved = { document: globalThis.document, window: globalThis.window, fetch: globalThis.fetch };
  const doc = Object.assign(new EventTarget(), testDocument()), ids = new Map();
  doc.querySelector = selector => selector.startsWith("#") ? ids.get(selector.slice(1)) || null : doc.body.querySelector(selector);
  globalThis.document = doc; globalThis.window = new EventTarget();
  for (const key of ["open-devices", "settings-target", "mobile-target-label", "target-connection-notice"]) {
    const node = doc.createElement(key === "open-devices" ? "button" : "div"); ids.set(key,node); doc.body.append(node);
    if (key === "open-devices") node.append(doc.createElement("span"));
    if (key === "target-connection-notice") {
      node.append(doc.createElement("p")); for (const attr of ["data-target-retry","data-target-local","data-target-choose"]) { const b=doc.createElement("button");b.setAttribute(attr,"");node.append(b); }
    }
  }
  const model = { owner: 2, devices: [{id,name:"本机测试",platform:"windows",online:true,allow_remote:true,revoked:false,viewers:0},
    {id:other,name:"另一台电脑",platform:"windows",online:true,allow_remote:true,revoked:false,viewers:0}],
    listing: null, cacheOwner:2, job: {id:"f".repeat(32),state:"idle"}, posts: [], refreshes:0, hold:null, failSync:false, failWrite:false, loseAdmission:false };
  model.listing = structuredClone(model.devices);
  Object.assign(model, options);
  const state = () => ({device_id:id,job:model.job,listing:model.cacheOwner===model.owner?{devices:structuredClone(model.listing)}:{}});
  globalThis.fetch = async (url, init={}) => {
    if (url === "/api/v1/account") return Response.json({ok:true,data:{state:"signed_in",profile:{id:model.owner,nickname:"测试账号"}}});
    if (url === "/api/v1/connector/devices") {
      const body=JSON.parse(init.body);model.posts.push(body);const key=String(model.posts.length).padStart(32,"0");
      model.job={id:key,state:"running",action:body.action,device_id:body.device_id||""};
      if (body.action === "refresh") {
        model.refreshes++; if(model.failSync){model.job.state="failed";model.job.status=503;model.job.error="service_unavailable";return Response.json({ok:true,data:state()});}
        model.listing=model.owner===2?structuredClone(model.devices):[];model.cacheOwner=model.owner;model.job.state="done";
      } else {
        if(model.failWrite){model.job.state="failed";model.job.status=403;model.job.error="denied";}
        else {
          if(body.action==="remove")model.devices=model.devices.filter(d=>d.id!==body.device_id);
          if(body.action==="revoke")model.devices=model.devices.map(d=>d.id===body.device_id?{...d,revoked:true,allow_remote:false}:d);
          if(!model.hold)model.job.state="done";
        }
        if(model.loseAdmission){model.loseAdmission=false;throw TypeError("Lost admission response");}
      }
      return Response.json({ok:true,data:state()});
    }
    if(url==="/api/v1/connector/state") {
      if(model.readFailed)return Response.json({ok:false,error:{code:"unavailable",message:"State unavailable"}},{status:503});
      if(model.hold&&model.job.state==="running"){const hold=model.hold;await hold.promise;model.job.state="done";}
      return Response.json({ok:true,data:state()});
    }
    return Response.json({ok:true,data:{}},{headers:{"X-Mdo-Write-Token":token}});
  };
  const {createDevices}=await import("../app/web/js/features/shell/devices.js");const picker=createDevices();
  t.after(()=>{picker.dispose();setTargetSwitchGuard(async()=>true);Object.assign(globalThis,saved);});
  await picker.open();await flush();
  const row = identity => descendants(doc.body).find(n=>n.classList?.contains("device-row")&&n.dataset.deviceId===identity);
  const action = (identity, key) => row(identity)?.querySelectorAll("button").find(n=>n.dataset.action===key);
  const named = text => descendants(doc.body).find(n=>n.tagName==="BUTTON"&&n.textContent===text);
  return {model,picker,doc,row,action,named};
}

test("revoke/remove consume success immediately and synchronize without manual refresh",async t=>{
  const {model,row,action,doc}=await harness(t);
  const before=model.refreshes;action(id,"revoke").click();await flush();
  assert(row(id).textContent.includes("已撤销"));assert(row(id).textContent.includes("当前设备"));assert(action(id,"remove"));assert.equal(model.refreshes,before+1);
  action(id,"remove").click();await flush();assert(!row(id));assert(doc.body.textContent.includes("已移除「本机测试」"));
  assert.equal(model.posts.filter(p=>p.action==="revoke").length,1);assert.equal(model.posts.filter(p=>p.action==="remove").length,1);
});

test("slow actions show row progress, reject double clicks and survive close/reopen",async t=>{
  const {model,picker,row,action,named}=await harness(t);model.hold=deferred();
  const original=action(id,"revoke");original.click();original.click();await flush();
  assert(row(id).textContent.includes("正在撤销"));assert.equal(action(id,"revoke").getAttribute("aria-busy"),"true");assert(action(other,"revoke").disabled);
  named("关闭").click();await flush();await picker.open();await flush();assert(row(id).textContent.includes("正在撤销"));
  model.hold.resolve();await flush();assert(action(id,"remove"));assert.equal(model.posts.filter(p=>p.action==="revoke").length,1);
});

test("confirmed success remains visible when full-list synchronization fails",async t=>{
  const {model,row,action,doc,named}=await harness(t);model.failSync=true;
  action(id,"revoke").click();await flush();assert(action(id,"remove"));assert(row(id).textContent.includes("已撤销"));
  assert(doc.body.textContent.includes("操作已成功"));assert(!action(id,"retry"));
  model.failSync=false;named("刷新设备列表").click();await flush();assert(!doc.body.textContent.includes("设备列表暂未同步"));
});

test("definitive failures keep the row and provide an explicit action retry",async t=>{
  const {model,row,action}=await harness(t);model.failWrite=true;
  action(id,"revoke").click();await flush();assert(row(id));assert(action(id,"retry"));assert(row(id).textContent.includes("权限"));assert(!action(id,"remove"));
  model.failWrite=false;action(id,"retry").click();await flush();assert(action(id,"remove"));assert.equal(model.posts.filter(p=>p.action==="revoke").length,2);
});

test("lost write admission is reconciled by receipt reads, never resubmitted",async t=>{
  const {model,action,row}=await harness(t);model.loseAdmission=true;
  action(id,"revoke").click();await flush();assert(row(id).textContent.includes("尚未确认"));assert(action(id,"revoke").disabled);
  action(id,"inspect").click();await flush();assert(action(id,"remove"));assert.equal(model.posts.filter(p=>p.action==="revoke").length,1);
});

test("a failed verification keeps an unknown write blocked instead of offering replay",async t=>{
  const {model,action,row}=await harness(t);model.loseAdmission=true;action(id,"revoke").click();await flush();
  model.readFailed=true;action(id,"inspect").click();await flush();assert(action(id,"inspect"));assert(!action(id,"retry"));assert(action(id,"revoke").disabled);
  assert(row(id).textContent.includes("尚未确认"));assert.equal(model.posts.filter(p=>p.action==="revoke").length,1);
});

test("a newly mounted picker resumes an already admitted backend mutation",async t=>{
  const {model,row}=await harness(t,{job:{id:"9".repeat(32),state:"queued",action:"remove",device_id:id}});
  assert(row(id).textContent.includes("核对"));model.devices=model.devices.filter(d=>d.id!==id);model.job.state="done";
  await new Promise(r=>setTimeout(r,300));await flush();assert(!row(id));assert(!model.posts.some(p=>p.action==="remove"));
});

test("account change fences an old mutation result from the newly opened account",async t=>{
  const {model,action,doc}=await harness(t);model.hold=deferred();action(id,"revoke").click();await flush();
  model.owner=3;window.dispatchEvent(new Event("mdo-controller-account-changed"));await flush();model.hold.resolve();await flush();
  assert(!doc.body.textContent.includes("已撤销「本机测试」的远控授权"));
  assert(!doc.body.textContent.includes("另一台电脑"));
  assert.equal(model.posts.filter(p=>p.action==="revoke").length,1);
});

test("a detached old-account action cannot submit for the new account",async t=>{
  const {model,action}=await harness(t),old=action(other,"revoke");
  model.owner=3;window.dispatchEvent(new Event("mdo-controller-account-changed"));await flush();old.click();await flush();
  assert(!model.posts.some(p=>p.action==="revoke"));
});

test("automatic polling pauses when hidden and resumes on visibility",async t=>{
  t.mock.timers.enable({apis:["setTimeout"]});
  const {model,doc}=await harness(t),before=model.refreshes;
  doc.hidden=true;doc.dispatchEvent(new Event("visibilitychange"));t.mock.timers.tick(10001);await flush();assert.equal(model.refreshes,before);
  doc.hidden=false;doc.dispatchEvent(new Event("visibilitychange"));await flush();assert.equal(model.refreshes,before+1);
});

test("selection guard feedback and copy-draft retry are preserved",async t=>{
  const {action,doc,named}=await harness(t);setTargetSwitchGuard(async()=>false);
  action(other,"connect").click();await flush();assert(doc.body.textContent.includes("保存或复制"));assert(named("复制未保存草稿并切换"));
});

test("focus refresh preserves existing rows and scroll position",async t=>{
  const {model,row,doc}=await harness(t);const before=row(id),dialog=doc.body.querySelector("dialog");
  const body=dialog.querySelector(".mdo-layer-body");assert(body);body.scrollTop=170;
  const count=model.refreshes;window.dispatchEvent(new Event("focus"));await flush();assert.equal(model.refreshes,count+1);assert.equal(row(id),before);assert.equal(body.scrollTop,170);
});
