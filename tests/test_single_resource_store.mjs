import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";

class Node extends EventTarget {
  constructor(tag) { super(); this.tagName = tag.toUpperCase(); this.children = []; this.attributes = {}; this.value = ""; this.isConnected = true; }
  setAttribute(k, v) { this.attributes[k] = v; if (["name", "value", "type"].includes(k)) this[k] = v; }
  append(...nodes) { for (const n of nodes) { this.children.push(n); if (typeof n === "object") n.parent = this; } }
  replaceChildren(...nodes) { this.children = []; this.append(...nodes); }
  querySelectorAll(selector) { return descendants(this).slice(1).filter(n => n.tagName === selector.toUpperCase()); }
  get elements() { return descendants(this).filter(n => ["INPUT", "TEXTAREA", "BUTTON", "SELECT"].includes(n.tagName)); }
  showModal() { this.open = true; } close() { this.open = false; }
  remove() { this.isConnected = false; if (this.parent) this.parent.children = this.parent.children.filter(n => n !== this); }
}
const descendants = n => [n, ...n.children.flatMap(c => typeof c === "object" ? descendants(c) : [])];
const flush = () => new Promise(r => setImmediate(r));
const deferred = () => { let resolve; const promise = new Promise(r => { resolve = r; }); return { promise, resolve }; };

test("single-resource sharing retains drafts on connection notifications and cancels stale device work", async t => {
  const original = { document: globalThis.document, window: globalThis.window, confirm: globalThis.confirm };
  const body = new Node("body"), listeners = new Set(); let selected = null;
  globalThis.document = { body, createElement: tag => new Node(tag), querySelector: () => null };
  globalThis.window = new EventTarget(); globalThis.confirm = () => true;
  const resource = { kind: "skills", id: "test", content: "---\nname: test\ndescription: Fixture\n---\nRead files." };
  const calls = []; let exportGate = null, submitGate = null;
  globalThis.__singleStoreTest = {
    api: { post: async (path, value, options) => {
      calls.push({ path, value, options });
      if (value.action === "export") { if (exportGate) await exportGate.promise; return { data: { resources: [resource] } }; }
      if (value.action === "submit") { if (submitGate) await submitGate.promise; return { data: { state: "pending" } }; }
      throw Error("Unexpected store action");
    } },
    targetState: () => ({ selected }), subscribeTarget: fn => { listeners.add(fn); fn(); return () => listeners.delete(fn); },
  };
  // Replace only transport/device imports; run the real form, portable format
  // validator, DOM helper and asynchronous store continuations.
  const url = new URL("../app/web/js/features/settings/single-resource-store.js", import.meta.url);
  const source = readFileSync(url, "utf8")
    .replace('import { api } from "../../api/client.js";', "const { api } = globalThis.__singleStoreTest;")
    .replace('import { targetState, subscribeTarget } from "../../api/target.js";', "const { targetState, subscribeTarget } = globalThis.__singleStoreTest;")
    .replace(/from "(\.[^"]+)"/g, (_, path) => `from ${JSON.stringify(new URL(path, url).href)}`);
  const { createSingleResourceStore } = await import("data:text/javascript;base64," + Buffer.from(source).toString("base64"));
  const store = createSingleResourceStore("skills", () => {});
  t.after(() => { store.destroy(); Object.assign(globalThis, original); delete globalThis.__singleStoreTest; });
  const notify = () => { for (const fn of listeners) fn(); };
  await store.publish({ id: "test", description: "Fixture", project: "default" });
  const dialog = body.children[0]; assert.equal(dialog.open, true);
  const form = descendants(dialog).find(n => n.tagName === "FORM");
  form.dispatchEvent(new Event("input")); notify();
  assert.equal(dialog.open, true, "same-device reconnect must not discard a publication form");
  assert.deepEqual(calls[0].value.references, [{ kind: "skills", id: "test", project: "default" }]);
  const field = name => form.elements.find(n => n.name === name);
  field("readme").value = "Use the complete Skill source.";
  field("review_notes").value = "Source-only read-only fixture, no network.";
  form.elements.find(n => n.type === "checkbox" && n.value === "windows-x86_64").checked = true;
  submitGate = deferred(); form.dispatchEvent(new Event("submit", { cancelable: true })); await flush();
  const submit = calls.find(c => c.value.action === "submit");
  assert.equal(submit.value.single_kind, "skills"); assert.equal(submit.value.package.resources.length, 1);
  notify(); assert.equal(submit.options.signal.aborted, false);
  selected = { id: "next-device" }; notify(); assert.equal(submit.options.signal.aborted, true);
  submitGate.resolve(); await flush(); assert.equal(body.children.length, 0);
  assert.equal(store.hasPendingChanges(), false, "late submission must not reopen a form on the new device");
  exportGate = deferred(); const pending = store.publish({ id: "test" }); await flush();
  const read = calls.at(-1); selected = null; notify(); assert.equal(read.options.signal.aborted, true);
  exportGate.resolve(); await pending; assert.equal(body.children.length, 0, "late export must not reveal the previous device's source");
});
