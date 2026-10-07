import assert from "node:assert/strict";
import test from "node:test";
import { isTransientReadError, needsRecoveryCard } from "../app/web/js/api/read-recovery.js";
import { api, setApiNetworkErrorHandler } from "../app/web/js/api/client.js";
import { createRemoteTransport } from "../app/web/js/api/remote-transport.js";

test("background transport failures never imply an unfinished reply", () => {
  const idle = { status: "error", error: { code: "network_error" },
    data: { resume_required: false, items: [] } };
  assert.equal(needsRecoveryCard(idle, "qa", "a"), false);
  assert.equal(needsRecoveryCard({ ...idle, error: { status: 503 } }, "qa", "a"), false);
  assert.equal(needsRecoveryCard({ ...idle, error: { status: 403 } }, "qa", "a"), true);
  assert.equal(isTransientReadError({ code: "remote_revoked", status: 403 }), false);
  for (const code of ["remote_protocol", "remote_scope", "remote_result_unconfirmed"])
    assert.equal(isTransientReadError({ code }), false);
});

test("an actual remote read deadline is recoverable while an admitted write remains uncertain", async () => {
  const timers = new Map();
  let serial = 0, timerSerial = 0, socket;
  const transport = createRemoteTransport({
    id: () => (++serial).toString(16).padStart(32, "0"),
    setTimer: (fn, delay) => { timers.set(++timerSerial, { fn, delay }); return timerSerial; },
    clearTimer: id => timers.delete(id),
    socket: () => (socket = { readyState: 1, sent: [],
      send(value) { this.sent.push(JSON.parse(value)); }, close() { this.readyState = 3; } }),
  });
  const connecting = transport.connect({ origin: "https://ai.xywhsoft.com",
    deviceId: "d".repeat(32), mode: "control", ticket: {
      path: "/api/v1/devices/connect", protocol: "xadmin.device-relay.v1",
      ticket: "a".repeat(64), payload_limit: 262123 } });
  socket.onmessage({ data: JSON.stringify({ type: "ready", version: 1,
    peer_id: "f".repeat(32), device_id: "d".repeat(32), mode: "control", payload_limit: 262123 }) });
  socket.onmessage({ data: JSON.stringify({ type: "hello", version: 1,
    runtime_id: "e".repeat(32), mode: "control", upload_limit: 8388608,
    chunk_limit: 65536, window_bytes: 65536, live: true, live_limit: 2097152 }) });
  await connecting;
  try {
    const read = transport.fetch("/api/v1/runs");
    const checkedRead = assert.rejects(read, error => {
      assert.equal(error.code, "remote_timeout");
      assert.equal(isTransientReadError(error), true);
      assert.equal(needsRecoveryCard({ status: "error", error,
        data: { resume_required: false } }, "qa", "a"), false);
      return true;
    });
    [...timers.values()].find(timer => timer.delay === 75000).fn();
    await checkedRead;
    assert.equal(transport.uncertain().length, 0);
    assert.equal(socket.sent.filter(message => message.type === "request").length, 1);
    assert.equal(socket.sent.at(-1).type, "cancel");

    const write = transport.fetch("/api/v1/projects", { method: "POST" });
    const checkedWrite = assert.rejects(write, error => {
      assert.equal(error.code, "remote_result_unconfirmed");
      assert.equal(isTransientReadError(error), false);
      return true;
    });
    [...timers.values()].find(timer => timer.delay === 75000).fn();
    await checkedWrite;
    assert.equal(transport.uncertain().length, 1);
    assert.equal(socket.sent.filter(message => message.type === "request").length, 2,
      "timeout classification never resubmits a mutation");
  } finally { transport.close(); }
});

test("a known interrupted response stays visible while its current inspection is offline", () => {
  const pending = { status: "error", error: { code: "network_error" },
    data: { project_id: "qa", session_id: "a", resume_required: true } };
  assert.equal(needsRecoveryCard(pending, "qa", "a"), true);
  assert.equal(needsRecoveryCard(pending, "qa", "b"), false);
  assert.equal(needsRecoveryCard(pending, "other", "a"), false);
  assert.equal(needsRecoveryCard(pending, "qa", ""), false);
  assert.equal(needsRecoveryCard({ ...pending,
    data: { ...pending.data, unavailable: true } }, "qa", "a"), false);
});

test("HTTP network detection signals recovery without replaying the original mutation", async () => {
  const oldFetch = globalThis.fetch;
  let signals = 0, requests = 0;
  setApiNetworkErrorHandler(() => signals += 1);
  globalThis.fetch = async () => { requests += 1; throw new TypeError("offline"); };
  try {
    await assert.rejects(api.post("/draft", { text: "kept" }), { code: "network_error" });
    assert.equal(signals, 1); assert.equal(requests, 1);
    globalThis.fetch = async () => { throw new DOMException("cancelled", "AbortError"); };
    await assert.rejects(api.get("/bootstrap"), { name: "AbortError" });
    assert.equal(signals, 1);
    globalThis.fetch = async () => Response.json({ ok: false,
      error: { code: "access_denied", message: "denied" } }, { status: 403 });
    await assert.rejects(api.get("/bootstrap"), { code: "access_denied" });
    assert.equal(signals, 1);
  } finally { globalThis.fetch = oldFetch; setApiNetworkErrorHandler(null); }
});
