import assert from "node:assert/strict";
import test from "node:test";
import { isTransientReadError, needsRecoveryCard } from "../app/web/js/api/read-recovery.js";
import { api, setApiNetworkErrorHandler } from "../app/web/js/api/client.js";

test("background transport failures never imply an unfinished reply", () => {
  const idle = { status: "error", error: { code: "network_error" },
    data: { resume_required: false, items: [] } };
  assert.equal(needsRecoveryCard(idle, "qa", "a"), false);
  assert.equal(needsRecoveryCard({ ...idle, error: { status: 503 } }, "qa", "a"), false);
  assert.equal(needsRecoveryCard({ ...idle, error: { status: 403 } }, "qa", "a"), true);
  assert.equal(isTransientReadError({ code: "remote_revoked", status: 403 }), false);
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
