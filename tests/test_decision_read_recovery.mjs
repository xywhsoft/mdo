import assert from "node:assert/strict";
import test from "node:test";
import { asksStore, clearAsks, selectAsks, refreshSelectedAsks, answerAsk } from "../app/web/js/state/asks.js";
import { approvalsStore, approvalDecisionStatus, loadApprovals, decideApproval } from "../app/web/js/state/approvals.js";
import { runsStore, loadRuns } from "../app/web/js/state/runs.js";
import { decisionReadError } from "../app/web/js/features/chat/decision-read-notice.js";
import { loadLocale } from "../app/web/js/i18n.js";
import { readFile } from "node:fs/promises";

const ok = data => Response.json({ ok: true, data });
const failed = () => Response.json({ ok: false, error: {
  code: "asks_unavailable", message: "temporarily unavailable" } }, { status: 503 });
const tick = () => new Promise(resolve => setImmediate(resolve));
const waitRecovery = () => new Promise(resolve => setTimeout(resolve, 650));
const question = { total: 1, items: [{ id: 7, question: "Continue?" }] };

test("conversation questions quietly recover and merge ordinary polling into the pending read", async () => {
  const original = globalThis.fetch;
  let reads = 0, fail = false; const signals = [], states = [];
  globalThis.fetch = async (_path, options) => {
    signals.push(options.signal); reads++;
    return fail ? (fail = false, failed()) : ok(question);
  };
  let unsubscribe;
  try {
    await selectAsks("qa", "first"); const saved = asksStore.get().data;
    unsubscribe = asksStore.subscribe(state => states.push(state.status));
    fail = true; await refreshSelectedAsks();
    assert.equal(asksStore.get().error, null); assert.equal(asksStore.get().data, saved);
    await refreshSelectedAsks(); await refreshSelectedAsks(); assert.equal(reads, 2);
    await waitRecovery(); assert.equal(reads, 3);
    assert.equal(asksStore.get().status, "ready"); assert.ok(!states.includes("error"));
    assert(signals.every(signal => signal instanceof AbortSignal));
  } finally { unsubscribe?.(); clearAsks(); globalThis.fetch = original; }
});

test("a permanent question refusal hides stale controls but keeps the selected read scope", async () => {
  const original = globalThis.fetch; let denied = false, reads = 0;
  globalThis.fetch = async path => {
    assert.equal(path, "/api/v1/projects/qa/sessions/first/asks"); reads++;
    return denied ? Response.json({ ok: false, error: { code: "permission_denied", message: "denied" } }, { status: 403 }) : ok(question);
  };
  try {
    await selectAsks("qa", "first"); denied = true;
    await refreshSelectedAsks(); assert.equal(asksStore.get().status, "error");
    assert.deepEqual(asksStore.get().data.items, []);
    denied = false; await refreshSelectedAsks(); assert.equal(reads, 2);
    await refreshSelectedAsks({ retry: true });
    assert.equal(reads, 3); assert.equal(asksStore.get().data.sessionId, "first");
    assert.equal(asksStore.get().data.items.length, 1);
  } finally { clearAsks(); globalThis.fetch = original; }
});

test("an accepted answer is not delayed by a hanging secondary read", async () => {
  const original = globalThis.fetch; let puts = 0, readSignal;
  globalThis.fetch = async () => ok(question);
  await selectAsks("qa", "first");
  globalThis.fetch = async (_path, options) => {
    if (options.method === "PUT") { puts++; return ok({ accepted: true }); }
    readSignal = options.signal;
    return new Promise((_resolve, reject) => options.signal?.addEventListener("abort", () =>
      reject(new DOMException("cancelled", "AbortError")), { once: true }));
  };
  try {
    const reply = await Promise.race([answerAsk("qa", "first", 7, "continue"),
      new Promise(resolve => setTimeout(() => resolve("blocked"), 100))]);
    assert.notEqual(reply, "blocked"); assert.equal(puts, 1);
    await tick(); assert(readSignal instanceof AbortSignal);
  } finally { clearAsks(); globalThis.fetch = original; }
});

test("an accepted approval keeps its one-shot guard while failed refreshes recover", async () => {
  const original = globalThis.fetch; let puts = 0, reads = 0;
  approvalsStore.reset();
  globalThis.fetch = async (_path, options) => {
    if (options.method === "PUT") { puts++; return ok({ decision: "allow" }); }
    reads++; return reads === 1 ? failed() : ok({ total: 0, items: [] });
  };
  try {
    assert.equal(await decideApproval("701", "allow"), true); await tick();
    assert.equal(approvalDecisionStatus("701"), "submitted");
    assert.equal(approvalsStore.get().error, null);
    assert.equal(await decideApproval("701", "deny"), false); assert.equal(puts, 1);
    await loadApprovals(); await loadApprovals(); assert.equal(reads, 1);
    await waitRecovery(); assert.equal(reads, 2);
    assert.equal(approvalDecisionStatus("701"), "idle");
  } finally { approvalsStore.reset(); globalThis.fetch = original; }
});

test("a malformed approval snapshot cannot release an already accepted decision", async () => {
  const original = globalThis.fetch; let reads = 0, puts = 0;
  approvalsStore.reset();
  globalThis.fetch = async (_path, options) => {
    if (options.method === "PUT") { puts++; return ok({ decision: "deny" }); }
    return ok(++reads === 1 ? {} : { items: [], total: 0 });
  };
  try {
    assert.equal(await decideApproval("702", "deny"), true); await tick();
    assert.equal(approvalDecisionStatus("702"), "submitted");
    assert.equal(approvalsStore.get().status, "error");
    assert.equal(await decideApproval("702", "allow"), false); assert.equal(puts, 1);
    await loadApprovals({ retry: true }); assert.equal(approvalDecisionStatus("702"), "idle");
  } finally { approvalsStore.reset(); globalThis.fetch = original; }
});

test("a temporary run-list failure retains the active run and coalesces polling", async () => {
  const original = globalThis.fetch; let reads = 0;
  const saved = { active_runs: 1, items: [{ id: "run", project_id: "qa", session_id: "first", terminal: false }] };
  runsStore.setData(saved);
  globalThis.fetch = async (_path, options) => {
    assert.equal(options.method, "GET"); assert(options.signal instanceof AbortSignal);
    return ++reads === 1 ? failed() : ok(saved);
  };
  try {
    await loadRuns(); assert.equal(runsStore.get().error, null); assert.equal(runsStore.get().data, saved);
    await loadRuns(); assert.equal(reads, 1);
    await waitRecovery(); assert.equal(reads, 2); assert.equal(runsStore.get().status, "ready");
  } finally { runsStore.reset(); globalThis.fetch = original; }
});

test("final read errors describe reading, not submitting, in every interface language", async () => {
  const original = globalThis.fetch;
  globalThis.fetch = async path => Response.json(JSON.parse(await readFile(new URL(
    "../app/web" + path, import.meta.url), "utf8")));
  try {
    for (const locale of ["zh-CN", "en-US", "ru-RU"]) {
      const messages = JSON.parse(await readFile(new URL(`../app/web/lang/${locale}.json`, import.meta.url), "utf8"));
      await loadLocale(locale);
      for (const kind of ["asks", "approvals", "runs"]) {
        const copy = decisionReadError({ code: kind + "_unavailable", status: 503, message: "internal transport failure" }, kind);
        assert.equal(copy, messages[`dock.read.${kind}`]);
        assert.notEqual(copy, messages["error.asksUnavailable"]);
        assert.notEqual(copy, messages["error.approvalsUnavailable"]);
      }
      assert.equal(decisionReadError({ code: "permission_denied", status: 403 }, "asks"), messages["error.permissionDenied"]);
    }
  } finally { globalThis.fetch = original; }
});
