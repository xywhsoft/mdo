import assert from "node:assert/strict";
import { createRemoteTransport } from "../app/web/js/api/remote-transport.js";

const peer = "a".repeat(32), runtime = "b".repeat(32), nonce = "c".repeat(32), device = "d".repeat(32);
const owner = "7", token = "e".repeat(32) + "-0";
const ticket = { path: "/api/v1/devices/connect", protocol: "xadmin.device-relay.v1", ticket: "f".repeat(64), payload_limit: 262123 };
const rudp = { version: 2, peer_id: peer, runtime_id: runtime, nonce, owner_id: owner, mode: "control",
  public_key: "1".repeat(64), scope: "2".repeat(32), binding: "3".repeat(64), families: [4, 6] };
const hello = { type: "hello", version: 1, runtime_id: runtime, mode: "control", upload_limit: 8388608,
  chunk_limit: 65536, window_bytes: 65536, live: true, live_limit: 2097152 };
const lan = { version: 1, peer_id: peer, token: "9".repeat(64), certificate: "test", port: 1, addresses: ["127.0.0.1"] };
class Socket {
  readyState = 1; sent = [];
  send(value) { this.sent.push(typeof value === "string" ? JSON.parse(value) : value); }
  close() { this.readyState = 3; }
  receive(value) { this.onmessage({ data: JSON.stringify(value) }); }
}
async function flush() { for (let i = 0; i < 6; i++) await Promise.resolve(); }
async function setup(overrides = {}, legacy = false, factory = null) {
  let serial = 0; const relay = new Socket(), natives = [], states = [], timers = [];
  const transport = createRemoteTransport({ socket: () => relay, id: () => (++serial).toString(16).padStart(32, "0"),
    directSocket: async (offer, options) => {
      const socket = new Socket(); natives.push({ socket, offer, options });
      return factory ? factory(socket, options, natives.length) : socket;
    },
    onState: value => states.push(value), setTimer: (fn, ms) => { const timer = { fn, ms, active: true }; timers.push(timer); return timer; },
    clearTimer: timer => { if (timer) timer.active = false; } });
  const connected = transport.connect({ origin: "https://ai.xywhsoft.com", ticket, deviceId: device, owner, mode: "control" });
  relay.receive({ type: "ready", version: 1, peer_id: peer, device_id: device, mode: "control", payload_limit: 262123 });
  relay.receive({ ...hello, rudp: { ...rudp, ...overrides }, ...(legacy ? { direct: lan } : {}) });
  await connected; await flush(); return { transport, relay, natives, states, timers };
}
function forward(socket, control) { socket.receive({ type: "rudp_forward", payload: { nonce, runtime_id: runtime, ...control } }); }
function ready(socket) { socket.receive({ type: "direct_ready", version: 1, runtime_id: runtime, peer_id: peer, mode: "control", transport: "xrudp" }); }

{
  const s = await setup(), direct = s.natives[0].socket;
  assert.deepEqual(s.natives[0].offer, { transport: "xrudp", offer: rudp });
  const initial = s.transport.fetch("/api/v1/projects").catch(error => error);
  assert.equal(s.relay.sent.filter(v => v.type === "request").length, 1, "relay stays usable during negotiation");
  forward(direct, { type: "rudp_init", public_key: "4".repeat(64), families: [4, 6] });
  forward(direct, { type: "rudp_candidates", family: 4, candidates: [{ ip: "127.0.0.1", port: 2222, kind: "host" }] });
  forward(direct, { type: "rudp_dial", family: 4, dialer: 2 });
  forward(direct, { type: "rudp_select", family: 4 });
  assert.deepEqual(s.relay.sent.slice(-4).map(v => v.type), ["rudp_init", "rudp_candidates", "rudp_dial", "rudp_select"]);
  const before = direct.sent.length;
  s.relay.receive({ type: "rudp_answer", nonce: "8".repeat(32), runtime_id: runtime, candidates: [] });
  assert.equal(direct.sent.length, before, "wrong attempt cannot signal the gateway");
  const answer = { type: "rudp_answer", nonce, runtime_id: runtime, family: 4, candidates: [{ ip: "127.0.0.1", port: 3333, kind: "host" }] };
  s.relay.receive(answer); assert.deepEqual(direct.sent.at(-1), { type: "rudp_signal", payload: answer });
  const selected = { type: "rudp_selected", nonce, runtime_id: runtime, family: 4 };
  s.relay.receive(selected); assert.deepEqual(direct.sent.at(-1), { type: "rudp_signal", payload: selected });
  ready(direct); assert.equal(s.states.at(-1).route, "xrudp");
  const write = s.transport.fetch("/api/v1/projects", { method: "POST", body: "{}", headers: { "X-Mdo-Write-Token": token } }).catch(error => error);
  assert.equal(direct.sent.at(-1).type, "request");
  direct.onclose(); const failure = await write;
  assert.equal(failure.code, "remote_result_unconfirmed");
  assert.equal(s.relay.sent.filter(v => v.type === "request" && v.method === "POST").length, 0, "uncertain write is never replayed");
  assert.equal(s.relay.sent.at(-1).type, "rudp_cancel");
  assert.equal(s.states.at(-1).route, "relay");
  s.transport.close(); await initial;
}
for (const override of [{ version: 1 }, { version: 3 }, { owner_id: "8" }, { peer_id: "0".repeat(32) }, { runtime_id: "0".repeat(32) }, { mode: "view" }, { nonce: "bad" }]) {
  const s = await setup(override); assert.equal(s.natives.length, 0, "authority mismatch must not open the native gate"); s.transport.close();
}
for (const control of [
  { type: "request", method: "POST" },
  { type: "rudp_init", public_key: "4".repeat(64), families: [4], access_token: "never-forward" },
  { type: "rudp_init", public_key: "4".repeat(64), families: [8] },
  { type: "rudp_init", public_key: "4".repeat(64), families: [4, 4] },
  { type: "rudp_init", public_key: "4".repeat(64), families: [] },
  { type: "rudp_select", family: 8 },
  { type: "rudp_dial", family: 4, dialer: 0 },
  { type: "rudp_dial", family: 4, dialer: 3 },
  { type: "rudp_dial", family: 8, dialer: 2 },
  { type: "rudp_dial", family: 4, dialer: 2, mode: "control" },
  { type: "rudp_selected", family: 4 },
  { type: "rudp_candidates", family: 4, candidates: [{ ip: "attacker.example", port: 1, kind: "host" }] },
  { type: "rudp_candidates", family: 4, candidates: [{ ip: "127.0.0.1", port: 0, kind: "host" }] },
]) {
  const s = await setup({}, true), direct = s.natives[0].socket;
  forward(direct, control); await flush();
  assert.ok(!s.relay.sent.some(v => v.access_token || v.type === control.type), "invalid native message cannot become a relay command");
  assert.deepEqual(s.natives[1].offer, lan, "failed native negotiation falls back to the existing LAN path"); s.transport.close();
}
for (const value of [
  { type: "rudp_selected", family: 8 },
  { type: "rudp_selected", family: 4, access_token: "never-forward" },
  { type: "rudp_answer", family: 4, candidates: [{ ip: "attacker.example", port: 2, kind: "host" }] },
  { type: "rudp_failed", code: "x".repeat(33) },
]) {
  const s = await setup({}, true), direct = s.natives[0].socket, before = direct.sent.length;
  s.relay.receive({ nonce, runtime_id: runtime, ...value }); await flush();
  assert.equal(direct.sent.length, before, "invalid target control must not reach the native gate");
  assert.equal(s.relay.readyState, 1, "malformed optional direct control leaves the relay usable");
  assert.deepEqual(s.natives[1].offer, lan); s.transport.close();
}
{
  const s = await setup({}, true), direct = s.natives[0].socket;
  const timer = s.timers.find(v => v.active && v.ms === 12000); assert.ok(timer); timer.fn(); await flush();
  assert.equal(direct.readyState, 3); assert.deepEqual(s.natives[1].offer, lan); s.transport.close();
}
{
  const s = await setup({}, true), direct = s.natives[0].socket;
  s.transport.close(); direct.receive({ type: "direct_ready", version: 1, runtime_id: runtime, peer_id: peer, mode: "control", transport: "xrudp" });
  await flush(); assert.equal(s.natives.length, 1, "late completion cannot reopen a retired account generation");
}
{
  let release;
  const s = await setup({}, true, (socket, options, count) => count === 1 ? new Promise(resolve => { release = () => resolve(socket); }) : socket);
  const first = s.natives[0], timer = s.timers.find(value => value.active && value.ms === 12000);
  assert.ok(timer, "whole-attempt deadline must start before the gateway factory resolves");
  timer.fn(); await flush();
  assert.ok(first.options.signal.aborted, "deadline aborts the local startup fetch");
  assert.deepEqual(s.natives[1].offer, lan, "pending gateway factory cannot prevent LAN fallback");
  release(); await flush();
  assert.equal(first.socket.readyState, 3, "late gateway is closed without replacing the current route");
  assert.equal(s.natives[1].socket.readyState, 1); assert.equal(s.relay.readyState, 1); s.transport.close();
}
{
  let release;
  const s = await setup({}, true, socket => new Promise(resolve => { release = () => resolve(socket); }));
  const first = s.natives[0], original = s.timers.find(value => value.active && value.ms === 12000);
  release(); await flush();
  assert.equal(s.timers.filter(value => value.ms === 12000).length, 1, "native startup must not renew the handshake budget");
  assert.ok(original.active); original.fn(); await flush();
  assert.equal(first.socket.readyState, 3); s.transport.close();
}
{
  let release;
  const s = await setup({}, true, socket => new Promise(resolve => { release = () => resolve(socket); }));
  const first = s.natives[0]; s.transport.close(); await flush();
  assert.ok(first.options.signal.aborted, "account generation cancellation aborts a pending startup");
  assert.ok(!s.timers.some(value => value.active));
  release(); await flush();
  assert.equal(first.socket.readyState, 3); assert.equal(s.natives.length, 1, "late factory cannot start a retired LAN attempt");
}
console.log("PASS rendezvous authority/bounds, complete startup deadline, abort/late completion, direct selection, fallback and no write replay");
