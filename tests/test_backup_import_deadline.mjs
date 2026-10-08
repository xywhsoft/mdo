import assert from "node:assert/strict";
import test, { beforeEach, afterEach, after } from "node:test";
import { ApiError } from "../app/web/js/api/client.js";
import { createBackupImportController } from "../app/web/js/features/sessions/backup-import-controller.js";

const previous = { setTimeout: globalThis.setTimeout, clearTimeout: globalThis.clearTimeout, now: Date.now };
const timers = new Map(), controllers = new Set();
let time = 0, serial = 0;
const flush = () => new Promise(resolve => setImmediate(resolve));
globalThis.setTimeout = (fn, ms) => { timers.set(++serial, { fn, at: time + ms }); return serial; };
globalThis.clearTimeout = id => timers.delete(id);
Date.now = () => time;
const id = "a".repeat(32), sha256 = "b".repeat(64), previewId = "c".repeat(32);
const review = { id, session_id: id, project_id: "target", source_sha256: sha256, preview_id: previewId,
  state: "review", terminal: false, accepted: false };
const committed = { ...review, state: "committed", terminal: true, accepted: true, committed: true };
const preview = { id: previewId, sha256, state: "succeeded", terminal: true, result_available: true,
  result: { legacy_partial: false } };
function setup(overrides = {}, original = review, options = {}) {
  let saved = original;
  const calls = [], failures = [];
  const handlers = {
    current: () => ({ empty: true }), previews: () => null,
    upload: (_file, opts) => { opts.onIdentity({ id, sha256, bytes: 10 }); return { id, sha256, bytes: 10 }; },
    preview: () => preview, readPreview: () => preview, review: () => review,
    apply: () => committed, read: () => committed, cancel: () => ({}),
    discardPreview: () => ({}), discardUpload: () => ({}), ...overrides,
  };
  const transport = Object.fromEntries(Object.entries(handlers).map(([method, handler]) =>
    [method, (...args) => { calls.push(method); return handler(...args); }]));
  const controller = createBackupImportController({ transport,
    bookmark: { read: () => saved, write: value => { saved = value; } }, pollMs: 350, ...options });
  controllers.add(controller);
  controller.subscribe(state => { if (state.error) failures.push(state.error); });
  return { controller, calls, failures, saved: () => saved };
}
async function prepared(ctx) { await ctx.controller.choose({ name: "source.json" }); await ctx.controller.review("target"); }
async function step() {
  const [id, timer] = [...timers].sort((a, b) => a[1].at - b[1].at)[0] ?? [];
  assert(timer, "missing deadline or backoff timer"); timers.delete(id); time = timer.at;
  timer.fn(); await flush();
}
beforeEach(() => { timers.clear(); time = 0; });
afterEach(() => { for (const controller of controllers) controller.destroy(); controllers.clear(); timers.clear(); });
after(() => { globalThis.setTimeout = previous.setTimeout; globalThis.clearTimeout = previous.clearTimeout; Date.now = previous.now; });

test("a hung restore read retries quietly, keeps identity, and ignores its late result", async () => {
  let reads = 0, finish, signal, settled = false;
  const ctx = setup({ read(_id, options) { if (++reads === 1) { signal = options.signal;
    return new Promise(resolve => { finish = resolve; }); } return committed; } });
  void ctx.controller.inspect().then(() => { settled = true; }); await flush();
  await step(); assert.equal(time, 10000); assert.equal(signal.aborted, true);
  assert.equal(ctx.failures.length, 0); assert.equal(settled, false);
  await step(); assert.equal(settled, true); assert.equal(reads, 2);
  assert.equal(ctx.controller.get().phase, "committed"); assert.equal(ctx.saved().id, id);
  finish({ ...committed, project_id: "wrong" }); await flush();
  assert.equal(ctx.controller.get().restore.project_id, "target"); assert.equal(ctx.failures.length, 0);
});

test("destroy releases an uncooperative read and cancels all observation timers", async () => {
  let signal, settled = false;
  const ctx = setup({ read(_id, options) { signal = options.signal; return new Promise(() => {}); } });
  void ctx.controller.inspect().then(() => { settled = true; }); await flush();
  ctx.controller.destroy(); await flush();
  assert.equal(signal.aborted, true); assert.equal(settled, true); assert.equal(timers.size, 0);
  assert.equal(ctx.calls.filter(name => name === "read").length, 1);
});

test("explicit cancellation releases the old observer without starting another restore", async () => {
  let reads = 0, signal, oldSettled = false;
  const ctx = setup({ read(_id, options) { if (++reads === 1) { signal = options.signal; return new Promise(() => {}); } return committed; } });
  void ctx.controller.inspect().then(() => { oldSettled = true; }); await flush();
  await ctx.controller.cancel(); await flush();
  assert.equal(oldSettled, true); assert.equal(signal.aborted, true);
  assert.equal(ctx.controller.get().phase, "committed"); assert.equal(ctx.saved().id, id);
  assert.deepEqual(ctx.calls, ["read", "cancel", "read"]); assert.equal(timers.size, 0);
});

test("an unconfirmed apply ends its wait once and queries the same identity without replaying POST", async () => {
  let signal, settled = false;
  const ctx = setup({ apply(_id, options) { signal = options.signal; return new Promise(() => {}); } }, null);
  await prepared(ctx);
  void ctx.controller.confirm().then(() => { settled = true; }); await flush(); await step();
  assert.equal(time, 10000); assert.equal(settled, true); assert.equal(signal.aborted, true);
  assert.equal(ctx.controller.get().phase, "unknown"); assert.equal(ctx.controller.get().busy, false);
  assert.equal(ctx.controller.get().error.code, "restore_observation_timeout");
  assert.equal(ctx.saved().id, id); assert.equal(ctx.calls.filter(name => name === "apply").length, 1);
  await ctx.controller.refresh();
  assert.equal(ctx.controller.get().phase, "committed");
  assert.equal(ctx.calls.filter(name => name === "apply").length, 1);
});

test("cancel settles a stuck upload and rejects callbacks from its previous owner", async () => {
  let late, settled = false;
  const ctx = setup({ upload(_file, options) { late = options; options.onIdentity({ id, sha256 }); return new Promise(() => {}); } }, null);
  void ctx.controller.choose({ name: "source.json" }).then(() => { settled = true; }); await flush();
  await ctx.controller.cancel(); await flush();
  assert.equal(settled, true); assert.equal(late.signal.aborted, true); assert.equal(timers.size, 0);
  late.onIdentity({ id, sha256 }); late.onProgress({ phase: "uploading" });
  assert.equal(ctx.controller.get().phase, "choose"); assert.equal(ctx.controller.get().upload, null);
  assert.deepEqual(ctx.calls, ["upload", "discardUpload"]);
});

test("an uncooperative upload has its original two-minute limit and preserves cleanup identity", async () => {
  let signal, settled = false;
  const ctx = setup({ upload(_file, options) { signal = options.signal; options.onIdentity({ id, sha256 }); return new Promise(() => {}); } }, null);
  void ctx.controller.choose({ name: "source.json" }).then(() => { settled = true; }); await flush();
  await step(); assert.equal(time, 120000); assert.equal(settled, true); assert.equal(signal.aborted, true);
  assert.equal(ctx.controller.get().busy, false); assert.equal(ctx.controller.get().error.code, "backup_upload_timeout");
  assert.equal(ctx.controller.get().upload.id, id); await ctx.controller.choose({});
  assert.equal(ctx.calls.filter(name => name === "upload").length, 1);
  await ctx.controller.cancel(); assert.equal(ctx.controller.get().phase, "choose");
});

test("the restore observation deadline includes retries and response parsing", async () => {
  let reads = 0, settled = false;
  const ctx = setup({ read() { if (++reads === 1) return { ...review, state: "pending", accepted: true };
    return new Promise(() => {}); } }, review, { observationMs: 12000 });
  void ctx.controller.inspect().then(() => { settled = true; }); await flush();
  for (let i = 0; i < 7 && !settled; i++) await step();
  assert.equal(settled, true); assert.equal(time, 12000); assert.equal(reads, 3);
  assert.equal(ctx.controller.get().phase, "unknown"); assert.equal(ctx.saved().id, id);
  assert.equal(ctx.controller.get().error.code, "restore_observation_timeout");
});

test("transient 503 discovery recovers before publishing an error and never writes", async () => {
  let reads = 0, settled = false;
  const ctx = setup({ current() { if (++reads === 1) throw new ApiError("internal upstream", { status: 503 }); return { empty: true }; } }, null);
  void ctx.controller.inspect().then(() => { settled = true; }); await flush();
  assert.equal(ctx.failures.length, 0); assert.equal(settled, false); await step();
  assert.equal(settled, true); assert.deepEqual(ctx.calls, ["current", "current", "previews"]);
  assert.equal(ctx.controller.get().phase, "choose"); assert.equal(ctx.failures.length, 0);
});

test("cancellation before the request microtask starts sends no ghost read", async () => {
  let settled = false;
  const ctx = setup({ read() { return new Promise(() => {}); } });
  void ctx.controller.inspect().then(() => { settled = true; }); ctx.controller.destroy(); await flush();
  assert.equal(settled, true); assert.deepEqual(ctx.calls, []); assert.equal(timers.size, 0);
});
