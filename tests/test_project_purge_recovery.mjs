import assert from "node:assert/strict";
import test from "node:test";
import { ApiError, api, setApiWriteGuard } from "../app/web/js/api/client.js";
import { createProjectPurgeRecovery } from
  "../app/web/js/features/settings/project-purge-recovery.js";
import { createDraftStore } from "../app/web/js/features/chat/draft-store.js";
import { createNewTaskController } from "../app/web/js/features/chat/new-task-controller.js";
import { createSubmissionController } from "../app/web/js/features/chat/submission-controller.js";

const intent = Object.freeze({ purge_request_id: "a".repeat(32), project_id: "demo",
  revision: 3, created_at: 1234567890, name: "Demo <not markup>" });
const empty = { data: { intent: null }, etag: '"mdo-purge-intent-empty"' };
const saved = (value = intent) => ({ data: { intent: value },
  etag: `"mdo-purge-intent-${value.purge_request_id}"` });
const result = (outcome, extra = {}) => ({ ...intent, accepted: true,
  committed: outcome === "committed", outcome, restart_required: outcome === "pending",
  target_count: 7, file_count: 5, directory_count: 2, total_bytes: 4096,
  schedule_count: 1, workspace_files_removed: false, shared_records_retained: true,
  ...extra });
const missing = () => new ApiError("No result", {
  status: 404, code: "purge_request_not_found" });

function fixture() {
  let remote = saved();
  let receipt = null;
  const calls = [];
  const transport = {
    async get(path) {
      calls.push(["get", path]);
      if (path === "/project-purge-intent") return remote;
      if (!receipt) throw missing();
      return { data: receipt };
    },
    async post(path, body, options) {
      calls.push(["post", path, body, options.ifMatch]);
      receipt ??= result("aborted", { target_count: 0, file_count: 0,
        directory_count: 0, total_bytes: 0, schedule_count: 0 });
      return { data: receipt };
    },
    async delete(path, options) {
      calls.push(["delete", path, options.ifMatch]);
      assert.equal(receipt.outcome, "aborted");
      remote = empty;
      return empty;
    },
  };
  const recovery = createProjectPurgeRecovery({ transport });
  return { recovery, transport, calls,
    set remote(value) { remote = value; },
    set receipt(value) { receipt = value; },
  };
}

test("cold empty reads unlock without creating or executing a request", async () => {
  const f = fixture(); f.remote = empty;
  assert.equal(f.recovery.isPaused(), true);
  await f.recovery.refresh();
  assert.equal(f.recovery.isPaused(), false);
  assert.deepEqual(f.calls, [["get", "/project-purge-intent"]]);
});

test("unknown saved requests cancel with the original binding and need explicit abort acknowledgement", async () => {
  const f = fixture();
  await f.recovery.refresh();
  assert.equal(f.recovery.get().result.outcome, "not_accepted");
  assert.equal(f.recovery.isPaused(), true);
  await f.recovery.acknowledgeAbort();
  assert.equal(f.calls.filter((call) => call[0] === "delete").length, 0);
  await f.recovery.cancel();
  assert.deepEqual(f.calls.find((call) => call[0] === "post"), ["post",
    "/projects/demo/purge-cancel", { purge_request_id: intent.purge_request_id,
      created_at: intent.created_at }, '"mdo-project-demo-3"']);
  assert.equal(f.recovery.get().result.outcome, "aborted");
  assert.equal(f.recovery.isPaused(), true);
  await f.recovery.acknowledgeAbort();
  assert.equal(f.recovery.isPaused(), false);
  assert.deepEqual(f.calls.at(-1), ["delete", "/project-purge-intent",
    `"mdo-purge-intent-${intent.purge_request_id}"`]);
  assert.ok(f.calls.every((call) => !call[1].endsWith("/purge")));
});

test("lost cancellation responses inspect the same ID without a new cancellation", async () => {
  const f = fixture(); await f.recovery.refresh();
  f.transport.post = async () => {
    f.receipt = result("aborted"); throw new ApiError("lost", { code: "network_error" });
  };
  await f.recovery.cancel();
  assert.equal(f.recovery.get().result.outcome, "aborted");
  assert.equal(f.recovery.get().error.code, "network_error");
  assert.equal(f.calls.at(-1)[1], `/project-purges/${intent.purge_request_id}`);
  await f.recovery.acknowledgeAbort();
  assert.equal(f.recovery.isPaused(), false);
});

test("an HTTP failure carrying committed facts never becomes cancelled or acknowledged", async () => {
  const f = fixture(); await f.recovery.refresh();
  f.transport.post = async () => { throw new ApiError("restart", {
    code: "purge_restart_required", status: 503,
    details: result("committed", { restart_required: true }),
  }); };
  await f.recovery.cancel();
  assert.equal(f.recovery.get().result.committed, true);
  await f.recovery.acknowledgeAbort();
  assert.equal(f.calls.filter((call) => call[0] === "delete").length, 0);
  assert.equal(f.recovery.isPaused(), true);
});

test("pending/committed results cannot trigger cancel or acknowledgement", async () => {
  for (const outcome of ["pending", "committed"]) {
    const f = fixture(); f.receipt = result(outcome);
    await f.recovery.refresh(); await f.recovery.cancel(); await f.recovery.acknowledgeAbort();
    assert.ok(f.calls.every((call) => call[0] === "get"));
    assert.equal(f.recovery.isPaused(), true);
  }
});

test("lost acknowledgement verifies portable absence before releasing the gate", async () => {
  const f = fixture(); f.receipt = result("aborted"); await f.recovery.refresh();
  f.transport.delete = async () => { f.remote = empty; throw new ApiError("lost"); };
  await f.recovery.acknowledgeAbort();
  assert.equal(f.recovery.get().intent, null);
  assert.equal(f.recovery.isPaused(), false);
});

test("a failed acknowledgement preserves the same abort and can be retried", async () => {
  const f = fixture(); f.receipt = result("aborted"); await f.recovery.refresh();
  f.transport.delete = async () => { throw new ApiError("disk", { code: "purge_intent_unavailable" }); };
  await f.recovery.acknowledgeAbort();
  assert.deepEqual(f.recovery.get().intent, intent);
  assert.equal(f.recovery.get().result.outcome, "aborted");
  assert.equal(f.recovery.isPaused(), true);
});

test("other-page acknowledgement never drops a known committed request", async () => {
  const f = fixture(); f.receipt = result("committed"); await f.recovery.refresh();
  f.remote = empty; await f.recovery.refresh();
  assert.deepEqual(f.recovery.get().intent, intent);
  assert.equal(f.recovery.get().result.committed, true);
  assert.equal(f.recovery.isPaused(), true);
});

test("a verified abort can hand over to another page's subsequent intent", async () => {
  const f = fixture(); f.receipt = result("aborted"); await f.recovery.refresh();
  const next = { ...intent, purge_request_id: "b".repeat(32), created_at: intent.created_at + 1 };
  f.remote = saved(next);
  const originalGet = f.transport.get;
  f.transport.get = (path) => path === `/project-purges/${next.purge_request_id}`
    ? Promise.resolve({ data: result("pending", next) }) : originalGet(path);
  await f.recovery.refresh();
  assert.deepEqual(f.recovery.get().intent, next);
  assert.equal(f.recovery.get().result.outcome, "pending");
  assert.equal(f.recovery.isPaused(), true);
});

test("corrupt intent/ETag/unsafe numbers fail closed without a mutation", async () => {
  for (const response of [
    { data: { intent: null }, etag: "" }, saved({ ...intent, revision: 2 ** 53 }),
    saved({ ...intent, project_id: "../demo" }), saved({ ...intent, name: "\0" }),
    saved({ ...intent, project_id: 123 }), saved({ ...intent, extra: true }),
    { ...saved(), etag: 'W/"mdo-purge-intent-a"' }, { data: {} },
  ]) {
    const f = fixture(); f.remote = response; await f.recovery.refresh();
    assert.equal(f.recovery.get().error.code, "purge_intent_unavailable");
    assert.equal(f.recovery.isPaused(), true);
    assert.equal(f.calls.length, 1);
  }
});

test("mismatched receipts and contradictory commit facts keep the original binding", async () => {
  for (const wrong of [ { created_at: intent.created_at + 1 }, { revision: 4 },
    { project_id: "other" }, { purge_request_id: "b".repeat(32) },
    { committed: true, outcome: "aborted" }, { accepted: null },
    { total_bytes: 2 ** 53 }, { workspace_files_removed: true } ]) {
    const f = fixture(); f.receipt = result("aborted", wrong); await f.recovery.refresh();
    assert.equal(f.recovery.get().error.code, "purge_intent_unavailable");
    assert.deepEqual(f.recovery.get().intent, intent);
    assert.equal(f.recovery.isPaused(), true);
  }
});

test("later contradictory results do not erase a known committed fact", async () => {
  const f = fixture(); f.receipt = result("committed"); await f.recovery.refresh();
  f.receipt = result("aborted"); await f.recovery.refresh();
  assert.equal(f.recovery.get().error.code, "purge_intent_unavailable");
  assert.equal(f.recovery.get().result.committed, true);
});

test("repeated clicks coalesce; a failed initial read remains paused during retry", async () => {
  const f = fixture(); f.transport.get = async () => { throw new ApiError("offline"); };
  await f.recovery.refresh();
  let resolve;
  f.transport.get = () => new Promise((done) => { resolve = done; });
  const first = f.recovery.refresh();
  const second = f.recovery.refresh();
  assert.equal(first, second);
  assert.equal(f.recovery.isPaused(), true);
  await Promise.resolve(); resolve(empty); await first;
  assert.equal(f.recovery.isPaused(), false);
});

test("page write guard covers JSON writes and raw uploads, but permits reads and stopping a run", async () => {
  const f = fixture(); await f.recovery.refresh();
  const previousFetch = globalThis.fetch;
  const calls = [];
  globalThis.fetch = async (path) => { calls.push(path); return Response.json({ ok: true, data: {} }); };
  const unset = setApiWriteGuard((request) => f.recovery.allowsWrite(request));
  try {
    await assert.rejects(api.put("/draft", { text: "stale" }), { code: "purge_review_required" });
    await assert.rejects(api.put("/draft", {}, { path: `/runs/${"b".repeat(32)}` }),
      { code: "purge_review_required" });
    await assert.rejects(api.post("/projects/demo/sessions", {}), { code: "purge_review_required" });
    await assert.rejects(api.uploadImage("demo", "a".repeat(32), new Blob(["x"])),
      { code: "purge_review_required" });
    await assert.rejects(api.delete("/project-purge-intent", {
      ifMatch: `"mdo-purge-intent-${intent.purge_request_id}"`,
    }), { code: "purge_review_required" });
    await api.get("/draft"); await api.delete(`/runs/${"b".repeat(32)}`);
    assert.deepEqual(calls, ["/api/v1/draft", `/api/v1/runs/${"b".repeat(32)}`]);
  } finally { unset(); globalThis.fetch = previousFetch; }
});

test("paused drafts retain local edits without retry loops or pagehide writes, then resume", async () => {
  const previousWindow = globalThis.window;
  const previousFetch = globalThis.fetch;
  let paused = true;
  let pagehide;
  let timers = 0;
  const puts = [];
  globalThis.window = { setTimeout() { timers += 1; return timers; }, clearTimeout() {},
    addEventListener(_event, callback) { pagehide = callback; } };
  globalThis.fetch = async (_path, options) => {
    if (options.method === "GET") return Response.json({ ok: true,
      data: { revision: 1, text: "original", attachments: [], submissions: [] } });
    puts.push(JSON.parse(options.body));
    return Response.json({ ok: true, data: { revision: 2 } });
  };
  try {
    const draft = createDraftStore({ onRestore() {}, onError() {}, onSaved() {},
      isWritePaused: () => paused });
    await draft.ensureLoaded("project:demo");
    draft.edit("project:demo", "kept input");
    assert.equal(await draft.flush("project:demo"), false); pagehide();
    assert.equal(timers, 0); assert.equal(puts.length, 0);
    assert.equal(draft.text("project:demo"), "kept input");
    paused = false; draft.resumeSaves(); assert.equal(timers, 1);
    assert.equal(await draft.flush("project:demo"), true);
    assert.equal(puts[0].text, "kept input");
  } finally { globalThis.window = previousWindow; globalThis.fetch = previousFetch; }
});

test("a pause during an in-flight save stops later dirty iterations without losing edits", async () => {
  const previousWindow = globalThis.window;
  const previousFetch = globalThis.fetch;
  let paused = false; let finish; let puts = 0; let timers = 0;
  globalThis.window = { setTimeout() { timers += 1; return timers; }, clearTimeout() {}, addEventListener() {} };
  globalThis.fetch = async (_path, options) => {
    if (options.method === "GET") return Response.json({ ok: true,
      data: { revision: 1, text: "", attachments: [], submissions: [] } });
    puts += 1; return new Promise((done) => { finish = done; });
  };
  try {
    const draft = createDraftStore({ onRestore() {}, onError() {}, onSaved() {}, isWritePaused: () => paused });
    await draft.ensureLoaded("project:demo"); draft.edit("project:demo", "first");
    const saving = draft.flush("project:demo"); await Promise.resolve();
    paused = true; draft.edit("project:demo", "later"); const before = timers;
    finish(Response.json({ ok: true, data: { revision: 2 } })); await saving;
    assert.equal(puts, 1); assert.equal(timers, before);
    assert.equal(draft.text("project:demo"), "later");
    paused = false; draft.resumeSaves(); assert.equal(timers, before + 1);
  } finally { globalThis.window = previousWindow; globalThis.fetch = previousFetch; }
});

test("new-task and submission recovery do not touch restored state while paused", async () => {
  const draftStore = new Proxy({}, { get() { throw new Error("Paused state was touched"); } });
  const pause = () => true;
  const fresh = createNewTaskController({ draftStore, isWritePaused: pause });
  assert.equal(await fresh.reconcile(), false);
  assert.equal(await fresh.submit({ projectId: "demo", text: "x" }), false);
  const existing = createSubmissionController({ draftStore, isWritePaused: pause });
  assert.equal(await existing.pump("demo/session"), false);
  assert.equal(await existing.reconcile("demo/session"), false);
  assert.equal(await existing.submit("demo/session", "x", [], false), false);
});

test("a failed recheck of a formerly empty Home does not unlock writes while retrying", async () => {
  const f = fixture(); f.remote = empty; await f.recovery.refresh();
  f.transport.get = async () => { throw new ApiError("offline"); };
  await f.recovery.refresh(); assert.equal(f.recovery.isPaused(), true);
  let finish;
  f.transport.get = () => new Promise((done) => { finish = done; });
  const retry = f.recovery.refresh(); await Promise.resolve();
  assert.equal(f.recovery.isPaused(), true);
  finish(empty); await retry;
  assert.equal(f.recovery.isPaused(), false);
});

test("a bounded read timeout preserves the startup recovery gate", async () => {
  const recovery = createProjectPurgeRecovery({ timeoutMs: 5, transport: {
    get(_path, options) {
      return new Promise((_resolve, reject) => options.signal.addEventListener("abort",
        () => reject(new ApiError("timeout", { code: "network_error" })), { once: true }));
    },
  } });
  await recovery.refresh();
  assert.equal(recovery.get().error.code, "network_error");
  assert.equal(recovery.isPaused(), true);
  assert.equal(recovery.get().busy, false);
});
