import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";

import { TestElement as Node, descendants, testDocument } from "./helpers/extension-dom.mjs";
const flush = () => new Promise(r => setImmediate(r));
const deferred = () => { let resolve; const promise = new Promise(r => { resolve = r; }); return { promise, resolve }; };

test("single-resource sharing retains drafts on connection notifications and cancels stale device work", async t => {
  const original = { document: globalThis.document, window: globalThis.window, confirm: globalThis.confirm };
  const body = new Node("body"), listeners = new Set(); let selected = null;
  globalThis.document = { ...testDocument(), body };
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

test("store details reject late selections and require C trust plus the exact installation plan", async t => {
  const original = { document: globalThis.document, window: globalThis.window, confirm: globalThis.confirm };
  globalThis.document = testDocument(); globalThis.window = new EventTarget(); globalThis.confirm = () => true;
  const first = deferred(), calls = [], items = [1, 2].map(id => ({ id, owner: 7, slug: `tool-${id}`, name: `Tool ${id}`, author: "Fixture", version: "1.0.0", description: "C source" }));
  let installed = {}, managed = null;
  const pack = id => ({ name: `Tool ${id}`, state: "published", author: "Fixture", version: "1.0.0", package: { manifest: { license: "MIT", platforms: ["windows-x86_64"], readme: "Read the source." }, resources: [{ kind: "tools", id: `tool-${id}`, content: "/* source */" }] } });
  globalThis.__singleStoreDetails = {
    api: { get: async () => ({ data: installed }), post: async (_, body) => {
      calls.push(body);
      if (body.action === "catalog") return { data: { items } };
      if (body.action === "detail") { if (body.id === 1) await first.promise; return { data: pack(body.id) }; }
      if (body.action === "plan") return { data: { sha256: "exact-plan-hash", files: [{ operation: "create", path: "tools/tool-2.c", bytes: 12 }] } };
      if (body.action === "install") { installed = { two: { ...items[1], source: "store", resources: [{ kind: "tools", id: "tool-2" }] } }; return { data: {} }; }
      throw Error(`Unexpected ${body.action}`);
    } }, targetState: () => ({ selected: null }), subscribeTarget: () => () => {},
  };
  const url = new URL("../app/web/js/features/settings/single-resource-store.js", import.meta.url);
  const source = readFileSync(url, "utf8").replace('import { api } from "../../api/client.js";', "const { api } = globalThis.__singleStoreDetails;")
    .replace('import { targetState, subscribeTarget } from "../../api/target.js";', "const { targetState, subscribeTarget } = globalThis.__singleStoreDetails;")
    .replace(/from "(\.[^"]+)"/g, (_, path) => `from ${JSON.stringify(new URL(path, url).href)}`);
  const { createSingleResourceStore } = await import("data:text/javascript;base64," + Buffer.from(source).toString("base64"));
  const store = createSingleResourceStore("tools", () => {}, () => {}, receipt => { managed = receipt; });
  t.after(() => { store.destroy(); Object.assign(globalThis, original); delete globalThis.__singleStoreDetails; });
  store.activate(true); await flush(); await flush();
  const rows = store.root.querySelectorAll(".eb-open"); assert.equal(rows.length, 2);
  rows[0].click(); rows[1].click(); await flush(); await flush();
  const pane = store.root.querySelector(".eb-detail");
  assert.equal(pane.querySelector("h3").textContent, "Tool 2");
  const install = pane.querySelectorAll("button").find(b => b.textContent === "下载安装");
  assert(install.disabled); install.click(); assert(!calls.some(c => c.action === "install"));
  first.resolve(); await flush(); assert.equal(pane.querySelector("h3").textContent, "Tool 2");
  const check = pane.querySelector("input"); check.checked = true; check.dispatchEvent(new Event("change"));
  assert.equal(install.disabled, false); install.click(); await flush();
  const request = calls.find(c => c.action === "install"); assert.equal(request.id, 2); assert.equal(request.single_kind, "tools");
  assert.equal(request.trust_code, true); assert.equal(request.expected_plan, "exact-plan-hash");
  await flush(); await flush();
  store.root.querySelectorAll("button").find(b => b.textContent === "管理").click();
  assert.equal(managed.id, 2, "installed management opens the actual local receipt");
});
