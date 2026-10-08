import assert from "node:assert/strict";
import test from "node:test";
import { ApiError } from "../app/web/js/api/client.js";
import { createBackupImportController, createRestoreBookmark } from "../app/web/js/features/sessions/backup-import-controller.js";

const id = "a".repeat(32), sha256 = "b".repeat(64), previewId = "c".repeat(32);
const review = { id, session_id: id, project_id: "target", source_sha256: sha256, preview_id: previewId,
  workspace_root: "/captured/workspace", project_revision: 7, state: "review", accepted: false, terminal: false };
const committed = { ...review, state: "committed", accepted: true, committed: true, terminal: true };
const preview = { id: previewId, sha256, state: "succeeded", terminal: true, result_available: true,
  result: { title: "Source", legacy_partial: false } };
const missing = () => new ApiError("missing", { code: "restore_request_not_found", status: 404 });
const network = () => new ApiError("lost", { code: "network_error" });
function setup(overrides = {}, original = null, options = {}) {
  const calls = [], marks = [];
  let saved = original;
  const bookmark = { read: () => saved, write(value) { marks.push(value); saved = value; calls.push(["bookmark", value]); } };
  const transport = Object.fromEntries(Object.entries({
    current: () => ({ empty: true }), previews: () => null,
    upload: (file, callbacks) => { callbacks.onIdentity({ id, sha256, bytes: 10 }); return { id, sha256, bytes: 10 }; },
    preview: () => preview, readPreview: () => preview,
    review: () => review, apply: () => committed, read: () => committed,
    cancel: () => ({ id, cancel_requested: true }), discardPreview: () => ({}), discardUpload: () => ({}),
    ...overrides,
  }).map(([method, handler]) => [method, async (...args) => { calls.push([method, ...args]); return handler(...args); }]));
  const controller = createBackupImportController({ transport, bookmark, pollMs: 1, ...options });
  return { controller, calls, marks, bookmark, saved: () => saved };
}
async function prepared(ctx) { await ctx.controller.choose({ name: "source.json" }); await ctx.controller.review("target"); }

test("upload/preview never apply; explicit confirmation pins target and bookmarks before acceptance", async () => {
  const ctx = setup();
  await prepared(ctx);
  assert.equal(ctx.controller.get().restore.workspace_root, "/captured/workspace");
  assert.equal(ctx.controller.get().restore.project_revision, 7);
  assert.equal(ctx.calls.filter(([name]) => name === "apply").length, 0);
  await ctx.controller.confirm();
  assert.equal(ctx.controller.get().phase, "committed");
  assert.deepEqual(ctx.calls.slice(-2).map(([name]) => name), ["bookmark", "apply"]);
  assert.equal(ctx.saved().id, id);
  ctx.controller.destroy();
});

test("lost apply response and refresh retain identity; recreation only queries and never reapplies", async () => {
  const ctx = setup({ apply() { throw network(); } });
  await prepared(ctx); await ctx.controller.confirm();
  assert.equal(ctx.controller.get().phase, "unknown");
  await ctx.controller.confirm(); await ctx.controller.choose({});
  await ctx.controller.refresh();
  assert.equal(ctx.controller.get().phase, "committed");
  assert.equal(ctx.calls.filter(([name]) => name === "apply").length, 1);
  const reload = setup({}, ctx.saved());
  assert.equal(reload.controller.hasRecovery(), true);
  await reload.controller.inspect();
  assert.deepEqual(reload.calls.map(([name]) => name), ["read"]);
  assert.equal(reload.controller.get().phase, "committed");
  ctx.controller.destroy(); reload.controller.destroy();
});

test("an unknown 404 is not permission to discard identity, upload another file or apply again", async () => {
  const ctx = setup({ read() { throw missing(); } }, review);
  await ctx.controller.inspect(); await ctx.controller.cancel(); await ctx.controller.acknowledge();
  await ctx.controller.confirm(); await ctx.controller.choose({});
  assert.equal(ctx.controller.get().phase, "unknown"); assert.equal(ctx.saved().id, id);
  assert.deepEqual(ctx.calls.map(([name]) => name), ["read", "cancel", "read"]);
  ctx.controller.destroy();
});

test("cancellation racing with commit preserves committed and asks no new restore", async () => {
  const ctx = setup({}, review);
  await ctx.controller.cancel();
  assert.equal(ctx.controller.get().phase, "committed");
  assert.equal(ctx.controller.get().restore.committed, true);
  assert.equal(ctx.calls.some(([name]) => name === "apply"), false);
  await ctx.controller.acknowledge();
  assert.equal(ctx.saved(), null); assert.equal(ctx.controller.get().phase, "choose");
  ctx.controller.destroy();
});

test("changing an unsubmitted destination deletes its review then proves it missing before reuse", async () => {
  const ctx = setup({ read() { throw missing(); } });
  await prepared(ctx); await ctx.controller.cancel();
  assert.equal(ctx.controller.get().restore, null); assert.equal(ctx.controller.get().phase, "preview");
  assert.deepEqual(ctx.calls.slice(-2).map(([name]) => name), ["cancel", "read"]);
  assert.equal(ctx.marks.length, 0);
  ctx.controller.destroy();
});

test("a concurrent acceptance during destination change is followed using the same ID", async () => {
  const ctx = setup(); await prepared(ctx); await ctx.controller.cancel();
  assert.equal(ctx.saved().id, id); assert.equal(ctx.controller.get().phase, "committed");
  assert.equal(ctx.controller.get().restore.id, id);
  ctx.controller.destroy();
});

test("failure to persist identity prevents POST apply; read may recover the original review", async () => {
  const ctx = setup({ read: () => review }); await prepared(ctx);
  ctx.bookmark.write = () => { throw new ApiError("no history", { code: "restore_bookmark_unavailable" }); };
  await ctx.controller.confirm();
  assert.equal(ctx.calls.some(([name]) => name === "apply"), false);
  assert.equal(ctx.controller.get().error.code, "restore_bookmark_unavailable");
  await ctx.controller.refresh(); assert.equal(ctx.controller.get().phase, "review");
  ctx.controller.destroy();
});

test("mismatched result cannot replace saved target or checksum", async () => {
  const ctx = setup({ read: () => ({ ...committed, project_id: "wrong" }) }, review);
  await ctx.controller.inspect();
  assert.equal(ctx.controller.get().phase, "unknown");
  assert.equal(ctx.controller.get().error.code, "restore_identity_mismatch");
  assert.equal(ctx.controller.get().restore.project_id, "target");
  assert.equal(ctx.saved().id, id); ctx.controller.destroy();
});

test("preview/review response recovery discovers existing work without creating anything", async () => {
  const ctx = setup({ current: () => review });
  await ctx.controller.inspect(); assert.equal(ctx.controller.get().phase, "review");
  assert.deepEqual(ctx.calls.map(([name]) => name), ["current"]);
  assert.equal(ctx.saved(), null);
  ctx.controller.destroy();
  const pending = setup({ previews: () => ({ ...preview, terminal: false }) });
  await pending.controller.refresh();
  assert.equal(pending.controller.get().phase, "preview");
  assert.deepEqual(pending.calls.map(([name]) => name), ["current", "previews", "readPreview"]);
  pending.controller.destroy();
});

test("legacy partial preview cannot enter destination review or apply", async () => {
  const ctx = setup({ preview: () => ({ ...preview, result: { legacy_partial: true } }) });
  await ctx.controller.choose({}); await ctx.controller.review("target"); await ctx.controller.confirm();
  assert.equal(ctx.controller.get().phase, "preview");
  assert.equal(ctx.calls.some(([name]) => ["review", "apply"].includes(name)), false);
  ctx.controller.destroy();
});

test("a lost seal response retains its upload until explicit discard; discovery cannot enable a no-op file input", async () => {
  const ctx = setup({ upload(file, options) {
    options.onIdentity({ id, sha256 }); throw network();
  } });
  await ctx.controller.choose({}); await ctx.controller.refresh();
  assert.equal(ctx.controller.get().phase, "failed"); assert.equal(ctx.controller.get().upload.id, id);
  await ctx.controller.choose({}); assert.equal(ctx.calls.filter(([name]) => name === "upload").length, 1);
  await ctx.controller.cancel(); assert.equal(ctx.controller.get().phase, "choose");
  assert.equal(ctx.controller.get().upload, null); ctx.controller.destroy();
});

test("bounded observation expiry retains pending identity for manual query", async () => {
  const ctx = setup({ read: () => ({ ...review, state: "pending", accepted: true }) }, review,
    { observationMs: 2 });
  await ctx.controller.inspect();
  assert.equal(ctx.controller.get().phase, "unknown");
  assert.equal(ctx.controller.get().error.code, "restore_observation_timeout");
  assert.equal(ctx.saved().id, id); ctx.controller.destroy();
});

test("obsolete upload completion after explicit cancellation cannot install a preview", async () => {
  let complete, late;
  const ctx = setup({ upload(file, options) { late = options;
    return new Promise((resolve) => { complete = resolve; }); } });
  const upload = ctx.controller.choose({});
  // Wait for the transport to start; a separate check covers cancellation
  // before its microtask, which must now send no upload at all.
  await new Promise(resolve => setImmediate(resolve)); await ctx.controller.cancel();
  assert.equal(late.signal.aborted, true);
  late.onIdentity({ id, sha256 }); late.onProgress({ phase: "uploading" }); complete({ id, sha256 });
  await upload;
  assert.equal(ctx.controller.get().phase, "choose"); assert.equal(ctx.controller.get().upload, null);
  assert.equal(ctx.calls.some(([name]) => name === "preview"), false); ctx.controller.destroy();
});

test("cleanup or restart uncertainty retains result bookmark and blocks acknowledgement", async () => {
  const ctx = setup({ read: () => ({ ...committed, cleanup_pending: true, restart_required: true }) }, review);
  await ctx.controller.inspect(); await ctx.controller.acknowledge();
  assert.equal(ctx.saved().id, id); assert.equal(ctx.controller.get().phase, "committed");
  assert.deepEqual(ctx.calls.map(([name]) => name), ["read"]); ctx.controller.destroy();
});

test("URL bookmark preserves unrelated query, route and history and rejects malformed identity", async () => {
  const location = { href: "http://localhost/?a=1#/projects/default/new" }, old = { mdoWorkspace: "old" };
  const history = { state: old, replaceState(state, title, url) {
    assert.equal(state, old); location.href = new URL(url, location.href).href;
  } };
  const bookmark = createRestoreBookmark({ location, history });
  bookmark.write(review); assert.equal(bookmark.read().id, id);
  assert.equal(new URL(location.href).hash, "#/projects/default/new");
  assert.equal(new URL(location.href).searchParams.get("a"), "1");
  bookmark.write(null); assert.equal(bookmark.read(), null);
  location.href += "&unused"; // hash content remains unrelated to query identity.
  location.href = "http://localhost/?mdoRestore=" + id;
  assert.throws(() => bookmark.read(), { code: "restore_bookmark_invalid" });
  const ctx = setup(); ctx.controller.destroy();
  const controller = createBackupImportController({ bookmark,
    transport: { current() { throw new Error("must not discover"); } } });
  await controller.inspect(); await controller.refresh(); await controller.choose({});
  assert.equal(controller.get().phase, "unknown");
  assert.equal(controller.get().error.code, "restore_bookmark_invalid"); controller.destroy();
});
