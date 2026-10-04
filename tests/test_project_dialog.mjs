import assert from "node:assert/strict";
import test from "node:test";
import { createProjectDialog } from "../app/web/js/features/sessions/project-dialog.js";

function setup(overrides = {}) {
  const previous = { document: globalThis.document, FormData: globalThis.FormData };
  let focused = "";
  const node = (label) => Object.assign(new EventTarget(), { value: "", hidden: false,
    children: [], focus() { focused = label; }, setAttribute() {},
    append(child) { this.children.push(child); },
    replaceChildren(...children) { this.children = children; } });
  globalThis.document = { createElement: () => node("option") };
  const fields = Object.fromEntries(["workspace_root", "name", "id", "default_model_id"]
    .map((key) => [key, node(key)]));
  const form = node("form"); form.elements = fields;
  form.reportValidity = () => true;
  form.reset = () => { for (const field of Object.values(fields)) field.value = ""; };
  globalThis.FormData = class {
    constructor() { return Object.entries(fields).filter(([, field]) => !field.disabled)
      .map(([key, field]) => [key, field.value]); }
  };
  const dialog = node("dialog");
  dialog.querySelector = () => node("heading");
  dialog.showModal = () => { dialog.open = true; };
  const closed = [];
  dialog.close = () => { dialog.open = false; closed.push(() => dialog.dispatchEvent(new Event("close"))); };
  const calls = [], reads = [], created = [], updated = [], notices = [];
  const models = [{ id: "ornith-1.5-35b", name: "Ornith", aliases: ["ling-3.0-tiny"] }];
  const modelsStore = { get: () => ({ data: { models } }), subscribe() {} };
  const submit = node("submit"), error = node("error"), status = node("status"), browse = node("browse");
  let picked;
  const view = createProjectDialog({ dialog, form, submit, error, status, browse, modelsStore,
    directoryPicker: { close() {}, open(path, callback) { picked = { path, callback }; } },
    read(id) { return new Promise((resolve, reject) => reads.push({ id, resolve, reject })); },
    create(values) { return new Promise((resolve, reject) => calls.push({ kind: "create", values, resolve, reject })); },
    update(id, values, etag) { return new Promise((resolve, reject) => calls.push({ kind: "update", id, values, etag, resolve, reject })); },
    onCreated: (value) => created.push(value), onUpdated: (value) => updated.push(value),
    notify: (...args) => notices.push(args), ...overrides });
  return { view, fields, form, dialog, reads, calls, submit, error, status, browse, models,
    created, updated, notices, picked: () => picked, focused: () => focused,
    submitForm() { form.dispatchEvent(new Event("submit", { cancelable: true })); },
    closeEvents() { while (closed.length) closed.shift()(); },
    finish() { Object.assign(globalThis, previous); } };
}
const tick = async () => { await Promise.resolve(); await Promise.resolve(); };
const definition = (id) => ({ id, name: id, workspace_root: `/work/${id}`,
  default_model_id: "ling-3.0-tiny", etag: `"${id}-1"` });

test("saving from the sidebar restores the refreshed project menu button", async () => {
  const ctx = setup();
  let restored = false;
  const origin = { isConnected: true, dataset: { sidebarFocus: "menu:project:a" } };
  const replacement = { dataset: origin.dataset, getClientRects: () => [{}],
    focus() { restored = true; } };
  globalThis.document.querySelectorAll = () => [replacement];
  try {
    const open = ctx.view.open({ id: "a" }, origin);
    ctx.reads[0].resolve(definition("a")); await open;
    ctx.submitForm();
    origin.isConnected = false; // The catalog refresh replaced the button.
    ctx.calls[0].resolve({ id: "a", name: "Renamed" }); await tick();
    ctx.closeEvents();
    assert.equal(restored, true);
    assert.equal(ctx.updated.length, 1);
  } finally { ctx.finish(); }
});

test("a dropped folder is checked on the host and stale checks cannot fill a newer dialog", async () => {
  const zone = Object.assign(new EventTarget(), { classList: { add() {}, remove() {} }, contains() { return false; } });
  const checks = [];
  const ctx = setup({ folderUI: { zone }, resolveDrop: async () => "D:\\dropped",
    readDirectory(path, signal) { return new Promise((resolve) => checks.push({path,signal,resolve})); } });
  const drop = () => zone.dispatchEvent(Object.assign(new Event("drop", {cancelable:true}), {dataTransfer:{}}));
  try {
    await ctx.view.open();
    drop(); await tick();
    assert.equal(checks[0].path, "D:\\dropped");
    assert.equal(ctx.submit.disabled, true);
    ctx.dialog.close(); ctx.closeEvents(); await ctx.view.open();
    assert.equal(checks[0].signal.aborted, true);
    checks[0].resolve({path:"D:\\stale"}); await tick();
    assert.equal(ctx.fields.workspace_root.value, "");
    drop(); await tick(); checks[1].resolve({path:"D:\\real folder"}); await tick();
    assert.equal(ctx.fields.workspace_root.value, "D:\\real folder");
    assert.equal(ctx.fields.name.value, "real folder");
    assert.equal(ctx.submit.disabled, false);
  } finally { ctx.finish(); }
});

test("late project reads and queued close events cannot overwrite a newly opened form", async () => {
  const ctx = setup();
  try {
    const first = ctx.view.open({ id: "first" });
    assert.equal(ctx.submit.disabled, true);
    ctx.dialog.close(); await ctx.view.open(); ctx.closeEvents();
    ctx.fields.workspace_root.value = "/work/new";
    ctx.reads[0].resolve(definition("first")); await first;
    assert.equal(ctx.dialog.open, true);
    assert.equal(ctx.fields.workspace_root.value, "/work/new");
    assert.equal(ctx.fields.id.readOnly, false);
    const a = ctx.view.open({ id: "a" }), b = ctx.view.open({ id: "b" });
    ctx.reads[2].resolve(definition("b")); await b;
    ctx.reads[1].reject(new Error("old failure")); await a;
    assert.equal(ctx.fields.id.value, "b"); assert.equal(ctx.error.hidden, true);
    assert.equal(ctx.focused(), "name");
    assert.equal(ctx.fields.default_model_id.value, "ornith-1.5-35b");
  } finally { ctx.finish(); }
});

test("an in-flight update keeps its owner and never closes or navigates a newer dialog", async () => {
  const ctx = setup();
  try {
    const open = ctx.view.open({ id: "a" }); ctx.reads[0].resolve(definition("a")); await open;
    ctx.fields.name.value = "Renamed"; ctx.submitForm(); ctx.submitForm();
    assert.equal(ctx.calls.length, 1); assert.equal(ctx.fields.name.disabled, true);
    assert.deepEqual(ctx.calls[0].values, { name: "Renamed", workspace_root: "/work/a", default_model_id: "ornith-1.5-35b" });
    assert.equal(ctx.calls[0].id, "a"); assert.equal(ctx.calls[0].etag, '"a-1"');
    ctx.dialog.close(); await ctx.view.open(); ctx.closeEvents();
    ctx.fields.name.value = "New project";
    ctx.fields.name.dispatchEvent(new Event("input"));
    ctx.fields.workspace_root.value = "/work/new";
    ctx.fields.workspace_root.dispatchEvent(new Event("input"));
    ctx.submitForm();
    assert.equal(ctx.calls.length, 1); assert.equal(ctx.submit.disabled, true);
    ctx.calls[0].resolve({ name: "Renamed", id: "a" }); await tick();
    assert.equal(ctx.dialog.open, true); assert.equal(ctx.fields.name.value, "New project");
    assert.equal(ctx.created.length, 0); assert.equal(ctx.updated.length, 0);
    assert.match(ctx.notices[0][0], /更新.*Renamed/); assert.equal(ctx.submit.disabled, false);
    ctx.submitForm(); ctx.calls[1].resolve({ name: "New project", id: "new" }); await tick();
    assert.equal(ctx.dialog.open, false); assert.equal(ctx.created.length, 1);
  } finally { ctx.finish(); }
});

test("unread definitions cannot be saved and background errors do not replace current form errors", async () => {
  const ctx = setup();
  try {
    const open = ctx.view.open({ id: "broken" }); ctx.reads[0].reject(new Error("read failed")); await open;
    ctx.submitForm(); assert.equal(ctx.calls.length, 0); assert.equal(ctx.submit.disabled, true);
    await ctx.view.open(); ctx.submitForm();
    ctx.dialog.close(); await ctx.view.open(); ctx.closeEvents();
    ctx.calls[0].reject(new Error("save failed")); await tick();
    assert.equal(ctx.error.hidden, true); assert.equal(ctx.dialog.open, true);
    assert.equal(ctx.notices[0][1], "error");
  } finally { ctx.finish(); }
});

test("picked directories suggest identities but preserve manually edited names and IDs", async () => {
  const ctx = setup();
  try {
    await ctx.view.open(); ctx.browse.dispatchEvent(new Event("click"));
    ctx.picked().callback("/work/hello-world");
    assert.equal(ctx.fields.name.value, "hello-world"); assert.equal(ctx.fields.id.value, "hello-world");
    ctx.fields.name.value = "Manual"; ctx.fields.name.dispatchEvent(new Event("input"));
    ctx.fields.id.value = "manual-id"; ctx.fields.id.dispatchEvent(new Event("input"));
    ctx.picked().callback("/work/other");
    assert.equal(ctx.fields.name.value, "Manual"); assert.equal(ctx.fields.id.value, "manual-id");
    const old = ctx.picked(); ctx.dialog.close(); await ctx.view.open();
    old.callback("/work/stale"); assert.equal(ctx.fields.workspace_root.value, "");
    // A user-defined model with a legacy ID takes priority over the built-in alias.
    ctx.models.push({ id: "ling-3.0-tiny", name: "Custom" });
    const open = ctx.view.open({ id: "a" }); ctx.reads[0].resolve(definition("a")); await open;
    assert.equal(ctx.fields.default_model_id.value, "ling-3.0-tiny");
  } finally { ctx.finish(); }
});
