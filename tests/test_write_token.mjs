import assert from "node:assert/strict";
import test from "node:test";
import { api, currentPageWriteToken, setApiWriteConflictHandler } from "../app/web/js/api/client.js";
import { createProjectPurgeRecovery } from "../app/web/js/features/settings/project-purge-recovery.js";
import { createDraftStore } from "../app/web/js/features/chat/draft-store.js";

const old = "a".repeat(32) + "-0";
const fresh = "a".repeat(32) + "-1";
const restarted = "b".repeat(32) + "-0";
const empty = (writeToken) => ({ data: { intent: null }, etag: '"mdo-purge-intent-empty"', writeToken });

test("the page adopts its first startup token once, sends it on JSON/uploads and never silently renews", async () => {
  const previous = globalThis.fetch;
  const calls = [];
  let token = old;
  let error = null;
  let conflicts = 0;
  globalThis.fetch = async (path, options) => {
    calls.push([path, new Headers(options.headers).get("X-Mdo-Write-Token")]);
    return Response.json(error ? { ok: false, error } : { ok: true, data: { intent: null } },
      { status: error ? 412 : 200, headers: { "X-Mdo-Write-Token": token } });
  };
  setApiWriteConflictHandler(() => { conflicts += 1; });
  try {
    await api.get("/bootstrap");
    assert.equal(currentPageWriteToken(), null);
    token = "invalid";
    await api.get("/project-purge-intent");
    assert.equal(currentPageWriteToken(), null);
    token = old;
    error = { code: "write_token_conflict", message: "Rejected" };
    await assert.rejects(api.get("/project-purge-intent"), { code: "write_token_conflict" });
    assert.equal(currentPageWriteToken(), null);
    error = null;
    await api.get("/project-purge-intent");
    assert.equal(currentPageWriteToken(), old);
    await api.put("/draft", { revision: 0, text: "kept" });
    await api.uploadImage("demo", "c".repeat(32), new Blob(["x"]));
    assert.deepEqual(calls.slice(-2).map((call) => call[1]), [old, old]);
    token = fresh;
    assert.equal((await api.get("/project-purge-intent")).writeToken, fresh);
    assert.equal(currentPageWriteToken(), old);
    error = { code: "write_token_conflict", message: "Rejected" };
    await assert.rejects(api.put("/draft", {}), { code: "write_token_conflict" });
    await assert.rejects(api.uploadImage("demo", "c".repeat(32), new Blob(["x"])),
      { code: "write_token_conflict" });
    assert.equal(conflicts, 3);
    assert.deepEqual(calls.slice(-2).map((call) => call[1]), [old, old]);
    error = null; token = restarted;
    await api.get("/project-purge-intent");
    assert.equal(currentPageWriteToken(), old);
  } finally { globalThis.fetch = previous; setApiWriteConflictHandler(null); }
});

test("an empty intent after another page's ACK or host restart still fences this page", async () => {
  for (const token of [fresh, restarted]) {
    let remote = old;
    let reloads = 0;
    const recovery = createProjectPurgeRecovery({ getWriteToken: () => old,
      onReload(projectId) { assert.equal(projectId, null); reloads += 1; },
      transport: { async get() { return empty(remote); } } });
    await recovery.refresh(); assert.equal(recovery.isPaused(), false);
    remote = token;
    await recovery.refresh();
    assert.equal(recovery.get().writeConflict, true);
    assert.equal(recovery.isPaused(), true);
    assert.equal(recovery.allowsWrite({ method: "PUT", path: "/draft" }), false);
    assert.equal(recovery.allowsWrite({ method: "DELETE", path: `/runs/${"c".repeat(32)}` }), true);
    assert.equal(reloads, 0);
    remote = old; await recovery.refresh();
    assert.equal(recovery.isPaused(), true); // conflict is sticky until an actual page reload
    recovery.reload(); assert.equal(reloads, 1);
  }
});

test("a rejected mutation retains the fence even if later reads have no intent", async () => {
  const recovery = createProjectPurgeRecovery({ getWriteToken: () => old,
    transport: { async get() { return empty(old); } } });
  await recovery.refresh();
  recovery.markWriteConflict(new Error("write rejected"));
  await recovery.refresh();
  assert.equal(recovery.get().error, null);
  assert.equal(recovery.isPaused(), true);
  assert.equal(recovery.get().writeConflict, true);
});

test("copying paused drafts includes image-only input and detached configuration/intent snapshots", async () => {
  const previousWindow = globalThis.window;
  const previousFetch = globalThis.fetch;
  const image = "c".repeat(32);
  const profile = { model_id: "ornith-1.5-35b", reasoning_effort: "high", permission_profile: "read-only" };
  globalThis.window = { setTimeout() { throw new Error("Paused draft scheduled a write"); },
    clearTimeout() {}, addEventListener() {} };
  globalThis.fetch = async (path, options) => {
    assert.equal(options.method, "GET");
    return Response.json({ ok: true, data: { revision: 1, text: "", attachments: [image],
      ...(path.includes("full") ? { run_admission_uncertain: true, composer_profile: profile,
        submissions: [{ id: "d".repeat(32), text: "pending", attachments: [image],
          interrupt: false, state: "prepared", profile }] } : {}) } });
  };
  try {
    const draft = createDraftStore({ onRestore() {}, onError() {}, onSaved() {}, isWritePaused: () => true });
    await draft.ensureLoaded("project:images"); await draft.ensureLoaded("project:full");
    const snapshots = draft.unsentSnapshots();
    assert.deepEqual(snapshots[0].attachments, [image]);
    assert.equal(snapshots[0].text, "");
    assert.equal(snapshots[1].run_admission_uncertain, true);
    assert.deepEqual(snapshots[1].composer_profile, profile);
    snapshots[1].attachments.pop(); snapshots[1].composer_profile.model_id = "changed";
    snapshots[1].submissions[0].attachments.pop(); snapshots[1].submissions[0].profile.model_id = "changed";
    const retained = draft.unsentSnapshots()[1];
    assert.deepEqual(retained.attachments, [image]);
    assert.deepEqual(retained.composer_profile, profile);
    assert.deepEqual(retained.submissions[0].profile, profile);
    assert.deepEqual(retained.submissions[0].attachments, [image]);
  } finally { globalThis.window = previousWindow; globalThis.fetch = previousFetch; }
});
