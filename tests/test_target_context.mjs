import assert from "node:assert/strict";

// Exercise the actual transport selection and connector, with network peers
// replaced at their public boundaries. No DOM or implementation-text assertions.
const saved = new Map(), calls = [], events = new Map();
globalThis.sessionStorage = { getItem: key => saved.get(key) || null,
  setItem: (key, value) => saved.set(key, value), removeItem: key => saved.delete(key) };
globalThis.window = { addEventListener(type, callback) {
  if (!events.has(type)) events.set(type, []); events.get(type).push(callback);
} };
let reloads = 0;
globalThis.location = { hash: "#/projects/local/sessions/local", reload() { reloads++; } };
globalThis.history = { state: { mdoWorkspace: "old workspace" }, replaceState(state, _, hash) { this.state = state; location.hash = hash; } };
const device = "d".repeat(32), selected = { id: device, owner: "2", name: "目标电脑", mode: "control" };
const token = "a".repeat(32) + "-0", job = "c".repeat(32);
let owner = 2, runtime = "e".repeat(32), sockets = [];
globalThis.fetch = async (path, options = {}) => {
  calls.push({ path, method: options.method || "GET" });
  let data = { local: true };
  if (path === "/api/v1/account") data = { state: "signed_in", profile: { id: owner }, origin: "https://ai.xywhsoft.com" };
  if (path === "/api/v1/connector/devices") data = { job: { id: job } };
  if (path === "/api/v1/connector/state") data = { job: { id: job, state: "done" } };
  if (path === "/api/v1/connector/ticket") data = { ticket: "a".repeat(64), path: "/api/v1/devices/connect",
    protocol: "xadmin.device-relay.v1", payload_limit: 262123 };
  return Response.json({ ok: true, data }, { headers: { "X-Mdo-Write-Token": token } });
};
globalThis.WebSocket = class {
  readyState = 1; sent = [];
  constructor(url, protocols) {
    this.url = url; this.protocols = protocols; sockets.push(this);
    if (url.includes("/devices/connect")) queueMicrotask(() => {
      this.receive({ type: "ready", version: 1, device_id: device, mode: "control", peer_id: "f".repeat(32), payload_limit: 262123 });
      this.receive({ type: "hello", version: 1, runtime_id: runtime, mode: "control", upload_limit: 8388608,
        chunk_limit: 65536, window_bytes: 65536, live: true, live_limit: 2097152 });
    });
  }
  send(value) { this.sent.push(typeof value === "string" ? JSON.parse(value) : value); }
  close() { this.readyState = 3; }
  receive(value) { this.onmessage?.({ data: JSON.stringify(value) }); }
};
const module = () => import(`../app/web/js/api/target.js?context=${Math.random()}`);
const local = await module();
assert.equal((await (await local.targetFetch("/api/v1/settings")).json()).data.local, true);
const localLive = local.targetLiveSocket("ws://127.0.0.1/api/v1/live", ["mdo.live.v1"]);
assert.equal(localLive.url, "ws://127.0.0.1/api/v1/live");
local.setTargetSwitchGuard(async () => false);
await assert.rejects(local.requestTargetSwitch(selected), error => error.code === "target_switch_busy");
assert.equal(reloads, 0); assert.equal(saved.has("mdo.target.v1"), false);
local.setTargetSwitchGuard(async () => true);
await local.requestTargetSwitch(selected);
assert.equal(reloads, 1); assert.equal(history.state.mdoWorkspace, null);
assert.equal(JSON.parse(saved.get("mdo.target.routes.v1")).local, "#/projects/local/sessions/local");
await assert.rejects(local.targetFetch("/api/v1/settings"), error => error.code === "target_changing");
assert.throws(() => local.targetLiveSocket("ws://127.0.0.1/api/v1/live", []), error => error.code === "target_changing");

const remote = await module();
const beforeOffline = calls.length;
await assert.rejects(remote.targetFetch("/api/v1/settings"), error => error.code === "remote_offline");
await assert.rejects(remote.targetFetch("/api/v1/update/install", { method: "post" }), error => error.code === "remote_native_unavailable");
assert.equal(calls.length, beforeOffline, "a remote failure must never invoke a local business route");
owner = 3;
await remote.initializeTarget();
assert.equal(remote.targetState().error.code, "connector_account_changed");
assert.equal(calls.at(-1).path, "/api/v1/account");
assert.equal(sockets.length, 1, "an account mismatch must not obtain or consume a target ticket");
owner = 2;
await remote.initializeTarget();
assert.equal(remote.targetState().connected, true);
const socket = sockets.at(-1), localCalls = calls.length;
const read = remote.targetFetch("/api/v1/settings");
const request = socket.sent.at(-1);
socket.receive({ type: "response", id: request.id, status: 204, headers: [] });
socket.receive({ type: "end", id: request.id, bytes: 0 });
assert.equal((await read).status, 204);
assert.equal(calls.length, localCalls, "target business operations use the relay, not controller HTTP");
socket.onclose({ code: 1006 });
const beforeDisconnected = calls.length;
await assert.rejects(remote.targetFetch("/api/v1/projects", { method: "POST" }), error => error.code === "remote_offline");
assert.equal(calls.length, beforeDisconnected);
assert.deepEqual(remote.targetState().selected, selected);
runtime = "b".repeat(32);
await remote.initializeTarget();
assert.equal(remote.targetState().runtimeChanged, true);
await assert.rejects(remote.targetFetch("/api/v1/settings", { method: "PUT" }), error => error.code === "remote_runtime_changed");
for (const callback of events.get("pagehide") || []) callback();
console.log("PASS target selection, account binding, route isolation, switch fence, native-only actions, no local fallback and changed runtime guard");
