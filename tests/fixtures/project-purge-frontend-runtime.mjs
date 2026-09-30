// Production browser protocol modules, real local HTTP/xs/TCC, synthetic Home.
import assert from "node:assert/strict";
import { api, ApiError, currentPageWriteToken, setApiWriteGuard, setApiWriteConflictHandler } from "../../app/web/js/api/client.js";
import { createProjectPurgeRecovery } from "../../app/web/js/features/settings/project-purge-recovery.js";

const [base, mode] = process.argv.slice(2);
const originalFetch = globalThis.fetch;
const calls = [];
let dropped = false, ids = 0, reloads = 0;
globalThis.fetch = async (path, options) => {
  calls.push([options.method, path]);
  const response = await originalFetch(new URL(path, base), options);
  const selected = { "lost-prepare": ["POST", "/purge-intent"],
    "lost-execute": ["POST", "/purge"], "lost-ack": ["DELETE", "/project-purge-intent"] }[mode];
  if (!dropped && selected && options.method === selected[0] && path.endsWith(selected[1])) {
    await response.text(); dropped = true; throw new TypeError("bounded lost reply after server handling");
  }
  return response;
};
const recovery = createProjectPurgeRecovery({ transport: api, getWriteToken: currentPageWriteToken,
  newId() { ids += 1; return "d".repeat(32); },
  async beforePrepare() { if (mode === "drain-fail") throw new ApiError("unsaved", { code: "purge_draft_unsaved" }); },
  onReload(project) { assert.equal(project, "purge-probe"); assert.equal(recovery.isPaused(), true); reloads += 1; } });
setApiWriteGuard(request => recovery.allowsWrite(request));
setApiWriteConflictHandler(error => recovery.markWriteConflict(error));
await recovery.refresh();
assert.equal(recovery.isPaused(), false);
const initialToken = currentPageWriteToken();
const response = await api.get("/projects/purge-probe/purge-preview");
const preview = { ...response.data, etag: response.etag };
const prepared = await recovery.prepare(preview);
if (mode === "drain-fail") {
  assert.equal(prepared, false); assert.equal(ids, 0);
  assert.equal(recovery.get().intent, null);
  assert.equal(calls.some(call => call[0] === "POST"), false);
} else {
  assert.equal(prepared, mode !== "lost-prepare");
  assert.equal(ids, 1); assert.equal(recovery.get().intentSaved, true);
  assert.equal(calls.filter(call => call[1].endsWith("/purge")).length, 0);
  await recovery.refresh();
  if (mode === "cancel") {
    await recovery.cancel();
    assert.equal(recovery.get().result.outcome, "aborted");
    await recovery.acknowledgeAbort(); assert.equal(recovery.isPaused(), false);
  } else {
    const executed = await recovery.execute(preview);
    if (mode === "rollback") {
      assert.equal(executed, false); assert.equal(recovery.get().result.outcome, "aborted");
      assert.equal(recovery.get().result.committed, false);
      await recovery.acknowledgeAbort(); assert.equal(recovery.isPaused(), true);
    } else {
      assert.equal(executed, mode !== "lost-execute");
      assert.equal(recovery.get().result.committed, true);
      assert.equal(recovery.get().writeConflict, true);
      await assert.rejects(api.put("/draft", { revision: 0, text: "resurrect" }), { code: "purge_review_required" });
      assert.equal(await recovery.completeCommitted(), true);
      assert.equal(reloads, 1); assert.equal(recovery.isPaused(), true);
    }
  }
  assert.equal((await api.get("/project-purge-intent")).data.intent, null);
  assert.equal(calls.filter(call => call[0] === "POST" && call[1].endsWith("/purge-intent")).length, 1);
  assert.equal(currentPageWriteToken(), initialToken);
}
console.log(JSON.stringify({ mode, ids, reloads, committed: recovery.get().result?.committed ?? false,
  calls: calls.length, paused: recovery.isPaused() }));
