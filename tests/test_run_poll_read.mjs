import assert from "node:assert/strict";
import test from "node:test";
import { readFile } from "node:fs/promises";
import vm from "node:vm";
import { createRunPollRead } from "../app/web/js/features/chat/run-poll-read.js";
import { createRequestRecovery } from "../app/web/js/api/request-recovery.js";
import { readRun } from "../app/web/js/state/runs.js";
import { isTransientReadError } from "../app/web/js/api/read-recovery.js";

const active = { id: "run-poll-a", project_id: "qa", session_id: "a", terminal: false };
const complete = { ...active, terminal: true, state: "succeeded" };
const flush = () => new Promise(resolve => setImmediate(resolve));
function clock() {
  let time = 0, serial = 0;
  const timers = new Map();
  const setTimer = (fn, delay) => { timers.set(++serial, { fn, at: time + delay }); return serial; };
  const clearTimer = id => timers.delete(id);
  return { timers, now: () => time, setTimer, clearTimer,
    async step() {
      const [id, timer] = [...timers].sort((a, b) => a[1].at - b[1].at)[0] ?? [];
      if (!timer) return false;
      timers.delete(id); time = timer.at; void timer.fn(); await flush(); return true;
    },
    createRecovery: () => createRequestRecovery({ now: () => time, setTimer, clearTimer,
      random: () => 0, eventTarget: null }),
  };
}

test("one hanging run read expires at eight seconds without repeating it", async () => {
  const c = clock(); let signal, reads = 0;
  const poll = createRunPollRead({ createRecovery: c.createRecovery,
    read: (_id, options) => { reads++; signal = options.signal; return new Promise(() => {}); } });
  const outcome = poll.read(active).catch(error => error); await flush();
  await c.step();
  assert.equal((await outcome).code, "network_error");
  assert.equal(c.now(), 8000); assert.equal(reads, 1); assert.equal(signal.aborted, true);
  assert.equal(c.timers.size, 0);
});

test("cancel releases an uncooperative read and ignores its late terminal state", async () => {
  const c = clock(); let signal, finish;
  const poll = createRunPollRead({ createRecovery: c.createRecovery,
    read: (_id, options) => { signal = options.signal; return new Promise(resolve => { finish = resolve; }); } });
  const outcome = poll.read(active).catch(error => error); await flush(); poll.cancel();
  assert.equal((await outcome).name, "AbortError"); assert.equal(signal.aborted, true);
  finish(complete); await flush(); assert.equal(c.timers.size, 0);
});

test("a new run cancels the old read without old cleanup cancelling its successor", async () => {
  const c = clock(); const signals = [], finishes = [];
  const poll = createRunPollRead({ createRecovery: c.createRecovery,
    read: (_id, options) => { signals.push(options.signal); return new Promise(resolve => finishes.push(resolve)); } });
  const first = poll.read(active).catch(error => error); await flush();
  const other = { ...active, id: "run-poll-b", session_id: "b" };
  const second = poll.read(other); await flush();
  assert.equal((await first).name, "AbortError"); assert.equal(signals[0].aborted, true);
  assert.equal(signals[1].aborted, false); assert.equal(c.timers.size, 1);
  finishes[0](complete); finishes[1]({ ...other, terminal: true });
  assert.equal((await second).id, other.id); assert.equal(c.timers.size, 0);
});

test("cancelling before the HTTP turn sends no ghost request", async () => {
  const c = clock(); let reads = 0;
  const poll = createRunPollRead({ createRecovery: c.createRecovery,
    read: async () => { reads++; return active; } });
  const outcome = poll.read(active).catch(error => error); poll.cancel();
  assert.equal((await outcome).name, "AbortError"); assert.equal(reads, 0);
  assert.equal(c.timers.size, 0);
});

test("a response proves only the exact run, project, session and boolean boundary", async () => {
  for (const response of [{ ...complete, id: "run-other" }, { ...complete, project_id: "other" },
    { ...complete, session_id: "other" }, { ...complete, terminal: "true" }, null]) {
    const c = clock(); const poll = createRunPollRead({ createRecovery: c.createRecovery,
      read: async () => response });
    await assert.rejects(poll.read(active), error => error.code === "invalid_response");
    assert.equal(c.timers.size, 0);
  }
});

test("the real run API adapter aborts fetch when its bounded wait expires", async () => {
  const c = clock(); const original = globalThis.fetch; let signal;
  globalThis.fetch = async (_url, options) => { signal = options.signal; return new Promise(() => {}); };
  try {
    const poll = createRunPollRead({ createRecovery: c.createRecovery, read: readRun });
    const outcome = poll.read(active).catch(error => error); await flush(); await c.step();
    assert.equal((await outcome).code, "network_error"); assert.equal(signal.aborted, true);
  } finally { globalThis.fetch = original; }
});

// Exercise the application's actual fallback scheduler, not a second copy of
// its algorithm. Only browser/services are fixtures; the function is unchanged.
const app = await readFile(new URL("../app/web/js/app.js", import.meta.url), "utf8");
const scheduler = app.slice(app.indexOf("  function scheduleRunPoll("), app.indexOf("  function monitorRun("));
function schedulerFixture(read) {
  const c = clock(), applied = [], errors = [];
  const poll = createRunPollRead({ createRecovery: c.createRecovery, read });
  const context = vm.createContext({ activeRun: active, selectedKey: "qa/a", routeVersion: 1,
    settingsActive: false, schedulesActive: false,
    runMonitor: 0, readRun: read, readRunForMonitor: owner => poll.read(owner), runPollReads: poll,
    window: { setTimeout: c.setTimer, clearTimeout: c.clearTimer },
    document: { hidden: false, activeElement: null, body: {}, querySelector: () => null },
    liveConnection: { isConnected: () => false }, isTransientReadError,
    terminalState: run => Boolean(run?.terminal), stop: {}, prompt: { disabled: false },
    shell: { dataset: {} }, showComposerError: error => errors.push(error),
    setRun: run => { applied.push(run); context.activeRun = run?.terminal ? null : run; },
    refreshSelectedTimeline: async () => {}, refreshSelectedAsks: async () => {},
    loadSessions: async () => {}, loadRuns: async () => {}, loadTasks: async () => {},
    loadRecovery: async () => {}, refreshSelectedQueue: async () => {},
  });
  new vm.Script(scheduler).runInContext(context);
  return { c, poll, applied, errors, context, schedule: delay => context.scheduleRunPoll(delay) };
}

test("the real fallback scheduler releases a hanging GET and backs off quietly", async () => {
  let signal, calls = 0;
  const f = schedulerFixture((_id, options = {}) => {
    signal = options.signal;
    if (++calls === 1) return new Promise(() => {});
    return Promise.resolve(complete);
  });
  f.schedule(300); await f.c.step(); await f.c.step();
  assert.equal(signal?.aborted, true); assert.equal(f.c.now(), 8300);
  assert.equal(f.errors.length, 0); assert.equal(f.context.activeRun.id, active.id);
  await f.c.step();
  assert.equal(calls, 2); assert.equal(f.context.activeRun, null);
  assert.equal(f.applied[0]?.id, active.id); assert.equal(f.errors.length, 0);
});

test("the real scheduler never adopts a wrong-session run response", async () => {
  const f = schedulerFixture(async () => ({ ...complete, session_id: "other" }));
  f.schedule(300); await f.c.step();
  assert.equal(f.applied.some(run => run?.session_id === "other"), false);
  assert.equal(f.errors.length, 1); assert.equal(f.errors[0].code, "invalid_response");
});

test("explicit lifecycle cancellation is silent in the real scheduler", async () => {
  let signal;
  const f = schedulerFixture((_id, options = {}) => { signal = options.signal; return new Promise(() => {}); });
  f.schedule(300); await f.c.step(); f.poll.cancel(); await flush();
  assert.equal(signal?.aborted, true); assert.equal(f.errors.length, 0);
  assert.equal(f.context.activeRun.id, active.id); assert.equal(f.c.timers.size, 0);
});

test("transient polling failures back off to fifteen seconds without failing the run", async () => {
  let calls = 0;
  const f = schedulerFixture(async () => {
    calls++; throw Object.assign(new Error("temporary limit"), { status: 429 });
  });
  f.schedule(2500);
  const waits = [];
  for (let i = 0; i < 6; ++i) {
    const before = f.c.now(); await f.c.step(); waits.push(f.c.now() - before);
  }
  assert.deepEqual(waits, [2500, 5000, 10000, 15000, 15000, 15000]);
  assert.equal(calls, 6); assert.equal(f.errors.length, 0);
  assert.equal(f.context.activeRun.id, active.id);
  assert.equal(f.applied.length, 0); f.c.clearTimer(f.context.runMonitor);
});

test("permanent polling denials retain their precise error without retry", async () => {
  const denied = Object.assign(new Error("not authorized"), { status: 403, code: "permission_denied" });
  let calls = 0;
  const f = schedulerFixture(async () => { calls++; throw denied; });
  f.schedule(300); await f.c.step();
  assert.equal(f.errors[0], denied); assert.equal(calls, 1); assert.equal(f.c.timers.size, 0);
});

test("hidden pages, utility views and healthy live connections do not start fallback reads", async () => {
  for (const mutate of [f => { f.context.document.hidden = true; },
    f => { f.context.settingsActive = true; }, f => { f.context.schedulesActive = true; },
    f => { f.context.liveConnection.isConnected = () => true; }]) {
    let calls = 0;
    const f = schedulerFixture(async () => { calls++; return active; }); mutate(f); f.schedule(300);
    assert.equal(calls, 0); assert.equal(f.c.timers.size, 0);
  }
});
