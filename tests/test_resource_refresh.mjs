import assert from "node:assert/strict";
import test from "node:test";
import { readFile } from "node:fs/promises";
import vm from "node:vm";
import { createResourceRefresh } from "../app/web/js/features/shell/resource-refresh.js";
import { runAwaitingRelease } from "../app/web/js/features/chat/run-finishing.js";

const flush = () => new Promise(resolve => setImmediate(resolve));
function clock() {
  let id = 0;
  const timers = new Map();
  return { timers,
    setTimer(fn, delay) { timers.set(++id, { fn, delay }); return id; },
    clearTimer(key) { timers.delete(key); },
    async tick() { const batch = [...timers.values()]; timers.clear();
      for (const timer of batch) void timer.fn(); await flush(); },
  };
}

test("slow tasks and recovery never block a later completed run or queue", async () => {
  const c = clock(), reads = []; let finished = false;
  const scheduler = createResourceRefresh({ ...c, operations: {
    runs: () => reads.push(["runs", finished]), queue: () => reads.push(["queue", finished]),
    tasks: () => new Promise(() => {}), recovery: () => new Promise(() => {}),
  } });
  scheduler.request(); await c.tick(); finished = true;
  scheduler.request(); await c.tick();
  assert.deepEqual(reads, [["runs",false],["queue",false],["runs",true],["queue",true]]);
  scheduler.pause();
});

test("a busy lane coalesces bursts into one trailing read without overlapping", async () => {
  const c = clock(); let release, calls = 0;
  const scheduler = createResourceRefresh({ ...c, operations: {
    runs: () => ++calls === 1 ? new Promise(resolve => { release = resolve; }) : Promise.resolve(),
  } });
  scheduler.request(); await c.tick();
  for(let i=0;i<100;i++) scheduler.request();
  assert.equal(c.timers.size,0); assert.equal(calls,1);
  release(); await flush(); assert.equal(c.timers.size,1);
  await c.tick(); assert.equal(calls,2); assert.equal(c.timers.size,0);
});

test("one rejected read releases its own lane and cannot reject the scheduler", async () => {
  const c=clock();let calls=0;
  const scheduler=createResourceRefresh({...c,operations:{runs:()=>{calls++;throw Error("read failed");}}});
  scheduler.request();await c.tick();scheduler.request();await c.tick();assert.equal(calls,2);
});

test("page hide fences queued and late finishing reads; resume retains one pending refresh", async () => {
  const c=clock();let release,calls=0;
  const scheduler=createResourceRefresh({...c,operations:{runs:()=>++calls===1?new Promise(r=>{release=r;}):undefined}});
  scheduler.request();await c.tick();scheduler.request();scheduler.pause();
  release();await flush();assert.equal(c.timers.size,0);
  scheduler.request();assert.equal(c.timers.size,0);
  scheduler.resume();await c.tick();assert.equal(calls,2);
  scheduler.request();scheduler.pause();await c.tick();assert.equal(calls,2);
});

test("disabled refreshes send no reads until explicitly resumed", async () => {
  const c=clock();let enabled=false,calls=0;
  const scheduler=createResourceRefresh({...c,enabled:()=>enabled,operations:{runs:()=>calls++}});
  scheduler.request();assert.equal(c.timers.size,0);
  enabled=true;scheduler.resume();await c.tick();assert.equal(calls,1);
});

const app=await readFile(new URL("../app/web/js/app.js",import.meta.url),"utf8");
test("actual live-change wiring refreshes runs and queue while a task request hangs",async()=>{
  const c=clock(),reads=[];let completed=false;
  const begin=app.indexOf("  resourceRefresh = createResourceRefresh({");
  const end=app.indexOf("  function syncLocalConnection",begin);
  assert(begin>0 && end>begin);
  const context=vm.createContext({resourceRefresh:null,
    createResourceRefresh:options=>createResourceRefresh({...options,...c}),
    document:{hidden:false,activeElement:null},stop:{},routeVersion:1,
    liveConnection:{isConnected:()=>true},
    loadRuns:()=>reads.push(["runs",completed]),refreshSelectedQueue:()=>reads.push(["queue",completed]),
    loadTasks:()=>new Promise(()=>{}),loadApprovals(){},refreshSelectedAsks(){},loadRecovery(){},
    loadSessions(){},sessionMetadataSync:{refresh(){}},refreshSelectedTask(){}});
  new vm.Script(app.slice(begin,end)).runInContext(context);
  context.scheduleLiveRefresh();await c.tick();completed=true;
  context.scheduleLiveRefresh();await c.tick();
  assert.deepEqual(reads,[["runs",false],["queue",false],["runs",true],["queue",true]]);
  context.resourceRefresh.pause();
});
const fallback=app.slice(app.indexOf("  function scheduleRunsRefresh()"),app.indexOf("  runsStore.subscribe(scheduleRunsRefresh)"));
function fallbackFixture() {
  const c=clock(),requests=[];
  const context=vm.createContext({window:{setTimeout:c.setTimer,clearTimeout:c.clearTimer},
    document:{hidden:false},runsTimer:0,activeRun:null,
    navigation:{get:()=>({projectId:"p",sessionId:"s"})},
    runsStore:{get:()=>({status:"ready",data:{active_runs:1}})},
    promptQueue:{hasUnsettled:()=>false},liveConnection:{isConnected:()=>true},
    resourceRefresh:{request:names=>requests.push([...names])}});
  new vm.Script(fallback).runInContext(context);
  return {c,context,requests};
}
test("actual application reconciles outstanding work even with healthy live connection",async()=>{
  const f=fallbackFixture();f.context.scheduleRunsRefresh();
  assert.equal([...f.c.timers.values()][0].delay,2500);
  await f.c.tick();await f.c.tick();assert.deepEqual(f.requests,[["runs","queue"],["runs","queue"]]);
  f.context.document.hidden=true;f.context.scheduleRunsRefresh();assert.equal(f.c.timers.size,0);
});
test("queued messages keep reconciliation alive without an active run; idle live view does no polling",async()=>{
  const f=fallbackFixture();f.context.runsStore.get=()=>({data:{active_runs:0}});
  f.context.scheduleRunsRefresh();assert.equal(f.c.timers.size,0);
  f.context.promptQueue.hasUnsettled=()=>true;f.context.scheduleRunsRefresh();await f.c.tick();
  assert.deepEqual(f.requests,[["runs","queue"]]);
  f.context.promptQueue.hasUnsettled=()=>false;f.context.scheduleRunsRefresh();assert.equal(f.c.timers.size,0);
});

const run={terminal:false,agent_run_id:1,project_id:"p",session_id:"s",started_at:1000};
const timeline=events=>({projectId:"p",sessionId:"s",events});
const event=(kind,extra={})=>({kind,run_id:1,time:1001,agent_depth:0,...extra});
test("finishing label uses main Agent terminal evidence and preserves the backend admission boundary",()=>{
  assert.equal(runAwaitingRelease(run,timeline([event("model_done")])),false);
  assert.equal(runAwaitingRelease(run,timeline([event("agent_done")])),true);
  assert.equal(runAwaitingRelease(run,timeline([event("error")])),true);
  assert.equal(runAwaitingRelease({...run,terminal:true},timeline([event("agent_done")])),false);
});
test("old native IDs, children, other sessions and later turns cannot produce a false finishing label",()=>{
  for(const events of [[event("agent_done",{time:999})],[event("agent_done",{agent_depth:1})],
    [event("agent_done",{run_id:2})],[event("agent_done"),event("agent_start",{time:1002})]])
    assert.equal(runAwaitingRelease(run,timeline(events)),false);
  assert.equal(runAwaitingRelease(run,{...timeline([event("agent_done")]),sessionId:"other"}),false);
  assert.equal(runAwaitingRelease({...run,started_at:0},timeline([event("agent_done")])),false);
});
