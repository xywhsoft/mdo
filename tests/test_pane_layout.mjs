import assert from "node:assert/strict";
import test from "node:test";
import { createPaneLayout } from "../app/web/js/features/shell/pane-layout.js";

function setup({mobile=false}={}) {
  const previous={window:globalThis.window,fetch:globalThis.fetch};
  const timers=new Map(); let timer=0;
  globalThis.window=Object.assign(new EventTarget(),{innerWidth:1440,
    setTimeout(fn){const id=++timer;timers.set(id,fn);return id;},
    clearTimeout(id){timers.delete(id);}});
  const shell={dataset:{sidebar:mobile?"closed":"open"},style:{setProperty(){}}};
  function handle() {
    const values=new Map(); return Object.assign(new EventTarget(),{dataset:{},
      getAttribute:key=>values.get(key),setAttribute:(key,value)=>values.set(key,value),focus(){}});
  }
  const sidebar=handle(),calls=[],applied=[];
  const mobileLayout=Object.assign(new EventTarget(),{matches:mobile});
  globalThis.fetch=(url,options)=>new Promise(resolve=>calls.push({url,options,resolve}));
  const layout=createPaneLayout({shell,mobileLayout,
    sidebarHandle:sidebar,onLoaded:saved=>applied.push(saved)});
  return {layout,shell,sidebar,calls,applied,
    runTimers(){const work=[...timers.values()];timers.clear();for(const fn of work)fn();},
    finish(){Object.assign(globalThis,previous);}};
}
const saved={sidebar_width:360,sidebar_open:true};
const response=data=>Response.json({ok:true,data});
const tick=()=>new Promise(setImmediate);

test("a toggle before first read preserves the other saved fields and cannot write defaults", async () => {
  const ctx=setup();
  try {
    const loading=ctx.layout.load(); ctx.shell.dataset.sidebar="closed";
    ctx.layout.remember(false);ctx.runTimers();await tick();
    assert.equal(ctx.calls.length,1);assert.equal(ctx.calls[0].options.method,"GET");
    ctx.calls[0].resolve(response(saved));await loading;await tick();
    assert.deepEqual(ctx.applied,[{...saved,sidebar_open:false}]);
    assert.equal(ctx.calls.length,2);
    assert.deepEqual(JSON.parse(ctx.calls[1].options.body),{...saved,sidebar_open:false});
    ctx.calls[1].resolve(response({}));await tick();
  } finally {ctx.finish();}
});

test("a resize owns its width and open panel while untouched geometry still restores", async () => {
  const ctx=setup();
  try {
    const loading=ctx.layout.load();
    const key=new Event("keydown",{cancelable:true});Object.assign(key,{key:"ArrowRight",shiftKey:false});
    ctx.sidebar.dispatchEvent(key);ctx.runTimers();await tick();
    assert.equal(ctx.calls.length,1);
    ctx.calls[0].resolve(response({...saved,sidebar_open:false}));await loading;await tick();
    assert.deepEqual(ctx.applied,[{...saved,sidebar_width:280}]);
    assert.deepEqual(JSON.parse(ctx.calls[1].options.body),{...saved,sidebar_width:280});
    ctx.calls[1].resolve(response({}));await tick();
  } finally {ctx.finish();}
});

test("mobile drawer clicks do not save desktop preferences or suppress their first read", async () => {
  const ctx=setup({mobile:true});
  try {
    const loading=ctx.layout.load();ctx.shell.dataset.sidebar="open";
    ctx.layout.remember(true);ctx.runTimers();await tick();
    assert.equal(ctx.calls.length,1);
    ctx.calls[0].resolve(response({...saved,sidebar_open:false}));await loading;
    assert.deepEqual(ctx.applied,[{...saved,sidebar_open:false}]);
    assert.equal(ctx.shell.dataset.sidebar,"open"); assert.equal(ctx.layout.sidebarOpen(),false);
    ctx.runTimers();await tick();assert.equal(ctx.calls.length,1);
  } finally {ctx.finish();}
});

test("edits arriving during a save are serialized as one latest complete snapshot", async () => {
  const ctx=setup();
  try {
    const loading=ctx.layout.load();ctx.calls[0].resolve(response(saved));await loading;
    ctx.shell.dataset.sidebar="closed";ctx.layout.remember(false);
    ctx.runTimers();await tick();assert.equal(ctx.calls.length,2);
    const key=new Event("keydown",{cancelable:true});Object.assign(key,{key:"ArrowRight",shiftKey:false});
    ctx.sidebar.dispatchEvent(key);ctx.runTimers();await tick();assert.equal(ctx.calls.length,2);
    ctx.calls[1].resolve(response({}));await tick();assert.equal(ctx.calls.length,3);
    assert.deepEqual(JSON.parse(ctx.calls[2].options.body),{...saved,sidebar_open:false,sidebar_width:368});
    ctx.calls[2].resolve(response({}));await tick();
  } finally {ctx.finish();}
});
