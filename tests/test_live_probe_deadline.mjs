import assert from "node:assert/strict";
import test from "node:test";
import { createLiveConnection, liveConnection } from "../app/web/js/api/live.js";
import { api, currentPageWriteToken } from "../app/web/js/api/client.js";

const token = `${"a".repeat(32)}-0`, fresh = `${"b".repeat(32)}-0`;
const flush = () => new Promise(resolve => setImmediate(resolve));
function fixture(options = {}) {
  const sockets = [], timers = new Map(), events = [];
  let serial = 0;
  const clock = {
    setTimeout(fn, delay) { timers.set(++serial, { fn, delay }); return serial; },
    clearTimeout(id) { timers.delete(id); },
  };
  function socket(url, protocols) {
    const value = { url, protocols, readyState: 1, closed: false, sent: [],
      close() { this.closed = true; this.onclose?.(); },
      send(raw) { this.sent.push(JSON.parse(raw)); },
      message(data) { this.onmessage({ data: JSON.stringify(data) }); } };
    sockets.push(value); return value;
  }
  const live = createLiveConnection({ url: () => "ws://127.0.0.1:1234/api/v1/live",
    socket, setTimer: clock.setTimeout, clearTimer: clock.clearTimeout, random: () => 0, ...options });
  live.subscribe(event => events.push(event));
  async function fire(delay) {
    const [id, timer] = [...timers].find(([, value]) => value.delay === delay) ?? [];
    assert(timer, `missing ${delay} ms timer`);
    timers.delete(id); timer.fn(); await flush();
  }
  function fail() { live.start(token); sockets.at(-1).message({ type: "ready", version: 1 }); sockets.at(-1).onclose(); }
  return { live, socket, clock, sockets, timers, events, fire, fail };
}

test("a probe ignoring abort times out and reconnects without adopting its late token", async () => {
  let calls = 0, signal, finish;
  const f = fixture({ refreshToken: value => {
    if (++calls > 1) return token;
    signal = value; return new Promise(resolve => { finish = resolve; });
  } });
  try {
    f.fail(); await f.fire(500); await f.fire(8000);
    assert.equal(signal.aborted, true); assert.equal(calls, 1);
    assert.deepEqual(f.events.at(-1), { type: "reachable", connected: false });
    await f.fire(1000); assert.equal(calls, 2); assert.equal(f.sockets.length, 2);
    f.sockets[1].message({ type: "ready", version: 1 });
    const count = f.events.length; finish(fresh); await flush();
    assert.equal(f.events.length, count); assert.equal(f.live.isConnected(), true);
    assert(!f.events.some(e => e.type === "runtime_changed"));
  } finally { f.live.stop(); }
});

test("pause and stop abort hanging probes and release their deadline immediately", async () => {
  for (const action of ["pause", "stop"]) {
    let signal;
    const f = fixture({ refreshToken: value => { signal = value; return new Promise(() => {}); } });
    try {
      f.fail(); await f.fire(500);
      if (action === "pause") f.live.pause(true); else f.live.stop();
      await flush(); assert.equal(signal.aborted, true); assert.equal(f.timers.size, 0);
      assert.equal(f.sockets.length, 1);
      assert(!f.events.some(e => e.type === "reachable"));
    } finally { f.live.stop(); }
  }
});

test("a replacement connection cancels its old probe without clearing the new watchdog", async () => {
  let signal, finish;
  const f = fixture({ refreshToken: value => { signal = value; return new Promise(resolve => { finish = resolve; }); } });
  try {
    f.fail(); await f.fire(500); f.live.start(fresh); await flush();
    assert.equal(signal.aborted, true); assert.equal(f.sockets.length, 2);
    assert.deepEqual(f.sockets[1].protocols, ["mdo.live.v1", `mdo.token.${fresh}`]);
    assert.equal(f.timers.size, 1); assert.equal([...f.timers.values()][0].delay, 10000);
    finish(token); await flush(); assert.equal(f.timers.size, 1);
    assert(!f.events.some(e => e.type === "runtime_changed"));
  } finally { f.live.stop(); }
});

test("cancellation before a probe's HTTP turn sends no ghost read", async () => {
  let reads = 0;
  const f = fixture({ refreshToken: () => { reads++; return token; } });
  f.fail(); const [id, timer] = [...f.timers][0]; f.timers.delete(id); timer.fn();
  f.live.stop(); await flush(); assert.equal(reads, 0); assert.equal(f.timers.size, 0);
});

test("a timed-out probe's late rejection cannot disturb its healthy successor", async () => {
  let calls = 0, reject;
  const f = fixture({ refreshToken: () => ++calls === 1
    ? new Promise((_, fail) => { reject = fail; }) : token });
  try {
    f.fail(); await f.fire(500); await f.fire(8000); await f.fire(1000);
    f.sockets[1].message({ type: "ready", version: 1 });
    const count = f.events.length; reject(new TypeError("late failure")); await flush();
    assert.equal(f.events.length, count); assert.equal(f.live.isConnected(), true);
  } finally { f.live.stop(); }
});

test("repeated hung probes retain the existing capped exponential reconnect pace", async () => {
  let calls = 0;
  const f = fixture({ refreshToken: () => { calls++; return new Promise(() => {}); } });
  try {
    f.fail();
    for (const delay of [500, 1000, 2000, 4000, 8000, 15000, 15000]) {
      await f.fire(delay); await f.fire(8000);
      assert.equal(f.timers.size, 1); assert.equal(f.sockets.length, 1);
    }
    assert.equal(calls, 7);
    assert(f.events.filter(e => e.type === "reachable").every(e => e.connected === false));
  } finally { f.live.stop(); }
});

test("stopping from a runtime-change observer never reports a stale reachable state", async () => {
  const f = fixture({ refreshToken: () => fresh });
  f.live.subscribe(e => { if (e.type === "runtime_changed") f.live.stop(); });
  f.fail(); await f.fire(500);
  assert.equal(f.sockets.length, 1); assert.equal(f.timers.size, 0);
  assert(!f.events.some(e => e.type === "reachable"));
});

test("the exported live connection bounds real API JSON and retains the page's mutation fence", async () => {
  const previous = { window: globalThis.window, location: globalThis.location,
    WebSocket: globalThis.WebSocket, fetch: globalThis.fetch };
  const f = fixture(); let reads = 0, signal;
  globalThis.window = Object.assign(new EventTarget(), f.clock);
  globalThis.location = { href: "http://127.0.0.1:1234/" };
  globalThis.WebSocket = function(url, protocols) { return f.socket(url, protocols); };
  globalThis.fetch = async (path, options) => {
    assert.equal(path, "/api/v1/project-purge-intent"); assert.equal(options.method, "GET");
    if (++reads === 2) { signal = options.signal;
      return { ok: true, status: 200, headers: new Headers({ "X-Mdo-Write-Token": fresh }),
        json: () => new Promise(() => {}) }; }
    return Response.json({ ok: true, data: {} }, { headers: {
      "X-Mdo-Write-Token": reads === 1 ? token : fresh } });
  };
  const events = []; const off = liveConnection.subscribe(e => events.push(e));
  async function nextTimer() {
    const [id, timer] = [...f.timers].sort((a, b) => a[1].delay - b[1].delay)[0];
    f.timers.delete(id); timer.fn(); await flush();
  }
  try {
    await api.get("/project-purge-intent"); assert.equal(currentPageWriteToken(), token);
    liveConnection.start(token); f.sockets[0].message({ type: "ready", version: 1 });
    f.sockets[0].onclose(); await nextTimer(); await nextTimer();
    assert.equal(signal.aborted, true);
    assert.deepEqual(events.at(-1), { type: "reachable", connected: false });
    await nextTimer(); assert.equal(reads, 3); assert.equal(f.sockets.length, 2);
    assert.deepEqual(f.sockets[1].protocols, ["mdo.live.v1", `mdo.token.${fresh}`]);
    assert.equal(currentPageWriteToken(), token);
  } finally { liveConnection.stop(); off(); f.live.stop(); Object.assign(globalThis, previous); }
});
