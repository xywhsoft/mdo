import assert from "node:assert/strict";
import test from "node:test";
import { createSessionBackupImport } from "../app/web/js/features/sessions/session-backup-import.js";
import { ApiError } from "../app/web/js/api/client.js";

class Node extends EventTarget {
  children = []; value = ""; disabled = false; isConnected = true;
  setAttribute() {} removeAttribute() {}
  replaceChildren(...children) { this.children = children; }
  append(...children) { this.children.push(...children); }
  get childElementCount() { return this.children.length; }
}
function setup(initial = {}) {
  const nodes = {}, methods = [], delayedClose = [];
  let focused, publish, state = { phase: "choose", busy: false, ...initial };
  for (const key of ["title", "scope", "status", "error", "progress", "file", "file-field", "file-label",
    "fields", "facts", "project", "project-label", "target", "close", "primary", "refresh", "cancel", "next", "body"])
    { nodes[key] = new Node(); nodes[key].focus = () => { focused = key; }; }
  const opener = { isConnected: true, focus() { focused = "opener"; } };
  const prior = globalThis.document;
  globalThis.document = { activeElement: opener, createElement: () => new Node() };
  const dialog = new Node(); dialog.querySelector = (selector) => selector === ".backup-import-body" ? nodes.body : nodes[selector.slice(13, -1)];
  dialog.showModal = () => { dialog.open = true; };
  dialog.close = () => { dialog.open = false; delayedClose.push(() => dialog.dispatchEvent(new Event("close"))); };
  const controller = { get: () => state, subscribe(listener) { publish = listener; listener(state); return () => {}; },
    hasRecovery: () => state.phase === "unknown", ...Object.fromEntries(["inspect", "review", "confirm", "cancel", "choose", "refresh", "acknowledge", "destroy"].map((method) =>
      [method, (...args) => { methods.push({ method, args }); }])) };
  const projectsStore = { get: () => ({ data: { items: [{ id: "target", name: "Target" }] } }),
    subscribe(listener) { listener(); return () => {}; } };
  const opened = [];
  const view = createSessionBackupImport({ dialog, projectsStore, preferredProject: () => "target", controller,
    async openSession(value) { opened.push(value); } });
  return { nodes, methods, view, dialog, opened, focus: () => focused,
    state(patch) { state = { ...state, ...patch }; publish(state); },
    closeEvents() { while (delayedClose.length) delayedClose.shift()(); },
    finish() { view.destroy(); globalThis.document = prior; } };
}
const result = { id: "a".repeat(32), session_id: "a".repeat(32), project_id: "target", terminal: false };

test("Esc hides the import, restores focus and keeps accepted work; obsolete close does not hide reopen", async () => {
  const ctx = setup({ phase: "restoring", restore: result, attempted: true });
  try {
    ctx.view.open(); ctx.dialog.dispatchEvent(new Event("cancel", { cancelable: true }));
    assert.equal(ctx.dialog.open, false); assert.equal(ctx.focus(), "opener");
    assert.equal(ctx.methods.length, 0);
    ctx.view.open(); ctx.closeEvents(); assert.equal(ctx.dialog.open, true);
    ctx.nodes.cancel.dispatchEvent(new Event("click"));
    assert.deepEqual(ctx.methods.map((call) => call.method), ["cancel"]);
  } finally { ctx.finish(); }
});

test("committed results wait for an explicit open; uncertain cleanup blocks both open and reset", async () => {
  const ctx = setup({ phase: "review", restore: result });
  try {
    ctx.view.open(); ctx.state({ phase: "committed", restore: { ...result, committed: true, terminal: true } });
    assert.equal(ctx.opened.length, 0); assert.equal(ctx.nodes.primary.disabled, false);
    ctx.nodes.primary.dispatchEvent(new Event("click")); await Promise.resolve(); await Promise.resolve();
    assert.equal(ctx.opened.length, 1); assert.equal(ctx.dialog.open, false);
    ctx.state({ restore: { ...result, committed: true, terminal: false, cleanup_pending: true, restart_required: true } });
    ctx.view.open(); assert.equal(ctx.nodes.primary.disabled, true); assert.equal(ctx.nodes.next.hidden, true);
    ctx.nodes.primary.dispatchEvent(new Event("click")); assert.equal(ctx.opened.length, 1);
  } finally { ctx.finish(); }
});

test("unknown recovery queries only, failure receives focus and partial preview cannot confirm", async () => {
  const ctx = setup({ phase: "unknown", restore: result });
  try {
    ctx.view.resume(); assert.deepEqual(ctx.methods.map((call) => call.method), ["inspect"]);
    assert.equal(ctx.nodes.facts.children.some((node) => node.textContent === "backupImport.bytes"), false);
    ctx.state({ error: new ApiError("lost", { code: "restore_observation_timeout" }) });
    assert.equal(ctx.focus(), "error"); assert.equal(ctx.nodes.next.hidden, true);
    assert.equal(ctx.nodes.primary.hidden, true); assert.equal(ctx.nodes.refresh.hidden, false);
    ctx.state({ error: null, restore: null, phase: "preview", preview: { result: { legacy_partial: true } } });
    assert.equal(ctx.nodes.primary.disabled, true);
    ctx.nodes.primary.dispatchEvent(new Event("click"));
    assert.equal(ctx.methods.length, 1);
  } finally { ctx.finish(); }
});

test("server microsecond timestamps render a real date and the reviewed target precedes source details", () => {
  const ctx = setup({ phase: "review", restore: { ...result, workspace_root: "/actual/destination" },
    preview: { result: { title: "Source", captured_at: 1720000000000000 } } });
  try {
    assert.equal(ctx.nodes.facts.children[0].textContent, "backupImport.targetProject");
    assert.equal(ctx.nodes.facts.children[3].textContent, "/actual/destination");
    assert.ok(ctx.nodes.facts.children.some((node) => /2024/.test(node.textContent || "")));
    assert.equal(ctx.nodes.scope.hidden, true);
  } finally { ctx.finish(); }
});
