import assert from "node:assert/strict";
import test from "node:test";
import { createDirectoryPicker, directoryChild } from "../app/web/js/features/sessions/directory-picker.js";

function setup() {
  const previous = globalThis.document;
  let focused = "";
  const node = (name) => Object.assign(new EventTarget(), { value: "", children: [],
    classList: { toggle() {} }, setAttribute() {},
    replaceChildren(...children) { this.children = children; },
    append(child) { this.children.push(child); },
    querySelector() { return this.children.find((child) => child.tag === "button"); },
    focus() { focused = name; } });
  globalThis.document = { createElement(tag) { return Object.assign(node(tag), { tag }); } };
  const names = ["form", "path", "list", "status", "parent", "choose", "shortcuts", "go", "close", "cancel"];
  const nodes = Object.fromEntries(names.map((name) => [name, node(name)]));
  const dialog = node("dialog"); dialog.querySelector = (id) => nodes[id.slice(11)];
  dialog.showModal = () => { dialog.open = true; };
  const closed = [];
  dialog.close = () => { dialog.open = false; closed.push(() => dialog.dispatchEvent(new Event("close"))); };
  const calls = [], selected = [];
  const view = createDirectoryPicker({ dialog, read(path, signal) {
    return new Promise((resolve, reject) => calls.push({ path, signal, resolve, reject }));
  } });
  return { view, dialog, nodes, calls, selected, focus: () => focused,
    open: (path) => view.open(path, (path) => selected.push(path)),
    click(node) { node.dispatchEvent(new Event("click")); },
    submit() { nodes.form.dispatchEvent(new Event("submit", { cancelable: true })); },
    closeEvents() { while (closed.length) closed.shift()(); },
    finish() { view.destroy(); globalThis.document = previous; } };
}
const data = (path, directories = ["nested", "two words", ".hidden"]) => ({ path,
  separator: "/", parent: "/", directories, shortcuts: [{kind:"home",path:"/user"}], truncated:false });
const tick = async () => { await Promise.resolve(); await Promise.resolve(); };

test("directory paths retain native roots, UNC prefixes and literal POSIX backslashes", () => {
  assert.equal(directoryChild(data("/"), "a"), "/a");
  assert.equal(directoryChild(data("/has\\"), "a"), "/has\\/a");
  assert.equal(directoryChild({path:"D:\\",separator:"\\"}, "two words"), "D:\\two words");
  assert.equal(directoryChild({path:"\\\\server\\share",separator:"\\"}, "a"), "\\\\server\\share\\a");
});

test("selection is unavailable until the requested directory loads; typed paths need confirmation", async () => {
  const ctx = setup();
  try {
    const open = ctx.open("/work"); assert.equal(ctx.nodes.choose.disabled, true);
    ctx.click(ctx.nodes.choose); assert.deepEqual(ctx.selected, []);
    ctx.calls[0].resolve(data("/work")); await open;
    assert.equal(ctx.nodes.choose.disabled, false);
    ctx.nodes.path.value = "/different"; ctx.nodes.path.dispatchEvent(new Event("input"));
    assert.equal(ctx.nodes.choose.disabled, true);
    ctx.click(ctx.nodes.choose); assert.deepEqual(ctx.selected, []);
    ctx.submit(); assert.equal(ctx.calls[1].path, "/different");
    ctx.calls[1].resolve(data("/different", [])); await tick();
    assert.equal(ctx.focus(), "choose"); ctx.click(ctx.nodes.choose);
    assert.deepEqual(ctx.selected, ["/different"]); assert.equal(ctx.dialog.open, false);
  } finally { ctx.finish(); }
});

test("a late response or queued close cannot replace a newly opened directory", async () => {
  const ctx = setup();
  try {
    const first = ctx.open("/first");
    ctx.dialog.close(); const second = ctx.open("/second"); ctx.closeEvents();
    assert.equal(ctx.calls[0].signal.aborted, true);
    ctx.calls[1].resolve(data("/second")); await second;
    ctx.calls[0].resolve(data("/first")); await first;
    assert.equal(ctx.nodes.path.value, "/second");
    ctx.click(ctx.nodes.list.children.find((button) => button.children[1].textContent === "two words"));
    assert.equal(ctx.calls[2].path, "/second/two words");
    ctx.calls[2].reject(new Error("no access")); await tick();
    assert.equal(ctx.nodes.choose.disabled, true);
    assert.equal(ctx.nodes.status.textContent, "no access");
    assert.equal(ctx.focus(), "path");
    ctx.click(ctx.nodes.cancel); ctx.closeEvents();
    assert.equal(ctx.calls[2].signal.aborted, true);
  } finally { ctx.finish(); }
});

test("parent and shortcuts navigate without selecting; IME Enter does not submit a partial path", async () => {
  const ctx = setup();
  try {
    const open = ctx.open("/work"); ctx.calls[0].resolve(data("/work")); await open;
    const key = new Event("keydown", {cancelable:true});
    Object.assign(key, {key:"Enter",keyCode:229}); ctx.nodes.path.dispatchEvent(key);
    assert.equal(key.defaultPrevented, true); assert.equal(ctx.calls.length, 1);
    ctx.click(ctx.nodes.parent); assert.equal(ctx.calls[1].path, "/");
    ctx.calls[1].resolve({...data("/"),parent:""}); await tick();
    assert.equal(ctx.nodes.parent.disabled, true);
    ctx.click(ctx.nodes.shortcuts.children[0]); assert.equal(ctx.calls[2].path, "/user");
    ctx.calls[2].resolve({...data("/user"),truncated:true}); await tick();
    assert.match(ctx.nodes.status.textContent, /完整路径/); assert.deepEqual(ctx.selected, []);
  } finally { ctx.finish(); }
});
