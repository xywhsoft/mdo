import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { testDocument, TestElement } from "./helpers/extension-dom.mjs";

test("extension center keeps cached results on failed refresh and discards details only on a real device change", async t => {
  const previous = { document: globalThis.document, window: globalThis.window };
  const root = new TestElement("div"), listeners = new Set(); let selected = null, fail = false, release;
  globalThis.document = { ...testDocument(), querySelector: selector => selector === "[data-extension-store]" ? root : null };
  globalThis.window = new EventTarget();
  const item = { id: 1, slug: "fixture", name: "Fixture plugin", version: "1.0.0", author: "Fixture", description: "Description", kinds: "plugins" };
  const gate = new Promise(r => { release = r; });
  globalThis.__storeBrowserTest = {
    targetState: () => ({ selected }), subscribeTarget: fn => { listeners.add(fn); return () => listeners.delete(fn); },
    api: { get: async () => ({ data: {} }), post: async (_, body) => {
      if (body.action === "catalog") { if (fail) throw Error("Catalog unavailable"); return { data: { items: [item] } }; }
      if (body.action === "detail") { await gate; return { data: { ...item, state: "published", package: { manifest: { license: "MIT", readme: "Source", platforms: ["windows-x86_64"] }, resources: [{ kind: "plugins", id: "fixture", content: "/* fixture */" }] } } }; }
      throw Error(`Unexpected ${body.action}`);
    } },
  };
  const url = new URL("../app/web/js/features/settings/store-panel.js", import.meta.url);
  const source = readFileSync(url, "utf8").replace('import { api } from "../../api/client.js";', "const { api } = globalThis.__storeBrowserTest;")
    .replace('import { targetState, subscribeTarget } from "../../api/target.js";', "const { targetState, subscribeTarget } = globalThis.__storeBrowserTest;")
    .replace(/from "(\.[^"]+)"/g, (_, path) => `from ${JSON.stringify(new URL(path, url).href)}`);
  const { createStorePanel } = await import("data:text/javascript;base64," + Buffer.from(source).toString("base64"));
  const panel = createStorePanel(), flush = () => new Promise(r => setImmediate(r));
  t.after(() => { panel.destroy(); Object.assign(globalThis, previous); delete globalThis.__storeBrowserTest; });
  panel.selectSection("store"); await flush(); assert.equal(root.querySelectorAll(".eb-row").length, 1);
  fail = true; root.querySelectorAll("button").find(b => b.textContent === "刷新").click(); await flush();
  assert.equal(root.querySelectorAll(".eb-row").length, 1, "transient refresh errors retain the loaded list");
  assert(root.textContent.includes("Catalog unavailable"));
  root.querySelector(".eb-open").click();
  const pane = document.body.querySelector(".eb-detail"); assert.equal(pane.hidden, false);
  for (const notify of listeners) notify(); assert.equal(pane.hidden, false, "connection status updates keep the current details");
  selected = { id: "another-device" }; for (const notify of listeners) notify(); release(); await flush();
  assert.equal(root.querySelectorAll(".eb-row").length, 0); assert.equal(pane.hidden, true, "late source must not cross device boundaries");
});
