import assert from "node:assert/strict";
import test from "node:test";
import { createLiveConnection } from "../app/web/js/api/live.js";

const token = `${"a".repeat(32)}-0`;
const flush = () => new Promise(resolve => setImmediate(resolve));
function fixture(options = {}) {
  const sockets = [], timers = new Map(), events = [];
  let timer = 0;
  const live = createLiveConnection({
    url: () => "ws://127.0.0.1:1234/api/v1/live",
    socket(url, protocols) {
      const connection = { url, protocols, readyState: 1, sent: [], closed: false,
        send(value) { this.sent.push(JSON.parse(value)); },
        close() { this.closed = true; this.onclose?.(); },
        message(value) { this.onmessage({ data: JSON.stringify(value) }); } };
      sockets.push(connection); return connection;
    },
    setTimer(fn, delay) { timers.set(++timer, { fn, delay }); return timer; },
    clearTimer(id) { timers.delete(id); }, random: () => 0,
    ...options,
  });
  live.subscribe((event) => events.push(event));
  const fire = (delay) => {
    const entry = [...timers].find(([, item]) => item.delay === delay);
    assert.ok(entry, `missing ${delay} ms timer`);
    timers.delete(entry[0]); return entry[1].fn();
  };
  return { live, sockets, timers, events, fire };
}

test("same-origin token and ready gate protect a single connection", () => {
  const { live, sockets, events } = fixture();
  live.start("invalid"); assert.equal(sockets.length, 0);
  live.start(token); live.start(token); assert.equal(sockets.length, 1);
  assert.deepEqual(sockets[0].protocols, ["mdo.live.v1", `mdo.token.${token}`]);
  assert.equal(live.isConnected(), false);
  sockets[0].message({ type: "changed" }); assert.equal(events.length, 0);
  sockets[0].message({ type: "ready", version: 1 });
  assert.equal(live.isConnected(), true);
  assert.deepEqual(events, [{ type: "status", connected: true }]);
  live.stop(); assert.equal(live.isConnected(), false);
});

test("restart renews only the read socket and announces the stale page before connecting", async () => {
  const fresh = `${"b".repeat(32)}-0`;
  let reads = 0;
  const f = fixture({ refreshToken: async () => { reads += 1; return fresh; } });
  f.live.start(token); f.sockets[0].message({ type: "ready", version: 1 });
  f.sockets[0].onclose(); f.fire(500);
  await flush();
  assert.equal(reads, 1);
  assert.equal(f.sockets.length, 2);
  assert.deepEqual(f.sockets[1].protocols, ["mdo.live.v1", `mdo.token.${fresh}`]);
  assert.deepEqual(f.events.slice(-2), [
    { type: "runtime_changed", reason: "restart" }, { type: "reachable", connected: true },
  ]);
  f.live.stop();
});

test("unreachable read-token probe backs off without opening stale sockets", async () => {
  let reachable = false;
  const f = fixture({ refreshToken: async () => {
    if (!reachable) throw new TypeError("offline");
    return token;
  } });
  f.live.start(token); f.sockets[0].message({ type: "ready", version: 1 });
  f.sockets[0].onclose(); f.fire(500);
  await flush();
  assert.equal(f.sockets.length, 1);
  assert.deepEqual(f.events.at(-1), { type: "reachable", connected: false });
  reachable = true; f.fire(1000);
  await flush();
  assert.equal(f.sockets.length, 2);
  assert.ok(!f.events.some(event => event.type === "runtime_changed"));
  f.live.stop();
});

test("an HTTP transport failure rechecks a seemingly open socket only once", async () => {
  const f = fixture({ refreshToken: async () => token });
  f.live.start(token); f.sockets[0].message({ type: "ready", version: 1 });
  f.live.recheck(); f.live.recheck();
  assert.equal(f.live.isConnected(), false);
  assert.equal(f.sockets[0].closed, true);
  assert.deepEqual(f.events.at(-1), { type: "status", connected: false });
  f.fire(500); await flush();
  assert.equal(f.sockets.length, 2);
  f.live.stop();
});

test("pause or stop invalidates delayed token probes and never reconnects in the background", async () => {
  for (const action of ["pause", "stop"]) {
    let resolve;
    const f = fixture({ refreshToken: () => new Promise(done => { resolve = done; }) });
    f.live.start(token); f.sockets[0].message({ type: "ready", version: 1 });
    f.sockets[0].onclose(); f.fire(500);
    await flush();
    if (action === "pause") f.live.pause(true); else f.live.stop();
    resolve(`${"b".repeat(32)}-0`);
    await flush();
    assert.equal(f.sockets.length, 1);
    assert.ok(!f.events.some(event => event.type === "runtime_changed"));
    f.live.stop();
  }
});

test("reconnect reads the latest offline cursor and ignores the old socket", () => {
  const { live, sockets, events, fire } = fixture();
  let cursor = 4;
  live.select("default", "a", () => cursor);
  live.start(token); sockets[0].message({ type: "ready", version: 1 });
  assert.equal(sockets[0].sent[0].after, 4);
  sockets[0].onclose(); cursor = 9;
  assert.equal(live.isConnected(), false);
  fire(500); assert.equal(sockets.length, 2);
  sockets[1].message({ type: "ready", version: 1 });
  assert.equal(sockets[1].sent[0].after, 9);
  const before = events.length;
  sockets[0].message({ type: "changed" }); assert.equal(events.length, before);
  live.stop();
});

test("selection generation rejects delayed packets even when returning to the same session", () => {
  const { live, sockets, events } = fixture();
  live.start(token); sockets[0].message({ type: "ready", version: 1 });
  live.select("qa", "a", () => 2);
  const old = sockets[0].sent.at(-1).selection;
  live.select("qa", "b"); live.select("qa", "a", () => 7);
  const current = sockets[0].sent.at(-1).selection;
  const before = events.length;
  sockets[0].message({ type: "events", project_id: "qa", session_id: "a", selection: old });
  assert.equal(events.length, before);
  sockets[0].message({ type: "events", project_id: "qa", session_id: "a", selection: current });
  assert.equal(events.at(-1).selection, current);
  live.stop();
});

test("heartbeat failure reconnects, and hidden pages release connections and timers", () => {
  const { live, sockets, timers, fire } = fixture();
  live.start(token); sockets[0].message({ type: "ready", version: 1 });
  sockets[0].message({ type: "ping" });
  assert.deepEqual(sockets[0].sent.at(-1), { type: "pong" });
  fire(45_000); assert.equal(live.isConnected(), false);
  live.pause(true); assert.equal(timers.size, 0);
  live.pause(false); assert.equal(sockets.length, 2);
  live.pause(true); assert.ok(sockets[1].closed); assert.equal(timers.size, 0);
  live.stop();
});

test("failed handshakes use bounded exponential backoff and stop cancels retries", () => {
  const { live, sockets, timers, fire } = fixture();
  live.start(token); sockets[0].onerror(); fire(500);
  sockets[1].onerror(); fire(1000); sockets[2].onerror();
  assert.ok([...timers.values()].some(({ delay }) => delay === 2000));
  live.stop(); assert.equal(timers.size, 0);
});
