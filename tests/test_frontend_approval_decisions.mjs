import assert from "node:assert/strict";
import test from "node:test";

import { approvalDecisionStatus, decideApproval, loadApprovals } from
  "../app/web/js/state/approvals.js";

function deferred() {
  let resolve;
  const promise = new Promise((done) => { resolve = done; });
  return { promise, resolve };
}

test("both approval surfaces share one in-flight submission", async () => {
  const entered = deferred();
  const release = deferred();
  const originalFetch = globalThis.fetch;
  let puts = 0;
  globalThis.fetch = async (_path, options) => {
    if (options.method === "PUT") {
      puts += 1;
      entered.resolve();
      await release.promise;
      return Response.json({ ok: true, data: { decision: "allow" } });
    }
    assert.equal(options.method, "GET");
    return Response.json({ ok: true, data: { total: 0, items: [] } });
  };
  try {
    const first = decideApproval("1", "allow");
    await entered.promise;
    assert.equal(approvalDecisionStatus("1"), "pending");
    assert.equal(await decideApproval("1", "deny"), false);
    assert.equal(puts, 1);
    release.resolve();
    assert.equal(await first, true);
    assert.equal(approvalDecisionStatus("1"), "idle");
  } finally {
    release.resolve();
    globalThis.fetch = originalFetch;
  }
});

test("a committed decision stays locked when the refresh fails", async () => {
  const originalFetch = globalThis.fetch;
  let refreshes = 0;
  let puts = 0;
  globalThis.fetch = async (_path, options) => {
    if (options.method === "PUT") {
      puts += 1;
      return Response.json({ ok: true, data: { decision: "deny" } });
    }
    refreshes += 1;
    if (refreshes === 1)
      return Response.json({ ok: false, error: { message: "refresh failed" } },
        { status: 503 });
    return Response.json({ ok: true, data: { total: 0, items: [] } });
  };
  try {
    await assert.rejects(decideApproval("2", "deny"), /refresh failed/);
    assert.equal(approvalDecisionStatus("2"), "submitted");
    assert.equal(await decideApproval("2", "allow"), false);
    assert.equal(puts, 1);
    await loadApprovals();
    assert.equal(approvalDecisionStatus("2"), "idle");
  } finally {
    globalThis.fetch = originalFetch;
  }
});
