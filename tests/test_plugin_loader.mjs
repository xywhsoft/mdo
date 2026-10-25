import assert from "node:assert/strict";
import test from "node:test";
import { createPluginLoader } from "../app/web/js/features/shell/plugin-loader.js";

const plugin = id => ({ id, revision: id, enabled: true, frontend: `web/__mdo_plugins/${id}/${id}/main.js` });
function fixture(importModule, dispose = async () => {}) {
  const a = plugin("a"), b = plugin("b"), calls = [];
  let current = { items: [a], pending: false }, changed = false;
  const api = { async call(method, params) {
    calls.push({ method, params });
    if (method === "plugin.list") return structuredClone(current);
    if (method === "plugin.frontend.leave") current.pending = false;
    return {};
  }, async events() { const items = changed ? [{}] : []; changed = false; return { items }; } };
  const loader = createPluginLoader({ client: "window", api: () => api, importModule,
    createScope(p) { const owned = []; return { sdk: { id: p.id }, own(f) { owned.push(f); },
      async dispose() { for (const f of owned.reverse()) await f(); await dispose(p); } }; } });
  return { loader, calls, a, b, change(value) { current = value; changed = true; } };
}

test("mutually exclusive replacement waits for asynchronous cleanup and the host acknowledgement", async () => {
  let release; const barrier = new Promise(r => { release = r; }), started = [];
  const f = fixture(async url => ({ activate(sdk) { started.push({ url, id: sdk.id }); } }), p => p.id === "a" ? barrier : Promise.resolve());
  await f.loader.poll(); assert.deepEqual(started.map(p => p.id), ["a"]);
  f.change({ items: [{ ...f.a, enabled: false }, f.b], pending: true });
  const switching = f.loader.poll(); await new Promise(r => setImmediate(r));
  assert.deepEqual(started.map(p => p.id), ["a"]);
  assert.equal(f.calls.filter(c => c.method === "plugin.frontend.leave").length, 0);
  release(); await switching;
  assert.deepEqual(started.map(p => p.id), ["a", "b"]);
  const left = f.calls.findIndex(c => c.method === "plugin.frontend.leave");
  const entered = f.calls.findIndex(c => c.method === "plugin.frontend.enter" && c.params.id === "b");
  assert(left >= 0 && left < entered);
  assert(started[1].url.startsWith("/__mdo_plugins/b/b/"));
  await f.loader.destroy();
});

test("failed activation is reported without retaining raw exception data or automatically replaying effects", async () => {
  let effects = 0;
  const f = fixture(async () => ({ activate() { effects++; throw Error("private fixture payload"); } }));
  await f.loader.poll(); f.change({ items: [f.a] }); await f.loader.poll();
  assert.equal(effects, 1);
  const failures = f.calls.filter(c => c.method === "plugin.frontend.fail");
  assert.equal(failures.length, 1); assert.equal(failures[0].params.phase, "activation");
  assert(!JSON.stringify(f.calls).includes("private fixture payload"));
  await f.loader.destroy();
});

test("failed cleanup retains the old reservation and cannot start its replacement", async () => {
  const started = [];
  const f = fixture(async () => ({ activate(sdk) { started.push(sdk.id); } }), async () => { throw Error("cleanup failed"); });
  await f.loader.poll(); f.change({ items: [{ ...f.a, enabled: false }, f.b], pending: true });
  await f.loader.poll(); f.change({ items: [{ ...f.a, enabled: false }, f.b], pending: true }); await f.loader.poll();
  assert.deepEqual(started, ["a"]);
  assert(!f.calls.some(c => c.method === "plugin.frontend.leave"));
  assert.equal(f.calls.filter(c => c.method === "plugin.frontend.fail" && c.params.phase === "cleanup").length, 1);
  await f.loader.destroy();
});

test("a successful plugin can be explicitly disabled and enabled again",async()=>{
 const started=[];const f=fixture(async()=>({activate(sdk){started.push(sdk.id);}}));
 await f.loader.poll();f.change({items:[{...f.a,enabled:false}]});await f.loader.poll();
 f.change({items:[f.a]});await f.loader.poll();assert.deepEqual(started,["a","a"]);await f.loader.destroy();
});
test("dependency replacement cleans dependents first and starts them after the new dependency",async()=>{
 const log=[];const f=fixture(async()=>({activate(sdk){log.push("start:"+sdk.id);}}),async p=>{log.push("stop:"+p.id);});
 const child={...f.b,manifest:{requires:[{id:"a"}]}};f.change({items:[child,f.a]});await f.loader.poll();
 assert.deepEqual(log,["start:a","start:b"]);
 f.change({items:[{...f.a,revision:"new-a"},child],pending:true});await f.loader.poll();
 assert.deepEqual(log,["start:a","start:b","stop:b","stop:a","start:a","start:b"]);await f.loader.destroy();
});
test("a transient retirement acknowledgement is retried without running cleanup twice",async()=>{
 let stops=0,leaves=0,phase=0;const p=plugin("one");
 const loader=createPluginLoader({client:"window",importModule:async()=>({activate(){}}),createScope:()=>({sdk:{},own(){},dispose:async()=>{stops++;}}),api:()=>({
  call:async method=>{if(method==="plugin.list")return {items:[{...p,enabled:!phase}]};if(method==="plugin.frontend.leave"&&++leaves===1)throw Object.assign(Error("busy"),{status:409});return{};},events:async()=>({items:[{}]})
 })});
 await loader.poll();phase=1;await loader.poll();await loader.poll();assert.equal(stops,1);assert.equal(leaves,2);await loader.destroy();
});

test("a connected window cleans its previous frontend when the host enters recovery",async()=>{
 const starts=[];let stops=0;const f=fixture(async()=>({activate(sdk){starts.push(sdk.id);}}),async()=>{stops++;});
 await f.loader.poll();f.change({items:[f.a],recovery_mode:true});await f.loader.poll();
 assert.equal(stops,1);assert.deepEqual(starts,["a"]);assert(f.calls.some(c=>c.method==="plugin.frontend.leave"));await f.loader.destroy();
});
