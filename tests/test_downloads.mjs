import test from "node:test";
import assert from "node:assert/strict";
import { createDownloadRegistry, downloadRegistry, downloadActive } from "../app/web/js/state/downloads.js";
import { api, hasPendingApiWrites } from "../app/web/js/api/client.js";
const flush = () => new Promise(r => setImmediate(r));

test("installed native update and superseded package stay reconciled across polling",()=>{
  const r=createDownloadRegistry(),hash="c".repeat(64),task={id:"U".repeat(32),source:"update",state:"ready",name:"mdo",sha256:hash};
  r.observe("/download-tasks",[task]);r.observe("/update",{status:"current",ready:false,busy:false,sha256:hash});
  assert.equal(r.list("local")[0].state,"complete");
  r.observe("/download-tasks",[task]);assert.equal(r.list("local")[0].state,"complete");
  r.observe("/update",{status:"available",ready:false,busy:false,sha256:"new"});assert.equal(r.list("local")[0].state,"expired");
  r.observe("/update",{status:"error",ready:true,message:"offline"});assert.equal(r.list("local")[0].state,"ready");
  r.observe("/download-tasks",[{...task,build_id:10}]);r.observe("/distribution",{build_id:11});
  assert.equal(r.list("local")[0].state,"complete","local build receipt confirms installation even offline");
});

test("cleared native archive never resurfaces from legacy history or a single-task reply",()=>{
  const r=createDownloadRegistry(),id="N".repeat(32),archive="A".repeat(32);
  r.observe("/extension-tasks",[{id:archive,state:"complete"}]);
  r.observe("/download-tasks",[{id,source:"extension",archive_id:archive,state:"ready"}]);
  assert.equal(r.list("local")[0].state,"complete");
  r.observe(`/extension-tasks/${archive}/forget`,{forgotten:true});
  r.observe(`/download-tasks/${id}/forget`,[]);r.observe("/extension-tasks",[]);
  assert.equal(r.list("local").length,0);
  r.observe("/extension-tasks",[{id:archive,state:"complete"}]);r.observe("/extension-tasks/"+id,{id,state:"paused"});
  assert.equal(r.list("local").length,2,"single-task reads must not remove unrelated archives");
  r.observe("/extension-tasks",[]);assert.equal(r.list("local").length,0);
});

test("durable native snapshots expose paused progress and merge archive ownership without duplicate rows",()=>{
  const r=createDownloadRegistry(),id="N".repeat(32),archive="A".repeat(32);
  r.observe("/extension-tasks",[{id:archive,state:"receiving",purpose:"install",kind:"skills",bytes:100,received:40,store:{xid:"work",version_id:"version"}}],"home");
  r.observe("/download-tasks",[{id,source:"extension",archive_id:archive,kind:"skills",name:"Model runtime",state:"paused",received:40,bytes:100,sha256:"a".repeat(64)}],"home");
  assert.equal(r.list("home").length,1);const row=r.list("home")[0];assert.equal(row.source,"native");assert.equal(row.done,40);assert.equal(row.store.xid,"work");assert.equal(downloadActive(row),false);
  r.observe("/extension-tasks",[{id:archive,state:"complete",purpose:"install",kind:"skills",bytes:100,received:100,store:{xid:"work"}}],"home");
  assert.equal(r.list("home")[0].state,"complete");
  r.observe("/download-tasks",[],"other");assert.equal(r.list("home").length,1);
  r.observe("/download-tasks",[],"home");assert(!r.list("home").some(row=>row.source==="native"));
});

test("a native update carries real byte speed and suppresses the old status-only row",()=>{
  const r=createDownloadRegistry(),hash="b".repeat(64);
  r.observe("/update",{status:"downloading",sha256:hash,build_id:10,size:100});
  r.observe("/download-tasks",[{id:"U".repeat(32),source:"update",name:"mdo",state:"downloading",received:50,bytes:100,sha256:hash,speed:25,eta_seconds:2,active:true}]);
  assert.equal(r.list("local").length,1);assert.equal(r.list("local")[0].speed,25);assert.equal(r.list("local")[0].eta,2);
});

test("native snapshots merge without duplicates and remain isolated by device", () => {
  const registry = createDownloadRegistry();
  const snapshot = { tasks: [{ id: "one", action: "install", name: "rg", state: "running", done: 5, total: 10 }, { id: "catalog", action: "catalog", state: "running" }, { id: "publish", action: "publish", state: "uploading" }] };
  registry.observe("/cli-tools", snapshot, "home"); registry.observe("/cli-tools", snapshot, "home");
  assert.equal(registry.list("home").length, 2); assert.equal(registry.list("company").length, 0);
  registry.observe("/update", { status: "downloading", build_id: 7, size: 100 }, "home");
  registry.observe("/update", { status: "ready", build_id: 7, ready: true }, "home");
  assert.equal(registry.list("home").find(r => r.source === "update").state, "ready");
  assert.equal(registry.list("home").find(r => r.source === "update").done, null, "unknown update byte progress must not be invented");
  registry.observe("/cli-tools", { tasks: [{ ...snapshot.tasks[0], state: "complete" }, snapshot.tasks[2]] }, "home");
  registry.clear("home"); registry.observe("/cli-tools", { tasks: [{ ...snapshot.tasks[0], state: "complete" }, snapshot.tasks[2]] }, "home");
  assert(!registry.list("home").some(r => r.taskId === "one"));
  assert(registry.list("home").some(downloadActive)); assert(registry.list("home").some(r => r.state === "ready"));
});

test("reloaded request history is uncertain until an installed receipt verifies it; no request is replayed", async () => {
  let saved = "[]", release, calls = 0;
  const storage = { getItem: () => saved, setItem: (_, value) => { saved = value; } }, registry = createDownloadRegistry(storage);
  const job = registry.run("home", { id: 42, name: "Skill" }, () => { calls++; return new Promise(r => { release = r; }); });
  const restored = createDownloadRegistry(storage); assert.equal(restored.list("home")[0].state, "unknown"); assert.equal(calls, 1);
  restored.observe("/ecosystem", { "42": { id: 42 } }, "company"); assert.equal(restored.list("home")[0].state, "unknown");
  restored.observe("/ecosystem", { "42": { id: 42 } }, "home"); assert.equal(restored.list("home")[0].state, "complete");
  release("ok"); assert.equal(await job, "ok"); assert.equal(calls, 1);
});

test("known operation failures remain failures; lost connections never claim completion", async () => {
  const registry = createDownloadRegistry();
  await assert.rejects(registry.run("local", { id: 1 }, async () => { throw Object.assign(Error("plan changed"), { status: 409 }); }));
  await assert.rejects(registry.run("local", { id: 2 }, async () => { throw Error("connection lost"); }));
  assert.equal(registry.list("local").find(r => r.resourceId === 1).state, "failed");
  assert.equal(registry.list("local").find(r => r.resourceId === 2).state, "unknown");
});

test("native update failures and cancellations stay distinct; superseded packages cannot stay ready", () => {
  const registry = createDownloadRegistry(), initial = { build_id: 7, edition: "lite", platform: "android-arm64-v8a", sha256: "lite-hash" };
  registry.observe("/update", { ...initial, status: "downloading", busy: true });
  registry.observe("/update", { ...initial, status: "available", busy: false, message: "Download failed or checksum mismatch" });
  assert.equal(registry.list("local")[0].state, "failed");
  registry.observe("/update", { ...initial, status: "downloading", busy: true });
  registry.observe("/update", { ...initial, status: "error", busy: false, message: "Update operation cancelled" });
  assert(registry.list("local").some(r => r.state === "cancelled"));
  registry.observe("/update", { ...initial, status: "ready", ready: true });
  registry.observe("/update", { ...initial, status: "error", ready: true, message: "Update service is unavailable; retry checking" });
  assert(registry.list("local").some(r => r.state === "ready"), "metadata check failure preserves the verified package");
  registry.observe("/update", { ...initial, edition: "full", sha256: "full-hash", status: "available", busy: false, ready: false });
  assert(!registry.list("local").some(r => r.state === "ready"));
  assert(registry.list("local").some(r => r.state === "expired"));
});

test("actual API installation requests enter the shared registry, metadata reads do not", async t => {
  const previous = globalThis.fetch; t.after(() => { globalThis.fetch = previous; }); let release;
  globalThis.fetch = async (_, options) => {
    const body = JSON.parse(options.body || "{}");
    if (body.action === "install") await new Promise(r => { release = r; });
    return new Response(JSON.stringify({ ok: true, data: {} }), { status: 200 });
  };
  const id = 987654; const job = api.post("/ecosystem", { action: "install", id }, { download: { name: "Fixture Skill" } });
  await flush(); assert(hasPendingApiWrites()); assert(downloadRegistry.list("local").some(r => r.resourceId === id && r.state === "running"));
  const count = downloadRegistry.list("local").length; await api.post("/ecosystem", { action: "detail", id });
  assert.equal(downloadRegistry.list("local").length, count);
  release(); await job; assert.equal(downloadRegistry.list("local").find(r => r.resourceId === id).state, "complete"); assert(!hasPendingApiWrites());
});
