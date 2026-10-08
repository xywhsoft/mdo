import assert from "node:assert/strict";
import test from "node:test";
import { createSessionBackupExport, saveBackupFile } from "../app/web/js/features/sessions/session-backup-export.js";
import { ANDROID_EXPORT_MAX_BYTES } from "../app/web/js/utils/file-download.js";
import { ApiError } from "../app/web/js/api/client.js";

function setup({ save } = {}) {
  const nodes = {}, closed = [], calls = [], saved = [], notices = [];
  let focused = "";
  for (const name of ["title", "scope", "status", "error", "cancel", "close", "retry", "progress"]) {
    const node = nodes[name] = new EventTarget();
    node.setAttribute = () => {}; node.removeAttribute = () => {};
    node.focus = () => { focused = name; };
  }
  const dialog = new EventTarget();
  dialog.setAttribute = () => {};
  dialog.querySelector = (selector) => nodes[selector === "progress" ? "progress" : selector.slice(13, -1)];
  dialog.showModal = () => { dialog.open = true; };
  dialog.close = () => { dialog.open = false; closed.push(() => dialog.dispatchEvent(new Event("close"))); };
  const opener = { isConnected: true, focus() { focused = "opener"; } };
  const previous = globalThis.document;
  globalThis.document = { activeElement: opener };
  const view = createSessionBackupExport({ dialog, download(session, options) {
    return new Promise((resolve, reject) => calls.push({ session, options, resolve, reject }));
  }, save: save ?? ((file) => { saved.push(file); }), notify(message) { notices.push(message); } });
  return { view, dialog, nodes, calls, saved, notices, focus: () => focused,
    closeEvents() { while (closed.length) closed.shift()(); },
    finish() { view.destroy(); globalThis.document = previous; } };
}
const session = { project_id: "p", id: "a", title: "Original" };
const tick = async () => { await Promise.resolve(); await Promise.resolve(); };

test("an Android limit refusal preserves the dialog and session without a success notice", async () => {
  const previous = globalThis.XsExport;
  globalThis.XsExport = { save() { assert.fail("An oversized export must never enter the native bridge"); } };
  const ctx = setup({ save: saveBackupFile });
  try {
    ctx.view.open(session);
    ctx.calls[0].resolve({ filename: "large.json", blob: { size: ANDROID_EXPORT_MAX_BYTES + 1 } });
    await tick();
    assert.equal(ctx.dialog.open, true); assert.equal(ctx.nodes.error.hidden, false);
    assert.match(ctx.nodes.error.textContent, /8 MiB/); assert.match(ctx.nodes.error.textContent, /仍保留/);
    assert.equal(ctx.nodes.retry.hidden, false); assert.equal(ctx.notices.length, 0);
    assert.equal(ctx.focus(), "error"); assert.equal(ctx.calls.length, 1);
    assert.equal(ctx.calls[0].session.id, session.id);
  } finally { ctx.finish();
    if (previous === undefined) delete globalThis.XsExport; else globalThis.XsExport = previous;
  }
});

test("duplicate actions and caller mutations cannot switch an active export owner", async () => {
  const ctx = setup(), owner = { ...session };
  try {
    ctx.view.open(owner); owner.id = "changed";
    ctx.view.open({ ...session, id: "another" });
    assert.equal(ctx.calls.length, 1); assert.equal(ctx.calls[0].session.id, "a");
    ctx.calls[0].options.onProgress({ phase: "receiving", received: 1024, total: 2048 });
    assert.equal(ctx.nodes.progress.value, 1024); assert.equal(ctx.nodes.progress.max, 2048);
    ctx.calls[0].resolve({ filename: "verified.json" }); await tick();
    assert.equal(ctx.saved.length, 1); assert.equal(ctx.notices.length, 1);
    assert.equal(ctx.dialog.open, false); assert.equal(ctx.focus(), "opener");
  } finally { ctx.finish(); }
});

test("Esc/reopen ignores old native close events, progress and even a successful late response", async () => {
  const ctx = setup();
  try {
    ctx.view.open(session); ctx.dialog.dispatchEvent(new Event("cancel", { cancelable: true }));
    assert.equal(ctx.calls[0].options.signal.aborted, true);
    ctx.view.open({ ...session, id: "b" }); ctx.closeEvents();
    assert.equal(ctx.dialog.open, true); assert.equal(ctx.calls[1].options.signal.aborted, false);
    ctx.calls[0].options.onProgress({ phase: "receiving", received: 99, total: 100 });
    ctx.calls[0].resolve({ filename: "obsolete.json" }); await tick();
    assert.equal(ctx.saved.length, 0); assert.equal(ctx.notices.length, 0);
    ctx.calls[1].resolve({ filename: "current.json" }); await tick();
    assert.deepEqual(ctx.saved.map((file) => file.filename), ["current.json"]);
  } finally { ctx.finish(); }
});

test("a failure stays visible with keyboard focus and retries exactly the same session", async () => {
  const ctx = setup();
  try {
    ctx.view.open(session);
    ctx.calls[0].reject(new ApiError("bad", { code: "backup_download_checksum" })); await tick();
    assert.equal(ctx.focus(), "error"); assert.equal(ctx.nodes.retry.hidden, false);
    assert.match(ctx.nodes.error.textContent, /校验和/); assert.equal(ctx.saved.length, 0);
    ctx.nodes.retry.dispatchEvent(new Event("click"));
    assert.equal(ctx.calls[1].session.id, "a"); assert.equal(ctx.focus(), "cancel");
    ctx.calls[1].resolve({ filename: "retry.json" }); await tick();
    assert.equal(ctx.saved.length, 1);
  } finally { ctx.finish(); }
});

test("external close and destroy cancel work and suppress late saves", async () => {
  const ctx = setup();
  try {
    ctx.view.open(session); ctx.dialog.close(); ctx.closeEvents();
    assert.equal(ctx.calls[0].options.signal.aborted, true);
    ctx.calls[0].resolve({}); await tick(); assert.equal(ctx.saved.length, 0);
    ctx.view.open(session); ctx.view.destroy();
    ctx.calls[1].resolve({}); await tick(); assert.equal(ctx.saved.length, 0);
  } finally { ctx.finish(); }
});
