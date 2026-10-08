import assert from "node:assert/strict";
import test from "node:test";
import { ApiError, api, hasPendingApiWrites } from "../app/web/js/api/client.js";
import { createProjectPurgeRecovery } from "../app/web/js/features/settings/project-purge-recovery.js";
import { createDraftStore } from "../app/web/js/features/chat/draft-store.js";
import { reviewedPurgeIntent } from "../app/web/js/features/settings/project-purge-contract.js";

const id = "d".repeat(32);
const preview = Object.freeze({ id: "demo", name: "Demo", revision: 2, created_at: 1234567890,
  etag: '"mdo-project-demo-2"', advisory: true, workspace_files_removed: false,
  shared_records_retained: true, target_count: 1, file_count: 1, directory_count: 0, total_bytes: 3,
  session_count: 0, schedule_count: 0, targets: [{ path: "projects/demo.json", type: "file" }],
  session_runtime_count: 0, active_interactive_run_count: 0, active_scheduled_run_count_global: 0,
  session_diagnostic_count: 0, session_catalog_diagnostic_count_global: 0, schedule_catalog_diagnostic_count_global: 0 });
const binding = reviewedPurgeIntent(preview, id);
const intentResponse = intent => ({ data: { intent },
  etag: intent ? `"mdo-purge-intent-${intent.purge_request_id}"` : '"mdo-purge-intent-empty"' });
const committed = { ...binding, accepted: true, outcome: "committed", committed: true, restart_required: false,
  workspace_files_removed: false, shared_records_retained: true, target_count: 1, file_count: 1,
  directory_count: 0, total_bytes: 3, schedule_count: 0 };

function fixture(options = {}) {
  let remote = null, receipt = null, ids = 0;
  const calls = [], reloads = [];
  const transport = {
    async get(path) {
      calls.push(["GET", path]);
      if (path === "/project-purge-intent") return intentResponse(remote);
      if (!receipt) throw new ApiError("Missing", { code: "purge_request_not_found", status: 404 });
      return { data: receipt };
    },
    async post(path, body, options) {
      calls.push(["POST", path, body, options.ifMatch]);
      if (path.endsWith("/purge-intent")) { remote = binding; return intentResponse(remote); }
      assert.ok(path.endsWith("/purge")); receipt = committed; return { data: receipt };
    },
    async delete(path, options) {
      calls.push(["DELETE", path, options.ifMatch]); remote = null; return intentResponse(null);
    },
  };
  const recovery = createProjectPurgeRecovery({ transport, newId() { ids += 1; return id; },
    onReload(project) { reloads.push([project, recovery.isPaused(), recovery.get().intent]); }, ...options });
  return { recovery, transport, calls, reloads, get ids() { return ids; },
    set remote(value) { remote = value; }, set receipt(value) { receipt = value; } };
}

test("explicit confirmation drains, saves one binding, executes it and ACKs without unlocking old memory", async () => {
  let drains = 0;
  const f = fixture({ async beforePrepare() { drains += 1; } });
  await f.recovery.refresh();
  assert.equal(await f.recovery.prepare(preview), true);
  assert.equal(drains, 1); assert.equal(f.ids, 1);
  assert.equal(f.recovery.get().intentSaved, true);
  assert.equal(f.calls.some(call => call[1].endsWith("/purge")), false);
  await f.recovery.refresh();
  assert.equal(f.calls.some(call => call[1].endsWith("/purge")), false);
  assert.equal(await f.recovery.execute(preview), true);
  const posts = f.calls.filter(call => call[0] === "POST");
  assert.deepEqual(posts.map(call => call[1]), ["/projects/demo/purge-intent", "/projects/demo/purge"]);
  for (const call of posts) {
    assert.deepEqual(call[2], { purge_request_id: id, created_at: preview.created_at });
    assert.equal(call[3], preview.etag);
  }
  assert.equal(f.reloads.length, 0);
  assert.equal(await f.recovery.completeCommitted(), true);
  assert.deepEqual(f.reloads, [["demo", true, binding]]);
  assert.equal(f.recovery.isPaused(), true);
  assert.deepEqual(f.calls.find(call => call[0] === "DELETE"),
    ["DELETE", "/project-purge-intent", `"mdo-purge-intent-${id}"`]);
});

test("an in-flight or failed draft drain creates no ID or saved request", async () => {
  for (const code of ["purge_client_busy", "purge_draft_unsaved"]) {
    const f = fixture({ async beforePrepare() { throw new ApiError("Wait", { code }); } });
    await f.recovery.refresh(); assert.equal(await f.recovery.prepare(preview), false);
    assert.equal(f.ids, 0); assert.equal(f.recovery.get().intent, null);
    assert.equal(f.recovery.get().error.code, code);
    assert.equal(f.calls.some(call => call[0] !== "GET"), false);
  }
});

test("a lost preparation keeps its original proposed ID and cannot execute until publication is read back", async () => {
  const f = fixture(); await f.recovery.refresh();
  f.transport.post = async () => { throw new ApiError("Lost", { code: "network_error" }); };
  assert.equal(await f.recovery.prepare(preview), false);
  assert.deepEqual(f.recovery.get().intent, binding);
  assert.equal(f.recovery.get().intentSaved, false);
  await f.recovery.refresh();
  assert.equal(await f.recovery.execute(preview), false);
  assert.equal(f.ids, 1);
  f.remote = binding;
  await f.recovery.refresh();
  assert.equal(f.recovery.get().intentSaved, true);
  assert.equal(await f.recovery.prepare(preview), true);
  assert.equal(f.ids, 1);
});

test("lost execution replies query the same original receipt and preserve verified commit facts", async () => {
  const f = fixture(); await f.recovery.refresh(); await f.recovery.prepare(preview);
  f.transport.post = async () => { f.receipt = committed; throw new ApiError("Lost", { code: "network_error" }); };
  assert.equal(await f.recovery.execute(preview), false);
  assert.equal(f.recovery.get().result.committed, true);
  assert.equal(f.ids, 1);
  assert.equal(f.calls.at(-1)[1], `/project-purges/${id}`);
  assert.equal(f.reloads.length, 0);
});

test("reloaded saved intent may be explicitly reviewed, but revision/incarnation changes never execute", async () => {
  for (const patch of [{ revision: 3, etag: '"mdo-project-demo-3"' }, { created_at: preview.created_at + 1 }]) {
    const f = fixture(); f.remote = binding; await f.recovery.refresh();
    assert.equal(await f.recovery.prepare({ ...preview, ...patch }), false);
    assert.equal(await f.recovery.execute({ ...preview, ...patch }), false);
    assert.equal(f.ids, 0); assert.equal(f.calls.some(call => call[0] !== "GET"), false);
    assert.deepEqual(f.recovery.get().intent, binding);
  }
});

test("a missing/different portable intent cannot authorize execution from cached review", async () => {
  const f = fixture(); await f.recovery.refresh(); await f.recovery.prepare(preview);
  f.remote = null;
  assert.equal(await f.recovery.execute(preview), false);
  assert.equal(f.recovery.get().intentSaved, false);
  assert.equal(f.calls.filter(call => call[1].endsWith("/purge")).length, 0);
});

test("lost ACK validates absence/new ownership, never erases known commit facts and reloads only explicitly", async () => {
  for (const remote of [null, { ...binding, purge_request_id: "e".repeat(32) }]) {
    const f = fixture(); f.remote = binding; f.receipt = committed; await f.recovery.refresh();
    f.transport.delete = async () => { f.remote = remote; throw new ApiError("Lost"); };
    const first = f.recovery.completeCommitted();
    assert.equal(f.recovery.completeCommitted(), first);
    assert.equal(await first, true);
    assert.deepEqual(f.reloads, [["demo", true, binding]]);
    assert.equal(f.recovery.get().result.committed, true);
  }
});

test("unsaved local edits, pending results and restart requirements cannot ACK a committed request", async () => {
  for (const [options, receipt] of [[{ canComplete: () => false }, committed],
    [{}, { ...committed, outcome: "pending", committed: false, restart_required: true }],
    [{}, { ...committed, restart_required: true }]]) {
    const f = fixture(options); f.remote = binding;
    f.receipt = receipt;
    await f.recovery.refresh();
    assert.equal(await f.recovery.completeCommitted(), false);
    assert.equal(f.calls.some(call => call[0] === "DELETE"), false);
    assert.equal(f.reloads.length, 0);
  }
});

test("advisory inventory rejects invalid headers, paths, unsafe numbers and active/diagnostic counts", () => {
  for (const patch of [{ etag: 'W/"mdo-project-demo-2"' }, { created_at: Number.MAX_SAFE_INTEGER + 1 },
    { target_count: 2 }, { targets: [{ path: "../data", type: "directory" }] },
    { active_interactive_run_count: 1 }, { session_catalog_diagnostic_count_global: 1 }])
    assert.throws(() => reviewedPurgeIntent({ ...preview, ...patch }, id));
});

test("API activity counts mutations through envelope parsing and releases on invalid response or upload failure", async () => {
  const previousFetch = globalThis.fetch;
  let finish;
  globalThis.fetch = () => new Promise(resolve => { finish = resolve; });
  try {
    const writing = api.put("/draft", {});
    assert.equal(hasPendingApiWrites(), true);
    finish(Response.json({ ok: true, data: {} })); await writing;
    assert.equal(hasPendingApiWrites(), false);
    const broken = api.uploadImage("demo", "a".repeat(32), new Blob(["x"]));
    assert.equal(hasPendingApiWrites(), true);
    finish(new Response("invalid JSON"));
    await assert.rejects(broken, { code: "invalid_response" });
    assert.equal(hasPendingApiWrites(), false);
  } finally { globalThis.fetch = previousFetch; }
});

test("draining drafts saves existing edits across owners without creating untouched empty drafts", async () => {
  const previousWindow = globalThis.window, previousFetch = globalThis.fetch;
  const puts = [];
  globalThis.window = { setTimeout() { return 1; }, clearTimeout() {}, addEventListener() {} };
  globalThis.fetch = async (path, options) => {
    if (options.method === "PUT") {
      const body = JSON.parse(options.body); puts.push([path, body]);
      return Response.json({ ok: true, data: { ...body, revision: body.revision + 1 } });
    }
    return Response.json({ ok: true, data: { revision: 1,
      text: "", attachments: [], submissions: [] } });
  };
  try {
    const draft = createDraftStore({ onRestore() {}, onError() {}, onSaved() {} });
    assert.equal(await draft.flushAll(), true); assert.equal(puts.length, 0);
    await draft.ensureLoaded("project:demo"); await draft.ensureLoaded("project:other");
    draft.edit("project:demo", "affected"); draft.edit("project:other", "retained");
    assert.equal(draft.hasUnsaved(), true); assert.equal(await draft.flushAll(), true);
    assert.deepEqual(puts.map(call => call[1].text), ["affected", "retained"]);
    assert.equal(draft.hasUnsaved(), false);
  } finally { globalThis.window = previousWindow; globalThis.fetch = previousFetch; }
});
